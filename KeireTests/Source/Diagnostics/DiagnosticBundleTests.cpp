#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleInternal.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    class TestDirectory final
    {
      public:
        explicit TestDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }
        ~TestDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset)
    {
        REQUIRE(offset + 2U <= bytes.size());
        return std::to_integer<std::uint16_t>(bytes[offset]) |
               (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U);
    }

    [[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset)
    {
        REQUIRE(offset + 4U <= bytes.size());
        return std::to_integer<std::uint32_t>(bytes[offset]) |
               (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
               (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
               (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }

    [[nodiscard]] std::map<std::string, std::string, std::less<>>
    StoredEntries(const std::span<const std::byte> archive)
    {
        std::map<std::string, std::string, std::less<>> result;
        std::size_t cursor = 0;
        while (cursor + 4U <= archive.size() && Read32(archive, cursor) == 0x04034b50U)
        {
            REQUIRE(cursor + 30U <= archive.size());
            CHECK(Read16(archive, cursor + 8U) == 0U);
            const auto size = Read32(archive, cursor + 18U);
            const auto nameSize = Read16(archive, cursor + 26U);
            const auto extraSize = Read16(archive, cursor + 28U);
            const auto nameOffset = cursor + 30U;
            const auto contentsOffset = nameOffset + nameSize + extraSize;
            REQUIRE(contentsOffset + size <= archive.size());
            const std::string name(reinterpret_cast<const char*>(archive.data() + nameOffset), nameSize);
            const std::string contents(reinterpret_cast<const char*>(archive.data() + contentsOffset), size);
            REQUIRE(result.emplace(name, contents).second);
            cursor = contentsOffset + size;
        }
        REQUIRE(cursor + 4U <= archive.size());
        CHECK(Read32(archive, cursor) == 0x02014b50U);
        return result;
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        const auto size = std::filesystem::file_size(path);
        std::vector<std::byte> result(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream);
        if (!result.empty())
            REQUIRE(stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())));
        return result;
    }
} // namespace

TEST_CASE("diagnostic bundles freeze sanitized bytes before preview and publication")
{
    TestDirectory directory("diagnostic-bundle");
    const std::string commit = "e3eb5acfce153e3d5d85d4a0f419d8a642ffa52a";
    const std::string credential = "AbCDefghijkLMNopqrstUVWXyz0123456789_-";
    const std::string sessionCookie = "session-cookie-canary";
    const std::string csrfCookie = "csrf-cookie-canary";
    const std::string jwt = "header.payload.signature-canary";
    const std::string projectName = "Private Adventure";
    const std::string assetName = "KeithPortrait.png";
    const std::string documentName = "Keith Resume.docx";
    const std::string jsonCredential = "json-secret-canary";
    const std::string jsonProject = "Confidential JSON Project";
    const std::string jsonWorkspace = "Private Workspace";
    const std::string jsonEnvironment = "C:\\Users\\Keith\\AppData\\Local\\Private";
    const std::string lowerDigitCredential = "ghijklmnopqrstuvwxyz0123456789abcdefgh";
    const std::string privateKey = "-----BEGIN PRIVATE KEY-----\nprivate material\n-----END PRIVATE KEY-----\n";
    const auto bundle = Keire::Internal::BuildDiagnosticBundle(
        {.TextSources = {{.Section = Keire::Internal::DiagnosticBundleSection::System,
                          .ArchivePath = "system/build.txt",
                          .Contents = "commit=" + commit + "\npassword=hunter2\nAuthorization: Bearer abc.def.ghi\n" +
                                      "Cookie: session=" + sessionCookie + "; csrf=" + csrfCookie + "\n" + "jwt=" +
                                      jwt + "\nprojectName=" + projectName + "\nasset_path=Characters/" + assetName +
                                      "\ndocumentName=" + documentName + "\nenvironment=PRIVATE_SETTING=canary\n" +
                                      "url=https://packages.example/private?signature=signed\n" +
                                      "email=user@example.com\nwindows=C:\\Users\\Keith\\project\n" +
                                      "unix=/Users/Keith Doe/Secret Project/file.txt\nopaque=" + lowerDigitCredential +
                                      "\ncredential=" + credential + "\n{\"token\":\"" + jsonCredential +
                                      "\",\"projectName\":\"" + jsonProject + "\",\"workspace\":\"" + jsonWorkspace +
                                      "\",\"TEMP\":\"" + jsonEnvironment + "\"}\n" + privateKey}}});

    const auto entries = StoredEntries(bundle.ArchiveBytes());
    REQUIRE(entries.contains("manifest.json"));
    REQUIRE(entries.contains("system/build.txt"));
    const auto& report = entries.at("system/build.txt");
    CHECK(report.find(commit) != std::string::npos);
    CHECK(report.find("hunter2") == std::string::npos);
    CHECK(report.find("abc.def.ghi") == std::string::npos);
    CHECK(report.find(sessionCookie) == std::string::npos);
    CHECK(report.find(csrfCookie) == std::string::npos);
    CHECK(report.find(jwt) == std::string::npos);
    CHECK(report.find(projectName) == std::string::npos);
    CHECK(report.find(assetName) == std::string::npos);
    CHECK(report.find(documentName) == std::string::npos);
    CHECK(report.find("PRIVATE_SETTING") == std::string::npos);
    CHECK(report.find("packages.example") == std::string::npos);
    CHECK(report.find("user@example.com") == std::string::npos);
    CHECK(report.find("Users\\Keith") == std::string::npos);
    CHECK(report.find("Keith Doe") == std::string::npos);
    CHECK(report.find("Secret Project") == std::string::npos);
    CHECK(report.find(lowerDigitCredential) == std::string::npos);
    CHECK(report.find(credential) == std::string::npos);
    CHECK(report.find(jsonCredential) == std::string::npos);
    CHECK(report.find(jsonProject) == std::string::npos);
    CHECK(report.find(jsonWorkspace) == std::string::npos);
    CHECK(report.find(jsonEnvironment) == std::string::npos);
    CHECK(report.find("private material") == std::string::npos);
    CHECK(report.find("<redacted:") != std::string::npos);

    REQUIRE(bundle.Preview().size() == entries.size());
    for (const auto& item : bundle.Preview())
    {
        REQUIRE(entries.contains(item.ArchivePath));
        CHECK(item.SizeBytes == entries.at(item.ArchivePath).size());
        const auto bytes = std::as_bytes(std::span(entries.at(item.ArchivePath)));
        CHECK(item.Sha256 == Keire::Detail::DigestToString(Keire::Detail::Sha256(bytes)));
    }

    const auto output = directory.Path / "diagnostics.zip";
    bundle.Save(output);
    CHECK(ReadBytes(output) == std::vector<std::byte>(bundle.ArchiveBytes().begin(), bundle.ArchiveBytes().end()));

    const auto preserved = directory.Path / "preserved.zip";
    Keire::Detail::WriteTextFileAtomically(preserved, "previous archive");
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [](const std::string_view operation, const std::filesystem::path&)
        {
            if (operation == "write-before-publish")
                throw std::runtime_error("injected diagnostic publication failure");
        });
    CHECK_THROWS(bundle.Save(preserved));
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(Keire::Detail::ReadTextFile(preserved, 1024) == "previous archive");
    bool retainedTemporary = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory.Path))
    {
        if (entry.path().filename().string().starts_with("preserved.zip.tmp."))
            retainedTemporary = true;
    }
    CHECK_FALSE(retainedTemporary);

    const auto linkedTarget = directory.Path / "linked-target";
    const auto linkedOutput = directory.Path / "linked-output";
    std::filesystem::create_directories(linkedTarget);
    std::error_code outputLinkError;
    std::filesystem::create_directory_symlink(linkedTarget, linkedOutput, outputLinkError);
    if (!outputLinkError)
    {
        CHECK_THROWS(bundle.Save(linkedOutput / "escaped.zip"));
        CHECK_FALSE(std::filesystem::exists(linkedTarget / "escaped.zip"));
        std::filesystem::remove(linkedOutput);
    }

    const auto linkedFileTarget = directory.Path / "linked-file-target.zip";
    const auto linkedFileOutput = directory.Path / "linked-file.zip";
    Keire::Detail::WriteTextFileAtomically(linkedFileTarget, "private target");
    outputLinkError.clear();
    std::filesystem::create_symlink(linkedFileTarget, linkedFileOutput, outputLinkError);
    if (!outputLinkError)
    {
        CHECK_THROWS(bundle.Save(linkedFileOutput));
        CHECK(Keire::Detail::ReadTextFile(linkedFileTarget, 1024) == "private target");
        std::filesystem::remove(linkedFileOutput);
    }
}

TEST_CASE("product diagnostic log filenames stay private across preview manifest and zip")
{
    TestDirectory directory("diagnostic-product-log-names");
    constexpr std::string_view privateCoreName = "Keith-SecretProject-core.log";
    constexpr std::string_view privateClientName = "Private Adventure-editor.log";
    Keire::Detail::WriteTextFileAtomically(directory.Path / std::string(privateCoreName), "core diagnostic\n");
    Keire::Detail::WriteTextFileAtomically(directory.Path / std::string(privateClientName), "client diagnostic\n");

    Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
    snapshot.Product = "Kéire Editor";
    snapshot.LogRoot = directory.Path;
    snapshot.LogFiles = {std::string(privateCoreName), std::string(privateClientName)};
    const auto bundle =
        Keire::Internal::BuildDiagnosticBundle(Keire::Internal::CreateProductDiagnosticBundleRequest(snapshot));
    const auto entries = StoredEntries(bundle.ArchiveBytes());
    REQUIRE(entries.contains("logs/core.log"));
    REQUIRE(entries.contains("logs/client.log"));
    REQUIRE(entries.contains("manifest.json"));
    for (const auto& preview : bundle.Preview())
    {
        CHECK(preview.ArchivePath.find(privateCoreName) == std::string::npos);
        CHECK(preview.ArchivePath.find(privateClientName) == std::string::npos);
    }
    CHECK(entries.at("manifest.json").find(privateCoreName) == std::string::npos);
    CHECK(entries.at("manifest.json").find(privateClientName) == std::string::npos);
    const std::string archiveText(reinterpret_cast<const char*>(bundle.ArchiveBytes().data()),
                                  bundle.ArchiveBytes().size());
    CHECK(archiveText.find(privateCoreName) == std::string::npos);
    CHECK(archiveText.find(privateClientName) == std::string::npos);
}

TEST_CASE("diagnostic bundle sanitization preserves labelled JSON structure")
{
    const auto bundle = Keire::Internal::BuildDiagnosticBundle(
        {.TextSources = {{.Section = Keire::Internal::DiagnosticBundleSection::System,
                          .ArchivePath = "system/json-canary.json",
                          .Contents = R"({"token":"secret-value","projectName":"Private Project","ok":true})"}}});

    const auto entries = StoredEntries(bundle.ArchiveBytes());
    REQUIRE(entries.contains("system/json-canary.json"));
    const auto report = nlohmann::json::parse(entries.at("system/json-canary.json"));
    CHECK(report.at("token") == "<redacted:credential>");
    CHECK(report.at("projectName") == "<redacted:private-metadata>");
    CHECK(report.at("ok") == true);
}

TEST_CASE("diagnostic sanitizer handles exact-limit privacy canaries deterministically")
{
    using namespace Keire::Internal;

    std::string contents(DiagnosticBundleMaximumTextEntryBytes, ' ');
    constexpr std::size_t scanBoundary = 256U * 1024U;
    constexpr std::array<std::string_view, 11> canaries{
        "password=contract-secret-canary\n",
        "{\"token\":\"json-secret-canary\",\"projectName\":\"Confidential JSON Project\"}\n",
        "Authorization: Bearer header.payload.signature-canary\n",
        "Cookie: session=private-cookie-canary\n",
        "url=https://packages.example/private?signature=signed-canary\n",
        "email=user@example.com\n",
        "windows=C:\\Users\\Keith\\Private\\file.txt\n",
        "unix=/Users/Keith Doe/Secret/file.txt\n",
        "-----BEGIN PRIVATE KEY-----\nprivate material\n-----END PRIVATE KEY-----\n",
        "opaque=AbCDefghijkLMNopqrstUVWXyz0123456789_-\n",
        "message=Bearer short-token-canary\n"};
    for (std::size_t index = 0; index < canaries.size(); ++index)
    {
        const auto offset = scanBoundary * (index + 1U) - 8U;
        REQUIRE(offset + canaries[index].size() <= contents.size());
        std::ranges::copy(canaries[index], contents.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    const DiagnosticBundleRequest request{
        .TextSources = {{.ArchivePath = "system/exact-limit.txt", .Contents = std::move(contents)}}};
    const auto first = BuildDiagnosticBundle(request);
    const auto entries = StoredEntries(first.ArchiveBytes());
    REQUIRE(entries.contains("system/exact-limit.txt"));
    const auto& report = entries.at("system/exact-limit.txt");
    for (const auto privateCanary : std::array<std::string_view, 12>{
             "contract-secret-canary", "json-secret-canary", "Confidential JSON Project",
             "header.payload.signature-canary", "private-cookie-canary", "packages.example", "user@example.com",
             "Users\\Keith", "Keith Doe", "private material", "AbCDefghijkLMNopqrstUVWXyz0123456789_-",
             "short-token-canary"})
        CHECK(report.find(privateCanary) == std::string::npos);
    CHECK(report.find("<redacted:credential>") != std::string::npos);
    CHECK(report.find("<redacted:private-metadata>") != std::string::npos);
    CHECK(report.find("<redacted:url>") != std::string::npos);
    CHECK(report.find("<redacted:email>") != std::string::npos);
    CHECK(report.find("<redacted:path>") != std::string::npos);
    CHECK(report.find("<redacted:private-key>") != std::string::npos);
    CHECK(report.find("<redacted:credential-like>") != std::string::npos);

    const auto second = BuildDiagnosticBundle(request);
    CHECK(std::ranges::equal(first.ArchiveBytes(), second.ArchiveBytes()));
}

TEST_CASE("diagnostic bundle optional sections and missing logs are explicit omissions")
{
    TestDirectory directory("diagnostic-bundle-omissions");
    const auto bundle = Keire::Internal::BuildDiagnosticBundle(
        {.Selection = {.IncludeLogs = true,
                       .IncludeProjectMetadata = false,
                       .IncludePackageVersions = false,
                       .IncludeCrashInformation = false},
         .TextSources = {{.Section = Keire::Internal::DiagnosticBundleSection::System,
                          .ArchivePath = "system/identity.txt",
                          .Contents = "Kéire test"},
                         {.Section = Keire::Internal::DiagnosticBundleSection::Project,
                          .ArchivePath = "project/metadata.txt",
                          .Contents = "allowlisted metadata"},
                         {.Section = Keire::Internal::DiagnosticBundleSection::Packages,
                          .ArchivePath = "packages/versions.txt",
                          .Contents = "package versions"},
                         {.Section = Keire::Internal::DiagnosticBundleSection::Crash,
                          .ArchivePath = "failures/last-failure.txt",
                          .Contents = "handled exception"}},
         .LogSources = {
             {.ArchivePath = "logs/missing.log", .TrustedRoot = directory.Path, .RelativePath = "missing.log"}}});

    const auto entries = StoredEntries(bundle.ArchiveBytes());
    CHECK(entries.contains("system/identity.txt"));
    CHECK_FALSE(entries.contains("project/metadata.txt"));
    CHECK_FALSE(entries.contains("packages/versions.txt"));
    CHECK_FALSE(entries.contains("failures/last-failure.txt"));
    CHECK_FALSE(entries.contains("logs/missing.log"));
    REQUIRE(bundle.Omissions().size() == 4U);
    CHECK(std::ranges::any_of(
        bundle.Omissions(), [](const auto& omission)
        { return omission.ArchivePath == "logs/missing.log" && omission.Reason == "source unavailable"; }));
}

TEST_CASE("diagnostic bundle reads only allowlisted logs and honors log deselection")
{
    TestDirectory directory("diagnostic-bundle-log-allowlist");
    const auto logs = directory.Path / "logs";
    std::filesystem::create_directories(logs / "unlisted");
    Keire::Detail::WriteTextFileAtomically(logs / "selected.log", "selected log marker\n");
    Keire::Detail::WriteTextFileAtomically(logs / "unlisted/private.log", "nested private canary\n");

    Keire::Internal::DiagnosticBundleRequest request;
    request.Selection.IncludeLogs = false;
    request.LogSources = {{.ArchivePath = "logs/core.log", .TrustedRoot = logs, .RelativePath = "selected.log"}};
    const auto deselected = Keire::Internal::BuildDiagnosticBundle(request);
    const auto deselectedEntries = StoredEntries(deselected.ArchiveBytes());
    CHECK_FALSE(deselectedEntries.contains("logs/core.log"));
    CHECK(std::ranges::any_of(
        deselected.Omissions(), [](const auto& omission)
        { return omission.ArchivePath == "logs/core.log" && omission.Reason == "section deselected"; }));

    request.Selection.IncludeLogs = true;
    const auto selected = Keire::Internal::BuildDiagnosticBundle(request);
    const auto selectedEntries = StoredEntries(selected.ArchiveBytes());
    REQUIRE(selectedEntries.contains("logs/core.log"));
    CHECK(selectedEntries.at("logs/core.log").find("selected log marker") != std::string::npos);
    CHECK_FALSE(selectedEntries.contains("logs/unlisted/private.log"));
    const std::string archiveText(reinterpret_cast<const char*>(selected.ArchiveBytes().data()),
                                  selected.ArchiveBytes().size());
    CHECK(archiveText.find("nested private canary") == std::string::npos);
}

TEST_CASE("diagnostic bundle log collection is tail bounded and rejects linked paths")
{
    TestDirectory directory("diagnostic-bundle-logs");
    const auto logs = directory.Path / "logs";
    std::filesystem::create_directories(logs);
    const auto log = logs / "Editor.log";
    {
        std::ofstream stream(log, std::ios::binary);
        REQUIRE(stream);
        const std::string prefix(Keire::Internal::DiagnosticBundleMaximumLogFileBytes, 'x');
        stream << prefix << "\npassword=tail-secret\ntail marker\n";
        const std::array malformed{static_cast<char>(0xff), static_cast<char>(0xc0), static_cast<char>(0xaf), '\0'};
        stream.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
        stream << "\nvalid UTF-8: Kéire\n";
    }
    const auto bundle = Keire::Internal::BuildDiagnosticBundle(
        {.LogSources = {{.ArchivePath = "logs/Editor.log", .TrustedRoot = logs, .RelativePath = "Editor.log"}}});
    const auto entries = StoredEntries(bundle.ArchiveBytes());
    REQUIRE(entries.contains("logs/Editor.log"));
    CHECK(entries.at("logs/Editor.log").size() <= Keire::Internal::DiagnosticBundleMaximumLogFileBytes);
    CHECK(entries.at("logs/Editor.log").find("tail marker") != std::string::npos);
    CHECK(entries.at("logs/Editor.log").find("tail-secret") == std::string::npos);
    CHECK(entries.at("logs/Editor.log").find(static_cast<char>(0xff)) == std::string::npos);
    CHECK(entries.at("logs/Editor.log").find(static_cast<char>(0xc0)) == std::string::npos);
    CHECK(entries.at("logs/Editor.log").find('\0') == std::string::npos);
    CHECK(entries.at("logs/Editor.log").find("valid UTF-8: Kéire") != std::string::npos);

    const auto outside = directory.Path / "outside";
    std::filesystem::create_directories(outside);
    {
        std::ofstream stream(outside / "private.log");
        stream << "must not be read";
    }
    std::error_code linkError;
    std::filesystem::create_directory_symlink(outside, logs / "linked", linkError);
    if (!linkError)
    {
        CHECK_THROWS_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                            {.LogSources = {{.ArchivePath = "logs/private.log",
                                             .TrustedRoot = logs,
                                             .RelativePath = "linked/private.log"}}})),
                        std::invalid_argument);
        std::filesystem::remove(logs / "linked");
    }

    linkError.clear();
    std::filesystem::create_symlink(outside / "private.log", logs / "linked-file.log", linkError);
    if (!linkError)
    {
        CHECK_THROWS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
            {.LogSources = {
                 {.ArchivePath = "logs/linked-file.log", .TrustedRoot = logs, .RelativePath = "linked-file.log"}}})));
        std::filesystem::remove(logs / "linked-file.log");
    }

    const auto linkedRoot = directory.Path / "logs-link";
    linkError.clear();
    std::filesystem::create_directory_symlink(logs, linkedRoot, linkError);
    if (!linkError)
    {
        CHECK_THROWS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
            {.LogSources = {
                 {.ArchivePath = "logs/root-link.log", .TrustedRoot = linkedRoot, .RelativePath = "Editor.log"}}})));
        std::filesystem::remove(linkedRoot);
    }
}

TEST_CASE("diagnostic bundle rejects unsafe inventory paths and cancellation leaves no output")
{
    CHECK_THROWS_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                        {.TextSources = {{.ArchivePath = "../escape.txt", .Contents = "private"}}})),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                        {.TextSources = {{.ArchivePath = "manifest.json", .Contents = "replacement"}}})),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                        {.TextSources = {{.ArchivePath = "system/CON.txt", .Contents = "unsafe"}}})),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                        {.TextSources = {{.ArchivePath = "system/trailing.", .Contents = "unsafe"}}})),
                    std::invalid_argument);
    bool cancelled = false;
    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::Internal::BuildDiagnosticBundle(
                             {.TextSources = {{.ArchivePath = "system/report.txt", .Contents = "safe"}},
                              .IsCancelled = [&] { return std::exchange(cancelled, true); }})),
                         "Diagnostic bundle collection was cancelled.", std::runtime_error);
}

TEST_CASE("diagnostic bundle byte limits are enforced before publication")
{
    using namespace Keire::Internal;

    CHECK_THROWS_WITH_AS(
        static_cast<void>(BuildDiagnosticBundle(
            {.TextSources = {{.ArchivePath = "system/oversized.txt",
                              .Contents = std::string(DiagnosticBundleMaximumTextEntryBytes + 1U, 'x')}}})),
        "A diagnostic bundle text source exceeds its maximum size.", std::runtime_error);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(BuildDiagnosticBundle(
            {.TextSources = {{.ArchivePath = "system/first.txt", .Contents = std::string(11U * 1024U * 1024U, 'x')},
                             {.ArchivePath = "system/second.txt", .Contents = std::string(11U * 1024U * 1024U, 'y')},
                             {.ArchivePath = "system/third.txt", .Contents = std::string(11U * 1024U * 1024U, 'z')}}})),
        "Diagnostic bundle exceeds its maximum archive size.", std::runtime_error);
}

TEST_CASE("diagnostic bundle total log bytes and unwritable output are bounded")
{
    using namespace Keire::Internal;

    TestDirectory directory("diagnostic-bundle-total-log-limit");
    std::vector<DiagnosticBundleLogSource> sources;
    for (std::size_t index = 0; index < 5U; ++index)
    {
        const auto filename = "log-" + std::to_string(index) + ".txt";
        std::ofstream stream(directory.Path / filename, std::ios::binary);
        REQUIRE(stream);
        stream << std::string(DiagnosticBundleMaximumLogFileBytes, static_cast<char>('a' + index));
        sources.push_back({.ArchivePath = "logs/" + filename, .TrustedRoot = directory.Path, .RelativePath = filename});
    }

    const auto bundle = BuildDiagnosticBundle({.LogSources = std::move(sources)});
    const auto entries = StoredEntries(bundle.ArchiveBytes());
    std::size_t retainedBytes = 0;
    for (const auto& [path, contents] : entries)
        if (path.starts_with("logs/"))
            retainedBytes += contents.size();
    CHECK(retainedBytes == DiagnosticBundleMaximumTotalLogBytes);
    CHECK_FALSE(entries.contains("logs/log-4.txt"));
    CHECK(std::ranges::any_of(
        bundle.Omissions(), [](const auto& omission)
        { return omission.ArchivePath == "logs/log-4.txt" && omission.Reason == "total log limit reached"; }));

    const auto nonDirectory = directory.Path / "not-a-directory";
    Keire::Detail::WriteTextFileAtomically(nonDirectory, "occupied");
    CHECK_THROWS(bundle.Save(nonDirectory / "diagnostics.zip"));
    CHECK_FALSE(std::filesystem::exists(nonDirectory / "diagnostics.zip"));
}
