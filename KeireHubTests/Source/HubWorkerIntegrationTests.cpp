#include "TestSupport.h"

#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"
#include "KeireHubRuntime/ManagedEditorRemoval.h"
#include "KeireHubRuntime/PackageArchive.h"
#include "KeireHubRuntime/PackageAssembly.h"
#include "KeireHubRuntime/PackagePublish.h"
#include "KeireHubRuntime/PackageReceipt.h"

#include "DistributionEncoding.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef KEIRE_HUB_WORKER_TEST_EXECUTABLE
#error "The Hub worker integration test requires its generated worker path."
#endif

using namespace KeireHub;

namespace
{
    using namespace std::chrono_literals;

    struct ArchiveFixture final
    {
        PackageManifest Manifest;
        std::filesystem::path Archive;
    };

    [[nodiscard]] std::string HostPlatform()
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#else
        return "linux";
#endif
    }

    [[nodiscard]] std::string Digest(const std::string_view contents)
    {
        return Detail::Sha256Hex(std::as_bytes(std::span(contents)));
    }

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] std::string EditorEntrypoint()
    {
#if defined(_WIN32)
        return "bin/KeireClient.exe";
#else
        return "bin/KeireClient";
#endif
    }

    [[nodiscard]] std::string WriteEditorManifest(const std::filesystem::path& payload,
                                                  const std::string_view editorBytes)
    {
        const auto entrypoint = EditorEntrypoint();
        nlohmann::json manifest{
            {"schemaVersion", 2},
            {"artifact", "editor"},
            {"packageId", "keire.editor"},
            {"version", "1.2.3"},
            {"channel", "stable"},
            {"platform", HostPlatform()},
            {"architecture", KEIRE_BUILD_ARCHITECTURE},
            {"entrypoints", {{"editor", entrypoint}}},
            {"projectSchema", {{"minimum", 1}, {"maximum", 3}}},
            {"inventoryExcludes", {"editor-package.json"}},
            {"files", {{{"path", entrypoint}, {"sizeBytes", editorBytes.size()}, {"sha256", Digest(editorBytes)}}}},
            {"installedSizeBytes", 1},
            {"manifestFingerprint", std::string(64, '0')}};
        auto fingerprint = ComputeEditorPackageManifestFingerprint(manifest.dump());
        if (!fingerprint)
            throw std::runtime_error(fingerprint.Error().Message);
        manifest["manifestFingerprint"] = fingerprint.Value();

        std::string document;
        for (std::size_t iteration = 0; iteration < 16; ++iteration)
        {
            document = manifest.dump(2) + '\n';
            const auto installedSize = editorBytes.size() + document.size();
            if (manifest["installedSizeBytes"].get<std::uint64_t>() == installedSize)
                break;
            manifest["installedSizeBytes"] = installedSize;
        }
        document = manifest.dump(2) + '\n';
        if (manifest["installedSizeBytes"].get<std::uint64_t>() != editorBytes.size() + document.size())
            throw std::runtime_error("The editor manifest size did not converge.");
        KeireHubTests::WriteText(payload / "editor-package.json", document);
        return document;
    }

    [[nodiscard]] ArchiveFixture WriteEditorArchive(const std::filesystem::path& root)
    {
        constexpr std::string_view editorBytes = "editor-worker-smoke";
        const auto payload = root / "editor-payload";
        KeireHubTests::WriteText(payload / EditorEntrypoint(), editorBytes);
        const auto editorManifest = WriteEditorManifest(payload, editorBytes);
        PackageManifest manifest{.Id = "keire.editor",
                                 .Version = Version("1.2.3"),
                                 .Kind = PackageKind::Editor,
                                 .DisplayName = "Kéire Editor",
                                 .Channel = "stable",
                                 .Platform = HostPlatform(),
                                 .Architecture = KEIRE_BUILD_ARCHITECTURE,
                                 .ArtifactSizeBytes = 1,
                                 .ArtifactSha256 = KeireHubTests::Digest('a'),
                                 .InstalledSizeBytes = editorBytes.size() + editorManifest.size(),
                                 .Files = {{.Path = EditorEntrypoint(),
                                            .SizeBytes = editorBytes.size(),
                                            .Sha256 = Digest(editorBytes),
                                            .Mode = 0755U},
                                           {.Path = "editor-package.json",
                                            .SizeBytes = editorManifest.size(),
                                            .Sha256 = Digest(editorManifest),
                                            .Mode = 0644U}},
                                 .SignatureKeyId = "test-key"};
        const auto archive = root / "editor.keirepackage";
        auto written =
            WritePackageArchive({.Manifest = std::move(manifest), .PayloadRoot = payload, .Output = archive});
        if (!written)
            throw std::runtime_error(written.Error().Message + " " + written.Error().TechnicalDetails);
        return {.Manifest = std::move(written).Value().Manifest, .Archive = archive};
    }

    [[nodiscard]] ArchiveFixture WriteComponentArchive(const std::filesystem::path& root)
    {
        constexpr std::string_view componentBytes = "build-support-worker-smoke";
        const auto payload = root / "component-payload";
        KeireHubTests::WriteText(payload / "modules/desktop.bin", componentBytes);
        auto constraint = VersionConstraint::Parse("=1.2.3");
        if (!constraint)
            throw std::runtime_error(constraint.Error().Message);
        PackageManifest manifest{
            .Id = "keire.desktop-support",
            .Version = Version("1.0.0"),
            .Kind = PackageKind::BuildSupport,
            .DisplayName = "Desktop Build Support",
            .Channel = "stable",
            .Platform = HostPlatform(),
            .Architecture = KEIRE_BUILD_ARCHITECTURE,
            .Dependencies = {{.PackageId = "keire.editor", .Versions = std::move(constraint).Value()}},
            .ArtifactSizeBytes = 1,
            .ArtifactSha256 = KeireHubTests::Digest('b'),
            .InstalledSizeBytes = componentBytes.size(),
            .Files = {{.Path = "modules/desktop.bin",
                       .SizeBytes = componentBytes.size(),
                       .Sha256 = Digest(componentBytes),
                       .Mode = 0644U}},
            .SignatureKeyId = "test-key"};
        const auto archive = root / "component.keirepackage";
        auto written =
            WritePackageArchive({.Manifest = std::move(manifest), .PayloadRoot = payload, .Output = archive});
        if (!written)
            throw std::runtime_error(written.Error().Message + " " + written.Error().TechnicalDetails);
        return {.Manifest = std::move(written).Value().Manifest, .Archive = archive};
    }

    [[nodiscard]] std::string FileUrl(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        return "file:///" + Detail::PathToUtf8(path);
#else
        return "file://" + Detail::PathToUtf8(path);
#endif
    }

    [[nodiscard]] DownloadRequest Download(const ArchiveFixture& package, const std::filesystem::path& cache)
    {
        return {.PackageId = package.Manifest.Id,
                .Url = FileUrl(package.Archive),
                .Sha256 = package.Manifest.ArtifactSha256,
                .SizeBytes = package.Manifest.ArtifactSizeBytes,
                .CacheRoot = cache,
                .Retry = {.MaximumAttempts = 1}};
    }

    void ValidateFileFixture(const DownloadRequest& request)
    {
        FileDownloadTransport transport;
        auto opened = transport.Open({.Url = request.Url});
        if (!opened)
        {
            throw std::runtime_error(opened.Error().Message + " " + opened.Error().TechnicalDetails +
                                     " URL=" + request.Url);
        }
    }

#if defined(_WIN32)
    [[nodiscard]] std::wstring Utf8ToWide(const std::string_view value)
    {
        if (value.empty())
            return {};
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("The worker argument is too long.");
        const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
            throw std::runtime_error("The worker argument is not valid UTF-8.");
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                result.data(), size) != size)
        {
            throw std::runtime_error("The worker argument could not be converted to UTF-16.");
        }
        return result;
    }

    [[nodiscard]] std::wstring QuoteArgument(const std::wstring_view value)
    {
        if (value.empty())
            return L"\"\"";
        if (value.find_first_of(L" \t\"") == std::wstring_view::npos)
            return std::wstring(value);
        std::wstring result(1, L'\"');
        std::size_t slashes = 0;
        for (const auto character : value)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            if (character == L'\"')
            {
                result.append(slashes * 2U + 1U, L'\\');
                result.push_back(L'\"');
            }
            else
            {
                result.append(slashes, L'\\');
                result.push_back(character);
            }
            slashes = 0;
        }
        result.append(slashes * 2U, L'\\');
        result.push_back(L'\"');
        return result;
    }
#endif

    class WorkerProcess final
    {
      public:
        WorkerProcess(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory,
                      const std::vector<std::string>& arguments)
        {
#if defined(_WIN32)
            auto command = QuoteArgument(executable.wstring());
            for (const auto& argument : arguments)
            {
                command.push_back(L' ');
                command += QuoteArgument(Utf8ToWide(argument));
            }
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                workingDirectory.c_str(), &startup, &process))
            {
                throw std::runtime_error("The Hub worker test process could not be started.");
            }
            CloseHandle(process.hThread);
            m_Process = process.hProcess;
#else
            std::vector<std::string> storage;
            storage.reserve(arguments.size() + 1U);
            storage.push_back(Detail::PathToUtf8(executable));
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char*> values;
            values.reserve(storage.size() + 1U);
            for (auto& value : storage)
                values.push_back(value.data());
            values.push_back(nullptr);
            m_Process = fork();
            if (m_Process < 0)
                throw std::runtime_error("The Hub worker test process could not be forked.");
            if (m_Process == 0)
            {
                if (chdir(workingDirectory.c_str()) != 0)
                    _exit(126);
                execv(executable.c_str(), values.data());
                _exit(127);
            }
#endif
        }

        WorkerProcess(const WorkerProcess&) = delete;
        WorkerProcess& operator=(const WorkerProcess&) = delete;

        ~WorkerProcess()
        {
#if defined(_WIN32)
            if (m_Process)
            {
                if (WaitForSingleObject(m_Process, 0) == WAIT_TIMEOUT)
                {
                    (void)TerminateProcess(m_Process, 125);
                    (void)WaitForSingleObject(m_Process, 5'000);
                }
                CloseHandle(m_Process);
            }
#else
            if (m_Process > 0)
            {
                int status = 0;
                const auto waited = waitpid(m_Process, &status, WNOHANG);
                if (waited == 0)
                {
                    (void)kill(m_Process, SIGKILL);
                    while (waitpid(m_Process, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                }
            }
#endif
        }

        [[nodiscard]] std::optional<int> Wait(const std::chrono::milliseconds timeout)
        {
#if defined(_WIN32)
            const auto bounded = std::min<std::uint64_t>(static_cast<std::uint64_t>(timeout.count()),
                                                         std::numeric_limits<DWORD>::max() - 1ULL);
            if (WaitForSingleObject(m_Process, static_cast<DWORD>(bounded)) != WAIT_OBJECT_0)
                return {};
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(m_Process, &exitCode))
                return {};
            return static_cast<int>(exitCode);
#else
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;)
            {
                int status = 0;
                const auto waited = waitpid(m_Process, &status, WNOHANG);
                if (waited == m_Process)
                {
                    m_Process = -1;
                    if (WIFEXITED(status))
                        return WEXITSTATUS(status);
                    if (WIFSIGNALED(status))
                        return 128 + WTERMSIG(status);
                    return 124;
                }
                if (waited < 0 && errno != EINTR)
                    return {};
                if (std::chrono::steady_clock::now() >= deadline)
                    return {};
                std::this_thread::sleep_for(5ms);
            }
#endif
        }

      private:
#if defined(_WIN32)
        HANDLE m_Process = nullptr;
#else
        pid_t m_Process = -1;
#endif
    };

    [[nodiscard]] PackageManifest FinalizeEditorStaging(const HubWorkerEditorInstallRequest& install,
                                                        const std::filesystem::path& stagingRoot)
    {
        auto prepared = PrepareManagedEditorPackage({.PackageRoot = stagingRoot,
                                                     .InstallationRoot = install.Destination,
                                                     .InstallationId = install.InstallationId,
                                                     .MarkerNonce = install.MarkerNonce,
                                                     .HostPlatform = install.HostPlatform,
                                                     .HostArchitecture = install.HostArchitecture,
                                                     .VerifiedUnixSeconds = install.VerifiedUnixSeconds,
                                                     .RequirePackageReceipt = true});
        if (!prepared)
            throw std::runtime_error(prepared.Error().Message);
        std::vector<PackageManifest> manifests;
        manifests.reserve(install.PackageSteps.size());
        for (const auto& step : install.PackageSteps)
            manifests.push_back(step.Package);
        auto publication = CreatePackagePublicationManifest(manifests);
        if (!publication)
            throw std::runtime_error(publication.Error().Message);
        auto withReceipt = FinalizePackageAssemblyReceipt(stagingRoot, publication.Value(), manifests);
        if (!withReceipt)
            throw std::runtime_error(withReceipt.Error().Message);
        auto withMarker = FinalizePackageAssemblyMarker(stagingRoot, withReceipt.Value());
        if (!withMarker)
            throw std::runtime_error(withMarker.Error().Message);
        return std::move(withMarker).Value();
    }

    [[nodiscard]] HubWorkerResult RunWorker(const std::filesystem::path& operation, const HubWorkerRequest& request,
                                            const bool requireSuccess = true)
    {
        std::filesystem::create_directories(operation);
        const auto requestPath = operation / "request.json";
        const auto statusPath = operation / "status.json";
        const auto resultPath = operation / "result.json";
        const auto controlPath = operation / "control.json";
        const auto written = WriteHubWorkerRequest(requestPath, request);
        if (!written)
            throw std::runtime_error(written.Error().Message + " " + written.Error().TechnicalDetails);
        const auto controlled = WriteHubWorkerControl(controlPath, DownloadControl::Continue);
        if (!controlled)
            throw std::runtime_error(controlled.Error().Message + " " + controlled.Error().TechnicalDetails);

        auto executable = std::filesystem::current_path() / KEIRE_HUB_WORKER_TEST_EXECUTABLE;
#if defined(_WIN32)
        executable += ".exe";
#endif
        if (!std::filesystem::is_regular_file(executable))
            throw std::runtime_error("The Hub worker test executable is missing: " + executable.string());
        const std::vector<std::string> arguments{
            "--request", Detail::PathToUtf8(requestPath), "--status",  Detail::PathToUtf8(statusPath),
            "--result",  Detail::PathToUtf8(resultPath),  "--control", Detail::PathToUtf8(controlPath)};
        WorkerProcess process(executable, std::filesystem::current_path(), arguments);
        const auto exitCode = process.Wait(15s);
        if (!exitCode)
            throw std::runtime_error("The Hub worker integration smoke timed out.");
        if (*exitCode != 0 && requireSuccess)
        {
            auto failure = ReadHubWorkerResult(resultPath);
            const auto details = failure && failure.Value().Failure ? " " + failure.Value().Failure->Message + " " +
                                                                          failure.Value().Failure->TechnicalDetails
                                                                    : std::string{};
            throw std::runtime_error("The Hub worker integration smoke failed with exit code " +
                                     std::to_string(*exitCode) + '.' + details);
        }
        auto result = ReadHubWorkerResult(resultPath);
        if (!result)
            throw std::runtime_error(result.Error().Message + " " + result.Error().TechnicalDetails);
        return std::move(result).Value();
    }
} // namespace

TEST_CASE("Hub worker installs and recognizes a receipt-bound multi-package editor")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto editor = WriteEditorArchive(temporary.Path());
    const auto component = WriteComponentArchive(temporary.Path());
    const auto cache = temporary.Path() / "Cache";
    const auto installs = temporary.Path() / "Editors";
    const auto destination = installs / "1.2.3";
    std::filesystem::create_directories(installs);

    const auto editorDownload = Download(editor, cache);
    const auto componentDownload = Download(component, cache);
    ValidateFileFixture(editorDownload);
    ValidateFileFixture(componentDownload);
    const HubWorkerRequest request{.TaskId = "worker-editor-install-smoke",
                                   .Download = editorDownload,
                                   .EditorInstall = HubWorkerEditorInstallRequest{
                                       .Package = editor.Manifest,
                                       .PackageSteps = {{.Package = editor.Manifest, .Download = editorDownload},
                                                        {.Package = component.Manifest, .Download = componentDownload}},
                                       .RequestedPackageIds = {editor.Manifest.Id, component.Manifest.Id},
                                       .AllowedInstallRoot = installs,
                                       .Destination = destination,
                                       .InstallationId = "editor-worker-smoke",
                                       .MarkerNonce = std::string(64, 'a'),
                                       .HostPlatform = HostPlatform(),
                                       .HostArchitecture = KEIRE_BUILD_ARCHITECTURE,
                                       .VerifiedUnixSeconds = 100}};

    const auto result = RunWorker(temporary.Path() / "operation-first", request);
    CHECK(result.TaskId == request.TaskId);
    CHECK(result.Outcome == DownloadOutcome::Completed);
    CHECK(result.CachePath == DownloadManager::CachePath(editorDownload));
    CHECK(result.InstalledRoot == destination);
    CHECK(result.InstallationId == "editor-worker-smoke");
    CHECK_FALSE(result.Failure.has_value());
    CHECK(KeireHubTests::ReadText(destination / EditorEntrypoint()) == "editor-worker-smoke");
    CHECK(KeireHubTests::ReadText(destination / "modules/desktop.bin") == "build-support-worker-smoke");

    auto receipt = ReadPackageInstallReceipt(destination);
    REQUIRE(receipt);
    REQUIRE(receipt.Value().Packages.size() == 2U);
    CHECK(receipt.Value().Packages.front().Id == editor.Manifest.Id);
    CHECK(receipt.Value().Packages.back().Id == component.Manifest.Id);
    CHECK(receipt.Value().AggregateInstalledSizeBytes ==
          editor.Manifest.InstalledSizeBytes + component.Manifest.InstalledSizeBytes);
    auto marker = EditorInstallationRegistry::ReadManagedMarker(destination);
    REQUIRE(marker);
    CHECK(marker.Value().InstallationId == "editor-worker-smoke");
    CHECK(marker.Value().Nonce == std::string(64, 'a'));
    CHECK(marker.Value().ReceiptSha256 == receipt.Value().DocumentSha256);
    auto prepared = PrepareManagedEditorPackage({.PackageRoot = destination,
                                                 .InstallationRoot = destination,
                                                 .InstallationId = "editor-worker-smoke",
                                                 .MarkerNonce = std::string(64, 'a'),
                                                 .HostPlatform = HostPlatform(),
                                                 .HostArchitecture = KEIRE_BUILD_ARCHITECTURE,
                                                 .VerifiedUnixSeconds = 101,
                                                 .RequirePackageReceipt = true});
    REQUIRE(prepared);
    CHECK(prepared.Value().PackageTreeIdentity == receipt.Value().AggregateIdentitySha256);
    CHECK(prepared.Value().InstalledPackages.size() == 2U);

    const auto healthyCopy = temporary.Path() / "healthy-editor-copy";
    std::filesystem::create_directories(healthyCopy);
    std::filesystem::copy(destination, healthyCopy,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    auto lateInstall = request;
    lateInstall.TaskId = "worker-editor-install-late-collision";
    lateInstall.EditorInstall->AllowedInstallRoot = temporary.Path() / "LateEditors";
    lateInstall.EditorInstall->Destination = lateInstall.EditorInstall->AllowedInstallRoot / "1.2.3";
    lateInstall.EditorInstall->InstallationId = "editor-worker-late-collision";
    lateInstall.EditorInstall->MarkerNonce = std::string(64, 'b');
    std::filesystem::create_directories(lateInstall.EditorInstall->AllowedInstallRoot);
    auto latePaths = PlanPackagePublish(lateInstall.EditorInstall->AllowedInstallRoot,
                                        lateInstall.EditorInstall->Destination, lateInstall.TaskId);
    REQUIRE(latePaths);
    std::filesystem::create_directories(latePaths.Value().StagingRoot);
    std::filesystem::copy(healthyCopy, latePaths.Value().StagingRoot,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    REQUIRE(std::filesystem::remove(latePaths.Value().StagingRoot / EditorInstallationRegistry::MarkerFileName));
    const auto lateManifest = FinalizeEditorStaging(*lateInstall.EditorInstall, latePaths.Value().StagingRoot);
    const PackagePublishOptions newInstallPolicy{.DestinationPolicy = PackagePublishDestinationPolicy::RequireAbsent};
    REQUIRE(PreparePackagePublish(latePaths.Value(), lateManifest, lateInstall.TaskId, newInstallPolicy));
    KeireHubTests::WriteText(lateInstall.EditorInstall->Destination / "unrelated.txt", "unrelated");
    const auto lateCollision = RunWorker(temporary.Path() / "operation-install-late-collision", lateInstall, false);
    CHECK(lateCollision.Outcome == DownloadOutcome::Failed);
    REQUIRE(lateCollision.Failure);
    CHECK(KeireHubTests::ReadText(lateInstall.EditorInstall->Destination / "unrelated.txt") == "unrelated");
    CHECK_FALSE(std::filesystem::exists(lateInstall.EditorInstall->Destination / EditorEntrypoint()));

    KeireHubTests::WriteText(destination / EditorEntrypoint(), "damaged-editor");
    auto ordinaryReplacement = request;
    ordinaryReplacement.TaskId = "worker-editor-install-cannot-repair";
    const auto refusedInstall =
        RunWorker(temporary.Path() / "operation-install-cannot-repair", ordinaryReplacement, false);
    CHECK(refusedInstall.Outcome == DownloadOutcome::Failed);
    REQUIRE(refusedInstall.Failure);
    CHECK(refusedInstall.Failure->Code == HubErrorCode::DestinationConflict);
    CHECK(KeireHubTests::ReadText(destination / EditorEntrypoint()) == "damaged-editor");
    REQUIRE(std::filesystem::remove(destination / PackageInstallReceiptFileName));

    auto repair = request;
    repair.TaskId = "worker-editor-repair-smoke";
    repair.EditorInstall->Mode = HubWorkerEditorInstallMode::Repair;
    repair.EditorInstall->RepairAuthorization = {.ManifestFingerprint = prepared.Value().ManifestFingerprint,
                                                 .PackageTreeIdentity = prepared.Value().PackageTreeIdentity,
                                                 .PackageReceiptSha256 = prepared.Value().PackageReceiptSha256,
                                                 .EditorEntrypoint = ResolveEditorEntrypoint(prepared.Value())};
    auto wrongIdentity = repair;
    wrongIdentity.TaskId = "worker-editor-repair-wrong-identity";
    wrongIdentity.EditorInstall->RepairAuthorization->PackageTreeIdentity = KeireHubTests::Digest('9');
    const auto refusedRepair = RunWorker(temporary.Path() / "operation-repair-wrong-identity", wrongIdentity, false);
    CHECK(refusedRepair.Outcome == DownloadOutcome::Failed);
    REQUIRE(refusedRepair.Failure);
    CHECK(refusedRepair.Failure->Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(KeireHubTests::ReadText(destination / EditorEntrypoint()) == "damaged-editor");
    CHECK_FALSE(std::filesystem::exists(destination / PackageInstallReceiptFileName));

    auto repairPaths = PlanPackagePublish(installs, destination, repair.TaskId);
    REQUIRE(repairPaths);
    std::filesystem::create_directories(repairPaths.Value().StagingRoot);
    std::filesystem::copy(healthyCopy, repairPaths.Value().StagingRoot,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    const auto repairManifest = FinalizeEditorStaging(*repair.EditorInstall, repairPaths.Value().StagingRoot);
    const PackagePublishOptions repairPolicy{.DestinationPolicy = PackagePublishDestinationPolicy::RequireExisting};
    REQUIRE(PreparePackagePublish(repairPaths.Value(), repairManifest, repair.TaskId, repairPolicy));
    const auto markerPath = destination / EditorInstallationRegistry::MarkerFileName;
    auto markerDocument = nlohmann::json::parse(KeireHubTests::ReadText(markerPath));
    markerDocument["nonce"] = std::string(64, '9');
    KeireHubTests::WriteText(markerPath, markerDocument.dump());
    const auto blockedRecovery = RunWorker(temporary.Path() / "operation-repair-blocked", repair, false);
    CHECK(blockedRecovery.Outcome == DownloadOutcome::Failed);
    REQUIRE(blockedRecovery.Failure);
    CHECK(blockedRecovery.Failure->Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(KeireHubTests::ReadText(destination / EditorEntrypoint()) == "damaged-editor");
    CHECK(std::filesystem::is_directory(repairPaths.Value().StagingRoot));
    markerDocument["nonce"] = marker.Value().Nonce;
    KeireHubTests::WriteText(markerPath, markerDocument.dump());

    const auto repaired = RunWorker(temporary.Path() / "operation-repair", repair);
    CHECK(repaired.Outcome == DownloadOutcome::Completed);
    CHECK(repaired.InstalledRoot == destination);
    CHECK(repaired.InstallationId == prepared.Value().Id);
    CHECK(KeireHubTests::ReadText(destination / EditorEntrypoint()) == "editor-worker-smoke");
    const auto repairedMarker = EditorInstallationRegistry::ReadManagedMarker(destination);
    REQUIRE(repairedMarker);
    CHECK(repairedMarker.Value().InstallationId == marker.Value().InstallationId);
    CHECK(repairedMarker.Value().Nonce == marker.Value().Nonce);
    CHECK(repairedMarker.Value().ReceiptSha256 == marker.Value().ReceiptSha256);
    CHECK(std::filesystem::is_regular_file(destination / PackageInstallReceiptFileName));

    std::filesystem::remove_all(cache);
    std::filesystem::remove(editor.Archive);
    std::filesystem::remove(component.Archive);
    const auto replay = RunWorker(temporary.Path() / "operation-replay", request);
    CHECK(replay.Outcome == DownloadOutcome::Completed);
    CHECK(replay.InstalledRoot == destination);
    CHECK(replay.InstallationId == "editor-worker-smoke");
    CHECK_FALSE(replay.Failure.has_value());
    CHECK_FALSE(std::filesystem::exists(installs / (".keire-stage-" + request.TaskId)));

    const HubWorkerRequest removalRequest{
        .TaskId = "worker-editor-removal-smoke",
        .EditorRemoval = HubWorkerEditorRemovalRequest{.AllowedInstallRoot = installs,
                                                       .Root = destination,
                                                       .InstallationId = prepared.Value().Id,
                                                       .ManifestFingerprint = prepared.Value().ManifestFingerprint,
                                                       .PackageTreeIdentity = prepared.Value().PackageTreeIdentity,
                                                       .PackageReceiptSha256 = prepared.Value().PackageReceiptSha256,
                                                       .MarkerNonce = prepared.Value().MarkerNonce}};
    KeireHubTests::WriteText(destination / "undeclared.bin", "hostile");
    const auto refused = RunWorker(temporary.Path() / "operation-removal-refused", removalRequest, false);
    CHECK(refused.Outcome == DownloadOutcome::Failed);
    REQUIRE(refused.Failure);
    CHECK(refused.Failure->Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(std::filesystem::is_directory(destination));
    std::filesystem::remove(destination / "undeclared.bin");

    const auto removed = RunWorker(temporary.Path() / "operation-removal", removalRequest);
    CHECK(removed.Outcome == DownloadOutcome::Completed);
    CHECK(removed.RemovedRoot == destination);
    CHECK(removed.InstallationId == prepared.Value().Id);
    CHECK(removed.CachePath.empty());
    CHECK_FALSE(std::filesystem::exists(destination));
    const auto repeated = RunWorker(temporary.Path() / "operation-removal-repeat", removalRequest);
    CHECK(repeated.Outcome == DownloadOutcome::Completed);
    CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("Managed editor removal resumes from its same-parent tombstone journal")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto editor = WriteEditorArchive(temporary.Path());
    const auto cache = temporary.Path() / "Cache";
    const auto installs = temporary.Path() / "Editors";
    const auto destination = installs / "Recovery";
    std::filesystem::create_directories(installs);
    const auto download = Download(editor, cache);
    const HubWorkerRequest installRequest{.TaskId = "worker-editor-removal-recovery-install",
                                          .Download = download,
                                          .EditorInstall = HubWorkerEditorInstallRequest{
                                              .Package = editor.Manifest,
                                              .PackageSteps = {{.Package = editor.Manifest, .Download = download}},
                                              .RequestedPackageIds = {editor.Manifest.Id},
                                              .AllowedInstallRoot = installs,
                                              .Destination = destination,
                                              .InstallationId = "editor-removal-recovery",
                                              .MarkerNonce = std::string(64, 'c'),
                                              .HostPlatform = HostPlatform(),
                                              .HostArchitecture = KEIRE_BUILD_ARCHITECTURE,
                                              .VerifiedUnixSeconds = 200}};
    (void)RunWorker(temporary.Path() / "operation-recovery-install", installRequest);
    auto prepared = PrepareManagedEditorPackage({.PackageRoot = destination,
                                                 .InstallationRoot = destination,
                                                 .InstallationId = "editor-removal-recovery",
                                                 .MarkerNonce = std::string(64, 'c'),
                                                 .HostPlatform = HostPlatform(),
                                                 .HostArchitecture = KEIRE_BUILD_ARCHITECTURE,
                                                 .VerifiedUnixSeconds = 201,
                                                 .RequirePackageReceipt = true});
    REQUIRE(prepared);
    const EditorManagedOperationPlan plan{.Operation = EditorManagedOperation::Remove,
                                          .InstallationId = prepared.Value().Id,
                                          .Root = destination,
                                          .ManifestFingerprint = prepared.Value().ManifestFingerprint,
                                          .PackageTreeIdentity = prepared.Value().PackageTreeIdentity,
                                          .PackageReceiptSha256 = prepared.Value().PackageReceiptSha256,
                                          .MarkerNonce = prepared.Value().MarkerNonce,
                                          .CurrentHealth = InstallationHealth::Healthy};
    auto interrupted = RemoveManagedEditorInstallation(plan, "removal-restart",
                                                       {.ContinueAfterPhase = [](const ManagedEditorRemovalPhase phase)
                                                        { return phase != ManagedEditorRemovalPhase::RootRenamed; }});
    REQUIRE_FALSE(interrupted);
    CHECK(interrupted.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK(interrupted.Error().Retryable);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK(std::filesystem::is_directory(installs / ".keire-remove-tombstone-removal-restart"));

    auto recovered = RemoveManagedEditorInstallation(plan, "removal-restart");
    const auto recoveryDetails =
        recovered ? std::string{} : recovered.Error().Message + " " + recovered.Error().TechnicalDetails;
    INFO(recoveryDetails);
    REQUIRE(recovered);
    CHECK(recovered.Value().Completed);
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK_FALSE(std::filesystem::exists(installs / ".keire-remove-tombstone-removal-restart"));
    for (const auto& entry : std::filesystem::directory_iterator(installs))
        CHECK_FALSE(Detail::PathToUtf8(entry.path().filename()).starts_with(".keire-remove-"));
}
