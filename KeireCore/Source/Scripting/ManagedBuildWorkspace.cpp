#include "KeireInternal/Scripting/ManagedBuildWorkspace.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <regex>
#include <stdexcept>

namespace Keire
{
    namespace Detail
    {
        namespace
        {
            [[nodiscard]] std::string PathText(const std::filesystem::path& path)
            {
                const auto value = path.generic_u8string();
                return {reinterpret_cast<const char*>(value.data()), value.size()};
            }
            [[nodiscard]] std::string XmlEscape(const std::string_view value)
            {
                std::string result;
                result.reserve(value.size());
                for (const char character : value)
                {
                    switch (character)
                    {
                    case '&':
                        result += "&amp;";
                        break;
                    case '<':
                        result += "&lt;";
                        break;
                    case '>':
                        result += "&gt;";
                        break;
                    case '\"':
                        result += "&quot;";
                        break;
                    default:
                        result.push_back(character);
                        break;
                    }
                }
                return result;
            }
        } // namespace

        void WriteText(const std::filesystem::path& path, const std::string_view value)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(value.data(), static_cast<std::streamsize>(value.size())))
                throw std::runtime_error("Managed build could not write '" + PathText(path) + "'.");
        }
        [[nodiscard]] std::vector<ManagedBuildDiagnostic> ParseDiagnostics(const std::string& output,
                                                                           const std::size_t maximum)
        {
            static const std::regex pattern(
                R"(^(.+?)\(([0-9]+),([0-9]+)\): (warning|error) ([A-Za-z]+[0-9]+): (.+?)(?: \[.+\])?\r?$)");
            std::vector<ManagedBuildDiagnostic> result;
            std::size_t begin = 0;
            while (begin < output.size() && result.size() < maximum)
            {
                const auto end = output.find('\n', begin);
                const std::string line = output.substr(begin, end - begin);
                std::smatch match;
                if (std::regex_match(line, match, pattern))
                {
                    result.push_back(
                        {match[4] == "warning" ? ManagedDiagnosticSeverity::Warning : ManagedDiagnosticSeverity::Error,
                         std::filesystem::path(match[1].str()), static_cast<std::uint32_t>(std::stoul(match[2].str())),
                         static_cast<std::uint32_t>(std::stoul(match[3].str())), match[5].str(), match[6].str()});
                }
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return result;
        }
        [[nodiscard]] std::string ManagedApiSourceFingerprint(const std::filesystem::path& project)
        {
            const auto sourceRoot = project.parent_path();
            std::vector<std::filesystem::path> sources{project};
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator
                     iterator(sourceRoot, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 iterator != end;)
            {
                if (error)
                {
                    error.clear();
                    iterator.increment(error);
                    continue;
                }
                const auto& entry = *iterator;
                if (entry.is_directory(error) && (entry.path().filename() == "bin" || entry.path().filename() == "obj"))
                {
                    iterator.disable_recursion_pending();
                }
                else if (entry.is_regular_file(error) && entry.path().extension() == ".cs")
                {
                    sources.push_back(entry.path());
                }
                error.clear();
                iterator.increment(error);
            }
            std::ranges::sort(sources);
            std::string fingerprint;
            for (const auto& source : sources)
            {
                error.clear();
                const auto relative = std::filesystem::relative(source, sourceRoot, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not fingerprint managed API source.", source, error);
                const auto size = std::filesystem::file_size(source, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not read managed API source size.", source, error);
                const auto writeTime = std::filesystem::last_write_time(source, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not read managed API source timestamp.", source,
                                                            error);
                fingerprint += PathText(relative) + "|" + std::to_string(size) + "|" +
                               std::to_string(writeTime.time_since_epoch().count()) + "\n";
            }
            return fingerprint;
        }
        [[nodiscard]] std::string
        GenerateProject(const ManagedAssemblyGraphEntry& assembly, const std::map<AssetId, std::string>& names,
                        const std::filesystem::path& projectRoot, const std::filesystem::path& projectDirectory,
                        const std::filesystem::path& managedApi, const std::filesystem::path& managedApiProject,
                        const std::string_view targetFramework, const std::string_view languageVersion)
        {
            std::string text = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                               "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n    "
                               "<TargetFramework>" +
                               XmlEscape(targetFramework) +
                               "</TargetFramework>\n"
                               "    <LangVersion>" +
                               XmlEscape(languageVersion) +
                               "</LangVersion>\n"
                               "    <ImplicitUsings>enable</ImplicitUsings>\n    <Nullable>enable</Nullable>\n"
                               "    <Deterministic>true</Deterministic>\n    <DebugType>portable</DebugType>\n"
                               "    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>\n"
                               "    <WarningsNotAsErrors>$(WarningsNotAsErrors);CS0168;CS0169;CS0219;CS0414"
                               "</WarningsNotAsErrors>\n"
                               "    <NoWarn>$(NoWarn);CS0649</NoWarn>\n"
                               "    <AllowUnsafeBlocks>" +
                               std::string(assembly.Definition.AllowUnsafe ? "true" : "false") +
                               "</AllowUnsafeBlocks>\n"
                               "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
                               "    <DefaultItemExcludes>$(DefaultItemExcludes);Library/**;Logs/**;Temp/**;Build/**"
                               "</DefaultItemExcludes>\n    <AssemblyName>" +
                               XmlEscape(assembly.Definition.Name) + "</AssemblyName>\n    <RootNamespace>" +
                               XmlEscape(assembly.Definition.RootNamespace) + "</RootNamespace>\n";
            if (!assembly.Definition.DefineSymbols.empty())
            {
                text += "    <DefineConstants>$(DefineConstants);";
                for (std::size_t index = 0; index < assembly.Definition.DefineSymbols.size(); ++index)
                {
                    if (index != 0)
                        text += ';';
                    text += XmlEscape(assembly.Definition.DefineSymbols[index]);
                }
                text += "</DefineConstants>\n";
            }
            text += "  </PropertyGroup>\n  <ItemGroup>\n";
            for (const auto& root : assembly.Definition.SourceRoots)
            {
                std::error_code error;
                auto source = std::filesystem::relative(projectRoot / root, projectDirectory, error);
                if (error || source.empty())
                    source = projectRoot / root;
                text += "    <Compile Include=\"" + XmlEscape(PathText(source / "**" / "*.cs")) + "\" LinkBase=\"" +
                        XmlEscape(PathText(root)) + "\" />\n";
            }
            text += "  </ItemGroup>\n";
            if (!managedApiProject.empty())
            {
                std::error_code error;
                auto referencePath = std::filesystem::relative(managedApiProject, projectDirectory, error);
                if (error || referencePath.empty())
                    referencePath = managedApiProject;
                text += "  <ItemGroup>\n    <ProjectReference Include=\"" + XmlEscape(PathText(referencePath)) +
                        "\" />\n  </ItemGroup>\n";
            }
            else if (!managedApi.empty())
            {
                std::error_code error;
                auto referencePath = std::filesystem::relative(managedApi, projectDirectory, error);
                if (error || referencePath.empty())
                    referencePath = managedApi;
                text += "  <ItemGroup>\n    <Reference Include=\"Keire.Managed\">\n      <HintPath>" +
                        XmlEscape(PathText(referencePath)) +
                        "</HintPath>\n      <Private>false</Private>\n    </Reference>\n  </ItemGroup>\n";
            }
            if (!assembly.Definition.References.empty())
            {
                text += "  <ItemGroup>\n";
                for (const auto reference : assembly.Definition.References)
                    text += "    <ProjectReference Include=\"" + XmlEscape(names.at(reference)) + ".csproj\" />\n";
                text += "  </ItemGroup>\n";
            }
            if (!assembly.Definition.Packages.empty())
            {
                text += "  <ItemGroup>\n";
                for (const auto& package : assembly.Definition.Packages)
                {
                    text += "    <PackageReference Include=\"" + XmlEscape(package.Name) + "\" Version=\"" +
                            XmlEscape(package.Version) + "\" />\n";
                }
                text += "  </ItemGroup>\n";
            }
            text += "</Project>\n";
            return text;
        }

        [[nodiscard]] std::string GenerateSolution(const ManagedBuildRequest& request,
                                                   const std::map<AssetId, std::string>& names,
                                                   const std::filesystem::path& projectRoot,
                                                   const std::filesystem::path& managedApiProject)
        {
            constexpr std::string_view csharpProject = "{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}";
            constexpr std::string_view managedApiGuid = "{4B454952-4D41-4E41-4745-440000000001}";
            std::string text = "Microsoft Visual Studio Solution File, Format Version 12.00\n"
                               "# Visual Studio Version 17\n"
                               "VisualStudioVersion = 17.0.31903.59\n"
                               "MinimumVisualStudioVersion = 10.0.40219.1\n"
                               "# Generated by Keire Editor\n";
            if (!managedApiProject.empty())
            {
                std::error_code error;
                auto referencePath = std::filesystem::relative(managedApiProject, projectRoot, error);
                if (error || referencePath.empty())
                    referencePath = managedApiProject;
                text += "Project(\"" + std::string(csharpProject) + "\") = \"Keire.Managed\", \"" +
                        PathText(referencePath) + "\", \"" + std::string(managedApiGuid) + "\"\nEndProject\n";
            }
            for (const auto& assembly : request.Assemblies)
            {
                const auto guid = "{" + assembly.Asset.ToString() + "}";
                text += "Project(\"" + std::string(csharpProject) + "\") = \"" + names.at(assembly.Asset) + "\", \"" +
                        names.at(assembly.Asset) + ".csproj\", \"" + guid + "\"\nEndProject\n";
            }
            text += "Global\n"
                    "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
                    "\t\tDebug|Any CPU = Debug|Any CPU\n"
                    "\t\tRelease|Any CPU = Release|Any CPU\n"
                    "\tEndGlobalSection\n"
                    "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
            if (!managedApiProject.empty())
            {
                text += "\t\t" + std::string(managedApiGuid) + ".Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
                text += "\t\t" + std::string(managedApiGuid) + ".Debug|Any CPU.Build.0 = Debug|Any CPU\n";
                text += "\t\t" + std::string(managedApiGuid) + ".Release|Any CPU.ActiveCfg = Release|Any CPU\n";
                text += "\t\t" + std::string(managedApiGuid) + ".Release|Any CPU.Build.0 = Release|Any CPU\n";
            }
            for (const auto& assembly : request.Assemblies)
            {
                const auto guid = "{" + assembly.Asset.ToString() + "}";
                text += "\t\t" + guid + ".Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
                text += "\t\t" + guid + ".Debug|Any CPU.Build.0 = Debug|Any CPU\n";
                text += "\t\t" + guid + ".Release|Any CPU.ActiveCfg = Release|Any CPU\n";
                text += "\t\t" + guid + ".Release|Any CPU.Build.0 = Release|Any CPU\n";
            }
            text += "\tEndGlobalSection\n"
                    "\tGlobalSection(SolutionProperties) = preSolution\n"
                    "\t\tHideSolutionNode = FALSE\n"
                    "\tEndGlobalSection\n"
                    "EndGlobal\n";
            return text;
        }

        [[nodiscard]] std::string GenerateManagedBuildAggregator(const ManagedBuildRequest& request)
        {
            std::string text = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n"
                               "    <TargetFramework>net10.0</TargetFramework>\n"
                               "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
                               "  </PropertyGroup>\n  <ItemGroup>\n";
            for (const auto& assembly : request.Assemblies)
                text += "    <ProjectReference Include=\"" + XmlEscape(assembly.Definition.Name) + ".csproj\" />\n";
            text += "  </ItemGroup>\n</Project>\n";
            return text;
        }

        [[nodiscard]] std::string
        GenerateManagedApiDesignTimeProject(const std::filesystem::path& managedApiSourceProject,
                                            const std::filesystem::path& designTimeProject)
        {
            std::error_code error;
            auto sourceDirectory = std::filesystem::relative(managedApiSourceProject.parent_path(),
                                                             designTimeProject.parent_path(), error);
            if (error || sourceDirectory.empty())
                sourceDirectory = managedApiSourceProject.parent_path();
            const auto sourceGlob = PathText(sourceDirectory / "**" / "*.cs");
            const auto binGlob = PathText(sourceDirectory / "bin" / "**" / "*.cs");
            const auto objGlob = PathText(sourceDirectory / "obj" / "**" / "*.cs");
            return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
                   "  <!-- Visual Studio 2022 design-time facade. Runtime builds remain net10.0/C# 14. -->\n"
                   "  <PropertyGroup>\n"
                   "    <TargetFramework>net8.0</TargetFramework>\n"
                   "    <LangVersion>12.0</LangVersion>\n"
                   "    <ImplicitUsings>enable</ImplicitUsings>\n"
                   "    <Nullable>enable</Nullable>\n"
                   "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
                   "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
                   "    <AssemblyName>Keire.Managed</AssemblyName>\n"
                   "    <RootNamespace>Keire</RootNamespace>\n"
                   "  </PropertyGroup>\n"
                   "  <ItemGroup>\n"
                   "    <Compile Include=\"" +
                   XmlEscape(sourceGlob) + "\" Exclude=\"" + XmlEscape(binGlob) + ";" + XmlEscape(objGlob) +
                   "\" LinkBase=\"Keire.Managed\" />\n"
                   "  </ItemGroup>\n"
                   "</Project>\n";
        }

    } // namespace Detail
} // namespace Keire
