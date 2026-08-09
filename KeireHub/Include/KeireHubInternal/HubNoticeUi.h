#pragma once

#include <chrono>
#include <string>

namespace Keire
{
    class UiFrame;
}

namespace KeireHub
{
    struct HubDesignTokens;

    namespace Detail
    {
        void DrawHubNotice(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string& notice, bool noticeError,
                           std::string& observedNotice, std::chrono::steady_clock::time_point& noticeStarted);
    }
} // namespace KeireHub
