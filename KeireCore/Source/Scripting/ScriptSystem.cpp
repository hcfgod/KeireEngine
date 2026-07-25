#include "Keire/Scripting/ScriptSystem.h"

#include "Keire/ECS/Component.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Entity.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/HostInstance.hpp>
#include <Coral/ManagedObject.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Keire
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

        void WriteText(const std::filesystem::path& path, const std::string_view value)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(value.data(), static_cast<std::streamsize>(value.size())))
                throw std::runtime_error("Managed build could not write '" + PathText(path) + "'.");
        }

        [[nodiscard]] bool HasDotnet10Sdk(const std::filesystem::path& executable)
        {
            const auto sdk = executable.parent_path() / "sdk";
            if (!std::filesystem::is_directory(sdk))
                return false;
            return std::ranges::any_of(std::filesystem::directory_iterator(sdk),
                                       [](const std::filesystem::directory_entry& entry)
                                       {
                                           const auto name = entry.path().filename().string();
                                           return entry.is_directory() && name.starts_with("10.");
                                       });
        }

        [[nodiscard]] ManagedSdkConfiguration ReadManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                                                          ManagedSdkConfiguration fallback)
        {
            const auto settingsPath = projectRoot / "ProjectSettings" / "Scripting.keiresettings";
            if (!std::filesystem::is_regular_file(settingsPath))
                return fallback;

            const auto document = nlohmann::json::parse(Detail::ReadTextFile(settingsPath, 1024U * 1024U));
            const auto selection = document.value("sdkSelection", std::string{"bundled"});
            if (selection == "systemPath")
                fallback.Selection = ManagedSdkSelection::SystemPath;
            else if (selection == "custom")
                fallback.Selection = ManagedSdkSelection::Custom;
            else
                fallback.Selection = ManagedSdkSelection::Bundled;
            if (const auto iterator = document.find("customSdkExecutable");
                iterator != document.end() && iterator->is_string())
            {
                fallback.CustomExecutable = Detail::PathFromUtf8(iterator->get<std::string>());
            }
            return fallback;
        }

        void WriteManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                          const ManagedSdkConfiguration& configuration)
        {
            const auto settingsPath = projectRoot / "ProjectSettings" / "Scripting.keiresettings";
            nlohmann::json document = nlohmann::json::object();
            if (std::filesystem::is_regular_file(settingsPath))
                document = nlohmann::json::parse(Detail::ReadTextFile(settingsPath, 1024U * 1024U));

            switch (configuration.Selection)
            {
            case ManagedSdkSelection::Bundled:
                document["sdkSelection"] = "bundled";
                break;
            case ManagedSdkSelection::SystemPath:
                document["sdkSelection"] = "systemPath";
                break;
            case ManagedSdkSelection::Custom:
                document["sdkSelection"] = "custom";
                break;
            }
            document["customSdkExecutable"] = Detail::PathToUtf8(configuration.CustomExecutable);
            Detail::WriteTextFileAtomically(settingsPath, document.dump(2) + '\n');
        }

        [[nodiscard]] std::filesystem::path ResolveDotnet(const std::filesystem::path& configured,
                                                          const ManagedSdkSelection selection,
                                                          const std::filesystem::path& projectRoot,
                                                          const std::filesystem::path& runtimeRoot)
        {
            constexpr std::string_view executableName =
#if defined(_WIN32)
                "dotnet.exe";
#else
                "dotnet";
#endif

            if (selection == ManagedSdkSelection::Custom)
            {
                if (configured.empty())
                    throw std::runtime_error(
                        "A custom .NET SDK was selected, but no dotnet executable was configured.");
                const auto resolved = std::filesystem::absolute(configured).lexically_normal();
                if (!std::filesystem::is_regular_file(resolved) || !HasDotnet10Sdk(resolved))
                    throw std::runtime_error("The configured dotnet executable does not provide the .NET 10 SDK.");
                return resolved;
            }

            if (selection == ManagedSdkSelection::Bundled)
            {
                std::vector<std::filesystem::path> roots;
                roots.reserve(16);
                const auto appendAncestors = [&roots](std::filesystem::path root)
                {
                    if (root.empty())
                        return;
                    root = std::filesystem::absolute(root).lexically_normal();
                    for (std::size_t depth = 0; depth < 8 && !root.empty(); ++depth)
                    {
                        roots.push_back(root);
                        const auto parent = root.parent_path();
                        if (parent == root)
                            break;
                        root = parent;
                    }
                };
                appendAncestors(std::filesystem::current_path());
                appendAncestors(projectRoot);

                std::vector<std::filesystem::path> candidates;
                candidates.reserve(roots.size() * 2 + 1);
                if (!runtimeRoot.empty())
                    candidates.push_back(runtimeRoot / executableName);
                for (const auto& root : roots)
                {
                    candidates.push_back(root / "Build" / "Dependencies" / "dotnet-sdk" / executableName);
                    candidates.push_back(root / "Library" / "DotnetSdk10" / "sdk" / executableName);
                }

                for (const auto& candidate : candidates)
                {
                    if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                        return std::filesystem::absolute(candidate).lexically_normal();
                }
                throw std::runtime_error(
                    "The bundled .NET 10 SDK was not found. Install the engine SDK dependency or choose System PATH "
                    "or Custom in Project Settings > Scripting.");
            }

#if defined(_WIN32)
            char* rawDotnetRoot = nullptr;
            std::size_t dotnetRootSize = 0;
            if (_dupenv_s(&rawDotnetRoot, &dotnetRootSize, "DOTNET_ROOT") != 0)
                rawDotnetRoot = nullptr;
            const std::unique_ptr<char, decltype(&std::free)> dotnetRoot(rawDotnetRoot, &std::free);
            const char* dotnetRootValue = dotnetRoot ? dotnetRoot.get() : nullptr;
#else
            const char* dotnetRoot = std::getenv("DOTNET_ROOT");
            const char* dotnetRootValue = dotnetRoot;
#endif
            if (dotnetRootValue)
            {
                const auto candidate = std::filesystem::path(dotnetRootValue) / executableName;
                if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                    return std::filesystem::absolute(candidate).lexically_normal();
            }

#if defined(_WIN32)
            char* rawEnvironment = nullptr;
            std::size_t environmentSize = 0;
            if (_dupenv_s(&rawEnvironment, &environmentSize, "PATH") != 0)
                rawEnvironment = nullptr;
            const std::unique_ptr<char, decltype(&std::free)> environment(rawEnvironment, &std::free);
            const std::string paths = environment ? std::string(environment.get()) : std::string{};
#else
            const char* environment = std::getenv("PATH");
            const std::string paths = environment ? std::string(environment) : std::string{};
#endif
            if (paths.empty())
                throw std::runtime_error("dotnet was not found because PATH is unavailable.");
#if defined(_WIN32)
            constexpr char separator = ';';
            const std::filesystem::path executable = executableName;
#else
            constexpr char separator = ':';
            const std::filesystem::path executable = executableName;
#endif
            std::size_t begin = 0;
            while (begin <= paths.size())
            {
                const auto end = paths.find(separator, begin);
                const auto candidate = std::filesystem::path(paths.substr(begin, end - begin)) / executable;
                if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                    return std::filesystem::absolute(candidate).lexically_normal();
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            throw std::runtime_error(
                "The .NET 10 SDK was not found on PATH. Choose Bundled or Custom in Project Settings > Scripting.");
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
                               "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n    <AssemblyName>" +
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

        [[nodiscard]] std::string ManagedFieldDisplayName(std::string name)
        {
            const auto fallback = name;
            while (!name.empty() && name.front() == '_')
                name.erase(name.begin());
            std::string result;
            result.reserve(name.size() + 8);
            for (std::size_t index = 0; index < name.size(); ++index)
            {
                const auto character = name[index];
                if (index > 0 && std::isupper(static_cast<unsigned char>(character)) &&
                    !std::isupper(static_cast<unsigned char>(name[index - 1])))
                {
                    result.push_back(' ');
                }
                result.push_back(character);
            }
            if (!result.empty())
                result.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(result.front())));
            return result.empty() ? fallback : result;
        }

        [[nodiscard]] std::optional<ComponentPropertyKind> ManagedFieldKind(Coral::Type& type)
        {
            const Coral::ScopedString scopedName(type.GetFullName());
            const auto name = static_cast<std::string>(scopedName);
            if (name == "System.Boolean")
                return ComponentPropertyKind::Boolean;
            if (name == "System.SByte" || name == "System.Byte" || name == "System.Int16" || name == "System.UInt16" ||
                name == "System.Int32" || name == "System.UInt32" || name == "System.Int64" || name == "System.UInt64")
            {
                return ComponentPropertyKind::Integer;
            }
            if (name == "System.Single" || name == "System.Double" || name == "System.Decimal")
                return ComponentPropertyKind::Scalar;
            if (name == "System.String")
                return ComponentPropertyKind::Text;
            if (name == "Keire.Vector2")
                return ComponentPropertyKind::Vector2;
            if (name == "Keire.Vector3")
                return ComponentPropertyKind::Vector3;
            if (name == "Keire.Vector4")
                return ComponentPropertyKind::Vector4;
            if (name == "Keire.Quaternion")
                return ComponentPropertyKind::Quaternion;
            if (name == "Keire.Color")
                return ComponentPropertyKind::Color;
            return std::nullopt;
        }

        [[nodiscard]] std::vector<ComponentProperty> ReflectManagedProperties(const Coral::Type& concreteType,
                                                                              const Coral::Type& behaviourType,
                                                                              const Coral::Type& serializeFieldType,
                                                                              const Coral::Type* hideInInspectorType)
        {
            std::vector<ComponentProperty> result;
            auto* current = const_cast<Coral::Type*>(std::addressof(concreteType));
            while (*current && !(*current == behaviourType))
            {
                for (auto field : current->GetFields())
                {
                    bool serialized = field.GetAccessibility() == Coral::TypeAccessibility::Public;
                    bool hidden = false;
                    for (auto attribute : field.GetAttributes())
                    {
                        if (attribute.GetType() == serializeFieldType)
                            serialized = true;
                        else if (hideInInspectorType && attribute.GetType() == *hideInInspectorType)
                            hidden = true;
                    }
                    if (!serialized || hidden)
                        continue;
                    const auto kind = ManagedFieldKind(field.GetType());
                    if (!kind)
                        continue;
                    const Coral::ScopedString scopedName(field.GetName());
                    const auto name = static_cast<std::string>(scopedName);
                    if (std::ranges::find(result, name, &ComponentProperty::Key) != result.end())
                        continue;
                    result.push_back(
                        {.Key = name, .DisplayName = ManagedFieldDisplayName(name), .Kind = *kind, .Step = 0.1});
                }
                current = std::addressof(current->GetBaseType());
            }
            return result;
        }

        [[nodiscard]] const nlohmann::json* JsonMember(const nlohmann::json& value, const std::string_view primary,
                                                       const std::string_view fallback)
        {
            if (const auto found = value.find(std::string(primary)); found != value.end())
                return std::addressof(*found);
            if (const auto found = value.find(std::string(fallback)); found != value.end())
                return std::addressof(*found);
            return nullptr;
        }

        [[nodiscard]] double JsonNumber(const nlohmann::json& value, const std::string_view primary,
                                        const std::string_view fallback)
        {
            const auto* member = JsonMember(value, primary, fallback);
            if (!member || !member->is_number())
                throw std::invalid_argument("Managed vector state is missing a numeric member.");
            return member->get<double>();
        }

        [[nodiscard]] ComponentPropertyValue DefaultManagedPropertyValue(const ComponentPropertyKind kind)
        {
            switch (kind)
            {
            case ComponentPropertyKind::Boolean:
                return false;
            case ComponentPropertyKind::Integer:
                return std::int64_t{0};
            case ComponentPropertyKind::Scalar:
                return 0.0;
            case ComponentPropertyKind::Text:
                return std::string{};
            case ComponentPropertyKind::Vector2:
                return Vector2{};
            case ComponentPropertyKind::Vector3:
                return Vector3{};
            case ComponentPropertyKind::Vector4:
                return Vector4{};
            case ComponentPropertyKind::Quaternion:
                return Quaternion{};
            case ComponentPropertyKind::Color:
                return Color{};
            case ComponentPropertyKind::Asset:
                return AssetId{};
            case ComponentPropertyKind::Entity:
                return EntityId{};
            }
            throw std::logic_error("Unsupported managed Inspector property kind.");
        }

        [[nodiscard]] ComponentPropertyValue ReadManagedPropertyValue(const nlohmann::json& value,
                                                                      const ComponentPropertyKind kind)
        {
            switch (kind)
            {
            case ComponentPropertyKind::Boolean:
                return value.get<bool>();
            case ComponentPropertyKind::Integer:
                return value.get<std::int64_t>();
            case ComponentPropertyKind::Scalar:
                return value.get<double>();
            case ComponentPropertyKind::Text:
                return value.get<std::string>();
            case ComponentPropertyKind::Vector2:
                return Vector2{static_cast<float>(JsonNumber(value, "X", "x")),
                               static_cast<float>(JsonNumber(value, "Y", "y"))};
            case ComponentPropertyKind::Vector3:
                return Vector3{static_cast<float>(JsonNumber(value, "X", "x")),
                               static_cast<float>(JsonNumber(value, "Y", "y")),
                               static_cast<float>(JsonNumber(value, "Z", "z"))};
            case ComponentPropertyKind::Vector4:
                return Vector4{
                    static_cast<float>(JsonNumber(value, "X", "x")), static_cast<float>(JsonNumber(value, "Y", "y")),
                    static_cast<float>(JsonNumber(value, "Z", "z")), static_cast<float>(JsonNumber(value, "W", "w"))};
            case ComponentPropertyKind::Quaternion:
                return Quaternion{
                    static_cast<float>(JsonNumber(value, "X", "x")), static_cast<float>(JsonNumber(value, "Y", "y")),
                    static_cast<float>(JsonNumber(value, "Z", "z")), static_cast<float>(JsonNumber(value, "W", "w"))};
            case ComponentPropertyKind::Color:
                return Color{static_cast<float>(JsonNumber(value, "Red", "red")),
                             static_cast<float>(JsonNumber(value, "Green", "green")),
                             static_cast<float>(JsonNumber(value, "Blue", "blue")),
                             static_cast<float>(JsonNumber(value, "Alpha", "alpha"))};
            case ComponentPropertyKind::Asset:
            case ComponentPropertyKind::Entity:
                break;
            }
            throw std::logic_error("Unsupported managed Inspector property kind.");
        }

        [[nodiscard]] nlohmann::json WriteManagedPropertyValue(const ComponentPropertyValue& value,
                                                               const ComponentPropertyKind kind)
        {
            switch (kind)
            {
            case ComponentPropertyKind::Boolean:
                return std::get<bool>(value);
            case ComponentPropertyKind::Integer:
                return std::get<std::int64_t>(value);
            case ComponentPropertyKind::Scalar:
                return std::get<double>(value);
            case ComponentPropertyKind::Text:
                return std::get<std::string>(value);
            case ComponentPropertyKind::Vector2:
            {
                const auto vector = std::get<Vector2>(value);
                return {{"X", vector.X}, {"Y", vector.Y}};
            }
            case ComponentPropertyKind::Vector3:
            {
                const auto vector = std::get<Vector3>(value);
                return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}};
            }
            case ComponentPropertyKind::Vector4:
            {
                const auto vector = std::get<Vector4>(value);
                return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}, {"W", vector.W}};
            }
            case ComponentPropertyKind::Quaternion:
            {
                const auto quaternion = std::get<Quaternion>(value);
                return {{"X", quaternion.X}, {"Y", quaternion.Y}, {"Z", quaternion.Z}, {"W", quaternion.W}};
            }
            case ComponentPropertyKind::Color:
            {
                const auto color = std::get<Color>(value);
                return {{"Red", color.Red}, {"Green", color.Green}, {"Blue", color.Blue}, {"Alpha", color.Alpha}};
            }
            case ComponentPropertyKind::Asset:
            case ComponentPropertyKind::Entity:
                break;
            }
            throw std::logic_error("Unsupported managed Inspector property kind.");
        }

        [[nodiscard]] nlohmann::json* ManagedStateField(nlohmann::json& document, const std::string_view name)
        {
            auto* fields = document.contains("Fields")   ? std::addressof(document["Fields"])
                           : document.contains("fields") ? std::addressof(document["fields"])
                                                         : nullptr;
            if (!fields || !fields->is_array())
                return nullptr;
            for (auto& field : *fields)
            {
                const auto* fieldName = JsonMember(field, "Name", "name");
                if (fieldName && fieldName->is_string() && fieldName->get<std::string>() == name)
                    return std::addressof(field);
            }
            return nullptr;
        }

        [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                               const std::vector<ComponentProperty>& properties)
        {
            ComponentPropertyBag result{{"managedState", state}};
            auto document = nlohmann::json::parse(state);
            for (const auto& property : properties)
            {
                const auto* field = ManagedStateField(document, property.Key);
                const auto* value = field ? JsonMember(*field, "Value", "value") : nullptr;
                result.emplace(property.Key, value ? ReadManagedPropertyValue(*value, property.Kind)
                                                   : DefaultManagedPropertyValue(property.Kind));
            }
            return result;
        }

        [[nodiscard]] std::string ApplyManagedState(const std::string& state, const ComponentPropertyBag& values,
                                                    const std::vector<ComponentProperty>& properties)
        {
            auto document = nlohmann::json::parse(state);
            if (!document.contains("Fields") && !document.contains("fields"))
                document["Fields"] = nlohmann::json::array();
            auto& fields = document.contains("Fields") ? document["Fields"] : document["fields"];
            if (!fields.is_array())
                throw std::invalid_argument("Managed component state fields are not an array.");
            for (const auto& property : properties)
            {
                const auto value = values.find(property.Key);
                if (value == values.end())
                    continue;
                auto* field = ManagedStateField(document, property.Key);
                if (!field)
                {
                    fields.push_back({{"StableId", ""},
                                      {"Name", property.Key},
                                      {"Type", ""},
                                      {"Aliases", nlohmann::json::array()},
                                      {"Value", nullptr}});
                    field = std::addressof(fields.back());
                }
                (*field)["Value"] = WriteManagedPropertyValue(value->second, property.Kind);
            }
            return document.dump();
        }
    } // namespace

    class ScriptSystem::Impl final
    {
      public:
        class ManagedComponent;

        struct BehaviourType final
        {
            std::string Name;
            ComponentTypeId ComponentType;
            std::int32_t ExecutionOrder = 0;
            const Coral::Type* Type = nullptr;
            std::vector<ComponentProperty> Properties;
        };

        struct BehaviourInstance final
        {
            std::string TypeName;
            ComponentTypeId ComponentType;
            std::uint64_t World = 0;
            AssetId Entity;
            Coral::ManagedObject Object;
            std::string State = "{\"version\":1,\"fields\":[]}";
            bool Enabled = true;
            bool Faulted = false;
            Keire::Entity NativeEntity;
        };

        class RuntimeScope final
        {
          public:
            explicit RuntimeScope(Impl& runtime) noexcept : m_Previous(CurrentRuntime) { CurrentRuntime = &runtime; }
            ~RuntimeScope() { CurrentRuntime = m_Previous; }

            RuntimeScope(const RuntimeScope&) = delete;
            RuntimeScope& operator=(const RuntimeScope&) = delete;

          private:
            Impl* m_Previous;
        };

        explicit Impl(ScriptSystemSpecification value)
            : Specification(std::move(value)), Owner(std::this_thread::get_id()),
              Lifetime(std::make_shared<Impl*>(this))
        {
        }

        ~Impl()
        {
            *Lifetime = nullptr;
            StopWorker();
            ShutdownRuntime();
        }

        void RequireOwner() const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error("ScriptSystem operation must run on the owner thread.");
        }

        void ResumeGenerationSequence()
        {
            std::uint64_t highestGeneration = 0;
            const auto generations = OutputRoot / "Generations";
            if (std::filesystem::is_directory(generations))
            {
                for (const auto& entry : std::filesystem::directory_iterator(generations))
                {
                    if (!entry.is_directory())
                        continue;
                    const auto name = entry.path().filename().string();
                    try
                    {
                        std::size_t consumed = 0;
                        const auto generation = std::stoull(name, &consumed);
                        if (consumed == name.size())
                            highestGeneration = std::max(highestGeneration, generation);
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }

            const auto manifest = OutputRoot / "active-generation.json";
            if (std::filesystem::is_regular_file(manifest))
            {
                try
                {
                    const auto document = nlohmann::json::parse(Detail::ReadTextFile(manifest, 1024U * 1024U));
                    highestGeneration = std::max(highestGeneration, document.value("generation", std::uint64_t{0}));
                }
                catch (const nlohmann::json::exception&)
                {
                }
            }

            if (highestGeneration == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Managed generation sequence is exhausted.");
            NextOperation = highestGeneration + 1;
        }

        [[nodiscard]] std::filesystem::path FindManagedApiProject() const
        {
            auto root = ProjectRoot;
            for (std::size_t depth = 0; depth < 8 && !root.empty(); ++depth)
            {
                const auto candidate = root / "KeireManaged" / "Keire.Managed.csproj";
                if (std::filesystem::is_regular_file(candidate))
                    return candidate;
                const auto parent = root.parent_path();
                if (parent == root)
                    break;
                root = parent;
            }
            return {};
        }

        void StopWorker() noexcept
        {
            if (Worker.joinable())
            {
                Worker.request_stop();
                Worker.join();
            }
        }

        void InitializeRuntime()
        {
            if (Specification.RuntimeHostDirectory.empty())
                return;
            const auto directory = std::filesystem::absolute(Specification.RuntimeHostDirectory).lexically_normal();
            if (!std::filesystem::is_directory(directory))
                throw std::invalid_argument("The managed runtime host directory does not exist.");
            Coral::HostSettings settings;
            settings.CoralDirectory = PathText(directory);
            if (!Specification.RuntimeRootDirectory.empty())
            {
                const auto runtimeRoot =
                    std::filesystem::absolute(Specification.RuntimeRootDirectory).lexically_normal();
                if (!std::filesystem::is_directory(runtimeRoot))
                    throw std::invalid_argument("The bundled .NET runtime root directory does not exist.");
                settings.DotnetRoot = PathText(runtimeRoot);
            }
            settings.ExceptionCallback = [this](const std::string_view message)
            {
                std::scoped_lock lock(Mutex);
                RuntimeException.assign(message);
            };
            const auto status = RuntimeHost.Initialize(std::move(settings));
            if (status != Coral::CoralInitStatus::Success)
                throw std::runtime_error("Coral could not initialize the bundled .NET 10 runtime host (status " +
                                         std::to_string(static_cast<int>(status)) + ").");
            RuntimeInitialized = true;
        }

        void Unload(std::unique_ptr<Coral::AssemblyLoadContext>& context) noexcept
        {
            if (!context || !RuntimeInitialized)
                return;
            try
            {
                RuntimeHost.UnloadAssemblyLoadContext(*context);
            }
            catch (...)
            {
            }
            context.reset();
        }

        void ShutdownRuntime() noexcept
        {
            Instances.clear();
            ActiveTypes.clear();
            CandidateTypes.clear();
            Unload(CandidateContext);
            Unload(ActiveContext);
            if (RuntimeInitialized)
            {
                try
                {
                    RuntimeHost.Shutdown();
                }
                catch (...)
                {
                }
                RuntimeInitialized = false;
            }
        }

        void ClearRuntimeException()
        {
            std::scoped_lock lock(Mutex);
            RuntimeException.clear();
        }

        void ThrowRuntimeException()
        {
            std::string exception;
            {
                std::scoped_lock lock(Mutex);
                exception = std::exchange(RuntimeException, {});
            }
            if (!exception.empty())
                throw std::runtime_error(exception);
        }

        [[nodiscard]] static Entity ResolveRuntimeEntity(const std::uint64_t world, const std::uint64_t high,
                                                         const std::uint64_t low) noexcept
        {
            if (!CurrentRuntime)
                return {};
            const AssetId id(high, low);
            const auto found = std::ranges::find_if(
                CurrentRuntime->Instances, [world, id](const auto& entry)
                { return entry.second.World == world && entry.second.Entity == id && entry.second.NativeEntity; });
            return found == CurrentRuntime->Instances.end() ? Entity{} : found->second.NativeEntity;
        }

        static void RuntimeWriteLog(const std::uint8_t level, const Coral::String message) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return;
            try
            {
                const auto clamped = std::min(level, static_cast<std::uint8_t>(ManagedLogLevel::Critical));
                CurrentRuntime->Specification.RuntimeServices->WriteManagedLog(static_cast<ManagedLogLevel>(clamped),
                                                                               static_cast<std::string>(message));
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] static float RuntimeDeltaTime() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                       ? CurrentRuntime->Specification.RuntimeServices->ManagedDeltaTime()
                       : 0.0F;
        }

        static void RuntimeInputAxis2D(const Coral::String action, float* x, float* y) noexcept
        {
            if (!x || !y)
                return;
            *x = 0.0F;
            *y = 0.0F;
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return;
            try
            {
                const auto value =
                    CurrentRuntime->Specification.RuntimeServices->ReadManagedInput(static_cast<std::string>(action));
                *x = value.X;
                *y = value.Y;
            }
            catch (...)
            {
            }
        }

        static void RuntimeSetCursorVisible(const std::uint8_t visible) noexcept
        {
            if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
                CurrentRuntime->Specification.RuntimeServices->SetManagedCursorVisible(visible != 0);
        }

        static void RuntimeSetCursorLocked(const std::uint8_t locked) noexcept
        {
            if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
                CurrentRuntime->Specification.RuntimeServices->SetManagedCursorLocked(locked != 0);
        }

        [[nodiscard]] static std::uint8_t RuntimeIsCursorVisible() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices &&
                           CurrentRuntime->Specification.RuntimeServices->IsManagedCursorVisible()
                       ? 1
                       : 0;
        }

        [[nodiscard]] static std::uint8_t RuntimeIsCursorLocked() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices &&
                           CurrentRuntime->Specification.RuntimeServices->IsManagedCursorLocked()
                       ? 1
                       : 0;
        }

        [[nodiscard]] static Vector3 RuntimeGetLocalPosition(const std::uint64_t world, const std::uint64_t high,
                                                             const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            return transform ? transform->LocalPosition() : Vector3{};
        }

        static void RuntimeSetLocalPosition(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Vector3 value) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
            {
                try
                {
                    transform->SetLocalPosition(value);
                }
                catch (...)
                {
                }
            }
        }

        [[nodiscard]] static Quaternion RuntimeGetLocalRotation(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            return transform ? transform->LocalRotation() : Quaternion{};
        }

        static void RuntimeSetLocalRotation(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Quaternion value) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
            {
                try
                {
                    transform->SetLocalRotation(value);
                }
                catch (...)
                {
                }
            }
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method)
        {
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            object.InvokeMethod(method);
            ThrowRuntimeException();
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method, const float value)
        {
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            object.InvokeMethod(method, value);
            ThrowRuntimeException();
        }

        [[nodiscard]] std::string CaptureState(Coral::ManagedObject& object, const bool persistent)
        {
            Invoke(object, persistent ? "RuntimeCapturePersistentState" : "RuntimeCaptureReloadState");
            return object.GetFieldValue<std::string>("RuntimeSerializedState");
        }

        void RestoreState(Coral::ManagedObject& object, const std::string& state, const bool persistent)
        {
            object.SetFieldValue<std::string>("RuntimeSerializedState", state);
            Invoke(object, persistent ? "RuntimeRestorePersistentState" : "RuntimeRestoreReloadState");
        }

        [[nodiscard]] Coral::ManagedObject CreateObject(const BehaviourType& type, const std::uint64_t world,
                                                        const AssetId entity)
        {
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            auto object = type.Type->CreateInstance();
            ThrowRuntimeException();
            if (!object.IsValid())
                throw std::runtime_error("Managed Behaviour construction returned an invalid object.");
            ClearRuntimeException();
            object.InvokeMethod("RuntimeAttach", world, entity.High(), entity.Low());
            ThrowRuntimeException();
            return object;
        }

        [[nodiscard]] const BehaviourType* FindType(const std::vector<BehaviourType>& types,
                                                    const std::string_view name) const
        {
            const auto found = std::ranges::find(types, name, &BehaviourType::Name);
            return found == types.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const BehaviourType* FindType(const std::vector<BehaviourType>& types,
                                                    const ComponentTypeId componentType) const
        {
            const auto found = std::ranges::find(types, componentType, &BehaviourType::ComponentType);
            return found == types.end() ? nullptr : std::addressof(*found);
        }

        void InvokeInstance(const std::uint64_t id, const ManagedBehaviourCallback callback,
                            const float deltaSeconds = 0.0F)
        {
            const auto found = Instances.find(id);
            if (found == Instances.end() || !found->second.Object.IsValid())
                return;
            auto& instance = found->second;
            if (instance.Faulted && callback != ManagedBehaviourCallback::Disable &&
                callback != ManagedBehaviourCallback::Destroy)
                return;
            try
            {
                switch (callback)
                {
                case ManagedBehaviourCallback::Awake:
                    Invoke(instance.Object, "RuntimeAwake");
                    break;
                case ManagedBehaviourCallback::Enable:
                    Invoke(instance.Object, "RuntimeEnable");
                    instance.Enabled = true;
                    break;
                case ManagedBehaviourCallback::Start:
                    Invoke(instance.Object, "RuntimeStart");
                    break;
                case ManagedBehaviourCallback::FixedUpdate:
                    Invoke(instance.Object, "RuntimeFixedUpdate", deltaSeconds);
                    break;
                case ManagedBehaviourCallback::Update:
                    Invoke(instance.Object, "RuntimeUpdate", deltaSeconds);
                    break;
                case ManagedBehaviourCallback::LateUpdate:
                    Invoke(instance.Object, "RuntimeLateUpdate");
                    break;
                case ManagedBehaviourCallback::Disable:
                    Invoke(instance.Object, "RuntimeDisable");
                    instance.Enabled = false;
                    break;
                case ManagedBehaviourCallback::Destroy:
                    Invoke(instance.Object, "RuntimeDestroy");
                    break;
                case ManagedBehaviourCallback::BeforeReload:
                    Invoke(instance.Object, "RuntimeBeforeReload");
                    break;
                case ManagedBehaviourCallback::AfterReload:
                    Invoke(instance.Object, "RuntimeAfterReload");
                    break;
                }
            }
            catch (const std::exception& error)
            {
                RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error,
                                              callback, Reload.Generation, instance.TypeName, instance.Entity,
                                              error.what()});
                if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                    throw;
                instance.Faulted = true;
                instance.Enabled = false;
            }
        }

        void SetState(const ManagedBuildState state)
        {
            {
                std::scoped_lock lock(Mutex);
                Status.State = state;
            }
            StatusChanged.notify_all();
        }

        void RunBuild(const std::stop_token cancellation, ManagedBuildRequest request,
                      const ManagedBuildOperationId operation) noexcept
        {
            const auto staging = OutputRoot / "Generations" / std::to_string(operation.Value());
            const auto projectDirectory = OutputRoot / "Intermediate" / "Projects";
            const auto started = std::chrono::steady_clock::now();
            try
            {
                SetState(ManagedBuildState::Generating);
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not clear managed build staging directory.", staging,
                                                            error);
                std::filesystem::create_directories(staging);
                std::filesystem::create_directories(projectDirectory);
                WriteText(projectDirectory / "Directory.Build.props",
                          "<Project>\n  <PropertyGroup>\n"
                          "    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)..\\"
                          "$(MSBuildProjectName)\\</BaseIntermediateOutputPath>\n"
                          "    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</"
                          "MSBuildProjectExtensionsPath>\n"
                          "  </PropertyGroup>\n</Project>\n");

                auto generationManagedApi = ManagedApi;
                const auto managedApiProject = FindManagedApiProject();
                const auto managedApiOutput = staging / "ManagedApi";
                if (!managedApiProject.empty())
                {
                    std::filesystem::create_directories(managedApiOutput);
                    const auto managedApiIntermediate = OutputRoot / "Intermediate" / "ManagedApi";
                    const std::vector<std::string> managedApiArguments{
                        "build",
                        PathText(managedApiProject),
                        "--configuration",
                        "Release",
                        "--nologo",
                        "--output",
                        PathText(managedApiOutput),
                        "--property:BaseIntermediateOutputPath=" + PathText(managedApiIntermediate) + "/"};
                    auto managedApiProcess = Detail::ChildProcess::Start(Dotnet, managedApiArguments, staging);
                    while (!managedApiProcess.Poll())
                    {
                        if (cancellation.stop_requested())
                        {
                            managedApiProcess.Terminate();
                            throw ManagedBuildState::Cancelled;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    const auto managedApiBuildOutput = managedApiProcess.TakeOutput();
                    if (managedApiProcess.ExitCode().value_or(1) != 0)
                    {
                        throw std::runtime_error(managedApiBuildOutput.empty()
                                                     ? "Keire.Managed compilation failed without output."
                                                     : managedApiBuildOutput);
                    }
                    generationManagedApi = managedApiOutput / "Keire.Managed.dll";
                    if (!std::filesystem::is_regular_file(generationManagedApi))
                        throw std::runtime_error("Keire.Managed compilation published no API assembly.");
                }
                else if (!generationManagedApi.empty())
                {
                    std::filesystem::create_directories(managedApiOutput);
                    const auto immutableManagedApi = managedApiOutput / generationManagedApi.filename();
                    std::filesystem::copy_file(generationManagedApi, immutableManagedApi,
                                               std::filesystem::copy_options::overwrite_existing);
                    generationManagedApi = immutableManagedApi;
                }

                std::map<AssetId, std::string> names;
                for (const auto& assembly : request.Assemblies)
                    names.emplace(assembly.Asset, assembly.Definition.Name);
                for (const auto& assembly : request.Assemblies)
                    WriteText(projectDirectory / (assembly.Definition.Name + ".csproj"),
                              GenerateProject(assembly, names, ProjectRoot, projectDirectory, generationManagedApi, {},
                                              "net10.0", "14.0"));

                std::string aggregator = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n"
                                         "    <TargetFramework>net10.0</TargetFramework>\n"
                                         "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
                                         "  </PropertyGroup>\n  <ItemGroup>\n";
                for (const auto& assembly : request.Assemblies)
                    aggregator +=
                        "    <ProjectReference Include=\"" + XmlEscape(assembly.Definition.Name) + ".csproj\" />\n";
                aggregator += "  </ItemGroup>\n</Project>\n";
                const auto aggregatorPath = projectDirectory / "Keire.Managed.Build.csproj";
                WriteText(aggregatorPath, aggregator);
                if (cancellation.stop_requested())
                    throw ManagedBuildState::Cancelled;

                SetState(ManagedBuildState::Compiling);
                const std::vector<std::string> arguments{
                    "build",    PathText(aggregatorPath),        "--configuration", request.Configuration, "--nologo",
                    "--output", PathText(staging / "Assemblies")};
                auto process = Detail::ChildProcess::Start(Dotnet, arguments, staging);
                while (!process.Poll())
                {
                    if (cancellation.stop_requested())
                    {
                        process.Terminate();
                        throw ManagedBuildState::Cancelled;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                const auto output = process.TakeOutput();
                auto diagnostics = ParseDiagnostics(output, Specification.MaximumDiagnostics);
                if (process.ExitCode().value_or(1) != 0)
                {
                    if (diagnostics.empty())
                        diagnostics.push_back({ManagedDiagnosticSeverity::Error,
                                               {},
                                               0,
                                               0,
                                               "KEIRECS0001",
                                               output.empty() ? "Managed compilation failed without output." : output});
                    {
                        std::scoped_lock lock(Mutex);
                        Status.Diagnostics = std::move(diagnostics);
                        Status.State = ManagedBuildState::Failed;
                    }
                    StatusChanged.notify_all();
                    std::filesystem::remove_all(staging, error);
                    return;
                }

                SetState(ManagedBuildState::Publishing);
                {
                    std::scoped_lock lock(Mutex);
                    if (cancellation.stop_requested() || Status.Operation != operation)
                        throw ManagedBuildState::Cancelled;
                }
                const auto active = staging;
                Detail::WriteTextFileAtomically(OutputRoot / "active-generation.json",
                                                "{\"generation\":" + std::to_string(operation.Value()) +
                                                    ",\"directory\":\"" +
                                                    PathText(std::filesystem::relative(active, OutputRoot)) + "\"}\n");
                {
                    std::scoped_lock lock(Mutex);
                    Status.Diagnostics = std::move(diagnostics);
                    Status.ActiveAssemblyDirectory = active / "Assemblies";
                    Status.ManagedApiAssembly = generationManagedApi;
                    Status.Generation = operation.Value();
                    Status.Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started);
                    Status.State = ManagedBuildState::Succeeded;
                }
                StatusChanged.notify_all();
            }
            catch (const ManagedBuildState state)
            {
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                SetState(state);
            }
            catch (const std::exception& exception)
            {
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                {
                    std::scoped_lock lock(Mutex);
                    Status.Diagnostics = {
                        {ManagedDiagnosticSeverity::Error, {}, 0, 0, "KEIRECS0002", exception.what()}};
                    Status.State = ManagedBuildState::Failed;
                }
                StatusChanged.notify_all();
            }
        }

        ScriptSystemSpecification Specification;
        std::thread::id Owner;
        std::atomic<bool> Open{true};
        std::filesystem::path ProjectRoot;
        std::filesystem::path OutputRoot;
        std::filesystem::path Dotnet;
        std::filesystem::path ManagedApi;
        mutable std::mutex Mutex;
        mutable std::condition_variable StatusChanged;
        ManagedBuildStatus Status;
        Coral::HostInstance RuntimeHost;
        std::unique_ptr<Coral::AssemblyLoadContext> ActiveContext;
        std::unique_ptr<Coral::AssemblyLoadContext> CandidateContext;
        std::vector<BehaviourType> ActiveTypes;
        std::vector<BehaviourType> CandidateTypes;
        std::unordered_map<std::uint64_t, BehaviourInstance> Instances;
        std::vector<ComponentTypeId> InstalledComponentTypes;
        std::vector<ManagedRuntimeDiagnostic> RuntimeDiagnostics;
        std::uint64_t NextInstance = 1;
        std::shared_ptr<Impl*> Lifetime;
        ManagedReloadStatus Reload;
        std::string RuntimeException;
        bool RuntimeInitialized = false;
        std::uint64_t NextReload = 1;
        std::jthread Worker;
        std::uint64_t NextOperation = 1;
        static inline thread_local Impl* CurrentRuntime = nullptr;
    };

    class ScriptSystem::Impl::ManagedComponent final : public Component
    {
      public:
        using ScriptImpl = ScriptSystem::Impl;

        ManagedComponent(const ComponentTypeId componentType, std::string managedType,
                         std::weak_ptr<ScriptImpl*> lifetime)
            : Component(componentType), m_ManagedType(std::move(managedType)), m_Lifetime(std::move(lifetime))
        {
        }

      protected:
        void Awake() override
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    const auto* type = implementation.FindType(implementation.ActiveTypes, m_ManagedType);
                    if (!type)
                        return;
                    const auto owner = Owner();
                    const auto id = implementation.NextInstance++;
                    auto object = implementation.CreateObject(*type, id, owner.Id().Value());
                    const auto [instance, inserted] = implementation.Instances.emplace(
                        id,
                        BehaviourInstance{m_ManagedType, Type(), id, owner.Id().Value(), std::move(object), m_State});
                    (void)inserted;
                    instance->second.NativeEntity = owner;
                    m_Instance = ManagedBehaviourInstanceId(id);
                    if (!m_State.empty())
                        implementation.RestoreState(implementation.Instances.at(id).Object, m_State, true);
                    implementation.InvokeInstance(id, ManagedBehaviourCallback::Awake);
                });
        }

        void OnEnable() override { Invoke(ManagedBehaviourCallback::Enable); }
        void Start() override { Invoke(ManagedBehaviourCallback::Start); }
        void FixedUpdate(const float deltaSeconds) override
        {
            Invoke(ManagedBehaviourCallback::FixedUpdate, deltaSeconds);
        }
        void Update(const float deltaSeconds) override
        {
            Invoke(ManagedBehaviourCallback::Update, deltaSeconds);
            Invoke(ManagedBehaviourCallback::LateUpdate);
        }
        void OnDisable() override { Invoke(ManagedBehaviourCallback::Disable); }
        void OnDestroy() override
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!m_Instance)
                        return;
                    const auto found = implementation.Instances.find(m_Instance.Value());
                    if (found == implementation.Instances.end())
                        return;
                    std::exception_ptr failure;
                    try
                    {
                        if (found->second.Object.IsValid())
                            implementation.InvokeInstance(found->first, ManagedBehaviourCallback::Destroy);
                    }
                    catch (...)
                    {
                        failure = std::current_exception();
                    }
                    implementation.Instances.erase(found);
                    m_Instance = {};
                    if (failure)
                        std::rethrow_exception(failure);
                });
        }

      private:
        template <typename Function> void WithImplementation(Function&& function)
        {
            const auto lifetime = m_Lifetime.lock();
            if (!lifetime || !*lifetime || !(*lifetime)->Open.load(std::memory_order_acquire))
                return;
            function(**lifetime);
        }

        void Invoke(const ManagedBehaviourCallback callback, const float deltaSeconds = 0.0F)
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!m_Instance)
                        return;
                    const auto found = implementation.Instances.find(m_Instance.Value());
                    if (found == implementation.Instances.end() || !found->second.Object.IsValid())
                        return;
                    implementation.InvokeInstance(found->first, callback, deltaSeconds);
                });
        }

      public:
        [[nodiscard]] std::string SerializedState() const
        {
            auto& component = const_cast<ManagedComponent&>(*this);
            component.WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!component.m_Instance)
                        return;
                    const auto found = implementation.Instances.find(component.m_Instance.Value());
                    if (found == implementation.Instances.end() || !found->second.Object.IsValid())
                        return;
                    found->second.State = implementation.CaptureState(found->second.Object, true);
                    component.m_State = found->second.State;
                });
            return m_State;
        }

        void SetSerializedState(std::string state)
        {
            m_State = std::move(state);
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!m_Instance)
                        return;
                    const auto found = implementation.Instances.find(m_Instance.Value());
                    if (found == implementation.Instances.end() || !found->second.Object.IsValid())
                        return;
                    found->second.State = m_State;
                    implementation.RestoreState(found->second.Object, m_State, true);
                });
        }

      private:
        std::string m_ManagedType;
        std::weak_ptr<ScriptImpl*> m_Lifetime;
        ManagedBehaviourInstanceId m_Instance;
        std::string m_State = "{\"version\":1,\"fields\":[]}";
    };

    ScriptSystem::ScriptSystem(ScriptSystemSpecification specification) : m_Impl(std::make_unique<Impl>(specification))
    {
        if (specification.Mode == ScriptMode::Disabled || specification.ProjectRoot.empty() ||
            specification.AssemblyDirectory.empty() || specification.AssemblyDirectory.is_absolute() ||
            specification.MaximumDiagnostics == 0 || specification.MaximumDiagnostics > 65536)
            throw std::invalid_argument("ScriptSystem specification is invalid.");
        m_Impl->ProjectRoot = std::filesystem::absolute(specification.ProjectRoot).lexically_normal();
        if (!std::filesystem::is_directory(m_Impl->ProjectRoot))
            throw std::invalid_argument("ScriptSystem project root does not exist.");
        const auto sdk = ReadManagedSdkConfiguration(m_Impl->ProjectRoot,
                                                     {specification.SdkSelection, specification.DotnetExecutable});
        m_Impl->Specification.SdkSelection = sdk.Selection;
        m_Impl->Specification.DotnetExecutable = sdk.CustomExecutable;
        m_Impl->OutputRoot = (m_Impl->ProjectRoot / specification.AssemblyDirectory).lexically_normal();
        m_Impl->ResumeGenerationSequence();
        if (!specification.ManagedApiAssembly.empty())
        {
            m_Impl->ManagedApi = std::filesystem::absolute(specification.ManagedApiAssembly).lexically_normal();
            if (!std::filesystem::is_regular_file(m_Impl->ManagedApi))
                throw std::invalid_argument("The Keire.Managed API assembly does not exist.");
        }
        m_Impl->Dotnet = m_Impl->Specification.DotnetExecutable.empty()
                             ? std::filesystem::path{}
                             : ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
                                             m_Impl->ProjectRoot, m_Impl->Specification.RuntimeRootDirectory);
        m_Impl->InitializeRuntime();
    }

    ScriptSystem::~ScriptSystem() = default;
    bool ScriptSystem::IsOpen() const noexcept { return m_Impl->Open.load(std::memory_order_acquire); }

    ManagedIdeWorkspace ScriptSystem::GenerateIdeWorkspace(const ManagedBuildRequest& request,
                                                           const std::string_view solutionName)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        ValidateManagedAssemblyGraph(request.Assemblies);
        if (request.Assemblies.empty())
            throw std::invalid_argument("IDE workspace generation requires at least one managed assembly.");

        std::string safeName(solutionName);
        for (char& character : safeName)
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
                character = '_';
        if (safeName.empty())
            safeName = "Game";
        std::map<AssetId, std::string> names;
        for (const auto& assembly : request.Assemblies)
            names.emplace(assembly.Asset, assembly.Definition.Name);

        ManagedIdeWorkspace result;
        result.Solution = m_Impl->ProjectRoot / (safeName + ".sln");
        auto ideManagedApi = m_Impl->ManagedApi;
        auto ideManagedApiProject = m_Impl->FindManagedApiProject();
        if (!ideManagedApi.empty())
        {
            const auto referenceDirectory = m_Impl->ProjectRoot / "Library/ScriptAssemblies/References";
            const auto reference = referenceDirectory / ideManagedApi.filename();
            const auto contents = Detail::ReadTextFile(ideManagedApi, 64U * 1024U * 1024U);
            Detail::WriteFileAtomically(reference, std::as_bytes(std::span(contents)));
            ideManagedApi = reference;
            if (!ideManagedApiProject.empty())
            {
                const auto designTimeProject = referenceDirectory / "Keire.Managed.VisualStudio.csproj";
                Detail::WriteTextFileAtomically(
                    designTimeProject, GenerateManagedApiDesignTimeProject(ideManagedApiProject, designTimeProject));
                ideManagedApiProject = designTimeProject;
            }
        }
        for (const auto& assembly : request.Assemblies)
        {
            const auto project = m_Impl->ProjectRoot / (assembly.Definition.Name + ".csproj");
            Detail::WriteTextFileAtomically(project,
                                            GenerateProject(assembly, names, m_Impl->ProjectRoot, m_Impl->ProjectRoot,
                                                            ideManagedApi, ideManagedApiProject, "net8.0", "12.0"));
            result.Projects.push_back(project);
        }
        Detail::WriteTextFileAtomically(result.Solution,
                                        GenerateSolution(request, names, m_Impl->ProjectRoot, ideManagedApiProject));
        return result;
    }

    ManagedBuildOperationId ScriptSystem::StartBuild(ManagedBuildRequest request)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (request.Configuration != "Debug" && request.Configuration != "Release")
            throw std::invalid_argument("Managed build configuration must be Debug or Release.");
        ValidateManagedAssemblyGraph(request.Assemblies);
        if (request.Assemblies.empty())
            throw std::invalid_argument("Managed build requires at least one assembly.");
        if (m_Impl->Worker.joinable())
        {
            m_Impl->Worker.request_stop();
            m_Impl->Worker.join();
        }
        if (m_Impl->Dotnet.empty())
            m_Impl->Dotnet =
                ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
                              m_Impl->Specification.ProjectRoot, m_Impl->Specification.RuntimeRootDirectory);

        const ManagedBuildOperationId operation(m_Impl->NextOperation++);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Status = {.Operation = operation, .State = ManagedBuildState::Generating};
            for (const auto& assembly : request.Assemblies)
                m_Impl->Status.ChangedAssemblies.push_back(assembly.Definition.Name);
        }
        m_Impl->Worker = std::jthread([implementation = m_Impl.get(), request = std::move(request),
                                       operation](const std::stop_token cancellation) mutable
                                      { implementation->RunBuild(cancellation, std::move(request), operation); });
        return operation;
    }

    void ScriptSystem::CancelBuild(const ManagedBuildOperationId operation)
    {
        m_Impl->RequireOwner();
        if (!operation || BuildStatus().Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        if (m_Impl->Worker.joinable())
            m_Impl->Worker.request_stop();
    }

    bool ScriptSystem::WaitForBuild(const ManagedBuildOperationId operation,
                                    const std::chrono::milliseconds timeout) const
    {
        if (!operation || timeout.count() < 0)
            throw std::invalid_argument("Managed build wait operation or timeout is invalid.");
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Status.Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        return m_Impl->StatusChanged.wait_for(lock, timeout,
                                              [this, operation]
                                              {
                                                  if (m_Impl->Status.Operation != operation)
                                                      return true;
                                                  const auto state = m_Impl->Status.State;
                                                  return state == ManagedBuildState::Succeeded ||
                                                         state == ManagedBuildState::Failed ||
                                                         state == ManagedBuildState::Cancelled;
                                              });
    }

    ManagedBuildStatus ScriptSystem::BuildStatus() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Status;
    }

    ManagedSdkConfiguration ScriptSystem::SdkConfiguration() const
    {
        m_Impl->RequireOwner();
        return {m_Impl->Specification.SdkSelection, m_Impl->Specification.DotnetExecutable};
    }

    void ScriptSystem::ConfigureManagedSdk(const ManagedSdkSelection selection, std::filesystem::path customExecutable)
    {
        m_Impl->RequireOwner();
        const auto state = BuildStatus().State;
        if (state == ManagedBuildState::Generating || state == ManagedBuildState::Compiling ||
            state == ManagedBuildState::Publishing)
            throw std::logic_error("The managed SDK cannot be changed while a script build is active.");
        if (selection != ManagedSdkSelection::Custom)
            customExecutable.clear();
        if (m_Impl->Specification.SdkSelection == selection &&
            m_Impl->Specification.DotnetExecutable == customExecutable)
            return;
        m_Impl->Specification.SdkSelection = selection;
        m_Impl->Specification.DotnetExecutable = std::move(customExecutable);
        m_Impl->Dotnet.clear();
        WriteManagedSdkConfiguration(m_Impl->ProjectRoot,
                                     {m_Impl->Specification.SdkSelection, m_Impl->Specification.DotnetExecutable});
    }

    bool ScriptSystem::RuntimeHostAvailable() const noexcept { return IsOpen() && m_Impl->RuntimeInitialized; }

    bool ScriptSystem::PrepareReload(ManagedReloadRequest request)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!m_Impl->RuntimeInitialized)
            throw std::logic_error("The managed runtime host is unavailable.");
        if (request.Assemblies.empty())
            throw std::invalid_argument("Managed reload requires at least one assembly.");

        m_Impl->Unload(m_Impl->CandidateContext);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Preparing;
            m_Impl->Reload.Diagnostic.clear();
            m_Impl->Reload.RetainedState = std::move(request.State);
            m_Impl->RuntimeException.clear();
        }

        try
        {
            auto candidate = m_Impl->RuntimeHost.CreateAssemblyLoadContext(
                "Keire.Reload." + std::to_string(m_Impl->NextReload++), PathText(m_Impl->OutputRoot));
            m_Impl->CandidateContext = std::make_unique<Coral::AssemblyLoadContext>(candidate);
            Coral::Type* behaviourType = nullptr;
            Coral::Type* stableComponentIdType = nullptr;
            Coral::Type* executionOrderType = nullptr;
            Coral::Type* serializeFieldType = nullptr;
            Coral::Type* hideInInspectorType = nullptr;
            auto managedApiPath =
                request.ManagedApiAssembly.empty() ? m_Impl->ManagedApi : std::move(request.ManagedApiAssembly);
            if (!managedApiPath.empty())
            {
                if (managedApiPath.is_relative())
                    managedApiPath = m_Impl->ProjectRoot / managedApiPath;
                managedApiPath = std::filesystem::absolute(managedApiPath).lexically_normal();
                auto& managedApi = m_Impl->CandidateContext->LoadAssembly(PathText(managedApiPath));
                if (managedApi.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                    throw std::runtime_error("Managed reload rejected Keire.Managed (status " +
                                             std::to_string(static_cast<int>(managedApi.GetLoadStatus())) + ").");
                managedApi.AddInternalCall("Keire.NativeRuntime", "WriteLogIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeWriteLog));
                managedApi.AddInternalCall("Keire.NativeRuntime", "DeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "InputAxis2DIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeInputAxis2D));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetCursorVisibleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetCursorVisible));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetCursorLockedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetCursorLocked));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsCursorVisibleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsCursorVisible));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsCursorLockedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsCursorLocked));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalRotation));
                managedApi.UploadInternalCalls();
                behaviourType = &managedApi.GetLocalType("Keire.Behaviour");
                if (!*behaviourType)
                    throw std::runtime_error("Keire.Managed does not expose Keire.Behaviour.");
                const auto hasMethod = [behaviourType](const std::string_view expected)
                {
                    return std::ranges::any_of(behaviourType->GetMethods(),
                                               [expected](const Coral::MethodInfo& method)
                                               {
                                                   const Coral::ScopedString name(method.GetName());
                                                   return static_cast<std::string>(name) == expected;
                                               });
                };
                const auto hasField = [behaviourType](const std::string_view expected)
                {
                    return std::ranges::any_of(behaviourType->GetFields(),
                                               [expected](const Coral::FieldInfo& field)
                                               {
                                                   const Coral::ScopedString name(field.GetName());
                                                   return static_cast<std::string>(name) == expected;
                                               });
                };
                if (!hasField("RuntimeSerializedState") || !hasMethod("RuntimeCapturePersistentState") ||
                    !hasMethod("RuntimeRestorePersistentState") || !hasMethod("RuntimeCaptureReloadState") ||
                    !hasMethod("RuntimeRestoreReloadState"))
                {
                    throw std::runtime_error(
                        "Keire.Managed is stale and does not provide the managed state runtime contract. Rebuild the "
                        "KeireManaged project or regenerate the native workspace.");
                }
                stableComponentIdType = &managedApi.GetLocalType("Keire.StableComponentIdAttribute");
                executionOrderType = &managedApi.GetLocalType("Keire.ExecutionOrderAttribute");
                serializeFieldType = &managedApi.GetLocalType("Keire.SerializeFieldAttribute");
                hideInInspectorType = &managedApi.GetLocalType("Keire.HideInInspectorAttribute");
                if (!*stableComponentIdType || !*executionOrderType || !*serializeFieldType)
                    throw std::runtime_error("Keire.Managed does not expose managed component metadata.");
            }
            std::vector<std::string> availableTypes;
            std::vector<Impl::BehaviourType> candidateTypes;
            for (auto path : request.Assemblies)
            {
                if (path.is_relative())
                    path = m_Impl->ProjectRoot / path;
                path = std::filesystem::absolute(path).lexically_normal();
                if (!std::filesystem::is_regular_file(path))
                    throw std::runtime_error("Managed reload assembly does not exist: " + PathText(path));
                const auto& assembly = m_Impl->CandidateContext->LoadAssembly(PathText(path));
                if (assembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                    throw std::runtime_error("Managed reload rejected assembly '" + PathText(path) + "' (status " +
                                             std::to_string(static_cast<int>(assembly.GetLoadStatus())) + ").");
                if (behaviourType)
                {
                    for (const auto& type : assembly.GetLocalTypes())
                    {
                        if (!type || !type.IsSubclassOf(*behaviourType))
                            continue;
                        const Coral::ScopedString name(type.GetFullName());
                        const auto typeName = static_cast<std::string>(name);
                        availableTypes.push_back(typeName);

                        ComponentTypeId componentType;
                        std::int32_t executionOrder = 0;
                        for (auto attribute : type.GetAttributes())
                        {
                            if (attribute.GetType() == *stableComponentIdType)
                            {
                                componentType = ComponentTypeId(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                                                        attribute.GetFieldValue<std::uint64_t>("Low")));
                            }
                            else if (attribute.GetType() == *executionOrderType)
                            {
                                executionOrder = attribute.GetFieldValue<std::int32_t>("Order");
                            }
                        }
                        if (componentType)
                        {
                            candidateTypes.push_back(
                                {typeName, componentType, executionOrder, std::addressof(type),
                                 ReflectManagedProperties(type, *behaviourType, *serializeFieldType,
                                                          *hideInInspectorType ? hideInInspectorType : nullptr)});
                        }
                    }
                }
            }
            std::ranges::sort(availableTypes);
            if (std::adjacent_find(availableTypes.begin(), availableTypes.end()) != availableTypes.end())
                throw std::runtime_error("Managed reload contains duplicate Behaviour type names.");
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::ComponentType);
            if (std::ranges::adjacent_find(candidateTypes, {}, &Impl::BehaviourType::ComponentType) !=
                candidateTypes.end())
            {
                throw std::runtime_error("Managed reload contains duplicate stable component IDs.");
            }
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::Name);
            std::string runtimeException;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                runtimeException = m_Impl->RuntimeException;
                if (runtimeException.empty())
                {
                    m_Impl->CandidateTypes = std::move(candidateTypes);
                    m_Impl->Reload.AvailableTypes = std::move(availableTypes);
                    m_Impl->Reload.State = ManagedReloadState::Prepared;
                }
            }
            if (!runtimeException.empty())
                throw std::runtime_error(runtimeException);
            return true;
        }
        catch (const std::exception& error)
        {
            m_Impl->CandidateTypes.clear();
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = error.what();
            return false;
        }
    }

    void ScriptSystem::CommitReload()
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Reload.State != ManagedReloadState::Prepared || !m_Impl->CandidateContext)
                throw std::logic_error("No prepared managed reload is available.");
        }

        std::unordered_map<std::uint64_t, Impl::BehaviourInstance> migrated;
        std::unordered_map<std::uint64_t, std::string> rollback;
        migrated.reserve(m_Impl->Instances.size());
        try
        {
            for (auto& [id, instance] : m_Impl->Instances)
            {
                if (instance.Object.IsValid())
                {
                    rollback.emplace(id, m_Impl->CaptureState(instance.Object, false));
                    m_Impl->Invoke(instance.Object, "RuntimeBeforeReload");
                    instance.State = m_Impl->CaptureState(instance.Object, false);
                }
                Impl::BehaviourInstance replacement{
                    instance.TypeName, instance.ComponentType, instance.World, instance.Entity, {},
                    instance.State,    instance.Enabled,       false};
                replacement.NativeEntity = instance.NativeEntity;
                auto* type = m_Impl->FindType(m_Impl->CandidateTypes, instance.ComponentType);
                if (!type)
                    type = m_Impl->FindType(m_Impl->CandidateTypes, instance.TypeName);
                if (type)
                {
                    replacement.TypeName = type->Name;
                    replacement.ComponentType = type->ComponentType;
                    replacement.Object = m_Impl->CreateObject(*type, instance.World, instance.Entity);
                    m_Impl->RestoreState(replacement.Object, replacement.State, false);
                }
                migrated.emplace(id, std::move(replacement));
            }
        }
        catch (...)
        {
            for (auto& [id, state] : rollback)
            {
                const auto found = m_Impl->Instances.find(id);
                if (found != m_Impl->Instances.end() && found->second.Object.IsValid())
                {
                    m_Impl->RestoreState(found->second.Object, state, false);
                    m_Impl->Invoke(found->second.Object, "RuntimeResumeAfterFailedReload");
                }
            }
            m_Impl->CandidateTypes.clear();
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = "Managed reload migration failed; the last-good generation remains active.";
            throw;
        }

        auto previous = std::move(m_Impl->ActiveContext);
        m_Impl->Instances = std::move(migrated);
        m_Impl->ActiveContext = std::move(m_Impl->CandidateContext);
        m_Impl->ActiveTypes = std::move(m_Impl->CandidateTypes);
        m_Impl->Unload(previous);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            ++m_Impl->Reload.Generation;
            m_Impl->Reload.State = ManagedReloadState::Active;
            m_Impl->Reload.Diagnostic.clear();
        }
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            if (instance.Object.IsValid())
                m_Impl->InvokeInstance(id, ManagedBehaviourCallback::AfterReload);
        }
    }

    void ScriptSystem::CancelReload()
    {
        m_Impl->RequireOwner();
        m_Impl->CandidateTypes.clear();
        m_Impl->Unload(m_Impl->CandidateContext);
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Reload.State == ManagedReloadState::Preparing ||
            m_Impl->Reload.State == ManagedReloadState::Prepared)
            m_Impl->Reload.State = ManagedReloadState::Cancelled;
    }

    ManagedReloadStatus ScriptSystem::ReloadStatus() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Reload;
    }

    std::vector<ManagedBehaviourTypeDescriptor> ScriptSystem::BehaviourTypes() const
    {
        m_Impl->RequireOwner();
        std::vector<ManagedBehaviourTypeDescriptor> result;
        result.reserve(m_Impl->ActiveTypes.size());
        for (const auto& type : m_Impl->ActiveTypes)
        {
            const auto separator = type.Name.find_last_of('.');
            result.push_back({type.Name, separator == std::string::npos ? type.Name : type.Name.substr(separator + 1),
                              type.ComponentType, type.ExecutionOrder});
        }
        return result;
    }

    ManagedBehaviourInstanceId ScriptSystem::CreateBehaviour(std::string typeName, const std::uint64_t world,
                                                             const AssetId entity)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        const auto* type = m_Impl->FindType(m_Impl->ActiveTypes, typeName);
        if (!type)
            throw std::invalid_argument("The requested managed Behaviour type is unavailable.");
        const auto id = m_Impl->NextInstance++;
        auto object = m_Impl->CreateObject(*type, world, entity);
        m_Impl->Instances.emplace(
            id, Impl::BehaviourInstance{std::move(typeName), type->ComponentType, world, entity, std::move(object)});
        return ManagedBehaviourInstanceId(id);
    }

    void ScriptSystem::InvokeBehaviour(const ManagedBehaviourInstanceId instance,
                                       const ManagedBehaviourCallback callback, const float deltaSeconds)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!instance)
            throw std::invalid_argument("Managed Behaviour instance ID is invalid.");
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            throw std::invalid_argument("Managed Behaviour instance is unavailable.");
        m_Impl->InvokeInstance(found->first, callback, deltaSeconds);
    }

    bool ScriptSystem::DestroyBehaviour(const ManagedBehaviourInstanceId instance)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            return false;
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        std::exception_ptr failure;
        try
        {
            if (found->second.Object.IsValid())
                m_Impl->InvokeInstance(found->first, ManagedBehaviourCallback::Destroy);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        m_Impl->Instances.erase(found);
        if (failure)
            std::rethrow_exception(failure);
        return true;
    }

    std::vector<ManagedRuntimeDiagnostic> ScriptSystem::RuntimeDiagnostics() const
    {
        m_Impl->RequireOwner();
        return m_Impl->RuntimeDiagnostics;
    }

    bool ScriptSystem::RetryBehaviour(const ManagedBehaviourInstanceId instance)
    {
        m_Impl->RequireOwner();
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        found->second.Faulted = false;
        return true;
    }

    bool ScriptSystem::SetBehaviourEnabled(const ManagedBehaviourInstanceId instance, const bool enabled)
    {
        m_Impl->RequireOwner();
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        if (enabled)
            found->second.Faulted = false;
        m_Impl->InvokeInstance(found->first,
                               enabled ? ManagedBehaviourCallback::Enable : ManagedBehaviourCallback::Disable);
        return true;
    }

    void ScriptSystem::InstallManagedComponents(Ref<ComponentRegistry> registry)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        if (!registry)
            throw std::invalid_argument("Managed component installation requires a component registry.");

        std::vector<ComponentRegistration> registrations;
        registrations.reserve(m_Impl->ActiveTypes.size());
        for (const auto& type : m_Impl->ActiveTypes)
        {
            if (!type.ComponentType)
                continue;
            ComponentRegistration registration;
            registration.Type = type.ComponentType;
            const auto separator = type.Name.find_last_of('.');
            registration.Name = separator == std::string::npos ? type.Name : type.Name.substr(separator + 1);
            registration.Category = "Scripts";
            registration.ExecutionOrder = type.ExecutionOrder;
            registration.Properties = type.Properties;
            const auto componentType = type.ComponentType;
            const auto managedType = type.Name;
            const auto properties = type.Properties;
            const std::weak_ptr<Impl*> lifetime = m_Impl->Lifetime;
            registration.Factory = [componentType, managedType, lifetime]
            { return Ref<Component>(CreateRef<Impl::ManagedComponent>(componentType, managedType, lifetime)); };
            registration.Serialize = [properties](const Component& component)
            {
                const auto& managed = dynamic_cast<const Impl::ManagedComponent&>(component);
                return ProjectManagedState(managed.SerializedState(), properties);
            };
            registration.Deserialize =
                [properties](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
            {
                if (version != 1)
                    throw std::invalid_argument("Unsupported managed component state version.");
                auto& managed = dynamic_cast<Impl::ManagedComponent&>(component);
                const auto found = values.find("managedState");
                if (found == values.end())
                    return;
                const auto* state = std::get_if<std::string>(&found->second);
                if (!state)
                    throw std::invalid_argument("Managed component state blob is not text.");
                managed.SetSerializedState(ApplyManagedState(*state, values, properties));
            };
            registrations.push_back(std::move(registration));
            if (std::ranges::find(m_Impl->InstalledComponentTypes, componentType) ==
                m_Impl->InstalledComponentTypes.end())
            {
                m_Impl->InstalledComponentTypes.push_back(componentType);
            }
        }
        registry->ReplaceBatch(m_Impl->InstalledComponentTypes, std::move(registrations));
    }

    void ScriptSystem::SetRuntimeServices(IScriptRuntimeServices* services)
    {
        m_Impl->RequireOwner();
        m_Impl->Specification.RuntimeServices = services;
    }

    void ScriptSystem::Close()
    {
        m_Impl->RequireOwner();
        if (!m_Impl->Open.exchange(false, std::memory_order_acq_rel))
            return;
        m_Impl->StopWorker();
        m_Impl->ShutdownRuntime();
    }
} // namespace Keire
