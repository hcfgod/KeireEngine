#include "KeireHubRuntime/DistributionConfiguration.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumConfigurationBytes = std::size_t{64} * 1024;
        constexpr std::size_t MaximumConfigurationDepth = 8;
        constexpr std::size_t MaximumTrustedKeys = 8;

        [[nodiscard]] HubResult<std::string> ValidateTrustedKey(const Detail::Json& value)
        {
            try
            {
                if (!value.is_object() || value.size() != 5U || value.at("schemaVersion").get<std::uint32_t>() != 1U ||
                    value.at("algorithm").get<std::string>() != "Ed25519")
                {
                    throw std::invalid_argument("invalid trusted key header");
                }
                const auto keyId = value.at("keyId").get<std::string>();
                const auto encodedKey = value.at("publicKey").get<std::string>();
                const auto fingerprint = value.at("fingerprint").get<std::string>();
                const auto key = Detail::DecodeCanonicalBase64(encodedKey, 32);
                if (!Detail::IsDistributionKeyId(keyId) || !key || key->size() != 32U)
                    throw std::invalid_argument("invalid trusted public key");
                const auto digest = Detail::Sha256Hex(*key);
                if (keyId != "ed25519-" + digest.substr(0, 32) || fingerprint != "sha256:" + digest)
                    throw std::invalid_argument("trusted key identity mismatch");
                return HubResult<std::string>::Success(value.dump());
            }
            catch (const std::exception& exception)
            {
                return HubResult<std::string>::Failure(
                    {.Code = HubErrorCode::DistributionConfigurationInvalid,
                     .Message = "A trusted distribution key in the Hub package is invalid.",
                     .AffectedItem = "trusted-key",
                     .TechnicalDetails = exception.what()});
            }
        }
    } // namespace

    HubResult<DistributionConfiguration> LoadDistributionConfiguration(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (status.type() == std::filesystem::file_type::not_found ||
            error == std::make_error_code(std::errc::no_such_file_or_directory))
        {
            return HubResult<DistributionConfiguration>::Success({});
        }
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
        {
            return HubResult<DistributionConfiguration>::Failure(
                {.Code = HubErrorCode::DistributionConfigurationInvalid,
                 .Message = "The packaged distribution configuration is missing or unsafe.",
                 .AffectedItem = Detail::PathToUtf8(path),
                 .TechnicalDetails = error.message()});
        }

        auto text = Detail::ReadTextFile(path, MaximumConfigurationBytes);
        if (!text)
            return HubResult<DistributionConfiguration>::Failure(text.Error());
        auto parsed = Detail::ParseStrictJson(
            text.Value(), MaximumConfigurationDepth, HubErrorCode::DistributionConfigurationInvalid,
            "The packaged distribution configuration is malformed.", Detail::PathToUtf8(path));
        if (!parsed)
            return HubResult<DistributionConfiguration>::Failure(parsed.Error());
        try
        {
            const auto& value = parsed.Value();
            if (!value.is_object() || !value.at("schemaVersion").is_number_unsigned() ||
                value.at("schemaVersion").get<std::uint32_t>() != DistributionConfiguration::CurrentSchemaVersion ||
                !value.at("onlineDiscoveryEnabled").is_boolean())
            {
                throw std::invalid_argument("invalid distribution configuration header");
            }

            DistributionConfiguration result;
            result.OnlineDiscoveryEnabled = value.at("onlineDiscoveryEnabled").get<bool>();
            if (!result.OnlineDiscoveryEnabled)
            {
                if (value.size() != 2U)
                    throw std::invalid_argument("disabled distribution configuration contains online fields");
                return HubResult<DistributionConfiguration>::Success(std::move(result));
            }
            if (value.size() != 5U || !value.at("minimumSequence").is_number_unsigned())
                throw std::invalid_argument("enabled distribution configuration is incomplete");
            result.ServiceBaseUrl = value.at("serviceBaseUrl").get<std::string>();
            const auto normalized = Detail::NormalizeServiceBaseUrl(result.ServiceBaseUrl, false);
            if (!normalized || *normalized != result.ServiceBaseUrl)
                throw std::invalid_argument("distribution service URL is not canonical HTTPS");
            result.MinimumSequence = value.at("minimumSequence").get<std::uint64_t>();
            if (result.MinimumSequence == 0U)
                throw std::invalid_argument("distribution sequence floor is invalid");

            const auto& keys = value.at("trustedKeys");
            if (!keys.is_array() || keys.empty() || keys.size() > MaximumTrustedKeys)
                throw std::invalid_argument("trusted key set is empty or too large");
            std::set<std::string, std::less<>> keyIds;
            for (const auto& key : keys)
            {
                auto document = ValidateTrustedKey(key);
                if (!document)
                    return HubResult<DistributionConfiguration>::Failure(document.Error());
                const auto keyId = key.at("keyId").get<std::string>();
                if (!keyIds.insert(keyId).second)
                    throw std::invalid_argument("trusted key ID is duplicated");
                result.TrustedPublicKeyDocuments.push_back(std::move(document).Value());
            }
            return HubResult<DistributionConfiguration>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<DistributionConfiguration>::Failure(
                {.Code = HubErrorCode::DistributionConfigurationInvalid,
                 .Message = "The packaged distribution configuration is invalid.",
                 .AffectedItem = Detail::PathToUtf8(path),
                 .TechnicalDetails = exception.what()});
        }
    }
} // namespace KeireHub
