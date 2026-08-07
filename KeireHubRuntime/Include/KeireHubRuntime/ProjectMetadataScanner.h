#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/HubProjectCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class ProjectMetadataScanState
    {
        Completed,
        Cancelled,
        LimitReached
    };

    enum class ProjectMetadataItemState
    {
        Ready,
        Missing,
        Invalid,
        LimitExceeded
    };

    enum class ProjectMetadataScanPhase
    {
        Validating,
        Descriptor,
        Files,
        Completed,
        Cancelled,
        LimitReached
    };

    struct ProjectMetadataScanCandidate final
    {
        std::string ProjectId;
        std::filesystem::path Root;

        friend bool operator==(const ProjectMetadataScanCandidate&, const ProjectMetadataScanCandidate&) = default;
    };

    struct ProjectMetadataScanLimits final
    {
        std::size_t MaximumCandidates = 256;
        std::size_t MaximumDepth = 16;
        std::size_t MaximumEntries = 100'000;
        std::uint64_t MaximumBytes = 256ULL * 1024ULL * 1024ULL * 1024ULL;
        std::uint64_t MaximumThumbnailBytes = 8ULL * 1024ULL * 1024ULL;
    };

    struct ProjectMetadataScanRequest final
    {
        std::vector<ProjectMetadataScanCandidate> Projects;
        ProjectMetadataScanLimits Limits;
    };

    struct ProjectThumbnailMetadata final
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::uint64_t ModifiedUnixSeconds = 0;

        friend bool operator==(const ProjectThumbnailMetadata&, const ProjectThumbnailMetadata&) = default;
    };

    struct ProjectThumbnailImage final
    {
        static constexpr std::uint32_t PixelWidth = 256;
        static constexpr std::uint32_t PixelHeight = 144;

        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::shared_ptr<const std::vector<std::byte>> RgbaPixels;

        [[nodiscard]] bool IsValid() const noexcept;
    };

    // Decodes a confined PNG into the Hub's fixed 16:9 artwork surface. Callers must bound and validate the source
    // path before invoking this function; decoding failures are returned as details without throwing.
    [[nodiscard]] std::optional<ProjectThumbnailImage>
    DecodeHubPngImage(const std::filesystem::path& path, std::uint64_t encodedSize, std::string& details) noexcept;

    struct ProjectThumbnail final
    {
        std::string ProjectId;
        ProjectThumbnailMetadata Metadata;
        ProjectThumbnailImage Image;
    };

    struct ProjectMetadataScanResult final
    {
        std::string ProjectId;
        std::filesystem::path Root;
        std::filesystem::path DescriptorPath;
        std::string DisplayName;
        std::optional<std::string> TemplateId;
        std::optional<std::string> TemplateVersion;
        HubProjectMetadata Metadata;
        std::optional<ProjectThumbnailMetadata> Thumbnail;
        std::optional<ProjectThumbnailImage> ThumbnailImage;
        std::optional<HubError> ThumbnailError;
        std::optional<HubError> Error;
        ProjectMetadataItemState State = ProjectMetadataItemState::Invalid;
        std::size_t EntriesVisited = 0;
    };

    struct ProjectMetadataScanSnapshot final
    {
        ProjectMetadataScanState State = ProjectMetadataScanState::Completed;
        std::size_t CandidatesCompleted = 0;
        std::size_t TotalCandidates = 0;
        std::size_t EntriesVisited = 0;
        std::uint64_t BytesVisited = 0;
        std::vector<ProjectMetadataScanResult> Results;
    };

    struct ProjectMetadataScanProgress final
    {
        ProjectMetadataScanPhase Phase = ProjectMetadataScanPhase::Validating;
        std::size_t CandidatesCompleted = 0;
        std::size_t TotalCandidates = 0;
        std::size_t EntriesVisited = 0;
        std::uint64_t BytesVisited = 0;
        std::string CurrentProjectId;
        std::filesystem::path CurrentPath;
    };

    struct ProjectMetadataScanHooks final
    {
        std::function<bool()> IsCancelled;
        std::function<void(const ProjectMetadataScanProgress&)> ReportProgress;
    };

    // Each scanner owns one worker slot. Requests and hooks are copied into that worker, and progress callbacks execute
    // on its thread. Results never mutate a HubProjectCatalog and are safe to publish to the owner thread after Get().
    class ProjectMetadataScanner final
    {
      public:
        ProjectMetadataScanner();

        ProjectMetadataScanner(const ProjectMetadataScanner&) = delete;
        ProjectMetadataScanner& operator=(const ProjectMetadataScanner&) = delete;

        [[nodiscard]] std::future<HubResult<std::shared_ptr<const ProjectMetadataScanSnapshot>>>
        ScanAsync(ProjectMetadataScanRequest request, ProjectMetadataScanHooks hooks = {}) const;

      private:
        class WorkerState;
        std::shared_ptr<WorkerState> m_WorkerState;
    };

    // Owner-thread metadata cache only; no image bytes are loaded or retained.
    class ProjectThumbnailMetadataCache final
    {
      public:
        static constexpr std::size_t MaximumCapacity = 1024;

        explicit ProjectThumbnailMetadataCache(std::size_t capacity = 64);

        void Store(std::string projectId, ProjectThumbnailMetadata metadata);
        [[nodiscard]] std::optional<ProjectThumbnailMetadata> Find(std::string_view projectId);
        void Erase(std::string_view projectId) noexcept;
        void Clear() noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t Capacity() const noexcept;

      private:
        struct Entry final
        {
            std::string ProjectId;
            ProjectThumbnailMetadata Metadata;
        };

        std::vector<Entry> m_Entries;
        std::size_t m_Capacity = 0;
    };

    // Owner-thread LRU of normalized immutable pixels. The fixed image dimensions make the capacity a strict memory
    // bound, while shared immutable pixel buffers keep published snapshots cheap and worker-to-owner transfer safe.
    class ProjectThumbnailCache final
    {
      public:
        static constexpr std::size_t MaximumCapacity = 1024;

        explicit ProjectThumbnailCache(std::size_t capacity = 64);

        void Store(ProjectThumbnail thumbnail);
        [[nodiscard]] std::optional<ProjectThumbnail> Find(std::string_view projectId);
        void Erase(std::string_view projectId) noexcept;
        void Clear() noexcept;
        [[nodiscard]] std::shared_ptr<const std::vector<ProjectThumbnail>> Snapshot() const;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t Capacity() const noexcept;

      private:
        std::vector<ProjectThumbnail> m_Entries;
        std::size_t m_Capacity = 0;
    };
} // namespace KeireHub
