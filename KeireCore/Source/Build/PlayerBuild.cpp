#include "Keire/Build/PlayerBuild.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumSettingsBytes = 1024U * 1024U;
        constexpr std::string_view DefaultProfileId = "10000000-0000-4000-8000-000000000001";

        [[nodiscard]] std::filesystem::path PlayerSettingsPath(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
                throw std::invalid_argument("Player settings require a project root.");
            return projectRoot / "ProjectSettings" / "Player.keiresettings";
        }

        [[nodiscard]] std::filesystem::path BuildProfilesPath(const std::filesystem::path& projectRoot)
        {
            if (projectRoot.empty())
                throw std::invalid_argument("Player build profiles require a project root.");
            return projectRoot / "ProjectSettings" / "BuildProfiles.keiresettings";
        }

        [[nodiscard]] std::string Lowercase(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] bool IsSafeText(const std::string_view value, const std::size_t maximum)
        {
            return !value.empty() && value.size() <= maximum &&
                   std::ranges::none_of(value, [](const unsigned char character)
                                        { return character < 0x20U || character == 0x7FU; });
        }

        [[nodiscard]] bool IsSafePathComponent(const std::string_view value)
        {
            if (!IsSafeText(value, 128) || value == "." || value == ".." || value.ends_with(' ') ||
                value.ends_with('.'))
                return false;
            constexpr std::string_view reserved = "<>:\"/\\|?*";
            if (std::ranges::any_of(value, [&](const char character)
                                    { return reserved.find(character) != std::string_view::npos; }))
                return false;

            const auto base = Lowercase(std::string(value.substr(0, value.find('.'))));
            constexpr std::array<std::string_view, 4> deviceNames{"con", "prn", "aux", "nul"};
            if (std::ranges::find(deviceNames, base) != deviceNames.end())
                return false;
            if (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '1' &&
                base[3] <= '9')
                return false;
            return true;
        }

        [[nodiscard]] bool IsDigits(const std::string_view value)
        {
            return !value.empty() && std::ranges::all_of(value, [](const unsigned char character)
                                                         { return std::isdigit(character) != 0; });
        }

        [[nodiscard]] bool IsSemVerIdentifierList(const std::string_view value, const bool rejectNumericLeadingZero)
        {
            if (value.empty() || value.starts_with('.') || value.ends_with('.') ||
                value.find("..") != std::string_view::npos)
                return false;
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('.', begin);
                const auto identifier =
                    value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (identifier.empty() ||
                    std::ranges::any_of(identifier, [](const unsigned char character)
                                        { return std::isalnum(character) == 0 && character != '-'; }) ||
                    (rejectNumericLeadingZero && IsDigits(identifier) && identifier.size() > 1 &&
                     identifier.starts_with('0')))
                    return false;
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return true;
        }

        [[nodiscard]] bool IsSemanticVersion(const std::string_view value)
        {
            const auto plus = value.find('+');
            if (plus != std::string_view::npos && (value.find('+', plus + 1) != std::string_view::npos ||
                                                   !IsSemVerIdentifierList(value.substr(plus + 1), false)))
                return false;
            const auto coreAndPrerelease = value.substr(0, plus);
            const auto dash = coreAndPrerelease.find('-');
            if (dash != std::string_view::npos && !IsSemVerIdentifierList(coreAndPrerelease.substr(dash + 1), true))
                return false;
            const auto core = coreAndPrerelease.substr(0, dash);
            std::array<std::string_view, 3> components;
            std::size_t begin = 0;
            for (std::size_t index = 0; index < components.size(); ++index)
            {
                const auto end = core.find('.', begin);
                if ((index < 2 && end == std::string_view::npos) || (index == 2 && end != std::string_view::npos))
                    return false;
                components[index] =
                    core.substr(begin, end == std::string_view::npos ? core.size() - begin : end - begin);
                begin = end == std::string_view::npos ? core.size() : end + 1;
            }
            return std::ranges::all_of(
                components, [](const auto component)
                { return IsDigits(component) && (component.size() == 1 || !component.starts_with('0')); });
        }

        [[nodiscard]] bool IsApplicationIdentifier(const std::string_view value)
        {
            if (value.size() > 255 || value.find('.') == std::string_view::npos || value.starts_with('.') ||
                value.ends_with('.') || value.find("..") != std::string_view::npos)
                return false;
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('.', begin);
                const auto segment =
                    value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (segment.empty() || !std::isalnum(static_cast<unsigned char>(segment.front())) ||
                    !std::isalnum(static_cast<unsigned char>(segment.back())) ||
                    std::ranges::any_of(segment, [](const unsigned char character)
                                        { return std::isalnum(character) == 0 && character != '-'; }))
                    return false;
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return true;
        }

        [[nodiscard]] bool IsEnvironmentName(const std::string_view value)
        {
            if (value.empty() || (value.front() != '_' && !std::isalpha(static_cast<unsigned char>(value.front()))))
                return false;
            return std::ranges::all_of(value, [](const unsigned char character)
                                       { return character == '_' || std::isalnum(character) != 0; });
        }

        template <typename Enum>
        [[nodiscard]] Enum ParseEnum(const std::string& value,
                                     const std::initializer_list<std::pair<std::string_view, Enum>> values,
                                     const std::string_view name)
        {
            const auto found =
                std::ranges::find_if(values, [&](const auto& candidate) { return candidate.first == value; });
            if (found == values.end())
                throw std::runtime_error("Unknown " + std::string(name) + ": " + value);
            return found->second;
        }

        [[nodiscard]] AssetId ParseOptionalAsset(const Json& value)
        {
            return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
        }

        [[nodiscard]] Json EncodeAsset(const AssetId value) { return value ? Json(value.ToString()) : Json(nullptr); }
    } // namespace

    PlayerPlatform HostPlayerPlatform() noexcept
    {
#if defined(_WIN32)
        return PlayerPlatform::Windows;
#elif defined(__APPLE__)
        return PlayerPlatform::MacOS;
#else
        return PlayerPlatform::Linux;
#endif
    }

    PlayerArchitecture HostPlayerArchitecture() noexcept
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        return PlayerArchitecture::Arm64;
#else
        return PlayerArchitecture::X86_64;
#endif
    }

    std::string_view ToString(const PlayerPlatform platform) noexcept
    {
        switch (platform)
        {
        case PlayerPlatform::Windows:
            return "windows";
        case PlayerPlatform::Linux:
            return "linux";
        case PlayerPlatform::MacOS:
            return "macos";
        }
        return "unknown";
    }

    std::string_view ToString(const PlayerArchitecture architecture) noexcept
    {
        switch (architecture)
        {
        case PlayerArchitecture::X86_64:
            return "x86_64";
        case PlayerArchitecture::Arm64:
            return "arm64";
        }
        return "unknown";
    }

    std::string_view ToString(const PlayerBuildConfiguration configuration) noexcept
    {
        switch (configuration)
        {
        case PlayerBuildConfiguration::Development:
            return "development";
        case PlayerBuildConfiguration::Release:
            return "release";
        case PlayerBuildConfiguration::Dist:
            return "dist";
        }
        return "unknown";
    }

    std::string_view ToString(const PlayerSigningPolicy policy) noexcept
    {
        switch (policy)
        {
        case PlayerSigningPolicy::Disabled:
            return "disabled";
        case PlayerSigningPolicy::SignIfConfigured:
            return "sign-if-configured";
        case PlayerSigningPolicy::Required:
            return "required";
        }
        return "unknown";
    }

    PlayerSettings DefaultPlayerSettings(const ProjectDescriptor& project)
    {
        if (!project.Id || project.Name.empty())
            throw std::invalid_argument("Default player settings require a valid project descriptor.");
        auto compactId = project.Id.ToString();
        std::erase(compactId, '-');
        return {.ProductName = project.Name,
                .Version = "0.1.0",
                .ApplicationIdentifier = "com.keire.project." + compactId,
                .WindowTitle = project.Name};
    }

    PlayerBuildProfiles DefaultPlayerBuildProfiles()
    {
        PlayerBuildProfile profile;
        profile.Id = AssetId::Parse(DefaultProfileId);
        profile.Name = "Desktop Development";
        profile.Platform = HostPlayerPlatform();
        profile.Architecture = HostPlayerArchitecture();
        profile.Configuration = PlayerBuildConfiguration::Development;
        profile.IncludeSymbols = true;
        profile.OutputSlug = "Desktop-Development";
        return {.ActiveProfile = profile.Id, .Profiles = {std::move(profile)}};
    }

    void ValidatePlayerSettings(const PlayerSettings& settings)
    {
        if (settings.SchemaVersion != PlayerSettingsSchemaVersion)
            throw std::invalid_argument("Player settings use an unsupported schema.");
        if (!IsSafePathComponent(settings.ProductName))
            throw std::invalid_argument("Player product name must be a safe cross-platform file name.");
        if (!IsSemanticVersion(settings.Version))
            throw std::invalid_argument("Player version must be a Semantic Version 2.0.0 value.");
        if (!IsApplicationIdentifier(settings.ApplicationIdentifier))
            throw std::invalid_argument("Player application identifier must use reverse-DNS segments.");
        if (!IsSafeText(settings.WindowTitle, 256))
            throw std::invalid_argument("Player window title must be non-empty and contain no control characters.");
    }

    void ValidatePlayerBuildProfiles(const PlayerBuildProfiles& profiles)
    {
        if (profiles.SchemaVersion != PlayerBuildProfilesSchemaVersion || profiles.Profiles.empty() ||
            !profiles.ActiveProfile)
            throw std::invalid_argument("Player build profiles require a supported schema and an active profile.");

        std::unordered_set<std::string> ids;
        std::unordered_set<std::string> names;
        std::unordered_set<std::string> outputs;
        bool activeFound = false;
        for (const auto& profile : profiles.Profiles)
        {
            if (!profile.Id || !IsSafeText(profile.Name, 64) || !IsSafePathComponent(profile.OutputSlug))
                throw std::invalid_argument("Player build profiles require IDs, safe names, and safe output slugs.");
            if (!ids.emplace(profile.Id.ToString()).second || !names.emplace(Lowercase(profile.Name)).second ||
                !outputs.emplace(Lowercase(profile.OutputSlug)).second)
                throw std::invalid_argument("Player build profile IDs, names, and output slugs must be unique.");
            activeFound = activeFound || profile.Id == profiles.ActiveProfile;

            const auto& signing = profile.Signing;
            if (signing.TimeoutSeconds == 0 || signing.TimeoutSeconds > 3600 || signing.Arguments.size() > 64 ||
                signing.RequiredEnvironment.size() > 64)
                throw std::invalid_argument("Player signing hook limits are invalid.");
            if (signing.Policy == PlayerSigningPolicy::Required && signing.Command.empty())
                throw std::invalid_argument("Required player signing needs a configured command.");
            if (std::ranges::any_of(signing.Arguments, [](const auto& argument) { return argument.size() > 4096; }) ||
                std::ranges::any_of(signing.RequiredEnvironment,
                                    [](const auto& name) { return !IsEnvironmentName(name); }))
                throw std::invalid_argument("Player signing hook arguments or environment names are invalid.");
        }
        if (!activeFound)
            throw std::invalid_argument("The active player build profile does not exist.");
    }

    PlayerSettings LoadPlayerSettings(const std::filesystem::path& projectRoot, const ProjectDescriptor& project)
    {
        const auto path = PlayerSettingsPath(projectRoot);
        if (!std::filesystem::is_regular_file(path))
            return DefaultPlayerSettings(project);
        const auto document = Json::parse(Detail::ReadTextFile(path, MaximumSettingsBytes));
        if (!document.is_object())
            throw std::runtime_error("Player settings root must be an object.");
        PlayerSettings result;
        result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        result.ProductName = document.at("productName").get<std::string>();
        result.Version = document.at("version").get<std::string>();
        result.ApplicationIdentifier = document.at("applicationIdentifier").get<std::string>();
        result.WindowTitle = document.at("windowTitle").get<std::string>();
        result.WindowsIcon = ParseOptionalAsset(document.value("windowsIcon", Json(nullptr)));
        result.LinuxIcon = ParseOptionalAsset(document.value("linuxIcon", Json(nullptr)));
        result.MacOSIcon = ParseOptionalAsset(document.value("macOSIcon", Json(nullptr)));
        ValidatePlayerSettings(result);
        return result;
    }

    void SavePlayerSettings(const std::filesystem::path& projectRoot, const PlayerSettings& settings)
    {
        ValidatePlayerSettings(settings);
        const Json document{{"schemaVersion", settings.SchemaVersion},
                            {"productName", settings.ProductName},
                            {"version", settings.Version},
                            {"applicationIdentifier", settings.ApplicationIdentifier},
                            {"windowTitle", settings.WindowTitle},
                            {"windowsIcon", EncodeAsset(settings.WindowsIcon)},
                            {"linuxIcon", EncodeAsset(settings.LinuxIcon)},
                            {"macOSIcon", EncodeAsset(settings.MacOSIcon)}};
        Detail::WriteTextFileAtomically(PlayerSettingsPath(projectRoot), document.dump(2) + '\n');
    }

    PlayerBuildProfiles LoadPlayerBuildProfiles(const std::filesystem::path& projectRoot)
    {
        const auto path = BuildProfilesPath(projectRoot);
        if (!std::filesystem::is_regular_file(path))
            return DefaultPlayerBuildProfiles();
        const auto document = Json::parse(Detail::ReadTextFile(path, MaximumSettingsBytes));
        if (!document.is_object() || !document.at("profiles").is_array())
            throw std::runtime_error("Player build profiles root is invalid.");

        PlayerBuildProfiles result;
        result.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        result.ActiveProfile = AssetId::Parse(document.at("activeProfile").get<std::string>());
        for (const auto& encoded : document.at("profiles"))
        {
            PlayerBuildProfile profile;
            profile.Id = AssetId::Parse(encoded.at("id").get<std::string>());
            profile.Name = encoded.at("name").get<std::string>();
            profile.Platform = ParseEnum<PlayerPlatform>(encoded.at("platform").get<std::string>(),
                                                         {{"windows", PlayerPlatform::Windows},
                                                          {"linux", PlayerPlatform::Linux},
                                                          {"macos", PlayerPlatform::MacOS}},
                                                         "player platform");
            profile.Architecture = ParseEnum<PlayerArchitecture>(
                encoded.at("architecture").get<std::string>(),
                {{"x86_64", PlayerArchitecture::X86_64}, {"arm64", PlayerArchitecture::Arm64}}, "player architecture");
            profile.Configuration =
                ParseEnum<PlayerBuildConfiguration>(encoded.at("configuration").get<std::string>(),
                                                    {{"development", PlayerBuildConfiguration::Development},
                                                     {"release", PlayerBuildConfiguration::Release},
                                                     {"dist", PlayerBuildConfiguration::Dist}},
                                                    "player configuration");
            profile.IncludeSymbols = encoded.at("includeSymbols").get<bool>();
            profile.OutputSlug = encoded.at("outputSlug").get<std::string>();
            const auto& signing = encoded.at("signing");
            profile.Signing.Policy =
                ParseEnum<PlayerSigningPolicy>(signing.at("policy").get<std::string>(),
                                               {{"disabled", PlayerSigningPolicy::Disabled},
                                                {"sign-if-configured", PlayerSigningPolicy::SignIfConfigured},
                                                {"required", PlayerSigningPolicy::Required}},
                                               "player signing policy");
            profile.Signing.Command = Detail::PathFromUtf8(signing.value("command", std::string{}));
            profile.Signing.Arguments = signing.value("arguments", std::vector<std::string>{});
            profile.Signing.RequiredEnvironment = signing.value("requiredEnvironment", std::vector<std::string>{});
            profile.Signing.TimeoutSeconds = signing.value("timeoutSeconds", 600U);
            result.Profiles.push_back(std::move(profile));
        }
        ValidatePlayerBuildProfiles(result);
        return result;
    }

    void SavePlayerBuildProfiles(const std::filesystem::path& projectRoot, const PlayerBuildProfiles& profiles)
    {
        ValidatePlayerBuildProfiles(profiles);
        Json encodedProfiles = Json::array();
        for (const auto& profile : profiles.Profiles)
        {
            encodedProfiles.push_back({{"id", profile.Id.ToString()},
                                       {"name", profile.Name},
                                       {"platform", ToString(profile.Platform)},
                                       {"architecture", ToString(profile.Architecture)},
                                       {"configuration", ToString(profile.Configuration)},
                                       {"includeSymbols", profile.IncludeSymbols},
                                       {"outputSlug", profile.OutputSlug},
                                       {"signing",
                                        {{"policy", ToString(profile.Signing.Policy)},
                                         {"command", Detail::PathToUtf8(profile.Signing.Command)},
                                         {"arguments", profile.Signing.Arguments},
                                         {"requiredEnvironment", profile.Signing.RequiredEnvironment},
                                         {"timeoutSeconds", profile.Signing.TimeoutSeconds}}}});
        }
        const Json document{{"schemaVersion", profiles.SchemaVersion},
                            {"activeProfile", profiles.ActiveProfile.ToString()},
                            {"profiles", std::move(encodedProfiles)}};
        Detail::WriteTextFileAtomically(BuildProfilesPath(projectRoot), document.dump(2) + '\n');
    }

    const PlayerBuildProfile& FindPlayerBuildProfile(const PlayerBuildProfiles& profiles, const AssetId id)
    {
        const auto found = std::ranges::find(profiles.Profiles, id, &PlayerBuildProfile::Id);
        if (found == profiles.Profiles.end())
            throw std::invalid_argument("Player build profile ID was not found.");
        return *found;
    }

    const PlayerBuildProfile& FindPlayerBuildProfile(const PlayerBuildProfiles& profiles, const std::string_view name)
    {
        const auto lowered = Lowercase(std::string(name));
        const auto found = std::ranges::find_if(profiles.Profiles, [&](const auto& profile)
                                                { return Lowercase(profile.Name) == lowered; });
        if (found == profiles.Profiles.end())
            throw std::invalid_argument("Player build profile name was not found.");
        return *found;
    }
} // namespace Keire
