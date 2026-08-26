#include "KeireInternal/Diagnostics/DiagnosticBundleInternal.h"

#include "DiagnosticBundleSupport.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace Keire::Internal
{
    namespace
    {
        using Json = nlohmann::json;
        using DiagnosticBundleDetail::ArchiveEntry;

        struct FrozenEntry final
        {
            DiagnosticBundleSection Section = DiagnosticBundleSection::System;
            std::string Path;
            std::vector<std::byte> Contents;
            std::uint64_t Redactions = 0;
        };

        [[nodiscard]] bool Selected(const DiagnosticBundleSelection& selection,
                                    const DiagnosticBundleSection section) noexcept
        {
            switch (section)
            {
            case DiagnosticBundleSection::Project:
                return selection.IncludeProjectMetadata;
            case DiagnosticBundleSection::Packages:
                return selection.IncludePackageVersions;
            case DiagnosticBundleSection::Logs:
                return selection.IncludeLogs;
            case DiagnosticBundleSection::Crash:
                return selection.IncludeCrashInformation;
            case DiagnosticBundleSection::System:
            case DiagnosticBundleSection::Renderer:
            case DiagnosticBundleSection::Failures:
                return true;
            }
            return false;
        }

        void CheckCancellation(const DiagnosticBundleRequest& request)
        {
            if (request.IsCancelled && request.IsCancelled())
                throw std::runtime_error("Diagnostic bundle collection was cancelled.");
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view value)
        {
            const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
            return {bytes.begin(), bytes.end()};
        }

        void RetainUtf8Tail(std::string& text, const std::size_t maximumBytes)
        {
            if (text.size() <= maximumBytes)
                return;
            auto first = text.size() - maximumBytes;
            while (first < text.size() && (static_cast<unsigned char>(text[first]) & 0xc0U) == 0x80U)
                ++first;
            text.erase(0, first);
        }

        [[nodiscard]] DiagnosticBundlePreviewEntry Preview(const FrozenEntry& entry)
        {
            return {.Section = entry.Section,
                    .ArchivePath = entry.Path,
                    .SizeBytes = static_cast<std::uint64_t>(entry.Contents.size()),
                    .Sha256 = Detail::DigestToString(Detail::Sha256(entry.Contents)),
                    .Redactions = entry.Redactions};
        }

        void AddPath(std::set<std::string, std::less<>>& paths, const std::string_view path)
        {
            if (!DiagnosticBundleDetail::IsPortableArchivePath(path) || path == "manifest.json" ||
                !paths.emplace(path).second)
                throw std::invalid_argument("A diagnostic bundle source path is unsafe, reserved, or duplicated.");
        }

        [[nodiscard]] Json Manifest(const std::vector<DiagnosticBundlePreviewEntry>& preview,
                                    const std::vector<DiagnosticBundleOmission>& omissions)
        {
            Json entries = Json::array();
            for (const auto& entry : preview)
            {
                entries.push_back({{"path", entry.ArchivePath},
                                   {"section", DiagnosticBundleSectionName(entry.Section)},
                                   {"sizeBytes", entry.SizeBytes},
                                   {"sha256", entry.Sha256},
                                   {"redactions", entry.Redactions}});
            }
            Json omitted = Json::array();
            for (const auto& omission : omissions)
                omitted.push_back({{"path", omission.ArchivePath}, {"reason", omission.Reason}});
            return {{"schemaVersion", 1},
                    {"localOnly", true},
                    {"automaticUpload", false},
                    {"nativeDumpsIncluded", false},
                    {"entries", std::move(entries)},
                    {"omissions", std::move(omitted)}};
        }
    } // namespace

    FrozenDiagnosticBundle::FrozenDiagnosticBundle(std::vector<DiagnosticBundlePreviewEntry> preview,
                                                   std::vector<DiagnosticBundleOmission> omissions,
                                                   std::vector<std::byte> archive)
        : m_Preview(std::move(preview)), m_Omissions(std::move(omissions)), m_Archive(std::move(archive))
    {
    }

    const std::vector<DiagnosticBundlePreviewEntry>& FrozenDiagnosticBundle::Preview() const noexcept
    {
        return m_Preview;
    }

    const std::vector<DiagnosticBundleOmission>& FrozenDiagnosticBundle::Omissions() const noexcept
    {
        return m_Omissions;
    }

    std::span<const std::byte> FrozenDiagnosticBundle::ArchiveBytes() const noexcept { return m_Archive; }

    void FrozenDiagnosticBundle::Save(const std::filesystem::path& output) const
    {
        if (m_Archive.empty())
            throw std::logic_error("An empty diagnostic bundle cannot be saved.");
        if (output.empty() || output.filename().empty())
            throw std::invalid_argument("A diagnostic bundle output filename is required.");
        const auto absolute = std::filesystem::absolute(output).lexically_normal();
        Detail::AnchoredFileSystem destination(absolute.parent_path(), Detail::AnchoredRootPolicy::RejectLink);
        destination.WriteFileAtomically(absolute.filename(), m_Archive);
    }

    const char* DiagnosticBundleSectionName(const DiagnosticBundleSection section) noexcept
    {
        switch (section)
        {
        case DiagnosticBundleSection::System:
            return "system";
        case DiagnosticBundleSection::Renderer:
            return "renderer";
        case DiagnosticBundleSection::Project:
            return "project";
        case DiagnosticBundleSection::Packages:
            return "packages";
        case DiagnosticBundleSection::Failures:
            return "failures";
        case DiagnosticBundleSection::Logs:
            return "logs";
        case DiagnosticBundleSection::Crash:
            return "crash";
        }
        return "unknown";
    }

    FrozenDiagnosticBundle BuildDiagnosticBundle(const DiagnosticBundleRequest& request)
    {
        std::vector<FrozenEntry> entries;
        std::vector<DiagnosticBundleOmission> omissions;
        std::set<std::string, std::less<>> paths;
        for (const auto& source : request.TextSources)
        {
            CheckCancellation(request);
            AddPath(paths, source.ArchivePath);
            if (!Selected(request.Selection, source.Section))
            {
                omissions.push_back({source.ArchivePath, "section deselected"});
                continue;
            }
            if (source.Contents.size() > DiagnosticBundleMaximumTextEntryBytes)
                throw std::runtime_error("A diagnostic bundle text source exceeds its maximum size.");
            auto sanitized = DiagnosticBundleDetail::SanitizeText(source.Contents);
            if (sanitized.Contents.size() > DiagnosticBundleMaximumTextEntryBytes)
                throw std::runtime_error("A sanitized diagnostic bundle text source exceeds its maximum size.");
            entries.push_back({.Section = source.Section,
                               .Path = source.ArchivePath,
                               .Contents = Bytes(sanitized.Contents),
                               .Redactions = sanitized.Redactions});
        }

        std::size_t retainedLogBytes = 0;
        for (const auto& source : request.LogSources)
        {
            CheckCancellation(request);
            AddPath(paths, source.ArchivePath);
            if (!request.Selection.IncludeLogs)
            {
                omissions.push_back({source.ArchivePath, "section deselected"});
                continue;
            }
            if (retainedLogBytes >= DiagnosticBundleMaximumTotalLogBytes)
            {
                omissions.push_back({source.ArchivePath, "total log limit reached"});
                continue;
            }
            try
            {
                Detail::AnchoredFileSystem logs(source.TrustedRoot, Detail::AnchoredRootPolicy::RejectLink);
                const auto available = DiagnosticBundleMaximumTotalLogBytes - retainedLogBytes;
                const auto maximum = (std::min)(DiagnosticBundleMaximumLogFileBytes, available);
                const auto bytes = logs.ReadTail(source.RelativePath, maximum);
                const auto text = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                auto sanitized = DiagnosticBundleDetail::SanitizeText(text);
                RetainUtf8Tail(sanitized.Contents, maximum);
                retainedLogBytes += sanitized.Contents.size();
                entries.push_back({.Section = DiagnosticBundleSection::Logs,
                                   .Path = source.ArchivePath,
                                   .Contents = Bytes(sanitized.Contents),
                                   .Redactions = sanitized.Redactions});
            }
            catch (const std::system_error&)
            {
                omissions.push_back({source.ArchivePath, "source unavailable"});
            }
        }
        CheckCancellation(request);

        std::ranges::sort(entries, {}, &FrozenEntry::Path);
        std::ranges::sort(omissions, {}, &DiagnosticBundleOmission::ArchivePath);
        std::vector<DiagnosticBundlePreviewEntry> preview;
        preview.reserve(entries.size() + 1U);
        for (const auto& entry : entries)
            preview.push_back(Preview(entry));

        auto manifestText = Manifest(preview, omissions).dump(2) + '\n';
        FrozenEntry manifest{
            .Section = DiagnosticBundleSection::System, .Path = "manifest.json", .Contents = Bytes(manifestText)};
        entries.push_back(manifest);
        preview.push_back(Preview(manifest));
        std::ranges::sort(entries, {}, &FrozenEntry::Path);
        std::ranges::sort(preview, {}, &DiagnosticBundlePreviewEntry::ArchivePath);

        std::vector<ArchiveEntry> archiveEntries;
        archiveEntries.reserve(entries.size());
        for (auto& entry : entries)
            archiveEntries.push_back({std::move(entry.Path), std::move(entry.Contents)});
        auto archive = DiagnosticBundleDetail::WriteZip(archiveEntries, DiagnosticBundleMaximumArchiveBytes);
        CheckCancellation(request);
        return {std::move(preview), std::move(omissions), std::move(archive)};
    }
} // namespace Keire::Internal
