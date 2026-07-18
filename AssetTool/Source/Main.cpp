#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Project/Project.h"
#include "Keire/Scenes/SceneAsset.h"

#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    struct CommandLine
    {
        std::string Command;
        std::filesystem::path Project = ".";
        std::filesystem::path Output = "Build/Assets";
        std::filesystem::path Catalog;
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

    [[nodiscard]] CommandLine Parse(const int argc, char** argv)
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
                result.Project = requireValue();
            else if (option == "--output")
                result.Output = requireValue();
            else if (option == "--catalog")
                result.Catalog = requireValue();
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
                     "  KeireAssetTool validate --catalog <path>\n";
    }
} // namespace

int main(const int argc, char** argv)
{
    try
    {
        const auto commandLine = Parse(argc, argv);
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

        const auto project = Keire::Project::Open(commandLine.Project);
        Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = project->Root()};
        databaseSpecification.Importers = {Keire::CreateInputActionAssetImporter(), Keire::CreateSceneAssetImporter()};
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
            auto output = commandLine.Output;
            if (output.is_relative())
                output = commandLine.Project / output;
            const auto result = Keire::AssetCooker::Cook(*database, commandLine.Profile, output);
            Keire::AssetCooker::Validate(result.CatalogPath);
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
        return 1;
    }
}
