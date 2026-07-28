#include "Keire/Project/Project.h"
#include "Keire/Project/ProjectAuthoringSettings.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/BuildInfo.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "KeireInternal/FileSystem.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ranges>
#include <span>
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
        constexpr std::size_t MaximumProjectFileBytes = 1024U * 1024U;
        constexpr std::size_t MaximumRegistryBytes = 4U * 1024U * 1024U;
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
            if (descriptor.SchemaVersion != 1 || !descriptor.Id)
                throw std::runtime_error("Project descriptor uses an unsupported schema or has no identity.");
            ValidateName(descriptor.Name);
            (void)ParseVersion(descriptor.CreatedWithEngineVersion);
            (void)ParseVersion(descriptor.MinimumEngineVersion);
            return descriptor;
        }

        void WriteDescriptor(const std::filesystem::path& root, const ProjectDescriptor& descriptor)
        {
            if (descriptor.SchemaVersion != 1 || !descriptor.Id)
                throw std::invalid_argument("Project descriptor uses an unsupported schema or has no identity.");
            ValidateName(descriptor.Name);
            (void)ParseVersion(descriptor.CreatedWithEngineVersion);
            (void)ParseVersion(descriptor.MinimumEngineVersion);
            Json document{{"schemaVersion", descriptor.SchemaVersion},
                          {"id", descriptor.Id.ToString()},
                          {"name", descriptor.Name},
                          {"createdWithEngineVersion", descriptor.CreatedWithEngineVersion},
                          {"minimumEngineVersion", descriptor.MinimumEngineVersion}};
            document["startupScene"] =
                descriptor.StartupScene ? Json(descriptor.StartupScene.ToString()) : Json(nullptr);
            document["defaultInput"] =
                descriptor.DefaultInput ? Json(descriptor.DefaultInput.ToString()) : Json(nullptr);
            Detail::WriteTextFileAtomically(MarkerPath(root), document.dump(2) + '\n');
        }

        [[nodiscard]] bool RequiresNewerEngine(const ProjectDescriptor& descriptor)
        {
            return ParseVersion(descriptor.MinimumEngineVersion) > ParseVersion(GetBuildInfo().Version);
        }

        [[nodiscard]] std::filesystem::path DefaultRegistryPath()
        {
            const auto name = std::string(GetBuildInfo().ProjectName);
            char* preference = SDL_GetPrefPath(name.c_str(), name.c_str());
            if (!preference)
                throw std::runtime_error("Cannot resolve the per-user project registry directory.");
            std::filesystem::path result = std::filesystem::path(preference) / "Hub" / "projects.json";
            SDL_free(preference);
            return result;
        }

        [[nodiscard]] std::uint64_t NowUnixSeconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
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

            ProjectDescriptor descriptor;
            descriptor.Id = ProjectId::Generate();
            descriptor.Name = specification.Name;
            descriptor.CreatedWithEngineVersion = std::string(GetBuildInfo().Version);
            descriptor.MinimumEngineVersion = descriptor.CreatedWithEngineVersion;
            if (specification.Template == ProjectTemplate::Starter)
            {
                AssetDatabaseSpecification databaseSpecification{.ProjectRoot = root};
                databaseSpecification.Importers = CreateBuiltinAssetImporters();
                auto database = CreateRef<AssetDatabase>(std::move(databaseSpecification));
                const auto inputBytes = InputActionAsset::Encode(InputActionAsset::DefaultDefinition());
                descriptor.DefaultInput = database->CreateAsset("Input/DefaultInput.keireinput",
                                                                CreateInputActionAssetImporter(), inputBytes);

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
                const auto sceneBytes = SceneAsset::Encode(SceneAsset::SampleDefinition(material));
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
        if (RequiresNewerEngine(descriptor))
            throw std::runtime_error("Project requires a newer Kéire engine version.");
        return CreateRef<Project>(std::make_unique<Impl>(root, std::move(descriptor), mode));
    }

    ProjectStatus Project::Inspect(const std::filesystem::path& path) noexcept
    {
        try
        {
            if (!std::filesystem::exists(path))
                return ProjectStatus::Missing;
            const auto descriptor = ParseDescriptor(ResolveRoot(path));
            if (RequiresNewerEngine(descriptor))
                return ProjectStatus::RequiresNewerEngine;
            return ProjectStatus::Ready;
        }
        catch (...)
        {
            return ProjectStatus::Invalid;
        }
    }

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
        WriteDescriptor(m_Impl->RootPath, descriptor);
        m_Impl->Description = std::move(descriptor);
    }

    class ProjectRegistry::Impl final
    {
      public:
        explicit Impl(std::filesystem::path value) : RegistryPath(std::move(value)) { Load(); }

        void Load()
        {
            if (!std::filesystem::exists(RegistryPath))
                return;
            try
            {
                const auto document = Json::parse(Detail::ReadTextFile(RegistryPath, MaximumRegistryBytes));
                if (document.at("schemaVersion").get<std::uint32_t>() != 1 || !document.at("projects").is_array())
                    throw std::runtime_error("Unsupported project registry schema.");
                for (const auto& value : document.at("projects"))
                {
                    RecentProject entry;
                    entry.Id = ProjectId::Parse(value.at("id").get<std::string>());
                    entry.Root = Detail::PathFromUtf8(value.at("root").get<std::string>());
                    entry.Name = value.at("name").get<std::string>();
                    entry.LastOpenedUnixSeconds = value.value("lastOpened", 0ULL);
                    entry.Pinned = value.value("pinned", false);
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
            RefreshEntries();
        }

        void Save() const
        {
            Json projects = Json::array();
            for (const auto& entry : Projects)
                projects.push_back({{"id", entry.Id.ToString()},
                                    {"root", Detail::PathToUtf8(entry.Root)},
                                    {"name", entry.Name},
                                    {"lastOpened", entry.LastOpenedUnixSeconds},
                                    {"pinned", entry.Pinned}});
            Detail::WriteTextFileAtomically(
                RegistryPath, Json{{"schemaVersion", 1}, {"projects", std::move(projects)}}.dump(2) + '\n');
        }

        void RefreshEntries()
        {
            for (auto& entry : Projects)
            {
                entry.Status = Project::Inspect(entry.Root);
                if (entry.Status == ProjectStatus::Ready && Project::IsLocked(entry.Root))
                    entry.Status = ProjectStatus::InUse;
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
    };

    ProjectRegistry::ProjectRegistry(std::filesystem::path path)
        : m_Impl(std::make_unique<Impl>(path.empty() ? DefaultRegistryPath() : std::move(path)))
    {
    }

    ProjectRegistry::~ProjectRegistry() = default;

    std::vector<RecentProject> ProjectRegistry::Entries() const { return m_Impl->Projects; }

    void ProjectRegistry::RecordOpened(const Project& project)
    {
        const auto id = project.Descriptor().Id;
        auto found = std::ranges::find(m_Impl->Projects, id, &RecentProject::Id);
        if (found == m_Impl->Projects.end())
        {
            m_Impl->Projects.push_back(
                {id, project.Root(), project.Descriptor().Name, NowUnixSeconds(), ProjectStatus::Ready, false});
        }
        else
        {
            found->Root = project.Root();
            found->Name = project.Descriptor().Name;
            found->LastOpenedUnixSeconds = NowUnixSeconds();
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
        m_Impl->RefreshEntries();
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
