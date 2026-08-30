#include "Keire/Project/Project.h"
#include "Keire/Project/ProjectAuthoringSettings.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/BuildInfo.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/PlatformDirectories.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Project/StarterProjectUiInternal.h"
#include "KeireInternal/Scripting/ManagedSdk.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumProjectFileBytes = 1024ULL * 1024U;
        constexpr std::size_t MaximumRegistryBytes = 4ULL * 1024ULL * 1024U;
        constexpr std::size_t MaximumRecentProjects = 50;

        struct EngineVersion
        {
            std::uint32_t Major = 0;
            std::uint32_t Minor = 0;
            std::uint32_t Patch = 0;
            auto operator<=>(const EngineVersion&) const noexcept = default;
        };

        [[nodiscard]] EngineVersion ParseVersion(const std::string_view value)
        {
            EngineVersion result;
            std::array<std::uint32_t*, 3> fields{&result.Major, &result.Minor, &result.Patch};
            std::size_t begin = 0;
            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                const auto end = value.find_first_of(index == 2 ? "-+" : ".", begin);
                const auto component =
                    value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (component.empty() ||
                    !std::ranges::all_of(component, [](const char character)
                                         { return std::isdigit(static_cast<unsigned char>(character)); }))
                    throw std::invalid_argument("Project contains an invalid semantic engine version.");
                std::uint64_t parsed = 0;
                for (const char character : component)
                    parsed = parsed * 10U + static_cast<unsigned>(character - '0');
                if (parsed > UINT32_MAX)
                    throw std::invalid_argument("Project engine version component is out of range.");
                *fields[index] = static_cast<std::uint32_t>(parsed);
                if (index < 2 && end == std::string_view::npos)
                    throw std::invalid_argument("Project contains an incomplete semantic engine version.");
                begin = end == std::string_view::npos ? value.size() : end + 1;
            }
            return result;
        }

        [[nodiscard]] std::string FormatUtc(const std::chrono::system_clock::time_point value)
        {
            const auto time = std::chrono::system_clock::to_time_t(value);
            std::tm utc{};
#if defined(_WIN32)
            if (gmtime_s(&utc, &time) != 0)
                throw std::runtime_error("Cannot format the project timestamp.");
#else
            if (!gmtime_r(&time, &utc))
                throw std::runtime_error("Cannot format the project timestamp.");
#endif
            std::ostringstream stream;
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        [[nodiscard]] std::string NowUtc() { return FormatUtc(std::chrono::system_clock::now()); }

        [[nodiscard]] std::string FileTimestampUtc(const std::filesystem::path& path)
        {
            const auto fileNow = std::filesystem::file_time_type::clock::now();
            const auto systemNow = std::chrono::system_clock::now();
            const auto fileTime = std::filesystem::last_write_time(path);
            return FormatUtc(systemNow +
                             std::chrono::duration_cast<std::chrono::system_clock::duration>(fileTime - fileNow));
        }

        void ValidateTimestamp(const std::string_view value, const std::string_view field)
        {
            if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
                value[16] != ':' || value[19] != 'Z')
                throw std::invalid_argument("Project " + std::string(field) + " must be a UTC ISO-8601 timestamp.");
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || index == 19)
                    continue;
                if (!std::isdigit(static_cast<unsigned char>(value[index])))
                    throw std::invalid_argument("Project " + std::string(field) + " must be a UTC ISO-8601 timestamp.");
            }
        }

        void ValidateName(const std::string_view name)
        {
            if (name.empty() || name.size() > 128 || name == "." || name == ".." ||
                name.find_first_of("<>:\"/\\|?*\r\n\t") != std::string_view::npos ||
                std::isspace(static_cast<unsigned char>(name.front())) ||
                std::isspace(static_cast<unsigned char>(name.back())))
                throw std::invalid_argument("Project name is empty, unsafe, or exceeds 128 UTF-8 bytes.");
        }

        [[nodiscard]] std::filesystem::path MarkerPath(const std::filesystem::path& root)
        {
            return root / "ProjectSettings" / "Project.keireproject";
        }

        [[nodiscard]] std::filesystem::path ResolveRoot(const std::filesystem::path& value)
        {
            auto path = Detail::CanonicalExistingPath(value);
            if (std::filesystem::is_regular_file(path))
            {
                if (path.filename() != "Project.keireproject" || path.parent_path().filename() != "ProjectSettings")
                    throw std::invalid_argument("Project file must be ProjectSettings/Project.keireproject.");
                path = path.parent_path().parent_path();
            }
            if (!std::filesystem::is_directory(path) || !std::filesystem::is_regular_file(MarkerPath(path)))
                throw std::invalid_argument("Directory is not a Kéire project: " + path.string());
            return path;
        }

        [[nodiscard]] ProjectDescriptor ParseDescriptor(const std::filesystem::path& root)
        {
            const auto document = Json::parse(Detail::ReadTextFile(MarkerPath(root), MaximumProjectFileBytes));
            if (!document.is_object())
                throw std::runtime_error("Project descriptor root must be an object.");
            ProjectDescriptor descriptor;
            descriptor.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
            descriptor.Id = ProjectId::Parse(document.at("id").get<std::string>());
            descriptor.Name = document.at("name").get<std::string>();
            descriptor.CreatedWithEngineVersion = document.at("createdWithEngineVersion").get<std::string>();
            descriptor.MinimumEngineVersion = document.at("minimumEngineVersion").get<std::string>();
            if (document.contains("startupScene") && !document["startupScene"].is_null())
                descriptor.StartupScene = AssetId::Parse(document["startupScene"].get<std::string>());
            if (document.contains("defaultInput") && !document["defaultInput"].is_null())
                descriptor.DefaultInput = AssetId::Parse(document["defaultInput"].get<std::string>());
            if (descriptor.SchemaVersion >= 4 && document.contains("defaultInputMap") &&
                !document["defaultInputMap"].is_null())
                descriptor.DefaultInputMap = AssetId::Parse(document["defaultInputMap"].get<std::string>());
            if (descriptor.SchemaVersion >= 2)
            {
                const auto& modules = document.at("requiredModules");
                if (!modules.is_array())
                    throw std::runtime_error("Project requiredModules must be an array.");
                for (const auto& module : modules)
                {
                    RequiredSourceModule requirement;
                    requirement.Id = module.at("id").get<std::string>();
                    requirement.VersionRange = module.at("version").get<std::string>();
                    if (requirement.Id.empty() || requirement.VersionRange.empty())
                        throw std::runtime_error("Project contains an invalid required source module.");
                    descriptor.RequiredModules.push_back(std::move(requirement));
                }
                if (!std::ranges::is_sorted(descriptor.RequiredModules, {}, &RequiredSourceModule::Id) ||
                    std::ranges::adjacent_find(descriptor.RequiredModules, {}, &RequiredSourceModule::Id) !=
                        descriptor.RequiredModules.end())
                    throw std::runtime_error("Project required source modules must be unique and sorted by ID.");
            }
            if (descriptor.SchemaVersion >= 3)
            {
                descriptor.CreatedAt = document.at("createdAt").get<std::string>();
                descriptor.LastSavedWithEngineVersion = document.at("lastSavedWithEngineVersion").get<std::string>();
                if (document.contains("template") && !document.at("template").is_null())
                {
                    const auto& value = document.at("template");
                    ProjectDescriptor::TemplateProvenance provenance;
                    provenance.Id = value.at("id").get<std::string>();
                    provenance.Version = value.at("version").get<std::string>();
                    if (provenance.Id.empty() || provenance.Id.size() > 128 || provenance.Version.empty() ||
                        provenance.Version.size() > 128)
                        throw std::runtime_error("Project contains invalid template provenance.");
                    descriptor.Template = std::move(provenance);
                }
            }
            if (descriptor.SchemaVersion == 0 || descriptor.SchemaVersion > CurrentProjectSchemaVersion ||
                !descriptor.Id)
                throw std::runtime_error("Project descriptor uses an unsupported schema or has no identity.");
            ValidateName(descriptor.Name);
            (void)ParseVersion(descriptor.CreatedWithEngineVersion);
            (void)ParseVersion(descriptor.MinimumEngineVersion);
            if (descriptor.SchemaVersion >= 3)
            {
                ValidateTimestamp(descriptor.CreatedAt, "createdAt");
                (void)ParseVersion(descriptor.LastSavedWithEngineVersion);
            }
            return descriptor;
        }

        void WriteDescriptor(const std::filesystem::path& root, const ProjectDescriptor& descriptor)
        {
            if (descriptor.SchemaVersion != CurrentProjectSchemaVersion || !descriptor.Id)
                throw std::invalid_argument("Project descriptor uses an unsupported schema or has no identity.");
            ValidateName(descriptor.Name);
            (void)ParseVersion(descriptor.CreatedWithEngineVersion);
            (void)ParseVersion(descriptor.MinimumEngineVersion);
            ValidateTimestamp(descriptor.CreatedAt, "createdAt");
            (void)ParseVersion(descriptor.LastSavedWithEngineVersion);
            Json document{{"schemaVersion", descriptor.SchemaVersion},
                          {"id", descriptor.Id.ToString()},
                          {"name", descriptor.Name},
                          {"createdWithEngineVersion", descriptor.CreatedWithEngineVersion},
                          {"minimumEngineVersion", descriptor.MinimumEngineVersion},
                          {"createdAt", descriptor.CreatedAt},
                          {"lastSavedWithEngineVersion", descriptor.LastSavedWithEngineVersion}};
            if (descriptor.Template)
            {
                if (descriptor.Template->Id.empty() || descriptor.Template->Id.size() > 128 ||
                    descriptor.Template->Version.empty() || descriptor.Template->Version.size() > 128)
                    throw std::invalid_argument("Template provenance requires bounded ID and version values.");
                document["template"] = {{"id", descriptor.Template->Id}, {"version", descriptor.Template->Version}};
            }
            else
                document["template"] = nullptr;
            document["startupScene"] =
                descriptor.StartupScene ? Json(descriptor.StartupScene.ToString()) : Json(nullptr);
            document["defaultInput"] =
                descriptor.DefaultInput ? Json(descriptor.DefaultInput.ToString()) : Json(nullptr);
            document["defaultInputMap"] =
                descriptor.DefaultInputMap ? Json(descriptor.DefaultInputMap.ToString()) : Json(nullptr);
            auto requiredModules = descriptor.RequiredModules;
            std::ranges::sort(requiredModules, {}, &RequiredSourceModule::Id);
            if (std::ranges::adjacent_find(requiredModules, {}, &RequiredSourceModule::Id) != requiredModules.end())
                throw std::invalid_argument("Required source module IDs must be unique.");
            document["requiredModules"] = Json::array();
            for (const auto& module : requiredModules)
            {
                if (module.Id.empty() || module.VersionRange.empty())
                    throw std::invalid_argument("Required source modules must include an ID and version range.");
                document["requiredModules"].push_back({{"id", module.Id}, {"version", module.VersionRange}});
            }
            Detail::WriteTextFileAtomically(MarkerPath(root), document.dump(2) + '\n');
        }

        [[nodiscard]] bool RequiresNewerEngine(const ProjectDescriptor& descriptor)
        {
            return ParseVersion(descriptor.MinimumEngineVersion) > ParseVersion(GetBuildInfo().Version);
        }

        [[nodiscard]] std::filesystem::path DefaultRegistryPath()
        {
            return GetPreferenceDirectory() / "Hub" / "projects.json";
        }

        [[nodiscard]] std::uint64_t NowUnixSeconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        [[nodiscard]] std::string_view RecentStatusName(const ProjectStatus status) noexcept
        {
            switch (status)
            {
            case ProjectStatus::Ready:
                return "ready";
            case ProjectStatus::UpgradeAvailable:
                return "upgradeAvailable";
            case ProjectStatus::RecoveryRequired:
                return "recoveryRequired";
            case ProjectStatus::Missing:
                return "missing";
            case ProjectStatus::Invalid:
                return "invalid";
            case ProjectStatus::RequiresNewerEngine:
                return "missingEditor";
            case ProjectStatus::InUse:
                return "locked";
            case ProjectStatus::UnsupportedSchema:
                return "unsupportedSchema";
            }
            return "unknown";
        }

        [[nodiscard]] ProjectStatus ParseRecentStatus(const std::string_view status) noexcept
        {
            if (status == "ready")
                return ProjectStatus::Ready;
            if (status == "upgradeAvailable")
                return ProjectStatus::UpgradeAvailable;
            if (status == "recoveryRequired")
                return ProjectStatus::RecoveryRequired;
            if (status == "missing")
                return ProjectStatus::Missing;
            if (status == "invalid")
                return ProjectStatus::Invalid;
            if (status == "missingEditor")
                return ProjectStatus::RequiresNewerEngine;
            if (status == "locked")
                return ProjectStatus::InUse;
            if (status == "unsupportedSchema")
                return ProjectStatus::UnsupportedSchema;
            return ProjectStatus::Invalid;
        }
    } // namespace

    class Project::Impl final
    {
      public:
        Impl(std::filesystem::path root, ProjectDescriptor descriptor, const ProjectOpenMode mode)
            : RootPath(std::move(root)), Description(std::move(descriptor)), OpenMode(mode)
        {
            if (mode == ProjectOpenMode::Exclusive)
                AcquireLock();
        }

        ~Impl() { ReleaseLock(); }

        void AcquireLock()
        {
            const auto path = RootPath / "Library" / "Editor.lock";
            std::filesystem::create_directories(path.parent_path());
#if defined(_WIN32)
            LockHandle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (LockHandle == INVALID_HANDLE_VALUE)
            {
                LockHandle = nullptr;
                throw std::runtime_error("Project is already open in another editor.");
            }
#else
            LockHandle = open(path.c_str(), O_CREAT | O_RDWR, 0644);
            if (LockHandle < 0 || flock(LockHandle, LOCK_EX | LOCK_NB) != 0)
            {
                if (LockHandle >= 0)
                    close(LockHandle);
                LockHandle = -1;
                throw std::runtime_error("Project is already open in another editor.");
            }
#endif
        }

        void ReleaseLock() noexcept
        {
#if defined(_WIN32)
            if (LockHandle)
                CloseHandle(LockHandle);
            LockHandle = nullptr;
#else
            if (LockHandle >= 0)
            {
                (void)flock(LockHandle, LOCK_UN);
                close(LockHandle);
            }
            LockHandle = -1;
#endif
        }

        std::filesystem::path RootPath;
        ProjectDescriptor Description;
        ProjectOpenMode OpenMode = ProjectOpenMode::ReadOnly;
#if defined(_WIN32)
        HANDLE LockHandle = nullptr;
#else
        int LockHandle = -1;
#endif
    };

    Project::Project(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    Project::~Project() = default;

    Ref<Project> Project::Create(const ProjectCreateSpecification& specification)
    {
        ValidateName(specification.Name);
        if (specification.Location.empty())
            throw std::invalid_argument("Project location must not be empty.");
        const auto location = std::filesystem::absolute(specification.Location).lexically_normal();
        if (!std::filesystem::is_directory(location))
            throw std::invalid_argument("Project location must be an existing directory.");
        const auto root = location / specification.Name;
        if (std::filesystem::exists(root))
            throw std::invalid_argument("Project destination already exists: " + root.string());

        std::filesystem::create_directory(root);
        try
        {
            std::filesystem::create_directories(root / "Assets");
            std::filesystem::create_directories(root / "ProjectSettings");
            std::filesystem::create_directories(root / "Library");
            Detail::WriteTextFileAtomically(root / ".gitignore",
                                            "/Library/\n/Logs/\n/Temp/\n/Build/\n/*.csproj\n/*.sln\n");
            SaveRenderEnvironmentSettings(root, {});
            SaveProjectAuthoringSettings(root, DefaultProjectAuthoringSettings());
            Detail::WriteManagedSdkConfiguration(root, {});

            ProjectDescriptor descriptor;
            descriptor.Id = ProjectId::Generate();
            descriptor.Name = specification.Name;
            descriptor.CreatedWithEngineVersion = std::string(GetBuildInfo().Version);
            descriptor.MinimumEngineVersion = descriptor.CreatedWithEngineVersion;
            descriptor.CreatedAt = NowUtc();
            descriptor.LastSavedWithEngineVersion = descriptor.CreatedWithEngineVersion;
            descriptor.Template = ProjectDescriptor::TemplateProvenance{
                specification.Template == ProjectTemplate::Empty ? "keire.empty" : "keire.3d-starter", "1.0.0"};
            if (specification.Template == ProjectTemplate::Starter)
            {
                AssetDatabaseSpecification databaseSpecification{.ProjectRoot = root};
                databaseSpecification.Importers = CreateBuiltinAssetImporters();
                auto database = CreateRef<AssetDatabase>(std::move(databaseSpecification));
                const auto inputDefinition = InputActionAsset::DefaultDefinition();
                const auto inputBytes = InputActionAsset::Encode(inputDefinition);
                descriptor.DefaultInput = database->CreateAsset("Input/DefaultInput.keireinput",
                                                                CreateInputActionAssetImporter(), inputBytes);
                if (!inputDefinition.ActionMaps.empty())
                    descriptor.DefaultInputMap = inputDefinition.ActionMaps.front().Id;

                ManagedAssemblyDefinition gameplayAssembly;
                gameplayAssembly.Name = "Gameplay";
                gameplayAssembly.RootNamespace = "Game";
                gameplayAssembly.SourceRoots = {"Assets/Scripts/Runtime"};
                const auto assemblyBytes = ManagedAssemblyAsset::Encode(gameplayAssembly);
                (void)database->CreateAsset("Scripts/Gameplay.keireasm", CreateManagedAssemblyAssetImporter(),
                                            assemblyBytes);
                std::filesystem::create_directories(root / "Assets/Scripts/Runtime");
                Detail::WriteTextFileAtomically(
                    root / "Assets/Scripts/Runtime/GameRoot.cs",
                    "using Keire;\n\nnamespace Game;\n\npublic sealed class GameRoot : Behaviour\n{\n"
                    "    protected override void Start() => Log.Info(\"Gameplay assembly loaded.\");\n}\n");
                Detail::WriteStarterProjectUiScript(root);

                constexpr std::string_view hlsl = R"(struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
    float4 Color : TEXCOORD3;
};

struct VertexOutput
{
    float3 Normal : TEXCOORD0;
    float2 UV0 : TEXCOORD1;
    float4 Color : TEXCOORD2;
    float4 Position : SV_Position;
};

cbuffer ObjectData : register(b0, space1)
{
    float4x4 Model;
    float4x4 View;
    float4x4 Projection;
    float4x4 NormalMatrix;
};

cbuffer SceneData : register(b0, space3)
{
    float4 AmbientColorIntensity;
    float4 DirectionalColorIntensity;
    float4 DirectionalDirectionExposure;
};

cbuffer MaterialData : register(b1, space3)
{
    float4 Tint;
};

Texture2D MainTexture : register(t0, space2);
SamplerState MainSampler : register(s0, space2);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(Model, float4(input.Position, 1.0F));
    output.Position = mul(Projection, mul(View, worldPosition));
    output.Normal = normalize(mul((float3x3)NormalMatrix, input.Normal));
    output.UV0 = input.UV0;
    output.Color = input.Color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float3 normal = normalize(input.Normal);
    const float directional = saturate(dot(normal, -DirectionalDirectionExposure.xyz));
    const float3 lighting = AmbientColorIntensity.rgb * AmbientColorIntensity.a +
                            DirectionalColorIntensity.rgb * DirectionalColorIntensity.a * directional;
    const float4 surface = MainTexture.Sample(MainSampler, input.UV0) * input.Color * Tint;
    return float4(surface.rgb * lighting * DirectionalDirectionExposure.w, surface.a);
}
)";
                const auto shaderSource = root / "Assets/Shaders/DefaultUnlit.hlsl";
                std::filesystem::create_directories(shaderSource.parent_path());
                Detail::WriteTextFileAtomically(shaderSource, hlsl);
                const auto shaderManifest =
                    Json{{"schemaVersion", 1},
                         {"source", "Assets/Shaders/DefaultUnlit.hlsl"},
                         {"stages", {{"vertex", "VSMain"}, {"fragment", "PSMain"}}},
                         {"defines", Json::object()},
                         {"includeRoots", Json::array({"Assets/Shaders"})},
                         {"renderState",
                          {{"topology", "TriangleList"},
                           {"culling", "Back"},
                           {"depthTest", true},
                           {"depthWrite", true},
                           {"blend", false}}},
                         {"properties",
                          Json::array({{{"name", "Tint"},
                                        {"type", "Color"},
                                        {"default", Json::array({0.25F, 0.55F, 1.0F, 1.0F})}},
                                       {{"name", "MainTexture"}, {"type", "Texture2D"}, {"default", nullptr}}})}}
                        .dump(2) +
                    '\n';
                const auto shaderBytes = std::as_bytes(std::span(shaderManifest.data(), shaderManifest.size()));
                const auto shader =
                    database->CreateAsset("Shaders/DefaultUnlit.keireshader", CreateShaderAssetImporter(), shaderBytes);
                const auto materialSource = Json{{"schemaVersion", 1},
                                                 {"shader", shader.ToString()},
                                                 {"properties", {{"Tint", Json::array({0.25F, 0.55F, 1.0F, 1.0F})}}}}
                                                .dump(2) +
                                            '\n';
                const auto materialBytes = std::as_bytes(std::span(materialSource.data(), materialSource.size()));
                const auto material = database->CreateAsset("Materials/DefaultUnlit.keirematerial",
                                                            CreateMaterialAssetImporter(), materialBytes);
                const auto starterUiAssets = Detail::CreateStarterProjectUiAssets(*database);

                auto sceneDefinition = SceneAsset::SampleDefinition(material);
                SceneObjectDefinition starterUi{
                    AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000004"), {}, "Starter UI Document"};
                starterUi.Components.push_back({UiDocumentComponent::StaticType(), 1, true,
                                                Json{{"visualTree", starterUiAssets.VisualTree.ToString()},
                                                     {"panelSettings", starterUiAssets.PanelSettings.ToString()},
                                                     {"sortingOrder", 0},
                                                     {"receivesInput", true}}
                                                    .dump()});
                starterUi.Components.push_back({ComponentTypeId(AssetId::Parse("b1b2d001-1000-4000-8000-000000000001")),
                                                1, true,
                                                Json{{"managedState", R"({"fields":[],"version":1})"}}.dump()});
                sceneDefinition.Objects.push_back(std::move(starterUi));

                SceneObjectDefinition worldUi{
                    AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000005"), {}, "Starter World UI"};
                worldUi.Transform.Position = {0.0F, 2.0F, 3.0F};
                worldUi.Components.push_back({UiDocumentComponent::StaticType(), 1, true,
                                              Json{{"visualTree", starterUiAssets.WorldVisualTree.ToString()},
                                                   {"panelSettings", starterUiAssets.WorldPanelSettings.ToString()},
                                                   {"sortingOrder", 0},
                                                   {"receivesInput", true}}
                                                  .dump()});
                worldUi.Components.push_back({ComponentTypeId(AssetId::Parse("b1b2d001-1000-4000-8000-000000000002")),
                                              1, true, Json{{"managedState", R"({"fields":[],"version":1})"}}.dump()});
                sceneDefinition.Objects.push_back(std::move(worldUi));

                SceneObjectDefinition renderTextureUi{
                    AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000006"), {}, "Starter UI Render Target"};
                renderTextureUi.Components.push_back(
                    {UiDocumentComponent::StaticType(), 1, true,
                     Json{{"visualTree", starterUiAssets.RenderTextureVisualTree.ToString()},
                          {"panelSettings", starterUiAssets.RenderTexturePanelSettings.ToString()},
                          {"sortingOrder", -10},
                          {"receivesInput", false}}
                         .dump()});
                sceneDefinition.Objects.push_back(std::move(renderTextureUi));
                const auto sceneBytes = SceneAsset::Encode(sceneDefinition);
                descriptor.StartupScene =
                    database->CreateAsset("Scenes/SampleScene.keirescene", CreateSceneAssetImporter(), sceneBytes);
                (void)database->ImportAll();
            }
            WriteDescriptor(root, descriptor);
            return Open(root, ProjectOpenMode::ReadOnly);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
            throw;
        }
    }

    Ref<Project> Project::Open(const std::filesystem::path& path, const ProjectOpenMode mode)
    {
        const auto root = ResolveRoot(path);
        auto descriptor = ParseDescriptor(root);
        if (descriptor.SchemaVersion < CurrentProjectSchemaVersion)
            throw std::runtime_error("Project upgrade is required before opening this project.");
        if (RequiresNewerEngine(descriptor))
            throw std::runtime_error("Project requires a newer Kéire engine version.");
        return CreateRef<Project>(std::make_unique<Impl>(root, std::move(descriptor), mode));
    }

    ProjectInspectionResult Project::InspectMetadata(const std::filesystem::path& path) noexcept
    {
        ProjectInspectionResult result;
        try
        {
            if (!std::filesystem::exists(path))
            {
                result.Status = ProjectStatus::Missing;
                result.Diagnostic = "Project path does not exist.";
                return result;
            }
            const auto root = ResolveRoot(path);
            result.Root = root;
            if (std::filesystem::is_regular_file(root / "Library" / "ProjectUpgrades" / "Active" / "journal.json"))
                result.Status = ProjectStatus::RecoveryRequired;

            const auto document = Json::parse(Detail::ReadTextFile(MarkerPath(root), MaximumProjectFileBytes));
            if (!document.is_object())
                throw std::runtime_error("Project descriptor root must be an object.");
            result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
            result.Id = ProjectId::Parse(document.at("id").get<std::string>());
            result.Name = document.at("name").get<std::string>();
            result.CreatedWithEngineVersion = document.at("createdWithEngineVersion").get<std::string>();
            result.MinimumEngineVersion = document.at("minimumEngineVersion").get<std::string>();
            ValidateName(result.Name);
            (void)ParseVersion(result.CreatedWithEngineVersion);
            (void)ParseVersion(result.MinimumEngineVersion);
            if (document.contains("createdAt") && document.at("createdAt").is_string())
                result.CreatedAt = document.at("createdAt").get<std::string>();
            else
                result.CreatedAt = FileTimestampUtc(MarkerPath(root));
            if (document.contains("lastSavedWithEngineVersion") &&
                document.at("lastSavedWithEngineVersion").is_string())
                result.LastSavedWithEngineVersion = document.at("lastSavedWithEngineVersion").get<std::string>();
            else
                result.LastSavedWithEngineVersion = result.CreatedWithEngineVersion;
            if (document.contains("template") && document.at("template").is_object())
            {
                ProjectDescriptor::TemplateProvenance provenance;
                provenance.Id = document.at("template").at("id").get<std::string>();
                provenance.Version = document.at("template").at("version").get<std::string>();
                if (!provenance.Id.empty() && provenance.Id.size() <= 128 && !provenance.Version.empty() &&
                    provenance.Version.size() <= 128)
                    result.Template = std::move(provenance);
            }
            if (result.SchemaVersion > CurrentProjectSchemaVersion)
            {
                result.Status = ProjectStatus::UnsupportedSchema;
                result.Diagnostic = "Project metadata was read, but this Hub does not support its project schema.";
                return result;
            }
            if (result.Status == ProjectStatus::RecoveryRequired)
                return result;
            if (result.SchemaVersion < CurrentProjectSchemaVersion)
            {
                result.Status = ProjectStatus::UpgradeAvailable;
                return result;
            }
            if (ParseVersion(result.MinimumEngineVersion) > ParseVersion(GetBuildInfo().Version))
            {
                result.Status = ProjectStatus::RequiresNewerEngine;
                return result;
            }
            result.Status = ProjectStatus::Ready;
            return result;
        }
        catch (const std::exception& error)
        {
            result.Status = ProjectStatus::Invalid;
            result.Diagnostic = error.what();
            return result;
        }
    }

    ProjectStatus Project::Inspect(const std::filesystem::path& path) noexcept { return InspectMetadata(path).Status; }

    bool Project::IsLocked(const std::filesystem::path& path) noexcept
    {
        try
        {
            const auto root = ResolveRoot(path);
            const auto lockPath = root / "Library" / "Editor.lock";
            if (!std::filesystem::exists(lockPath))
                return false;
#if defined(_WIN32)
            const auto handle = CreateFileW(lockPath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION;
            CloseHandle(handle);
            return false;
#else
            const auto handle = open(lockPath.c_str(), O_RDWR);
            if (handle < 0)
                return false;
            const bool locked = flock(handle, LOCK_EX | LOCK_NB) != 0;
            if (!locked)
                (void)flock(handle, LOCK_UN);
            close(handle);
            return locked;
#endif
        }
        catch (...)
        {
            return false;
        }
    }

    const ProjectDescriptor& Project::Descriptor() const noexcept { return m_Impl->Description; }

    const std::filesystem::path& Project::Root() const noexcept { return m_Impl->RootPath; }

    std::filesystem::path Project::AssetsDirectory() const { return m_Impl->RootPath / "Assets"; }

    std::filesystem::path Project::AssetCatalog() const
    {
        return m_Impl->RootPath / "Library" / "AssetCache" / "Runtime" / "catalog.json";
    }

    std::filesystem::path Project::WorkspaceDirectory() const
    {
        return m_Impl->RootPath / "Library" / "UserSettings" / "Workspace";
    }

    std::filesystem::path Project::InputOverridesDirectory() const
    {
        return m_Impl->RootPath / "Library" / "UserSettings" / "InputOverrides";
    }

    std::filesystem::path Project::SceneRecoveryDirectory() const
    {
        return m_Impl->RootPath / "Library" / "SceneRecovery";
    }

    bool Project::Writable() const noexcept { return m_Impl->OpenMode == ProjectOpenMode::Exclusive; }

    void Project::Save(ProjectDescriptor descriptor)
    {
        if (!Writable())
            throw std::logic_error("Read-only projects cannot save project settings.");
        if (descriptor.Id != m_Impl->Description.Id || descriptor.SchemaVersion != m_Impl->Description.SchemaVersion)
            throw std::invalid_argument("Project identity and schema version are immutable.");
        descriptor.LastSavedWithEngineVersion = std::string(GetBuildInfo().Version);
        WriteDescriptor(m_Impl->RootPath, descriptor);
        m_Impl->Description = std::move(descriptor);
    }

    class ProjectRegistry::Impl final
    {
      public:
        Impl(std::filesystem::path value, const ProjectRegistryLoadMode loadMode)
            : RegistryPath(std::move(value)), LoadMode(loadMode)
        {
            Load();
        }

        void Load()
        {
            if (!std::filesystem::exists(RegistryPath))
                return;
            try
            {
                const auto document = Json::parse(Detail::ReadTextFile(RegistryPath, MaximumRegistryBytes));
                const auto schema = document.at("schemaVersion").get<std::uint32_t>();
                if ((schema != 1 && schema != 2) || !document.at("projects").is_array())
                    throw std::runtime_error("Unsupported project registry schema.");
                for (const auto& value : document.at("projects"))
                {
                    RecentProject entry;
                    entry.Id = ProjectId::Parse(value.at("id").get<std::string>());
                    entry.Root = Detail::PathFromUtf8(value.at("root").get<std::string>());
                    entry.Name = value.at("name").get<std::string>();
                    entry.LastOpenedUnixSeconds = value.value("lastOpened", 0ULL);
                    entry.AddedUnixSeconds = value.value("added", entry.LastOpenedUnixSeconds);
                    entry.CreatedUnixSeconds = value.value("created", 0ULL);
                    entry.LastSavedWithEngineVersion = value.value("lastSavedWithEngineVersion", std::string{});
                    entry.PreferredEditorInstallation = value.value(
                        "preferredEditorInstallation", value.value("preferredEditorInstallationId", std::string{}));
                    entry.Pinned = value.value("pinned", false);
                    if (value.contains("cachedMetadata") && value.at("cachedMetadata").is_object())
                    {
                        const auto& cached = value.at("cachedMetadata");
                        entry.Status = ParseRecentStatus(cached.value("status", std::string("invalid")));
                        entry.CreatedUnixSeconds = cached.value("created", entry.CreatedUnixSeconds);
                        entry.ModifiedUnixSeconds = cached.value("modified", 0ULL);
                        if (cached.contains("sizeBytes"))
                            entry.SizeBytes = cached.at("sizeBytes").get<std::uint64_t>();
                        entry.CreatedWithEngineVersion = cached.value("createdWithEngineVersion", std::string{});
                        entry.LastSavedWithEngineVersion =
                            cached.value("lastSavedWithEngineVersion", entry.LastSavedWithEngineVersion);
                        entry.MinimumEngineVersion = cached.value("minimumEngineVersion", std::string{});
                        entry.ProjectSchemaVersion = cached.value("projectSchemaVersion", 0U);
                    }
                    Projects.push_back(std::move(entry));
                }
            }
            catch (...)
            {
                std::error_code ignored;
                auto corrupt = RegistryPath;
                corrupt += ".corrupt";
                std::filesystem::rename(RegistryPath, corrupt, ignored);
                Projects.clear();
            }
            if (LoadMode == ProjectRegistryLoadMode::RefreshMetadata)
                RefreshEntries();
        }

        void Save() const
        {
            Json projects = Json::array();
            for (const auto& entry : Projects)
            {
                Json cached{{"status", RecentStatusName(entry.Status)},
                            {"created", entry.CreatedUnixSeconds},
                            {"modified", entry.ModifiedUnixSeconds},
                            {"createdWithEngineVersion", entry.CreatedWithEngineVersion},
                            {"lastSavedWithEngineVersion", entry.LastSavedWithEngineVersion},
                            {"minimumEngineVersion", entry.MinimumEngineVersion},
                            {"projectSchemaVersion", entry.ProjectSchemaVersion}};
                if (entry.SizeBytes)
                    cached["sizeBytes"] = *entry.SizeBytes;
                projects.push_back({{"id", entry.Id.ToString()},
                                    {"root", Detail::PathToUtf8(entry.Root)},
                                    {"name", entry.Name},
                                    {"added", entry.AddedUnixSeconds},
                                    {"lastOpened", entry.LastOpenedUnixSeconds},
                                    {"created", entry.CreatedUnixSeconds},
                                    {"lastSavedWithEngineVersion", entry.LastSavedWithEngineVersion},
                                    {"preferredEditorInstallation", entry.PreferredEditorInstallation},
                                    {"preferredEditorInstallationId", entry.PreferredEditorInstallation},
                                    {"pinned", entry.Pinned},
                                    {"cachedMetadata", std::move(cached)}});
            }
            Detail::WriteTextFileAtomically(
                RegistryPath, Json{{"schemaVersion", 2}, {"projects", std::move(projects)}}.dump(2) + '\n');
        }

        void RefreshEntries()
        {
            for (auto& entry : Projects)
            {
                const auto inspection = Project::InspectMetadata(entry.Root);
                entry.Status = inspection.Status;
                if (entry.Status == ProjectStatus::Ready && Project::IsLocked(entry.Root))
                    entry.Status = ProjectStatus::InUse;
                if (inspection.HasIdentity())
                {
                    entry.Id = inspection.Id;
                    entry.Name = inspection.Name;
                    entry.Root = inspection.Root;
                    entry.LastSavedWithEngineVersion = inspection.LastSavedWithEngineVersion;
                    entry.CreatedWithEngineVersion = inspection.CreatedWithEngineVersion;
                    entry.MinimumEngineVersion = inspection.MinimumEngineVersion;
                    entry.ProjectSchemaVersion = inspection.SchemaVersion;
                    if (entry.CreatedUnixSeconds == 0)
                    {
                        std::error_code timestampError;
                        const auto timestamp = std::filesystem::last_write_time(MarkerPath(entry.Root), timestampError);
                        if (!timestampError)
                        {
                            const auto fileNow = std::filesystem::file_time_type::clock::now();
                            const auto systemNow = std::chrono::system_clock::now();
                            const auto created =
                                systemNow +
                                std::chrono::duration_cast<std::chrono::system_clock::duration>(timestamp - fileNow);
                            entry.CreatedUnixSeconds = static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::seconds>(created.time_since_epoch()).count());
                        }
                    }
                    std::error_code modifiedError;
                    const auto timestamp = std::filesystem::last_write_time(MarkerPath(entry.Root), modifiedError);
                    if (!modifiedError)
                    {
                        const auto modified = std::chrono::system_clock::now() +
                                              std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                  timestamp - std::filesystem::file_time_type::clock::now());
                        entry.ModifiedUnixSeconds = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(modified.time_since_epoch()).count());
                    }
                }
                if (entry.Status == ProjectStatus::Ready || entry.Status == ProjectStatus::InUse)
                {
                    try
                    {
                        const auto project = Project::Open(entry.Root);
                        entry.Id = project->Descriptor().Id;
                        entry.Name = project->Descriptor().Name;
                        entry.Root = project->Root();
                    }
                    catch (...)
                    {
                        entry.Status = ProjectStatus::Invalid;
                    }
                }
            }
            std::ranges::sort(Projects,
                              [](const auto& left, const auto& right)
                              {
                                  return std::tie(left.Pinned, left.LastOpenedUnixSeconds) >
                                         std::tie(right.Pinned, right.LastOpenedUnixSeconds);
                              });
        }

        void Trim()
        {
            std::size_t unpinned = 0;
            std::erase_if(Projects,
                          [&](const auto& entry) { return !entry.Pinned && ++unpinned > MaximumRecentProjects; });
        }

        std::filesystem::path RegistryPath;
        std::vector<RecentProject> Projects;
        ProjectRegistryLoadMode LoadMode = ProjectRegistryLoadMode::RefreshMetadata;
    };

    ProjectRegistry::ProjectRegistry(std::filesystem::path path, const ProjectRegistryLoadMode loadMode)
        : m_Impl(std::make_unique<Impl>(path.empty() ? DefaultRegistryPath() : std::move(path), loadMode))
    {
    }

    ProjectRegistry::~ProjectRegistry() = default;

    std::vector<RecentProject> ProjectRegistry::Entries() const { return m_Impl->Projects; }

    void ProjectRegistry::RecordOpened(const Project& project, const std::string_view preferredEditorInstallation)
    {
        const auto id = project.Descriptor().Id;
        auto found = std::ranges::find(m_Impl->Projects, id, &RecentProject::Id);
        if (found == m_Impl->Projects.end())
        {
            RecentProject entry;
            entry.Id = id;
            entry.Root = project.Root();
            entry.Name = project.Descriptor().Name;
            entry.LastOpenedUnixSeconds = NowUnixSeconds();
            entry.AddedUnixSeconds = entry.LastOpenedUnixSeconds;
            entry.CreatedUnixSeconds = entry.LastOpenedUnixSeconds;
            entry.CreatedWithEngineVersion = project.Descriptor().CreatedWithEngineVersion;
            entry.LastSavedWithEngineVersion = project.Descriptor().LastSavedWithEngineVersion;
            entry.MinimumEngineVersion = project.Descriptor().MinimumEngineVersion;
            entry.ProjectSchemaVersion = project.Descriptor().SchemaVersion;
            entry.PreferredEditorInstallation = preferredEditorInstallation;
            entry.Status = ProjectStatus::Ready;
            m_Impl->Projects.push_back(std::move(entry));
        }
        else
        {
            found->Root = project.Root();
            found->Name = project.Descriptor().Name;
            found->LastOpenedUnixSeconds = NowUnixSeconds();
            found->LastSavedWithEngineVersion = project.Descriptor().LastSavedWithEngineVersion;
            if (!preferredEditorInstallation.empty())
                found->PreferredEditorInstallation = preferredEditorInstallation;
            found->Status = ProjectStatus::Ready;
        }
        m_Impl->RefreshEntries();
        m_Impl->Trim();
        m_Impl->Save();
    }

    bool ProjectRegistry::SetPinned(const ProjectId id, const bool pinned)
    {
        const auto found = std::ranges::find(m_Impl->Projects, id, &RecentProject::Id);
        if (found == m_Impl->Projects.end())
            return false;
        found->Pinned = pinned;
        m_Impl->Save();
        return true;
    }

    bool ProjectRegistry::Remove(const ProjectId id)
    {
        const auto removed = std::erase_if(m_Impl->Projects, [id](const auto& entry) { return entry.Id == id; });
        if (removed == 0)
            return false;
        m_Impl->Save();
        return true;
    }

    void ProjectRegistry::Refresh()
    {
        m_Impl->RefreshEntries();
        m_Impl->Save();
    }

    const std::filesystem::path& ProjectRegistry::Path() const noexcept { return m_Impl->RegistryPath; }
} // namespace Keire
