#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Keire::Internal
{
    inline constexpr std::size_t DiagnosticBundleMaximumLogFileBytes = 2U * 1024U * 1024U;
    inline constexpr std::size_t DiagnosticBundleMaximumTotalLogBytes = 8U * 1024U * 1024U;
    inline constexpr std::size_t DiagnosticBundleMaximumTextEntryBytes = 16U * 1024U * 1024U;
    inline constexpr std::size_t DiagnosticBundleMaximumArchiveBytes = 32U * 1024U * 1024U;

    enum class DiagnosticBundleSection : std::uint8_t
    {
        System,
        Renderer,
        Project,
        Packages,
        Failures,
        Logs,
        Crash
    };

    struct DiagnosticBundleSelection final
    {
        bool IncludeLogs = true;
        bool IncludeProjectMetadata = true;
        bool IncludePackageVersions = true;
        bool IncludeCrashInformation = true;
    };

    struct DiagnosticBundleTextSource final
    {
        DiagnosticBundleSection Section = DiagnosticBundleSection::System;
        std::string ArchivePath;
        std::string Contents;
    };

    struct DiagnosticBundleLogSource final
    {
        std::string ArchivePath;
        std::filesystem::path TrustedRoot;
        std::filesystem::path RelativePath;
    };

    struct DiagnosticBundleRequest final
    {
        DiagnosticBundleSelection Selection;
        std::vector<DiagnosticBundleTextSource> TextSources;
        std::vector<DiagnosticBundleLogSource> LogSources;
        std::function<bool()> IsCancelled;
    };

    struct DiagnosticBundlePreviewEntry final
    {
        DiagnosticBundleSection Section = DiagnosticBundleSection::System;
        std::string ArchivePath;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
        std::uint64_t Redactions = 0;
    };

    struct DiagnosticBundleOmission final
    {
        std::string ArchivePath;
        std::string Reason;
    };

    class FrozenDiagnosticBundle final
    {
      public:
        FrozenDiagnosticBundle() = default;
        FrozenDiagnosticBundle(std::vector<DiagnosticBundlePreviewEntry> preview,
                               std::vector<DiagnosticBundleOmission> omissions, std::vector<std::byte> archive);

        [[nodiscard]] const std::vector<DiagnosticBundlePreviewEntry>& Preview() const noexcept;
        [[nodiscard]] const std::vector<DiagnosticBundleOmission>& Omissions() const noexcept;
        [[nodiscard]] std::span<const std::byte> ArchiveBytes() const noexcept;
        void Save(const std::filesystem::path& output) const;

      private:
        std::vector<DiagnosticBundlePreviewEntry> m_Preview;
        std::vector<DiagnosticBundleOmission> m_Omissions;
        std::vector<std::byte> m_Archive;
    };

    [[nodiscard]] const char* DiagnosticBundleSectionName(DiagnosticBundleSection section) noexcept;
    [[nodiscard]] FrozenDiagnosticBundle BuildDiagnosticBundle(const DiagnosticBundleRequest& request);
} // namespace Keire::Internal
