#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/BuildInfo.h"
#include "Keire/Log.h"
#include "Keire/Project/Project.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ScriptSystem.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
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
        std::filesystem::path Input;
        Keire::AssetBuildProfile Profile;
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
                result.Project = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--output")
                result.Output = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--catalog")
                result.Catalog = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--input")
                result.Input = Keire::Detail::PathFromUtf8(requireValue());
            else if (option == "--profile")
            {
                result.Profile.Name = requireValue();
                result.Profile.Strict = result.Profile.Name == "Dist";
            }
            else if (option == "--compression-level")
                result.Profile.CompressionLevel =
                    static_cast<int>(ParseUnsigned(requireValue(), "--compression-level"));
            else if (option == "--pack-mib")
                result.Profile.MaximumPackBytes = ParseUnsigned(requireValue(), "--pack-mib") * 1024ULL * 1024ULL;
            else if (option == "--target")
                result.Profile.Target = ParseTarget(requireValue());
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
                     "  KeireAssetTool import [--project <path>]\n"
                     "  KeireAssetTool cook [--project <path>] [--output <path>] [--profile <name>]\n"
                     "                      [--compression-level <level>] [--pack-mib <size>]\n"
                     "                      [--target <host|windows|linux|macos>]\n"
                     "  KeireAssetTool validate --catalog <path>\n";
        std::cout << "  KeireAssetTool convert-mesh --input <model> [--output <file.keiremesh>]\n";
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Could not read managed assembly definition: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        std::vector<std::byte> result(characters.size());
        std::ranges::transform(characters, result.begin(), [](const char value) { return std::byte(value); });
        return result;
    }

    [[nodiscard]] bool BuildManagedAssemblies(const Keire::AssetDatabase& database, const Keire::Project& project,
                                              const std::filesystem::path& output, const std::string& profile,
                                              const std::filesystem::path& executable)
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
            return false;
        std::vector<std::string> assemblyNames;
        assemblyNames.reserve(request.Assemblies.size());
        for (const auto& assembly : request.Assemblies)
            assemblyNames.push_back(assembly.Definition.Name);

        Keire::ScriptSystemSpecification specification;
        specification.Mode = Keire::ScriptMode::Enabled;
        specification.ProjectRoot = project.Root();
        specification.ManagedApiAssembly = executable.parent_path() / "Managed" / "Keire.Managed.dll";
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

        const auto destination = output / "ManagedAssemblies";
        const auto staging = output / ".managed-staging";
        std::error_code error;
        std::filesystem::remove_all(staging, error);
        if (error)
            throw std::filesystem::filesystem_error("Could not clear managed assembly staging.", staging, error);
        std::filesystem::create_directories(staging);
        for (const auto& entry : std::filesystem::directory_iterator(status.ActiveAssemblyDirectory))
        {
            const auto filename = entry.path().filename().string();
            const bool requested = std::ranges::any_of(
                assemblyNames, [&](const std::string& name)
                { return filename == name + ".dll" || filename == name + ".pdb" || filename == name + ".deps.json"; });
            if (entry.is_regular_file() && requested)
                std::filesystem::copy_file(entry.path(), staging / entry.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
        }
        if (std::filesystem::is_empty(staging))
            throw std::runtime_error("Managed gameplay build published no runtime assemblies.");
        std::filesystem::remove_all(destination, error);
        if (error)
            throw std::filesystem::filesystem_error("Could not replace managed assembly output.", destination, error);
        std::filesystem::rename(staging, destination);
        scripts->Close();
        return true;
    }

    void WriteRuntimeManifest(const Keire::Project& project, const std::filesystem::path& output, const bool scripting)
    {
        const auto& descriptor = project.Descriptor();
        if (!descriptor.StartupScene)
            throw std::runtime_error("Runtime cooking requires a configured startup scene.");
        const auto rendering = Keire::LoadRenderEnvironmentSettings(project.Root());
        const auto& build = Keire::GetBuildInfo();
        nlohmann::json manifest{
            {"schemaVersion", 2},
            {"startupScene", descriptor.StartupScene.ToString()},
            {"defaultInput",
             descriptor.DefaultInput ? nlohmann::json(descriptor.DefaultInput.ToString()) : nlohmann::json(nullptr)},
            {"buildIdentity",
             {{"engineVersion", build.Version},
              {"configuration", build.Configuration},
              {"platform", build.Platform},
              {"architecture", build.Architecture}}},
            {"managedAssemblyRoots",
             scripting ? nlohmann::json::array({"ManagedAssemblies"}) : nlohmann::json::array()},
            {"subsystems", {{"scripting", scripting}, {"physics", true}, {"audio", true}, {"navigation", true}}},
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

            const auto project = Keire::Project::Open(commandLine.Project);
            Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = project->Root()};
            databaseSpecification.Importers = {Keire::CreateTextAssetImporter(),
                                               Keire::CreateInputActionAssetImporter(),
                                               Keire::CreateSceneAssetImporter(),
                                               Keire::CreatePrefabAssetImporter(),
                                               Keire::CreateManagedAssemblyAssetImporter(),
                                               Keire::CreateShaderAssetImporter(),
                                               Keire::CreateMaterialAssetImporter(),
                                               Keire::CreateMeshAssetImporter(),
                                               Keire::CreateTexture2DAssetImporter()};
            auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            if (commandLine.Command == "scan")
            {
                std::cout << "Discovered " << database->Refresh() << " assets.\n";
            }
            else if (commandLine.Command == "import")
            {
                const auto result = database->ImportAll();
                std::cout << "Imported " << result.Imported << " assets (" << result.CacheHits
                          << " cache hits). Catalog: " << result.CatalogPath.string() << '\n';
            }
            else if (commandLine.Command == "cook")
            {
                (void)database->Refresh();
                commandLine.Profile.Roots = {project->Descriptor().StartupScene};
                if (project->Descriptor().DefaultInput)
                    commandLine.Profile.Roots.push_back(project->Descriptor().DefaultInput);
                auto output = commandLine.Output;
                if (output.is_relative())
                    output = commandLine.Project / output;
                const auto result = Keire::AssetCooker::Cook(*database, commandLine.Profile, output);
                Keire::AssetCooker::Validate(result.CatalogPath);
                const auto scripting = BuildManagedAssemblies(
                    *database, *project, result.CatalogPath.parent_path(), commandLine.Profile.Name,
                    std::filesystem::absolute(Keire::Detail::PathFromUtf8(argv[0])));
                WriteRuntimeManifest(*project, result.CatalogPath.parent_path(), scripting);
                std::cout << "Cooked " << result.AssetCount << " assets into " << result.PackCount
                          << " pack(s). Catalog: " << result.CatalogPath.string() << '\n';
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
