#include "KeireHubRuntime/HubActivation.h"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        constexpr std::array ActivationMagic{'K', 'H', 'A', 'C'};
        constexpr std::uint8_t ActivationProtocolVersion = 1;
        constexpr std::size_t ActivationHeaderBytes = 10;
        constexpr std::size_t MaximumVersionIdBytes = 128;
        constexpr std::string_view MarketplaceProductUrlPrefix = "keirehub://marketplace/product/";

        [[nodiscard]] HubError InvalidArgument(std::string message)
        {
            return {.Code = HubErrorCode::InvalidArgument, .Message = std::move(message)};
        }

        [[nodiscard]] HubError InvalidFrame(std::string message)
        {
            return {.Code = HubErrorCode::InvalidData, .Message = std::move(message)};
        }

        [[nodiscard]] bool IsAsciiAlphaNumeric(const unsigned char value) noexcept
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
        }

        [[nodiscard]] bool IsValidUtf8(const std::string_view value) noexcept
        {
            std::size_t index = 0;
            while (index < value.size())
            {
                const auto first = static_cast<unsigned char>(value[index]);
                if (first <= 0x7fU)
                {
                    ++index;
                    continue;
                }

                std::size_t continuationCount = 0;
                std::uint32_t codePoint = 0;
                if (first >= 0xc2U && first <= 0xdfU)
                {
                    continuationCount = 1;
                    codePoint = first & 0x1fU;
                }
                else if (first >= 0xe0U && first <= 0xefU)
                {
                    continuationCount = 2;
                    codePoint = first & 0x0fU;
                }
                else if (first >= 0xf0U && first <= 0xf4U)
                {
                    continuationCount = 3;
                    codePoint = first & 0x07U;
                }
                else
                    return false;
                if (index + continuationCount >= value.size())
                    return false;

                for (std::size_t offset = 1; offset <= continuationCount; ++offset)
                {
                    const auto continuation = static_cast<unsigned char>(value[index + offset]);
                    if ((continuation & 0xc0U) != 0x80U)
                        return false;
                    codePoint = (codePoint << 6U) | (continuation & 0x3fU);
                }
                if ((continuationCount == 2 && codePoint < 0x800U) ||
                    (continuationCount == 3 && codePoint < 0x10000U) ||
                    (codePoint >= 0xd800U && codePoint <= 0xdfffU) || codePoint > 0x10ffffU)
                {
                    return false;
                }
                index += continuationCount + 1;
            }
            return true;
        }

        [[nodiscard]] bool HasControlCharacter(const std::string_view value) noexcept
        {
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte < 0x20U || byte == 0x7fU)
                    return true;
            }
            return false;
        }

        [[nodiscard]] std::optional<std::string_view> PageName(const HubPage page) noexcept
        {
            switch (page)
            {
            case HubPage::Home:
                return "home";
            case HubPage::Projects:
                return "projects";
            case HubPage::Installs:
                return "installs";
            case HubPage::Templates:
                return "templates";
            case HubPage::Learn:
                return "learn";
            case HubPage::Resources:
                return "resources";
            case HubPage::Licenses:
                return "licenses";
            case HubPage::Settings:
                return "settings";
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<HubPage> ParsePage(const std::string_view value) noexcept
        {
            constexpr std::array pages{
                std::pair{"home", HubPage::Home},         std::pair{"projects", HubPage::Projects},
                std::pair{"installs", HubPage::Installs}, std::pair{"templates", HubPage::Templates},
                std::pair{"learn", HubPage::Learn},       std::pair{"resources", HubPage::Resources},
                std::pair{"licenses", HubPage::Licenses}, std::pair{"settings", HubPage::Settings},
            };
            for (const auto& [name, page] : pages)
            {
                if (value == name)
                    return page;
            }
            return std::nullopt;
        }

        [[nodiscard]] HubResult<std::string> EncodePath(const std::filesystem::path& path)
        {
            try
            {
                const auto encoded = path.generic_u8string();
                return HubResult<std::string>::Success(
                    std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
            }
            catch (const std::exception&)
            {
                return HubResult<std::string>::Failure(InvalidArgument("Activation path is not valid UTF-8."));
            }
        }

        [[nodiscard]] HubResult<std::filesystem::path> DecodePath(const std::string_view encoded)
        {
            if (!IsValidUtf8(encoded))
                return HubResult<std::filesystem::path>::Failure(InvalidFrame("Activation path is not valid UTF-8."));
            try
            {
                const auto* first = reinterpret_cast<const char8_t*>(encoded.data());
                return HubResult<std::filesystem::path>::Success(
                    std::filesystem::path(std::u8string(first, first + encoded.size())));
            }
            catch (const std::exception&)
            {
                return HubResult<std::filesystem::path>::Failure(InvalidFrame("Activation path is invalid."));
            }
        }

        [[nodiscard]] HubStatus ValidatePath(const std::optional<std::filesystem::path>& path)
        {
            if (!path || path->empty() || !path->is_absolute())
                return HubStatus::Failure(InvalidArgument("Activation paths must be absolute."));
            if (path->native().size() > MaximumHubActivationFrameBytes)
                return HubStatus::Failure(InvalidArgument("Activation path is too large."));
            for (const auto& component : *path)
            {
                if (component == "." || component == "..")
                    return HubStatus::Failure(InvalidArgument("Activation paths must not contain traversal."));
            }
            const auto encoded = EncodePath(*path);
            if (!encoded)
                return HubStatus::Failure(encoded.Error());
            if (encoded.Value().empty() || !IsValidUtf8(encoded.Value()) || HasControlCharacter(encoded.Value()))
                return HubStatus::Failure(InvalidArgument("Activation path contains invalid characters."));
            return HubStatus::Success();
        }

        [[nodiscard]] bool IsValidVersionId(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > MaximumVersionIdBytes ||
                !IsAsciiAlphaNumeric(static_cast<unsigned char>(value.front())))
            {
                return false;
            }
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (!IsAsciiAlphaNumeric(byte) && character != '.' && character != '-' && character != '_' &&
                    character != '+' && character != ':' && character != '@')
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsHexDigit(const char value) noexcept
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        }

        [[nodiscard]] bool IsValidProductId(const std::string_view value) noexcept
        {
            if (value.size() != 36U || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-')
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 8U || index == 13U || index == 18U || index == 23U)
                    continue;
                if (!IsHexDigit(value[index]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidBuildSupport(const HubActivationRequest& request) noexcept
        {
            if (!request.Platform || !request.Architecture)
                return false;
            const bool platform =
                *request.Platform == "windows" || *request.Platform == "linux" || *request.Platform == "macos";
            const bool architecture = *request.Architecture == "x86_64" || *request.Architecture == "arm64";
            return platform && architecture;
        }

        [[nodiscard]] bool ValidOAuthCallback(const HubActivationRequest& request) noexcept
        {
            if (!request.Url || !request.Url->starts_with("keirehub://oauth/callback?") ||
                request.Url->size() > MaximumHubActivationFrameBytes - ActivationHeaderBytes - 2U ||
                request.Url->find('#') != request.Url->npos || HasControlCharacter(*request.Url))
            {
                return false;
            }
            return std::ranges::all_of(*request.Url,
                                       [](const unsigned char value) { return value >= 0x20U && value <= 0x7eU; });
        }

        [[nodiscard]] bool HasNoPayload(const HubActivationRequest& request) noexcept
        {
            return !request.Page && !request.Path && !request.VersionId && !request.Platform && !request.Architecture &&
                   !request.Url && !request.ProductId;
        }

        void AppendUint16(std::string& target, const std::size_t value)
        {
            target.push_back(static_cast<char>((value >> 8U) & 0xffU));
            target.push_back(static_cast<char>(value & 0xffU));
        }

        [[nodiscard]] std::uint16_t ReadUint16(const std::string_view source, const std::size_t offset) noexcept
        {
            const auto high = static_cast<unsigned char>(source[offset]);
            const auto low = static_cast<unsigned char>(source[offset + 1]);
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
        }

        [[nodiscard]] HubResult<HubActivationRequest> ParsedRequest(HubActivationRequest request)
        {
            const auto status = ValidateHubActivation(request);
            if (!status)
                return HubResult<HubActivationRequest>::Failure(status.Error());
            return HubResult<HubActivationRequest>::Success(std::move(request));
        }
    } // namespace

    HubStatus ValidateHubActivation(const HubActivationRequest& request)
    {
        switch (request.Action)
        {
        case HubActivationAction::Show:
            if (!HasNoPayload(request))
                return HubStatus::Failure(InvalidArgument("Show activation must not contain a payload."));
            break;
        case HubActivationAction::Navigate:
            if (!request.Page || !PageName(*request.Page) || request.Path || request.VersionId || request.Platform ||
                request.Architecture || request.Url || request.ProductId)
            {
                return HubStatus::Failure(InvalidArgument("Navigate activation requires exactly one valid page."));
            }
            break;
        case HubActivationAction::OpenProject:
        case HubActivationAction::ImportPackage:
            if (request.Page || request.VersionId || request.Platform || request.Architecture || request.Url ||
                request.ProductId)
                return HubStatus::Failure(InvalidArgument("Path activation contains an unexpected field."));
            if (const auto pathStatus = ValidatePath(request.Path); !pathStatus)
                return pathStatus;
            break;
        case HubActivationAction::InstallVersion:
            if (request.Page || request.Path || request.Platform || request.Architecture || request.Url ||
                request.ProductId || !request.VersionId || !IsValidVersionId(*request.VersionId))
            {
                return HubStatus::Failure(InvalidArgument("Install-version activation requires one valid ID."));
            }
            break;
        case HubActivationAction::BuildSupport:
            if (request.Page || request.Path || request.VersionId || request.Url || request.ProductId ||
                !ValidBuildSupport(request))
            {
                return HubStatus::Failure(
                    InvalidArgument("Build Support activation requires a valid platform and architecture."));
            }
            break;
        case HubActivationAction::OAuthCallback:
            if (request.Page || request.Path || request.VersionId || request.Platform || request.Architecture ||
                request.ProductId || !ValidOAuthCallback(request))
            {
                return HubStatus::Failure(
                    InvalidArgument("OAuth callback activation requires one valid callback URL."));
            }
            break;
        case HubActivationAction::MarketplaceProduct:
            if (request.Page || request.Path || request.VersionId || request.Platform || request.Architecture ||
                request.Url || !request.ProductId || !IsValidProductId(*request.ProductId))
            {
                return HubStatus::Failure(
                    InvalidArgument("Marketplace product activation requires one valid product ID."));
            }
            break;
        default:
            return HubStatus::Failure(InvalidArgument("Activation action is invalid."));
        }
        return HubStatus::Success();
    }

    HubResult<std::string> EncodeHubActivation(const HubActivationRequest& request)
    {
        const auto status = ValidateHubActivation(request);
        if (!status)
            return HubResult<std::string>::Failure(status.Error());

        std::vector<std::string> fields;
        switch (request.Action)
        {
        case HubActivationAction::Show:
            break;
        case HubActivationAction::Navigate:
            fields.emplace_back(*PageName(*request.Page));
            break;
        case HubActivationAction::OpenProject:
        case HubActivationAction::ImportPackage:
        {
            auto encoded = EncodePath(*request.Path);
            if (!encoded)
                return HubResult<std::string>::Failure(encoded.Error());
            fields.push_back(std::move(encoded).Value());
            break;
        }
        case HubActivationAction::InstallVersion:
            fields.push_back(*request.VersionId);
            break;
        case HubActivationAction::BuildSupport:
            fields.push_back(*request.Platform);
            fields.push_back(*request.Architecture);
            break;
        case HubActivationAction::OAuthCallback:
            fields.push_back(*request.Url);
            break;
        case HubActivationAction::MarketplaceProduct:
            fields.push_back(*request.ProductId);
            break;
        default:
            return HubResult<std::string>::Failure(InvalidArgument("Activation action is invalid."));
        }

        std::string frame;
        frame.reserve(MaximumHubActivationFrameBytes);
        frame.append(ActivationMagic.data(), ActivationMagic.size());
        frame.push_back(static_cast<char>(ActivationProtocolVersion));
        frame.push_back(static_cast<char>(request.Action));
        AppendUint16(frame, 0);
        frame.push_back(static_cast<char>(fields.size()));
        frame.push_back(0);
        for (const auto& field : fields)
        {
            if (field.size() > std::numeric_limits<std::uint16_t>::max())
                return HubResult<std::string>::Failure(InvalidArgument("Activation field is too large."));
            AppendUint16(frame, field.size());
            frame.append(field);
        }
        if (frame.size() > MaximumHubActivationFrameBytes)
            return HubResult<std::string>::Failure(InvalidArgument("Activation request is too large."));
        frame[6] = static_cast<char>((frame.size() >> 8U) & 0xffU);
        frame[7] = static_cast<char>(frame.size() & 0xffU);
        return HubResult<std::string>::Success(std::move(frame));
    }

    HubResult<HubActivationRequest> DecodeHubActivation(const std::string_view frame)
    {
        if (frame.size() < ActivationHeaderBytes || frame.size() > MaximumHubActivationFrameBytes)
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation frame size is invalid."));
        if (!std::equal(ActivationMagic.begin(), ActivationMagic.end(), frame.begin()))
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation frame magic is invalid."));
        if (static_cast<unsigned char>(frame[4]) != ActivationProtocolVersion)
            return HubResult<HubActivationRequest>::Failure(
                InvalidFrame("Activation protocol version is unsupported."));
        if (ReadUint16(frame, 6) != frame.size() || frame[9] != 0)
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation frame header is invalid."));

        const auto actionValue = static_cast<unsigned char>(frame[5]);
        if (actionValue < static_cast<unsigned char>(HubActivationAction::Show) ||
            actionValue > static_cast<unsigned char>(HubActivationAction::MarketplaceProduct))
        {
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation action is unknown."));
        }
        const auto action = static_cast<HubActivationAction>(actionValue);
        const std::size_t expectedFields = action == HubActivationAction::Show           ? 0
                                           : action == HubActivationAction::BuildSupport ? 2
                                                                                         : 1;
        if (static_cast<unsigned char>(frame[8]) != expectedFields)
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation field count is invalid."));

        std::vector<std::string_view> fields;
        std::size_t offset = ActivationHeaderBytes;
        for (std::size_t index = 0; index < expectedFields; ++index)
        {
            if (offset + 2 > frame.size())
                return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation field header is truncated."));
            const auto length = ReadUint16(frame, offset);
            offset += 2;
            if (offset + length > frame.size())
                return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation field is truncated."));
            fields.push_back(frame.substr(offset, length));
            offset += length;
        }
        if (offset != frame.size())
            return HubResult<HubActivationRequest>::Failure(InvalidFrame("Activation frame has trailing bytes."));
        for (const auto field : fields)
        {
            if (!IsValidUtf8(field) || HasControlCharacter(field))
                return HubResult<HubActivationRequest>::Failure(
                    InvalidFrame("Activation field contains invalid text."));
        }

        HubActivationRequest request{.Action = action};
        switch (action)
        {
        case HubActivationAction::Show:
            break;
        case HubActivationAction::Navigate:
            request.Page = ParsePage(fields[0]);
            break;
        case HubActivationAction::OpenProject:
        case HubActivationAction::ImportPackage:
        {
            auto path = DecodePath(fields[0]);
            if (!path)
                return HubResult<HubActivationRequest>::Failure(path.Error());
            request.Path = std::move(path).Value();
            break;
        }
        case HubActivationAction::InstallVersion:
            request.VersionId = fields[0];
            break;
        case HubActivationAction::BuildSupport:
            request.Platform = fields[0];
            request.Architecture = fields[1];
            break;
        case HubActivationAction::OAuthCallback:
            request.Url = fields[0];
            break;
        case HubActivationAction::MarketplaceProduct:
            request.ProductId = fields[0];
            break;
        default:
            break;
        }
        const auto status = ValidateHubActivation(request);
        if (!status)
        {
            auto error = status.Error();
            error.Code = HubErrorCode::InvalidData;
            return HubResult<HubActivationRequest>::Failure(std::move(error));
        }
        return HubResult<HubActivationRequest>::Success(std::move(request));
    }

    HubResult<HubActivationRequest> ParseHubActivationArguments(const std::span<const std::string_view> arguments)
    {
        for (const auto argument : arguments)
        {
            if (argument.size() > MaximumHubActivationFrameBytes)
            {
                return HubResult<HubActivationRequest>::Failure(
                    InvalidArgument("Hub activation argument is too large."));
            }
        }
        if (arguments.empty())
            return ParsedRequest({});
        const auto option = arguments.front();
        if (arguments.size() == 1U && option.starts_with("keirehub://oauth/callback?"))
        {
            return ParsedRequest({.Action = HubActivationAction::OAuthCallback, .Url = std::string(option)});
        }
        if (arguments.size() == 1U && option.starts_with(MarketplaceProductUrlPrefix))
        {
            return ParsedRequest({.Action = HubActivationAction::MarketplaceProduct,
                                  .ProductId = std::string(option.substr(MarketplaceProductUrlPrefix.size()))});
        }
        if (option == "--show" && arguments.size() == 1)
            return ParsedRequest({});
        if (option == "--navigate" && arguments.size() == 2)
            return ParsedRequest({.Action = HubActivationAction::Navigate, .Page = ParsePage(arguments[1])});
        if ((option == "--open-project" || option == "--import-package" || option == "--locate-package") &&
            arguments.size() == 2)
        {
            auto path = DecodePath(arguments[1]);
            if (!path)
            {
                auto error = path.Error();
                error.Code = HubErrorCode::InvalidArgument;
                return HubResult<HubActivationRequest>::Failure(std::move(error));
            }
            const auto action =
                option == "--open-project" ? HubActivationAction::OpenProject : HubActivationAction::ImportPackage;
            return ParsedRequest({.Action = action, .Path = std::move(path).Value()});
        }
        if (option == "--install-version" && arguments.size() == 2)
        {
            return ParsedRequest(
                {.Action = HubActivationAction::InstallVersion, .VersionId = std::string(arguments[1])});
        }
        if (option == "--build-support" && arguments.size() == 3)
        {
            return ParsedRequest({.Action = HubActivationAction::BuildSupport,
                                  .Platform = std::string(arguments[1]),
                                  .Architecture = std::string(arguments[2])});
        }
        if (option == "--marketplace-product" && arguments.size() == 2)
        {
            return ParsedRequest(
                {.Action = HubActivationAction::MarketplaceProduct, .ProductId = std::string(arguments[1])});
        }
        return HubResult<HubActivationRequest>::Failure(
            InvalidArgument("Specify exactly one valid Hub activation action."));
    }
} // namespace KeireHub
