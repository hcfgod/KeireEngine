#include "KeireHubRuntime/PackageArchive.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include "KeireInternal/Build/PlayerSupportPackage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using Json = KeireHub::Detail::Json;

    enum class Command
    {
        CreateEditor,
        CreateBuildSupport,
        CreateHubInstaller
    };

    struct Arguments final
    {
        Command SelectedCommand = Command::CreateEditor;
        std::filesystem::path PayloadRoot;
        std::filesystem::path HubManifest;
        std::filesystem::path Installer;
        std::filesystem::path PlayerSupportPackage;
        std::filesystem::path Output;
        std::filesystem::path ManifestOutput;
        std::string SignatureKeyId;
        std::string Channel;
    };

    [[nodiscard]] std::filesystem::path Path(const std::string_view value)
    {
        const auto* begin = reinterpret_cast<const char8_t*>(value.data());
        return std::filesystem::path(std::u8string(begin, begin + value.size()));
    }

    [[nodiscard]] std::string Text(const std::filesystem::path& value)
    {
        const auto encoded = value.generic_u8string();
        return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    }

    [[nodiscard]] std::string Lower(std::string value);

    [[nodiscard]] Arguments Parse(const int count, const char* const* values)
    {
        if (count == 2 && std::string_view(values[1]) == "--help")
        {
            std::cout << "Usage: KeireHubPackagePublisher create-editor --payload-root <directory> "
                         "--output <archive.keirepackage> --manifest-output <manifest.json> "
                         "--signature-key-id <ed25519-key-id>\n"
                         "       KeireHubPackagePublisher create-build-support "
                         "--player-support-package <module.keireplayersupport> --channel <channel> "
                         "--output <archive.keirepackage> --manifest-output <manifest.json> "
                         "--signature-key-id <ed25519-key-id>\n"
                         "       KeireHubPackagePublisher create-hub-installer "
                         "--hub-manifest <hub-package.json> --installer <native-installer> "
                         "--manifest-output <manifest.json> --signature-key-id <ed25519-key-id>\n";
            std::exit(0);
        }
        if (count < 2)
            throw std::invalid_argument("A publisher command is required. Use --help for usage.");
        const auto command = std::string_view(values[1]);
        if (command != "create-editor" && command != "create-build-support" && command != "create-hub-installer")
            throw std::invalid_argument("The publisher command is unsupported. Use --help for usage.");
        std::map<std::string, std::string, std::less<>> options;
        for (int index = 2; index < count; index += 2)
        {
            if (index + 1 >= count || !std::string_view(values[index]).starts_with("--"))
                throw std::invalid_argument("Every publisher option requires one value.");
            if (!options.emplace(values[index], values[index + 1]).second)
                throw std::invalid_argument("A publisher option was repeated.");
        }
        constexpr std::array editorNames{"--payload-root", "--output", "--manifest-output", "--signature-key-id"};
        constexpr std::array componentNames{"--player-support-package", "--channel", "--output", "--manifest-output",
                                            "--signature-key-id"};
        constexpr std::array installerNames{"--hub-manifest", "--installer", "--manifest-output", "--signature-key-id"};
        const bool validOptions =
            (command == "create-editor" && options.size() == editorNames.size() &&
             std::ranges::all_of(editorNames, [&](const std::string_view name) { return options.contains(name); })) ||
            (command == "create-build-support" && options.size() == componentNames.size() &&
             std::ranges::all_of(componentNames,
                                 [&](const std::string_view name) { return options.contains(name); })) ||
            (command == "create-hub-installer" && options.size() == installerNames.size() &&
             std::ranges::all_of(installerNames, [&](const std::string_view name) { return options.contains(name); }));
        if (!validOptions)
        {
            throw std::invalid_argument("The publisher options are incomplete or unsupported. Use --help for usage.");
        }
        Arguments result;
        result.SelectedCommand = command == "create-editor"          ? Command::CreateEditor
                                 : command == "create-build-support" ? Command::CreateBuildSupport
                                                                     : Command::CreateHubInstaller;
        if (result.SelectedCommand == Command::CreateEditor)
        {
            result.PayloadRoot = std::filesystem::absolute(Path(options.at("--payload-root"))).lexically_normal();
            result.Output = std::filesystem::absolute(Path(options.at("--output"))).lexically_normal();
        }
        else if (result.SelectedCommand == Command::CreateBuildSupport)
        {
            result.PlayerSupportPackage =
                std::filesystem::absolute(Path(options.at("--player-support-package"))).lexically_normal();
            result.Output = std::filesystem::absolute(Path(options.at("--output"))).lexically_normal();
            result.Channel = Lower(options.at("--channel"));
            if (result.Channel != "stable" && result.Channel != "preview" && result.Channel != "nightly")
                throw std::invalid_argument("The Build Support channel is invalid.");
        }
        else
        {
            result.HubManifest = std::filesystem::absolute(Path(options.at("--hub-manifest"))).lexically_normal();
            result.Installer = std::filesystem::absolute(Path(options.at("--installer"))).lexically_normal();
        }
        result.ManifestOutput = std::filesystem::absolute(Path(options.at("--manifest-output"))).lexically_normal();
        result.SignatureKeyId = options.at("--signature-key-id");
        if (!KeireHub::Detail::IsDistributionKeyId(result.SignatureKeyId))
            throw std::invalid_argument("The package signature key ID is invalid.");
        return result;
    }

    [[nodiscard]] std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    [[nodiscard]] std::uint32_t FileMode(const std::filesystem::path& path, const std::string_view platform)
    {
        if (platform == "windows")
            return 0644U;
        std::error_code error;
        const auto permissions = std::filesystem::status(path, error).permissions();
        if (error)
            throw std::runtime_error("A package file's permissions could not be inspected: " + Text(path));
        constexpr auto executable = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                    std::filesystem::perms::others_exec;
        return (permissions & executable) == std::filesystem::perms::none ? 0644U : 0755U;
    }

    [[nodiscard]] KeireHub::PackageFile InventoryFile(const std::filesystem::path& root,
                                                      const std::filesystem::path& relative,
                                                      const std::string_view platform)
    {
        const auto full = root / relative;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(full, error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
            throw std::runtime_error("The package contains an unsafe inventory file: " + Text(relative));
        const auto size = std::filesystem::file_size(full, error);
        if (error)
            throw std::runtime_error("An editor package file size could not be read: " + Text(relative));
        auto digest = KeireHub::Detail::Sha256File(full, KeireHub::PackageArchiveLimits::MaximumFileBytes);
        if (!digest)
            throw std::runtime_error(digest.Error().Message + " " + digest.Error().TechnicalDetails);
        return {
            .Path = relative, .SizeBytes = size, .Sha256 = std::move(digest).Value(), .Mode = FileMode(full, platform)};
    }

    [[nodiscard]] KeireHub::PackageManifest ReadEditorManifest(const Arguments& arguments)
    {
        const auto productManifestPath = arguments.PayloadRoot / "editor-package.json";
        auto document = KeireHub::Detail::ReadJsonFile(productManifestPath, std::size_t{8U} * 1024U * 1024U);
        if (!document)
            throw std::runtime_error(document.Error().Message + " " + document.Error().TechnicalDetails);
        const auto& value = document.Value();
        if (!value.is_object() || value.at("schemaVersion").get<std::uint32_t>() != 2U ||
            value.at("artifact").get<std::string>() != "editor" || value.at("dirty").get<bool>() ||
            value.at("developmentArtifact").get<bool>() || !value.at("files").is_array())
        {
            throw std::runtime_error("The editor product manifest must be a clean schema-2 distribution manifest.");
        }

        auto version = KeireHub::SemanticVersion::Parse(value.at("version").get<std::string>());
        if (!version)
            throw std::runtime_error(version.Error().Message);
        KeireHub::PackageManifest result;
        result.Id = value.at("packageId").get<std::string>();
        result.Version = std::move(version).Value();
        result.Kind = KeireHub::PackageKind::Editor;
        result.DisplayName = value.at("project").get<std::string>() + " Editor " + result.Version.ToString();
        result.Channel = Lower(value.at("channel").get<std::string>());
        result.Platform = Lower(value.at("platform").get<std::string>());
        result.Architecture = Lower(value.at("architecture").get<std::string>());
        result.SignatureKeyId = arguments.SignatureKeyId;
        for (const auto& license : value.at("licenseReferences"))
            result.LicenseReferences.push_back(license.get<std::string>());

        std::map<std::string, KeireHub::PackageFile, std::less<>> files;
        for (const auto& item : value.at("files"))
        {
            const auto relative = Path(item.at("path").get<std::string>());
            auto file = InventoryFile(arguments.PayloadRoot, relative, result.Platform);
            if (file.SizeBytes != item.at("sizeBytes").get<std::uint64_t>() ||
                file.Sha256 != item.at("sha256").get<std::string>())
            {
                throw std::runtime_error("The editor product inventory does not match: " + Text(relative));
            }
            if (!files.emplace(Text(relative), std::move(file)).second)
                throw std::runtime_error("The editor product inventory contains a duplicate path.");
        }
        auto productManifest = InventoryFile(arguments.PayloadRoot, "editor-package.json", result.Platform);
        files.emplace("editor-package.json", std::move(productManifest));
        result.Files.reserve(files.size());
        for (auto& [path, file] : files)
        {
            (void)path;
            result.InstalledSizeBytes += file.SizeBytes;
            result.Files.push_back(std::move(file));
        }
        // Archive transport size and digest are unknowable until the final compressed bytes exist. The archive
        // encoder validates all other fields with its canonical placeholders, then replaces them after writing.
        if (auto canonical = KeireHub::EncodePackageArchiveManifest(result); !canonical)
            throw std::runtime_error(canonical.Error().Message + " " + canonical.Error().TechnicalDetails);
        return result;
    }

    [[nodiscard]] KeireHub::PackageManifest
    ReadBuildSupportManifest(const Arguments& arguments, const Keire::Detail::PlayerSupportManifest& support,
                             const std::filesystem::path& payloadRoot)
    {
        auto version = KeireHub::SemanticVersion::Parse(support.EngineVersion);
        if (!version)
            throw std::runtime_error("The Build Support engine version is not semantic.");
        auto compatibility = KeireHub::VersionConstraint::Parse('=' + support.EngineVersion);
        if (!compatibility)
            throw std::runtime_error(compatibility.Error().Message);

        const auto platform = std::string(Keire::ToString(support.Platform));
        const auto architecture = std::string(Keire::ToString(support.Architecture));
        KeireHub::PackageManifest result;
        result.Id = "keire-build-support-" + platform + '-' + architecture;
        result.Version = std::move(version).Value();
        result.Kind = KeireHub::PackageKind::BuildSupport;
        result.DisplayName = platform + " " + architecture + " Build Support";
        result.Channel = arguments.Channel;
        // Build targets are cross-installable; compatibility is expressed against the editor engine version.
        result.Platform = "any";
        result.Architecture = "any";
        result.EngineCompatibility = std::move(compatibility).Value();
        result.SignatureKeyId = arguments.SignatureKeyId;

        std::map<std::string, KeireHub::PackageFile, std::less<>> files;
        for (std::filesystem::recursive_directory_iterator iterator(payloadRoot), end; iterator != end; ++iterator)
        {
            const auto status = iterator->symlink_status();
            if (std::filesystem::is_symlink(status) ||
                (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
                throw std::runtime_error("The Build Support payload contains an unsafe filesystem entry.");
            if (std::filesystem::is_directory(status))
                continue;
            const auto relative = iterator->path().lexically_relative(payloadRoot).lexically_normal();
            auto file = InventoryFile(payloadRoot, relative, platform);
            if (!files.emplace(Text(relative), std::move(file)).second)
                throw std::runtime_error("The Build Support payload contains a duplicate path.");
        }
        for (auto& [path, file] : files)
        {
            (void)path;
            result.InstalledSizeBytes += file.SizeBytes;
            result.Files.push_back(std::move(file));
        }
        if (auto canonical = KeireHub::EncodePackageArchiveManifest(result); !canonical)
            throw std::runtime_error(canonical.Error().Message + " " + canonical.Error().TechnicalDetails);
        return result;
    }

    [[nodiscard]] KeireHub::PackageManifest ReadHubInstallerManifest(const Arguments& arguments)
    {
        std::error_code error;
        const auto manifestStatus = std::filesystem::symlink_status(arguments.HubManifest, error);
        if (error || !std::filesystem::is_regular_file(manifestStatus) || std::filesystem::is_symlink(manifestStatus))
            throw std::invalid_argument("The Hub product manifest is missing or unsafe.");
        const auto installerStatus = std::filesystem::symlink_status(arguments.Installer, error);
        if (error || !std::filesystem::is_regular_file(installerStatus) || std::filesystem::is_symlink(installerStatus))
            throw std::invalid_argument("The native Hub installer is missing or unsafe.");

        auto document = KeireHub::Detail::ReadJsonFile(arguments.HubManifest, std::size_t{8U} * 1024U * 1024U);
        if (!document)
            throw std::runtime_error(document.Error().Message + " " + document.Error().TechnicalDetails);
        const auto& value = document.Value();
        if (!value.is_object() || value.at("schemaVersion").get<std::uint32_t>() != 2U ||
            value.at("artifact").get<std::string>() != "hub" || value.at("dirty").get<bool>() ||
            value.at("developmentArtifact").get<bool>())
        {
            throw std::runtime_error("The Hub product manifest must be a clean schema-2 distribution manifest.");
        }

        auto version = KeireHub::SemanticVersion::Parse(value.at("version").get<std::string>());
        if (!version)
            throw std::runtime_error(version.Error().Message);
        KeireHub::PackageManifest result;
        result.Id = value.at("packageId").get<std::string>();
        result.Version = std::move(version).Value();
        result.Kind = KeireHub::PackageKind::HubInstaller;
        result.DisplayName = value.at("project").get<std::string>() + " Hub " + result.Version.ToString();
        result.Channel = Lower(value.at("channel").get<std::string>());
        result.Platform = Lower(value.at("platform").get<std::string>());
        result.Architecture = Lower(value.at("architecture").get<std::string>());
        result.SignatureKeyId = arguments.SignatureKeyId;

        const auto extension = Lower(Text(arguments.Installer.extension()));
        const bool expectedExtension = (result.Platform == "windows" && extension == ".exe") ||
                                       (result.Platform == "macos" && extension == ".dmg") ||
                                       (result.Platform == "linux" && extension == ".deb");
        if (!expectedExtension || (result.Architecture != "x86_64" && result.Architecture != "arm64"))
            throw std::invalid_argument("The native Hub installer does not match a supported host identity.");

        const auto size = std::filesystem::file_size(arguments.Installer, error);
        if (error || size == 0 || size > KeireHub::PackageArchiveLimits::MaximumFileBytes)
            throw std::invalid_argument("The native Hub installer size is invalid.");
        auto digest =
            KeireHub::Detail::Sha256File(arguments.Installer, KeireHub::PackageArchiveLimits::MaximumFileBytes);
        if (!digest)
            throw std::runtime_error(digest.Error().Message + " " + digest.Error().TechnicalDetails);
        result.ArtifactSizeBytes = size;
        result.ArtifactSha256 = digest.Value();
        result.InstalledSizeBytes = size;
        result.Files.push_back({.Path = arguments.Installer.filename(),
                                .SizeBytes = size,
                                .Sha256 = std::move(digest).Value(),
                                .Mode = 0644U});
        if (auto canonical = KeireHub::EncodePackageManifest(result); !canonical)
            throw std::runtime_error(canonical.Error().Message + " " + canonical.Error().TechnicalDetails);
        return result;
    }

    void WriteManifest(const std::filesystem::path& path, const KeireHub::PackageManifest& manifest)
    {
        auto encoded = KeireHub::EncodePackageManifest(manifest);
        if (!encoded)
            throw std::runtime_error(encoded.Error().Message + " " + encoded.Error().TechnicalDetails);
        auto parsed = KeireHub::ParsePackageManifest(encoded.Value());
        if (!parsed)
            throw std::runtime_error("The published package manifest failed its parser round trip.");
        auto roundTrip = KeireHub::EncodePackageManifest(parsed.Value());
        if (!roundTrip || roundTrip.Value() != encoded.Value())
            throw std::runtime_error("The published package manifest changed during its parser round trip.");
        if (const auto status = KeireHub::Detail::WriteTextFileAtomically(path, encoded.Value()); !status)
            throw std::runtime_error(status.Error().Message + " " + status.Error().TechnicalDetails);
    }
} // namespace

namespace
{
    [[nodiscard]] int Run(const int count, const char* const* values)
    {
        try
        {
            const auto arguments = Parse(count, values);
            if (!std::filesystem::is_directory(arguments.ManifestOutput.parent_path()) ||
                std::filesystem::exists(arguments.ManifestOutput))
            {
                throw std::invalid_argument("The publisher input or output path is unavailable.");
            }
            if (arguments.SelectedCommand == Command::CreateHubInstaller)
            {
                const auto manifest = ReadHubInstallerManifest(arguments);
                WriteManifest(arguments.ManifestOutput, manifest);
                std::cout << "Published " << manifest.Id << '@' << manifest.Version.ToString()
                          << " native installer manifest\nArtifact: " << Text(arguments.Installer)
                          << "\nSHA-256: " << manifest.ArtifactSha256 << "\nBytes: " << manifest.ArtifactSizeBytes
                          << '\n';
                return 0;
            }
            if (arguments.SelectedCommand == Command::CreateBuildSupport)
            {
                if (!std::filesystem::is_regular_file(arguments.PlayerSupportPackage) ||
                    !std::filesystem::is_directory(arguments.Output.parent_path()) ||
                    arguments.ManifestOutput == arguments.Output || std::filesystem::exists(arguments.Output))
                {
                    throw std::invalid_argument("The publisher input or output path is unavailable.");
                }
                // Keep the entire staging root short: Build Support contains deep .NET SDK paths that otherwise reach
                // the legacy Windows MAX_PATH boundary even when long paths are enabled for the final installation.
                const auto stagingIdentity = std::hash<std::string>{}(Text(arguments.Output.lexically_normal()));
                const auto payloadRoot =
                    std::filesystem::temp_directory_path() / (".keire-bs-" + std::to_string(stagingIdentity));
                if (std::filesystem::exists(payloadRoot))
                    throw std::invalid_argument("A Build Support publication staging directory already exists.");
                try
                {
                    const auto support = Keire::Detail::InstallPlayerSupportPackage(arguments.PlayerSupportPackage, {},
                                                                                    {}, payloadRoot / "BuildSupport");
                    auto archive = KeireHub::WritePackageArchive(
                        {.Manifest = ReadBuildSupportManifest(arguments, support.Manifest, payloadRoot),
                         .PayloadRoot = payloadRoot,
                         .Output = arguments.Output});
                    if (!archive)
                        throw std::runtime_error(archive.Error().Message + " " + archive.Error().TechnicalDetails);
                    try
                    {
                        WriteManifest(arguments.ManifestOutput, archive.Value().Manifest);
                    }
                    catch (...)
                    {
                        std::error_code archiveError;
                        std::filesystem::remove(arguments.Output, archiveError);
                        throw;
                    }
                    std::error_code cleanupError;
                    std::filesystem::remove_all(payloadRoot, cleanupError);
                    if (cleanupError)
                        throw std::filesystem::filesystem_error("Could not remove Build Support publisher staging.",
                                                                payloadRoot, cleanupError);
                    std::cout << "Published " << archive.Value().Manifest.Id << '@'
                              << archive.Value().Manifest.Version.ToString() << "\nArchive: " << Text(arguments.Output)
                              << "\nSHA-256: " << archive.Value().ArchiveSha256
                              << "\nBytes: " << archive.Value().ArchiveSizeBytes << '\n';
                    return 0;
                }
                catch (...)
                {
                    std::error_code cleanupError;
                    std::filesystem::remove_all(payloadRoot, cleanupError);
                    throw;
                }
            }
            if (!std::filesystem::is_directory(arguments.PayloadRoot) ||
                !std::filesystem::is_directory(arguments.Output.parent_path()) ||
                arguments.ManifestOutput == arguments.Output || std::filesystem::exists(arguments.Output))
            {
                throw std::invalid_argument("The publisher input or output path is unavailable.");
            }
            auto archive = KeireHub::WritePackageArchive({.Manifest = ReadEditorManifest(arguments),
                                                          .PayloadRoot = arguments.PayloadRoot,
                                                          .Output = arguments.Output});
            if (!archive)
                throw std::runtime_error(archive.Error().Message + " " + archive.Error().TechnicalDetails);
            try
            {
                WriteManifest(arguments.ManifestOutput, archive.Value().Manifest);
            }
            catch (...)
            {
                std::error_code cleanupError;
                std::filesystem::remove(arguments.Output, cleanupError);
                throw;
            }
            std::cout << "Published " << archive.Value().Manifest.Id << '@'
                      << archive.Value().Manifest.Version.ToString() << "\nArchive: " << Text(arguments.Output)
                      << "\nSHA-256: " << archive.Value().ArchiveSha256
                      << "\nBytes: " << archive.Value().ArchiveSizeBytes << '\n';
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "KeireHubPackagePublisher: " << error.what() << '\n';
            return 1;
        }
    }

#if defined(_WIN32)
    [[nodiscard]] std::string Utf8(const std::wstring_view value)
    {
        const auto encoded = std::filesystem::path(value).generic_u8string();
        return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    }
#endif
} // namespace

#if defined(_WIN32)
int wmain(const int count, const wchar_t* const* values)
{
    std::vector<std::string> encodedValues;
    encodedValues.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
        encodedValues.push_back(Utf8(values[index]));
    std::vector<const char*> pointers;
    pointers.reserve(encodedValues.size());
    for (const auto& value : encodedValues)
        pointers.push_back(value.c_str());
    return Run(count, pointers.data());
}
#else
int main(const int count, const char* const* values) { return Run(count, values); }
#endif
