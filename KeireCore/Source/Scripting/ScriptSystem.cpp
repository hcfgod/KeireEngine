#include "Keire/Scripting/ScriptSystem.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Component.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireInternal/Scripting/CoralLog.h"
#include "KeireInternal/Scripting/ManagedGenerationSequence.h"
#include "KeireInternal/Scripting/ManagedSdk.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/ManagedObject.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
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
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Keire
{
    namespace
    {
        struct NativeAnimatorState
        {
            float Speed = 1.0F;
            float NormalizedTime = 0.0F;
            std::uint8_t Playing = 0;
            std::uint8_t Paused = 0;
        };

        struct NativeAudioSourceProperties
        {
            std::uint64_t ClipHigh = 0;
            std::uint64_t ClipLow = 0;
            float Gain = 1.0F;
            float Pitch = 1.0F;
            float PositionSeconds = 0.0F;
            float DurationSeconds = 0.0F;
            std::uint32_t Priority = 128;
            std::uint8_t Loop = 0;
            std::uint8_t Spatial = 0;
            std::uint8_t PlaybackState = 0;
        };
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

        [[nodiscard]] std::string ManagedTypeName(Coral::Type& type)
        {
            const Coral::ScopedString scopedName(type.GetFullName());
            return static_cast<std::string>(scopedName);
        }

        [[nodiscard]] bool ManagedTypeIsEnum(Coral::Type& type)
        {
            auto& baseType = type.GetBaseType();
            return baseType && ManagedTypeName(baseType) == "System.Enum";
        }

        [[nodiscard]] std::optional<ComponentPropertyKind> ManagedFieldKind(Coral::Type& type)
        {
            const auto name = ManagedTypeName(type);
            if (name == "System.Boolean")
                return ComponentPropertyKind::Boolean;
            if (name == "System.SByte" || name == "System.Byte" || name == "System.Int16" || name == "System.UInt16" ||
                name == "System.Int32" || name == "System.UInt32" || name == "System.Int64" ||
                name == "System.UInt64" || name == "System.Char" || ManagedTypeIsEnum(type))
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
            if (name == "Keire.Entity")
                return ComponentPropertyKind::Entity;
            if (name == "Keire.UiButton")
                return ComponentPropertyKind::Entity;
            if (name == "Keire.KeireEvent" || name.starts_with("Keire.KeireEvent`"))
                return ComponentPropertyKind::Event;
            if (name.starts_with("Keire.AssetReference`1"))
                return ComponentPropertyKind::Asset;
            return std::nullopt;
        }

        [[nodiscard]] std::size_t ManagedEventArgumentCount(const std::string_view name)
        {
            const auto marker = name.find('`');
            if (marker == std::string_view::npos)
                return 0;
            std::size_t result = 0;
            for (auto index = marker + 1; index < name.size() && std::isdigit(static_cast<unsigned char>(name[index]));
                 ++index)
            {
                result = result * 10 + static_cast<std::size_t>(name[index] - '0');
            }
            return result;
        }

        [[nodiscard]] std::string ManagedAttributeText(Coral::Attribute& attribute, const std::string_view field)
        {
            const Coral::ScopedString value(attribute.GetFieldValue<Coral::String>(field));
            return static_cast<std::string>(value);
        }

        void ReflectManagedFieldSet(Coral::Type& ownerType, const Coral::Type& serializeFieldType,
                                    const Coral::Type* hideInInspectorType, const Coral::Type* serializableType,
                                    const Coral::Type* rangeType, const Coral::Type* tooltipType,
                                    const Coral::Type* groupType, const std::string_view prefix,
                                    const std::string_view inheritedGroup, const std::size_t depth,
                                    std::vector<std::int32_t>& typeStack, std::vector<ComponentProperty>& result)
        {
            for (auto field : ownerType.GetFields())
            {
                bool serialized = field.GetAccessibility() == Coral::TypeAccessibility::Public;
                bool hidden = false;
                std::optional<double> minimum;
                std::optional<double> maximum;
                std::string tooltip;
                std::string group(inheritedGroup);
                for (auto attribute : field.GetAttributes())
                {
                    if (attribute.GetType() == serializeFieldType)
                        serialized = true;
                    else if (hideInInspectorType && attribute.GetType() == *hideInInspectorType)
                        hidden = true;
                    else if (rangeType && attribute.GetType() == *rangeType)
                    {
                        minimum = attribute.GetFieldValue<double>("Minimum");
                        maximum = attribute.GetFieldValue<double>("Maximum");
                    }
                    else if (tooltipType && attribute.GetType() == *tooltipType)
                        tooltip = ManagedAttributeText(attribute, "Text");
                    else if (groupType && attribute.GetType() == *groupType)
                        group = ManagedAttributeText(attribute, "Name");
                }
                if (!serialized || hidden)
                    continue;

                const Coral::ScopedString scopedName(field.GetName());
                const auto name = static_cast<std::string>(scopedName);
                const auto key = prefix.empty() ? name : std::string(prefix) + "." + name;
                auto& fieldType = field.GetType();
                if (const auto kind = ManagedFieldKind(fieldType))
                {
                    if (std::ranges::find(result, key, &ComponentProperty::Key) != result.end())
                        continue;
                    ComponentProperty property;
                    property.Key = key;
                    property.DisplayName = ManagedFieldDisplayName(name);
                    property.Group = std::move(group);
                    property.Kind = *kind;
                    property.Minimum = minimum;
                    property.Maximum = maximum;
                    property.Step = *kind == ComponentPropertyKind::Integer ? 1.0 : 0.1;
                    property.Tooltip = std::move(tooltip);
                    if (*kind == ComponentPropertyKind::Event)
                        property.EventArgumentCount = ManagedEventArgumentCount(ManagedTypeName(fieldType));
                    if (*kind == ComponentPropertyKind::Asset &&
                        ManagedTypeName(fieldType).find("Keire.AudioClip") != std::string::npos)
                    {
                        property.ExpectedAssetType = AudioClipAsset::StaticType();
                    }
                    result.push_back(std::move(property));
                    continue;
                }
                if (!serializableType || depth >= 4 || fieldType.IsSZArray() ||
                    !fieldType.HasAttribute(*serializableType) ||
                    std::ranges::find(typeStack, fieldType.GetTypeId()) != typeStack.end())
                {
                    continue;
                }
                typeStack.push_back(fieldType.GetTypeId());
                const auto nestedGroup =
                    group.empty() ? ManagedFieldDisplayName(name) : group + " / " + ManagedFieldDisplayName(name);
                ReflectManagedFieldSet(fieldType, serializeFieldType, hideInInspectorType, serializableType, rangeType,
                                       tooltipType, groupType, key, nestedGroup, depth + 1, typeStack, result);
                typeStack.pop_back();
            }
        }

        [[nodiscard]] std::vector<ComponentProperty>
        ReflectManagedProperties(const Coral::Type& concreteType, const Coral::Type& behaviourType,
                                 const Coral::Type& serializeFieldType, const Coral::Type* hideInInspectorType,
                                 const Coral::Type* serializableType, const Coral::Type* rangeType,
                                 const Coral::Type* tooltipType, const Coral::Type* groupType)
        {
            std::vector<ComponentProperty> result;
            std::vector<std::int32_t> typeStack;
            auto* current = const_cast<Coral::Type*>(std::addressof(concreteType));
            while (*current && !(*current == behaviourType))
            {
                typeStack.push_back(current->GetTypeId());
                ReflectManagedFieldSet(*current, serializeFieldType, hideInInspectorType, serializableType, rangeType,
                                       tooltipType, groupType, {}, {}, 0, typeStack, result);
                typeStack.pop_back();
                current = std::addressof(current->GetBaseType());
            }
            return result;
        }

        [[nodiscard]] std::vector<ComponentMethod> ReflectManagedMethods(const Coral::Type& concreteType)
        {
            std::vector<ComponentMethod> result;
            for (auto method : concreteType.GetMethods())
            {
                const Coral::ScopedString scopedName(method.GetName());
                const auto name = static_cast<std::string>(scopedName);
                if (ManagedTypeName(method.GetReturnType()) != "System.Void" || name.starts_with("Runtime") ||
                    name.starts_with("get_") || name.starts_with("set_") || name == "Awake" || name == "OnEnable" ||
                    name == "Start" || name == "FixedUpdate" || name == "Update" || name == "LateUpdate" ||
                    name == "OnDisable" || name == "OnDestroy" || name == "OnCollisionEnter" ||
                    name == "OnCollisionStay" || name == "OnCollisionExit" || name == "OnTriggerEnter" ||
                    name == "OnTriggerStay" || name == "OnTriggerExit" || name == "OnAnimationEvent" ||
                    name == "OnAnimatorIk" || name == "OnBeforeReload" || name == "OnAfterReload")
                {
                    continue;
                }

                ComponentMethod reflected;
                reflected.Name = name;
                for (auto* parameter : method.GetParameterTypes())
                {
                    if (!parameter)
                        break;
                    reflected.ParameterTypes.push_back(ManagedTypeName(*parameter));
                }
                if (reflected.ParameterTypes.size() == method.GetParameterTypes().size())
                {
                    reflected.DisplayName = reflected.Name + "(";
                    for (std::size_t index = 0; index < reflected.ParameterTypes.size(); ++index)
                    {
                        if (index != 0)
                            reflected.DisplayName += ", ";
                        reflected.DisplayName += reflected.ParameterTypes[index];
                    }
                    reflected.DisplayName += ")";
                    result.push_back(std::move(reflected));
                }
            }
            std::ranges::sort(result,
                              [](const ComponentMethod& left, const ComponentMethod& right) {
                                  return left.Name != right.Name ? left.Name < right.Name
                                                                 : left.ParameterTypes < right.ParameterTypes;
                              });
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
            case ComponentPropertyKind::Event:
                return ComponentEventValue{};
            case ComponentPropertyKind::Curve:
                return Curve1D{};
            case ComponentPropertyKind::Gradient:
                return ColorGradient{};
            }
            throw std::logic_error("Unsupported managed Inspector property kind.");
        }

        [[nodiscard]] ComponentPropertyValue ReadManagedPropertyValue(const nlohmann::json& value,
                                                                      const ComponentPropertyKind kind)
        {
            const auto readUnsignedInteger = [](const nlohmann::json& source) -> std::optional<std::uint64_t>
            {
                if (source.is_number_unsigned())
                    return source.get<std::uint64_t>();
                if (!source.is_number_integer())
                    return std::nullopt;
                const auto signedValue = source.get<std::int64_t>();
                return signedValue < 0 ? std::nullopt
                                       : std::optional<std::uint64_t>(static_cast<std::uint64_t>(signedValue));
            };
            const auto readReferenceId = [&readUnsignedInteger](const nlohmann::json& source)
            {
                const auto* nested = JsonMember(source, "Id", "id");
                const auto& id = nested && nested->is_object() ? *nested : source;
                const auto* high = JsonMember(id, "High", "high");
                const auto* low = JsonMember(id, "Low", "low");
                if (!high || !low)
                    return AssetId{};
                const auto parsedHigh = readUnsignedInteger(*high);
                const auto parsedLow = readUnsignedInteger(*low);
                return parsedHigh && parsedLow ? AssetId(*parsedHigh, *parsedLow) : AssetId{};
            };
            if (value.is_null())
                return DefaultManagedPropertyValue(kind);
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
                return readReferenceId(value);
            case ComponentPropertyKind::Entity:
                return EntityId(readReferenceId(value));
            case ComponentPropertyKind::Event:
            {
                ComponentEventValue result;
                const auto* calls = JsonMember(value, "persistentCalls", "PersistentCalls");
                if (!calls || !calls->is_array())
                    return result;
                constexpr std::size_t maximumPersistentListeners = 256;
                for (const auto& call : *calls)
                {
                    if (!call.is_object() || result.Listeners.size() >= maximumPersistentListeners)
                        break;
                    ComponentEventListener listener;
                    if (const auto* enabled = JsonMember(call, "Enabled", "enabled"); enabled && enabled->is_boolean())
                        listener.Enabled = enabled->get<bool>();
                    if (const auto* target = JsonMember(call, "Target", "target"); target && target->is_object())
                        listener.Target = EntityId(readReferenceId(*target));
                    if (const auto* component = JsonMember(call, "Component", "component");
                        component && component->is_object())
                    {
                        listener.Component = ComponentTypeId(readReferenceId(*component));
                    }
                    if (const auto* method = JsonMember(call, "Method", "method"); method && method->is_string())
                        listener.Method = method->get<std::string>();
                    result.Listeners.push_back(std::move(listener));
                }
                return result;
            }
            case ComponentPropertyKind::Curve:
            case ComponentPropertyKind::Gradient:
                throw std::logic_error("Managed fields do not expose native authoring curve values.");
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
            {
                const auto asset = std::get<AssetId>(value);
                return {{"Id", {{"High", asset.High()}, {"Low", asset.Low()}}}};
            }
            case ComponentPropertyKind::Entity:
            {
                const auto entity = std::get<EntityId>(value).Value();
                return {{"Id", {{"High", entity.High()}, {"Low", entity.Low()}}}};
            }
            case ComponentPropertyKind::Event:
            {
                nlohmann::json listeners = nlohmann::json::array();
                for (const auto& listener : std::get<ComponentEventValue>(value).Listeners)
                {
                    const auto target = listener.Target.Value();
                    const auto component = listener.Component.Value();
                    listeners.push_back({{"Enabled", listener.Enabled},
                                         {"Target", {{"Id", {{"High", target.High()}, {"Low", target.Low()}}}}},
                                         {"Component", {{"High", component.High()}, {"Low", component.Low()}}},
                                         {"Method", listener.Method}});
                }
                return {{"persistentCalls", std::move(listeners)}};
            }
            case ComponentPropertyKind::Curve:
            case ComponentPropertyKind::Gradient:
                throw std::logic_error("Managed fields do not expose native authoring curve values.");
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
            nlohmann::json* legacyField = nullptr;
            for (auto& field : *fields)
            {
                const auto* fieldName = JsonMember(field, "Name", "name");
                if (!fieldName || !fieldName->is_string() || fieldName->get<std::string>() != name)
                    continue;
                const auto* stableId = JsonMember(field, "StableId", "stableId");
                if (stableId && stableId->is_string() && !stableId->get_ref<const std::string&>().empty())
                    return std::addressof(field);
                if (!legacyField)
                    legacyField = std::addressof(field);
            }
            return legacyField;
        }

        [[nodiscard]] const nlohmann::json* ManagedStateField(const nlohmann::json& document,
                                                              const std::string_view name)
        {
            const auto* fields = document.contains("Fields")   ? std::addressof(document["Fields"])
                                 : document.contains("fields") ? std::addressof(document["fields"])
                                                               : nullptr;
            if (!fields || !fields->is_array())
                return nullptr;
            const nlohmann::json* legacyField = nullptr;
            for (const auto& field : *fields)
            {
                const auto* fieldName = JsonMember(field, "Name", "name");
                if (!fieldName || !fieldName->is_string() || fieldName->get<std::string>() != name)
                    continue;
                const auto* stableId = JsonMember(field, "StableId", "stableId");
                if (stableId && stableId->is_string() && !stableId->get_ref<const std::string&>().empty())
                    return std::addressof(field);
                if (!legacyField)
                    legacyField = std::addressof(field);
            }
            return legacyField;
        }

        [[nodiscard]] std::vector<std::string_view> ManagedPropertyPath(const std::string_view path)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor <= path.size())
            {
                const auto separator = path.find('.', cursor);
                result.push_back(path.substr(cursor, separator == std::string_view::npos ? path.size() - cursor
                                                                                         : separator - cursor));
                if (separator == std::string_view::npos)
                    break;
                cursor = separator + 1;
            }
            return result;
        }

        [[nodiscard]] const nlohmann::json* ManagedStateValue(const nlohmann::json& document,
                                                              const std::string_view path)
        {
            const auto segments = ManagedPropertyPath(path);
            if (segments.empty())
                return nullptr;
            const auto* field = ManagedStateField(document, segments.front());
            const nlohmann::json* value = field ? JsonMember(*field, "Value", "value") : nullptr;
            for (std::size_t index = 1; value && index < segments.size(); ++index)
            {
                if (!value->is_object())
                    return nullptr;
                const auto found = value->find(std::string(segments[index]));
                value = found == value->end() ? nullptr : std::addressof(*found);
            }
            return value;
        }

        [[nodiscard]] nlohmann::json& EnsureManagedStateValue(nlohmann::json& document, nlohmann::json& fields,
                                                              const std::string_view path)
        {
            const auto segments = ManagedPropertyPath(path);
            if (segments.empty())
                throw std::invalid_argument("Managed Inspector property path is empty.");
            auto* field = ManagedStateField(document, segments.front());
            if (!field)
            {
                fields.push_back({{"StableId", ""},
                                  {"Name", std::string(segments.front())},
                                  {"Type", ""},
                                  {"Aliases", nlohmann::json::array()},
                                  {"Value", nullptr}});
                field = std::addressof(fields.back());
            }
            auto* value = std::addressof((*field)["Value"]);
            for (std::size_t index = 1; index < segments.size(); ++index)
            {
                if (!value->is_object())
                    *value = nlohmann::json::object();
                value = std::addressof((*value)[std::string(segments[index])]);
            }
            return *value;
        }

        [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                               const std::vector<ComponentProperty>& properties)
        {
            ComponentPropertyBag result{{"managedState", state}};
            auto document = nlohmann::json::parse(state);
            for (const auto& property : properties)
            {
                const auto* value = ManagedStateValue(document, property.Key);
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
                EnsureManagedStateValue(document, fields, property.Key) =
                    WriteManagedPropertyValue(value->second, property.Kind);
            }
            return document.dump();
        }

        [[nodiscard]] ManagedAssetPropertyDescriptor ParseManagedAssetPropertyDescriptor(const nlohmann::json& source)
        {
            ManagedAssetPropertyDescriptor result;
            result.StableFieldId = AssetId::Parse(source.at("stableFieldId").get<std::string>());
            result.Name = source.at("name").get<std::string>();
            result.DisplayName = source.at("displayName").get<std::string>();
            result.ManagedTypeName = source.at("managedTypeName").get<std::string>();
            const auto kind = source.at("kind").get<std::uint32_t>();
            if (kind > static_cast<std::uint32_t>(ManagedAssetPropertyKind::AssetReference))
                throw std::runtime_error("Managed asset metadata contains an unsupported property kind.");
            result.Kind = static_cast<ManagedAssetPropertyKind>(kind);
            result.ReadOnly = source.value("readOnly", false);
            result.Hidden = source.value("hidden", false);
            if (const auto found = source.find("minimum"); found != source.end())
                result.Minimum = found->get<double>();
            if (const auto found = source.find("maximum"); found != source.end())
                result.Maximum = found->get<double>();
            result.Header = source.value("header", std::string{});
            result.Tooltip = source.value("tooltip", std::string{});
            if (const auto found = source.find("expectedAssetType"); found != source.end())
                result.ExpectedAssetType = AssetTypeId(AssetId::Parse(found->get<std::string>()));
            if (const auto found = source.find("expectedManagedType"); found != source.end())
                result.ExpectedManagedType = ManagedTypeId::Parse(found->get<std::string>());
            result.IncludeDerivedAssetTypes = source.value("includeDerivedAssetTypes", true);
            if (const auto found = source.find("children"); found != source.end())
            {
                if (!found->is_array())
                    throw std::runtime_error("Managed asset metadata property children are malformed.");
                result.Children.reserve(found->size());
                for (const auto& child : *found)
                    result.Children.push_back(ParseManagedAssetPropertyDescriptor(child));
            }
            return result;
        }

        struct ManagedAssetMetadataResult final
        {
            std::vector<ManagedAssetTypeDescriptor> Types;
            std::vector<ManagedAssetTypeDiagnostic> Diagnostics;
        };

        [[nodiscard]] ManagedAssetMetadataResult ParseManagedAssetMetadata(const std::string_view text)
        {
            const auto document = nlohmann::json::parse(text);
            if (document.at("schemaVersion").get<std::uint32_t>() != 1)
                throw std::runtime_error("Keire.Managed returned an unsupported managed asset metadata schema.");
            const auto& types = document.at("types");
            const auto& diagnostics = document.at("diagnostics");
            if (!types.is_array() || !diagnostics.is_array())
                throw std::runtime_error("Keire.Managed returned malformed managed asset metadata.");

            ManagedAssetMetadataResult result;
            result.Types.reserve(types.size());
            for (const auto& source : types)
            {
                ManagedAssetTypeDescriptor descriptor;
                descriptor.StableTypeId = ManagedTypeId::Parse(source.at("stableTypeId").get<std::string>());
                descriptor.FullName = source.at("fullName").get<std::string>();
                descriptor.DisplayName = source.at("displayName").get<std::string>();
                if (const auto found = source.find("baseTypeId"); found != source.end())
                    descriptor.BaseTypeId = ManagedTypeId::Parse(found->get<std::string>());
                descriptor.MenuPath = source.value("menuPath", std::string{});
                descriptor.DefaultFileName = source.at("defaultFileName").get<std::string>();
                const auto& properties = source.at("properties");
                if (!properties.is_array())
                    throw std::runtime_error("Managed asset metadata properties are malformed.");
                descriptor.Properties.reserve(properties.size());
                for (const auto& property : properties)
                    descriptor.Properties.push_back(ParseManagedAssetPropertyDescriptor(property));
                ValidateManagedAssetTypeDescriptor(descriptor);
                result.Types.push_back(std::move(descriptor));
            }
            result.Diagnostics.reserve(diagnostics.size());
            for (const auto& source : diagnostics)
            {
                result.Diagnostics.push_back(
                    {source.at("typeName").get<std::string>(), source.at("message").get<std::string>()});
            }

            std::ranges::sort(result.Types, {}, &ManagedAssetTypeDescriptor::FullName);
            std::ranges::sort(result.Diagnostics,
                              [](const ManagedAssetTypeDiagnostic& left, const ManagedAssetTypeDiagnostic& right) {
                                  return left.TypeName != right.TypeName ? left.TypeName < right.TypeName
                                                                         : left.Message < right.Message;
                              });
            std::set<ManagedTypeId> stableTypeIds;
            std::set<std::string, std::less<>> fullNames;
            std::set<std::string, std::less<>> menuPaths;
            for (const auto& descriptor : result.Types)
            {
                if (!stableTypeIds.emplace(descriptor.StableTypeId).second ||
                    !fullNames.emplace(descriptor.FullName).second)
                {
                    throw std::runtime_error("Managed asset metadata contains duplicate type names or stable IDs.");
                }
                if (!descriptor.MenuPath.empty() && !menuPaths.emplace(descriptor.MenuPath).second)
                    throw std::runtime_error("Managed asset metadata contains duplicate CreateAssetMenu paths.");
            }
            return result;
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
            std::vector<ComponentMethod> Methods;
            std::vector<ComponentTypeId> RequiredComponents;
        };

        struct CallbackProfile final
        {
            std::uint64_t Invocations = 0;
            std::uint64_t SkippedInvocations = 0;
            double Milliseconds = 0.0;
            double MaximumMilliseconds = 0.0;
        };

        static constexpr std::size_t CallbackProfileCount =
            static_cast<std::size_t>(ManagedBehaviourCallback::AnimatorIk) + 1;

        struct BehaviourInstance final
        {
            std::string TypeName;
            ComponentTypeId ComponentType;
            std::uint64_t World = 0;
            AssetId Entity;
            Coral::ManagedObject Object;
            std::string State = "{\"version\":1,\"fields\":[]}";
            CallbackProfile CallbackProfiles[CallbackProfileCount]{};
            bool Enabled = true;
            bool Faulted = false;
            Keire::Entity NativeEntity;
            std::uint32_t CallbackMask = ~std::uint32_t{0};
        };

        struct ManagedAssetSource final
        {
            AssetHandle<ManagedDataAsset> Handle;
            std::uint64_t ObservedRevision = 0;
        };

        struct PendingManagedAssetLoad final
        {
            AssetHandle<ManagedDataAsset> Handle;
            std::uint64_t Generation = 0;
        };

        using ManagedJobCallback = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t);

        struct ManagedJobRecord final
        {
            JobHandle Work;
            std::uint64_t Generation = 0;
        };

        static constexpr std::uint32_t FixedUpdateCallback = 1U << 0;
        static constexpr std::uint32_t UpdateCallback = 1U << 1;
        static constexpr std::uint32_t LateUpdateCallback = 1U << 2;
        static constexpr std::uint32_t AnimatorIkCallback = 1U << 3;

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

        explicit Impl(ScriptSystemSpecification value, Ref<JobSystem> jobs)
            : Specification(std::move(value)), Owner(std::this_thread::get_id()),
              Lifetime(std::make_shared<Impl*>(this)), Scheduler(std::move(jobs))
        {
            if (!Scheduler)
            {
                JobSystemSpecification jobsSpecification;
                jobsSpecification.WorkerCount = 1;
                jobsSpecification.BlockingWorkerCount = 1;
                Scheduler = CreateRef<JobSystem>(jobsSpecification);
                OwnScheduler = true;
            }
            WorkScope = Scheduler->CreateScope("Managed builds");
            ManagedJobs = Scheduler->CreateScope("Managed assembly jobs");
        }

        ~Impl()
        {
            StopWorker();
            if (WorkScope)
            {
                WorkScope->Cancel();
                WorkScope->Wait();
            }
            DrainManagedJobs(false);
            if (OwnScheduler && Scheduler)
                Scheduler->Close();
            *Lifetime = nullptr;
            ShutdownRuntime();
        }

        void RequireOwner() const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error("ScriptSystem operation must run on the owner thread.");
        }

        [[nodiscard]] Coral::ManagedObject
        HydrateManagedAsset(const ManagedDataAsset& source,
                            const std::map<ManagedTypeId, const Coral::Type*>& runtimeTypes)
        {
            const auto found = runtimeTypes.find(source.Definition().ManagedType);
            if (found == runtimeTypes.end() || !found->second)
            {
                throw std::runtime_error("Managed data type '" + source.Definition().ManagedTypeName +
                                         "' is unavailable in the target script generation.");
            }
            auto object = const_cast<Coral::Type*>(found->second)->CreateInstance();
            if (!object.IsValid())
                throw std::runtime_error("Managed data type '" + source.Definition().ManagedTypeName +
                                         "' could not be constructed.");
            const auto encoded = ManagedDataAsset::Encode(source.Definition());
            const std::string document(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            const RuntimeScope scope(*this);
            object.InvokeMethod("RuntimeHydrateManagedData", document);
            return object;
        }

        void InstallManagedAssetGeneration(Coral::Type& nativeRuntime, const std::uint64_t generation)
        {
            if (generation == 0)
                throw std::invalid_argument("Managed asset generation must be non-zero.");
            if (Specification.MaximumManagedDataAssets == 0 ||
                Specification.MaximumManagedDataAssets >
                    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
                Specification.MaximumManagedDataLoads == 0 ||
                Specification.MaximumManagedDataLoads > Specification.MaximumManagedDataAssets ||
                Specification.MaximumManagedDataLoads >
                    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw std::invalid_argument("Managed data asset capacities are invalid.");
            }
            const RuntimeScope scope(*this);
            nativeRuntime.InvokeStaticMethod("InstallManagedAssetGeneration", generation,
                                             static_cast<std::int32_t>(Specification.MaximumManagedDataAssets),
                                             static_cast<std::int32_t>(Specification.MaximumManagedDataLoads));
        }

        void ResetManagedAssetGeneration(const Coral::Type* nativeRuntime, const std::uint64_t generation) noexcept
        {
            if (!nativeRuntime || generation == 0)
                return;
            try
            {
                const RuntimeScope scope(*this);
                (void)nativeRuntime->InvokeStaticMethod<bool>("ResetManagedAssets", generation);
            }
            catch (...)
            {
            }
        }

        void ResumeGenerationSequence() { NextOperation = Detail::NextManagedGeneration(OutputRoot); }

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
            BuildCancellation.request_stop();
            if (Worker)
            {
                (void)Worker.Wait();
                Worker = {};
            }
        }

        void InitializeRuntime()
        {
            if (Specification.RuntimeHostDirectory.empty())
                return;
            const auto directory = std::filesystem::absolute(Specification.RuntimeHostDirectory).lexically_normal();
            if (!std::filesystem::is_directory(directory))
                throw std::invalid_argument("The managed runtime host directory does not exist.");
            auto settings = Detail::CreateCoralHostSettings(PathText(directory));
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
            DrainManagedJobs(false);
            ResetManagedAssetGeneration(CandidateNativeRuntimeType, Reload.Generation + 1);
            ResetManagedAssetGeneration(ActiveNativeRuntimeType, Reload.Generation);
            {
                std::scoped_lock lock(ManagedAssetMutex);
                PendingManagedAssetLoads.clear();
                ManagedAssetSources.clear();
            }
            Instances.clear();
            ActiveTypes.clear();
            CandidateTypes.clear();
            ActiveManagedAssetTypes.clear();
            CandidateManagedAssetTypes.clear();
            ActiveManagedAssetDiagnostics.clear();
            CandidateManagedAssetDiagnostics.clear();
            ActiveManagedAssetRuntimeTypes.clear();
            CandidateManagedAssetRuntimeTypes.clear();
            ActiveNativeRuntimeType = nullptr;
            CandidateNativeRuntimeType = nullptr;
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

        void DrainManagedJobs(const bool recreate) noexcept
        {
            Ref<JobScope> jobs;
            {
                std::scoped_lock lock(ManagedJobMutex);
                jobs = std::move(ManagedJobs);
            }
            if (jobs)
            {
                jobs->Cancel();
                jobs->Wait();
            }
            {
                std::scoped_lock lock(ManagedJobMutex);
                ManagedJobRecords.clear();
            }
            if (recreate && Scheduler && Scheduler->IsOpen())
            {
                try
                {
                    auto replacement = Scheduler->CreateScope("Managed assembly jobs");
                    std::scoped_lock lock(ManagedJobMutex);
                    ManagedJobs = std::move(replacement);
                }
                catch (...)
                {
                }
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
            const auto found =
                std::ranges::find_if(CurrentRuntime->Instances, [world](const auto& entry)
                                     { return entry.second.World == world && entry.second.NativeEntity; });
            return found == CurrentRuntime->Instances.end() ? Entity{}
                                                            : found->second.NativeEntity.Resolve(EntityId(id));
        }

        static std::uint8_t RuntimeRequestManagedAssetLoad(const std::uint64_t generation, const std::uint64_t high,
                                                           const std::uint64_t low) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Assets)
                return 0;
            try
            {
                {
                    std::scoped_lock lock(CurrentRuntime->Mutex);
                    const bool active = CurrentRuntime->Reload.Generation == generation &&
                                        CurrentRuntime->Reload.State == ManagedReloadState::Active;
                    const bool candidate =
                        CurrentRuntime->Reload.State == ManagedReloadState::Prepared &&
                        CurrentRuntime->Reload.Generation != std::numeric_limits<std::uint64_t>::max() &&
                        CurrentRuntime->Reload.Generation + 1 == generation;
                    if (!active && !candidate)
                        return 0;
                }
                const AssetId id(high, low);
                if (!id)
                    return 0;
                std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
                if (CurrentRuntime->PendingManagedAssetLoads.contains(id) ||
                    CurrentRuntime->ManagedAssetSources.contains(id) ||
                    CurrentRuntime->PendingManagedAssetLoads.size() >=
                        CurrentRuntime->Specification.MaximumManagedDataLoads)
                {
                    return 0;
                }
                CurrentRuntime->PendingManagedAssetLoads.emplace(
                    id, PendingManagedAssetLoad{CurrentRuntime->Assets->Load<ManagedDataAsset>(id, AssetPriority::High),
                                                generation});
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        static void RuntimeCancelManagedAssetLoad(const std::uint64_t generation, const std::uint64_t high,
                                                  const std::uint64_t low) noexcept
        {
            if (!CurrentRuntime)
                return;
            try
            {
                std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
                const auto found = CurrentRuntime->PendingManagedAssetLoads.find(AssetId(high, low));
                if (found != CurrentRuntime->PendingManagedAssetLoads.end() && found->second.Generation == generation)
                    CurrentRuntime->PendingManagedAssetLoads.erase(found);
            }
            catch (...)
            {
            }
        }

        static void RuntimeReleaseManagedAsset(const std::uint64_t generation, const std::uint64_t high,
                                               const std::uint64_t low) noexcept
        {
            if (!CurrentRuntime)
                return;
            try
            {
                {
                    std::scoped_lock lock(CurrentRuntime->Mutex);
                    if (CurrentRuntime->Reload.Generation != generation)
                        return;
                }
                std::scoped_lock lock(CurrentRuntime->ManagedAssetMutex);
                const AssetId id(high, low);
                CurrentRuntime->PendingManagedAssetLoads.erase(id);
                CurrentRuntime->ManagedAssetSources.erase(id);
            }
            catch (...)
            {
            }
        }

        static std::uint64_t RuntimeSubmitManagedJob(const std::uint64_t* dependencyIds,
                                                     const std::int32_t dependencyCount, const std::uint8_t priority,
                                                     const std::uint8_t jobClass, const Coral::String name, void* state,
                                                     const ManagedJobCallback callback) noexcept
        {
            auto* runtime = CurrentRuntime;
            if (!runtime || !callback || !state || dependencyCount < 0 || dependencyCount > 1024 ||
                priority > static_cast<std::uint8_t>(JobPriority::Background) ||
                jobClass > static_cast<std::uint8_t>(JobClass::Blocking))
                return 0;
            JobHandle work;
            bool completionInstalled = false;
            try
            {
                std::uint64_t generation = 0;
                {
                    std::scoped_lock lock(runtime->Mutex);
                    if (runtime->Reload.State != ManagedReloadState::Active)
                        return 0;
                    generation = runtime->Reload.Generation;
                }
                JobDescription description;
                description.Name = static_cast<std::string>(name);
                if (description.Name.empty())
                    description.Name = "Managed callback";
                description.Priority = static_cast<JobPriority>(priority);
                description.Class = static_cast<JobClass>(jobClass);
                std::uint64_t id = 0;
                const auto lifetime = std::weak_ptr<Impl*>(runtime->Lifetime);
                std::scoped_lock lock(runtime->ManagedJobMutex);
                if (!runtime->ManagedJobs)
                    return 0;
                if (runtime->ManagedJobRecords.size() >= 65536)
                {
                    std::set<std::uint64_t> retainedDependencies;
                    for (std::int32_t index = 0; index < dependencyCount; ++index)
                        retainedDependencies.emplace(dependencyIds[index]);
                    std::erase_if(runtime->ManagedJobRecords,
                                  [&](const auto& entry)
                                  {
                                      const auto status = entry.second.Work.Status();
                                      return !retainedDependencies.contains(entry.first) &&
                                             (status == JobStatus::Succeeded || status == JobStatus::Failed ||
                                              status == JobStatus::Cancelled);
                                  });
                    if (runtime->ManagedJobRecords.size() >= 65536)
                        return 0;
                }
                for (std::int32_t index = 0; index < dependencyCount; ++index)
                {
                    const auto found = runtime->ManagedJobRecords.find(dependencyIds[index]);
                    if (found == runtime->ManagedJobRecords.end() || found->second.Generation != generation)
                        return 0;
                    description.Dependencies.push_back(found->second.Work);
                }
                if (runtime->NextManagedJob == 0)
                    return 0;
                id = runtime->NextManagedJob++;
                work = runtime->ManagedJobs->Submit(
                    std::move(description),
                    [lifetime, callback, state](JobContext& context)
                    {
                        const auto locked = lifetime.lock();
                        if (!locked || !*locked)
                            throw std::runtime_error("Managed job runtime is unavailable.");
                        RuntimeScope scope(**locked);
                        std::stop_callback stop(context.StopToken(), [&] { (void)callback(state, 4, 0); });
                        if (callback(state, 0, context.StopRequested() ? 1 : 0) != 0)
                            throw std::runtime_error("Managed job callback failed.");
                    });
                work.OnComplete(
                    [lifetime, callback, state](const JobResult& result)
                    {
                        const auto locked = lifetime.lock();
                        if (!locked || !*locked)
                            return;
                        RuntimeScope scope(**locked);
                        const auto phase = result.Status == JobStatus::Succeeded ? std::uint8_t{1}
                                           : result.Status == JobStatus::Failed  ? std::uint8_t{2}
                                                                                 : std::uint8_t{3};
                        (void)callback(state, phase, 0);
                    });
                completionInstalled = true;
                runtime->ManagedJobRecords.emplace(id, ManagedJobRecord{work, generation});
                return id;
            }
            catch (...)
            {
                if (work)
                {
                    work.Cancel();
                    (void)work.Wait();
                    if (!completionInstalled)
                    {
                        const auto result = work.Result();
                        const auto phase = result.Status == JobStatus::Succeeded ? std::uint8_t{1}
                                           : result.Status == JobStatus::Failed  ? std::uint8_t{2}
                                                                                 : std::uint8_t{3};
                        (void)callback(state, phase, 0);
                    }
                }
                return 0;
            }
        }

        static void RuntimeCancelManagedJob(const std::uint64_t id) noexcept
        {
            if (!CurrentRuntime || id == 0)
                return;
            JobHandle job;
            {
                std::scoped_lock lock(CurrentRuntime->ManagedJobMutex);
                const auto found = CurrentRuntime->ManagedJobRecords.find(id);
                if (found != CurrentRuntime->ManagedJobRecords.end())
                    job = found->second.Work;
            }
            job.Cancel();
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

        static void RuntimeRegisterProfileName(const std::uint64_t id, const Coral::String name) noexcept
        {
            if (!CurrentRuntime || id == 0)
                return;
            try
            {
                std::scoped_lock lock(CurrentRuntime->Mutex);
                CurrentRuntime->ProfileNames.insert_or_assign(id, static_cast<std::string>(name));
            }
            catch (...)
            {
            }
        }

        static void RuntimeRecordProfileSpan(const std::uint64_t id, const double startMicroseconds,
                                             const double durationMicroseconds) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return;
            try
            {
                std::scoped_lock lock(CurrentRuntime->Mutex);
                const auto found = CurrentRuntime->ProfileNames.find(id);
                if (found != CurrentRuntime->ProfileNames.end())
                {
                    CurrentRuntime->Specification.RuntimeServices->RecordManagedProfileSpan(
                        found->second, startMicroseconds, durationMicroseconds);
                }
            }
            catch (...)
            {
            }
        }

        static void RuntimeSetProfileCounter(const std::uint64_t id, const double value) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return;
            try
            {
                std::scoped_lock lock(CurrentRuntime->Mutex);
                const auto found = CurrentRuntime->ProfileNames.find(id);
                if (found != CurrentRuntime->ProfileNames.end())
                    CurrentRuntime->Specification.RuntimeServices->SetManagedProfileCounter(found->second, value);
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

        [[nodiscard]] static float RuntimeFixedDeltaTime() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                       ? CurrentRuntime->Specification.RuntimeServices->ManagedFixedDeltaTime()
                       : 1.0F / 60.0F;
        }

        [[nodiscard]] static float RuntimeUnscaledDeltaTime() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                       ? CurrentRuntime->Specification.RuntimeServices->ManagedUnscaledDeltaTime()
                       : 0.0F;
        }

        [[nodiscard]] static double RuntimeElapsedTime() noexcept
        {
            return CurrentRuntime && CurrentRuntime->Specification.RuntimeServices
                       ? CurrentRuntime->Specification.RuntimeServices->ManagedElapsedTime()
                       : 0.0;
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

        [[nodiscard]] static std::uint8_t RuntimeInputState(const Coral::String action) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                return static_cast<std::uint8_t>(CurrentRuntime->Specification.RuntimeServices->ReadManagedInputState(
                    static_cast<std::string>(action)));
            }
            catch (...)
            {
                return 0;
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

        [[nodiscard]] static Quaternion RuntimeGetWorldRotation(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!transform)
                return Quaternion{};
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
            return Math::DecomposeTransform(transform->WorldMatrix(), position, rotation, scale) ? rotation
                                                                                                 : Quaternion{};
        }

        [[nodiscard]] static Ref<CharacterControllerComponent>
        RuntimeCharacterController(const std::uint64_t world, const std::uint64_t high,
                                   const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity ? entity.GetComponent<CharacterControllerComponent>() : Ref<CharacterControllerComponent>{};
        }

        [[nodiscard]] static std::uint8_t RuntimeMoveCharacterController(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low,
                                                                         const Vector3 displacement) noexcept
        {
            try
            {
                const auto character = RuntimeCharacterController(world, high, low);
                return character && character->Enabled() && character->QueueDesiredMovement(displacement) ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeGetCharacterControllerState(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                           std::uint8_t* grounded, Vector3* normal, Vector3* velocity) noexcept
        {
            const auto character = RuntimeCharacterController(world, high, low);
            if (!character || !grounded || !normal || !velocity)
                return 0;
            const auto state = character->RuntimeState();
            *grounded = state.Grounded ? 1 : 0;
            *normal = state.GroundNormal;
            *velocity = state.Velocity;
            return 1;
        }

        [[nodiscard]] static Ref<AnimatorComponent> RuntimeAnimator(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity ? entity.GetComponent<AnimatorComponent>() : Ref<AnimatorComponent>{};
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorFloat(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low,
                                                                  const Coral::String parameter,
                                                                  const float value) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->SetFloat(static_cast<std::string>(parameter), value);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorInteger(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::int32_t value) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->SetInteger(static_cast<std::string>(parameter), value);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorBoolean(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::uint8_t value) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->SetBool(static_cast<std::string>(parameter), value != 0);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorTrigger(const std::uint64_t world, const std::uint64_t high,
                                                                    const std::uint64_t low,
                                                                    const Coral::String parameter,
                                                                    const std::uint8_t set) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                if (set != 0)
                    animator->SetTrigger(static_cast<std::string>(parameter));
                else
                    animator->ResetTrigger(static_cast<std::string>(parameter));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorLayerWeight(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                      const Coral::String layer, const float value) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->SetLayerWeight(static_cast<std::string>(layer), value);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePlayAnimator(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low, const Coral::String state,
                                                              const Coral::String layer,
                                                              const float normalizedTime) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->Play(static_cast<std::string>(state), static_cast<std::string>(layer), normalizedTime);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeCrossFadeAnimator(const std::uint64_t world, const std::uint64_t high,
                                                                   const std::uint64_t low, const Coral::String state,
                                                                   const Coral::String layer, const float duration,
                                                                   const float normalizedTime) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->CrossFade(static_cast<std::string>(state), duration, static_cast<std::string>(layer),
                                    normalizedTime);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePauseAnimator(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low,
                                                               const std::uint8_t paused) noexcept
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetPaused(paused != 0);
            return 1;
        }

        [[nodiscard]] static std::uint8_t RuntimeStopAnimator(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->Stop();
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAnimatorSpeed(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low, const float speed) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                animator->SetSpeed(speed);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeGetAnimatorState(const std::uint64_t world, const std::uint64_t high,
                                                                  const std::uint64_t low,
                                                                  NativeAnimatorState* state) noexcept
        {
            if (!state)
                return 0;
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            state->Speed = animator->Speed();
            state->NormalizedTime = animator->NormalizedTime();
            state->Playing = animator->RuntimePlaying() ? 1 : 0;
            state->Paused = animator->Paused() ? 1 : 0;
            return 1;
        }

        [[nodiscard]] static std::int32_t RuntimeGetAnimatorStateName(const std::uint64_t world,
                                                                      const std::uint64_t high, const std::uint64_t low,
                                                                      char* destination,
                                                                      const std::int32_t capacity) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator)
                    return 0;
                const auto name = animator->CurrentState();
                if (destination && capacity > 0)
                    std::copy_n(name.begin(), std::min(name.size(), static_cast<std::size_t>(capacity)), destination);
                return static_cast<std::int32_t>(
                    std::min<std::size_t>(name.size(), std::numeric_limits<std::int32_t>::max()));
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorTwoBoneIk(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                    const Coral::String goal, const Coral::String root, const Coral::String middle,
                                    const Coral::String end, const Vector3 target, const Vector3 pole,
                                    const float weight, const std::uint8_t space) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator || space > static_cast<std::uint8_t>(AnimatorIkSpace::World))
                    return 0;
                animator->SetTwoBoneIk(static_cast<std::string>(goal), static_cast<std::string>(root),
                                       static_cast<std::string>(middle), static_cast<std::string>(end), target, pole,
                                       weight, static_cast<AnimatorIkSpace>(space));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetAnimatorFabrikIk(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                   const Coral::String goal, const Coral::String encodedBones, const Vector3 target,
                                   const float weight, const std::uint32_t maximumIterations, const float tolerance,
                                   const std::uint8_t space) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                if (!animator || space > static_cast<std::uint8_t>(AnimatorIkSpace::World))
                    return 0;
                const auto encoded = static_cast<std::string>(encodedBones);
                std::vector<std::string> bones;
                std::size_t offset = 0;
                while (offset <= encoded.size())
                {
                    const auto next = encoded.find('\x1f', offset);
                    bones.emplace_back(encoded.substr(offset, next == std::string::npos ? next : next - offset));
                    if (next == std::string::npos)
                        break;
                    offset = next + 1;
                }
                animator->SetFabrikIk(static_cast<std::string>(goal), std::move(bones), target, weight,
                                      maximumIterations, tolerance, static_cast<AnimatorIkSpace>(space));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeClearAnimatorIk(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low,
                                                                 const Coral::String goal) noexcept
        {
            try
            {
                const auto animator = RuntimeAnimator(world, high, low);
                return animator && animator->ClearIk(static_cast<std::string>(goal)) ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeTryGetAnimatorFloat(const std::uint64_t world,
                                                                     const std::uint64_t high, const std::uint64_t low,
                                                                     const Coral::String parameter,
                                                                     float* value) noexcept
        {
            try
            {
                if (!value)
                    return 0;
                const auto animator = RuntimeAnimator(world, high, low);
                const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
                const auto name = static_cast<std::string>(parameter);
                if (!snapshot)
                    return 0;
                const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                        { return candidate.Name == name || candidate.Id == name; });
                if (found == snapshot->Parameters.end() || found->Type != AnimationParameterType::Float)
                    return 0;
                *value = found->FloatValue;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorInteger(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                     const Coral::String parameter, std::int32_t* value) noexcept
        {
            try
            {
                if (!value)
                    return 0;
                const auto animator = RuntimeAnimator(world, high, low);
                const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
                const auto name = static_cast<std::string>(parameter);
                if (!snapshot)
                    return 0;
                const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                        { return candidate.Name == name || candidate.Id == name; });
                if (found == snapshot->Parameters.end() || found->Type != AnimationParameterType::Integer)
                    return 0;
                *value = found->IntegerValue;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorBoolean(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                     const Coral::String parameter, std::uint8_t* value) noexcept
        {
            try
            {
                if (!value)
                    return 0;
                const auto animator = RuntimeAnimator(world, high, low);
                const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
                const auto name = static_cast<std::string>(parameter);
                if (!snapshot)
                    return 0;
                const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                        { return candidate.Name == name || candidate.Id == name; });
                if (found == snapshot->Parameters.end() ||
                    (found->Type != AnimationParameterType::Boolean && found->Type != AnimationParameterType::Trigger))
                    return 0;
                *value = found->BooleanValue ? 1 : 0;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeTryGetAnimatorLayerWeight(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                         const Coral::String layer, float* value) noexcept
        {
            try
            {
                if (!value)
                    return 0;
                const auto animator = RuntimeAnimator(world, high, low);
                const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
                const auto name = static_cast<std::string>(layer);
                if (!snapshot)
                    return 0;
                const auto found = std::ranges::find_if(snapshot->Layers, [&](const auto& candidate)
                                                        { return candidate.Name == name || candidate.Id == name; });
                if (found == snapshot->Layers.end())
                    return 0;
                *value = found->Weight;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeEntityExists(const std::uint64_t world, const std::uint64_t high,
                                                              const std::uint64_t low) noexcept
        {
            return ResolveRuntimeEntity(world, high, low) ? 1 : 0;
        }

        [[nodiscard]] static std::uint8_t RuntimeGetEntityActive(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity && entity.ActiveSelf() ? 1 : 0;
        }

        static void RuntimeSetEntityActive(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t active) noexcept
        {
            try
            {
                if (auto entity = ResolveRuntimeEntity(world, high, low))
                    entity.SetActive(active != 0);
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] static std::uint32_t RuntimeGetEntityLayer(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, high, low);
                return entity ? entity.Layer() : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetEntityLayer(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low,
                                                                const std::uint32_t layer) noexcept
        {
            try
            {
                auto entity = ResolveRuntimeEntity(world, high, low);
                if (!entity)
                    return 0;
                entity.SetLayer(layer);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeGetEntityActiveInHierarchy(const std::uint64_t world,
                                                                            const std::uint64_t high,
                                                                            const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity && entity.ActiveInHierarchy() ? 1 : 0;
        }

        [[nodiscard]] static std::int32_t RuntimeGetEntityName(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low, char* destination,
                                                               const std::int32_t capacity) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, high, low);
                if (!entity)
                    return 0;
                const auto name = entity.Name();
                if (destination && capacity > 0)
                    std::copy_n(name.begin(), std::min(name.size(), static_cast<std::size_t>(capacity)), destination);
                return static_cast<std::int32_t>(
                    std::min<std::size_t>(name.size(), std::numeric_limits<std::int32_t>::max()));
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetEntityName(const std::uint64_t world, const std::uint64_t high,
                                                               const std::uint64_t low,
                                                               const Coral::String name) noexcept
        {
            try
            {
                auto entity = ResolveRuntimeEntity(world, high, low);
                if (!entity)
                    return 0;
                entity.SetName(static_cast<std::string>(name));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeGetEntityParent(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low, std::uint64_t* parentHigh,
                                                                 std::uint64_t* parentLow) noexcept
        {
            if (!parentHigh || !parentLow)
                return 0;
            *parentHigh = 0;
            *parentLow = 0;
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto parent = entity ? entity.Parent() : Entity{};
            if (!parent)
                return 0;
            *parentHigh = parent.Id().Value().High();
            *parentLow = parent.Id().Value().Low();
            return 1;
        }

        [[nodiscard]] static std::uint8_t RuntimeSetEntityParent(const std::uint64_t world, const std::uint64_t high,
                                                                 const std::uint64_t low,
                                                                 const std::uint64_t parentHigh,
                                                                 const std::uint64_t parentLow,
                                                                 const std::uint8_t preserveWorld) noexcept
        {
            try
            {
                auto entity = ResolveRuntimeEntity(world, high, low);
                const auto parent =
                    parentHigh || parentLow ? ResolveRuntimeEntity(world, parentHigh, parentLow) : Entity{};
                if (!entity || ((parentHigh || parentLow) && !parent))
                    return 0;
                entity.SetParent(parent, preserveWorld != 0);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::int32_t RuntimeGetEntityChildCount(const std::uint64_t world,
                                                                     const std::uint64_t high,
                                                                     const std::uint64_t low) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, high, low);
                return entity ? static_cast<std::int32_t>(std::min<std::size_t>(
                                    entity.Children().size(), std::numeric_limits<std::int32_t>::max()))
                              : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeGetEntityChild(const std::uint64_t world, const std::uint64_t high,
                                                                const std::uint64_t low, const std::int32_t index,
                                                                std::uint64_t* childHigh,
                                                                std::uint64_t* childLow) noexcept
        {
            if (!childHigh || !childLow || index < 0)
                return 0;
            *childHigh = 0;
            *childLow = 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, high, low);
                const auto children = entity ? entity.Children() : std::vector<Entity>{};
                if (static_cast<std::size_t>(index) >= children.size())
                    return 0;
                *childHigh = children[static_cast<std::size_t>(index)].Id().Value().High();
                *childLow = children[static_cast<std::size_t>(index)].Id().Value().Low();
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static ComponentTypeId RuntimeComponentType(const std::uint64_t high,
                                                                  const std::uint64_t low) noexcept
        {
            return ComponentTypeId(AssetId(high, low));
        }

        [[nodiscard]] static std::uint8_t
        RuntimeComponentExists(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t typeHigh, const std::uint64_t typeLow) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && entity.HasComponent(RuntimeComponentType(typeHigh, typeLow)) ? 1 : 0;
        }

        [[nodiscard]] static std::uint8_t RuntimeAddComponent(const std::uint64_t world, const std::uint64_t entityHigh,
                                                              const std::uint64_t entityLow,
                                                              const std::uint64_t typeHigh,
                                                              const std::uint64_t typeLow) noexcept
        {
            try
            {
                auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto type = RuntimeComponentType(typeHigh, typeLow);
                if (!entity || !type)
                    return 0;
                return (entity.GetComponent(type) || entity.AddComponent(type)) ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeRemoveComponent(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t typeHigh, const std::uint64_t typeLow) noexcept
        {
            try
            {
                auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && entity.RemoveComponent(RuntimeComponentType(typeHigh, typeLow)) ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeGetComponentEnabled(const std::uint64_t world,
                                                                     const std::uint64_t entityHigh,
                                                                     const std::uint64_t entityLow,
                                                                     const std::uint64_t typeHigh,
                                                                     const std::uint64_t typeLow) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto component =
                entity ? entity.GetComponent(RuntimeComponentType(typeHigh, typeLow)) : Ref<Component>{};
            return component && component->Enabled() ? 1 : 0;
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetComponentEnabled(const std::uint64_t world, const std::uint64_t entityHigh,
                                   const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                   const std::uint64_t typeLow, const std::uint8_t enabled) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto component =
                    entity ? entity.GetComponent(RuntimeComponentType(typeHigh, typeLow)) : Ref<Component>{};
                if (!component)
                    return 0;
                component->SetEnabled(enabled != 0);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static Vector3 RuntimeGetLocalScale(const std::uint64_t world, const std::uint64_t high,
                                                          const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            return transform ? transform->LocalScale() : Vector3{1.0F, 1.0F, 1.0F};
        }

        static void RuntimeSetLocalScale(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                         const Vector3 value) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
            {
                try
                {
                    transform->SetLocalScale(value);
                }
                catch (...)
                {
                }
            }
        }

        [[nodiscard]] static Vector3 RuntimeGetWorldPosition(const std::uint64_t world, const std::uint64_t high,
                                                             const std::uint64_t low) noexcept
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            return transform ? transform->WorldPosition() : Vector3{};
        }

        static void RuntimeCloneEntity(const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
                                       std::uint64_t* resultHigh, std::uint64_t* resultLow) noexcept
        {
            if (!resultHigh || !resultLow)
                return;
            *resultHigh = 0;
            *resultLow = 0;
            try
            {
                if (auto entity = ResolveRuntimeEntity(world, high, low))
                {
                    const auto clone = entity.Clone();
                    *resultHigh = clone.Id().Value().High();
                    *resultLow = clone.Id().Value().Low();
                }
            }
            catch (...)
            {
            }
        }

        static void RuntimeDestroyEntity(const std::uint64_t world, const std::uint64_t high,
                                         const std::uint64_t low) noexcept
        {
            try
            {
                if (auto entity = ResolveRuntimeEntity(world, high, low))
                    (void)entity.Destroy();
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePlayAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow, const std::uint64_t clipHigh,
                                                           const std::uint64_t clipLow, const float gain) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(
                                     {.Entity = entity.Id().Value(), .Clip = AssetId(clipHigh, clipLow), .Gain = gain})
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimePlayAudioAdvanced(const std::uint64_t world, const std::uint64_t entityHigh,
                                 const std::uint64_t entityLow, const std::uint64_t clipHigh,
                                 const std::uint64_t clipLow, const std::uint64_t mixerHigh,
                                 const std::uint64_t mixerLow, const std::uint64_t busHigh, const std::uint64_t busLow,
                                 const Coral::String bus, const float gain, const float pitch,
                                 const std::uint32_t priority, const std::uint8_t loop, const std::uint8_t spatial,
                                 const float minimumDistance, const float maximumDistance) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(
                                     {.Entity = entity.Id().Value(),
                                      .Clip = AssetId(clipHigh, clipLow),
                                      .Bus = static_cast<std::string>(bus),
                                      .Gain = gain,
                                      .Pitch = pitch,
                                      .Priority = priority,
                                      .Loop = loop != 0,
                                      .Spatial = spatial != 0,
                                      .MinimumDistance = minimumDistance,
                                      .MaximumDistance = maximumDistance,
                                      .Mixer = AssetId(mixerHigh, mixerLow),
                                      .BusId = AssetId(busHigh, busLow)})
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeStopAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->StopManagedAudio(entity.Id().Value())
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePlayAudioSource(const std::uint64_t world,
                                                                 const std::uint64_t entityHigh,
                                                                 const std::uint64_t entityLow) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!source || !source->Clip())
                    return 0;
                ManagedAudioPlayback playback;
                playback.Entity = entity.Id().Value();
                playback.Clip = source->Clip();
                playback.Bus = source->Bus();
                playback.Gain = source->Gain();
                playback.Pitch = source->Pitch();
                playback.Priority = source->Priority();
                playback.Loop = source->Loop();
                playback.Spatial = source->Spatial();
                playback.MinimumDistance = source->MinimumDistance();
                playback.MaximumDistance = source->MaximumDistance();
                playback.Mixer = source->Mixer();
                playback.BusId = source->BusId();
                playback.Attenuation = source->Attenuation();
                return CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(playback) ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePauseAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                            const std::uint64_t entityLow,
                                                            const std::uint8_t paused) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->PauseManagedAudio(entity.Id().Value(),
                                                                                                  paused != 0)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSeekAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow,
                                                           const float positionSeconds) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->SeekManagedAudio(entity.Id().Value(),
                                                                                                 positionSeconds)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeGetAudioSourceProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                        const std::uint64_t entityLow, NativeAudioSourceProperties* properties) noexcept
        {
            if (!properties)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!source)
                    return 0;
                const auto clip = source->Clip();
                properties->ClipHigh = clip.High();
                properties->ClipLow = clip.Low();
                properties->Gain = source->Gain();
                properties->Pitch = source->Pitch();
                properties->Priority = source->Priority();
                properties->Loop = source->Loop() ? 1 : 0;
                properties->Spatial = source->Spatial() ? 1 : 0;
                if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
                {
                    const auto status =
                        CurrentRuntime->Specification.RuntimeServices->ManagedAudioStatus(entity.Id().Value());
                    properties->PositionSeconds = status.PositionSeconds;
                    properties->DurationSeconds = status.DurationSeconds;
                    properties->PlaybackState = static_cast<std::uint8_t>(status.State);
                }
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceClip(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const std::uint64_t clipHigh,
                                                                    const std::uint64_t clipLow) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!source)
                    return 0;
                source->SetClip(AssetId(clipHigh, clipLow));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceScalar(const std::uint64_t world,
                                                                      const std::uint64_t entityHigh,
                                                                      const std::uint64_t entityLow,
                                                                      const std::uint8_t property,
                                                                      const float value) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!source)
                    return 0;
                if (property == 0)
                    source->SetGain(value);
                else if (property == 1)
                    source->SetPitch(value);
                else
                    return 0;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSetAudioSourceFlag(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const std::uint8_t property,
                                                                    const std::uint8_t value) noexcept
        {
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!source)
                    return 0;
                if (property == 0)
                    source->SetLoop(value != 0);
                else if (property == 1)
                    source->SetSpatial(value != 0);
                else
                    return 0;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePlayVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow, const std::uint64_t effectHigh,
                                                         const std::uint64_t effectLow,
                                                         const std::uint8_t restart) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedVfx(
                                     entity.Id().Value(), AssetId(effectHigh, effectLow), restart != 0)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeStopVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                         const std::uint64_t entityLow) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->StopManagedVfx(entity.Id().Value()) ? 1
                                                                                                                    : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimePauseVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                                          const std::uint64_t entityLow,
                                                          const std::uint8_t paused) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->PauseManagedVfx(entity.Id().Value(),
                                                                                                paused != 0)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeIsVfxAlive(const std::uint64_t world, const std::uint64_t entityHigh,
                                                            const std::uint64_t entityLow) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->IsManagedVfxAlive(entity.Id().Value())
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeSendVfxEvent(const std::uint64_t world, const std::uint64_t entityHigh,
                                                              const std::uint64_t entityLow,
                                                              const Coral::String eventName,
                                                              const std::uint32_t spawnCount) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->SendManagedVfxEvent(
                                     entity.Id().Value(), static_cast<std::string>(eventName), spawnCount)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxParameter(const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
                               const std::uint64_t parameterHigh, const std::uint64_t parameterLow,
                               VfxParameterValue value) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->SetManagedVfxParameter(
                                     entity.Id().Value(), {AssetId(parameterHigh, parameterLow), std::move(value)})
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxScalarRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                 const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                 const std::uint64_t parameterLow, const float minimum, const float maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxScalarRange{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxIntegerRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const std::int64_t minimum,
                                  const std::int64_t maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxIntegerRange{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxUnsignedIntegerRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                          const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                          const std::uint64_t parameterLow, const std::uint64_t minimum,
                                          const std::uint64_t maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxUnsignedIntegerRange{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector2Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector2 minimum,
                                  const Vector2 maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxVector2Range{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector3Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector3 minimum,
                                  const Vector3 maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxVector3Range{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxVector4Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                  const std::uint64_t parameterLow, const Vector4 minimum,
                                  const Vector4 maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxVector4Range{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t
        RuntimeSetVfxColorRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                const std::uint64_t parameterLow, const Color minimum, const Color maximum) noexcept
        {
            return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                          VfxColorRange{minimum, maximum});
        }

        [[nodiscard]] static std::uint8_t RuntimeSetUiText(const std::uint64_t world, const std::uint64_t entityHigh,
                                                           const std::uint64_t entityLow,
                                                           const Coral::String text) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity && CurrentRuntime->Specification.RuntimeServices->SetManagedUiText(
                                     entity.Id().Value(), static_cast<std::string>(text))
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] static std::uint8_t RuntimeConsumeUiClick(const std::uint64_t world,
                                                                const std::uint64_t entityHigh,
                                                                const std::uint64_t entityLow) noexcept
        {
            if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
                return entity &&
                               CurrentRuntime->Specification.RuntimeServices->ConsumeManagedUiClick(entity.Id().Value())
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        struct RuntimeRaycastResult
        {
            std::uint64_t EntityHigh = 0;
            std::uint64_t EntityLow = 0;
            Vector3 Point;
            Vector3 Normal;
            float Distance = 0.0F;
        };

        [[nodiscard]] static std::uint8_t RuntimeRaycast(const std::uint64_t world, const Vector3 origin,
                                                         const Vector3 direction, const float maximumDistance,
                                                         const std::uint32_t mask, const std::uint64_t ignoredHigh,
                                                         const std::uint64_t ignoredLow,
                                                         RuntimeRaycastResult* result) noexcept
        {
            if (!result || !CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
                return 0;
            try
            {
                const auto hit = CurrentRuntime->Specification.RuntimeServices->RaycastManaged(
                    {.World = world,
                     .Origin = origin,
                     .Direction = direction,
                     .MaximumDistance = maximumDistance,
                     .Mask = mask,
                     .IgnoredEntity = AssetId(ignoredHigh, ignoredLow),
                     .IncludeTriggers = false});
                if (!hit)
                    return 0;
                result->EntityHigh = hit->Entity.High();
                result->EntityLow = hit->Entity.Low();
                result->Point = hit->Point;
                result->Normal = hit->Normal;
                result->Distance = hit->Distance;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method)
        {
            ++ManagedInteropCalls;
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            object.InvokeMethod(method);
            ThrowRuntimeException();
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method, const float value)
        {
            ++ManagedInteropCalls;
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            object.InvokeMethod(method, value);
            ThrowRuntimeException();
        }

        [[nodiscard]] std::uint32_t ReadCallbackMask(Coral::ManagedObject& object)
        {
            const RuntimeScope scope(*this);
            ClearRuntimeException();
            const auto result = object.InvokeMethod<std::uint32_t>("RuntimeGetCallbackMask");
            ThrowRuntimeException();
            return result;
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
            auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
            if ((callback == ManagedBehaviourCallback::FixedUpdate &&
                 (instance.CallbackMask & FixedUpdateCallback) == 0) ||
                (callback == ManagedBehaviourCallback::LateUpdate &&
                 (instance.CallbackMask & LateUpdateCallback) == 0) ||
                (callback == ManagedBehaviourCallback::AnimatorIk && (instance.CallbackMask & AnimatorIkCallback) == 0))
            {
                ++SkippedCallbacks;
                ++callbackProfile.SkippedInvocations;
                return;
            }
            const auto callbackStarted = std::chrono::steady_clock::now();
            ++CallbackInvocations;
            ++callbackProfile.Invocations;
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
                case ManagedBehaviourCallback::AnimationEvent:
                    throw std::logic_error("Animation events require an event payload.");
                case ManagedBehaviourCallback::PhysicsContact:
                    throw std::logic_error("Physics contacts require a contact payload.");
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
                case ManagedBehaviourCallback::AnimatorIk:
                    Invoke(instance.Object, "RuntimeAnimatorIk", deltaSeconds);
                    break;
                }
            }
            catch (const std::exception& error)
            {
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted)
                        .count();
                CallbackMilliseconds += elapsed;
                MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
                callbackProfile.Milliseconds += elapsed;
                callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
                RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error,
                                              callback, Reload.Generation, instance.TypeName, instance.Entity,
                                              error.what()});
                if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                    throw;
                instance.Faulted = true;
                instance.Enabled = false;
                return;
            }
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
        }

        void InvokeAnimationEvent(const std::uint64_t id, const AnimationEventMessage& event)
        {
            const auto found = Instances.find(id);
            if (found == Instances.end() || !found->second.Object.IsValid() || found->second.Faulted)
                return;
            auto& instance = found->second;
            constexpr auto callback = ManagedBehaviourCallback::AnimationEvent;
            auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
            const auto callbackStarted = std::chrono::steady_clock::now();
            ++CallbackInvocations;
            ++callbackProfile.Invocations;
            try
            {
                ++ManagedInteropCalls;
                const RuntimeScope scope(*this);
                ClearRuntimeException();
                instance.Object.InvokeMethod("RuntimeAnimationEvent", event.Name, event.NormalizedTime, event.Integer,
                                             event.Scalar, event.Text);
                ThrowRuntimeException();
            }
            catch (const std::exception& error)
            {
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted)
                        .count();
                CallbackMilliseconds += elapsed;
                MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
                callbackProfile.Milliseconds += elapsed;
                callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
                RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error,
                                              callback, Reload.Generation, instance.TypeName, instance.Entity,
                                              error.what()});
                if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                    throw;
                instance.Faulted = true;
                instance.Enabled = false;
                return;
            }
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
        }

        void InvokePhysicsContact(const std::uint64_t id, const PhysicsContactPhase phase,
                                  const PhysicsContactMessage& contact)
        {
            const auto found = Instances.find(id);
            if (found == Instances.end() || !found->second.Object.IsValid() || found->second.Faulted)
                return;
            auto& instance = found->second;
            constexpr auto callback = ManagedBehaviourCallback::PhysicsContact;
            auto& callbackProfile = instance.CallbackProfiles[static_cast<std::size_t>(callback)];
            const auto callbackStarted = std::chrono::steady_clock::now();
            ++CallbackInvocations;
            ++callbackProfile.Invocations;
            try
            {
                ++ManagedInteropCalls;
                const RuntimeScope scope(*this);
                ClearRuntimeException();
                instance.Object.InvokeMethod("RuntimePhysicsContact", static_cast<std::uint8_t>(phase),
                                             contact.Trigger ? std::uint8_t{1} : std::uint8_t{0},
                                             contact.Other.Value().High(), contact.Other.Value().Low(), contact.Point,
                                             contact.Normal, contact.Impulse);
                ThrowRuntimeException();
            }
            catch (const std::exception& error)
            {
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted)
                        .count();
                CallbackMilliseconds += elapsed;
                MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
                callbackProfile.Milliseconds += elapsed;
                callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
                RuntimeDiagnostics.push_back({ManagedBehaviourInstanceId(id), ManagedDiagnosticSeverity::Error,
                                              callback, Reload.Generation, instance.TypeName, instance.Entity,
                                              error.what()});
                if (Specification.ExceptionPolicy == ManagedExceptionPolicy::Propagate)
                    throw;
                instance.Faulted = true;
                instance.Enabled = false;
                return;
            }
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callbackStarted).count();
            CallbackMilliseconds += elapsed;
            MaximumCallbackMilliseconds = std::max(MaximumCallbackMilliseconds, elapsed);
            callbackProfile.Milliseconds += elapsed;
            callbackProfile.MaximumMilliseconds = std::max(callbackProfile.MaximumMilliseconds, elapsed);
        }

        void SetState(const ManagedBuildState state)
        {
            {
                std::scoped_lock lock(Mutex);
                Status.State = state;
            }
            StatusChanged.notify_all();
        }

        void RunBuild(const std::stop_token& cancellation, const ManagedBuildRequest& request,
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
                          "    <UseSharedCompilation>false</UseSharedCompilation>\n"
                          "  </PropertyGroup>\n</Project>\n");

                const auto managedApiOutput = staging / "ManagedApi";
                std::filesystem::create_directories(managedApiOutput);
                auto generationManagedApi = managedApiOutput / "Keire.Managed.dll";
                const auto managedApiProject = FindManagedApiProject();
                if (!managedApiProject.empty())
                {
                    const auto fingerprint = ManagedApiSourceFingerprint(managedApiProject);
                    const auto cacheDirectory = OutputRoot / "ManagedApiCache";
                    const auto cachedAssembly = cacheDirectory / "Keire.Managed.dll";
                    const auto cachedFingerprint = cacheDirectory / "source.fingerprint";
                    bool cacheHit = false;
                    if (std::filesystem::is_regular_file(cachedAssembly) &&
                        std::filesystem::is_regular_file(cachedFingerprint))
                    {
                        cacheHit = Detail::ReadTextFile(cachedFingerprint, std::size_t{1} << 20U) == fingerprint;
                    }
                    if (cacheHit)
                    {
                        std::filesystem::copy_file(cachedAssembly, generationManagedApi,
                                                   std::filesystem::copy_options::overwrite_existing);
                    }
                    else
                    {
                        const auto managedApiIntermediate = OutputRoot / "Intermediate" / "ManagedApi";
                        const std::vector<std::string> managedApiArguments{
                            "build",
                            PathText(managedApiProject),
                            "--configuration",
                            "Release",
                            "--nologo",
                            "--disable-build-servers",
                            "/nodeReuse:false",
                            "--output",
                            PathText(managedApiOutput),
                            "--property:UseSharedCompilation=false",
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
                        if (!std::filesystem::is_regular_file(generationManagedApi))
                            throw std::runtime_error("Keire.Managed compilation published no API assembly.");
                        std::filesystem::create_directories(cacheDirectory);
                        const auto assemblyBytes = Detail::ReadTextFile(generationManagedApi, std::size_t{64} << 20U);
                        Detail::WriteFileAtomically(cachedAssembly, std::as_bytes(std::span(assemblyBytes)));
                        Detail::WriteTextFileAtomically(cachedFingerprint, fingerprint);
                    }
                }
                else
                {
                    if (ManagedApi.empty() || !std::filesystem::is_regular_file(ManagedApi))
                        throw std::runtime_error("The managed build has no valid Keire.Managed API assembly.");
                    std::filesystem::copy_file(ManagedApi, generationManagedApi,
                                               std::filesystem::copy_options::overwrite_existing);
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
                const std::vector<std::string> arguments{"build",
                                                         PathText(aggregatorPath),
                                                         "--configuration",
                                                         request.Configuration,
                                                         "--nologo",
                                                         "--disable-build-servers",
                                                         "/nodeReuse:false",
                                                         "--output",
                                                         PathText(staging / "Assemblies"),
                                                         "--property:UseSharedCompilation=false"};
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
                const auto& active = staging;
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
        std::vector<ManagedAssetTypeDescriptor> ActiveManagedAssetTypes;
        std::vector<ManagedAssetTypeDescriptor> CandidateManagedAssetTypes;
        std::vector<ManagedAssetTypeDiagnostic> ActiveManagedAssetDiagnostics;
        std::vector<ManagedAssetTypeDiagnostic> CandidateManagedAssetDiagnostics;
        std::map<ManagedTypeId, const Coral::Type*> ActiveManagedAssetRuntimeTypes;
        std::map<ManagedTypeId, const Coral::Type*> CandidateManagedAssetRuntimeTypes;
        const Coral::Type* ActiveNativeRuntimeType = nullptr;
        const Coral::Type* CandidateNativeRuntimeType = nullptr;
        Ref<AssetSystem> Assets;
        std::mutex ManagedAssetMutex;
        std::map<AssetId, PendingManagedAssetLoad> PendingManagedAssetLoads;
        std::map<AssetId, ManagedAssetSource> ManagedAssetSources;
        std::unordered_map<std::uint64_t, BehaviourInstance> Instances;
        std::vector<ComponentTypeId> InstalledComponentTypes;
        std::vector<ManagedRuntimeDiagnostic> RuntimeDiagnostics;
        std::uint64_t CallbackInvocations = 0;
        std::uint64_t SkippedCallbacks = 0;
        std::uint64_t ManagedInteropCalls = 0;
        double CallbackMilliseconds = 0.0;
        double MaximumCallbackMilliseconds = 0.0;
        std::unordered_map<std::uint64_t, std::string> ProfileNames;
        std::uint64_t NextInstance = 1;
        std::shared_ptr<Impl*> Lifetime;
        ManagedReloadStatus Reload;
        std::string RuntimeException;
        bool RuntimeInitialized = false;
        std::uint64_t NextReload = 1;
        Ref<JobSystem> Scheduler;
        Ref<JobScope> WorkScope;
        Ref<JobScope> ManagedJobs;
        std::mutex ManagedJobMutex;
        std::unordered_map<std::uint64_t, ManagedJobRecord> ManagedJobRecords;
        std::uint64_t NextManagedJob = 1;
        JobHandle Worker;
        std::stop_source BuildCancellation;
        bool OwnScheduler = false;
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
                    auto object = implementation.CreateObject(*type, owner.World(), owner.Id().Value());
                    const auto [instance, inserted] = implementation.Instances.emplace(
                        id, BehaviourInstance{m_ManagedType, Type(), owner.World(), owner.Id().Value(),
                                              std::move(object), m_State});
                    (void)inserted;
                    instance->second.NativeEntity = owner;
                    instance->second.CallbackMask = implementation.ReadCallbackMask(instance->second.Object);
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
        void Update(const float deltaSeconds) override { Invoke(ManagedBehaviourCallback::Update, deltaSeconds); }
        void LateUpdate() override { Invoke(ManagedBehaviourCallback::LateUpdate); }
        void OnAnimationEvent(const AnimationEventMessage& event) override
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (m_Instance)
                        implementation.InvokeAnimationEvent(m_Instance.Value(), event);
                });
        }
        void OnAnimatorIk(const AnimationIkMessage& context) override
        {
            Invoke(ManagedBehaviourCallback::AnimatorIk, context.LayerWeight);
        }
        void OnCollisionEnter(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Enter, contact);
        }
        void OnCollisionStay(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Stay, contact);
        }
        void OnCollisionExit(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Exit, contact);
        }
        void OnTriggerEnter(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Enter, contact);
        }
        void OnTriggerStay(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Stay, contact);
        }
        void OnTriggerExit(const PhysicsContactMessage& contact) override
        {
            InvokeContact(PhysicsContactPhase::Exit, contact);
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

        void InvokeContact(const PhysicsContactPhase phase, const PhysicsContactMessage& contact)
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (m_Instance)
                        implementation.InvokePhysicsContact(m_Instance.Value(), phase, contact);
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
        std::string m_State = "{\"Version\":1,\"Fields\":[]}";
    };

    ScriptSystem::ScriptSystem(ScriptSystemSpecification specification, Ref<JobSystem> jobs)
        : m_Impl(std::make_unique<Impl>(specification, std::move(jobs)))
    {
        if (specification.Mode == ScriptMode::Disabled || specification.ProjectRoot.empty() ||
            specification.AssemblyDirectory.empty() || specification.AssemblyDirectory.is_absolute() ||
            specification.MaximumDiagnostics == 0 || specification.MaximumDiagnostics > 65536)
            throw std::invalid_argument("ScriptSystem specification is invalid.");
        m_Impl->ProjectRoot = std::filesystem::absolute(specification.ProjectRoot).lexically_normal();
        if (!std::filesystem::is_directory(m_Impl->ProjectRoot))
            throw std::invalid_argument("ScriptSystem project root does not exist.");
        const auto sdk = Detail::ReadManagedSdkConfiguration(
            m_Impl->ProjectRoot, {specification.SdkSelection, specification.DotnetExecutable});
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
        m_Impl->Dotnet =
            m_Impl->Specification.DotnetExecutable.empty()
                ? std::filesystem::path{}
                : Detail::ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
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
            const auto contents = Detail::ReadTextFile(ideManagedApi, std::size_t{64} << 20U);
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
        m_Impl->StopWorker();
        if (m_Impl->Dotnet.empty())
            m_Impl->Dotnet =
                Detail::ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
                                      m_Impl->Specification.ProjectRoot, m_Impl->Specification.RuntimeRootDirectory);

        const ManagedBuildOperationId operation(m_Impl->NextOperation++);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Status = {.Operation = operation, .State = ManagedBuildState::Generating};
            for (const auto& assembly : request.Assemblies)
                m_Impl->Status.ChangedAssemblies.push_back(assembly.Definition.Name);
        }
        m_Impl->BuildCancellation = std::stop_source{};
        const auto buildCancellation = m_Impl->BuildCancellation.get_token();
        m_Impl->Worker = m_Impl->WorkScope->Submit(
            {.Name = "Managed assembly build",
             .Priority = JobPriority::High,
             .Class = JobClass::Blocking,
             .Domain = JobDomain::Tooling},
            [implementation = m_Impl.get(), request = std::move(request), operation,
             buildCancellation](JobContext& context)
            {
                std::stop_source combined;
                std::stop_callback schedulerStop(context.StopToken(), [&combined] { combined.request_stop(); });
                std::stop_callback buildStop(buildCancellation, [&combined] { combined.request_stop(); });
                implementation->RunBuild(combined.get_token(), request, operation);
            });
        return operation;
    }

    void ScriptSystem::CancelBuild(const ManagedBuildOperationId operation)
    {
        m_Impl->RequireOwner();
        if (!operation || BuildStatus().Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        m_Impl->BuildCancellation.request_stop();
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
        Detail::WriteManagedSdkConfiguration(
            m_Impl->ProjectRoot, {m_Impl->Specification.SdkSelection, m_Impl->Specification.DotnetExecutable});
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

        m_Impl->ResetManagedAssetGeneration(
            m_Impl->CandidateNativeRuntimeType,
            m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max() ? 0 : m_Impl->Reload.Generation + 1);
        m_Impl->CandidateManagedAssetRuntimeTypes.clear();
        m_Impl->CandidateNativeRuntimeType = nullptr;
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
            Coral::Type* requireComponentType = nullptr;
            Coral::Type* serializeFieldType = nullptr;
            Coral::Type* hideInInspectorType = nullptr;
            Coral::Type* serializableType = nullptr;
            Coral::Type* rangeType = nullptr;
            Coral::Type* tooltipType = nullptr;
            Coral::Type* groupType = nullptr;
            Coral::Type* managedAssetMetadataType = nullptr;
            Coral::Type* nativeRuntimeType = nullptr;
            std::map<std::string, const Coral::Type*, std::less<>> managedRuntimeTypesByName;
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
                managedApi.AddInternalCall("Keire.NativeRuntime", "RegisterProfileNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRegisterProfileName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RecordProfileSpanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRecordProfileSpan));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetProfileCounterIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetProfileCounter));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RequestManagedAssetLoadIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRequestManagedAssetLoad));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CancelManagedAssetLoadIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCancelManagedAssetLoad));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ReleaseManagedAssetIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeReleaseManagedAsset));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SubmitManagedJobIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSubmitManagedJob));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CancelManagedJobIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCancelManagedJob));
                managedApi.AddInternalCall("Keire.NativeRuntime", "DeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "FixedDeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeFixedDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "UnscaledDeltaTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeUnscaledDeltaTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ElapsedTimeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeElapsedTime));
                managedApi.AddInternalCall("Keire.NativeRuntime", "InputAxis2DIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeInputAxis2D));
                managedApi.AddInternalCall("Keire.NativeRuntime", "InputStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeInputState));
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
                managedApi.AddInternalCall("Keire.NativeRuntime", "MoveCharacterControllerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeMoveCharacterController));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetCharacterControllerStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetCharacterControllerState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorFloatIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorFloat));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorIntegerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorInteger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorBooleanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorBoolean));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorTriggerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorTrigger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorLayerWeightIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorLayerWeight));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CrossFadeAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCrossFadeAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopAnimatorIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopAnimator));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorSpeedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorSpeed));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAnimatorStateIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAnimatorState));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAnimatorStateNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAnimatorStateName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorTwoBoneIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorTwoBoneIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAnimatorFabrikIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAnimatorFabrikIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ClearAnimatorIkIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeClearAnimatorIk));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorFloatIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorFloat));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorIntegerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorInteger));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorBooleanIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorBoolean));
                managedApi.AddInternalCall("Keire.NativeRuntime", "TryGetAnimatorLayerWeightIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeTryGetAnimatorLayerWeight));
                managedApi.AddInternalCall("Keire.NativeRuntime", "EntityExistsIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeEntityExists));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityActiveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityActive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityActiveInHierarchyIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityActiveInHierarchy));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityActiveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityActive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityLayerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityLayer));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityLayerIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityLayer));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityNameIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityName));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityParentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityParent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetEntityParentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetEntityParent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityChildCountIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityChildCount));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetEntityChildIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetEntityChild));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ComponentExistsIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeComponentExists));
                managedApi.AddInternalCall("Keire.NativeRuntime", "AddComponentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeAddComponent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RemoveComponentIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRemoveComponent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetComponentEnabledIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetComponentEnabled));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetComponentEnabledIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetComponentEnabled));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetLocalScaleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetLocalScale));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetLocalScaleIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetLocalScale));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetWorldPositionIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetWorldPosition));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetWorldRotationIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetWorldRotation));
                managedApi.AddInternalCall("Keire.NativeRuntime", "CloneEntityIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeCloneEntity));
                managedApi.AddInternalCall("Keire.NativeRuntime", "DestroyEntityIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeDestroyEntity));
                managedApi.AddInternalCall("Keire.NativeRuntime", "RaycastIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeRaycast));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioAdvancedIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudioAdvanced));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayAudioSourceIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayAudioSource));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SeekAudioIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSeekAudio));
                managedApi.AddInternalCall("Keire.NativeRuntime", "GetAudioSourcePropertiesIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeGetAudioSourceProperties));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceClipIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceClip));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceScalarIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceScalar));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetAudioSourceFlagIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetAudioSourceFlag));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PlayVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePlayVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "StopVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeStopVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "PauseVfxIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimePauseVfx));
                managedApi.AddInternalCall("Keire.NativeRuntime", "IsVfxAliveIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeIsVfxAlive));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SendVfxEventIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSendVfxEvent));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxScalarRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxScalarRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxIntegerRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxIntegerRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxUnsignedIntegerRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxUnsignedIntegerRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector2RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector2Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector3RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector3Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxVector4RangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxVector4Range));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetVfxColorRangeIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetVfxColorRange));
                managedApi.AddInternalCall("Keire.NativeRuntime", "SetUiTextIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeSetUiText));
                managedApi.AddInternalCall("Keire.NativeRuntime", "ConsumeUiClickIcall",
                                           reinterpret_cast<void*>(&Impl::RuntimeConsumeUiClick));
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
                requireComponentType = &managedApi.GetLocalType("Keire.RequireComponentAttribute");
                serializeFieldType = &managedApi.GetLocalType("Keire.SerializeFieldAttribute");
                hideInInspectorType = &managedApi.GetLocalType("Keire.HideInInspectorAttribute");
                serializableType = &managedApi.GetLocalType("Keire.SerializableTypeAttribute");
                rangeType = &managedApi.GetLocalType("Keire.RangeAttribute");
                tooltipType = &managedApi.GetLocalType("Keire.TooltipAttribute");
                groupType = &managedApi.GetLocalType("Keire.InspectorGroupAttribute");
                managedAssetMetadataType = &managedApi.GetLocalType("Keire.ManagedAssetMetadata");
                nativeRuntimeType = &managedApi.GetLocalType("Keire.NativeRuntime");
                if (!*stableComponentIdType || !*executionOrderType || !*requireComponentType || !*serializeFieldType)
                    throw std::runtime_error("Keire.Managed does not expose managed component metadata.");
                if (!*managedAssetMetadataType)
                    throw std::runtime_error("Keire.Managed does not expose managed asset metadata.");
                if (!*nativeRuntimeType)
                    throw std::runtime_error("Keire.Managed does not expose its managed asset runtime.");
                for (const auto& type : managedApi.GetLocalTypes())
                    if (type)
                        managedRuntimeTypesByName.emplace(ManagedTypeName(const_cast<Coral::Type&>(type)),
                                                          std::addressof(type));
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
                for (const auto& type : assembly.GetLocalTypes())
                    if (type)
                        managedRuntimeTypesByName.emplace(ManagedTypeName(const_cast<Coral::Type&>(type)),
                                                          std::addressof(type));
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
                        std::vector<ComponentTypeId> requiredComponents;
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
                            else if (attribute.GetType() == *requireComponentType)
                            {
                                requiredComponents.emplace_back(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                                                        attribute.GetFieldValue<std::uint64_t>("Low")));
                            }
                        }
                        if (componentType)
                        {
                            std::ranges::sort(requiredComponents);
                            if (std::ranges::adjacent_find(requiredComponents) != requiredComponents.end())
                                throw std::runtime_error("Managed Behaviour declares a duplicate required component.");
                            if (std::ranges::find(requiredComponents, componentType) != requiredComponents.end())
                                throw std::runtime_error("Managed Behaviour cannot require itself.");
                            Impl::BehaviourType behaviour;
                            behaviour.Name = typeName;
                            behaviour.ComponentType = componentType;
                            behaviour.ExecutionOrder = executionOrder;
                            behaviour.Type = std::addressof(type);
                            behaviour.Properties = ReflectManagedProperties(
                                type, *behaviourType, *serializeFieldType,
                                *hideInInspectorType ? hideInInspectorType : nullptr,
                                *serializableType ? serializableType : nullptr, *rangeType ? rangeType : nullptr,
                                *tooltipType ? tooltipType : nullptr, *groupType ? groupType : nullptr);
                            behaviour.Methods = ReflectManagedMethods(type);
                            behaviour.RequiredComponents = std::move(requiredComponents);
                            candidateTypes.push_back(std::move(behaviour));
                        }
                    }
                }
            }
            if (!managedAssetMetadataType)
                throw std::runtime_error("Managed asset discovery requires Keire.Managed metadata.");
            const Coral::ScopedString managedAssetMetadata(
                managedAssetMetadataType->InvokeStaticMethod<Coral::String>("Export"));
            auto discoveredManagedAssets = ParseManagedAssetMetadata(static_cast<std::string>(managedAssetMetadata));
            std::map<ManagedTypeId, const Coral::Type*> discoveredManagedRuntimeTypes;
            for (const auto& descriptor : discoveredManagedAssets.Types)
            {
                const auto found = managedRuntimeTypesByName.find(descriptor.FullName);
                if (found == managedRuntimeTypesByName.end())
                    throw std::runtime_error("Managed asset metadata references an unavailable runtime type.");
                discoveredManagedRuntimeTypes.emplace(descriptor.StableTypeId, found->second);
            }
            std::uint64_t candidateGeneration = 0;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Managed reload generation is exhausted.");
                candidateGeneration = m_Impl->Reload.Generation + 1;
            }
            m_Impl->CandidateNativeRuntimeType = nativeRuntimeType;
            m_Impl->InstallManagedAssetGeneration(*nativeRuntimeType, candidateGeneration);
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
                    m_Impl->CandidateManagedAssetTypes = std::move(discoveredManagedAssets.Types);
                    m_Impl->CandidateManagedAssetDiagnostics = std::move(discoveredManagedAssets.Diagnostics);
                    m_Impl->CandidateManagedAssetRuntimeTypes = std::move(discoveredManagedRuntimeTypes);
                    m_Impl->CandidateNativeRuntimeType = nativeRuntimeType;
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
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType,
                                                m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max()
                                                    ? 0
                                                    : m_Impl->Reload.Generation + 1);
            m_Impl->CandidateTypes.clear();
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
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

        std::uint64_t candidateGeneration = 0;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            candidateGeneration = m_Impl->Reload.Generation + 1;
        }
        try
        {
            std::vector<std::pair<AssetId, AssetHandle<ManagedDataAsset>>> sources;
            {
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                sources.reserve(m_Impl->ManagedAssetSources.size());
                for (const auto& [id, source] : m_Impl->ManagedAssetSources)
                    sources.emplace_back(id, source.Handle);
            }
            for (const auto& [id, handle] : sources)
            {
                const auto asset = handle.TryGetLoaded();
                if (!asset)
                    throw std::runtime_error("A loaded managed data source became unavailable during script reload.");
                auto object = m_Impl->HydrateManagedAsset(*asset, m_Impl->CandidateManagedAssetRuntimeTypes);
                const Impl::RuntimeScope scope(*m_Impl);
                if (object.InvokeMethod<Coral::Bool32>("RuntimeRegisterManagedAsset", candidateGeneration, id.High(),
                                                       id.Low()) == 0)
                    throw std::runtime_error("The candidate managed asset registry rejected a hydrated object.");
            }
        }
        catch (...)
        {
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType, candidateGeneration);
            m_Impl->CandidateTypes.clear();
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic =
                "Managed data hydration failed; the last-good script and asset generation remains active.";
            throw;
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
                Impl::BehaviourInstance replacement{instance.TypeName,
                                                    instance.ComponentType,
                                                    instance.World,
                                                    instance.Entity,
                                                    {},
                                                    instance.State,
                                                    {},
                                                    instance.Enabled,
                                                    false};
                replacement.NativeEntity = instance.NativeEntity;
                auto* type = m_Impl->FindType(m_Impl->CandidateTypes, instance.ComponentType);
                if (!type)
                    type = m_Impl->FindType(m_Impl->CandidateTypes, instance.TypeName);
                if (type)
                {
                    replacement.TypeName = type->Name;
                    replacement.ComponentType = type->ComponentType;
                    replacement.Object = m_Impl->CreateObject(*type, instance.World, instance.Entity);
                    replacement.CallbackMask = m_Impl->ReadCallbackMask(replacement.Object);
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
            m_Impl->CandidateManagedAssetTypes.clear();
            m_Impl->CandidateManagedAssetDiagnostics.clear();
            m_Impl->ResetManagedAssetGeneration(m_Impl->CandidateNativeRuntimeType, candidateGeneration);
            m_Impl->CandidateManagedAssetRuntimeTypes.clear();
            m_Impl->CandidateNativeRuntimeType = nullptr;
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = "Managed reload migration failed; the last-good generation remains active.";
            throw;
        }

        // Reverse-P/Invoke callbacks must finish while the old assembly load context is still alive.
        m_Impl->DrainManagedJobs(true);
        auto previous = std::move(m_Impl->ActiveContext);
        const auto* previousNativeRuntime = m_Impl->ActiveNativeRuntimeType;
        const auto previousGeneration = m_Impl->Reload.Generation;
        m_Impl->Instances = std::move(migrated);
        m_Impl->ActiveContext = std::move(m_Impl->CandidateContext);
        m_Impl->ActiveTypes = std::move(m_Impl->CandidateTypes);
        m_Impl->ActiveManagedAssetTypes = std::move(m_Impl->CandidateManagedAssetTypes);
        m_Impl->ActiveManagedAssetDiagnostics = std::move(m_Impl->CandidateManagedAssetDiagnostics);
        m_Impl->ActiveManagedAssetRuntimeTypes = std::move(m_Impl->CandidateManagedAssetRuntimeTypes);
        m_Impl->ActiveNativeRuntimeType = m_Impl->CandidateNativeRuntimeType;
        m_Impl->CandidateNativeRuntimeType = nullptr;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            std::erase_if(m_Impl->PendingManagedAssetLoads, [previousGeneration](const auto& entry)
                          { return entry.second.Generation == previousGeneration; });
        }
        m_Impl->ResetManagedAssetGeneration(previousNativeRuntime, previousGeneration);
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
        m_Impl->ResetManagedAssetGeneration(
            m_Impl->CandidateNativeRuntimeType,
            m_Impl->Reload.Generation == std::numeric_limits<std::uint64_t>::max() ? 0 : m_Impl->Reload.Generation + 1);
        m_Impl->CandidateTypes.clear();
        m_Impl->CandidateManagedAssetTypes.clear();
        m_Impl->CandidateManagedAssetDiagnostics.clear();
        m_Impl->CandidateManagedAssetRuntimeTypes.clear();
        m_Impl->CandidateNativeRuntimeType = nullptr;
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
                              type.ComponentType, type.ExecutionOrder, type.RequiredComponents});
        }
        return result;
    }

    std::vector<ManagedAssetTypeDescriptor> ScriptSystem::ManagedAssetTypes() const
    {
        m_Impl->RequireOwner();
        return m_Impl->ActiveManagedAssetTypes;
    }

    std::vector<ManagedAssetTypeDiagnostic> ScriptSystem::ManagedAssetTypeDiagnostics() const
    {
        m_Impl->RequireOwner();
        return m_Impl->ActiveManagedAssetDiagnostics;
    }

    void ScriptSystem::SetAssetSystem(Ref<AssetSystem> assets)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!assets || !assets->IsOpen())
            throw std::invalid_argument("Managed data integration requires an open AssetSystem.");
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            if ((!m_Impl->PendingManagedAssetLoads.empty() || !m_Impl->ManagedAssetSources.empty()) &&
                m_Impl->Assets != assets)
            {
                throw std::logic_error("The managed data AssetSystem cannot change while assets are active.");
            }
        }
        m_Impl->Assets = std::move(assets);
    }

    void ScriptSystem::PumpManagedAssets()
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext || !m_Impl->ActiveNativeRuntimeType)
            return;
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Reload.State != ManagedReloadState::Active)
                return;
            generation = m_Impl->Reload.Generation;
        }

        struct Completion final
        {
            AssetId Id;
            AssetHandle<ManagedDataAsset> Handle;
            bool Failed = false;
            std::string Diagnostic;
        };
        std::vector<Completion> completions;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            for (auto iterator = m_Impl->PendingManagedAssetLoads.begin();
                 iterator != m_Impl->PendingManagedAssetLoads.end();)
            {
                if (iterator->second.Generation != generation)
                {
                    ++iterator;
                    continue;
                }
                const auto state = iterator->second.Handle.State();
                if (state == AssetState::Ready || state == AssetState::Reloading)
                {
                    completions.push_back({iterator->first, iterator->second.Handle});
                    iterator = m_Impl->PendingManagedAssetLoads.erase(iterator);
                }
                else if (state == AssetState::Failed || state == AssetState::Cancelled)
                {
                    const auto diagnostic = iterator->second.Handle.Diagnostic();
                    completions.push_back(
                        {iterator->first, iterator->second.Handle, true,
                         diagnostic.Message.empty() ? "Managed data asset loading failed." : diagnostic.Message});
                    iterator = m_Impl->PendingManagedAssetLoads.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }

        for (auto& completion : completions)
        {
            try
            {
                const Impl::RuntimeScope scope(*m_Impl);
                if (completion.Failed)
                {
                    (void)m_Impl->ActiveNativeRuntimeType->InvokeStaticMethod<bool>(
                        "FailManagedAssetLoad", generation, completion.Id.High(), completion.Id.Low(),
                        completion.Diagnostic);
                    continue;
                }
                const auto asset = completion.Handle.TryGetLoaded();
                if (!asset)
                    throw std::runtime_error("Managed data asset completed without a loaded object.");
                auto object = m_Impl->HydrateManagedAsset(*asset, m_Impl->ActiveManagedAssetRuntimeTypes);
                if (object.InvokeMethod<Coral::Bool32>("RuntimeCompleteManagedAssetLoad", generation,
                                                       completion.Id.High(), completion.Id.Low()) == 0)
                {
                    continue;
                }
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                m_Impl->ManagedAssetSources.insert_or_assign(
                    completion.Id, Impl::ManagedAssetSource{completion.Handle, completion.Handle.Revision()});
            }
            catch (const std::exception& exception)
            {
                const Impl::RuntimeScope scope(*m_Impl);
                (void)m_Impl->ActiveNativeRuntimeType->InvokeStaticMethod<bool>(
                    "FailManagedAssetLoad", generation, completion.Id.High(), completion.Id.Low(),
                    std::string(exception.what()));
            }
        }

        std::vector<std::pair<AssetId, Impl::ManagedAssetSource>> reloads;
        {
            std::scoped_lock lock(m_Impl->ManagedAssetMutex);
            for (const auto& [id, source] : m_Impl->ManagedAssetSources)
                if (source.Handle.Revision() != source.ObservedRevision && source.Handle.TryGetLoaded())
                    reloads.emplace_back(id, source);
        }
        for (const auto& [id, source] : reloads)
        {
            try
            {
                const auto asset = source.Handle.TryGetLoaded();
                if (!asset)
                    continue;
                auto candidate = m_Impl->HydrateManagedAsset(*asset, m_Impl->ActiveManagedAssetRuntimeTypes);
                const Impl::RuntimeScope scope(*m_Impl);
                if (candidate.InvokeMethod<Coral::Bool32>("RuntimeReloadManagedAsset", generation, id.High(),
                                                          id.Low()) == 0)
                    continue;
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                if (const auto found = m_Impl->ManagedAssetSources.find(id); found != m_Impl->ManagedAssetSources.end())
                    found->second.ObservedRevision = source.Handle.Revision();
            }
            catch (const std::exception& exception)
            {
                std::scoped_lock lock(m_Impl->ManagedAssetMutex);
                m_Impl->ActiveManagedAssetDiagnostics.push_back(
                    {"Asset " + id.ToString(),
                     std::string("Hot reload kept the last-good object: ") + exception.what()});
                if (const auto found = m_Impl->ManagedAssetSources.find(id); found != m_Impl->ManagedAssetSources.end())
                    found->second.ObservedRevision = source.Handle.Revision();
            }
        }
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
        const auto [instance, inserted] = m_Impl->Instances.emplace(
            id, Impl::BehaviourInstance{std::move(typeName), type->ComponentType, world, entity, std::move(object)});
        (void)inserted;
        instance->second.CallbackMask = m_Impl->ReadCallbackMask(instance->second.Object);
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

    ManagedRuntimeMetrics ScriptSystem::Metrics() const
    {
        m_Impl->RequireOwner();
        ManagedRuntimeMetrics result;
        result.Generation = m_Impl->Reload.Generation;
        result.ActiveInstances = m_Impl->Instances.size();
        result.Diagnostics = m_Impl->RuntimeDiagnostics.size();
        result.FaultedInstances = static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->Instances, [](const auto& entry) { return entry.second.Faulted; }));
        result.CallbackInvocations = m_Impl->CallbackInvocations;
        result.SkippedCallbacks = m_Impl->SkippedCallbacks;
        result.ManagedInteropCalls = m_Impl->ManagedInteropCalls;
        result.CallbackMilliseconds = m_Impl->CallbackMilliseconds;
        result.MaximumCallbackMilliseconds = m_Impl->MaximumCallbackMilliseconds;
        return result;
    }

    ManagedCallbackMetrics ScriptSystem::CallbackMetrics() const
    {
        m_Impl->RequireOwner();
        constexpr std::size_t maximumEntries = 64;
        ManagedCallbackMetrics result;
        result.Entries.reserve(std::min(maximumEntries, m_Impl->Instances.size() * 3));
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            (void)id;
            for (std::size_t callbackIndex = 0; callbackIndex < Impl::CallbackProfileCount; ++callbackIndex)
            {
                const auto& source = instance.CallbackProfiles[callbackIndex];
                if (source.Invocations == 0 && source.SkippedInvocations == 0)
                    continue;
                const auto callback = static_cast<ManagedBehaviourCallback>(callbackIndex);
                const auto found =
                    std::ranges::find_if(result.Entries, [&](const ManagedCallbackMetric& entry)
                                         { return entry.TypeName == instance.TypeName && entry.Callback == callback; });
                ManagedCallbackMetric* destination = nullptr;
                if (found == result.Entries.end())
                {
                    if (result.Entries.size() == maximumEntries)
                    {
                        result.Truncated = true;
                        continue;
                    }
                    result.Entries.push_back({instance.TypeName, callback, 0, 0, 0, 0.0, 0.0});
                    destination = std::addressof(result.Entries.back());
                }
                else
                {
                    destination = std::addressof(*found);
                }
                ++destination->InstanceCount;
                destination->Invocations += source.Invocations;
                destination->SkippedInvocations += source.SkippedInvocations;
                destination->Milliseconds += source.Milliseconds;
                destination->MaximumMilliseconds =
                    std::max(destination->MaximumMilliseconds, source.MaximumMilliseconds);
            }
        }
        std::ranges::sort(result.Entries,
                          [](const ManagedCallbackMetric& left, const ManagedCallbackMetric& right)
                          {
                              if (left.Milliseconds != right.Milliseconds)
                                  return left.Milliseconds > right.Milliseconds;
                              if (left.TypeName != right.TypeName)
                                  return left.TypeName < right.TypeName;
                              return left.Callback < right.Callback;
                          });
        return result;
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

    std::vector<ManagedBehaviourCheckpoint> ScriptSystem::CaptureReplayCheckpoint()
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        std::vector<ManagedBehaviourCheckpoint> result;
        result.reserve(m_Impl->Instances.size());
        for (auto& [id, instance] : m_Impl->Instances)
        {
            (void)id;
            if (!instance.Object.IsValid())
                continue;
            instance.State = m_Impl->CaptureState(instance.Object, true);
            result.push_back({instance.TypeName, instance.ComponentType, instance.World, instance.Entity,
                              instance.State, instance.Enabled, instance.Faulted});
        }
        std::ranges::sort(result,
                          [](const ManagedBehaviourCheckpoint& left, const ManagedBehaviourCheckpoint& right)
                          {
                              return std::tie(left.World, left.Entity, left.ComponentType, left.TypeName) <
                                     std::tie(right.World, right.Entity, right.ComponentType, right.TypeName);
                          });
        return result;
    }

    void ScriptSystem::RestoreReplayCheckpoint(const std::span<const ManagedBehaviourCheckpoint> checkpoint)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        using Key = std::tuple<std::uint64_t, AssetId, ComponentTypeId>;
        std::map<Key, std::uint64_t> instances;
        for (const auto& [id, instance] : m_Impl->Instances)
        {
            if (instance.Object.IsValid() &&
                !instances.emplace(Key{instance.World, instance.Entity, instance.ComponentType}, id).second)
            {
                throw std::runtime_error("Managed replay state contains duplicate runtime behaviour identities.");
            }
        }
        std::set<Key> checkpointKeys;
        for (const auto& state : checkpoint)
        {
            const Key key{state.World, state.Entity, state.ComponentType};
            const auto found = instances.find(key);
            if (state.World == 0 || !state.Entity || !state.ComponentType || state.TypeName.empty() ||
                !checkpointKeys.insert(key).second || found == instances.end() ||
                m_Impl->Instances.at(found->second).TypeName != state.TypeName)
            {
                throw std::runtime_error("Managed replay checkpoint is incompatible with the runtime behaviours.");
            }
        }
        if (checkpointKeys.size() != instances.size())
            throw std::runtime_error("Managed replay checkpoint does not contain every runtime behaviour.");

        struct RollbackState final
        {
            std::uint64_t Instance = 0;
            std::string State;
            bool Enabled = true;
            bool Faulted = false;
        };
        std::vector<RollbackState> rollback;
        rollback.reserve(checkpoint.size());
        try
        {
            for (const auto& state : checkpoint)
            {
                const auto id = instances.at(Key{state.World, state.Entity, state.ComponentType});
                auto& instance = m_Impl->Instances.at(id);
                rollback.push_back(
                    {id, m_Impl->CaptureState(instance.Object, true), instance.Enabled, instance.Faulted});
                m_Impl->RestoreState(instance.Object, state.State, true);
                instance.State = state.State;
                instance.Enabled = state.Enabled;
                instance.Faulted = state.Faulted;
            }
        }
        catch (...)
        {
            const auto original = std::current_exception();
            for (auto iterator = rollback.rbegin(); iterator != rollback.rend(); ++iterator)
            {
                try
                {
                    auto& instance = m_Impl->Instances.at(iterator->Instance);
                    m_Impl->RestoreState(instance.Object, iterator->State, true);
                    instance.State = iterator->State;
                    instance.Enabled = iterator->Enabled;
                    instance.Faulted = iterator->Faulted;
                }
                catch (...)
                {
                }
            }
            std::rethrow_exception(original);
        }
    }

    void ScriptSystem::InstallManagedComponents(const Ref<ComponentRegistry>& registry)
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
            registration.RequiredComponents = type.RequiredComponents;
            registration.Properties = type.Properties;
            registration.Methods = std::make_shared<const std::vector<ComponentMethod>>(type.Methods);
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
        if (m_Impl->WorkScope)
        {
            m_Impl->WorkScope->Cancel();
            m_Impl->WorkScope->Wait();
        }
        if (m_Impl->OwnScheduler && m_Impl->Scheduler)
            m_Impl->Scheduler->Close();
        m_Impl->ShutdownRuntime();
    }
} // namespace Keire
