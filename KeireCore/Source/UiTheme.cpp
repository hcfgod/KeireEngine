#include "KeireInternal/UiThemeInternal.h"

#include <imgui.h>

namespace Keire::Detail
{
    void ApplyUiTheme(const UiTheme theme)
    {
        switch (theme)
        {
        case UiTheme::Light:
            ImGui::StyleColorsLight();
            break;
        case UiTheme::Classic:
            ImGui::StyleColorsClassic();
            break;
        case UiTheme::Dark:
        default:
            ImGui::StyleColorsDark();
            break;
        }
        auto& style = ImGui::GetStyle();
        style.WindowPadding = {7.0F, 6.0F};
        style.FramePadding = {7.0F, 4.0F};
        style.ItemSpacing = {6.0F, 4.0F};
        style.WindowRounding = 3.0F;
        style.ChildRounding = 3.0F;
        style.FrameRounding = 3.0F;
        style.PopupRounding = 3.0F;
        style.TabRounding = 2.0F;
        style.ScrollbarRounding = 3.0F;
        style.GrabRounding = 3.0F;
        style.WindowBorderSize = 1.0F;
        if (theme == UiTheme::Dark)
        {
            auto& colors = style.Colors;
            colors[ImGuiCol_WindowBg] = {0.075F, 0.078F, 0.086F, 1.0F};
            colors[ImGuiCol_ChildBg] = {0.105F, 0.11F, 0.12F, 1.0F};
            colors[ImGuiCol_PopupBg] = {0.125F, 0.13F, 0.145F, 1.0F};
            colors[ImGuiCol_FrameBg] = {0.145F, 0.15F, 0.165F, 1.0F};
            colors[ImGuiCol_FrameBgHovered] = {0.18F, 0.20F, 0.23F, 1.0F};
            colors[ImGuiCol_Button] = {0.16F, 0.17F, 0.19F, 1.0F};
            colors[ImGuiCol_ButtonHovered] = {0.20F, 0.40F, 0.70F, 1.0F};
            colors[ImGuiCol_ButtonActive] = {0.16F, 0.34F, 0.64F, 1.0F};
            colors[ImGuiCol_Header] = {0.16F, 0.34F, 0.60F, 0.65F};
            colors[ImGuiCol_HeaderHovered] = {0.20F, 0.42F, 0.74F, 0.86F};
            colors[ImGuiCol_HeaderActive] = {0.16F, 0.34F, 0.64F, 1.0F};
            colors[ImGuiCol_Border] = {0.225F, 0.23F, 0.25F, 1.0F};
            colors[ImGuiCol_CheckMark] = {0.30F, 0.58F, 1.0F, 1.0F};
            colors[ImGuiCol_SliderGrab] = {0.30F, 0.58F, 1.0F, 1.0F};
        }
    }
} // namespace Keire::Detail
