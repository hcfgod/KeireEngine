#include "Keire/Assets/AssetPackage.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Build/PlayerBuild.h"
#include "Keire/BuildInfo.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Log.h"
#include "Keire/Project/Project.h"
#include "Keire/Project/ProjectAuthoringSettings.h"
#include "Keire/Project/ProjectUpgrade.h"
#include "Keire/Project/ShaderGraphMigration.h"
#include "Keire/Rendering/LightingBaker.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "Keire/Scripting/ScriptSystem.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/AssetWorkerProtocol.h"
#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/Build/PlayerSupport.h"
#include "KeireInternal/Build/PlayerSupportCatalog.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireProjectModules/SourceModulePack.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    struct CommandLine
    {
        std::string Command;
        std::filesystem::path Project = ".";
        std::filesystem::path Output = "Build/Assets";
        std::filesystem::path Catalog;
        std::filesystem::path Manifest;
        std::filesystem::path Input;
        std::filesystem::path Status;
        std::filesystem::path Cancel;
        std::string PlayerProfile;
        std::string StagingId;
        std::string PackId;
        std::string EngineVersion;
        std::string ExpectedPlatform;
        std::string ExpectedArchitecture;
        std::string Url;
        std::string Sha256;
        std::uint64_t ExpectedSize = 0;
        Keire::AssetBuildProfile Profile;
        std::chrono::seconds WorkerTimeout = std::chrono::minutes(10);
        bool Force = false;
        bool ApplyUpgrade = false;
        bool RecoverUpgrade = false;
        bool RollbackUpgrade = false;
        bool CheckOnly = false;
        bool ProjectSpecified = false;
    };

    [[nodiscard]] std::uint64_t ParseUnsigned(const std::string_view value, const char* option)
    {
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            throw std::invalid_argument(std::string(option) + " requires an unsigned integer.");
        return result;
    }

    [[nodiscard]] Keire::AssetTargetPlatform ParseTarget(const std::string_view value)
    {
        if (value == "host")
            return Keire::AssetTargetPlatform::Host;
        if (value == "windows")
            return Keire::AssetTargetPlatform::Windows;
        if (value == "linux")
            return Keire::AssetTargetPlatform::Linux;
        if (value == "macos")
            return Keire::AssetTargetPlatform::MacOS;
        throw std::invalid_argument("--target requires host, windows, linux, or macos.");
    }

    [[nodiscard]] CommandLine Parse(const int argc, char* const* argv)
    {
        if (argc < 2)
            throw std::invalid_argument("A command is required. Run with --help for usage.");
        CommandLine result;
        result.Command = argv[1];
        if (result.Command == "--help" || result.Command == "-h")
            return result;
        for (int index = 2; index < argc; ++index)
        {
            const std::string_view option = argv[index];
            const auto requireValue = [&]() -> std::string_view
            {
                if (++index >= argc)
                    throw std::invalid_argument(std::string(option) + " requires a value.");
                return argv[index];
            };
            if (option == "--project")
            {
                result.Project = Keire::Detail::PathFromUtf8(requireValue());
                result.ProjectSpecified = true;
            }
            else if (option == "--output")
                result.Output = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--catalog")
                result.Catalog = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--manifest")
                result.Manifest = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--input")
                result.Input = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--status")
                result.Status = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--cancel")
                result.Cancel = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--pack-id")
                result.PackId = requireValue();
            else if (option == "--engine-version")
                result.EngineVersion = requireValue();
            else if (option == "--expected-platform")
                result.ExpectedPlatform = requireValue();
            else if (option == "--expected-architecture")
                result.ExpectedArchitecture = requireValue();
            else if (option == "--url")
                result.Url = requireValue();
            else if (option == "--sha256")
                result.Sha256 = requireValue();
            else if (option == "--size")
                result.ExpectedSize = ParseUnsigned(requireValue(), "--size");
            else if (option == "--staging-id")
            {
                result.StagingId = requireValue();
                if (result.StagingId.size() != 12 ||
                    !std::ranges::all_of(result.StagingId,
                                         [](const unsigned char character) { return std::isxdigit(character) != 0; }))
                    throw std::invalid_argument("--staging-id requires exactly 12 hexadecimal digits.");
            }
            else if (option == "--profile")
            {
                if (result.Command == "build-player")
                    result.PlayerProfile = requireValue();
                else
                {
                    result.Profile.Name = requireValue();
                    result.Profile.Strict = result.Profile.Name == "Dist";
                }
            }
            else if (option == "--compression-level")
                result.Profile.CompressionLevel =
                    static_cast<int>(ParseUnsigned(requireValue(), "--compression-level"));
            else if (option == "--pack-mib")
                result.Profile.MaximumPackBytes = ParseUnsigned(requireValue(), "--pack-mib") * 1024ULL * 1024ULL;
            else if (option == "--worker-timeout-seconds")
            {
                const auto seconds = ParseUnsigned(requireValue(), "--worker-timeout-seconds");
                if (seconds == 0 || seconds > static_cast<std::uint64_t>(std::chrono::seconds::max().count()))
                {
                    throw std::invalid_argument("--worker-timeout-seconds must be greater than zero.");
                }
                result.WorkerTimeout = std::chrono::seconds(static_cast<std::chrono::seconds::rep>(seconds));
            }
            else if (option == "--target")
                result.Profile.Target = ParseTarget(requireValue());
            else if (option == "--force")
                result.Force = true;
            else if (option == "--apply")
                result.ApplyUpgrade = true;
            else if (option == "--recover")
                result.RecoverUpgrade = true;
            else if (option == "--rollback")
                result.RollbackUpgrade = true;
            else if (option == "--check")
                result.CheckOnly = true;
            else
                throw std::invalid_argument("Unknown option: " + std::string(option));
        }
        return result;
    }

    void PrintHelp()
    {
        std::cout << "Kéire Asset Tool\n\n"
                     "Usage:\n"
                     "  KeireAssetTool scan [--project <path>]\n"
                     "  KeireAssetTool import [--project <path>] [--worker-timeout-seconds <seconds>]\n"
                     "  KeireAssetTool cook [--project <path>] [--output <path>] [--profile <name>]\n"
                     "                      [--compression-level <level>] [--pack-mib <size>]\n"
                     "                      [--worker-timeout-seconds <seconds>]\n"
                     "                      [--target <host|windows|linux|macos>]\n"
                     "  KeireAssetTool build-player [--project <path>] [--profile <id-or-name>]\n"
                     "                              [--status <path>] [--worker-timeout-seconds <seconds>]\n"
                     "  KeireAssetTool pack-player-support --catalog <manifest.json> --input <payload>\n"
                     "                                     --output <module.keireplayersupport>\n"
                     "  KeireAssetTool install-player-support --input <module.keireplayersupport>\n"
                     "                                        [--pack-id <id>] [--expected-platform <platform>]\n"
                     "                                        [--expected-architecture <architecture>]\n"
                     "                                        [--status <path>] [--cancel <path>]\n"
                     "  KeireAssetTool fetch-player-support-catalog --output <catalog.json>\n"
                     "                                               [--status <path>] [--cancel <path>]\n"
                     "  KeireAssetTool download-install-player-support --url <https-url> --size <bytes>\n"
                     "                                                  --sha256 <digest> --output <temporary>\n"
                     "                                                  [--status <path>] [--cancel <path>]\n"
                     "  KeireAssetTool verify-player-support --input <module.keireplayersupport>\n"
                     "  KeireAssetTool list-player-support\n"
                     "  KeireAssetTool remove-player-support --engine-version <version> --pack-id <id>\n"
                     "  KeireAssetTool describe-player-support-host\n"
                     "  KeireAssetTool validate-project --project <path>\n"
                     "  KeireAssetTool upgrade-project [--project <path>] [--apply|--recover|--rollback]\n"
                     "  KeireAssetTool migrate-shader-graphs --project <path> [--check]\n"
                     "  KeireAssetTool create-asset-package --manifest <manifest.json> --input <payload>\n"
                     "                                      --output <package.keireassetpackage>\n"
                     "                                      [--compression-level <level>]\n"
                     "  KeireAssetTool inspect-asset-package --input <package.keireassetpackage>\n"
                     "  KeireAssetTool verify-asset-package --input <package.keireassetpackage>\n"
                     "                                      [--size <bytes>] [--sha256 <digest>]\n"
                     "  KeireAssetTool extract-asset-package --input <package.keireassetpackage>\n"
                     "                                       --output <new-staging-directory>\n"
                     "                                       [--size <bytes>] [--sha256 <digest>]\n"
                     "  KeireAssetTool validate --catalog <path>\n";
        std::cout << "  KeireAssetTool bake-lighting [--project <path>] [--input <scene.keirescene>] [--force]\n";
        std::cout << "  KeireAssetTool convert-mesh --input <model> [--output <file.keiremesh>]\n";
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Could not read file: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        std::vector<std::byte> result(characters.size());
        std::ranges::transform(characters, result.begin(), [](const char value) { return std::byte(value); });
        return result;
    }

    [[nodiscard]] Keire::AssetImportResult ImportAssetsWithWorker(const std::filesystem::path& projectRoot,
                                                                  const std::filesystem::path& executable,
                                                                  const std::chrono::seconds timeout)
    {
        const auto normalizedProject = std::filesystem::absolute(projectRoot).lexically_normal();
        const auto worker = Keire::Detail::ResolveCompanionExecutable(executable, "KeireAssetWorker");
        const auto operationId = Keire::AssetId::Generate().ToString();
        const auto operation = normalizedProject / "Library/AssetOperations" / operationId;
        std::filesystem::create_directories(operation);
        const auto requestPath = operation / "request.json";
        const auto progressPath = operation / "progress.json";
        const auto resultPath = operation / "result.json";
        const auto cancelPath = operation / "cancel";
        const auto logPath = operation / "worker.log";

        Keire::Detail::AssetWorkerRequest request;
        request.OperationId = operationId;
        request.Kind = Keire::Detail::AssetWorkerOperationKind::ImportAll;
        request.ProjectRoot = normalizedProject;
        request.SourceIndexPath = normalizedProject / "Library/AssetCache/Runtime/source-index.json";
        Keire::Detail::WriteAssetWorkerRequest(requestPath, request);
        const std::vector<std::string> arguments{
            "--request", Keire::Detail::PathToUtf8(requestPath), "--progress", Keire::Detail::PathToUtf8(progressPath),
            "--result",  Keire::Detail::PathToUtf8(resultPath),  "--cancel",   Keire::Detail::PathToUtf8(cancelPath)};
        const auto process = Keire::Detail::RunProcess(worker, arguments, normalizedProject, timeout);
        Keire::Detail::WriteTextFileAtomically(logPath, process.Output);
        if (process.TimedOut)
        {
            throw std::runtime_error("Asset worker import timed out after " + std::to_string(timeout.count()) +
                                     " seconds. See " + Keire::Detail::PathToUtf8(logPath) + ".");
        }
        if (!std::filesystem::is_regular_file(resultPath))
        {
            throw std::runtime_error("Asset worker exited with code " + std::to_string(process.ExitCode) +
                                     " before writing a result. See " + Keire::Detail::PathToUtf8(logPath) + ".");
        }

        auto result = Keire::Detail::ReadAssetWorkerResult(resultPath);
        if (process.ExitCode != 0 || !result.Success)
        {
            const auto diagnostic =
                result.Diagnostic.empty() ? "no diagnostic was produced" : std::move(result.Diagnostic);
            throw std::runtime_error("Asset worker import failed: " + diagnostic + " See " +
                                     Keire::Detail::PathToUtf8(logPath) + ".");
        }
        if (const auto failed = std::ranges::find(result.Import.Statuses, Keire::AssetImportState::Failed,
                                                  &Keire::AssetImportStatus::State);
            failed != result.Import.Statuses.end())
        {
            const auto detail = failed->Diagnostics.empty() ? std::string("no diagnostic was produced")
                                                            : failed->Diagnostics.front().Message;
            const auto source = !failed->Diagnostics.empty() && !failed->Diagnostics.front().RelativePath.empty()
                                    ? Keire::Detail::PathToUtf8(failed->Diagnostics.front().RelativePath)
                                    : failed->Id.ToString();
            throw std::runtime_error("Asset worker import rejected '" + source + "': " + detail + ". See " +
                                     Keire::Detail::PathToUtf8(logPath) + ".");
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(operation, cleanupError);
        if (cleanupError)
        {
            std::cerr << "warning: Could not remove successful asset-worker operation '"
                      << Keire::Detail::PathToUtf8(operation) << "': " << cleanupError.message() << '\n';
        }
        return std::move(result.Import);
    }

    struct ManagedCookBuild final
    {
        bool Scripting = false;
        std::filesystem::path AssemblyDirectory;
        std::vector<std::string> AssemblyNames;
        std::vector<Keire::ManagedAssetTypeDescriptor> AssetTypes;
    };

    [[nodiscard]] ManagedCookBuild BuildManagedAssemblies(const Keire::AssetDatabase& database,
                                                          const Keire::Project& project, const std::string& profile,
                                                          const std::filesystem::path& executable,
                                                          const bool discoverManagedTypes)
    {
        Keire::ManagedBuildRequest request;
        for (const auto& record : database.Records())
        {
            if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
                continue;
            const auto assembly =
                Keire::ManagedAssemblyAsset::Decode(ReadBytes(project.Root() / "Assets" / record.RelativePath));
            if (assembly->Definition().Classification != Keire::ManagedAssemblyClassification::Runtime)
                continue;
            request.Assemblies.push_back({record.Id, assembly->Definition()});
        }
        if (request.Assemblies.empty())
            return {};
        ManagedCookBuild result;
        result.Scripting = true;
        result.AssemblyNames.reserve(request.Assemblies.size());
        for (const auto& assembly : request.Assemblies)
            result.AssemblyNames.push_back(assembly.Definition.Name);

        Keire::ScriptSystemSpecification specification;
        specification.Mode = Keire::ScriptMode::Enabled;
        specification.ProjectRoot = project.Root();
        const auto managedHost = executable.parent_path() / "Managed";
        specification.RuntimeHostDirectory = managedHost;
        specification.RuntimeRootDirectory = managedHost / "Dotnet";
        specification.ManagedApiAssembly = managedHost / "Keire.Managed.dll";
#if defined(_WIN32)
        const auto developmentDotnet =
            executable.parent_path().parent_path().parent_path().parent_path().parent_path() /
            "Build/Dependencies/dotnet-sdk/dotnet.exe";
#else
        const auto developmentDotnet =
            executable.parent_path().parent_path().parent_path().parent_path().parent_path() /
            "Build/Dependencies/dotnet-sdk/dotnet";
#endif
        if (std::filesystem::is_regular_file(developmentDotnet))
            specification.DotnetExecutable = developmentDotnet;
        auto scripts = Keire::CreateRef<Keire::ScriptSystem>(specification);
        request.Configuration = profile == "Development" ? "Debug" : "Release";
        const auto operation = scripts->StartBuild(std::move(request));
        if (!scripts->WaitForBuild(operation, std::chrono::minutes(5)))
        {
            scripts->CancelBuild(operation);
            throw std::runtime_error("Managed gameplay build timed out.");
        }
        const auto status = scripts->BuildStatus();
        if (status.State != Keire::ManagedBuildState::Succeeded)
        {
            const auto detail = status.Diagnostics.empty() ? std::string("no diagnostic was produced")
                                                           : status.Diagnostics.front().Message;
            throw std::runtime_error("Managed gameplay build failed: " + detail);
        }
        result.AssemblyDirectory = status.ActiveAssemblyDirectory;
        if (discoverManagedTypes)
        {
            Keire::ManagedReloadRequest reload;
            reload.ManagedApiAssembly = status.ManagedApiAssembly;
            for (const auto& name : result.AssemblyNames)
            {
                const auto assembly = status.ActiveAssemblyDirectory / (name + ".dll");
                if (!std::filesystem::is_regular_file(assembly))
                    throw std::runtime_error("Managed gameplay build did not publish " + name + ".dll.");
                reload.Assemblies.push_back(assembly);
            }
            std::ranges::sort(reload.Assemblies);
            if (!scripts->PrepareReload(std::move(reload)))
                throw std::runtime_error("Managed gameplay type discovery failed: " +
                                         scripts->ReloadStatus().Diagnostic);
            scripts->CommitReload();
            const auto diagnostics = scripts->ManagedAssetTypeDiagnostics();
            if (!diagnostics.empty())
            {
                throw std::runtime_error("Managed gameplay type discovery rejected '" + diagnostics.front().TypeName +
                                         "': " + diagnostics.front().Message);
            }
            result.AssetTypes = scripts->ManagedAssetTypes();
        }
        scripts->Close();
        return result;
    }

    void CopyManagedAssemblies(const ManagedCookBuild& build, const std::filesystem::path& output)
    {
        if (!build.Scripting)
            return;
        const auto destination = output / "ManagedAssemblies";
        std::filesystem::create_directories(destination);
        std::size_t copied = 0;
        for (const auto& entry : std::filesystem::directory_iterator(build.AssemblyDirectory))
        {
            const auto filename = entry.path().filename().string();
            const bool requested = std::ranges::any_of(
                build.AssemblyNames, [&](const std::string& name)
                { return filename == name + ".dll" || filename == name + ".pdb" || filename == name + ".deps.json"; });
            if (!entry.is_regular_file() || !requested)
                continue;
            std::filesystem::copy_file(entry.path(), destination / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
            ++copied;
        }
        if (copied == 0)
            throw std::runtime_error("Managed gameplay build published no runtime assemblies.");
    }

    void PublishCookOutput(const std::filesystem::path& requestedStaging,
                           const std::filesystem::path& requestedDestination)
    {
        const auto staging = std::filesystem::absolute(requestedStaging).lexically_normal();
        const auto destination = std::filesystem::absolute(requestedDestination).lexically_normal();
        if (staging.parent_path() != destination.parent_path() || staging == destination)
            throw std::invalid_argument("Cook publication staging must be a distinct sibling of its destination.");
        const auto backup =
            Keire::Detail::PathWithSuffix(destination, ".previous-" + Keire::AssetId::Generate().ToString());
        std::error_code error;
        std::filesystem::remove_all(backup, error);
        if (error)
            throw std::filesystem::filesystem_error("Could not prepare cooked output backup.", backup, error);
        const bool hadDestination = std::filesystem::exists(destination);
        if (hadDestination)
            Keire::Detail::RenamePathWithRetry(destination, backup);
        try
        {
            Keire::Detail::RenamePathWithRetry(staging, destination);
        }
        catch (...)
        {
            if (hadDestination && !std::filesystem::exists(destination))
                Keire::Detail::RenamePathWithRetry(backup, destination);
            throw;
        }
        std::filesystem::remove_all(backup, error);
        if (error)
            throw std::filesystem::filesystem_error("Could not remove previous cooked output.", backup, error);
    }

    [[nodiscard]] std::vector<Keire::AssetId> ResolvePlayerBuildScenes(const Keire::Project& project,
                                                                       const Keire::AssetDatabase& database)
    {
        auto scenes =
            Keire::EnabledPlayerBuildScenes(Keire::LoadPlayerBuildScenes(project.Root(), project.Descriptor()));
        if (scenes.empty())
            throw std::runtime_error("Player builds require at least one enabled scene in Build Settings.");
        for (const auto scene : scenes)
        {
            const auto record = database.Find(scene);
            if (!record || record->Type != Keire::SceneAsset::StaticType())
                throw std::runtime_error("Build Settings references a missing or invalid scene asset: " +
                                         scene.ToString());
        }
        return scenes;
    }

    void WriteRuntimeManifest(const Keire::Project& project, const std::filesystem::path& output,
                              const Keire::ModuleRegistry& modules, const bool scripting,
                              const std::span<const Keire::AssetId> buildScenes,
                              const Keire::PlayerBuildProfile* playerProfile = nullptr)
    {
        if (buildScenes.empty())
            throw std::runtime_error("Runtime cooking requires at least one enabled build scene.");
        const auto& descriptor = project.Descriptor();
        const auto rendering = Keire::LoadRenderEnvironmentSettings(project.Root());
        const auto authoring = Keire::LoadProjectAuthoringSettings(project.Root());
        const auto& build = Keire::GetBuildInfo();
        nlohmann::json physicsLayerNames = nlohmann::json::array();
        nlohmann::json physicsCollisionMatrix = nlohmann::json::array();
        for (std::size_t index = 0; index < Keire::PhysicsCollisionLayerCount; ++index)
        {
            physicsLayerNames.push_back(authoring.PhysicsLayerNames[index]);
            physicsCollisionMatrix.push_back(authoring.PhysicsCollisionMatrix[index]);
        }
        nlohmann::json moduleCatalog = nlohmann::json::array();
        for (const auto& module : modules.OrderedCatalog())
            moduleCatalog.push_back({{"id", module.Id}, {"version", module.Version.ToString()}});
        nlohmann::json encodedBuildScenes = nlohmann::json::array();
        for (const auto scene : buildScenes)
            encodedBuildScenes.push_back(scene.ToString());
        const auto audioLayout = [&]
        {
            switch (authoring.Audio.OutputLayout)
            {
            case Keire::AudioChannelLayout::Mono:
                return "mono";
            case Keire::AudioChannelLayout::Stereo:
                return "stereo";
            case Keire::AudioChannelLayout::Surround51:
                return "5.1";
            case Keire::AudioChannelLayout::Surround71:
                return "7.1";
            }
            throw std::logic_error("Project audio output layout is unsupported.");
        }();
        nlohmann::json manifest{
            {"schemaVersion", 4},
            {"startupScene", buildScenes.front().ToString()},
            {"buildScenes", std::move(encodedBuildScenes)},
            {"defaultInput",
             descriptor.DefaultInput ? nlohmann::json(descriptor.DefaultInput.ToString()) : nlohmann::json(nullptr)},
            {"defaultInputMap", descriptor.DefaultInputMap ? nlohmann::json(descriptor.DefaultInputMap.ToString())
                                                           : nlohmann::json(nullptr)},
            {"defaultMixer",
             authoring.DefaultMixer ? nlohmann::json(authoring.DefaultMixer.ToString()) : nlohmann::json(nullptr)},
            {"buildIdentity",
             {{"engineVersion", build.Version},
              {"configuration", playerProfile ? Keire::ToString(playerProfile->Configuration) : build.Configuration},
              {"platform", playerProfile ? Keire::ToString(playerProfile->Platform) : build.Platform},
              {"architecture", playerProfile ? Keire::ToString(playerProfile->Architecture) : build.Architecture}}},
            {"sourceModules", std::move(moduleCatalog)},
            {"managedAssemblyRoots",
             scripting ? nlohmann::json::array({"ManagedAssemblies"}) : nlohmann::json::array()},
            {"subsystems", {{"scripting", scripting}, {"physics", true}, {"audio", true}, {"navigation", true}}},
            {"physics",
             {{"layerNames", std::move(physicsLayerNames)}, {"collisionMatrix", std::move(physicsCollisionMatrix)}}},
            {"audio",
             {{"mixSampleRate", authoring.Audio.MixSampleRate},
              {"periodFrames", authoring.Audio.PeriodFrames},
              {"outputLayout", audioLayout},
              {"maximumVoices", authoring.Audio.MaximumVoices},
              {"maximumVirtualVoices", authoring.Audio.MaximumVirtualVoices}}},
            {"streaming", {{"pageBytes", 262144}, {"maximumConcurrentReads", 8}}},
            {"rendering",
             {{"ambientColor",
               {rendering.AmbientColor.Red, rendering.AmbientColor.Green, rendering.AmbientColor.Blue,
                rendering.AmbientColor.Alpha}},
              {"ambientIntensity", rendering.AmbientIntensity},
              {"exposure", rendering.Exposure}}}};
        std::ofstream stream(output / "runtime-manifest.json", std::ios::binary | std::ios::trunc);
        stream << std::setprecision(9) << manifest.dump(2) << '\n';
        if (!stream)
            throw std::runtime_error("Could not write the cooked runtime manifest.");
    }

    [[nodiscard]] Keire::AssetTargetPlatform AssetTarget(const Keire::PlayerPlatform platform) noexcept
    {
        switch (platform)
        {
        case Keire::PlayerPlatform::Windows:
            return Keire::AssetTargetPlatform::Windows;
        case Keire::PlayerPlatform::Linux:
            return Keire::AssetTargetPlatform::Linux;
        case Keire::PlayerPlatform::MacOS:
            return Keire::AssetTargetPlatform::MacOS;
        }
        return Keire::AssetTargetPlatform::Host;
    }

    [[nodiscard]] std::string AssetProfileName(const Keire::PlayerBuildConfiguration configuration)
    {
        switch (configuration)
        {
        case Keire::PlayerBuildConfiguration::Development:
            return "Development";
        case Keire::PlayerBuildConfiguration::Release:
            return "Release";
        case Keire::PlayerBuildConfiguration::Dist:
            return "Dist";
        }
        throw std::logic_error("Unknown player build configuration.");
    }

    void WritePlayerBuildStatus(const std::filesystem::path& requestedPath, const std::string_view state,
                                const std::string_view phase, const float progress, const std::string_view message,
                                const std::filesystem::path& output = {}, const std::filesystem::path& executable = {},
                                const std::string_view errorCode = {})
    {
        if (requestedPath.empty())
            return;
        Keire::Detail::WritePlayerBuildStatusDocument(requestedPath, {.State = std::string(state),
                                                                      .Phase = std::string(phase),
                                                                      .Progress = progress,
                                                                      .Message = std::string(message),
                                                                      .ErrorCode = std::string(errorCode),
                                                                      .Output = output,
                                                                      .Executable = executable});
    }

    void LogBuildSupportFailure(const std::string_view context, const std::exception& error) noexcept
    {
        try
        {
            KEIRE_CORE_ERROR("{}: {}", context, error.what());
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] const Keire::PlayerBuildProfile& SelectPlayerProfile(const Keire::PlayerBuildProfiles& profiles,
                                                                       const std::string_view selector)
    {
        if (selector.empty())
            return Keire::FindPlayerBuildProfile(profiles, profiles.ActiveProfile);
        try
        {
            return Keire::FindPlayerBuildProfile(profiles, selector);
        }
        catch (const std::invalid_argument&)
        {
            try
            {
                return Keire::FindPlayerBuildProfile(profiles, Keire::AssetId::Parse(selector));
            }
            catch (const std::exception&)
            {
                throw std::invalid_argument("Player build profile was not found: " + std::string(selector));
            }
        }
    }
} // namespace

namespace
{
    int RunAssetTool(const int argc, char* const* argv)
    {
        try
        {
            auto commandLine = Parse(argc, argv);
            if (commandLine.Command == "--help" || commandLine.Command == "-h")
            {
                PrintHelp();
                return 0;
            }
            if (commandLine.Command == "validate")
            {
                if (commandLine.Catalog.empty())
                    throw std::invalid_argument("validate requires --catalog <path>.");
                Keire::AssetCooker::Validate(commandLine.Catalog);
                std::cout << "Validated " << commandLine.Catalog.string() << '\n';
                return 0;
            }
            if (commandLine.Command == "create-asset-package")
            {
                if (commandLine.Manifest.empty() || commandLine.Input.empty() ||
                    commandLine.Output == std::filesystem::path("Build/Assets"))
                {
                    throw std::invalid_argument(
                        "create-asset-package requires --manifest <manifest.json>, --input <payload>, and --output "
                        "<package.keireassetpackage>.");
                }
                if (commandLine.Output.extension() != ".keireassetpackage")
                    throw std::invalid_argument("Asset packages must use the .keireassetpackage extension.");
                const auto manifestBytes = ReadBytes(commandLine.Manifest);
                const auto* manifestText = reinterpret_cast<const char*>(manifestBytes.data());
                auto manifest = Keire::DecodeAssetPackageManifest(std::string_view(manifestText, manifestBytes.size()));
                manifest = Keire::InventoryAssetPackagePayload(std::move(manifest), commandLine.Input);
                const auto result =
                    Keire::WriteAssetPackageArchive({.Manifest = std::move(manifest),
                                                     .PayloadRoot = commandLine.Input,
                                                     .Output = commandLine.Output,
                                                     .CompressionLevel = commandLine.Profile.CompressionLevel});
                std::cout << "Created " << Keire::Detail::PathToUtf8(commandLine.Output) << " ("
                          << result.ArchiveSizeBytes << " bytes, sha256 " << result.ArchiveSha256 << ")\n";
                return 0;
            }
            if (commandLine.Command == "inspect-asset-package" || commandLine.Command == "verify-asset-package")
            {
                if (commandLine.Input.empty())
                {
                    throw std::invalid_argument(commandLine.Command + " requires --input <package.keireassetpackage>.");
                }
                Keire::AssetPackageVerification verification;
                if (commandLine.ExpectedSize != 0U)
                    verification.ExpectedArchiveSizeBytes = commandLine.ExpectedSize;
                verification.ExpectedArchiveSha256 = commandLine.Sha256;
                const auto result = Keire::InspectAssetPackageArchive(commandLine.Input, verification);
                if (commandLine.Command == "verify-asset-package")
                {
                    std::cout << "Verified " << result.Manifest.PackageId << '@' << result.Manifest.Version << " ("
                              << result.ArchiveSizeBytes << " bytes, sha256 " << result.ArchiveSha256 << ")\n";
                    return 0;
                }
                auto report = nlohmann::json::parse(Keire::EncodeAssetPackageManifest(result.Manifest));
                report["archive"] = {{"sizeBytes", result.ArchiveSizeBytes},
                                     {"sha256", result.ArchiveSha256},
                                     {"manifestSha256", Keire::Detail::DigestToString(Keire::Detail::Sha256(
                                                            std::span(result.ExactManifestBytes)))}};
                if (result.Signature)
                {
                    report["detachedSignature"] = {{"algorithm", result.Signature->Algorithm},
                                                   {"keyId", result.Signature->KeyId},
                                                   {"sizeBytes", result.Signature->Bytes.size()}};
                }
                else
                    report["detachedSignature"] = nullptr;
                std::cout << report.dump(2) << '\n';
                return 0;
            }
            if (commandLine.Command == "extract-asset-package")
            {
                if (commandLine.Input.empty() || commandLine.Output == std::filesystem::path("Build/Assets"))
                {
                    throw std::invalid_argument(
                        "extract-asset-package requires --input <package.keireassetpackage> and "
                        "--output <new-staging-directory>.");
                }
                Keire::AssetPackageVerification verification;
                if (commandLine.ExpectedSize != 0U)
                    verification.ExpectedArchiveSizeBytes = commandLine.ExpectedSize;
                verification.ExpectedArchiveSha256 = commandLine.Sha256;
                const auto staging = std::filesystem::absolute(commandLine.Output).lexically_normal();
                const auto parent = staging.parent_path();
                const auto result = Keire::ExtractAssetPackageToStaging({.Archive = commandLine.Input,
                                                                         .AllowedStagingParent = parent,
                                                                         .StagingRoot = staging,
                                                                         .Verification = std::move(verification)});
                auto report = nlohmann::json::parse(Keire::EncodeAssetPackageManifest(result.Metadata.Manifest));
                report["archive"] = {{"sizeBytes", result.Metadata.ArchiveSizeBytes},
                                     {"sha256", result.Metadata.ArchiveSha256},
                                     {"manifestSha256", Keire::Detail::DigestToString(Keire::Detail::Sha256(
                                                            std::span(result.Metadata.ExactManifestBytes)))}};
                report["stagingRoot"] = Keire::Detail::PathToUtf8(result.StagingRoot);
                std::cout << report.dump(2) << '\n';
                return 0;
            }
            if (commandLine.Command == "pack-player-support")
            {
                if (commandLine.Catalog.empty() || commandLine.Input.empty() ||
                    commandLine.Output == std::filesystem::path("Build/Assets"))
                    throw std::invalid_argument(
                        "pack-player-support requires --catalog <manifest.json>, --input <payload>, and --output "
                        "<module.keireplayersupport>.");
                if (commandLine.Output.extension() != ".keireplayersupport")
                    throw std::invalid_argument("Build Support packages must use the .keireplayersupport extension.");
                const auto result = Keire::Detail::CreatePlayerSupportPackage(
                    Keire::Detail::LoadPlayerSupportManifest(commandLine.Catalog), commandLine.Input,
                    commandLine.Output, commandLine.Profile.CompressionLevel);
                std::cout << "Created " << commandLine.Output.string() << " (" << result.ArchiveSize
                          << " bytes, sha256 " << result.ArchiveSha256 << ")\n";
                return 0;
            }
            if (commandLine.Command == "install-player-support")
            {
                if (commandLine.Input.empty())
                    throw std::invalid_argument("install-player-support requires --input <module.keireplayersupport>.");
                if (commandLine.ExpectedPlatform.empty() != commandLine.ExpectedArchitecture.empty())
                {
                    throw std::invalid_argument("Build Support target validation requires both --expected-platform and "
                                                "--expected-architecture.");
                }
                if (!commandLine.PackId.empty() || !commandLine.ExpectedPlatform.empty())
                {
                    const auto manifest = Keire::Detail::ReadPlayerSupportPackageManifest(commandLine.Input);
                    if (!commandLine.PackId.empty() && manifest.Id != commandLine.PackId)
                        throw std::invalid_argument("The Build Support package does not match the requested pack ID.");
                    if (!commandLine.ExpectedPlatform.empty() &&
                        (Keire::ToString(manifest.Platform) != commandLine.ExpectedPlatform ||
                         Keire::ToString(manifest.Architecture) != commandLine.ExpectedArchitecture))
                    {
                        throw std::invalid_argument("The Build Support package does not match the requested target.");
                    }
                }
                auto statusPath = commandLine.Status;
                if (!statusPath.empty())
                    statusPath = std::filesystem::absolute(statusPath).lexically_normal();
                auto cancelPath = commandLine.Cancel;
                if (!cancelPath.empty())
                    cancelPath = std::filesystem::absolute(cancelPath).lexically_normal();
                try
                {
                    WritePlayerBuildStatus(statusPath, "running", "verify", 0.0F,
                                           "Verifying the Build Support package.");
                    auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                        Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
                    float lastProgress = -1.0F;
                    std::string lastMessage;
                    const auto result = Keire::Detail::InstallPlayerSupportPackage(
                        commandLine.Input, modules->Fingerprint(),
                        {.Cancelled = [cancelPath]
                         { return !cancelPath.empty() && std::filesystem::exists(cancelPath); },
                         .Progress =
                             [&](const float progress, const std::string_view message)
                         {
                             if (progress - lastProgress < 0.01F && message == lastMessage)
                                 return;
                             lastProgress = progress;
                             lastMessage = message;
                             WritePlayerBuildStatus(statusPath, "running", "install", progress, message);
                         }});
                    WritePlayerBuildStatus(statusPath, "succeeded", "complete", 1.0F,
                                           "Build Support installed: " + result.Manifest.Id);
                    std::cout << "Installed " << result.Manifest.Id << " for "
                              << Keire::ToString(result.Manifest.Platform) << ' '
                              << Keire::ToString(result.Manifest.Architecture) << '\n';
                }
                catch (const std::exception& error)
                {
                    LogBuildSupportFailure("Build Support installation failed", error);
                    const auto failure = Keire::Detail::DescribePlayerSupportFailure(
                        Keire::Detail::PlayerSupportFailureKind::InstallationFailed);
                    try
                    {
                        WritePlayerBuildStatus(statusPath, "failed", "failed", 1.0F, failure.Message, {}, {},
                                               failure.Code);
                    }
                    catch (...)
                    {
                    }
                    throw;
                }
                return 0;
            }
            if (commandLine.Command == "fetch-player-support-catalog")
            {
                if (commandLine.Output == std::filesystem::path("Build/Assets"))
                    throw std::invalid_argument("fetch-player-support-catalog requires --output <catalog.json>.");
                const auto cancelled = [path = commandLine.Cancel]
                { return !path.empty() && std::filesystem::exists(path); };
                try
                {
                    WritePlayerBuildStatus(commandLine.Status, "running", "download", 0.0F,
                                           "Downloading the Build Support catalog.");
                    const auto catalog = Keire::Detail::FetchPlayerSupportCatalog(
                        Keire::GetBuildInfo().RepositorySlug, Keire::GetBuildInfo().Version, commandLine.Output,
                        {.Cancelled = cancelled,
                         .Progress = [&](const float progress, const std::string_view message)
                         { WritePlayerBuildStatus(commandLine.Status, "running", "download", progress, message); }});
                    WritePlayerBuildStatus(commandLine.Status, "succeeded", "complete", 1.0F,
                                           "Build Support catalog downloaded (" +
                                               std::to_string(catalog.Packages.size()) + " modules).");
                }
                catch (const std::exception& error)
                {
                    LogBuildSupportFailure("Build Support catalog download failed", error);
                    const auto failure = Keire::Detail::DescribePlayerSupportFailure(
                        Keire::Detail::PlayerSupportFailureKind::CatalogUnavailable);
                    try
                    {
                        WritePlayerBuildStatus(commandLine.Status, "failed", "failed", 1.0F, failure.Message, {}, {},
                                               failure.Code);
                    }
                    catch (...)
                    {
                    }
                    throw;
                }
                return 0;
            }
            if (commandLine.Command == "download-install-player-support")
            {
                if (commandLine.Url.empty() || commandLine.ExpectedSize == 0 || commandLine.Sha256.empty() ||
                    commandLine.Output == std::filesystem::path("Build/Assets"))
                    throw std::invalid_argument(
                        "download-install-player-support requires --url, --size, --sha256, and --output.");
                const auto cancelled = [path = commandLine.Cancel]
                { return !path.empty() && std::filesystem::exists(path); };
                try
                {
                    WritePlayerBuildStatus(commandLine.Status, "running", "download", 0.0F,
                                           "Downloading Build Support.");
                    Keire::Detail::PlayerSupportCatalogEntry entry{
                        .Url = commandLine.Url, .Size = commandLine.ExpectedSize, .Sha256 = commandLine.Sha256};
                    Keire::Detail::DownloadPlayerSupportPackage(
                        entry, commandLine.Output,
                        {.Cancelled = cancelled,
                         .Progress = [&](const float progress, const std::string_view message)
                         {
                             WritePlayerBuildStatus(commandLine.Status, "running", "download", progress * 0.6F,
                                                    message);
                         }});
                    auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                        Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
                    const auto installed = Keire::Detail::InstallPlayerSupportPackage(
                        commandLine.Output, modules->Fingerprint(),
                        {.Cancelled = cancelled,
                         .Progress = [&](const float progress, const std::string_view message)
                         {
                             WritePlayerBuildStatus(commandLine.Status, "running", "install", 0.6F + progress * 0.4F,
                                                    message);
                         }});
                    std::error_code ignored;
                    std::filesystem::remove(commandLine.Output, ignored);
                    WritePlayerBuildStatus(commandLine.Status, "succeeded", "complete", 1.0F,
                                           "Build Support installed: " + installed.Manifest.Id);
                }
                catch (const std::exception& error)
                {
                    LogBuildSupportFailure("Build Support download/install failed", error);
                    const auto failure = Keire::Detail::DescribePlayerSupportFailure(
                        Keire::Detail::PlayerSupportFailureKind::DownloadAndInstallationFailed);
                    std::error_code ignored;
                    std::filesystem::remove(commandLine.Output, ignored);
                    try
                    {
                        WritePlayerBuildStatus(commandLine.Status, "failed", "failed", 1.0F, failure.Message, {}, {},
                                               failure.Code);
                    }
                    catch (...)
                    {
                    }
                    throw;
                }
                return 0;
            }
            if (commandLine.Command == "verify-player-support")
            {
                if (commandLine.Input.empty())
                    throw std::invalid_argument("verify-player-support requires --input <module.keireplayersupport>.");
                auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                    Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
                const auto verificationRoot = std::filesystem::temp_directory_path() /
                                              ("keire-player-support-verify-" + Keire::AssetId::Generate().ToString());
                try
                {
                    const auto result = Keire::Detail::InstallPlayerSupportPackage(
                        commandLine.Input, modules->Fingerprint(), {}, verificationRoot);
                    std::string diagnostic;
                    const auto installation = verificationRoot / result.Manifest.EngineVersion / result.Manifest.Id;
                    if (!Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic))
                        throw std::runtime_error(diagnostic);
                    std::error_code cleanupError;
                    std::filesystem::remove_all(verificationRoot, cleanupError);
                    std::cout << "Verified " << result.Manifest.Id << " (" << result.ArchiveSize << " bytes, sha256 "
                              << result.ArchiveSha256 << ")\n";
                }
                catch (...)
                {
                    std::error_code cleanupError;
                    std::filesystem::remove_all(verificationRoot, cleanupError);
                    throw;
                }
                return 0;
            }
            if (commandLine.Command == "list-player-support")
            {
                for (const auto& installed : Keire::Detail::InstalledPlayerSupport())
                {
                    std::string diagnostic;
                    const auto root = Keire::Detail::PlayerSupportStorageRoot() / installed.Manifest.EngineVersion /
                                      Keire::Detail::PathFromUtf8(installed.Manifest.Id);
                    const auto healthy = Keire::Detail::ValidateInstalledPlayerSupport(root, diagnostic);
                    std::cout << installed.Manifest.Id << '\t' << Keire::ToString(installed.Manifest.Platform) << '\t'
                              << Keire::ToString(installed.Manifest.Architecture) << '\t' << installed.ArchiveSize
                              << " bytes\t" << (healthy ? "installed" : "repair required") << '\n';
                }
                return 0;
            }
            if (commandLine.Command == "describe-player-support-host")
            {
                auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                    Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
                nlohmann::json sourceModules = nlohmann::json::array();
                for (const auto& module : modules->OrderedCatalog())
                    sourceModules.push_back(module.Id);
                std::cout << nlohmann::json{{"engineVersion", Keire::GetBuildInfo().Version},
                                            {"playerAbi", Keire::Detail::PlayerBuildAbiVersion},
                                            {"platform", Keire::ToString(Keire::HostPlayerPlatform())},
                                            {"architecture", Keire::ToString(Keire::HostPlayerArchitecture())},
                                            {"storageRoot",
                                             Keire::Detail::PathToUtf8(Keire::Detail::PlayerSupportStorageRoot())},
                                            {"moduleFingerprint", modules->Fingerprint()},
                                            {"sourceModules", std::move(sourceModules)}}
                                 .dump()
                          << '\n';
                return 0;
            }
            if (commandLine.Command == "remove-player-support")
            {
                if (commandLine.EngineVersion.empty() || commandLine.PackId.empty())
                    throw std::invalid_argument(
                        "remove-player-support requires --engine-version <version> and --pack-id <id>.");
                if (commandLine.EngineVersion != Keire::GetBuildInfo().Version)
                    throw std::invalid_argument("The selected Asset Tool cannot remove another editor version's "
                                                "Build Support.");
                Keire::Detail::RemovePlayerSupport(commandLine.EngineVersion, commandLine.PackId);
                std::cout << "Removed " << commandLine.PackId << '\n';
                return 0;
            }
            if (commandLine.Command == "convert-mesh")
            {
                if (commandLine.Input.empty())
                    throw std::invalid_argument("convert-mesh requires --input <model>.");
                const auto input = std::filesystem::absolute(commandLine.Input);
                std::ifstream source(input, std::ios::binary);
                if (!source)
                    throw std::runtime_error("Cannot open mesh source: " + input.string());
                const std::vector<char> characters{std::istreambuf_iterator<char>(source),
                                                   std::istreambuf_iterator<char>()};
                std::vector<std::byte> bytes(characters.size());
                std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
                const auto importer = Keire::CreateMeshAssetImporter();
                Keire::AssetImportContext context;
                context.ProjectRoot = input.parent_path();
                context.SourceRoot = input.parent_path();
                context.SourcePath = input;
                context.RelativePath = input.filename();
                Keire::Detail::AnchoredFileSystem modelFiles(input.parent_path());
                context.ReadProjectFile =
                    [&modelFiles, maximum = context.MaximumDependencyBytes](const std::filesystem::path& relative)
                { return modelFiles.Read(relative, maximum); };
                const auto imported = importer.ContextualImport(context, bytes);
                auto output = commandLine.Output;
                if (output == std::filesystem::path("Build/Assets"))
                {
                    output = input;
                    output.replace_extension(".keiremesh");
                }
                if (output.has_parent_path())
                    std::filesystem::create_directories(output.parent_path());
                std::ofstream destination(output, std::ios::binary | std::ios::trunc);
                if (!destination || (!imported.Bytes.empty() &&
                                     !destination.write(reinterpret_cast<const char*>(imported.Bytes.data()),
                                                        static_cast<std::streamsize>(imported.Bytes.size()))))
                    throw std::runtime_error("Cannot write converted mesh: " + output.string());
                std::cout << "Converted " << input.string() << " to " << output.string() << '\n';
                return 0;
            }
            if (commandLine.Command == "upgrade-project")
            {
                const auto actionCount = static_cast<unsigned>(commandLine.ApplyUpgrade) +
                                         static_cast<unsigned>(commandLine.RecoverUpgrade) +
                                         static_cast<unsigned>(commandLine.RollbackUpgrade);
                if (actionCount > 1)
                    throw std::invalid_argument(
                        "upgrade-project accepts only one of --apply, --recover, or --rollback.");
                auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                    Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
                Keire::ProjectUpgradeService upgrades(commandLine.Project, modules->ProjectUpgrades());
                if (upgrades.State() == Keire::ProjectUpgradeTransactionState::Interrupted)
                {
                    if (commandLine.RecoverUpgrade)
                    {
                        upgrades.Recover();
                        std::cout << "Recovered the interrupted project upgrade.\n";
                    }
                    else if (commandLine.RollbackUpgrade)
                    {
                        upgrades.Rollback();
                        std::cout << "Rolled back the interrupted project upgrade.\n";
                    }
                    else
                    {
                        if (commandLine.ApplyUpgrade)
                            throw std::invalid_argument("Use --recover or --rollback for an interrupted upgrade.");
                        std::cout << "Interrupted upgrade detected. Re-run with --recover or --rollback.\n";
                    }
                    return 0;
                }
                if (commandLine.RecoverUpgrade || commandLine.RollbackUpgrade)
                    throw std::invalid_argument("The project has no interrupted upgrade transaction.");
                const auto plan = upgrades.Plan();
                std::cout << "Project schema " << plan.CurrentSchema << " -> " << plan.TargetSchema << '\n';
                std::cout << "Estimated backup: " << plan.EstimatedBackupBytes << " bytes\n";
                for (const auto& step : plan.Steps)
                {
                    std::cout << "  " << step.Id << " (" << step.FromSchema << " -> " << step.ToSchema << ")\n";
                    for (const auto& path : step.AffectedPaths)
                        std::cout << "    " << path.generic_string() << '\n';
                    if (!step.Warning.empty())
                        std::cout << "    warning: " << step.Warning << '\n';
                }
                if (plan.Steps.empty())
                    std::cout << "Project schema is current.\n";
                else if (commandLine.ApplyUpgrade)
                {
                    upgrades.Apply(plan);
                    std::cout << "Project upgrade completed.\n";
                }
                else
                    std::cout << "Dry run only; re-run with --apply to publish this plan.\n";
                return 0;
            }

            if (commandLine.Command == "validate-project" && !commandLine.ProjectSpecified)
                throw std::invalid_argument("validate-project requires --project <path>.");
            if (commandLine.Command == "migrate-shader-graphs")
            {
                if (!commandLine.ProjectSpecified)
                    throw std::invalid_argument("migrate-shader-graphs requires --project <path>.");
                const auto report = commandLine.CheckOnly ? Keire::InspectShaderGraphMigration(commandLine.Project)
                                                          : Keire::ApplyShaderGraphMigration(commandLine.Project);
                for (const auto& item : report.Items)
                {
                    const auto disposition =
                        item.Disposition == Keire::ShaderGraphMigrationDisposition::Migrate
                            ? (commandLine.CheckOnly ? "migrate" : "migrated")
                        : item.Disposition == Keire::ShaderGraphMigrationDisposition::AlreadyMigrated ? "current"
                        : item.Disposition == Keire::ShaderGraphMigrationDisposition::Conflict        ? "conflict"
                                                                                                      : "invalid";
                    std::cout << disposition << ": " << item.MaterialGraph.generic_string();
                    if (item.Disposition == Keire::ShaderGraphMigrationDisposition::Migrate)
                        std::cout << " -> " << item.ShaderGraph.generic_string();
                    if (!item.Diagnostic.empty())
                        std::cout << " (" << item.Diagnostic << ')';
                    std::cout << '\n';
                }
                std::cout << (commandLine.CheckOnly ? "Migration check: " : "Migration complete: ")
                          << report.PendingCount() << " legacy Material Graph asset(s).\n";
                return report.CanApply() ? 0 : 2;
            }
            auto modules = Keire::CreateRef<Keire::ModuleRegistry>(
                Keire::ModuleRegistrySpecification{KeireProjectModules::CreateSourceModules()});
            const auto project = Keire::Project::Open(commandLine.Project, Keire::ProjectOpenMode::ReadOnly);
            modules->ValidateRequired(project->Descriptor().RequiredModules);
            if (commandLine.Command == "validate-project")
            {
                std::cout << "Validated project " << project->Root().string() << " with Kéire "
                          << Keire::GetBuildInfo().Version << ".\n";
                return 0;
            }
            const auto executable = std::filesystem::absolute(Keire::Detail::PathFromUtf8(argv[0])).lexically_normal();
            Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = project->Root()};
            databaseSpecification.Jobs = Keire::CreateRef<Keire::JobSystem>();
            databaseSpecification.Importers = Keire::CreateBuiltinAssetImporters();
            for (auto& importer : modules->Importers())
            {
                if (std::ranges::find(databaseSpecification.Importers, importer.Name,
                                      &Keire::AssetImporterRegistration::Name) != databaseSpecification.Importers.end())
                    throw std::invalid_argument("A source module importer duplicates an existing importer: " +
                                                importer.Name);
                databaseSpecification.Importers.push_back(std::move(importer));
            }
            auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            if (commandLine.Command == "build-player")
            {
                auto statusPath = commandLine.Status;
                if (!statusPath.empty() && statusPath.is_relative())
                    statusPath = project->Root() / statusPath;
                if (!statusPath.empty())
                    statusPath = std::filesystem::absolute(statusPath).lexically_normal();
                const auto outputRoot = project->Root() / "Build";
                auto stagingToken = commandLine.StagingId;
                if (stagingToken.empty())
                {
                    stagingToken = Keire::AssetId::Generate().ToString();
                    std::erase(stagingToken, '-');
                    stagingToken.resize(12);
                }
                const auto staging = outputRoot / ".staging" / stagingToken;
                std::error_code cleanupError;
                try
                {
                    WritePlayerBuildStatus(statusPath, "running", "validate", 0.02F,
                                           "Validating player profile and Build Support.");
                    const auto profiles = Keire::LoadPlayerBuildProfiles(project->Root());
                    const auto profile = SelectPlayerProfile(profiles, commandLine.PlayerProfile);
                    const auto settings = Keire::LoadPlayerSettings(project->Root(), project->Descriptor());
                    const auto support =
                        Keire::Detail::ResolvePlayerSupport(executable, profile.Platform, profile.Architecture,
                                                            profile.Configuration, modules->Fingerprint());
                    const auto output = std::filesystem::absolute(outputRoot / profile.OutputSlug).lexically_normal();
                    std::filesystem::remove_all(staging, cleanupError);
                    if (cleanupError)
                        throw std::filesystem::filesystem_error("Could not prepare player build staging.", staging,
                                                                cleanupError);

                    WritePlayerBuildStatus(statusPath, "running", "import", 0.10F,
                                           "Importing project assets in the isolated worker.", output);
                    (void)ImportAssetsWithWorker(project->Root(), executable, commandLine.WorkerTimeout);
                    (void)database->Refresh();
                    const auto buildScenes = ResolvePlayerBuildScenes(*project, *database);

                    const auto iconId = profile.Platform == Keire::PlayerPlatform::Windows ? settings.WindowsIcon
                                        : profile.Platform == Keire::PlayerPlatform::Linux ? settings.LinuxIcon
                                                                                           : settings.MacOSIcon;
                    std::vector<std::byte> iconSource;
                    if (iconId)
                    {
                        const auto record = database->Find(iconId);
                        if (!record || record->Type != Keire::Texture2DAsset::StaticType())
                            throw std::runtime_error(
                                "The selected player icon is missing or is not a Texture2D asset.");
                        const auto sourcePath = project->AssetsDirectory() / record->RelativePath;
                        if (std::filesystem::file_size(sourcePath) > std::uintmax_t{64U} * 1024U * 1024U)
                            throw std::runtime_error("The selected player icon exceeds 64 MiB.");
                        iconSource = ReadBytes(sourcePath);
                    }

                    WritePlayerBuildStatus(statusPath, "running", "assemble", 0.22F,
                                           "Staging and branding the target player template.", output);
                    const auto layout =
                        Keire::Detail::AssemblePlayerPackage(support, settings, profile, staging, iconSource);

                    Keire::AssetBuildProfile cookProfile;
                    cookProfile.Name = AssetProfileName(profile.Configuration);
                    cookProfile.Target = AssetTarget(profile.Platform);
                    cookProfile.Strict = true;
                    cookProfile.CompressionLevel =
                        profile.Configuration == Keire::PlayerBuildConfiguration::Development ? 3 : 9;
                    cookProfile.Roots = buildScenes;
                    if (project->Descriptor().DefaultInput)
                        cookProfile.Roots.push_back(project->Descriptor().DefaultInput);
                    const auto authoring = Keire::LoadProjectAuthoringSettings(project->Root());
                    if (authoring.DefaultMixer)
                        cookProfile.Roots.push_back(authoring.DefaultMixer);

                    WritePlayerBuildStatus(statusPath, "running", "scripts", 0.40F,
                                           "Building runtime managed assemblies.", output);
                    const bool containsManagedData =
                        std::ranges::any_of(database->Records(), [](const Keire::AssetSourceRecord& record)
                                            { return record.Type == Keire::ManagedDataAsset::StaticType(); });
                    const auto managed =
                        BuildManagedAssemblies(*database, *project, cookProfile.Name, executable, containsManagedData);
                    if (containsManagedData)
                    {
                        cookProfile.ManagedTypeDiscoveryComplete = true;
                        cookProfile.ManagedTypeCatalog = Keire::EncodeManagedAssetTypeCatalog(managed.AssetTypes);
                    }

                    WritePlayerBuildStatus(statusPath, "running", "cook", 0.58F, "Cooking strict target content.",
                                           output);
                    const auto cooked = Keire::AssetCooker::Cook(*database, cookProfile, layout.Content);
                    Keire::AssetCooker::Validate(cooked.CatalogPath);
                    CopyManagedAssemblies(managed, layout.Content);
                    WriteRuntimeManifest(*project, layout.Content, *modules, managed.Scripting, buildScenes, &profile);

                    WritePlayerBuildStatus(statusPath, "running", "sign", 0.82F,
                                           "Applying the configured signing policy.", output);
                    Keire::Detail::RunPlayerSigningHook(project->Root(), settings, profile, layout);
                    if (!Keire::Detail::LoadPackagedPlayerConfiguration(layout.Executable))
                        throw std::runtime_error("Staged player did not publish its build descriptor.");

                    WritePlayerBuildStatus(statusPath, "running", "publish", 0.94F,
                                           "Publishing the completed player atomically.", output);
                    const auto executableRelative = layout.Executable.lexically_relative(layout.Root);
                    Keire::Detail::PublishPlayerPackage(layout.Root, output);
                    const auto publishedExecutable = output / executableRelative;
                    std::filesystem::remove(outputRoot / ".staging", cleanupError);
                    WritePlayerBuildStatus(statusPath, "succeeded", "complete", 1.0F, "Player build completed.", output,
                                           publishedExecutable);
                    std::cout << "Built " << settings.ProductName << " for " << Keire::ToString(profile.Platform) << ' '
                              << Keire::ToString(profile.Architecture) << ": "
                              << Keire::Detail::PathToUtf8(publishedExecutable) << '\n';
                }
                catch (const std::exception& error)
                {
                    std::filesystem::remove_all(staging, cleanupError);
                    try
                    {
                        WritePlayerBuildStatus(statusPath, "failed", "failed", 1.0F, error.what());
                    }
                    catch (...)
                    {
                    }
                    throw;
                }
            }
            else if (commandLine.Command == "scan")
            {
                std::cout << "Discovered " << database->Refresh() << " assets.\n";
            }
            else if (commandLine.Command == "import")
            {
                const auto result = ImportAssetsWithWorker(project->Root(), executable, commandLine.WorkerTimeout);
                std::cout << "Imported " << result.Imported << " assets (" << result.CacheHits
                          << " cache hits). Catalog: " << result.CatalogPath.string() << '\n';
            }
            else if (commandLine.Command == "cook")
            {
                (void)ImportAssetsWithWorker(project->Root(), executable, commandLine.WorkerTimeout);
                (void)database->Refresh();
                const auto buildScenes = ResolvePlayerBuildScenes(*project, *database);
                commandLine.Profile.Roots = buildScenes;
                if (project->Descriptor().DefaultInput)
                    commandLine.Profile.Roots.push_back(project->Descriptor().DefaultInput);
                const auto authoring = Keire::LoadProjectAuthoringSettings(project->Root());
                if (authoring.DefaultMixer)
                    commandLine.Profile.Roots.push_back(authoring.DefaultMixer);
                auto output = commandLine.Output;
                if (output.is_relative())
                    output = commandLine.Project / output;
                output = std::filesystem::absolute(output).lexically_normal();
                const bool containsManagedData =
                    std::ranges::any_of(database->Records(), [](const Keire::AssetSourceRecord& record)
                                        { return record.Type == Keire::ManagedDataAsset::StaticType(); });
                const auto managed = BuildManagedAssemblies(*database, *project, commandLine.Profile.Name, executable,
                                                            commandLine.Profile.Strict && containsManagedData);
                if (commandLine.Profile.Strict && containsManagedData)
                {
                    commandLine.Profile.ManagedTypeDiscoveryComplete = true;
                    commandLine.Profile.ManagedTypeCatalog = Keire::EncodeManagedAssetTypeCatalog(managed.AssetTypes);
                }

                auto stagingToken = Keire::AssetId::Generate().ToString();
                std::erase(stagingToken, '-');
                stagingToken.resize(20);
                const auto staging = Keire::Detail::PathWithSuffix(output, ".runtime-staging-" + stagingToken);
                std::error_code cleanupError;
                std::filesystem::remove_all(staging, cleanupError);
                if (cleanupError)
                    throw std::filesystem::filesystem_error("Could not prepare runtime cook staging.", staging,
                                                            cleanupError);
                Keire::AssetCookResult result;
                try
                {
                    result = Keire::AssetCooker::Cook(*database, commandLine.Profile, staging);
                    Keire::AssetCooker::Validate(result.CatalogPath);
                    CopyManagedAssemblies(managed, staging);
                    WriteRuntimeManifest(*project, staging, *modules, managed.Scripting, buildScenes);
                    PublishCookOutput(staging, output);
                }
                catch (...)
                {
                    std::filesystem::remove_all(staging, cleanupError);
                    throw;
                }
                result.CatalogPath = output / "catalog.json";
                std::cout << "Cooked " << result.AssetCount << " assets into " << result.PackCount
                          << " pack(s). Catalog: " << result.CatalogPath.string() << '\n';
            }
            else if (commandLine.Command == "bake-lighting")
            {
                (void)ImportAssetsWithWorker(project->Root(), executable, commandLine.WorkerTimeout);
                (void)database->Refresh();
                std::optional<Keire::AssetSourceRecord> sceneRecord;
                if (commandLine.Input.empty())
                {
                    if (!project->Descriptor().StartupScene)
                        throw std::invalid_argument(
                            "bake-lighting requires --input when the project has no startup scene.");
                    sceneRecord = database->Find(project->Descriptor().StartupScene);
                }
                else
                {
                    auto relative = commandLine.Input.lexically_normal();
                    if (relative.is_absolute())
                        relative = std::filesystem::relative(relative, project->Root() / "Assets");
                    if (!relative.empty() && *relative.begin() == std::filesystem::path("Assets"))
                        relative = std::filesystem::relative(relative, "Assets");
                    sceneRecord = database->Find(relative);
                }
                if (!sceneRecord || sceneRecord->Type != Keire::SceneAsset::StaticType())
                    throw std::invalid_argument("bake-lighting input does not resolve to an imported scene asset.");
                const auto scenePath = project->Root() / "Assets" / sceneRecord->RelativePath;
                const auto source = Keire::SceneAsset::Decode(ReadBytes(scenePath));
                Keire::LightingBakeRequest request;
                request.Scene = sceneRecord->Id;
                request.Definition = source->Definition();
                request.ProjectRoot = project->Root();
                request.Force = commandLine.Force;
                if (commandLine.Output != std::filesystem::path("Build/Assets"))
                    request.OutputDirectory = commandLine.Output;
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
                    request.Inputs.push_back({id, found->second.SourceDigest});
                    pending.insert(pending.end(), found->second.Dependencies.begin(), found->second.Dependencies.end());
                }
                request.Progress = [](const Keire::LightingBakeProgress& progress)
                {
                    std::cout << "[lighting] " << progress.Message;
                    if (progress.Total != 0U)
                        std::cout << " (" << progress.Completed << '/' << progress.Total << ')';
                    std::cout << '\n';
                };
                const auto baked = Keire::LightingBaker::Bake(request);
                auto updated = source->Definition();
                updated.BakedLighting = baked.LightingSet;
                Keire::Detail::WriteFileAtomically(scenePath, Keire::SceneAsset::Encode(updated));
                (void)ImportAssetsWithWorker(project->Root(), executable, commandLine.WorkerTimeout);
                std::cout << "Baked " << baked.Assets.size() << " lighting assets for "
                          << sceneRecord->RelativePath.string() << (baked.CacheHit ? " (cache hit).\n" : ".\n");
            }
            else
            {
                throw std::invalid_argument("Unknown command: " + commandLine.Command);
            }
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: " << error.what() << '\n';
            try
            {
                KEIRE_CORE_ERROR("Asset tool failed: {}", error.what());
                Keire::Log::Flush();
            }
            catch (...)
            {
            }
            return 1;
        }
    }
} // namespace

int main(const int argc, char* argv[])
{
    try
    {
        Keire::Detail::Utf8CommandLine commandLine(argc, argv);
        return RunAssetTool(commandLine.Count(), commandLine.Values());
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
