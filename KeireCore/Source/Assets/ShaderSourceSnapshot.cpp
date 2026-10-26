#include "KeireInternal/Assets/ShaderSourceSnapshot.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/RenderingAssetValidation.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] std::string_view TrimDirectiveSpace(const std::string_view text)
        {
            const auto first = text.find_first_not_of(" \t\r\v\f");
            if (first == std::string_view::npos)
            {
                return {};
            }
            const auto last = text.find_last_not_of(" \t\r\v\f");
            return text.substr(first, last - first + 1U);
        }

        [[nodiscard]] std::optional<std::filesystem::path> LiteralInclude(std::string_view line)
        {
            line = TrimDirectiveSpace(line);
            if (!line.starts_with('#'))
            {
                return std::nullopt;
            }
            line = TrimDirectiveSpace(line.substr(1U));
            constexpr std::string_view keyword = "include";
            if (!line.starts_with(keyword))
            {
                return std::nullopt;
            }
            line.remove_prefix(keyword.size());
            if (!line.empty() && (std::isalnum(static_cast<unsigned char>(line.front())) != 0 || line.front() == '_'))
            {
                return std::nullopt;
            }
            line = TrimDirectiveSpace(line);
            const char delimiter = line.starts_with('"') ? '"' : line.starts_with('<') ? '>' : '\0';
            const auto end = delimiter == '\0' ? std::string_view::npos : line.find(delimiter, 1U);
            constexpr std::size_t maximumHeaderNameBytes = 1024;
            if (end == std::string_view::npos || end <= 1U || end - 1U > maximumHeaderNameBytes ||
                !TrimDirectiveSpace(line.substr(end + 1U)).empty())
            {
                throw std::invalid_argument(
                    "Shader snapshots require bounded literal quoted or angle-bracket includes.");
            }
            const std::filesystem::path include = std::string(line.substr(1U, end - 1U));
            if (include.has_root_path() || include.generic_string().find(':') != std::string::npos ||
                include.generic_string().starts_with('\\'))
            {
                throw std::invalid_argument("Shader includes may not use absolute or drive-relative paths.");
            }
            return include;
        }

        [[nodiscard]] bool IsIncludePrefix(std::string_view text)
        {
            const auto trim = [&text]
            {
                const auto first = text.find_first_not_of(" \t\r");
                text.remove_prefix(first == std::string_view::npos ? text.size() : first);
            };
            trim();
            if (!text.starts_with('#'))
            {
                return false;
            }
            text.remove_prefix(1U);
            trim();
            if (!text.starts_with("include"))
            {
                return false;
            }
            text.remove_prefix(7U);
            trim();
            return text.empty();
        }

        [[nodiscard]] std::string DirectiveText(const std::span<const std::byte> bytes)
        {
            std::string result;
            result.reserve(bytes.size());
            // Preprocessor line splicing happens before comment removal, including CRLF source files.
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                const char character = static_cast<char>(bytes[index]);
                if (character == '\\' && index + 1U < bytes.size())
                {
                    if (bytes[index + 1U] == std::byte{'\n'})
                    {
                        ++index;
                        continue;
                    }
                    if (index + 2U < bytes.size() && bytes[index + 1U] == std::byte{'\r'} &&
                        bytes[index + 2U] == std::byte{'\n'})
                    {
                        index += 2U;
                        continue;
                    }
                }
                result.push_back(character);
            }

            bool blockComment = false;
            bool lineComment = false;
            char quote = '\0';
            bool escaped = false;
            std::size_t lineStart = 0;
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                const char character = result[index];
                const char next = index + 1U < result.size() ? result[index + 1U] : '\0';
                if (character == '\n')
                {
                    lineStart = index + 1U;
                }
                if (blockComment)
                {
                    if (character == '*' && next == '/')
                    {
                        result[index] = ' ';
                        result[++index] = ' ';
                        blockComment = false;
                    }
                    else if (character != '\n' && character != '\r')
                    {
                        result[index] = ' ';
                    }
                }
                else if (lineComment)
                {
                    if (character == '\n')
                    {
                        lineComment = false;
                    }
                    else
                    {
                        result[index] = ' ';
                    }
                }
                else if (quote != '\0')
                {
                    if (!escaped && character == quote)
                    {
                        quote = '\0';
                    }
                    escaped = quote != '>' && !escaped && character == '\\';
                }
                else if (character == '"' || character == '\'')
                {
                    quote = character;
                    escaped = false;
                }
                else if (character == '<' &&
                         IsIncludePrefix(std::string_view(result).substr(lineStart, index - lineStart)))
                {
                    quote = '>';
                    escaped = false;
                }
                else if (character == '/' && (next == '*' || next == '/'))
                {
                    blockComment = next == '*';
                    lineComment = next == '/';
                    result[index] = ' ';
                    result[++index] = ' ';
                }
            }
            return result;
        }

        class SnapshotCapture final
        {
          public:
            SnapshotCapture(const AssetImportContext& context, const std::span<const std::filesystem::path> roots)
                : m_Context(context)
            {
                if (!context.ReadProjectFile || context.MaximumDependencyBytes == 0)
                {
                    throw std::invalid_argument("Shader snapshots require a bounded project-file reader.");
                }
                if (roots.size() > MaximumShaderIncludeRoots)
                {
                    throw std::invalid_argument("Shader include roots exceed their limit.");
                }
                for (const auto& root : roots)
                {
                    m_Roots.push_back(ConfinedShaderSourcePath(root, true));
                }
            }

            [[nodiscard]] std::vector<ShaderSourceFile> Capture(const std::filesystem::path& source)
            {
                const auto normalized = ConfinedShaderSourcePath(source);
                Discover(Store(normalized, m_Context.ReadProjectFile(normalized)));
                return std::move(m_Files);
            }

          private:
            [[nodiscard]] std::size_t Store(const std::filesystem::path& path, std::vector<std::byte> bytes)
            {
                if (m_Files.size() >= MaximumShaderDependencies)
                {
                    throw std::invalid_argument("Shader include graph exceeds its dependency limit.");
                }
                if (bytes.size() > m_Context.MaximumDependencyBytes - m_TotalBytes)
                {
                    throw std::invalid_argument("Shader source snapshot exceeds its aggregate byte limit.");
                }
                m_TotalBytes += bytes.size();
                const auto index = m_Files.size();
                const auto digest = DigestToString(Sha256(bytes));
                m_Files.push_back({{path, digest}, std::move(bytes)});
                m_Indices.emplace(path, index);
                return index;
            }

            [[nodiscard]] std::optional<std::size_t> TryRead(const std::filesystem::path& candidate)
            {
                std::filesystem::path path;
                try
                {
                    path = ConfinedShaderSourcePath(candidate);
                }
                catch (const std::invalid_argument&)
                {
                    return std::nullopt;
                }
                if (const auto found = m_Indices.find(path); found != m_Indices.end())
                {
                    return found->second;
                }
                if (m_Missing.contains(path))
                {
                    return std::nullopt;
                }
                std::vector<std::byte> bytes;
                try
                {
                    bytes = m_Context.ReadProjectFile(path);
                }
                catch (const std::bad_alloc&)
                {
                    throw;
                }
                catch (const std::exception&)
                {
                    m_Missing.insert(path);
                    return std::nullopt;
                }
                return Store(path, std::move(bytes));
            }

            void Discover(const std::size_t index)
            {
                const auto path = m_Files[index].Dependency.RelativePath;
                if (m_Visiting.contains(path))
                {
                    throw std::invalid_argument("Shader include graph contains a cycle at " + path.generic_string());
                }
                if (!m_Visited.insert(path).second)
                {
                    return;
                }
                m_Visiting.insert(path);
                const auto text = DirectiveText(m_Files[index].Bytes);
                std::string_view remaining = text;
                while (!remaining.empty())
                {
                    const auto end = remaining.find('\n');
                    const auto line = remaining.substr(0U, end);
                    remaining.remove_prefix(end == std::string_view::npos ? remaining.size() : end + 1U);
                    const auto header = LiteralInclude(line);
                    if (!header)
                    {
                        continue;
                    }
                    const auto& include = *header;
                    auto resolved = TryRead(path.parent_path() / include);
                    for (const auto& root : m_Roots)
                    {
                        if (resolved)
                        {
                            break;
                        }
                        const auto includeText = include.lexically_normal().generic_string();
                        const auto rootText = root.generic_string();
                        if (rootText == "." || includeText == rootText || includeText.starts_with(rootText + '/'))
                        {
                            resolved = TryRead(include);
                        }
                        if (!resolved)
                        {
                            resolved = TryRead(root / include);
                        }
                    }
                    if (!resolved)
                    {
                        throw std::invalid_argument("Shader include could not be resolved: " +
                                                    include.generic_string());
                    }
                    Discover(*resolved);
                }
                m_Visiting.erase(path);
            }

            const AssetImportContext& m_Context;
            std::vector<std::filesystem::path> m_Roots;
            std::vector<ShaderSourceFile> m_Files;
            std::map<std::filesystem::path, std::size_t> m_Indices;
            std::set<std::filesystem::path> m_Missing;
            std::set<std::filesystem::path> m_Visiting;
            std::set<std::filesystem::path> m_Visited;
            std::size_t m_TotalBytes = 0;
        };
    } // namespace

    std::filesystem::path ConfinedShaderSourcePath(const std::filesystem::path& path, const bool allowProjectRoot)
    {
        const auto normalized = path.lexically_normal();
        const auto text = normalized.generic_string();
        if (path.empty() || normalized.has_root_path() || text.find(':') != std::string::npos ||
            text.starts_with('\\') || (!allowProjectRoot && normalized == ".") ||
            std::ranges::any_of(normalized, [](const auto& component) { return component == ".."; }))
        {
            throw std::invalid_argument("Shader source and include roots must remain project-relative paths.");
        }
        return normalized;
    }

    std::vector<ShaderSourceFile>
    CaptureShaderSourceSnapshot(const AssetImportContext& context, const std::filesystem::path& source,
                                const std::span<const std::filesystem::path> projectRelativeIncludeRoots)
    {
        return SnapshotCapture(context, projectRelativeIncludeRoots).Capture(source);
    }
} // namespace Keire::Detail
