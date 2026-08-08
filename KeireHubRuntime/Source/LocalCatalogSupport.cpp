#include <KeireHubRuntimeInternal/LocalCatalogSupport.h>

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumDirectoryEntries = 4096;

        [[nodiscard]] char FoldAscii(const unsigned char value) noexcept
        {
            return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
        }

        [[nodiscard]] bool EqualCaseInsensitive(const std::string_view left, const std::string_view right) noexcept
        {
            return left.size() == right.size() &&
                   std::ranges::equal(left, right, [](const unsigned char first, const unsigned char second)
                                      { return FoldAscii(first) == FoldAscii(second); });
        }

        [[nodiscard]] std::optional<std::filesystem::path> ResolveComponent(const std::filesystem::path& parent,
                                                                            const std::filesystem::path& component)
        {
            std::error_code error;
            auto exact = parent / component;
            const auto exactStatus = std::filesystem::symlink_status(exact, error);
            if (!error && exactStatus.type() != std::filesystem::file_type::not_found)
                return exact;

            error.clear();
            const auto expected = PathToUtf8(component);
            std::optional<std::filesystem::path> match;
            std::size_t visited = 0;
            for (std::filesystem::directory_iterator iterator(parent, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (++visited > MaximumDirectoryEntries)
                    return std::nullopt;
                if (!EqualCaseInsensitive(PathToUtf8(iterator->path().filename()), expected))
                    continue;
                if (match)
                    return std::nullopt;
                match = iterator->path();
            }
            return error ? std::nullopt : match;
        }
    } // namespace

    std::optional<std::filesystem::path> ResolveConfinedRegularFile(const std::filesystem::path& root,
                                                                    const std::filesystem::path& relative)
    {
        if (!IsSafeRelativePath(relative) || PathToUtf8(relative).size() > 4096)
            return std::nullopt;

        std::error_code error;
        auto current = std::filesystem::weakly_canonical(root, error);
        if (error || !std::filesystem::is_directory(current, error) || error)
            return std::nullopt;
        const auto canonicalRoot = current;

        for (const auto& component : relative)
        {
            auto resolved = ResolveComponent(current, component);
            if (!resolved)
                return std::nullopt;
            const auto status = std::filesystem::symlink_status(*resolved, error);
            if (error || status.type() == std::filesystem::file_type::symlink)
                return std::nullopt;
            current = std::move(*resolved);
        }

        if (!std::filesystem::is_regular_file(current, error) || error)
            return std::nullopt;
        auto canonical = std::filesystem::weakly_canonical(current, error);
        if (error || !IsSafeRelativePath(canonical.lexically_relative(canonicalRoot)))
            return std::nullopt;
        return canonical;
    }

    std::optional<std::string> ReadBoundedCatalogText(const std::filesystem::path& path, const std::size_t maximumBytes)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > maximumBytes)
            return std::nullopt;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return std::nullopt;
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!text.empty())
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        return !stream && !text.empty() ? std::nullopt : std::optional<std::string>(std::move(text));
    }

    bool ContainsCaseInsensitive(const std::string_view text, const std::string_view query) noexcept
    {
        if (query.empty())
            return true;
        return std::ranges::search(text, query, [](const unsigned char left, const unsigned char right)
                                   { return FoldAscii(left) == FoldAscii(right); })
                   .begin() != text.end();
    }
} // namespace KeireHub::Detail
