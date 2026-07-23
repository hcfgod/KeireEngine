#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Project/Project.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
    struct CommandLine
    {
        std::filesystem::path Request;
        std::filesystem::path Progress;
        std::filesystem::path Result;
        std::filesystem::path Cancel;
    };

    [[nodiscard]] CommandLine Parse(const int argc, char* const* argv)
    {
        CommandLine result;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view option = argv[index];
            const auto requireValue = [&]() -> std::filesystem::path
            {
                if (++index >= argc)
                    throw std::invalid_argument(std::string(option) + " requires a path.");
                return Keire::Detail::PathFromUtf8(argv[index]);
            };
            if (option == "--request")
                result.Request = requireValue();
            else if (option == "--progress")
                result.Progress = requireValue();
            else if (option == "--result")
                result.Result = requireValue();
            else if (option == "--cancel")
                result.Cancel = requireValue();
            else
                throw std::invalid_argument("Unknown asset-worker option: " + std::string(option));
        }
        if (result.Request.empty() || result.Progress.empty() || result.Result.empty() || result.Cancel.empty())
            throw std::invalid_argument("Asset worker requires request, progress, result, and cancel paths.");
        return result;
    }

    [[nodiscard]] bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        const auto relative =
            std::filesystem::weakly_canonical(path).lexically_relative(std::filesystem::weakly_canonical(root));
        return !relative.empty() && *relative.begin() != "..";
    }

    [[nodiscard]] Keire::Ref<Keire::AssetDatabase> CreateDatabase(const std::filesystem::path& projectRoot)
    {
        (void)Keire::Project::Open(projectRoot);
        Keire::AssetDatabaseSpecification specification{.ProjectRoot = projectRoot};
        specification.Importers = {Keire::CreateInputActionAssetImporter(), Keire::CreateSceneAssetImporter(),
                                   Keire::CreatePrefabAssetImporter(),      Keire::CreateManagedAssemblyAssetImporter(),
                                   Keire::CreateShaderAssetImporter(),      Keire::CreateMaterialAssetImporter(),
                                   Keire::CreateMeshAssetImporter(),        Keire::CreateTexture2DAssetImporter()};
        return Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
    }

    void RecoverAuxiliaryPublications(const std::filesystem::path& projectRoot)
    {
        const auto operations = projectRoot / "Library/AssetOperations";
        const auto assets = projectRoot / "Assets";
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(operations, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            const auto journal = iterator->path() / "create-auxiliary.journal";
            if (!std::filesystem::is_regular_file(journal))
                continue;
            std::istringstream stream(Keire::Detail::ReadTextFile(journal, 1024U * 1024U));
            std::string encoded;
            if (!std::getline(stream, encoded))
                throw std::runtime_error("Asset-worker auxiliary publication journal is empty.");
            const auto mainSource = assets / Keire::Detail::PathFromUtf8(encoded);
            const auto mainMetadata = Keire::Detail::PathWithSuffix(mainSource, ".keiremeta");
            const bool committed =
                std::filesystem::is_regular_file(mainSource) && std::filesystem::is_regular_file(mainMetadata);
            while (std::getline(stream, encoded))
            {
                const auto auxiliary = assets / Keire::Detail::PathFromUtf8(encoded);
                if (!IsWithin(assets, auxiliary))
                    throw std::runtime_error("Asset-worker auxiliary journal path escapes Assets.");
                if (!committed)
                    std::filesystem::remove(auxiliary);
            }
            std::filesystem::remove(journal);
        }
        if (error)
            throw std::runtime_error("Could not recover asset-worker auxiliary publications: " + error.message());
    }
} // namespace

namespace
{
    int RunWorker(const int argc, char* const* argv)
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            std::cout << "KeireAssetWorker --request <path> --progress <path> --result <path> --cancel <path>\n";
            return 0;
        }
        std::filesystem::path resultPath;
        Keire::Detail::AssetWorkerResult result;
        try
        {
            const auto commandLine = Parse(argc, argv);
            resultPath = commandLine.Result;
            const auto request = Keire::Detail::ReadAssetWorkerRequest(commandLine.Request);
            const auto operationRoot = request.ProjectRoot / "Library" / "AssetOperations" / request.OperationId;
            for (const auto& path : {commandLine.Request, commandLine.Progress, commandLine.Result, commandLine.Cancel})
                if (!IsWithin(operationRoot, path))
                    throw std::invalid_argument("Asset-worker protocol path escapes its operation directory.");
            if (request.Kind == Keire::Detail::AssetWorkerOperationKind::CreateAsset &&
                !IsWithin(operationRoot, request.CreatePayloadPath))
                throw std::invalid_argument("Asset-worker create payload escapes its operation directory.");
            if (request.Kind == Keire::Detail::AssetWorkerOperationKind::ExtractMaterials &&
                (!request.ExtractModel || request.ExtractDirectory.empty() || request.ExtractDirectory.is_absolute() ||
                 !IsWithin(request.ProjectRoot / "Assets", request.ProjectRoot / "Assets" / request.ExtractDirectory)))
                throw std::invalid_argument("Asset-worker material extraction escapes Assets.");
            for (const auto& auxiliary : request.CreateAuxiliarySources)
            {
                if (!IsWithin(operationRoot, auxiliary.PayloadPath))
                    throw std::invalid_argument("Asset-worker auxiliary payload escapes its operation directory.");
                if (auxiliary.RelativePath.empty() || auxiliary.RelativePath.is_absolute() ||
                    !IsWithin(request.ProjectRoot / "Assets", request.ProjectRoot / "Assets" / auxiliary.RelativePath))
                    throw std::invalid_argument("Asset-worker auxiliary destination escapes Assets.");
            }
            const auto sourceIndexRoot = request.ProjectRoot / "Library/AssetCache/Runtime";
            if (!IsWithin(sourceIndexRoot, request.SourceIndexPath))
                throw std::invalid_argument(
                    "Asset-worker source index path escapes the development catalog directory.");

            RecoverAuxiliaryPublications(request.ProjectRoot);
            auto database = CreateDatabase(request.ProjectRoot);
            const auto progress = [&](const Keire::AssetOperationProgress& value)
            {
                if (std::filesystem::exists(commandLine.Cancel))
                    throw Keire::AssetOperationCancelled();
                Keire::Detail::WriteAssetWorkerProgress(commandLine.Progress, value);
            };

            switch (request.Kind)
            {
            case Keire::Detail::AssetWorkerOperationKind::ImportAll:
                result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                break;
            case Keire::Detail::AssetWorkerOperationKind::ExternalImport:
            {
                auto external = database->ImportExternal(request.ExternalItems, {}, progress);
                result.Import = std::move(external.Import);
                result.ExternalEntries = std::move(external.Entries);
                result.Receipt = external.Receipt;
                break;
            }
            case Keire::Detail::AssetWorkerOperationKind::CreateAsset:
            {
                const auto journal = operationRoot / "create-auxiliary.journal";
                std::vector<std::filesystem::path> publishedAuxiliary;
                if (!request.CreateAuxiliarySources.empty())
                {
                    std::string journalText = Keire::Detail::PathToUtf8(request.CreateRelativePath) + '\n';
                    for (const auto& auxiliary : request.CreateAuxiliarySources)
                        journalText += Keire::Detail::PathToUtf8(auxiliary.RelativePath) + '\n';
                    Keire::Detail::WriteTextFileAtomically(journal, journalText);
                }
                try
                {
                    for (const auto& auxiliary : request.CreateAuxiliarySources)
                    {
                        const auto destination = request.ProjectRoot / "Assets" / auxiliary.RelativePath;
                        if (std::filesystem::exists(destination))
                            throw std::runtime_error("Asset creation auxiliary destination already exists: " +
                                                     Keire::Detail::PathToUtf8(auxiliary.RelativePath));
                        Keire::Detail::WriteTextFileAtomically(
                            destination, Keire::Detail::ReadTextFile(auxiliary.PayloadPath, 64U * 1024U * 1024U));
                        publishedAuxiliary.push_back(destination);
                    }
                    const std::array item{
                        Keire::ExternalAssetImportItem{.SourcePath = request.CreatePayloadPath,
                                                       .RelativeDestination = request.CreateRelativePath,
                                                       .Settings = request.CreateSettings,
                                                       .Conflict = Keire::ExternalAssetConflictPolicy::UniqueName}};
                    auto external = database->ImportExternal(item, {}, progress);
                    if (external.Entries.size() != 1 || !external.Entries.front().Id)
                        throw std::runtime_error("Asset creation did not publish exactly one asset.");
                    result.CreatedAsset = external.Entries.front().Id;
                    result.Import = std::move(external.Import);
                    result.ExternalEntries = std::move(external.Entries);
                    result.Receipt = external.Receipt;
                    std::filesystem::remove(journal);
                }
                catch (...)
                {
                    std::error_code ignored;
                    for (const auto& auxiliary : publishedAuxiliary)
                        std::filesystem::remove(auxiliary, ignored);
                    std::filesystem::remove(journal, ignored);
                    throw;
                }
                break;
            }
            case Keire::Detail::AssetWorkerOperationKind::ExtractMaterials:
                result.MutatedAssets = database->ExtractMaterials(request.ExtractModel, request.ExtractDirectory);
                result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                break;
            case Keire::Detail::AssetWorkerOperationKind::Mutate:
            {
                const auto& mutation = request.Mutation;
                switch (mutation.Kind)
                {
                case Keire::Detail::AssetWorkerMutationKind::CreateFolder:
                    database->CreateFolder(mutation.Destination);
                    break;
                case Keire::Detail::AssetWorkerMutationKind::MoveAsset:
                    database->MoveAsset(mutation.Asset, mutation.Destination);
                    result.MutatedAssets.push_back(mutation.Asset);
                    break;
                case Keire::Detail::AssetWorkerMutationKind::DuplicateAsset:
                    result.MutatedAssets.push_back(database->Duplicate(mutation.Asset, mutation.Destination));
                    break;
                case Keire::Detail::AssetWorkerMutationKind::MoveFolder:
                    database->MoveFolder(mutation.Source, mutation.Destination);
                    break;
                case Keire::Detail::AssetWorkerMutationKind::DuplicateFolder:
                    result.MutatedAssets = database->DuplicateFolder(mutation.Source, mutation.Destination);
                    break;
                case Keire::Detail::AssetWorkerMutationKind::TrashAsset:
                {
                    const auto trashed = database->TrashAsset(mutation.Asset);
                    result.Trash = trashed.Id;
                    result.MutatedAssets = trashed.Assets;
                    break;
                }
                case Keire::Detail::AssetWorkerMutationKind::TrashFolder:
                {
                    const auto trashed = database->TrashFolder(mutation.Source);
                    result.Trash = trashed.Id;
                    result.MutatedAssets = trashed.Assets;
                    break;
                }
                case Keire::Detail::AssetWorkerMutationKind::RestoreTrash:
                    database->RestoreTrash(mutation.Trash);
                    break;
                case Keire::Detail::AssetWorkerMutationKind::PermanentlyDeleteTrash:
                    database->PermanentlyDeleteTrash(mutation.Trash);
                    break;
                }
                result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                break;
            }
            case Keire::Detail::AssetWorkerOperationKind::Cook:
                (void)database->Refresh();
                result.Cook =
                    Keire::AssetCooker::Cook(*database, request.BuildProfile, request.CookOutput, {}, progress);
                Keire::AssetCooker::Validate(result.Cook->CatalogPath);
                break;
            case Keire::Detail::AssetWorkerOperationKind::UndoExternalImport:
                database->UndoExternalImport(request.Receipt);
                result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                break;
            case Keire::Detail::AssetWorkerOperationKind::RedoExternalImport:
                database->RedoExternalImport(request.Receipt);
                result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                break;
            }
            Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, request.SourceIndexPath);
            result.Success = true;
            Keire::Detail::WriteAssetWorkerResult(commandLine.Result, result);
            std::cout << "Asset operation " << request.OperationId << " completed.\n";
            return 0;
        }
        catch (const Keire::AssetOperationCancelled&)
        {
            result.Cancelled = true;
            result.Diagnostic = "Asset operation was cancelled.";
        }
        catch (const std::exception& error)
        {
            result.Diagnostic = error.what();
        }
        if (!resultPath.empty())
        {
            try
            {
                Keire::Detail::WriteAssetWorkerResult(resultPath, result);
            }
            catch (...)
            {
            }
        }
        std::cerr << "Asset worker failed: " << result.Diagnostic << '\n';
        return result.Cancelled ? 2 : 1;
    }
} // namespace

int main(const int argc, char* argv[])
{
    try
    {
        Keire::Detail::Utf8CommandLine commandLine(argc, argv);
        return RunWorker(commandLine.Count(), commandLine.Values());
    }
    catch (const std::exception& error)
    {
        std::cerr << "Asset worker failed: " << error.what() << '\n';
        return 1;
    }
}
