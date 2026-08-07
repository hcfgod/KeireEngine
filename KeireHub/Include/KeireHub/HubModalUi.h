#pragma once

#include "KeireHub/HubDesignTokens.h"

#include <string_view>

namespace KeireHub
{
    class HubModalStyleScope final
    {
      public:
        HubModalStyleScope(Keire::UiFrame& ui, const HubDesignTokens& tokens);

        HubModalStyleScope(const HubModalStyleScope&) = delete;
        HubModalStyleScope& operator=(const HubModalStyleScope&) = delete;

      private:
        Keire::UiStyleColorScope m_Background;
        Keire::UiStyleColorScope m_BorderColor;
        Keire::UiStyleVariableScope m_Padding;
        Keire::UiStyleVariableScope m_Rounding;
        Keire::UiStyleVariableScope m_Border;
        Keire::UiStyleVariableScope m_FramePadding;
        Keire::UiStyleVariableScope m_FrameRounding;
        Keire::UiStyleVariableScope m_ItemSpacing;
    };

    [[nodiscard]] Keire::UiWindowOptions HubModalWindowOptions() noexcept;
    void PrepareHubModal(Keire::UiFrame& ui, Keire::UiSize size);
    void DrawHubModalHeader(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string_view title,
                            std::string_view subtitle, std::string_view eyebrow = {});
    [[nodiscard]] bool HubPrimaryButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string_view label,
                                        Keire::UiSize size = {});
    [[nodiscard]] bool HubSecondaryButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string_view label,
                                          Keire::UiSize size = {});
    [[nodiscard]] bool HubDangerButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string_view label,
                                       Keire::UiSize size = {});
} // namespace KeireHub
