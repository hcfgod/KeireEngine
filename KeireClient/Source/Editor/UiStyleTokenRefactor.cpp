#include "KeireClient/Editor/UiStyleTokenRefactor.h"

#include "Keire/Ui/UiToolkit.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool TokenBoundary(const std::string_view source, const std::size_t begin,
                                         const std::size_t length) noexcept
        {
            const auto identifier = [](const char value) noexcept
            { return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '_'; };
            return (begin == 0U || !identifier(source[begin - 1U])) &&
                   (begin + length == source.size() || !identifier(source[begin + length]));
        }

        [[nodiscard]] UiStyleTokenRefactorOccurrence Occurrence(const std::string_view source, const std::size_t offset)
        {
            const auto lineBegin = offset == 0U ? std::string_view::npos : source.rfind('\n', offset - 1U);
            const auto lineEnd = source.find('\n', offset);
            const auto contentBegin = lineBegin == std::string_view::npos ? 0U : lineBegin + 1U;
            const auto contentEnd = lineEnd == std::string_view::npos ? source.size() : lineEnd;
            return {.Line = static_cast<std::size_t>(std::count(source.begin(), source.begin() + offset, '\n')) + 1U,
                    .Column = offset - contentBegin + 1U,
                    .Preview = std::string(source.substr(contentBegin, contentEnd - contentBegin))};
        }

        void ValidateCandidate(const Keire::AssetTypeId type, const std::string_view source)
        {
            const auto bytes = std::as_bytes(std::span(source));
            if (type == Keire::UiStyleSheetAsset::StaticType())
            {
                const auto definition = Keire::UiStyleSheetAsset::ParseSource(bytes);
                Keire::UiStyleSheetAsset::Validate(definition);
                return;
            }
            if (type == Keire::UiVisualTreeAsset::StaticType())
            {
                const auto definition = Keire::UiVisualTreeAsset::ParseSource(bytes);
                Keire::UiVisualTreeAsset::Validate(definition);
                return;
            }
            throw std::invalid_argument("UI token refactors accept only UI documents and style sheets.");
        }
    } // namespace

    UiStyleTokenRefactorPreview
    BuildUiStyleTokenRefactorPreview(const std::filesystem::path& sourceRoot,
                                     const std::span<const UiStyleTokenRefactorInput> inputs,
                                     const std::string_view currentName, const std::string_view replacementName)
    {
        if (!currentName.starts_with("--") || currentName.size() < 3U || !replacementName.starts_with("--") ||
            replacementName.size() < 3U || currentName == replacementName)
        {
            throw std::invalid_argument("Project token refactors require two different '--name' identifiers.");
        }
        const auto root = Keire::Detail::CanonicalExistingPath(sourceRoot);
        UiStyleTokenRefactorPreview result{.CurrentName = std::string(currentName),
                                           .ReplacementName = std::string(replacementName)};
        for (const auto& input : inputs)
        {
            if (input.Type != Keire::UiStyleSheetAsset::StaticType() &&
                input.Type != Keire::UiVisualTreeAsset::StaticType())
            {
                continue;
            }
            const auto path = Keire::Detail::ResolveConfinedPath(root, input.RelativePath);
            const auto status = std::filesystem::symlink_status(path);
            if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                throw std::runtime_error("UI token refactor rejected a missing or linked source: " + path.string());
            auto baseline = Keire::Detail::ReadTextFile(path, Keire::MaximumUiDocumentBytes);
            auto candidate = baseline;
            std::vector<UiStyleTokenRefactorOccurrence> occurrences;
            std::size_t cursor = 0;
            while ((cursor = baseline.find(currentName, cursor)) != std::string::npos)
            {
                if (!TokenBoundary(baseline, cursor, currentName.size()))
                {
                    cursor += currentName.size();
                    continue;
                }
                occurrences.push_back(Occurrence(baseline, cursor));
                cursor += currentName.size();
            }
            if (occurrences.empty())
                continue;
            cursor = 0;
            while ((cursor = candidate.find(currentName, cursor)) != std::string::npos)
            {
                if (!TokenBoundary(candidate, cursor, currentName.size()))
                {
                    cursor += currentName.size();
                    continue;
                }
                candidate.replace(cursor, currentName.size(), replacementName);
                cursor += replacementName.size();
            }
            ValidateCandidate(input.Type, candidate);
            result.OccurrenceCount += occurrences.size();
            result.Changes.push_back({input.Asset, path, input.RelativePath, std::move(occurrences),
                                      std::move(baseline), std::move(candidate)});
        }
        return result;
    }

    void ApplyUiStyleTokenRefactor(const UiStyleTokenRefactorPreview& preview)
    {
        if (preview.Changes.empty() || preview.OccurrenceCount == 0U)
            throw std::invalid_argument("The UI token refactor preview contains no changes.");
        for (const auto& change : preview.Changes)
        {
            const auto status = std::filesystem::symlink_status(change.Path);
            if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) ||
                Keire::Detail::ReadTextFile(change.Path, Keire::MaximumUiDocumentBytes) != change.BaselineSource)
            {
                throw std::runtime_error("UI token refactor source changed after preview: " + change.Path.string());
            }
        }

        std::size_t committed = 0;
        try
        {
            for (; committed < preview.Changes.size(); ++committed)
                Keire::Detail::WriteTextFileAtomically(preview.Changes[committed].Path,
                                                       preview.Changes[committed].CandidateSource);
        }
        catch (...)
        {
            const auto original = std::current_exception();
            while (committed > 0U)
            {
                --committed;
                try
                {
                    Keire::Detail::WriteTextFileAtomically(preview.Changes[committed].Path,
                                                           preview.Changes[committed].BaselineSource);
                }
                catch (...)
                {
                }
            }
            std::rethrow_exception(original);
        }
    }
} // namespace KeireEditor
