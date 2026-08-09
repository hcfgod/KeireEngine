#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumProtocolBytes = std::size_t{32U} * 1024U * 1024U;

        [[nodiscard]] std::filesystem::path DecodeHubOwnedPath(const std::string_view value)
        {
            const auto path = Detail::PathFromUtf8(value);
            const std::filesystem::path legacy(std::u8string(u8"KÃ©ire"));
            const std::filesystem::path canonical(std::u8string(u8"Kéire"));
            std::filesystem::path repaired;
            for (const auto& component : path)
                repaired /= component == legacy ? canonical : component;
            return repaired;
        }

        [[nodiscard]] HubError ProtocolError(const std::filesystem::path& path, const std::string_view details)
        {
            return {.Code = HubErrorCode::WorkerProtocolInvalid,
                    .Message = "The Hub worker operation journal is invalid.",
                    .AffectedItem = Detail::PathToUtf8(path.filename()),
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] bool IsSafeRelativeProtocolPath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
                !path.has_filename())
                return false;
            try
            {
                if (path.generic_u8string().size() > 4096U)
                    return false;
            }
            catch (...)
            {
                return false;
            }
            return std::ranges::none_of(path, [](const std::filesystem::path& component)
                                        { return component == "." || component == ".."; });
        }

        [[nodiscard]] std::string_view StateName(const HubTaskState state) noexcept
        {
            switch (state)
            {
            case HubTaskState::Downloading:
                return "downloading";
            case HubTaskState::Paused:
                return "paused";
            case HubTaskState::Verifying:
                return "verifying";
            case HubTaskState::Extracting:
                return "extracting";
            case HubTaskState::Installing:
                return "installing";
            case HubTaskState::Configuring:
                return "configuring";
            case HubTaskState::Completed:
                return "completed";
            case HubTaskState::Failed:
                return "failed";
            case HubTaskState::Cancelled:
                return "cancelled";
            default:
                return "invalid";
            }
        }

        [[nodiscard]] std::optional<HubTaskState> ParseState(const std::string_view value) noexcept
        {
            constexpr std::array values{HubTaskState::Downloading, HubTaskState::Paused,     HubTaskState::Verifying,
                                        HubTaskState::Extracting,  HubTaskState::Installing, HubTaskState::Configuring,
                                        HubTaskState::Completed,   HubTaskState::Failed,     HubTaskState::Cancelled};
            for (const auto state : values)
            {
                if (StateName(state) == value)
                    return state;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string_view OutcomeName(const DownloadOutcome outcome) noexcept
        {
            switch (outcome)
            {
            case DownloadOutcome::Completed:
                return "completed";
            case DownloadOutcome::Paused:
                return "paused";
            case DownloadOutcome::Cancelled:
                return "cancelled";
            case DownloadOutcome::Failed:
                return "failed";
            }
            return "invalid";
        }

        [[nodiscard]] std::optional<DownloadOutcome> ParseOutcome(const std::string_view value) noexcept
        {
            constexpr std::array values{DownloadOutcome::Completed, DownloadOutcome::Paused, DownloadOutcome::Cancelled,
                                        DownloadOutcome::Failed};
            for (const auto outcome : values)
            {
                if (OutcomeName(outcome) == value)
                    return outcome;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string_view EditorInstallModeName(const HubWorkerEditorInstallMode mode) noexcept
        {
            return mode == HubWorkerEditorInstallMode::Repair ? "repair" : "install";
        }

        [[nodiscard]] std::optional<HubWorkerEditorInstallMode>
        ParseEditorInstallMode(const std::string_view value) noexcept
        {
            if (value == "install")
                return HubWorkerEditorInstallMode::Install;
            if (value == "repair")
                return HubWorkerEditorInstallMode::Repair;
            return std::nullopt;
        }

        [[nodiscard]] Detail::Json EncodeError(const HubError& error)
        {
            return {{"code", ToString(error.Code)},
                    {"message", error.Message},
                    {"retryable", error.Retryable},
                    {"affectedItem", error.AffectedItem},
                    {"technicalDetails", error.TechnicalDetails},
                    {"logReference", error.LogReference}};
        }

        [[nodiscard]] std::optional<HubError> DecodeError(const Detail::Json& value, std::string& reason)
        {
            const auto code = ParseHubErrorCode(value.value("code", std::string{}));
            if (!code)
            {
                reason = "Unknown Hub error code.";
                return std::nullopt;
            }
            HubError result{.Code = *code,
                            .Message = value.value("message", std::string{}),
                            .Retryable = value.value("retryable", false),
                            .AffectedItem = value.value("affectedItem", std::string{}),
                            .TechnicalDetails = value.value("technicalDetails", std::string{}),
                            .LogReference = value.value("logReference", std::string{})};
            if (result.Message.empty() || result.Message.size() > 4096 || result.AffectedItem.size() > 512 ||
                result.TechnicalDetails.size() > 8192 || result.LogReference.size() > 512)
            {
                reason = "Invalid Hub error fields.";
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] DownloadRequest DecodeDownload(const Detail::Json& value)
        {
            const auto& retry = value.at("retry");
            DownloadRequest result{
                .PackageId = value.at("packageId").get<std::string>(),
                .Url = value.at("url").get<std::string>(),
                .Sha256 = value.at("sha256").get<std::string>(),
                .SizeBytes = value.at("sizeBytes").get<std::uint64_t>(),
                .CacheRoot = DecodeHubOwnedPath(value.at("cacheRoot").get<std::string>()),
                .Retry = {.MaximumAttempts = retry.at("maximumAttempts").get<std::uint32_t>(),
                          .BaseDelay = std::chrono::milliseconds(retry.at("baseDelayMs").get<std::int64_t>()),
                          .MaximumDelay = std::chrono::milliseconds(retry.at("maximumDelayMs").get<std::int64_t>()),
                          .JitterPermille = retry.at("jitterPermille").get<std::uint32_t>()},
                .AllowInsecureLoopbackDevelopment = value.value("allowInsecureLoopbackDevelopment", false),
                .BandwidthLimitBytesPerSecond = value.value("bandwidthLimitBytesPerSecond", std::uint64_t{0})};
            if (value.contains("customProxyUrl"))
                result.CustomProxyUrl = value.at("customProxyUrl").get<std::string>();
            return result;
        }

        [[nodiscard]] Detail::Json EncodeDownload(const DownloadRequest& request)
        {
            Detail::Json result{{"packageId", request.PackageId},
                                {"url", request.Url},
                                {"sha256", request.Sha256},
                                {"sizeBytes", request.SizeBytes},
                                {"cacheRoot", Detail::PathToUtf8(request.CacheRoot)},
                                {"allowInsecureLoopbackDevelopment", request.AllowInsecureLoopbackDevelopment},
                                {"retry",
                                 {{"maximumAttempts", request.Retry.MaximumAttempts},
                                  {"baseDelayMs", request.Retry.BaseDelay.count()},
                                  {"maximumDelayMs", request.Retry.MaximumDelay.count()},
                                  {"jitterPermille", request.Retry.JitterPermille}}}};
            if (request.CustomProxyUrl)
                result["customProxyUrl"] = *request.CustomProxyUrl;
            if (request.BandwidthLimitBytesPerSecond != 0)
                result["bandwidthLimitBytesPerSecond"] = request.BandwidthLimitBytesPerSecond;
            return result;
        }

        [[nodiscard]] bool SameDownload(const DownloadRequest& left, const DownloadRequest& right) noexcept
        {
            return left.PackageId == right.PackageId && left.Url == right.Url && left.Sha256 == right.Sha256 &&
                   left.SizeBytes == right.SizeBytes && left.CacheRoot == right.CacheRoot &&
                   left.Retry.MaximumAttempts == right.Retry.MaximumAttempts &&
                   left.Retry.BaseDelay == right.Retry.BaseDelay &&
                   left.Retry.MaximumDelay == right.Retry.MaximumDelay &&
                   left.Retry.JitterPermille == right.Retry.JitterPermille &&
                   left.AllowInsecureLoopbackDevelopment == right.AllowInsecureLoopbackDevelopment &&
                   left.CustomProxyUrl == right.CustomProxyUrl &&
                   left.BandwidthLimitBytesPerSecond == right.BandwidthLimitBytesPerSecond;
        }

        [[nodiscard]] HubResult<Detail::Json> ReadDocument(const std::filesystem::path& path)
        {
            auto document = Detail::ReadJsonFile(path, MaximumProtocolBytes);
            if (!document)
            {
                if (document.Error().Code == HubErrorCode::IoRead)
                    return HubResult<Detail::Json>::Failure(document.Error());
                return HubResult<Detail::Json>::Failure(ProtocolError(path, document.Error().TechnicalDetails));
            }
            return document;
        }

        [[nodiscard]] HubStatus ValidateEditorInstall(const HubWorkerRequest& request)
        {
            if (!request.EditorInstall)
                return HubStatus::Success();
            const auto& install = *request.EditorInstall;
            const auto allowedRoot = install.AllowedInstallRoot.lexically_normal();
            const auto destination = install.Destination.lexically_normal();
            const bool nonceValid = install.MarkerNonce.size() >= 32 && install.MarkerNonce.size() <= 256 &&
                                    std::ranges::all_of(install.MarkerNonce, [](const unsigned char value)
                                                        { return std::isxdigit(value) != 0; });
            const bool repair = install.Mode == HubWorkerEditorInstallMode::Repair;
            if (install.Mode != HubWorkerEditorInstallMode::Install &&
                install.Mode != HubWorkerEditorInstallMode::Repair)
                return HubStatus::Failure(ProtocolError({}, "Invalid managed editor operation mode."));
            const bool repairAuthorizationValid =
                install.RepairAuthorization && Detail::IsSha256(install.RepairAuthorization->ManifestFingerprint) &&
                Detail::IsSha256(install.RepairAuthorization->PackageTreeIdentity) &&
                Detail::IsSha256(install.RepairAuthorization->PackageReceiptSha256) &&
                IsSafeRelativeProtocolPath(install.RepairAuthorization->EditorEntrypoint);
            if ((repair && !repairAuthorizationValid) || (!repair && install.RepairAuthorization))
                return HubStatus::Failure(ProtocolError({}, "Invalid managed editor repair authorization."));
            if (const auto status = ValidatePackageManifest(install.Package); !status)
                return status;
            if (install.PackageSteps.empty() || install.PackageSteps.size() > MaximumHubWorkerInstallPackageSteps ||
                !SameDownload(request.Download, install.PackageSteps.front().Download))
            {
                return HubStatus::Failure(ProtocolError({}, "Invalid managed editor package plan."));
            }
            std::set<std::string, std::less<>> packageIds;
            std::map<std::string, std::size_t, std::less<>> packageIndices;
            std::size_t editorMatches = 0;
            std::uint64_t totalDownloadBytes = 0;
            auto encodedEditor = EncodePackageManifest(install.Package);
            if (!encodedEditor)
                return HubStatus::Failure(encodedEditor.Error());
            for (const auto& step : install.PackageSteps)
            {
                const auto stepIndex = packageIndices.size();
                if (const auto status = ValidatePackageManifest(step.Package); !status)
                    return status;
                if (const auto status = DownloadManager::Validate(step.Download); !status)
                    return status;
                if (step.Package.Id != step.Download.PackageId || step.Package.ArtifactSha256 != step.Download.Sha256 ||
                    step.Package.ArtifactSizeBytes != step.Download.SizeBytes ||
                    !packageIds.insert(step.Package.Id).second ||
                    (step.Package.Platform != "any" && step.Package.Platform != install.HostPlatform) ||
                    (step.Package.Architecture != "any" && step.Package.Architecture != install.HostArchitecture))
                {
                    return HubStatus::Failure(ProtocolError({}, "Invalid package in managed editor plan."));
                }
                packageIndices.emplace(step.Package.Id, stepIndex);
                if (totalDownloadBytes > std::numeric_limits<std::uint64_t>::max() - step.Download.SizeBytes)
                    return HubStatus::Failure(ProtocolError({}, "Managed editor package sizes overflow."));
                totalDownloadBytes += step.Download.SizeBytes;
                if (step.Package.Id == install.Package.Id)
                {
                    auto encodedStep = EncodePackageManifest(step.Package);
                    if (!encodedStep || encodedStep.Value() != encodedEditor.Value())
                        return HubStatus::Failure(ProtocolError({}, "Editor package plan identity mismatch."));
                    ++editorMatches;
                }
            }
            std::set<std::string, std::less<>> requestedIds;
            if (install.RequestedPackageIds.empty() || install.RequestedPackageIds.size() > install.PackageSteps.size())
            {
                return HubStatus::Failure(ProtocolError({}, "Invalid requested package roots."));
            }
            std::vector<std::size_t> reachabilityStack;
            reachabilityStack.reserve(install.PackageSteps.size());
            for (const auto& requestedId : install.RequestedPackageIds)
            {
                const auto found = packageIndices.find(requestedId);
                if (!Detail::IsBoundedIdentifier(requestedId) || found == packageIndices.end() ||
                    !requestedIds.insert(requestedId).second)
                {
                    return HubStatus::Failure(ProtocolError({}, "Invalid requested package roots."));
                }
                reachabilityStack.push_back(found->second);
            }
            if (!requestedIds.contains(install.Package.Id))
                return HubStatus::Failure(ProtocolError({}, "The editor package is not a requested root."));

            for (std::size_t index = 0; index < install.PackageSteps.size(); ++index)
            {
                const auto& step = install.PackageSteps[index];
                for (const auto& dependency : step.Package.Dependencies)
                {
                    const auto found = packageIndices.find(dependency.PackageId);
                    if (found == packageIndices.end() || found->second >= index ||
                        !dependency.Versions.Matches(install.PackageSteps[found->second].Package.Version))
                    {
                        return HubStatus::Failure(ProtocolError({}, "The managed editor dependency order is invalid."));
                    }
                }
                for (const auto& conflict : step.Package.Conflicts)
                {
                    const auto found = packageIndices.find(conflict.PackageId);
                    if (found != packageIndices.end() &&
                        conflict.Versions.Matches(install.PackageSteps[found->second].Package.Version))
                    {
                        return HubStatus::Failure(ProtocolError({}, "The managed editor package plan conflicts."));
                    }
                }
            }
            std::vector<bool> reachable(install.PackageSteps.size());
            while (!reachabilityStack.empty())
            {
                const auto index = reachabilityStack.back();
                reachabilityStack.pop_back();
                if (reachable[index])
                    continue;
                reachable[index] = true;
                for (const auto& dependency : install.PackageSteps[index].Package.Dependencies)
                    reachabilityStack.push_back(packageIndices.at(dependency.PackageId));
            }
            if (std::ranges::any_of(reachable, [](const bool value) { return !value; }))
                return HubStatus::Failure(
                    ProtocolError({}, "The managed editor plan contains an unrequested package."));
            if (install.Package.Kind != PackageKind::Editor || editorMatches != 1 || allowedRoot.empty() ||
                !allowedRoot.is_absolute() || allowedRoot == allowedRoot.root_path() || destination.empty() ||
                !destination.is_absolute() || destination.parent_path() != allowedRoot ||
                destination.filename().empty() || !Detail::IsBoundedIdentifier(install.InstallationId) || !nonceValid ||
                (install.HostPlatform != "windows" && install.HostPlatform != "linux" &&
                 install.HostPlatform != "macos") ||
                (install.HostArchitecture != "x86_64" && install.HostArchitecture != "arm64") ||
                install.Package.Platform != install.HostPlatform ||
                install.Package.Architecture != install.HostArchitecture || install.VerifiedUnixSeconds == 0)
            {
                return HubStatus::Failure(ProtocolError({}, "Invalid managed editor install request."));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateEditorRemoval(const HubWorkerRequest& request)
        {
            if (!request.EditorRemoval)
                return HubStatus::Success();
            const auto& removal = *request.EditorRemoval;
            const auto parent = removal.AllowedInstallRoot.lexically_normal();
            const auto root = removal.Root.lexically_normal();
            const bool nonceValid = removal.MarkerNonce.size() >= 32 && removal.MarkerNonce.size() <= 256 &&
                                    std::ranges::all_of(removal.MarkerNonce, [](const unsigned char value)
                                                        { return std::isxdigit(value) != 0; });
            if (request.EditorInstall || parent.empty() || !parent.is_absolute() || parent == parent.root_path() ||
                root.empty() || !root.is_absolute() || root == root.root_path() || root.parent_path() != parent ||
                root.filename().empty() || !Detail::IsBoundedIdentifier(removal.InstallationId) ||
                !Detail::IsSha256(removal.ManifestFingerprint) || !Detail::IsSha256(removal.PackageTreeIdentity) ||
                !Detail::IsSha256(removal.PackageReceiptSha256) || !nonceValid || !request.Download.PackageId.empty() ||
                !request.Download.Url.empty() || !request.Download.Sha256.empty() || request.Download.SizeBytes != 0 ||
                !request.Download.CacheRoot.empty() || request.Download.CustomProxyUrl ||
                request.Download.BandwidthLimitBytesPerSecond != 0)
            {
                return HubStatus::Failure(ProtocolError({}, "Invalid managed editor removal request."));
            }
            return HubStatus::Success();
        }
    } // namespace

    HubStatus ValidateHubWorkerRequest(const HubWorkerRequest& request)
    {
        if (!Detail::IsBoundedIdentifier(request.TaskId))
            return HubStatus::Failure(ProtocolError({}, "Invalid task identity."));
        if (request.EditorRemoval)
            return ValidateEditorRemoval(request);
        if (auto status = DownloadManager::Validate(request.Download); !status)
            return status;
        return ValidateEditorInstall(request);
    }

    HubResult<HubWorkerRequest> ReadHubWorkerRequest(const std::filesystem::path& path)
    {
        auto document = ReadDocument(path);
        if (!document)
            return HubResult<HubWorkerRequest>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            if (value.at("schemaVersion").get<std::uint32_t>() != HubWorkerRequest::CurrentSchemaVersion)
                throw std::invalid_argument("Unsupported worker request schema.");
            HubWorkerRequest result;
            result.TaskId = value.at("taskId").get<std::string>();
            if (value.contains("download"))
                result.Download = DecodeDownload(value.at("download"));
            if (value.contains("editorInstall"))
            {
                const auto& install = value.at("editorInstall");
                auto package = ParsePackageManifest(install.at("package").dump());
                if (!package)
                    throw std::invalid_argument(package.Error().Message);
                result.EditorInstall = {.Package = std::move(package).Value(),
                                        .Mode = HubWorkerEditorInstallMode::Install,
                                        .AllowedInstallRoot =
                                            DecodeHubOwnedPath(install.at("allowedInstallRoot").get<std::string>()),
                                        .Destination = DecodeHubOwnedPath(install.at("destination").get<std::string>()),
                                        .InstallationId = install.at("installationId").get<std::string>(),
                                        .MarkerNonce = install.at("markerNonce").get<std::string>(),
                                        .HostPlatform = install.at("hostPlatform").get<std::string>(),
                                        .HostArchitecture = install.at("hostArchitecture").get<std::string>(),
                                        .VerifiedUnixSeconds = install.at("verifiedUnixSeconds").get<std::uint64_t>()};
                if (install.contains("mode"))
                {
                    const auto mode = ParseEditorInstallMode(install.at("mode").get<std::string>());
                    if (!mode)
                        throw std::invalid_argument("Unknown editor install mode.");
                    result.EditorInstall->Mode = *mode;
                }
                if (install.contains("repairAuthorization"))
                {
                    const auto& authorization = install.at("repairAuthorization");
                    result.EditorInstall->RepairAuthorization = {
                        .ManifestFingerprint = authorization.at("manifestFingerprint").get<std::string>(),
                        .PackageTreeIdentity = authorization.at("packageTreeIdentity").get<std::string>(),
                        .PackageReceiptSha256 = authorization.at("packageReceiptSha256").get<std::string>(),
                        .EditorEntrypoint =
                            Detail::PathFromUtf8(authorization.at("editorEntrypoint").get<std::string>())};
                }
                if (install.contains("packageSteps"))
                {
                    for (const auto& encodedStep : install.at("packageSteps"))
                    {
                        auto stepPackage = ParsePackageManifest(encodedStep.at("package").dump());
                        if (!stepPackage)
                            throw std::invalid_argument(stepPackage.Error().Message);
                        result.EditorInstall->PackageSteps.push_back(
                            {.Package = std::move(stepPackage).Value(),
                             .Download = DecodeDownload(encodedStep.at("download"))});
                    }
                }
                else
                {
                    result.EditorInstall->PackageSteps.push_back(
                        {.Package = result.EditorInstall->Package, .Download = result.Download});
                }
                if (install.contains("requestedPackageIds"))
                    result.EditorInstall->RequestedPackageIds =
                        install.at("requestedPackageIds").get<std::vector<std::string>>();
                else
                    result.EditorInstall->RequestedPackageIds = {result.EditorInstall->Package.Id};
            }
            if (value.contains("editorRemoval"))
            {
                const auto& removal = value.at("editorRemoval");
                result.EditorRemoval = {.AllowedInstallRoot =
                                            DecodeHubOwnedPath(removal.at("allowedInstallRoot").get<std::string>()),
                                        .Root = DecodeHubOwnedPath(removal.at("root").get<std::string>()),
                                        .InstallationId = removal.at("installationId").get<std::string>(),
                                        .ManifestFingerprint = removal.at("manifestFingerprint").get<std::string>(),
                                        .PackageTreeIdentity = removal.at("packageTreeIdentity").get<std::string>(),
                                        .PackageReceiptSha256 = removal.at("packageReceiptSha256").get<std::string>(),
                                        .MarkerNonce = removal.at("markerNonce").get<std::string>()};
            }
            if (auto status = ValidateHubWorkerRequest(result); !status)
                throw std::invalid_argument(status.Error().Message + " " + status.Error().TechnicalDetails);
            return HubResult<HubWorkerRequest>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubWorkerRequest>::Failure(ProtocolError(path, error.what()));
        }
    }

    HubStatus WriteHubWorkerRequest(const std::filesystem::path& path, const HubWorkerRequest& request)
    {
        if (auto status = ValidateHubWorkerRequest(request); !status)
            return status;
        Detail::Json document{{"schemaVersion", HubWorkerRequest::CurrentSchemaVersion}, {"taskId", request.TaskId}};
        if (!request.EditorRemoval)
            document["download"] = EncodeDownload(request.Download);
        if (request.EditorInstall)
        {
            auto package = EncodePackageManifest(request.EditorInstall->Package);
            if (!package)
                return HubStatus::Failure(package.Error());
            Detail::Json packageSteps = Detail::Json::array();
            for (const auto& step : request.EditorInstall->PackageSteps)
            {
                auto stepPackage = EncodePackageManifest(step.Package);
                if (!stepPackage)
                    return HubStatus::Failure(stepPackage.Error());
                packageSteps.push_back({{"package", Detail::Json::parse(stepPackage.Value())},
                                        {"download", EncodeDownload(step.Download)}});
            }
            document["editorInstall"] = {
                {"package", Detail::Json::parse(package.Value())},
                {"mode", EditorInstallModeName(request.EditorInstall->Mode)},
                {"packageSteps", std::move(packageSteps)},
                {"requestedPackageIds", request.EditorInstall->RequestedPackageIds},
                {"allowedInstallRoot", Detail::PathToUtf8(request.EditorInstall->AllowedInstallRoot)},
                {"destination", Detail::PathToUtf8(request.EditorInstall->Destination)},
                {"installationId", request.EditorInstall->InstallationId},
                {"markerNonce", request.EditorInstall->MarkerNonce},
                {"hostPlatform", request.EditorInstall->HostPlatform},
                {"hostArchitecture", request.EditorInstall->HostArchitecture},
                {"verifiedUnixSeconds", request.EditorInstall->VerifiedUnixSeconds}};
            if (request.EditorInstall->RepairAuthorization)
            {
                const auto& authorization = *request.EditorInstall->RepairAuthorization;
                document["editorInstall"]["repairAuthorization"] = {
                    {"manifestFingerprint", authorization.ManifestFingerprint},
                    {"packageTreeIdentity", authorization.PackageTreeIdentity},
                    {"packageReceiptSha256", authorization.PackageReceiptSha256},
                    {"editorEntrypoint", Detail::PathToUtf8(authorization.EditorEntrypoint)}};
            }
        }
        if (request.EditorRemoval)
        {
            document["editorRemoval"] = {
                {"allowedInstallRoot", Detail::PathToUtf8(request.EditorRemoval->AllowedInstallRoot)},
                {"root", Detail::PathToUtf8(request.EditorRemoval->Root)},
                {"installationId", request.EditorRemoval->InstallationId},
                {"manifestFingerprint", request.EditorRemoval->ManifestFingerprint},
                {"packageTreeIdentity", request.EditorRemoval->PackageTreeIdentity},
                {"packageReceiptSha256", request.EditorRemoval->PackageReceiptSha256},
                {"markerNonce", request.EditorRemoval->MarkerNonce}};
        }
        return Detail::WriteJsonFileAtomically(path, document);
    }

    HubResult<HubWorkerStatus> ReadHubWorkerStatus(const std::filesystem::path& path)
    {
        auto document = ReadDocument(path);
        if (!document)
            return HubResult<HubWorkerStatus>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            const auto state = ParseState(value.at("state").get<std::string>());
            if (value.at("schemaVersion").get<std::uint32_t>() != HubWorkerStatus::CurrentSchemaVersion || !state)
                throw std::invalid_argument("Unsupported status schema or state.");
            const auto& progress = value.at("progress");
            HubWorkerStatus result{.TaskId = value.at("taskId").get<std::string>(),
                                   .State = *state,
                                   .Progress = {.BytesTransferred = progress.value("bytesTransferred", 0ULL),
                                                .TotalBytes = progress.value("totalBytes", 0ULL),
                                                .BytesPerSecond = progress.value("bytesPerSecond", 0ULL),
                                                .Attempt = progress.value("attempt", 0U),
                                                .CurrentPackage = progress.value("currentPackage", std::string{}),
                                                .RemainingComponents = progress.value("remainingComponents", 0U),
                                                .Phase = progress.value("phase", std::string{})},
                                   .WorkerProcessId = value.at("workerProcessId").get<std::uint64_t>(),
                                   .UpdatedUnixSeconds = value.at("updated").get<std::uint64_t>()};
            if (!Detail::IsBoundedIdentifier(result.TaskId) || result.WorkerProcessId == 0 ||
                result.Progress.Attempt > 1'000'000U || result.Progress.CurrentPackage.size() > 256 ||
                result.Progress.Phase.size() > 256 ||
                (result.Progress.TotalBytes != 0 && result.Progress.BytesTransferred > result.Progress.TotalBytes))
            {
                throw std::invalid_argument("Invalid worker status fields.");
            }
            return HubResult<HubWorkerStatus>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubWorkerStatus>::Failure(ProtocolError(path, error.what()));
        }
    }

    HubStatus WriteHubWorkerStatus(const std::filesystem::path& path, const HubWorkerStatus& status)
    {
        if (!Detail::IsBoundedIdentifier(status.TaskId) || status.WorkerProcessId == 0 ||
            StateName(status.State) == "invalid" || status.Progress.Attempt > 1'000'000U ||
            status.Progress.CurrentPackage.size() > 256 || status.Progress.Phase.size() > 256 ||
            (status.Progress.TotalBytes != 0 && status.Progress.BytesTransferred > status.Progress.TotalBytes))
        {
            return HubStatus::Failure(ProtocolError(path, "Invalid worker status fields."));
        }
        return Detail::WriteJsonFileAtomically(path, {{"schemaVersion", HubWorkerStatus::CurrentSchemaVersion},
                                                      {"taskId", status.TaskId},
                                                      {"state", StateName(status.State)},
                                                      {"workerProcessId", status.WorkerProcessId},
                                                      {"updated", status.UpdatedUnixSeconds},
                                                      {"progress",
                                                       {{"bytesTransferred", status.Progress.BytesTransferred},
                                                        {"totalBytes", status.Progress.TotalBytes},
                                                        {"bytesPerSecond", status.Progress.BytesPerSecond},
                                                        {"attempt", status.Progress.Attempt},
                                                        {"currentPackage", status.Progress.CurrentPackage},
                                                        {"remainingComponents", status.Progress.RemainingComponents},
                                                        {"phase", status.Progress.Phase}}}});
    }

    HubResult<HubWorkerResult> ReadHubWorkerResult(const std::filesystem::path& path)
    {
        auto document = ReadDocument(path);
        if (!document)
            return HubResult<HubWorkerResult>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            const auto outcome = ParseOutcome(value.at("outcome").get<std::string>());
            if (value.at("schemaVersion").get<std::uint32_t>() != HubWorkerResult::CurrentSchemaVersion || !outcome)
                throw std::invalid_argument("Unsupported result schema or outcome.");
            HubWorkerResult result{.TaskId = value.at("taskId").get<std::string>(), .Outcome = *outcome};
            if (value.contains("cachePath"))
                result.CachePath = DecodeHubOwnedPath(value.at("cachePath").get<std::string>());
            if (value.contains("installedRoot"))
                result.InstalledRoot = DecodeHubOwnedPath(value.at("installedRoot").get<std::string>());
            if (value.contains("removedRoot"))
                result.RemovedRoot = DecodeHubOwnedPath(value.at("removedRoot").get<std::string>());
            if (value.contains("installationId"))
                result.InstallationId = value.at("installationId").get<std::string>();
            if (value.contains("failure"))
            {
                std::string reason;
                auto failure = DecodeError(value.at("failure"), reason);
                if (!failure)
                    throw std::invalid_argument(reason);
                result.Failure = std::move(*failure);
            }
            const bool hasInstalledResult = !result.InstalledRoot.empty();
            const bool hasRemovedResult = !result.RemovedRoot.empty();
            const bool hasInstallationIdentity = !result.InstallationId.empty();
            if (!Detail::IsBoundedIdentifier(result.TaskId) ||
                (result.Outcome == DownloadOutcome::Completed &&
                 ((result.CachePath.empty() && !hasRemovedResult) || result.Failure)) ||
                (result.Outcome == DownloadOutcome::Failed && !result.Failure) ||
                (result.Outcome != DownloadOutcome::Completed && result.Outcome != DownloadOutcome::Failed &&
                 result.Failure) ||
                (hasInstalledResult && hasRemovedResult) ||
                (hasInstallationIdentity != (hasInstalledResult || hasRemovedResult)) ||
                ((hasInstalledResult || hasRemovedResult) &&
                 (result.Outcome != DownloadOutcome::Completed ||
                  (hasInstalledResult && !result.InstalledRoot.is_absolute()) ||
                  (hasRemovedResult && !result.RemovedRoot.is_absolute()) ||
                  !Detail::IsBoundedIdentifier(result.InstallationId))))
            {
                throw std::invalid_argument("Invalid worker result fields.");
            }
            return HubResult<HubWorkerResult>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<HubWorkerResult>::Failure(ProtocolError(path, error.what()));
        }
    }

    HubStatus WriteHubWorkerResult(const std::filesystem::path& path, const HubWorkerResult& result)
    {
        const bool hasInstalledResult = !result.InstalledRoot.empty();
        const bool hasRemovedResult = !result.RemovedRoot.empty();
        const bool hasInstallationIdentity = !result.InstallationId.empty();
        if (!Detail::IsBoundedIdentifier(result.TaskId) ||
            (result.Outcome == DownloadOutcome::Completed &&
             ((result.CachePath.empty() && !hasRemovedResult) || result.Failure)) ||
            (result.Outcome == DownloadOutcome::Failed && !result.Failure) ||
            (result.Outcome != DownloadOutcome::Completed && result.Outcome != DownloadOutcome::Failed &&
             result.Failure) ||
            (hasInstalledResult && hasRemovedResult) ||
            (hasInstallationIdentity != (hasInstalledResult || hasRemovedResult)) ||
            ((hasInstalledResult || hasRemovedResult) && (result.Outcome != DownloadOutcome::Completed ||
                                                          (hasInstalledResult && !result.InstalledRoot.is_absolute()) ||
                                                          (hasRemovedResult && !result.RemovedRoot.is_absolute()) ||
                                                          !Detail::IsBoundedIdentifier(result.InstallationId))))
        {
            return HubStatus::Failure(ProtocolError(path, "Invalid worker result fields."));
        }
        Detail::Json document{{"schemaVersion", HubWorkerResult::CurrentSchemaVersion},
                              {"taskId", result.TaskId},
                              {"outcome", OutcomeName(result.Outcome)}};
        if (!result.CachePath.empty())
            document["cachePath"] = Detail::PathToUtf8(result.CachePath);
        if (!result.InstalledRoot.empty())
            document["installedRoot"] = Detail::PathToUtf8(result.InstalledRoot);
        if (!result.RemovedRoot.empty())
            document["removedRoot"] = Detail::PathToUtf8(result.RemovedRoot);
        if (!result.InstallationId.empty())
            document["installationId"] = result.InstallationId;
        if (result.Failure)
            document["failure"] = EncodeError(*result.Failure);
        return Detail::WriteJsonFileAtomically(path, document);
    }

    HubResult<DownloadControl> ReadHubWorkerControl(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return HubResult<DownloadControl>::Success(DownloadControl::Continue);
        auto document = ReadDocument(path);
        if (!document)
            return HubResult<DownloadControl>::Failure(document.Error());
        try
        {
            const auto& value = document.Value();
            if (value.at("schemaVersion").get<std::uint32_t>() != 1U)
                throw std::invalid_argument("Unsupported worker control schema.");
            const auto action = value.at("action").get<std::string>();
            if (action == "continue")
                return HubResult<DownloadControl>::Success(DownloadControl::Continue);
            if (action == "pause")
                return HubResult<DownloadControl>::Success(DownloadControl::Pause);
            if (action == "cancel")
                return HubResult<DownloadControl>::Success(DownloadControl::Cancel);
            throw std::invalid_argument("Unknown worker control action.");
        }
        catch (const std::exception& error)
        {
            return HubResult<DownloadControl>::Failure(ProtocolError(path, error.what()));
        }
    }

    HubStatus WriteHubWorkerControl(const std::filesystem::path& path, const DownloadControl control)
    {
        std::string_view action = "continue";
        if (control == DownloadControl::Pause)
            action = "pause";
        else if (control == DownloadControl::Cancel)
            action = "cancel";
        return Detail::WriteJsonFileAtomically(path, {{"schemaVersion", 1}, {"action", action}});
    }
} // namespace KeireHub
