#include "KeireInternal/Build/PlayerPackage.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-player-package-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Keire::PlayerSettings Settings()
    {
        return {.ProductName = "Sample Game",
                .Version = "1.2.3",
                .ApplicationIdentifier = "com.example.sample-game",
                .WindowTitle = "Sample Game"};
    }

    [[nodiscard]] Keire::PlayerBuildProfile Profile(const Keire::PlayerPlatform platform)
    {
        return {.Id = Keire::AssetId::Parse("40000000-0000-4000-8000-000000000004"),
                .Name = "Test Profile",
                .Platform = platform,
                .Architecture = Keire::PlayerArchitecture::X86_64,
                .Configuration = Keire::PlayerBuildConfiguration::Development,
                .IncludeSymbols = true,
                .OutputSlug = "Test-Profile"};
    }

    [[nodiscard]] Keire::Detail::ResolvedPlayerSupport Support(const std::filesystem::path& root,
                                                               const Keire::PlayerPlatform platform,
                                                               const std::filesystem::path& executable,
                                                               const std::filesystem::path& bundle = {})
    {
        Keire::Detail::PlayerSupportVariant variant{.Root = ".", .Executable = executable, .Bundle = bundle};
        return {.Manifest = {.Id = "test",
                             .EngineVersion = "0.1.0",
                             .Platform = platform,
                             .Architecture = Keire::PlayerArchitecture::X86_64,
                             .ModuleFingerprint = "modules",
                             .Variants = {variant}},
                .Variant = std::move(variant),
                .InstallationRoot = root};
    }

    void Write(const std::filesystem::path& path, const std::string_view contents = "fixture")
    {
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteTextFileAtomically(path, contents);
    }

    void WritePortableExecutable(const std::filesystem::path& path, const std::size_t size = 512)
    {
        constexpr std::size_t peOffset = 0x80;
        constexpr std::size_t optionalOffset = peOffset + 24;
        std::vector<std::byte> bytes(std::max(size, optionalOffset + 70U));
        const auto write16 = [&](const std::size_t offset, const std::uint16_t value)
        {
            bytes[offset] = static_cast<std::byte>(value & 0xffU);
            bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
        };
        const auto write32 = [&](const std::size_t offset, const std::uint32_t value)
        {
            for (std::size_t index = 0; index < 4; ++index)
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
        };
        bytes[0] = std::byte{'M'};
        bytes[1] = std::byte{'Z'};
        write32(0x3c, peOffset);
        bytes[peOffset] = std::byte{'P'};
        bytes[peOffset + 1] = std::byte{'E'};
        write16(peOffset + 20, 0xf0);
        write16(optionalOffset, 0x20b);
        write16(optionalOffset + 68, 3);
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteFileAtomically(path, bytes);
    }

    [[nodiscard]] std::uint16_t ReadPortableExecutableSubsystem(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        stream.seekg(0x3c);
        std::uint32_t peOffset = 0;
        stream.read(reinterpret_cast<char*>(&peOffset), sizeof(peOffset));
        stream.seekg(static_cast<std::streamoff>(peOffset) + 24 + 68);
        std::uint16_t subsystem = 0;
        stream.read(reinterpret_cast<char*>(&subsystem), sizeof(subsystem));
        return subsystem;
    }

    void ConfigureSigningHook(Keire::PlayerBuildProfile& profile, const std::filesystem::path& directory,
                              const std::string_view mode)
    {
        profile.Signing.Policy = Keire::PlayerSigningPolicy::Required;
        profile.Signing.TimeoutSeconds = 1;
#if defined(_WIN32)
        const auto script = directory / "signing-hook.ps1";
        Write(script, R"(param([string]$Mode, [string]$Root, [string]$Request, [string]$Response)
if ($Mode -eq 'timeout') { Start-Sleep -Seconds 5 }
if ($Mode -eq 'modify') { [IO.File]::WriteAllText((Join-Path $Root 'unexpected.txt'), 'changed') }
if ($Mode -eq 'malformed') { $body = '{}' } else { $body = '{"schemaVersion":1,"success":true,"modifiedFiles":[]}' }
[IO.File]::WriteAllText($Response, $body, [Text.UTF8Encoding]::new($false))
)");
        char* systemRoot = nullptr;
        std::size_t systemRootLength = 0;
        (void)_dupenv_s(&systemRoot, &systemRootLength, "SystemRoot");
        profile.Signing.Command = std::filesystem::path(systemRoot ? systemRoot : "C:\\Windows") /
                                  "System32/WindowsPowerShell/v1.0/powershell.exe";
        std::free(systemRoot);
        profile.Signing.Arguments = {"-NoProfile",
                                     "-ExecutionPolicy",
                                     "Bypass",
                                     "-File",
                                     Keire::Detail::PathToUtf8(script),
                                     std::string(mode),
                                     Keire::Detail::PathToUtf8(directory / "stage")};
#else
        const auto script = directory / "signing-hook.sh";
        Write(script, R"(#!/usr/bin/env sh
mode="$1"
root="$2"
shift 2
response=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --response) response="$2"; shift 2 ;;
        *) shift ;;
    esac
done
[ "$mode" != timeout ] || sleep 5
[ "$mode" != modify ] || printf changed > "$root/unexpected.txt"
if [ "$mode" = malformed ]; then
    printf '{}' > "$response"
else
    printf '{"schemaVersion":1,"success":true,"modifiedFiles":[]}' > "$response"
fi
)");
        profile.Signing.Command = "/bin/sh";
        profile.Signing.Arguments = {Keire::Detail::PathToUtf8(script), std::string(mode),
                                     Keire::Detail::PathToUtf8(directory / "stage")};
#endif
    }

    template <typename Callback> [[nodiscard]] std::string RuntimeErrorMessage(Callback&& callback)
    {
        try
        {
            callback();
        }
        catch (const std::runtime_error& error)
        {
            return error.what();
        }
        return {};
    }
} // namespace

TEST_CASE("Windows player packages rename the executable and self-describe their runtime layout")
{
    TemporaryDirectory directory;
    const auto supportRoot = directory.Path / "support";
    WritePortableExecutable(supportRoot / "KeireRuntime.exe");
    Write(supportRoot / "Managed/hostfxr.dll");
    const auto profile = Profile(Keire::PlayerPlatform::Windows);
    const auto layout = Keire::Detail::AssemblePlayerPackage(Support(supportRoot, profile.Platform, "KeireRuntime.exe"),
                                                             Settings(), profile, directory.Path / "stage");
    CHECK(layout.Executable.filename() == "Sample Game.exe");
    CHECK(std::filesystem::is_regular_file(layout.Executable));
    CHECK(ReadPortableExecutableSubsystem(layout.Executable) == 2);
    CHECK(std::filesystem::is_regular_file(layout.Root / "Sample Game.ico"));
    CHECK(std::filesystem::is_regular_file(layout.Descriptor));
    Write(layout.Content / "runtime-manifest.json", "{}");

    const auto loaded = Keire::Detail::LoadPackagedPlayerConfiguration(layout.Executable);
    REQUIRE(loaded.has_value());
    CHECK(loaded->Settings.ProductName == "Sample Game");
    CHECK(loaded->Content == layout.Content);
    CHECK(loaded->ManagedRuntime == layout.ManagedRuntime);
}

TEST_CASE("Linux player packages include a desktop entry")
{
    TemporaryDirectory directory;
    const auto supportRoot = directory.Path / "support";
    Write(supportRoot / "KeireRuntime");
    Write(supportRoot / "Managed/libhostfxr.so");
    const auto profile = Profile(Keire::PlayerPlatform::Linux);
    const auto layout = Keire::Detail::AssemblePlayerPackage(Support(supportRoot, profile.Platform, "KeireRuntime"),
                                                             Settings(), profile, directory.Path / "stage");
    CHECK(layout.Executable.filename() == "Sample Game");
    CHECK(std::filesystem::is_regular_file(layout.Root / "Sample Game.desktop"));
    CHECK(
        std::filesystem::is_regular_file(layout.Root / "share/icons/hicolor/256x256/apps/com.example.sample-game.png"));
    CHECK(
        std::filesystem::is_regular_file(layout.Root / "share/icons/hicolor/512x512/apps/com.example.sample-game.png"));
}

TEST_CASE("macOS player packages publish a conventionally named app bundle")
{
    TemporaryDirectory directory;
    const auto supportRoot = directory.Path / "support";
    Write(supportRoot / "KeireRuntime.app/Contents/MacOS/KeireRuntime");
    Write(supportRoot / "KeireRuntime.app/Contents/Resources/Managed/libhostfxr.dylib");
    const auto profile = Profile(Keire::PlayerPlatform::MacOS);
    const auto layout = Keire::Detail::AssemblePlayerPackage(
        Support(supportRoot, profile.Platform, "KeireRuntime.app/Contents/MacOS/KeireRuntime", "KeireRuntime.app"),
        Settings(), profile, directory.Path / "stage");
    CHECK(layout.Executable == directory.Path / "stage/Sample Game.app/Contents/MacOS/Sample Game");
    CHECK(std::filesystem::is_regular_file(layout.Root / "Sample Game.app/Contents/Info.plist"));
    CHECK(std::filesystem::is_regular_file(layout.Root / "Sample Game.app/Contents/Resources/PlayerIcon.icns"));
    CHECK(layout.Descriptor == directory.Path / "stage/Sample Game.app/Contents/Resources/PlayerBuild.json");
}

TEST_CASE("Windows player branding writes only within manifest-declared bounded slots")
{
    TemporaryDirectory directory;
    const auto supportRoot = directory.Path / "support";
    WritePortableExecutable(supportRoot / "KeireRuntime.exe", 2U * 1024U * 1024U);
    Write(supportRoot / "Managed/hostfxr.dll");
    const auto profile = Profile(Keire::PlayerPlatform::Windows);
    auto support = Support(supportRoot, profile.Platform, "KeireRuntime.exe");
    support.Manifest.BrandingSlots = {
        {.Path = "KeireRuntime.exe", .Kind = "windows-icon", .Offset = 1024, .Size = 1024U * 1024U},
        {.Path = "KeireRuntime.exe", .Kind = "windows-version", .Offset = 1024U * 1024U + 1024U, .Size = 4096}};
    const auto layout = Keire::Detail::AssemblePlayerPackage(support, Settings(), profile, directory.Path / "stage");
    std::ifstream executable(layout.Executable, std::ios::binary);
    REQUIRE(executable.good());
    executable.seekg(1024);
    std::uint32_t iconBytes = 0;
    executable.read(reinterpret_cast<char*>(&iconBytes), sizeof(iconBytes));
    CHECK(iconBytes > 0);
    CHECK(iconBytes < 1024U * 1024U);
    executable.seekg(1024U * 1024U + 1024U);
    std::uint32_t versionBytes = 0;
    executable.read(reinterpret_cast<char*>(&versionBytes), sizeof(versionBytes));
    CHECK(versionBytes > 0);
    CHECK(versionBytes < 4096U);
}

#if defined(_WIN32)
TEST_CASE("Windows player packaging embeds the generated fallback icon in the executable")
{
    TemporaryDirectory directory;
    std::array<wchar_t, 32768> executablePath{};
    const auto length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    REQUIRE(length > 0);
    REQUIRE(length < executablePath.size());
    const auto supportRoot = directory.Path / "support";
    std::filesystem::create_directories(supportRoot / "Managed");
    std::filesystem::copy_file(std::filesystem::path(executablePath.data()), supportRoot / "KeireRuntime.exe");
    Write(supportRoot / "Managed/hostfxr.dll");
    const auto profile = Profile(Keire::PlayerPlatform::Windows);
    auto support = Support(supportRoot, profile.Platform, "KeireRuntime.exe");
    support.Manifest.BrandingSlots = {
        {.Path = "KeireRuntime.exe", .Kind = "windows-resource-update", .Offset = 0, .Size = 1}};
    const auto layout = Keire::Detail::AssemblePlayerPackage(support, Settings(), profile, directory.Path / "stage");

    const auto module =
        LoadLibraryExW(layout.Executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    REQUIRE(module != nullptr);
    const auto iconGroup = FindResourceW(module, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(14));
    CHECK(iconGroup != nullptr);
    if (iconGroup)
        CHECK(SizeofResource(module, iconGroup) > 6);
    CHECK(FreeLibrary(module));
}
#endif

TEST_CASE("Player package publication replaces complete builds and rejects invalid staging")
{
    TemporaryDirectory directory;
    const auto output = directory.Path / "Build/Profile";
    Write(output / "old.txt", "old");
    const auto staging = directory.Path / "Build/.staging/new";
    Write(staging / "new.txt", "new");
    Keire::Detail::PublishPlayerPackage(staging, output);
    CHECK_FALSE(std::filesystem::exists(output / "old.txt"));
    CHECK(std::filesystem::is_regular_file(output / "new.txt"));
    CHECK_THROWS_AS(Keire::Detail::PublishPlayerPackage(directory.Path / "missing", output), std::invalid_argument);
    CHECK(std::filesystem::is_regular_file(output / "new.txt"));
}

TEST_CASE("Player signing hooks enforce responses modifications and timeouts")
{
    TemporaryDirectory directory;
    const auto root = directory.Path / "stage";
    Write(root / "Sample Game.exe", "unsigned");
    const Keire::Detail::PlayerPackageLayout layout{.Root = root, .Executable = root / "Sample Game.exe"};

    auto profile = Profile(Keire::PlayerPlatform::Windows);
    ConfigureSigningHook(profile, directory.Path, "success");
    CHECK_NOTHROW(Keire::Detail::RunPlayerSigningHook(directory.Path, Settings(), profile, layout));

    ConfigureSigningHook(profile, directory.Path, "modify");
    CHECK(RuntimeErrorMessage([&] { Keire::Detail::RunPlayerSigningHook(directory.Path, Settings(), profile, layout); })
              .find("outside its declared response") != std::string::npos);
    std::filesystem::remove(root / "unexpected.txt");

    ConfigureSigningHook(profile, directory.Path, "malformed");
    CHECK(RuntimeErrorMessage([&] { Keire::Detail::RunPlayerSigningHook(directory.Path, Settings(), profile, layout); })
              .find("malformed") != std::string::npos);

    ConfigureSigningHook(profile, directory.Path, "timeout");
    CHECK(RuntimeErrorMessage([&] { Keire::Detail::RunPlayerSigningHook(directory.Path, Settings(), profile, layout); })
              .find("timed out") != std::string::npos);
}
