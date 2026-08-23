#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Project/Project.h"
#include "Keire/Rendering/LightingBaker.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/Assets/TextureImportBackend.h"
#include "KeireInternal/Audio/AudioImportBackend.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include "KeireProjectModules/SourceModulePack.h"
#include <KeireAssetWorkerInternal/FfmpegAudioImportBackend.h>
#include <KeireAssetWorkerInternal/FfmpegTextureImportBackend.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <span>
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

    [[nodiscard]] Keire::AssetDatabaseSpecification
    CreateDatabaseSpecification(const std::filesystem::path& projectRoot)
    {
        auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
            Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
        const auto project = Keire::Project::Open(projectRoot);
        modules->ValidateRequired(project->Descriptor().RequiredModules);
        Keire::AssetDatabaseSpecification specification{.ProjectRoot = projectRoot};
        specification.Jobs = Keire::CreateRef<Keire::JobSystem>();
        specification.Importers = Keire::CreateBuiltinAssetImporters();
        for (auto& importer : modules->Importers())
        {
            if (std::ranges::find(specification.Importers, importer.Name, &Keire::AssetImporterRegistration::Name) !=
                specification.Importers.end())
                throw std::invalid_argument("A source module importer duplicates an existing importer: " +
                                            importer.Name);
            specification.Importers.push_back(std::move(importer));
        }
        for (auto& importer : specification.Importers)
        {
            if (importer.Name == "Keire.AudioClip")
                importer = Keire::Detail::CreateAudioClipAssetImporter(Keire::Detail::CreateFfmpegAudioImportBackend());
            else if (importer.Name == "Keire.Texture2D")
                importer =
                    Keire::Detail::CreateTexture2DAssetImporter({}, Keire::Detail::CreateFfmpegTextureImportBackend());
        }
        return specification;
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
            std::istringstream stream(Keire::Detail::ReadTextFile(journal, std::size_t{1024} * 1024U));
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
            std::cout << "Asset worker request: operation=" << request.OperationId
                      << " kind=" << Keire::Detail::AssetWorkerOperationName(request.Kind) << " reason='"
                      << (request.Reason.empty() ? "unspecified" : request.Reason)
                      << "' targets=" << request.ImportAssets.size() << " project='"
                      << Keire::Detail::PathToUtf8(request.ProjectRoot) << "'\n";
            for (const auto asset : request.ImportAssets)
                std::cout << "  requested asset: " << asset.ToString() << '\n';
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
            if (request.Kind == Keire::Detail::AssetWorkerOperationKind::BakeLighting && !request.BakeScene)
                throw std::invalid_argument("Asset-worker lighting bake requires a scene asset.");
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
            const auto runtimeCatalog = sourceIndexRoot / "catalog.json";
            const bool hadRuntimeCatalog = std::filesystem::is_regular_file(runtimeCatalog);
            std::cout << "Asset worker runtime catalog: " << (hadRuntimeCatalog ? "present" : "absent") << " ('"
                      << Keire::Detail::PathToUtf8(runtimeCatalog) << "')\n";
            if (request.Kind == Keire::Detail::AssetWorkerOperationKind::ImportAssets && !hadRuntimeCatalog)
            {
                std::cout << "Asset worker targeted import must bootstrap with a full import because no prior runtime "
                             "catalog exists.\n";
            }

            RecoverAuxiliaryPublications(request.ProjectRoot);
            auto databaseSpecification = CreateDatabaseSpecification(request.ProjectRoot);
            Keire::Ref<Keire::AssetDatabase> database;
            bool loadedSourceIndex = false;
            if (std::filesystem::is_regular_file(request.SourceIndexPath))
            {
                try
                {
                    database = Keire::Detail::AssetDatabaseWorkerAccess::CreateFromSourceIndex(databaseSpecification,
                                                                                               request.SourceIndexPath);
                    loadedSourceIndex = true;
                    std::cout << "Asset worker loaded the published source index without a full source scan.\n";
                }
                catch (const std::exception& error)
                {
                    std::cerr << "Asset worker ignored a stale source index and will rescan: " << error.what() << '\n';
                }
            }
            if (!database)
            {
                if (!std::filesystem::is_regular_file(request.SourceIndexPath))
                {
                    std::cout << "Asset worker source index is absent; the database will scan the project: "
                              << Keire::Detail::PathToUtf8(request.SourceIndexPath) << '\n';
                }
                database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            }
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
            case Keire::Detail::AssetWorkerOperationKind::ImportAssets:
                if (request.ImportAssets.empty())
                    throw std::invalid_argument("Targeted asset import requires at least one asset identity.");
                if (loadedSourceIndex)
                {
                    bool rescanned = false;
                    result.Import = Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndexOrRescan(
                        database, CreateDatabaseSpecification(request.ProjectRoot), request.ImportAssets,
                        Keire::AssetImportPolicy::KeepLastGood, rescanned, {}, progress);
                    if (rescanned)
                        std::cout << "Asset worker targeted identities were absent from the published source index; "
                                     "rescanning before retry.\n";
                }
                else
                {
                    result.Import = database->ImportAssets(request.ImportAssets, Keire::AssetImportPolicy::KeepLastGood,
                                                           {}, progress);
                }
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
                            destination,
                            Keire::Detail::ReadTextFile(auxiliary.PayloadPath, std::size_t{64} * 1024U * 1024U));
                        publishedAuxiliary.push_back(destination);
                    }
                    const auto importer = database->FindImporterForPath(request.CreateRelativePath);
                    if (request.CreateParentSource && (!request.CreateAuxiliarySources.empty() || !importer))
                        throw std::runtime_error(
                            "Parented asset creation requires a directly registered source importer.");
                    if (request.CreateAuxiliarySources.empty() && importer)
                    {
                        const auto source =
                            Keire::Detail::ReadTextFile(request.CreatePayloadPath, std::size_t{64} * 1024U * 1024U);
                        result.CreatedAsset =
                            database->CreateAsset(request.CreateRelativePath, *importer,
                                                  std::as_bytes(std::span(source.data(), source.size())),
                                                  request.CreateSettings, request.CreateParentSource);
                        const std::array targets{result.CreatedAsset};
                        result.Import = Keire::Detail::AssetDatabaseWorkerAccess::ImportAssetsFromSourceIndex(
                            *database, targets, Keire::AssetImportPolicy::FailFast, {}, progress);
                    }
                    else
                    {
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
                    }
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
                const bool requiresImport = mutation.Kind == Keire::Detail::AssetWorkerMutationKind::DuplicateAsset ||
                                            mutation.Kind == Keire::Detail::AssetWorkerMutationKind::DuplicateFolder;
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
                if (requiresImport)
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
            case Keire::Detail::AssetWorkerOperationKind::BakeLighting:
            {
                (void)database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                const auto sceneRecord = database->Find(request.BakeScene);
                if (!sceneRecord || sceneRecord->Type != Keire::SceneAsset::StaticType())
                    throw std::invalid_argument(
                        "Asset-worker lighting bake scene is unavailable or has the wrong type.");
                const auto sourcePath = request.ProjectRoot / "Assets" / sceneRecord->RelativePath;
                const auto originalSource = Keire::Detail::ReadTextFile(sourcePath, std::size_t{64} * 1024U * 1024U);
                const auto scene = Keire::SceneAsset::Decode(std::as_bytes(std::span(originalSource)));
                Keire::LightingBakeRequest bake;
                bake.Scene = request.BakeScene;
                bake.Definition = scene->Definition();
                bake.ProjectRoot = request.ProjectRoot;
                bake.Force = request.BakeForce;
                const auto records = database->Records();
                std::map<Keire::AssetId, Keire::AssetSourceRecord> indexed;
                for (const auto& record : records)
                    indexed.emplace(record.Id, record);
                std::vector<Keire::AssetId> pending(sceneRecord->Dependencies.begin(), sceneRecord->Dependencies.end());
                std::set<Keire::AssetId> visited;
                while (!pending.empty())
                {
                    const auto id = pending.back();
                    pending.pop_back();
                    if (!id || !visited.emplace(id).second)
                        continue;
                    const auto found = indexed.find(id);
                    if (found == indexed.end())
                        continue;
                    bake.Inputs.push_back({id, found->second.SourceDigest});
                    pending.insert(pending.end(), found->second.Dependencies.begin(), found->second.Dependencies.end());
                }
                bake.Progress = [&](const Keire::LightingBakeProgress& value)
                {
                    if (std::filesystem::exists(commandLine.Cancel))
                        throw Keire::AssetOperationCancelled();
                    const auto phase = value.Phase == Keire::LightingBakePhase::Publishing
                                           ? Keire::AssetOperationPhase::Publishing
                                           : Keire::AssetOperationPhase::Importing;
                    Keire::Detail::WriteAssetWorkerProgress(
                        commandLine.Progress,
                        {phase, value.Completed, value.Total, std::filesystem::path(value.Message)});
                };
                const auto baked = Keire::LightingBaker::Bake(bake);
                auto updated = scene->Definition();
                updated.BakedLighting = baked.LightingSet;
                try
                {
                    Keire::Detail::WriteFileAtomically(sourcePath, Keire::SceneAsset::Encode(updated));
                    result.Import = database->ImportAll(Keire::AssetImportPolicy::KeepLastGood, {}, progress);
                }
                catch (...)
                {
                    Keire::Detail::WriteTextFileAtomically(sourcePath, originalSource);
                    throw;
                }
                result.CreatedAsset = baked.LightingSet;
                result.LightingCacheHit = baked.CacheHit;
                result.MutatedAssets.push_back(request.BakeScene);
                for (const auto& output : baked.Assets)
                    result.MutatedAssets.push_back(output.Id);
                break;
            }
            }
            Keire::Detail::AssetDatabaseWorkerAccess::PublishSourceIndex(*database, request.SourceIndexPath);
            std::cout << "Asset worker completed " << Keire::Detail::AssetWorkerOperationName(request.Kind)
                      << ": imported-statuses=" << result.Import.Statuses.size() << '\n';
            if (request.Kind == Keire::Detail::AssetWorkerOperationKind::ImportAssets)
            {
                std::cout << "Asset worker targeted closure: requested=" << request.ImportAssets.size()
                          << " imported-statuses=" << result.Import.Statuses.size() << '\n';
            }
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
