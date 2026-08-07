#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    enum class HubFirstRunDiscoveryState
    {
        NotStarted,
        Completed,
        Cancelled
    };

    enum class HubFirstRunDiscoveryPhase
    {
        Validating,
        Projects,
        Editors,
        PackagedAncestry,
        Completed,
        Cancelled
    };

    struct HubFirstRunDiscoveryLimits final
    {
        std::size_t MaximumRoots = 16;
        std::size_t MaximumDepth = 4;
        std::size_t MaximumEntries = 20'000;
        std::size_t MaximumCandidates = 512;
        std::size_t MaximumAncestryLevels = 8;
    };

    struct HubFirstRunDiscoveryRequest final
    {
        std::vector<std::filesystem::path> ProjectRoots;
        std::vector<std::filesystem::path> EditorRoots;
        std::optional<std::filesystem::path> PackagedOrCombinedAncestry;
        std::string HostPlatform;
        std::string HostArchitecture;
        HubFirstRunDiscoveryLimits Limits;
    };

    struct HubFirstRunProjectCandidate final
    {
        std::string Id;
        std::string Name;
        std::filesystem::path Root;
        std::filesystem::path DescriptorPath;
        std::uint32_t SchemaVersion = 0;
        std::string CreatedWithEngineVersion;
        std::string LastSavedWithEngineVersion;
        std::string MinimumEngineVersion;

        friend bool operator==(const HubFirstRunProjectCandidate&, const HubFirstRunProjectCandidate&) = default;
    };

    struct HubFirstRunEditorCandidate final
    {
        std::filesystem::path Root;
        std::string Version;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::string ManifestFingerprint;
        std::vector<std::filesystem::path> Entrypoints;

        friend bool operator==(const HubFirstRunEditorCandidate&, const HubFirstRunEditorCandidate&) = default;
    };

    struct HubFirstRunDiscoverySnapshot final
    {
        HubFirstRunDiscoveryState State = HubFirstRunDiscoveryState::NotStarted;
        std::size_t EntriesVisited = 0;
        std::vector<HubFirstRunProjectCandidate> Projects;
        std::vector<HubFirstRunEditorCandidate> Editors;

        friend bool operator==(const HubFirstRunDiscoverySnapshot&, const HubFirstRunDiscoverySnapshot&) = default;
    };

    struct HubFirstRunDiscoveryProgress final
    {
        HubFirstRunDiscoveryPhase Phase = HubFirstRunDiscoveryPhase::Validating;
        std::size_t RootsCompleted = 0;
        std::size_t TotalRoots = 0;
        std::size_t EntriesVisited = 0;
        std::size_t ProjectsFound = 0;
        std::size_t EditorsFound = 0;
        std::filesystem::path CurrentPath;
    };

    struct HubFirstRunDiscoveryHooks final
    {
        std::function<bool()> IsCancelled;
        std::function<void(const HubFirstRunDiscoveryProgress&)> ReportProgress;
    };

    class HubFirstRunDiscovery final
    {
      public:
        HubFirstRunDiscovery();

        [[nodiscard]] HubStatus Discover(const HubFirstRunDiscoveryRequest& request,
                                         HubFirstRunDiscoveryHooks hooks = {});
        [[nodiscard]] std::shared_ptr<const HubFirstRunDiscoverySnapshot> Snapshot() const noexcept;

      private:
        std::shared_ptr<const HubFirstRunDiscoverySnapshot> m_Snapshot;
    };
} // namespace KeireHub
