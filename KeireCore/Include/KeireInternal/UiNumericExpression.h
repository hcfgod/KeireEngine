#pragma once

#include "KeireInternal/NumericExpression.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <limits>
#include <type_traits>

namespace Keire::Detail
{
    template <typename T, typename Draw>
    [[nodiscard]] bool DrawNumericExpression(const char* label, T& value, const ImGuiDataType type, const char* format,
                                             const T* minimum, const T* maximum, Draw&& draw)
    {
        const auto id = ImGui::GetID(label);
        const auto original = value;
        auto candidate = value;
        bool changed = false;
        const bool editing = ImGui::TempInputIsActive(id);
        if (editing)
        {
            auto* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;
            const auto& style = ImGui::GetStyle();
            const auto labelSize = ImGui::CalcTextSize(label, nullptr, true);
            const auto position = window->DC.CursorPos;
            const ImRect frame(position, {position.x + ImGui::CalcItemWidth(),
                                          position.y + labelSize.y + style.FramePadding.y * 2.0F});
            const ImRect total(
                frame.Min, {frame.Max.x + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0), frame.Max.y});
            ImGui::ItemSize(total, style.FramePadding.y);
            if (!ImGui::ItemAdd(total, id, &frame, ImGuiItemFlags_Inputable))
                return false;
            char buffer[257]{};
            ImGui::DataTypeFormatString(buffer, sizeof(buffer), type, &candidate, format);
            changed = ImGui::TempInputText(frame, id, label, buffer, sizeof(buffer), ImGuiInputTextFlags_AutoSelectAll);
            if (labelSize.x > 0)
                ImGui::RenderText({frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y}, label);
        }
        else
            changed = draw(candidate);

        // ImGui's stock numeric parser accepts a numeric prefix. Never publish that prefix while entering an
        // expression.
        const auto* input = ImGui::GetInputTextState(id);
        if (input && (editing || ImGui::TempInputIsActive(id)))
        {
            const auto result = EvaluateNumericField<T>({input->TextA.Data, static_cast<std::size_t>(input->TextLen)});
            bool valid = result.has_value();
            if (valid)
            {
                valid = (!minimum || *result >= *minimum) && (!maximum || *result <= *maximum);
            }
            if (!valid)
            {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Enter a finite value or expression using + - * / and parentheses within the field's range.");
                return false;
            }
            if (!changed)
                return false;
            candidate = *result;
        }
        if (!changed || candidate == original)
            return false;
        value = candidate;
        ImGui::MarkItemEdited(id);
        return true;
    }

    [[nodiscard]] inline bool DrawExpressionVector(const char* label, float* values, const int count, const float speed,
                                                   const char* format, const float* minimum = nullptr,
                                                   const float* maximum = nullptr)
    {
        ImGui::BeginGroup();
        ImGui::PushID(label);
        ImGui::PushMultiItemsWidths(count, ImGui::CalcItemWidth());
        bool changed = false;
        for (int axis = 0; axis < count; ++axis)
        {
            ImGui::PushID(axis);
            if (axis != 0)
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            changed |= DrawNumericExpression("##axis", values[axis], ImGuiDataType_Float, format, minimum, maximum,
                                             [&](auto& candidate)
                                             {
                                                 return ImGui::DragScalar("##axis", ImGuiDataType_Float, &candidate,
                                                                          speed, minimum, maximum, format);
                                             });
            ImGui::PopID();
            ImGui::PopItemWidth();
        }
        ImGui::PopID();
        const auto* end = ImGui::FindRenderedTextEnd(label);
        if (end != label)
        {
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(label, end);
        }
        ImGui::EndGroup();
        return changed;
    }
} // namespace Keire::Detail
