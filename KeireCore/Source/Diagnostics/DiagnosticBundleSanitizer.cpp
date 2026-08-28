#include "KeireInternal/Diagnostics/DiagnosticBundleSupport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace Keire::Internal::DiagnosticBundleDetail
{
    namespace
    {
        [[nodiscard]] bool IsAsciiAlpha(const unsigned char character) noexcept
        {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        }

        [[nodiscard]] bool IsAsciiDigit(const unsigned char character) noexcept
        {
            return character >= '0' && character <= '9';
        }

        [[nodiscard]] bool IsAsciiAlphaNumeric(const unsigned char character) noexcept
        {
            return IsAsciiAlpha(character) || IsAsciiDigit(character);
        }

        [[nodiscard]] bool IsWordCharacter(const unsigned char character) noexcept
        {
            return IsAsciiAlphaNumeric(character) || character == '_';
        }

        [[nodiscard]] bool IsLabelCharacter(const unsigned char character) noexcept
        {
            return IsWordCharacter(character) || character == '-';
        }

        [[nodiscard]] char LowerAscii(const unsigned char character) noexcept
        {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                        : static_cast<char>(character);
        }

        [[nodiscard]] bool StartsWithInsensitive(const std::string_view text, const std::size_t offset,
                                                 const std::string_view expected) noexcept
        {
            if (offset > text.size() || expected.size() > text.size() - offset)
                return false;
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                if (LowerAscii(static_cast<unsigned char>(text[offset + index])) !=
                    LowerAscii(static_cast<unsigned char>(expected[index])))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::string NormalizeLabel(const std::string_view label, const bool removeSeparators)
        {
            std::string result;
            result.reserve(label.size());
            for (const auto value : label)
            {
                const auto character = static_cast<unsigned char>(value);
                if (character == '_' || character == '-')
                {
                    if (!removeSeparators)
                        result.push_back('_');
                    continue;
                }
                result.push_back(LowerAscii(character));
            }
            return result;
        }

        [[nodiscard]] bool IsCredentialLabel(const std::string_view label)
        {
            static constexpr std::array<std::string_view, 23> labels{
                "authorization", "password",   "passwd",    "secret",        "token",        "cookie",
                "session",       "session_id", "sessionid", "session_token", "sessiontoken", "csrf",
                "csrf_token",    "csrftoken",  "jwt",       "jwt_token",     "jwttoken",     "credential",
                "entitlement",   "api_key",    "apikey",    "access_key",    "accesskey"};
            static constexpr std::array<std::string_view, 8> compoundLabels{
                "auth_token", "authtoken", "client_secret", "clientsecret",
                "id_token",   "idtoken",   "refresh_token", "refreshtoken"};
            const auto normalized = NormalizeLabel(label, false);
            const auto matches = [&normalized](const std::string_view expected) noexcept
            {
                return normalized == expected ||
                       (normalized.size() > expected.size() && normalized.ends_with(expected) &&
                        normalized[normalized.size() - expected.size() - 1U] == '_');
            };
            return std::ranges::any_of(labels, matches) || std::ranges::any_of(compoundLabels, matches);
        }

        [[nodiscard]] bool IsPrivateMetadataLabel(const std::string_view label)
        {
            static constexpr std::array<std::string_view, 44> labels{"project",
                                                                     "projectname",
                                                                     "projectpath",
                                                                     "projectroot",
                                                                     "asset",
                                                                     "assetname",
                                                                     "assetpath",
                                                                     "document",
                                                                     "documentname",
                                                                     "documentpath",
                                                                     "scene",
                                                                     "scenename",
                                                                     "scenepath",
                                                                     "workspace",
                                                                     "workspacename",
                                                                     "workspacepath",
                                                                     "workspaceroot",
                                                                     "repository",
                                                                     "repositoryname",
                                                                     "repositorypath",
                                                                     "repositoryroot",
                                                                     "repo",
                                                                     "reponame",
                                                                     "repopath",
                                                                     "reporoot",
                                                                     "sourcepath",
                                                                     "sourceurl",
                                                                     "installpath",
                                                                     "installroot",
                                                                     "cachepath",
                                                                     "cacheroot",
                                                                     "logpath",
                                                                     "workingdirectory",
                                                                     "cwd",
                                                                     "pwd",
                                                                     "commandline",
                                                                     "arguments",
                                                                     "environment",
                                                                     "env",
                                                                     "home",
                                                                     "user",
                                                                     "username",
                                                                     "userprofile",
                                                                     "userdomain"};
            static constexpr std::array<std::string_view, 6> systemLabels{"computername", "hostname", "appdata",
                                                                          "localappdata", "temp",     "tmp"};
            const auto normalized = NormalizeLabel(label, true);
            return std::ranges::find(labels, normalized) != labels.end() ||
                   std::ranges::find(systemLabels, normalized) != systemLabels.end();
        }

        [[nodiscard]] bool IsSensitiveHeaderLabel(const std::string_view label)
        {
            static constexpr std::array<std::string_view, 4> labels{"authorization", "proxyauthorization", "cookie",
                                                                    "setcookie"};
            const auto normalized = NormalizeLabel(label, true);
            return std::ranges::find(labels, normalized) != labels.end();
        }

        struct LabelValueOptions final
        {
            bool AllowEquals = true;
            bool StopAtComma = true;
            bool StopAtSemicolon = false;
            bool RespectQuotes = true;
            bool QuoteReplacement = true;
        };

        using LabelPredicate = bool (*)(std::string_view);

        void ReplaceLabelledValues(std::string& text, const LabelPredicate predicate,
                                   const std::string_view replacement, const LabelValueOptions options,
                                   std::uint64_t& redactions)
        {
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                const auto character = static_cast<unsigned char>(text[cursor]);
                if (!IsLabelCharacter(character) ||
                    (cursor != 0U && IsWordCharacter(static_cast<unsigned char>(text[cursor - 1U]))))
                {
                    ++cursor;
                    continue;
                }

                const auto labelStart = cursor;
                while (cursor < text.size() && IsLabelCharacter(static_cast<unsigned char>(text[cursor])))
                    ++cursor;
                const auto labelEnd = cursor;
                auto separator = labelEnd;
                if (separator < text.size() && (text[separator] == '"' || text[separator] == '\''))
                    ++separator;
                while (separator < text.size() && std::isspace(static_cast<unsigned char>(text[separator])) != 0)
                    ++separator;
                if (separator >= text.size() ||
                    (text[separator] != ':' && (!options.AllowEquals || text[separator] != '=')))
                    continue;
                if (!predicate(std::string_view(text).substr(labelStart, labelEnd - labelStart)))
                    continue;
                ++separator;
                while (separator < text.size() && std::isspace(static_cast<unsigned char>(text[separator])) != 0)
                    ++separator;
                if (separator >= text.size())
                    continue;

                const auto valueStart = separator;
                auto valueEnd = valueStart;
                if (options.RespectQuotes && (text[valueStart] == '"' || text[valueStart] == '\''))
                {
                    const auto quote = text[valueStart];
                    const auto closing = text.find(quote, valueStart + 1U);
                    if (closing != std::string::npos)
                        valueEnd = closing + 1U;
                }
                if (valueEnd == valueStart)
                {
                    while (valueEnd < text.size() && text[valueEnd] != '\r' && text[valueEnd] != '\n' &&
                           (!options.StopAtComma || text[valueEnd] != ',') &&
                           (!options.StopAtSemicolon || text[valueEnd] != ';'))
                        ++valueEnd;
                }
                if (valueEnd == valueStart)
                    continue;

                result.append(text, copied, valueStart - copied);
                if (options.QuoteReplacement)
                    result.push_back('"');
                result.append(replacement);
                if (options.QuoteReplacement)
                    result.push_back('"');
                copied = valueEnd;
                cursor = valueEnd;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        [[nodiscard]] std::string RemovePrivateKeys(const std::string_view input, std::uint64_t& redactions)
        {
            std::string result;
            result.reserve(input.size());
            bool insideKey = false;
            std::size_t cursor = 0;
            while (cursor < input.size())
            {
                const auto end = input.find('\n', cursor);
                const auto length = end == std::string_view::npos ? input.size() - cursor : end - cursor + 1U;
                const auto line = input.substr(cursor, length);
                if (!insideKey && line.find("-----BEGIN ") != std::string_view::npos &&
                    line.find("PRIVATE KEY-----") != std::string_view::npos)
                {
                    result += "<redacted:private-key>\n";
                    insideKey = true;
                    ++redactions;
                }
                else if (insideKey)
                {
                    if (line.find("-----END ") != std::string_view::npos &&
                        line.find("PRIVATE KEY-----") != std::string_view::npos)
                        insideKey = false;
                }
                else
                {
                    result.append(line);
                }
                cursor += length;
            }
            return result;
        }

        [[nodiscard]] bool IsCredentialTokenCharacter(const unsigned char character) noexcept
        {
            return IsAsciiAlphaNumeric(character) || character == '_' || character == '-' || character == '+' ||
                   character == '/';
        }

        [[nodiscard]] bool IsBearerTokenCharacter(const unsigned char character) noexcept
        {
            return IsAsciiAlphaNumeric(character) || character == '.' || character == '_' || character == '~' ||
                   character == '+' || character == '-' || character == '/' || character == '=';
        }

        void ReplaceBearerTokens(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view label = "Bearer";
            constexpr std::string_view replacement = "<redacted:credential>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                if (!StartsWithInsensitive(text, cursor, label) ||
                    (cursor != 0U && IsWordCharacter(static_cast<unsigned char>(text[cursor - 1U]))) ||
                    (cursor + label.size() < text.size() &&
                     IsWordCharacter(static_cast<unsigned char>(text[cursor + label.size()]))))
                {
                    ++cursor;
                    continue;
                }
                auto tokenStart = cursor + label.size();
                if (tokenStart >= text.size() || std::isspace(static_cast<unsigned char>(text[tokenStart])) == 0)
                {
                    ++cursor;
                    continue;
                }
                while (tokenStart < text.size() && std::isspace(static_cast<unsigned char>(text[tokenStart])) != 0)
                    ++tokenStart;
                auto tokenEnd = tokenStart;
                while (tokenEnd < text.size() && IsBearerTokenCharacter(static_cast<unsigned char>(text[tokenEnd])))
                    ++tokenEnd;
                if (tokenEnd == tokenStart)
                {
                    cursor = tokenStart;
                    continue;
                }
                result.append(text, copied, tokenStart - copied);
                result.append(replacement);
                copied = tokenEnd;
                cursor = tokenEnd;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        [[nodiscard]] bool IsUrlTerminator(const unsigned char character) noexcept
        {
            return std::isspace(character) != 0 || character == '"' || character == '\'';
        }

        void ReplaceUrls(std::string& text, std::uint64_t& redactions)
        {
            static constexpr std::array<std::string_view, 3> schemes{"https://", "http://", "ftp://"};
            constexpr std::string_view replacement = "<redacted:url>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                std::string_view matched;
                for (const auto scheme : schemes)
                {
                    if (StartsWithInsensitive(text, cursor, scheme))
                    {
                        matched = scheme;
                        break;
                    }
                }
                if (matched.empty() || (cursor != 0U && IsWordCharacter(static_cast<unsigned char>(text[cursor - 1U]))))
                {
                    ++cursor;
                    continue;
                }
                auto end = cursor + matched.size();
                while (end < text.size() && !IsUrlTerminator(static_cast<unsigned char>(text[end])))
                    ++end;
                result.append(text, copied, cursor - copied);
                result.append(replacement);
                copied = end;
                cursor = end;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        [[nodiscard]] bool IsEmailLocalCharacter(const unsigned char character) noexcept
        {
            return IsAsciiAlphaNumeric(character) || character == '.' || character == '_' || character == '%' ||
                   character == '+' || character == '-';
        }

        [[nodiscard]] bool IsEmailDomainCharacter(const unsigned char character) noexcept
        {
            return IsAsciiAlphaNumeric(character) || character == '.' || character == '-';
        }

        void ReplaceEmails(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view replacement = "<redacted:email>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                if (!IsEmailLocalCharacter(static_cast<unsigned char>(text[cursor])) ||
                    (cursor != 0U && IsEmailLocalCharacter(static_cast<unsigned char>(text[cursor - 1U]))))
                {
                    ++cursor;
                    continue;
                }
                auto localEnd = cursor;
                while (localEnd < text.size() && IsEmailLocalCharacter(static_cast<unsigned char>(text[localEnd])))
                    ++localEnd;
                if (localEnd >= text.size() || text[localEnd] != '@')
                {
                    cursor = localEnd;
                    continue;
                }
                const auto domainStart = localEnd + 1U;
                auto domainEnd = domainStart;
                while (domainEnd < text.size() && IsEmailDomainCharacter(static_cast<unsigned char>(text[domainEnd])))
                    ++domainEnd;
                const auto domain = std::string_view(text).substr(domainStart, domainEnd - domainStart);
                const auto lastDot = domain.rfind('.');
                const bool validSuffix = lastDot != std::string_view::npos && lastDot + 3U <= domain.size() &&
                                         std::ranges::all_of(domain.substr(lastDot + 1U), [](const unsigned char value)
                                                             { return IsAsciiAlpha(value); });
                if (!validSuffix ||
                    (domainEnd < text.size() && IsWordCharacter(static_cast<unsigned char>(text[domainEnd]))))
                {
                    cursor = domainEnd;
                    continue;
                }
                result.append(text, copied, cursor - copied);
                result.append(replacement);
                copied = domainEnd;
                cursor = domainEnd;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        [[nodiscard]] bool IsQuotedPathTerminator(const unsigned char character) noexcept
        {
            return character == '\r' || character == '\n' || character == '"' || character == '\'';
        }

        void ReplaceWindowsPaths(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view replacement = "<redacted:path>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor + 2U < text.size())
            {
                if (!IsAsciiAlpha(static_cast<unsigned char>(text[cursor])) || text[cursor + 1U] != ':' ||
                    (text[cursor + 2U] != '\\' && text[cursor + 2U] != '/'))
                {
                    ++cursor;
                    continue;
                }
                auto end = cursor + 3U;
                while (end < text.size() && !IsQuotedPathTerminator(static_cast<unsigned char>(text[end])))
                    ++end;
                result.append(text, copied, cursor - copied);
                result.append(replacement);
                copied = end;
                cursor = end;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        void ReplaceUncPaths(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view replacement = "<redacted:path>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor + 1U < text.size())
            {
                if (text[cursor] != '\\' || text[cursor + 1U] != '\\')
                {
                    ++cursor;
                    continue;
                }
                auto end = cursor + 2U;
                while (end < text.size() && !IsQuotedPathTerminator(static_cast<unsigned char>(text[end])))
                    ++end;
                result.append(text, copied, cursor - copied);
                result.append(replacement);
                copied = end;
                cursor = end;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        [[nodiscard]] bool IsUnixPathPrefix(const unsigned char character) noexcept
        {
            return std::isspace(character) != 0 || character == '=' || character == ':' || character == '(' ||
                   character == '\'' || character == '"';
        }

        [[nodiscard]] bool IsUnixPathTerminator(const unsigned char character) noexcept
        {
            return character == '\r' || character == '\n' || character == '"' || character == '\'' ||
                   character == ',' || character == ';';
        }

        void ReplaceUnixPaths(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view replacement = "<redacted:path>";
            std::string result;
            result.reserve(text.size());
            std::size_t copied = 0;
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                if (text[cursor] != '/' ||
                    (cursor != 0U && !IsUnixPathPrefix(static_cast<unsigned char>(text[cursor - 1U]))) ||
                    cursor + 1U >= text.size() || IsUnixPathTerminator(static_cast<unsigned char>(text[cursor + 1U])))
                {
                    ++cursor;
                    continue;
                }
                auto end = cursor + 1U;
                while (end < text.size() && !IsUnixPathTerminator(static_cast<unsigned char>(text[end])))
                    ++end;
                result.append(text, copied, cursor - copied);
                result.append(replacement);
                copied = end;
                cursor = end;
                ++redactions;
            }
            if (copied == 0U)
                return;
            result.append(text, copied, text.size() - copied);
            text = std::move(result);
        }

        void ReplaceMalformedUtf8(std::string& text, std::uint64_t& redactions)
        {
            const auto continuation = [&text](const std::size_t index) noexcept
            { return index < text.size() && (static_cast<unsigned char>(text[index]) & 0xc0U) == 0x80U; };
            std::string result;
            result.reserve(text.size());
            for (std::size_t index = 0; index < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[index]);
                std::size_t length = 0;
                if (first <= 0x7fU)
                {
                    length = 1;
                }
                else if (first >= 0xc2U && first <= 0xdfU && continuation(index + 1U))
                {
                    length = 2;
                }
                else if (first >= 0xe0U && first <= 0xefU && continuation(index + 1U) && continuation(index + 2U))
                {
                    const auto second = static_cast<unsigned char>(text[index + 1U]);
                    if ((first != 0xe0U || second >= 0xa0U) && (first != 0xedU || second <= 0x9fU))
                        length = 3;
                }
                else if (first >= 0xf0U && first <= 0xf4U && continuation(index + 1U) && continuation(index + 2U) &&
                         continuation(index + 3U))
                {
                    const auto second = static_cast<unsigned char>(text[index + 1U]);
                    if ((first != 0xf0U || second >= 0x90U) && (first != 0xf4U || second <= 0x8fU))
                        length = 4;
                }

                if (length == 0)
                {
                    result.push_back('?');
                    ++redactions;
                    ++index;
                    continue;
                }
                result.append(text, index, length);
                index += length;
            }
            text = std::move(result);
        }

        [[nodiscard]] bool LooksLikeCredential(const std::string_view token) noexcept
        {
            if (token.size() < 32U || token.size() > 4096U)
                return false;
            bool uppercase = false;
            bool lowercase = false;
            bool digit = false;
            bool onlyHex = true;
            std::array<bool, 128> seen{};
            std::size_t unique = 0;
            for (const auto value : token)
            {
                const auto character = static_cast<unsigned char>(value);
                uppercase |= character >= 'A' && character <= 'Z';
                lowercase |= character >= 'a' && character <= 'z';
                digit |= IsAsciiDigit(character);
                onlyHex &= IsAsciiDigit(character) || (character >= 'a' && character <= 'f') ||
                           (character >= 'A' && character <= 'F');
                if (character < seen.size() && !seen[character])
                {
                    seen[character] = true;
                    ++unique;
                }
            }
            const auto classes = static_cast<unsigned int>(uppercase) + static_cast<unsigned int>(lowercase) +
                                 static_cast<unsigned int>(digit);
            return !onlyHex && classes >= 2U && unique >= 12U;
        }

        void RemoveCredentialLikeTokens(std::string& text, std::uint64_t& redactions)
        {
            constexpr std::string_view Replacement = "<redacted:credential-like>";
            std::string result;
            result.reserve(text.size());
            std::size_t cursor = 0;
            while (cursor < text.size())
            {
                const auto character = static_cast<unsigned char>(text[cursor]);
                if (!IsCredentialTokenCharacter(character))
                {
                    result.push_back(text[cursor++]);
                    continue;
                }
                auto end = cursor + 1U;
                while (end < text.size() && IsCredentialTokenCharacter(static_cast<unsigned char>(text[end])))
                    ++end;
                const auto token = std::string_view(text).substr(cursor, end - cursor);
                if (LooksLikeCredential(token))
                {
                    result.append(Replacement);
                    ++redactions;
                }
                else
                {
                    result.append(token);
                }
                cursor = end;
            }
            text = std::move(result);
        }

        [[nodiscard]] bool IsWindowsDeviceComponent(const std::string_view component) noexcept
        {
            const auto base = component.substr(0, component.find('.'));
            std::string normalized;
            normalized.reserve(base.size());
            for (const auto value : base)
                normalized.push_back(LowerAscii(static_cast<unsigned char>(value)));
            if (normalized == "con" || normalized == "prn" || normalized == "aux" || normalized == "nul" ||
                normalized == "clock$")
                return true;
            return normalized.size() == 4U && (normalized.starts_with("com") || normalized.starts_with("lpt")) &&
                   normalized.back() >= '1' && normalized.back() <= '9';
        }
    } // namespace

    bool IsPortableArchivePath(const std::string_view path) noexcept
    {
        if (path.empty() || path.size() > 240U || path.front() == '/' || path.back() == '/' ||
            path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos)
            return false;
        std::size_t cursor = 0;
        while (cursor < path.size())
        {
            const auto end = path.find('/', cursor);
            const auto component =
                path.substr(cursor, end == std::string_view::npos ? path.size() - cursor : end - cursor);
            if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
                component.back() == ' ' || IsWindowsDeviceComponent(component) ||
                std::ranges::any_of(component, [](const unsigned char character)
                                    { return character < 0x20U || character == 0x7fU; }))
                return false;
            if (end == std::string_view::npos)
                break;
            cursor = end + 1U;
        }
        return true;
    }

    SanitizedText SanitizeText(const std::string_view input)
    {
        SanitizedText result;
        result.Contents = RemovePrivateKeys(input, result.Redactions);
        ReplaceMalformedUtf8(result.Contents, result.Redactions);
        for (auto& value : result.Contents)
        {
            const auto character = static_cast<unsigned char>(value);
            if ((character < 0x20U && character != '\n' && character != '\r' && character != '\t') ||
                character == 0x7fU)
            {
                value = '?';
                ++result.Redactions;
            }
        }

        const LabelValueOptions headerOptions{.AllowEquals = false,
                                              .StopAtComma = false,
                                              .StopAtSemicolon = false,
                                              .RespectQuotes = false,
                                              .QuoteReplacement = false};
        ReplaceLabelledValues(result.Contents, IsSensitiveHeaderLabel, "<redacted:credential>", headerOptions,
                              result.Redactions);
        ReplaceBearerTokens(result.Contents, result.Redactions);
        ReplaceLabelledValues(result.Contents, IsCredentialLabel, "<redacted:credential>", {}, result.Redactions);
        const LabelValueOptions metadataOptions{.AllowEquals = true,
                                                .StopAtComma = true,
                                                .StopAtSemicolon = true,
                                                .RespectQuotes = true,
                                                .QuoteReplacement = true};
        ReplaceLabelledValues(result.Contents, IsPrivateMetadataLabel, "<redacted:private-metadata>", metadataOptions,
                              result.Redactions);
        ReplaceUrls(result.Contents, result.Redactions);
        ReplaceEmails(result.Contents, result.Redactions);
        ReplaceWindowsPaths(result.Contents, result.Redactions);
        ReplaceUncPaths(result.Contents, result.Redactions);
        ReplaceUnixPaths(result.Contents, result.Redactions);
        RemoveCredentialLikeTokens(result.Contents, result.Redactions);
        return result;
    }
} // namespace Keire::Internal::DiagnosticBundleDetail
