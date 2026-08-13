#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubActivation.h"
#include "KeireHubRuntime/HubController.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace KeireHub
{
    class HubActivationCallbacks
    {
      public:
        virtual ~HubActivationCallbacks() = default;

        virtual void ShowHub() = 0;
        virtual void Open(const std::filesystem::path& path) = 0;
        virtual void StartBuildSupportInstall(const std::filesystem::path& package) = 0;
        virtual void RequestEditorInstall(std::string_view packageOrVersion) = 0;
        [[nodiscard]] virtual HubStatus CompleteOAuthCallback(std::string_view callbackUrl) = 0;
        [[nodiscard]] virtual HubStatus FocusBuildSupport(std::string_view platform, std::string_view architecture) = 0;
        [[nodiscard]] virtual HubStatus OpenMarketplaceProduct(std::string_view productId) = 0;
    };

    class HubActivationWorkflow final
    {
      public:
        static void Dispatch(HubActivationRequest request, const std::filesystem::path& hubExecutable,
                             HubController* controller, std::span<const HubEditorUiRecord> editors, HubPage& page,
                             std::string& notice, bool& noticeError, HubActivationCallbacks& callbacks);
    };
} // namespace KeireHub
