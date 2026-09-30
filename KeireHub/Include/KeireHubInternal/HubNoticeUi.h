#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace Keire
{
    class UiFrame;
}

namespace KeireHub
{
    struct HubDesignTokens;

    namespace Detail
    {
        inline constexpr float HubNoticeRailHeight = 64.0F;

        void AdvanceHubNotice(std::string& notice, std::string& observedNotice,
                              std::chrono::steady_clock::time_point& noticeStarted,
                              std::chrono::steady_clock::time_point now) noexcept;
        void DrawHubNotice(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string& notice, bool noticeError,
                           std::string& observedNotice, std::chrono::steady_clock::time_point& noticeStarted);
    } // namespace Detail
} // namespace KeireHub
