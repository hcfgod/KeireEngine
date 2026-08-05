#include "KeireInternal/Build/PlayerSupportCatalog.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::uint64_t MaximumCatalogBytes = 4ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t MaximumPackageBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] bool SafeSegment(const std::string_view value)
        {
            return !value.empty() && value != "." && value != ".." && value.size() <= 128 &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isalnum(character) || character == '.' || character == '-' || character == '_'; });
        }

        [[nodiscard]] bool SafeVersion(const std::string_view value)
        {
            return !value.empty() && value.size() <= 128 &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isalnum(character) || character == '.' || character == '-' || character == '+'; });
        }

        [[nodiscard]] PlayerPlatform ParsePlatform(const std::string_view value)
        {
            if (value == "windows")
                return PlayerPlatform::Windows;
            if (value == "linux")
                return PlayerPlatform::Linux;
            if (value == "macos")
                return PlayerPlatform::MacOS;
            throw std::invalid_argument("Build Support catalog contains an unknown platform.");
        }

        [[nodiscard]] PlayerArchitecture ParseArchitecture(const std::string_view value)
        {
            if (value == "x86_64")
                return PlayerArchitecture::X86_64;
            if (value == "arm64")
                return PlayerArchitecture::Arm64;
            throw std::invalid_argument("Build Support catalog contains an unknown architecture.");
        }

        [[nodiscard]] std::string ReleaseRoot(const std::string_view catalogUrl)
        {
            if (!catalogUrl.starts_with("https://"))
                throw std::invalid_argument("Build Support catalogs must be loaded from HTTPS.");
            const auto separator = catalogUrl.find_last_of('/');
            if (separator == std::string_view::npos || separator < 8)
                throw std::invalid_argument("Build Support catalog URL is invalid.");
            return std::string(catalogUrl.substr(0, separator + 1));
        }

        void DownloadVerified(const std::string_view url, const std::filesystem::path& destination,
                              const std::uint64_t maximumBytes, const std::uint64_t expectedSize,
                              const std::string_view expectedSha256, const PlayerSupportInstallCallbacks& callbacks)
        {
            if (!url.starts_with("https://") || destination.empty())
                throw std::invalid_argument("Build Support downloads require HTTPS and a destination path.");
            std::filesystem::create_directories(destination.parent_path());
            const auto temporary = PathWithSuffix(destination, ".download-" + AssetId::Generate().ToString());
            try
            {
                DownloadHttpsFileNative(url, temporary, maximumBytes, callbacks);
                const auto size = std::filesystem::file_size(temporary);
                if ((expectedSize != 0 && size != expectedSize) || size > maximumBytes)
                    throw std::runtime_error("Build Support download size verification failed.");
                if (!expectedSha256.empty() && DigestToString(Sha256File(temporary, maximumBytes)) != expectedSha256)
                    throw std::runtime_error("Build Support download SHA-256 verification failed.");
                PublishFileAtomically(temporary, destination);
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw;
            }
        }
    } // namespace

    std::string DefaultPlayerSupportCatalogUrl(const std::string_view repositorySlug,
                                               const std::string_view engineVersion)
    {
        const auto separator = repositorySlug.find('/');
        if (separator == std::string_view::npos || repositorySlug.find('/', separator + 1) != std::string_view::npos ||
            !SafeSegment(repositorySlug.substr(0, separator)) || !SafeSegment(repositorySlug.substr(separator + 1)) ||
            !SafeVersion(engineVersion))
            throw std::invalid_argument("Build Support release identity is invalid.");
        return "https://github.com/" + std::string(repositorySlug) + "/releases/download/v" +
               std::string(engineVersion) + "/player-support-catalog.json";
    }

    PlayerSupportCatalog LoadPlayerSupportCatalog(const std::filesystem::path& path, const std::string_view sourceUrl,
                                                  const std::string_view expectedEngineVersion)
    {
        const auto encoded = Json::parse(ReadTextFile(path, MaximumCatalogBytes));
        if (encoded.value("schemaVersion", 0U) != 1U || !encoded.contains("packages") ||
            !encoded.at("packages").is_array() || encoded.at("packages").size() > 32)
            throw std::invalid_argument("Build Support catalog schema is invalid.");
        PlayerSupportCatalog result{.EngineVersion = encoded.value("engineVersion", std::string{})};
        if (result.EngineVersion != expectedEngineVersion)
            throw std::invalid_argument("Build Support catalog engine version does not match this Kéire build.");
        const auto root = ReleaseRoot(sourceUrl);
        for (const auto& value : encoded.at("packages"))
        {
            PlayerSupportCatalogEntry entry{.Id = value.value("id", std::string{}),
                                            .Platform = ParsePlatform(value.value("platform", std::string{})),
                                            .Architecture =
                                                ParseArchitecture(value.value("architecture", std::string{})),
                                            .File = value.value("file", std::string{}),
                                            .Size = value.value("size", 0ULL),
                                            .Sha256 = value.value("sha256", std::string{})};
            if (!SafeSegment(entry.Id) || !SafeSegment(entry.File) || !entry.File.ends_with(".keireplayersupport") ||
                entry.Size == 0 || entry.Size > MaximumPackageBytes ||
                !std::regex_match(entry.Sha256, std::regex("[0-9a-f]{64}")))
                throw std::invalid_argument("Build Support catalog package entry is invalid.");
            entry.Url = root + entry.File;
            result.Packages.push_back(std::move(entry));
        }
        std::ranges::sort(result.Packages, {}, &PlayerSupportCatalogEntry::Id);
        const auto duplicate =
            std::ranges::adjacent_find(result.Packages, std::ranges::equal_to{}, &PlayerSupportCatalogEntry::Id);
        if (duplicate != result.Packages.end())
            throw std::invalid_argument("Build Support catalog contains duplicate package IDs.");
        return result;
    }

    PlayerSupportCatalog FetchPlayerSupportCatalog(const std::string_view repositorySlug,
                                                   const std::string_view engineVersion,
                                                   const std::filesystem::path& destination,
                                                   const PlayerSupportInstallCallbacks& callbacks)
    {
        const auto url = DefaultPlayerSupportCatalogUrl(repositorySlug, engineVersion);
        DownloadVerified(url, destination, MaximumCatalogBytes, 0, {}, callbacks);
        return LoadPlayerSupportCatalog(destination, url, engineVersion);
    }

    void DownloadPlayerSupportPackage(const PlayerSupportCatalogEntry& entry, const std::filesystem::path& destination,
                                      const PlayerSupportInstallCallbacks& callbacks)
    {
        DownloadVerified(entry.Url, destination, MaximumPackageBytes, entry.Size, entry.Sha256, callbacks);
    }
} // namespace Keire::Detail
