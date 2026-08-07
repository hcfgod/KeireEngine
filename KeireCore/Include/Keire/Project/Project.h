#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t CurrentProjectSchemaVersion = 3;

    enum class ProjectTemplate : std::uint8_t
    {
        Empty,
        Starter
    };

    enum class ProjectOpenMode : std::uint8_t
    {
        ReadOnly,
        Exclusive
    };

    enum class ProjectMode : std::uint8_t
    {
        Disabled,
        Editor
    };

    struct ProjectSystemSpecification
    {
        ProjectMode Mode = ProjectMode::Disabled;
        std::filesystem::path Root;
    };

    enum class ProjectStatus : std::uint8_t
    {
        Ready,
        UpgradeAvailable,
        RecoveryRequired,
        Missing,
        Invalid,
        RequiresNewerEngine,
        InUse,
        UnsupportedSchema
    };

    struct RequiredSourceModule
    {
        std::string Id;
        std::string VersionRange;

        [[nodiscard]] bool operator==(const RequiredSourceModule&) const = default;
    };

    class ProjectId final
    {
      public:
        constexpr ProjectId() noexcept = default;
        explicit constexpr ProjectId(const AssetId value) noexcept : m_Value(value) {}
        [[nodiscard]] static ProjectId Generate() { return ProjectId(AssetId::Generate()); }
        [[nodiscard]] static ProjectId Parse(const std::string_view value) { return ProjectId(AssetId::Parse(value)); }
        [[nodiscard]] std::string ToString() const { return m_Value.ToString(); }
        [[nodiscard]] constexpr AssetId Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const ProjectId&) const noexcept = default;

      private:
        AssetId m_Value;
    };

    struct ProjectDescriptor
    {
        std::uint32_t SchemaVersion = CurrentProjectSchemaVersion;
        ProjectId Id;
        std::string Name;
        std::string CreatedWithEngineVersion;
        std::string MinimumEngineVersion;
        struct TemplateProvenance
        {
            std::string Id;
            std::string Version;

            [[nodiscard]] bool operator==(const TemplateProvenance&) const = default;
        };
        AssetId StartupScene;
        AssetId DefaultInput;
        std::vector<RequiredSourceModule> RequiredModules;
        std::string CreatedAt;
        std::string LastSavedWithEngineVersion;
        std::optional<TemplateProvenance> Template;
    };

    struct ProjectCreateSpecification
    {
        std::filesystem::path Location;
        std::string Name;
        ProjectTemplate Template = ProjectTemplate::Starter;
    };

    struct RecentProject
    {
        ProjectId Id;
        std::filesystem::path Root;
        std::string Name;
        std::uint64_t LastOpenedUnixSeconds = 0;
        ProjectStatus Status = ProjectStatus::Ready;
        bool Pinned = false;
        std::uint64_t AddedUnixSeconds = 0;
        std::uint64_t CreatedUnixSeconds = 0;
        std::uint64_t ModifiedUnixSeconds = 0;
        std::optional<std::uint64_t> SizeBytes;
        std::string CreatedWithEngineVersion;
        std::string LastSavedWithEngineVersion;
        std::string MinimumEngineVersion;
        std::uint32_t ProjectSchemaVersion = 0;
        std::string PreferredEditorInstallation;
    };

    enum class ProjectRegistryLoadMode : std::uint8_t
    {
        RefreshMetadata,
        CachedMetadata
    };

    struct ProjectInspectionResult
    {
        std::filesystem::path Root;
        ProjectStatus Status = ProjectStatus::Invalid;
        std::uint32_t SchemaVersion = 0;
        ProjectId Id;
        std::string Name;
        std::string CreatedWithEngineVersion;
        std::string MinimumEngineVersion;
        std::string CreatedAt;
        std::string LastSavedWithEngineVersion;
        std::optional<ProjectDescriptor::TemplateProvenance> Template;
        std::string Diagnostic;

        [[nodiscard]] bool HasIdentity() const noexcept { return static_cast<bool>(Id) && !Name.empty(); }
    };

    class KEIRE_API Project final : public RefCounted
    {
      public:
        class Impl;
        ~Project() override;

        Project(const Project&) = delete;
        Project& operator=(const Project&) = delete;

        [[nodiscard]] static Ref<Project> Create(const ProjectCreateSpecification& specification);
        [[nodiscard]] static Ref<Project> Open(const std::filesystem::path& path,
                                               ProjectOpenMode mode = ProjectOpenMode::ReadOnly);
        [[nodiscard]] static ProjectStatus Inspect(const std::filesystem::path& path) noexcept;
        [[nodiscard]] static ProjectInspectionResult InspectMetadata(const std::filesystem::path& path) noexcept;
        [[nodiscard]] static bool IsLocked(const std::filesystem::path& path) noexcept;

        [[nodiscard]] const ProjectDescriptor& Descriptor() const noexcept;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;
        [[nodiscard]] std::filesystem::path AssetsDirectory() const;
        [[nodiscard]] std::filesystem::path AssetCatalog() const;
        [[nodiscard]] std::filesystem::path WorkspaceDirectory() const;
        [[nodiscard]] std::filesystem::path InputOverridesDirectory() const;
        [[nodiscard]] std::filesystem::path SceneRecoveryDirectory() const;
        [[nodiscard]] bool Writable() const noexcept;
        void Save(ProjectDescriptor descriptor);

      private:
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit Project(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API ProjectRegistry final : public RefCounted
    {
      public:
        explicit ProjectRegistry(std::filesystem::path path = {},
                                 ProjectRegistryLoadMode loadMode = ProjectRegistryLoadMode::RefreshMetadata);
        ~ProjectRegistry() override;

        ProjectRegistry(const ProjectRegistry&) = delete;
        ProjectRegistry& operator=(const ProjectRegistry&) = delete;

        [[nodiscard]] std::vector<RecentProject> Entries() const;
        void RecordOpened(const Project& project, std::string_view preferredEditorInstallation = {});
        [[nodiscard]] bool SetPinned(ProjectId id, bool pinned);
        [[nodiscard]] bool Remove(ProjectId id);
        void Refresh();
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire

template <> struct std::hash<Keire::ProjectId>
{
    std::size_t operator()(const Keire::ProjectId value) const noexcept
    {
        return std::hash<Keire::AssetId>{}(value.Value());
    }
};
