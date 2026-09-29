#include "Keire/Ui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire
{
    bool UiFrame::Shortcut(const UiShortcut shortcut)
    {
        RequireActive("Shortcut");
        ImGuiKeyChord chord = ImGuiKey_None;
        switch (shortcut.Key)
        {
        case UiKey::Enter:
            chord = ImGuiKey_Enter;
            break;
        case UiKey::Escape:
            chord = ImGuiKey_Escape;
            break;
        case UiKey::Tab:
            chord = ImGuiKey_Tab;
            break;
        case UiKey::Delete:
            chord = ImGuiKey_Delete;
            break;
        case UiKey::F2:
            chord = ImGuiKey_F2;
            break;
        case UiKey::A:
            chord = ImGuiKey_A;
            break;
        case UiKey::B:
            chord = ImGuiKey_B;
            break;
        case UiKey::Backspace:
            chord = ImGuiKey_Backspace;
            break;
        case UiKey::C:
            chord = ImGuiKey_C;
            break;
        case UiKey::D:
            chord = ImGuiKey_D;
            break;
        case UiKey::Down:
            chord = ImGuiKey_DownArrow;
            break;
        case UiKey::E:
            chord = ImGuiKey_E;
            break;
        case UiKey::F:
            chord = ImGuiKey_F;
            break;
        case UiKey::Left:
            chord = ImGuiKey_LeftArrow;
            break;
        case UiKey::Q:
            chord = ImGuiKey_Q;
            break;
        case UiKey::R:
            chord = ImGuiKey_R;
            break;
        case UiKey::P:
            chord = ImGuiKey_P;
            break;
        case UiKey::Right:
            chord = ImGuiKey_RightArrow;
            break;
        case UiKey::S:
            chord = ImGuiKey_S;
            break;
        case UiKey::Up:
            chord = ImGuiKey_UpArrow;
            break;
        case UiKey::V:
            chord = ImGuiKey_V;
            break;
        case UiKey::W:
            chord = ImGuiKey_W;
            break;
        case UiKey::X:
            chord = ImGuiKey_X;
            break;
        case UiKey::Y:
            chord = ImGuiKey_Y;
            break;
        case UiKey::Z:
            chord = ImGuiKey_Z;
            break;
        }
        if (shortcut.Control)
            chord |= ImGuiMod_Ctrl;
        if (shortcut.Shift)
            chord |= ImGuiMod_Shift;
        if (shortcut.Alt)
            chord |= ImGuiMod_Alt;
        if (shortcut.Primary)
            chord |= ImGuiMod_Shortcut;
        const ImGuiInputFlags flags =
            shortcut.Global ? ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused : ImGuiInputFlags_None;
        return ImGui::Shortcut(chord, flags);
    }

    UiItemState UiFrame::LastItemState() const
    {
        RequireActive("LastItemState");
        const bool hovered = ImGui::IsItemHovered();
        return {hovered,
                ImGui::IsItemActive(),
                ImGui::IsItemActivated(),
                ImGui::IsItemEdited(),
                ImGui::IsItemDeactivatedAfterEdit(),
                hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)};
    }

    UiItemRect UiFrame::LastItemRect() const
    {
        RequireActive("LastItemRect");
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        return {{minimum.x, minimum.y}, {maximum.x, maximum.y}};
    }

    bool UiFrame::Button(std::string_view label, const UiSize size)
    {
        RequireActive("Button");
        const std::string safeLabel(label);
        return ImGui::Button(safeLabel.c_str(), {size.Width, size.Height});
    }

    bool UiFrame::Checkbox(std::string_view label, bool& value)
    {
        RequireActive("Checkbox");
        const std::string safeLabel(label);
        return ImGui::Checkbox(safeLabel.c_str(), &value);
    }

    bool UiFrame::DragInteger(const std::string_view label, std::int64_t& value, const double speed,
                              const std::optional<std::int64_t> minimum, const std::optional<std::int64_t> maximum)
    {
        RequireActive("DragInteger");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0 || (minimum && maximum && *minimum > *maximum))
            throw std::invalid_argument("DragInteger requires a label, positive speed, and ordered bounds.");
        const std::string safeLabel(label);
        const auto* minimumValue = minimum ? &*minimum : nullptr;
        const auto* maximumValue = maximum ? &*maximum : nullptr;
        return ImGui::DragScalar(safeLabel.c_str(), ImGuiDataType_S64, &value, static_cast<float>(speed), minimumValue,
                                 maximumValue, "%lld");
    }

    bool UiFrame::DragScalar(const std::string_view label, double& value, const double speed,
                             const std::optional<double> minimum, const std::optional<double> maximum)
    {
        RequireActive("DragScalar");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0 || (minimum && maximum && *minimum > *maximum))
            throw std::invalid_argument("DragScalar requires a label, positive speed, and ordered bounds.");
        const std::string safeLabel(label);
        const auto* minimumValue = minimum ? &*minimum : nullptr;
        const auto* maximumValue = maximum ? &*maximum : nullptr;
        return ImGui::DragScalar(safeLabel.c_str(), ImGuiDataType_Double, &value, static_cast<float>(speed),
                                 minimumValue, maximumValue, "%.6g");
    }

    bool UiFrame::DragVector2(const std::string_view label, Vector2& value, const float speed)
    {
        RequireActive("DragVector2");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector2 requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat2(safeLabel.c_str(), &value.X, speed, 0.0F, 0.0F, "%.3f");
    }

    bool UiFrame::DragVector3(const std::string_view label, Vector3& value, const float speed)
    {
        RequireActive("DragVector3");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector3 requires a label and a finite positive speed.");

        const std::string safeLabel(label);
        ImGui::PushID(safeLabel.c_str());
        ImGui::BeginGroup();

        const float rowStart = ImGui::GetCursorPosX();
        const float available = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
        const bool compact = available < 300.0F;
        ImGui::AlignTextToFramePadding();
        const auto visibleLabel = label.substr(0, label.find("##"));
        ImGui::TextUnformatted(visibleLabel.data(), visibleLabel.data() + visibleLabel.size());
        if (!compact)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowStart + 72.0F));
        }

        const auto drawAxis = [speed](const char* axis, float& component, const ImVec4 color, const float width)
        {
            ImGui::TextColored(color, "%s", axis);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(width);
            return ImGui::DragFloat((std::string("##") + axis).c_str(), &component, speed, 0.0F, 0.0F, "%.3f");
        };

        const auto& style = ImGui::GetStyle();
        const float controlsWidth = std::max(ImGui::GetContentRegionAvail().x, 90.0F);
        const float axisLabelWidth = ImGui::CalcTextSize("X").x;
        const float fieldWidth =
            std::max(24.0F, (controlsWidth - axisLabelWidth * 3.0F - style.ItemSpacing.x * 5.0F) / 3.0F);
        bool changed = drawAxis("X", value.X, {0.95F, 0.35F, 0.35F, 1.0F}, fieldWidth);
        ImGui::SameLine();
        changed |= drawAxis("Y", value.Y, {0.40F, 0.85F, 0.45F, 1.0F}, fieldWidth);
        ImGui::SameLine();
        changed |= drawAxis("Z", value.Z, {0.35F, 0.60F, 1.0F, 1.0F}, fieldWidth);

        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
    }

    bool UiFrame::DragVector4(const std::string_view label, Vector4& value, const float speed)
    {
        RequireActive("DragVector4");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragVector4 requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat4(safeLabel.c_str(), &value.X, speed, 0.0F, 0.0F, "%.3f");
    }
    bool UiFrame::DragQuaternion(const std::string_view label, Quaternion& value, const float speed)
    {
        RequireActive("DragQuaternion");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0F)
            throw std::invalid_argument("DragQuaternion requires a label and a finite positive speed.");
        const std::string safeLabel(label);
        return ImGui::DragFloat4(safeLabel.c_str(), &value.X, speed, -1.0F, 1.0F, "%.4f");
    }
    bool UiFrame::InputText(std::string_view label, std::string& value, const bool selectAllOnFocus)
    {
        RequireActive("InputText");
        return ImGui::InputText(std::string(label).c_str(), &value,
                                selectAllOnFocus ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None);
    }

    bool UiFrame::InputPassword(std::string_view label, std::string& value)
    {
        RequireActive("InputPassword");
        const std::string safeLabel(label);
        return ImGui::InputText(safeLabel.c_str(), &value, ImGuiInputTextFlags_Password);
    }
    bool UiFrame::InputTextWithHint(const std::string_view label, const std::string_view hint, std::string& value)
    {
        RequireActive("InputTextWithHint");
        const std::string safeLabel(label);
        const std::string safeHint(hint);
        return ImGui::InputTextWithHint(safeLabel.c_str(), safeHint.c_str(), &value);
    }
    bool UiFrame::Selectable(std::string_view label, const bool selected, const bool keepPopupOpen)
    {
        RequireActive("Selectable");
        const std::string safeLabel(label);
        const auto flags = keepPopupOpen ? ImGuiSelectableFlags_DontClosePopups : ImGuiSelectableFlags_None;
        const bool activated = ImGui::Selectable(safeLabel.c_str(), selected, flags);
        if (selected && ImGui::GetCurrentContext()->BeginComboDepth > 0)
            ImGui::SetItemDefaultFocus();
        return activated;
    }

    bool UiFrame::MenuItem(std::string_view label, const bool selected, const bool enabled)
    {
        RequireActive("MenuItem");
        const std::string safeLabel(label);
        return ImGui::MenuItem(safeLabel.c_str(), nullptr, selected, enabled);
    }

} // namespace Keire
