#include "KeireHub/HubActivationWorkflow.h"

#include "Keire/Core.h"

#include "KeireHub/HubProjectUiSupport.h"
#include "KeireHub/HubRuntimeUiBridge.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>

namespace
{
    void NotifyActivation(KeireHub::HubController* controller, const KeireHub::NotificationSeverity severity,
                          std::string title, std::string message, std::string& notice, bool& noticeError,
                          std::optional<std::string> route = {})
    {
        notice = message;
        noticeError = severity == KeireHub::NotificationSeverity::Error;
        if (!controller)
            return;

        const auto status =
            controller->Notifications().Add({.Id = "activation-" + Keire::AssetId::Generate().ToString(),
                                             .Severity = severity,
                                             .Title = std::move(title),
                                             .Message = std::move(message),
                                             .CreatedUnixSeconds = KeireHub::HubNowUnixSeconds(),
                                             .ActionRoute = std::move(route)});
        if (!status)
            KEIRE_CLIENT_WARN("[Project Hub] Could not persist an activation notification: {}", status.Error().Message);
    }

    void ImportPackage(const std::filesystem::path&, const std::filesystem::path& package,
                       KeireHub::HubController* controller, std::string& notice, bool& noticeError,
                       KeireHub::HubActivationCallbacks& callbacks)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(package, error) || error)
        {
            NotifyActivation(controller, KeireHub::NotificationSeverity::Error, "Package import failed",
                             "The requested package is missing or is not a regular file. No task was queued.", notice,
                             noticeError, "installs");
            return;
        }
        if (package.extension() != ".keireplayersupport")
        {
            NotifyActivation(
                controller, KeireHub::NotificationSeverity::Warning, "Package type unavailable",
                "This Hub can currently import validated legacy Build Support packages only. No task was queued for " +
                    KeireHub::Utf8Path(package.filename()) + ".",
                notice, noticeError, "installs");
            return;
        }
        try
        {
            callbacks.StartBuildSupportInstall(package);
        }
        catch (const std::exception& exception)
        {
            KEIRE_CLIENT_ERROR("[Project Hub] Activation package import failed: {}", exception.what());
            NotifyActivation(controller, KeireHub::NotificationSeverity::Error, "Package import failed",
                             "The Build Support package could not be queued. See the Hub logs for details.", notice,
                             noticeError, "installs");
        }
    }
} // namespace

namespace KeireHub
{
    void HubActivationWorkflow::Dispatch(HubActivationRequest request, const std::filesystem::path& hubExecutable,
                                         HubController* controller, const std::span<const HubEditorUiRecord> editors,
                                         HubPage& page, std::string& notice, bool& noticeError,
                                         HubActivationCallbacks& callbacks)
    {
        callbacks.ShowHub();
        if (const auto status = ValidateHubActivation(request); !status)
        {
            NotifyActivation(controller, NotificationSeverity::Error, "Activation request rejected",
                             "The Hub ignored an invalid activation request.", notice, noticeError);
            return;
        }

        switch (request.Action)
        {
        case HubActivationAction::Show:
            break;
        case HubActivationAction::Navigate:
            page = *request.Page;
            break;
        case HubActivationAction::OpenProject:
            page = HubPage::Projects;
            callbacks.Open(*request.Path);
            break;
        case HubActivationAction::ImportPackage:
            page = HubPage::Installs;
            ImportPackage(hubExecutable, *request.Path, controller, notice, noticeError, callbacks);
            break;
        case HubActivationAction::InstallVersion:
        {
            page = HubPage::Installs;
            const auto installed = std::ranges::find_if(
                editors, [&](const HubEditorUiRecord& editor)
                { return editor.Id == *request.VersionId || editor.Version == *request.VersionId; });
            if (installed != editors.end())
            {
                NotifyActivation(controller, NotificationSeverity::Info, "Editor already installed",
                                 installed->Version + " is already registered with this Hub.", notice, noticeError,
                                 "installs");
                break;
            }
            callbacks.RequestEditorInstall(*request.VersionId);
            NotifyActivation(controller, NotificationSeverity::Info, "Editor install requested",
                             "Looking for a verified editor catalog entry matching " + *request.VersionId + ".", notice,
                             noticeError, "installs");
            break;
        }
        case HubActivationAction::BuildSupport:
        {
            page = HubPage::Installs;
            const auto focused = callbacks.FocusBuildSupport(*request.Platform, *request.Architecture);
            if (!focused)
            {
                NotifyActivation(controller, NotificationSeverity::Warning, "Build Support unavailable",
                                 focused.Error().Message, notice, noticeError, "installs");
                break;
            }
            notice = "Showing Build Support for " + *request.Platform + " / " + *request.Architecture + ".";
            noticeError = false;
            break;
        }
        case HubActivationAction::OAuthCallback:
        {
            const auto completed = callbacks.CompleteOAuthCallback(*request.Url);
            if (!completed)
            {
                NotifyActivation(controller, NotificationSeverity::Error, "Hub sign-in not completed",
                                 completed.Error().Message, notice, noticeError);
                break;
            }
            NotifyActivation(controller, NotificationSeverity::Info, "Hub sign-in continuing",
                             "The authorization code was accepted and is being exchanged securely.", notice,
                             noticeError);
            break;
        }
        case HubActivationAction::MarketplaceProduct:
        {
            page = HubPage::Projects;
            const auto opened = callbacks.OpenMarketplaceProduct(*request.ProductId);
            if (!opened)
            {
                NotifyActivation(controller, NotificationSeverity::Error, "Marketplace asset unavailable",
                                 opened.Error().Message, notice, noticeError, "projects");
                break;
            }
            NotifyActivation(controller, NotificationSeverity::Info, "Marketplace asset requested",
                             "Kéire Hub is synchronizing My Assets and preparing a verified package for the Editor.",
                             notice, noticeError, "projects");
            break;
        }
        default:
            NotifyActivation(controller, NotificationSeverity::Error, "Activation request rejected",
                             "The Hub ignored an unsupported activation request.", notice, noticeError);
            break;
        }
    }
} // namespace KeireHub
