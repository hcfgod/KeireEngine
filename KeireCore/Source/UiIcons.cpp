#include "Keire/Ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire
{
    namespace
    {
        [[nodiscard]] const char* FallbackLabel(const UiIcon icon) noexcept
        {
            switch (icon)
            {
            case UiIcon::Play:
                return ">";
            case UiIcon::Stop:
                return "[]";
            case UiIcon::Pause:
                return "||";
            case UiIcon::Step:
                return ">|";
            case UiIcon::View:
                return "Q";
            case UiIcon::Translate:
                return "W";
            case UiIcon::Rotate:
                return "E";
            case UiIcon::Scale:
                return "R";
            case UiIcon::Local:
                return "L";
            case UiIcon::Global:
                return "G";
            case UiIcon::Snap:
                return "S";
            case UiIcon::Settings:
            case UiIcon::More:
                return "...";
            case UiIcon::Camera:
                return "C";
            case UiIcon::Perspective:
                return "P";
            case UiIcon::Orthographic:
                return "O";
            case UiIcon::AxisX:
                return "X";
            case UiIcon::AxisY:
                return "Y";
            case UiIcon::AxisZ:
                return "Z";
            case UiIcon::Create:
                return "+";
            case UiIcon::Search:
                return "?";
            case UiIcon::Filter:
                return "F";
            case UiIcon::Lock:
                return "L";
            case UiIcon::Unlock:
                return "U";
            case UiIcon::Physics:
                return "P";
            case UiIcon::ColliderEdit:
                return "C";
            case UiIcon::Folder:
                return "D";
            case UiIcon::Refresh:
                return "R";
            case UiIcon::List:
                return "=";
            case UiIcon::Grid:
                return "#";
            case UiIcon::Warning:
                return "!";
            case UiIcon::Information:
                return "i";
            case UiIcon::Close:
                return "x";
            case UiIcon::Minimize:
                return "-";
            case UiIcon::Maximize:
                return "[]";
            case UiIcon::Restore:
                return "o";
            case UiIcon::Home:
                return "H";
            case UiIcon::Documentation:
            case UiIcon::Description:
                return "?";
            case UiIcon::LightMode:
                return "L";
            case UiIcon::DarkMode:
                return "D";
            case UiIcon::Notifications:
                return "!";
            case UiIcon::Download:
                return "v";
            case UiIcon::OpenExternal:
                return ">";
            case UiIcon::Favorite:
                return "*";
            case UiIcon::License:
                return "L";
            case UiIcon::Learn:
                return "?";
            case UiIcon::Build:
                return "B";
            case UiIcon::Package:
                return "P";
            case UiIcon::Bug:
                return "!";
            case UiIcon::Link:
                return "@";
            case UiIcon::Copy:
                return "=";
            }
            return "?";
        }

        [[nodiscard]] std::optional<std::uint32_t> MaterialSymbol(const UiIcon icon) noexcept
        {
            switch (icon)
            {
            case UiIcon::Pause:
                return 0xE034;
            case UiIcon::Play:
                return 0xE037;
            case UiIcon::Create:
                return 0xE145;
            case UiIcon::Camera:
                return 0xE04B;
            case UiIcon::Copy:
            case UiIcon::Restore:
                return 0xE14D;
            case UiIcon::Link:
                return 0xE250;
            case UiIcon::Folder:
                return 0xE2C7;
            case UiIcon::LightMode:
                return 0xE518;
            case UiIcon::DarkMode:
                return 0xE51C;
            case UiIcon::Close:
                return 0xE5CD;
            case UiIcon::More:
                return 0xE5D3;
            case UiIcon::Refresh:
                return 0xE5D5;
            case UiIcon::Scale:
                return 0xE56B;
            case UiIcon::AxisX:
                return 0xE5C8;
            case UiIcon::AxisY:
                return 0xF1E0;
            case UiIcon::Notifications:
                return 0xE7F5;
            case UiIcon::Learn:
                return 0xE80C;
            case UiIcon::Maximize:
            case UiIcon::Stop:
                return 0xE835;
            case UiIcon::Bug:
                return 0xE868;
            case UiIcon::Grid:
                return 0xE871;
            case UiIcon::Description:
            case UiIcon::List:
                return 0xE873;
            case UiIcon::Favorite:
                return 0xE87E;
            case UiIcon::OpenExternal:
                return 0xE89E;
            case UiIcon::Settings:
                return 0xE8B8;
            case UiIcon::Lock:
                return 0xE897;
            case UiIcon::Unlock:
                return 0xE898;
            case UiIcon::Global:
                return 0xE894;
            case UiIcon::Translate:
                return 0xE89F;
            case UiIcon::Rotate:
                return 0xE84D;
            case UiIcon::Local:
                return 0xE55C;
            case UiIcon::View:
                return 0xEF4E;
            case UiIcon::Snap:
                return 0xE3EC;
            case UiIcon::Filter:
                return 0xE8F4;
            case UiIcon::Physics:
                return 0xEA50;
            case UiIcon::ColliderEdit:
                return 0xE3C2;
            case UiIcon::Perspective:
                return 0xEA81;
            case UiIcon::Orthographic:
                return 0xE3C6;
            case UiIcon::AxisZ:
                return 0xE39E;
            case UiIcon::Minimize:
                return 0xE931;
            case UiIcon::Home:
                return 0xE9B2;
            case UiIcon::Documentation:
                return 0xEA19;
            case UiIcon::Build:
                return 0xEA3C;
            case UiIcon::License:
                return 0xEB04;
            case UiIcon::Search:
                return 0xEF7A;
            case UiIcon::Download:
                return 0xF090;
            case UiIcon::Package:
                return 0xF720;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::array<char, 4> EncodePrivateUseGlyph(const std::uint32_t codepoint) noexcept
        {
            return {static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU)),
                    static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)),
                    static_cast<char>(0x80U | (codepoint & 0x3FU)), '\0'};
        }

        [[nodiscard]] bool DrawIconButton(const std::string_view id, const char* glyph, const bool selected,
                                          const UiSize size)
        {
            const std::string label = std::string(glyph) + "##" + std::string(id);
            if (selected)
            {
                const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
                ImGui::PushStyleColor(ImGuiCol_Button, accent);
            }
            const bool activated = ImGui::Button(label.c_str(), {size.Width, size.Height});
            if (selected)
                ImGui::PopStyleColor();
            return activated;
        }

        [[nodiscard]] bool DrawOverlayIconButton(const std::string_view id, const char* glyph,
                                                 const UiOverlayIconButtonSpecification& specification)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            const ImRect bounds{{specification.Position.X, specification.Position.Y},
                                {specification.Position.X + specification.Size.Width,
                                 specification.Position.Y + specification.Size.Height}};
            const ImGuiID itemId = ImGui::GetID(id.data(), id.data() + id.size());

            ImGui::BeginDisabled(!specification.Enabled);
            bool activated = false;
            bool hovered = false;
            bool held = false;
            if (ImGui::ItemAdd(bounds, itemId))
            {
                activated = ImGui::ButtonBehavior(bounds, itemId, &hovered, &held);

                const ImGuiCol background = specification.Selected || held ? ImGuiCol_ButtonActive
                                            : hovered                      ? ImGuiCol_ButtonHovered
                                                                           : ImGuiCol_Button;
                const ImGuiStyle& style = ImGui::GetStyle();
                window->DrawList->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(background),
                                                style.FrameRounding);
                if (style.FrameBorderSize > 0.0F)
                {
                    window->DrawList->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(ImGuiCol_Border),
                                              style.FrameRounding, 0, style.FrameBorderSize);
                }

                const ImVec2 textSize = ImGui::CalcTextSize(glyph);
                const ImVec2 textPosition{bounds.Min.x + (bounds.GetWidth() - textSize.x) * 0.5F,
                                          bounds.Min.y + (bounds.GetHeight() - textSize.y) * 0.5F};
                window->DrawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), glyph);

                if (!specification.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("%.*s", static_cast<int>(specification.Tooltip.size()),
                                      specification.Tooltip.data());
                }
            }
            ImGui::EndDisabled();
            return activated;
        }
    } // namespace

    bool UiFrame::IconButton(const std::string_view id, const UiIcon icon, const bool selected, const UiSize size)
    {
        (void)ContentRect();
        if (id.empty())
            throw std::invalid_argument("IconButton requires a stable identifier.");

        if (const auto symbol = MaterialSymbol(icon))
        {
            auto font = PushFont(UiFontRole::Icons);
            if (font)
            {
                const auto glyph = EncodePrivateUseGlyph(*symbol);
                return DrawIconButton(id, glyph.data(), selected, size);
            }
        }
        return DrawIconButton(id, FallbackLabel(icon), selected, size);
    }

    bool UiFrame::OverlayIconButton(const std::string_view id, const UiIcon icon,
                                    const UiOverlayIconButtonSpecification specification)
    {
        (void)ContentRect();
        if (id.empty() || specification.Size.Width <= 0.0F || specification.Size.Height <= 0.0F)
            throw std::invalid_argument("OverlayIconButton requires an identifier and positive size.");

        if (const auto symbol = MaterialSymbol(icon))
        {
            auto font = PushFont(UiFontRole::Icons);
            if (font)
            {
                const auto glyph = EncodePrivateUseGlyph(*symbol);
                return DrawOverlayIconButton(id, glyph.data(), specification);
            }
        }
        return DrawOverlayIconButton(id, FallbackLabel(icon), specification);
    }

    void UiFrame::DrawOverlayIcon(const UiIcon icon, const UiPosition position, const UiColor color)
    {
        if (const auto symbol = MaterialSymbol(icon))
        {
            auto font = PushFont(UiFontRole::Icons);
            if (font)
            {
                const auto glyph = EncodePrivateUseGlyph(*symbol);
                DrawOverlayText(position, color, glyph.data());
                return;
            }
        }
        DrawOverlayText(position, color, FallbackLabel(icon));
    }
} // namespace Keire
