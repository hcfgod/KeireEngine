#pragma once

#include "KeireInternal/NumericExpression.h"
#include "KeireInternal/UiScalarFormat.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>

namespace Keire::Detail
{
    static_assert(MaximumScalarDecimalCharacters <= MaximumNumericLiteralCharacters);

    template <typename T, typename Draw>
    [[nodiscard]] bool DrawNumericExpression(const char* label, T& value, const ImGuiDataType type, const char* format,
                                             const T* minimum, const T* maximum, Draw&& draw)
    {
        const auto id = ImGui::GetID(label);
        const auto original = value;
        auto candidate = value;
        bool changed = false;
        const bool editing = ImGui::TempInputIsActive(id);
        auto* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return false;
        }
        const auto& style = ImGui::GetStyle();
        const auto labelSize = ImGui::CalcTextSize(label, nullptr, true);
        const auto position = window->DC.CursorPos;
        const ImRect decimalFrame(
            position, {position.x + ImGui::CalcItemWidth(), position.y + labelSize.y + style.FramePadding.y * 2.0F});
        bool decimalInputRequested = false;
        if constexpr (std::is_floating_point_v<T>)
        {
            const auto& context = *GImGui;
            const bool hovered =
                ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(decimalFrame.Min, decimalFrame.Max);
            const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            decimalInputRequested =
                (clicked && context.IO.KeyCtrl) || doubleClicked ||
                (context.NavActivateId == id && (context.NavActivateFlags & ImGuiActivateFlags_PreferInput) != 0);
        }
        if (editing || decimalInputRequested)
        {
            const auto& frame = decimalFrame;
            const ImRect total(
                frame.Min, {frame.Max.x + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0), frame.Max.y});
            ImGui::ItemSize(total, style.FramePadding.y);
            if (!ImGui::ItemAdd(total, id, &frame, ImGuiItemFlags_Inputable))
                return false;
            std::array<char, MaximumScalarDecimalCharacters + 1> buffer{};
            if constexpr (std::is_floating_point_v<T>)
            {
                const auto text = ScalarEditText(candidate);
                std::copy(text.begin(), text.end(), buffer.begin());
            }
            else
            {
                ImGui::DataTypeFormatString(buffer.data(), static_cast<int>(buffer.size()), type, &candidate, format);
            }
            changed =
                ImGui::TempInputText(frame, id, label, buffer.data(), buffer.size(), ImGuiInputTextFlags_AutoSelectAll);
            if (labelSize.x > 0)
                ImGui::RenderText({frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y}, label);
        }
        else
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Inputable, true);
                auto hiddenText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                hiddenText.w = 0.0F;
                ImGui::PushStyleColor(ImGuiCol_Text, hiddenText);
                changed = draw(candidate);
                ImGui::PopStyleColor();
                ImGui::PopItemFlag();
                if (ImGui::IsItemVisible())
                {
                    const auto fullText = ScalarEditText(candidate);
                    auto visibleText = ScalarDisplayText(candidate);
                    const float available = std::max(1.0F, decimalFrame.GetWidth() - style.FramePadding.x * 2.0F);
                    // Explicit middle elision keeps a tiny nonzero value from appearing to be a run of zeros.
                    const auto completeDisplay = visibleText;
                    if (ImGui::CalcTextSize(visibleText.c_str()).x > available)
                    {
                        std::size_t lower = 2;
                        std::size_t upper = visibleText.size();
                        while (lower < upper)
                        {
                            const auto retained = lower + (upper - lower + 1) / 2;
                            const auto probe = completeDisplay.substr(0, (retained + 1) / 2) + "..." +
                                               completeDisplay.substr(completeDisplay.size() - retained / 2);
                            if (ImGui::CalcTextSize(probe.c_str()).x <= available)
                            {
                                lower = retained;
                            }
                            else
                            {
                                upper = retained - 1;
                            }
                        }
                        visibleText = completeDisplay.substr(0, (lower + 1) / 2) + "..." +
                                      completeDisplay.substr(completeDisplay.size() - lower / 2);
                    }
                    ImGui::RenderTextClipped(decimalFrame.Min, decimalFrame.Max, visibleText.c_str(), nullptr, nullptr,
                                             {0.5F, 0.5F});
                    if (labelSize.x > 0.0F)
                    {
                        ImGui::RenderText(
                            {decimalFrame.Max.x + style.ItemInnerSpacing.x, decimalFrame.Min.y + style.FramePadding.y},
                            label, ImGui::FindRenderedTextEnd(label));
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0F);
                        ImGui::TextUnformatted(fullText.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            else
            {
                changed = draw(candidate);
            }
        }

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
                                                 return ImGui::DragScalar(
                                                     "##axis", ImGuiDataType_Float, &candidate, speed, minimum, maximum,
                                                     "%g", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_NoRoundToFormat);
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
