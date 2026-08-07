#include "KeireHubRuntime/HubSettingsStore.h"

#include "Persistence.h"

#include <array>
#include <limits>
#include <string_view>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumSettingsBytes = 256 * 1024;
        constexpr std::size_t MaximumLegacySettingsBytes = 16 * 1024;
        constexpr std::size_t MaximumPathBytes = 4096;
        constexpr std::size_t MaximumDiscoveryRoots = 32;
        constexpr std::uint64_t MaximumBandwidthBytesPerSecond = 16ULL * 1024ULL * 1024ULL * 1024ULL;

        template <typename Enum, typename Name, std::size_t Size>
        [[nodiscard]] std::optional<Enum> ParseEnum(const std::string_view value,
                                                    const std::array<std::pair<Name, Enum>, Size>& values)
        {
            for (const auto& [name, result] : values)
            {
                if (name == value)
                    return result;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ToString(const HubPage value) noexcept
        {
            constexpr std::array names{"home",  "projects",  "installs", "templates",
                                       "learn", "resources", "licenses", "settings"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view ToString(const HubAppearance value) noexcept
        {
            constexpr std::array names{"system", "dark", "light"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view ToString(const ProjectView value) noexcept
        {
            constexpr std::array names{"table", "cards"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view ToString(const ProjectSort value) noexcept
        {
            constexpr std::array names{"lastOpened", "name", "created", "modified", "version", "status", "size"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view ToString(const ProxyMode value) noexcept
        {
            constexpr std::array names{"system", "custom"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] HubStatus ValidatePath(const std::filesystem::path& path, const std::string_view name,
                                             const bool mayBeEmpty = true)
        {
            const auto encoded = Detail::PathToUtf8(path);
            if ((!mayBeEmpty && encoded.empty()) || encoded.size() > MaximumPathBytes ||
                (!encoded.empty() && !path.is_absolute()))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A Hub setting contains an invalid path.",
                                           .AffectedItem = std::string(name)});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool IsValidProxyUrl(const std::string_view url) noexcept
        {
            if (!(url.starts_with("http://") || url.starts_with("https://")) || url.size() < 8 ||
                url.find_first_of("\r\n") != std::string_view::npos || url.find('\0') != std::string_view::npos)
                return false;
            const auto authority = url.find("://") + 3;
            return authority < url.size() && url[authority] != '/' && url[authority] != '?' && url[authority] != '#';
        }

        [[nodiscard]] HubStatus Validate(const HubSettings& settings)
        {
            if (settings.StartupPage < HubPage::Home || settings.StartupPage > HubPage::Settings ||
                settings.Appearance < HubAppearance::System || settings.Appearance > HubAppearance::Light ||
                settings.ProjectsView < ProjectView::Table || settings.ProjectsView > ProjectView::Cards ||
                settings.ProjectsSort < ProjectSort::LastOpened || settings.ProjectsSort > ProjectSort::Size ||
                settings.NetworkProxyMode < ProxyMode::System || settings.NetworkProxyMode > ProxyMode::Custom)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A Hub setting contains an unsupported option."});
            }
            if (settings.ConcurrentDownloads == 0 || settings.ConcurrentDownloads > 8)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "Concurrent downloads must be between 1 and 8.",
                                           .AffectedItem = "concurrentDownloads"});
            }
            if (settings.ProjectDiscoveryRoots.size() > MaximumDiscoveryRoots)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "Too many project discovery roots were configured.",
                                           .AffectedItem = "projectDiscoveryRoots"});
            }
            for (const auto& [path, name] : std::array<std::pair<const std::filesystem::path*, std::string_view>, 3>{
                     std::pair{&settings.DefaultProjectLocation, "defaultProjectLocation"},
                     std::pair{&settings.DefaultEditorRoot, "defaultEditorRoot"},
                     std::pair{&settings.CacheRoot, "cacheRoot"}})
            {
                if (const auto status = ValidatePath(*path, name); !status)
                    return status;
            }
            if (const auto status = ValidatePath(settings.TemporaryRoot, "temporaryRoot"); !status)
                return status;
            for (const auto& path : settings.ProjectDiscoveryRoots)
            {
                if (const auto status = ValidatePath(path, "projectDiscoveryRoots", false); !status)
                    return status;
            }
            if (settings.CustomProxyUrl.size() > 2048 || settings.LogLevel.size() > 16 ||
                (settings.DevelopmentServiceUrl && settings.DevelopmentServiceUrl->size() > 2048) ||
                (settings.DevelopmentTrustedKey && settings.DevelopmentTrustedKey->size() > 256))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A text Hub setting exceeds its allowed length."});
            }
            if (settings.BandwidthLimitBytesPerSecond > MaximumBandwidthBytesPerSecond ||
                (settings.NetworkProxyMode == ProxyMode::Custom && !IsValidProxyUrl(settings.CustomProxyUrl)) ||
                settings.DevelopmentServiceUrl.has_value() != settings.DevelopmentTrustedKey.has_value())
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A Hub network setting is invalid.",
                                           .AffectedItem = "network"});
            }
            constexpr std::array validLogLevels{"trace", "debug", "info", "warning", "error"};
            bool validLogLevel = false;
            for (const auto value : validLogLevels)
                validLogLevel |= settings.LogLevel == value;
            if (!validLogLevel)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The configured log level is not supported.",
                                           .AffectedItem = "logLevel"});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] Detail::Json Serialize(const HubSettings& settings)
        {
            Detail::Json discoveryRoots = Detail::Json::array();
            for (const auto& root : settings.ProjectDiscoveryRoots)
                discoveryRoots.push_back(Detail::PathToUtf8(root));
            Detail::Json document{{"schemaVersion", HubSettings::CurrentSchemaVersion},
                                  {"firstRunCompleted", settings.FirstRunCompleted},
                                  {"general",
                                   {{"startupPage", ToString(settings.StartupPage)},
                                    {"keepRunningAfterEditorLaunch", settings.KeepRunningAfterEditorLaunch},
                                    {"closeToTray", settings.CloseToTray},
                                    {"appearance", ToString(settings.Appearance)},
                                    {"reducedMotion", settings.ReducedMotion},
                                    {"checkForUpdates", settings.CheckForUpdates}}},
                                  {"projects",
                                   {{"view", ToString(settings.ProjectsView)},
                                    {"sort", ToString(settings.ProjectsSort)},
                                    {"defaultLocation", Detail::PathToUtf8(settings.DefaultProjectLocation)},
                                    {"discoveryRoots", std::move(discoveryRoots)},
                                    {"removeMissingAutomatically", settings.RemoveMissingProjectsAutomatically},
                                    {"confirmRemoval", settings.ConfirmProjectRemoval}}},
                                  {"installs",
                                   {{"defaultRoot", Detail::PathToUtf8(settings.DefaultEditorRoot)},
                                    {"cacheRoot", Detail::PathToUtf8(settings.CacheRoot)},
                                    {"temporaryRoot", Detail::PathToUtf8(settings.TemporaryRoot)},
                                    {"concurrentDownloads", settings.ConcurrentDownloads},
                                    {"retainVerifiedCache", settings.RetainVerifiedCache},
                                    {"channels",
                                     {{"stable", settings.EnableStableChannel},
                                      {"preRelease", settings.EnablePreReleaseChannel},
                                      {"nightly", settings.EnableNightlyChannel}}}}},
                                  {"network",
                                   {{"offline", settings.OfflineMode},
                                    {"proxyMode", ToString(settings.NetworkProxyMode)},
                                    {"customProxyUrl", settings.CustomProxyUrl},
                                    {"bandwidthLimitBytesPerSecond", settings.BandwidthLimitBytesPerSecond}}},
                                  {"advanced", {{"logLevel", settings.LogLevel}}}};
            if (settings.DevelopmentServiceUrl)
                document["advanced"]["developmentServiceUrl"] = *settings.DevelopmentServiceUrl;
            if (settings.DevelopmentTrustedKey)
                document["advanced"]["developmentTrustedKey"] = *settings.DevelopmentTrustedKey;
            return document;
        }

        template <typename T> [[nodiscard]] T Required(const Detail::Json& object, const char* key)
        {
            return object.at(key).get<T>();
        }

        [[nodiscard]] HubResult<HubSettings> Parse(const Detail::Json& document)
        {
            try
            {
                if (!document.is_object() ||
                    Required<std::uint32_t>(document, "schemaVersion") != HubSettings::CurrentSchemaVersion)
                {
                    return HubResult<HubSettings>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This Hub settings file uses an unsupported schema.",
                         .AffectedItem = "settings"});
                }
                HubSettings result;
                result.FirstRunCompleted = Required<bool>(document, "firstRunCompleted");
                const auto& general = document.at("general");
                const auto& projects = document.at("projects");
                const auto& installs = document.at("installs");
                const auto& network = document.at("network");
                const auto& advanced = document.at("advanced");

                constexpr std::array pages{
                    std::pair{"home", HubPage::Home},         std::pair{"projects", HubPage::Projects},
                    std::pair{"installs", HubPage::Installs}, std::pair{"templates", HubPage::Templates},
                    std::pair{"learn", HubPage::Learn},       std::pair{"resources", HubPage::Resources},
                    std::pair{"licenses", HubPage::Licenses}, std::pair{"settings", HubPage::Settings}};
                constexpr std::array appearances{std::pair{"system", HubAppearance::System},
                                                 std::pair{"dark", HubAppearance::Dark},
                                                 std::pair{"light", HubAppearance::Light}};
                constexpr std::array views{std::pair{"table", ProjectView::Table},
                                           std::pair{"cards", ProjectView::Cards}};
                constexpr std::array sorts{std::pair{"lastOpened", ProjectSort::LastOpened},
                                           std::pair{"name", ProjectSort::Name},
                                           std::pair{"created", ProjectSort::Created},
                                           std::pair{"modified", ProjectSort::Modified},
                                           std::pair{"version", ProjectSort::Version},
                                           std::pair{"status", ProjectSort::Status},
                                           std::pair{"size", ProjectSort::Size}};
                constexpr std::array proxies{std::pair{"system", ProxyMode::System},
                                             std::pair{"custom", ProxyMode::Custom}};
                const auto page = ParseEnum(Required<std::string>(general, "startupPage"), pages);
                const auto appearance = ParseEnum(Required<std::string>(general, "appearance"), appearances);
                const auto view = ParseEnum(Required<std::string>(projects, "view"), views);
                const auto sort = ParseEnum(Required<std::string>(projects, "sort"), sorts);
                const auto proxy = ParseEnum(Required<std::string>(network, "proxyMode"), proxies);
                if (!page || !appearance || !view || !sort || !proxy)
                    throw std::invalid_argument("Settings contain an unknown enumeration value.");

                result.StartupPage = *page;
                result.KeepRunningAfterEditorLaunch = Required<bool>(general, "keepRunningAfterEditorLaunch");
                result.CloseToTray = Required<bool>(general, "closeToTray");
                result.Appearance = *appearance;
                result.ReducedMotion = Required<bool>(general, "reducedMotion");
                result.CheckForUpdates = Required<bool>(general, "checkForUpdates");
                result.ProjectsView = *view;
                result.ProjectsSort = *sort;
                result.DefaultProjectLocation =
                    Detail::PathFromUtf8(Required<std::string>(projects, "defaultLocation"));
                const auto& roots = projects.at("discoveryRoots");
                if (!roots.is_array() || roots.size() > MaximumDiscoveryRoots)
                    throw std::invalid_argument("Invalid project discovery roots.");
                for (const auto& root : roots)
                    result.ProjectDiscoveryRoots.push_back(Detail::PathFromUtf8(root.get<std::string>()));
                result.RemoveMissingProjectsAutomatically = Required<bool>(projects, "removeMissingAutomatically");
                result.ConfirmProjectRemoval = Required<bool>(projects, "confirmRemoval");
                result.DefaultEditorRoot = Detail::PathFromUtf8(Required<std::string>(installs, "defaultRoot"));
                result.CacheRoot = Detail::PathFromUtf8(Required<std::string>(installs, "cacheRoot"));
                result.TemporaryRoot = Detail::PathFromUtf8(Required<std::string>(installs, "temporaryRoot"));
                result.ConcurrentDownloads = Required<std::uint32_t>(installs, "concurrentDownloads");
                result.RetainVerifiedCache = Required<bool>(installs, "retainVerifiedCache");
                const auto& channels = installs.at("channels");
                result.EnableStableChannel = Required<bool>(channels, "stable");
                result.EnablePreReleaseChannel = Required<bool>(channels, "preRelease");
                result.EnableNightlyChannel = Required<bool>(channels, "nightly");
                result.OfflineMode = Required<bool>(network, "offline");
                result.NetworkProxyMode = *proxy;
                result.CustomProxyUrl = Required<std::string>(network, "customProxyUrl");
                result.BandwidthLimitBytesPerSecond = Required<std::uint64_t>(network, "bandwidthLimitBytesPerSecond");
                result.LogLevel = Required<std::string>(advanced, "logLevel");
                if (advanced.contains("developmentServiceUrl"))
                    result.DevelopmentServiceUrl = Required<std::string>(advanced, "developmentServiceUrl");
                if (advanced.contains("developmentTrustedKey"))
                    result.DevelopmentTrustedKey = Required<std::string>(advanced, "developmentTrustedKey");
                if (const auto status = Validate(result); !status)
                    return HubResult<HubSettings>::Failure(status.Error());
                return HubResult<HubSettings>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<HubSettings>::Failure({.Code = HubErrorCode::InvalidData,
                                                        .Message = "The Hub settings file is malformed.",
                                                        .AffectedItem = "settings",
                                                        .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] HubResult<HubSettings> ParseLegacy(const std::string_view text)
        {
            HubSettings result;
            std::size_t offset = 0;
            while (offset < text.size())
            {
                auto end = text.find('\n', offset);
                if (end == std::string_view::npos)
                    end = text.size();
                auto line = text.substr(offset, end - offset);
                if (line.ends_with('\r'))
                    line.remove_suffix(1);
                if (!line.empty())
                {
                    const auto equals = line.find('=');
                    if (equals == std::string_view::npos)
                    {
                        return HubResult<HubSettings>::Failure(
                            {.Code = HubErrorCode::MigrationFailed,
                             .Message = "Legacy Hub preferences could not be migrated.",
                             .AffectedItem = "HubUi.settings"});
                    }
                    const auto key = line.substr(0, equals);
                    const auto value = line.substr(equals + 1);
                    if (key == "view")
                    {
                        if (value == "cards")
                            result.ProjectsView = ProjectView::Cards;
                        else if (value == "list" || value == "table")
                            result.ProjectsView = ProjectView::Table;
                        else
                            return HubResult<HubSettings>::Failure(
                                {.Code = HubErrorCode::MigrationFailed,
                                 .Message = "Legacy project view is not recognized."});
                    }
                    else if (key == "sort")
                    {
                        if (value == "name")
                            result.ProjectsSort = ProjectSort::Name;
                        else if (value == "status")
                            result.ProjectsSort = ProjectSort::Status;
                        else if (value == "last-opened" || value == "last_opened" || value == "lastOpened")
                            result.ProjectsSort = ProjectSort::LastOpened;
                        else
                            return HubResult<HubSettings>::Failure(
                                {.Code = HubErrorCode::MigrationFailed,
                                 .Message = "Legacy project sort is not recognized."});
                    }
                }
                offset = end + (end < text.size() ? 1 : 0);
            }
            return HubResult<HubSettings>::Success(std::move(result));
        }
    } // namespace

    HubSettingsStore::HubSettingsStore(std::filesystem::path settingsPath, std::filesystem::path legacySettingsPath)
        : m_Path(std::move(settingsPath)), m_LegacyPath(std::move(legacySettingsPath)),
          m_Snapshot(std::make_shared<const HubSettings>())
    {
    }

    HubStatus HubSettingsStore::Load()
    {
        m_MigratedLegacy = false;
        if (!std::filesystem::exists(m_Path))
        {
            if (m_LegacyPath.empty() || !std::filesystem::exists(m_LegacyPath))
            {
                m_Snapshot = std::make_shared<const HubSettings>();
                return HubStatus::Success();
            }
            auto legacy = Detail::ReadTextFile(m_LegacyPath, MaximumLegacySettingsBytes);
            if (!legacy)
                return HubStatus::Failure(legacy.Error());
            auto migrated = ParseLegacy(legacy.Value());
            if (!migrated)
                return HubStatus::Failure(migrated.Error());
            auto status = Save(std::move(migrated).Value());
            if (status)
                m_MigratedLegacy = true;
            return status;
        }

        auto document = Detail::ReadJsonFile(m_Path, MaximumSettingsBytes);
        if (!document)
        {
            if (document.Error().Code == HubErrorCode::InvalidData)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(document.Error());
        }
        auto settings = Parse(document.Value());
        if (!settings)
        {
            if (settings.Error().Code != HubErrorCode::UnsupportedSchema)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(settings.Error());
        }
        m_Snapshot = std::make_shared<const HubSettings>(std::move(settings).Value());
        return HubStatus::Success();
    }

    HubStatus HubSettingsStore::Save(HubSettings settings)
    {
        if (const auto status = Validate(settings); !status)
            return status;
        if (auto status = Detail::WriteJsonFileAtomically(m_Path, Serialize(settings)); !status)
            return status;
        m_Snapshot = std::make_shared<const HubSettings>(std::move(settings));
        return HubStatus::Success();
    }

    std::shared_ptr<const HubSettings> HubSettingsStore::Snapshot() const noexcept { return m_Snapshot; }

    bool HubSettingsStore::MigratedLegacySettings() const noexcept { return m_MigratedLegacy; }

    const std::filesystem::path& HubSettingsStore::Path() const noexcept { return m_Path; }
} // namespace KeireHub
