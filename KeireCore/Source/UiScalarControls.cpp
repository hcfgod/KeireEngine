#include "Keire/Ui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        int UpdateCodeEditorState(ImGuiInputTextCallbackData* data)
        {
            auto& state = *static_cast<UiCodeEditorState*>(data->UserData);
            if (state.RequestCursor)
            {
                data->CursorPos =
                    static_cast<int>(std::min(state.CursorOffset, static_cast<std::size_t>(data->BufTextLen)));
                data->SelectionStart =
                    static_cast<int>(std::min(state.SelectionBegin, static_cast<std::size_t>(data->BufTextLen)));
                data->SelectionEnd =
                    static_cast<int>(std::min(state.SelectionEnd, static_cast<std::size_t>(data->BufTextLen)));
                state.RequestCursor = false;
            }
            state.CursorOffset = static_cast<std::size_t>(std::max(data->CursorPos, 0));
            state.SelectionBegin = static_cast<std::size_t>(std::max(data->SelectionStart, 0));
            state.SelectionEnd = static_cast<std::size_t>(std::max(data->SelectionEnd, 0));
            return 0;
        }

        [[nodiscard]] ImU32 EncodeColor(const UiColor color, const float alphaScale = 1.0F) noexcept
        {
            return IM_COL32(static_cast<int>(std::clamp(color.Red, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Green, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Blue, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Alpha * alphaScale, 0.0F, 1.0F) * 255.0F));
        }

        void DrawCodeEditorHighlights(const std::string_view value, UiCodeEditorState& state, const ImVec2 minimum,
                                      const ImVec2 maximum)
        {
            if (state.Highlights.empty() || maximum.x <= minimum.x || maximum.y <= minimum.y)
                return;
            const auto id = ImGui::GetItemID();
            if (const auto* input = ImGui::GetInputTextState(id))
            {
                state.ScrollX = input->Scroll.x;
                state.ScrollY = input->Scroll.y;
            }

            auto* drawList = ImGui::GetWindowDrawList();
            const auto padding = ImGui::GetStyle().FramePadding;
            const float lineHeight = ImGui::GetTextLineHeight();
            const ImVec2 origin{minimum.x + padding.x - state.ScrollX, minimum.y + padding.y - state.ScrollY};
            drawList->PushClipRect(minimum, maximum, true);
            for (const auto& highlight : state.Highlights)
            {
                if (highlight.Length == 0U || highlight.Offset >= value.size())
                    continue;
                const auto end = std::min(value.size(), highlight.Offset + highlight.Length);
                auto cursor = highlight.Offset;
                while (cursor < end)
                {
                    const auto lineBegin = cursor == 0U ? 0U : value.rfind('\n', cursor - 1U) + 1U;
                    const auto lineEnd = std::min(end, value.find('\n', cursor));
                    const auto line =
                        static_cast<std::size_t>(std::ranges::count(value.begin(), value.begin() + lineBegin, '\n'));
                    const auto prefix = value.substr(lineBegin, cursor - lineBegin);
                    const auto token = value.substr(cursor, lineEnd - cursor);
                    const float x = origin.x + ImGui::CalcTextSize(prefix.data(), prefix.data() + prefix.size()).x;
                    const float y = origin.y + static_cast<float>(line) * lineHeight;
                    const float width =
                        std::max(1.0F, ImGui::CalcTextSize(token.data(), token.data() + token.size()).x);
                    if (y + lineHeight >= minimum.y && y <= maximum.y)
                    {
                        drawList->AddRectFilled({x, y}, {x + width, y + lineHeight},
                                                EncodeColor(highlight.Color, 0.14F), 2.0F);
                        drawList->AddLine({x, y + lineHeight - 1.0F}, {x + width, y + lineHeight - 1.0F},
                                          EncodeColor(highlight.Color, 0.9F), 1.0F);
                    }
                    if (lineEnd >= end)
                        break;
                    cursor = lineEnd + 1U;
                }
            }
            drawList->PopClipRect();
        }

        [[nodiscard]] bool InputCodeEditorImpl(const std::string_view label, std::string& value,
                                               UiCodeEditorState& state, const ImVec2 size)
        {
            const std::string safeLabel(label);
            const auto flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;
            const auto edited =
                ImGui::InputTextMultiline(safeLabel.c_str(), &value, size, flags, UpdateCodeEditorState, &state);
            state.CursorOffset = std::min(state.CursorOffset, value.size());
            state.SelectionBegin = std::min(state.SelectionBegin, value.size());
            state.SelectionEnd = std::min(state.SelectionEnd, value.size());
            DrawCodeEditorHighlights(value, state, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            return edited;
        }
    } // namespace

    bool UiFrame::SliderFloat(std::string_view label, float& value, const float minimum, const float maximum)
    {
        RequireActive("SliderFloat");
        if (!(minimum < maximum))
            throw std::invalid_argument("SliderFloat minimum must be less than maximum.");
        const std::string safeLabel(label);
        return ImGui::SliderFloat(safeLabel.c_str(), &value, minimum, maximum);
    }

    bool UiFrame::SliderInt(std::string_view label, int& value, const int minimum, const int maximum)
    {
        RequireActive("SliderInt");
        if (minimum > maximum)
            throw std::invalid_argument("SliderInt minimum must not exceed maximum.");
        const std::string safeLabel(label);
        return ImGui::SliderInt(safeLabel.c_str(), &value, minimum, maximum);
    }

    bool UiFrame::DragUnsignedInteger(const std::string_view label, std::uint64_t& value, const double speed,
                                      const std::optional<std::uint64_t> minimum,
                                      const std::optional<std::uint64_t> maximum)
    {
        RequireActive("DragUnsignedInteger");
        if (label.empty() || !std::isfinite(speed) || speed <= 0.0 || (minimum && maximum && *minimum > *maximum))
            throw std::invalid_argument("DragUnsignedInteger requires a label, positive speed, and ordered bounds.");
        const std::string safeLabel(label);
        const auto* minimumValue = minimum ? &*minimum : nullptr;
        const auto* maximumValue = maximum ? &*maximum : nullptr;
        return ImGui::DragScalar(safeLabel.c_str(), ImGuiDataType_U64, &value, static_cast<float>(speed), minimumValue,
                                 maximumValue, "%llu");
    }

    bool UiFrame::SliderInteger(const std::string_view label, std::int64_t& value, const std::int64_t minimum,
                                const std::int64_t maximum)
    {
        RequireActive("SliderInteger");
        if (label.empty() || minimum >= maximum)
            throw std::invalid_argument("SliderInteger requires a label and an increasing range.");
        const std::string safeLabel(label);
        return ImGui::SliderScalar(safeLabel.c_str(), ImGuiDataType_S64, &value, &minimum, &maximum, "%lld");
    }

    bool UiFrame::SliderUnsignedInteger(const std::string_view label, std::uint64_t& value, const std::uint64_t minimum,
                                        const std::uint64_t maximum)
    {
        RequireActive("SliderUnsignedInteger");
        if (label.empty() || minimum >= maximum)
            throw std::invalid_argument("SliderUnsignedInteger requires a label and an increasing range.");
        const std::string safeLabel(label);
        return ImGui::SliderScalar(safeLabel.c_str(), ImGuiDataType_U64, &value, &minimum, &maximum, "%llu");
    }

    bool UiFrame::SliderScalar(const std::string_view label, double& value, const double minimum, const double maximum)
    {
        RequireActive("SliderScalar");
        if (label.empty() || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum)
            throw std::invalid_argument("SliderScalar requires a label and a finite increasing range.");
        const std::string safeLabel(label);
        return ImGui::SliderScalar(safeLabel.c_str(), ImGuiDataType_Double, &value, &minimum, &maximum, "%.6g");
    }

    bool UiFrame::InputTextMultiline(const std::string_view label, std::string& value, const std::uint32_t visibleLines)
    {
        RequireActive("InputTextMultiline");
        if (label.empty() || visibleLines < 2 || visibleLines > 32)
            throw std::invalid_argument("InputTextMultiline requires a label and 2..32 visible lines.");
        const std::string safeLabel(label);
        const auto height = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(visibleLines);
        return ImGui::InputTextMultiline(safeLabel.c_str(), &value, {0.0F, height});
    }

    bool UiFrame::InputCodeEditor(const std::string_view label, std::string& value, UiCodeEditorState& state,
                                  const std::uint32_t visibleLines)
    {
        RequireActive("InputCodeEditor");
        if (label.empty() || visibleLines < 8 || visibleLines > 64)
            throw std::invalid_argument("InputCodeEditor requires a label and 8..64 visible lines.");
        const auto height = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(visibleLines);
        return InputCodeEditorImpl(label, value, state, {0.0F, height});
    }

    bool UiFrame::InputCodeEditor(const std::string_view label, std::string& value, UiCodeEditorState& state,
                                  const UiSize size)
    {
        RequireActive("InputCodeEditor");
        if (label.empty() || !std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width < 0.0F ||
            size.Height < ImGui::GetTextLineHeightWithSpacing() * 8.0F)
        {
            throw std::invalid_argument("InputCodeEditor requires a label and room for at least eight lines.");
        }
        return InputCodeEditorImpl(label, value, state, {size.Width, size.Height});
    }
} // namespace Keire
