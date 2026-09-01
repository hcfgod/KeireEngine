#include "Keire/Ui.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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
            if (data->Ctx->IO.MouseClickedCount[0] >= 2)
            {
                if (auto* input = ImGui::GetInputTextState(data->ID))
                    input->CursorFollow = false;
            }
            return 0;
        }

        [[nodiscard]] ImU32 EncodeColor(const UiColor color) noexcept
        {
            return IM_COL32(static_cast<int>(std::clamp(color.Red, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Green, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Blue, 0.0F, 1.0F) * 255.0F),
                            static_cast<int>(std::clamp(color.Alpha, 0.0F, 1.0F) * 255.0F));
        }

        [[nodiscard]] ImGuiWindow* FindCodeEditorChild(ImGuiWindow& parent, const ImGuiID id) noexcept
        {
            for (int index = parent.DC.ChildWindows.Size - 1; index >= 0; --index)
            {
                auto* child = parent.DC.ChildWindows[index];
                if (child && child->ChildId == id)
                    return child;
            }
            // Before InputTextMultiline begins for the current frame, the persistent child is not yet present in
            // parent.DC.ChildWindows. Its live scroll position is still authoritative over the public state mirror.
            for (auto* child : GImGui->Windows)
            {
                if (child && child->ParentWindow == &parent && child->ChildId == id)
                    return child;
            }
            return nullptr;
        }

        struct CodeEditorSelection final
        {
            std::size_t Begin = 0;
            std::size_t End = 0;
        };

        [[nodiscard]] bool IsCodeWordCharacter(const char character) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
        }

        [[nodiscard]] float MeasureCodeEditorText(ImFont& font, const float fontSize, const char* begin,
                                                  const char* end) noexcept
        {
            if (begin >= end)
                return 0.0F;
            return font.CalcTextSizeA(fontSize, FLT_MAX, 0.0F, begin, end).x;
        }

        [[nodiscard]] std::optional<CodeEditorSelection>
        SelectionAtMouse(const std::string_view value, const ImGuiTextIndex& lineIndex, const ImVec2 origin,
                         const ImVec2 mousePosition, const int clickCount)
        {
            if (value.empty() || lineIndex.Offsets.empty())
                return std::nullopt;

            auto& context = *GImGui;
            const auto requestedLine =
                static_cast<int>(std::clamp(std::floor((mousePosition.y - origin.y) / context.FontSize), 0.0F,
                                            static_cast<float>(lineIndex.Offsets.Size - 1)));
            const auto* textBegin = value.data();
            const auto lineBeginOffset = static_cast<std::size_t>(lineIndex.Offsets[requestedLine]);
            auto lineEndOffset = requestedLine + 1 < lineIndex.Offsets.Size
                                     ? static_cast<std::size_t>(lineIndex.Offsets[requestedLine + 1])
                                     : static_cast<std::size_t>(lineIndex.EndOffset);
            lineEndOffset = std::min(lineEndOffset, value.size());
            if (lineEndOffset > lineBeginOffset && value[lineEndOffset - 1U] == '\n')
                --lineEndOffset;
            const auto* lineBegin = textBegin + std::min(lineBeginOffset, value.size());
            const auto* lineEnd = textBegin + std::max(lineBeginOffset, lineEndOffset);

            const float requestedX = std::max(0.0F, mousePosition.x - origin.x);
            const auto* character = lineBegin;
            float x = 0.0F;
            while (character < lineEnd && *character != '\n')
            {
                unsigned int codePoint = 0;
                const auto length = std::max(1, ImTextCharFromUtf8(&codePoint, character, lineEnd));
                const auto* next = std::min(character + length, lineEnd);
                const float width = MeasureCodeEditorText(*context.Font, context.FontSize, character, next);
                if (requestedX < x + width * 0.5F)
                    break;
                x += width;
                character = next;
            }

            auto offset = static_cast<std::size_t>(character - textBegin);
            if (clickCount < 2)
                return CodeEditorSelection{offset, offset};
            if (((clickCount - 2) % 2) != 0)
            {
                const auto logicalBegin = offset == 0U ? 0U : value.rfind('\n', offset - 1U) + 1U;
                const auto newline = value.find('\n', offset);
                return CodeEditorSelection{logicalBegin,
                                           newline == std::string_view::npos ? value.size() : newline + 1U};
            }

            if (offset == value.size() || (offset < value.size() && !IsCodeWordCharacter(value[offset])))
            {
                if (offset > 0U && IsCodeWordCharacter(value[offset - 1U]))
                    --offset;
                else
                    return CodeEditorSelection{offset, std::min(offset + 1U, value.size())};
            }
            auto begin = offset;
            while (begin > 0U && IsCodeWordCharacter(value[begin - 1U]))
                --begin;
            auto end = offset;
            while (end < value.size() && IsCodeWordCharacter(value[end]))
                ++end;
            return CodeEditorSelection{begin, end};
        }

        [[nodiscard]] float DrawCodeEditorTextSegment(ImDrawList& drawList, ImFont& font, const float fontSize,
                                                      const ImVec2 position, const ImU32 color, const char* begin,
                                                      const char* end, const ImVec4& clipRectangle)
        {
            if (begin >= end)
                return 0.0F;
            drawList.AddText(&font, fontSize, position, color, begin, end, 0.0F, &clipRectangle);
            return MeasureCodeEditorText(font, fontSize, begin, end);
        }

        void DrawCodeEditorText(const std::string_view value, UiCodeEditorState& state, const ImGuiID id,
                                ImGuiWindow& parent, const ImVec2 frameMinimum, const ImVec2 frameMaximum,
                                const ImU32 defaultTextColor)
        {
            auto* child = FindCodeEditorChild(parent, id);
            if (!child || value.empty())
                return;

            if (const auto* input = ImGui::GetInputTextState(id))
                state.ScrollX = input->Scroll.x;
            else
                state.ScrollX = 0.0F;
            state.ScrollY = child->Scroll.y;

            auto& context = *GImGui;
            const auto& lineIndex = context.InputTextLineIndex;
            if (lineIndex.Offsets.empty())
                return;

            std::vector<UiCodeEditorState::Highlight> highlights = state.Highlights;
            std::ranges::sort(highlights,
                              [](const auto& left, const auto& right) { return left.Offset < right.Offset; });

            auto* drawList = child->DrawList;
            const auto padding = ImGui::GetStyle().FramePadding;
            // InputTextEx performs hit testing from the child's cursor start, which includes window decoration,
            // rounding, and scrolling. Reusing that exact origin keeps painted tokens under the mouse cursor.
            const ImVec2 origin{child->DC.CursorStartPos.x + padding.x - state.ScrollX,
                                child->DC.CursorStartPos.y + padding.y};
            ImRect frameClip(frameMinimum, frameMaximum);
            frameClip.ClipWith(child->ClipRect);
            if (frameClip.IsInverted())
                return;
            const auto clip = frameClip.ToVec4();
            const auto* textBegin = value.data();
            const auto textLength = value.size();
            drawList->PushClipRect(frameClip.Min, frameClip.Max, true);
            for (int line = 0; line < lineIndex.Offsets.Size; ++line)
            {
                const float y = origin.y + static_cast<float>(line) * context.FontSize;
                if (y + context.FontSize < frameClip.Min.y)
                    continue;
                if (y >= frameClip.Max.y)
                    break;

                const auto lineBegin = static_cast<std::size_t>(lineIndex.Offsets[line]);
                auto lineEnd = line + 1 < lineIndex.Offsets.Size ? static_cast<std::size_t>(lineIndex.Offsets[line + 1])
                                                                 : static_cast<std::size_t>(lineIndex.EndOffset);
                lineEnd = std::min(lineEnd, textLength);
                if (lineEnd > lineBegin && value[lineEnd - 1U] == '\n')
                    --lineEnd;
                if (lineBegin >= lineEnd || lineBegin >= textLength)
                    continue;

                auto cursor = lineBegin;
                float x = origin.x;
                for (const auto& highlight : highlights)
                {
                    if (highlight.Length == 0U)
                        continue;
                    if (highlight.Offset >= lineEnd)
                        break;
                    const auto highlightEnd = std::min(
                        textLength, highlight.Offset + std::min(highlight.Length, textLength - highlight.Offset));
                    if (highlightEnd <= cursor || highlightEnd <= lineBegin)
                        continue;

                    const auto tokenBegin = std::max(cursor, std::max(lineBegin, highlight.Offset));
                    const auto tokenEnd = std::min(lineEnd, highlightEnd);
                    x += DrawCodeEditorTextSegment(*drawList, *context.Font, context.FontSize, {x, y}, defaultTextColor,
                                                   textBegin + cursor, textBegin + tokenBegin, clip);
                    x += DrawCodeEditorTextSegment(*drawList, *context.Font, context.FontSize, {x, y},
                                                   EncodeColor(highlight.Color), textBegin + tokenBegin,
                                                   textBegin + tokenEnd, clip);
                    cursor = tokenEnd;
                    if (cursor >= lineEnd)
                        break;
                }
                (void)DrawCodeEditorTextSegment(*drawList, *context.Font, context.FontSize, {x, y}, defaultTextColor,
                                                textBegin + cursor, textBegin + lineEnd, clip);
            }
            drawList->PopClipRect();
        }

        [[nodiscard]] bool InputCodeEditorImpl(const std::string_view label, std::string& value,
                                               UiCodeEditorState& state, const ImVec2 size)
        {
            std::string safeLabel = "##";
            safeLabel.append(label);
            const auto id = ImGui::GetID(safeLabel.c_str());
            auto* parent = ImGui::GetCurrentWindow();
            const auto defaultTextColor = ImGui::GetColorU32(ImGuiCol_Text);
            auto& io = ImGui::GetIO();
            const auto frameMinimum = ImGui::GetCursorScreenPos();
            const float frameWidth = size.x == 0.0F ? ImGui::GetContentRegionAvail().x : size.x;
            const ImRect expectedFrame(frameMinimum, {frameMinimum.x + frameWidth, frameMinimum.y + size.y});
            const bool originalMouseClicked = io.MouseClicked[0];
            const auto originalClickCount = io.MouseClickedCount[0];
            const auto* existingChild = FindCodeEditorChild(*parent, id);
            const float previousScrollY = existingChild ? existingChild->Scroll.y : state.ScrollY;
            const bool requestedCursor = state.RequestCursor;
            const auto flags =
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_WordWrap;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{});
            const auto edited =
                ImGui::InputTextMultiline(safeLabel.c_str(), &value, {size.x == 0.0F ? -FLT_MIN : size.x, size.y},
                                          flags, UpdateCodeEditorState, &state);
            ImGui::PopStyleColor();
            if (requestedCursor)
            {
                if (auto* input = ImGui::GetInputTextState(id))
                {
                    input->CursorFollow = false;
                    input->CursorCenterY = false;
                }
            }
            auto* child = FindCodeEditorChild(*parent, id);
            const bool replaceClick = originalMouseClicked && !io.KeyShift && expectedFrame.Contains(io.MousePos) &&
                                      child && child->ClipRect.Contains(io.MousePos);
            if (replaceClick)
            {
                const auto& style = ImGui::GetStyle();
                const ImVec2 origin{child->DC.CursorStartPos.x + style.FramePadding.x - state.ScrollX,
                                    child->DC.CursorStartPos.y + style.FramePadding.y};
                if (const auto selection =
                        SelectionAtMouse(value, GImGui->InputTextLineIndex, origin, io.MousePos, originalClickCount))
                {
                    state.CursorOffset = selection->End;
                    state.SelectionBegin = selection->Begin;
                    state.SelectionEnd = selection->End;
                    state.RequestCursor = false;
                    if (auto* input = ImGui::GetInputTextState(id))
                    {
                        input->SetSelection(static_cast<int>(selection->Begin), static_cast<int>(selection->End));
                        input->CursorFollow = false;
                        input->CursorCenterY = false;
                        input->Scroll.y = std::clamp(previousScrollY, 0.0F, child->ScrollMax.y);
                        input->CursorAnimReset();
                    }
                    else
                    {
                        state.RequestCursor = true;
                    }
                    const float restoredScrollY = std::clamp(previousScrollY, 0.0F, child->ScrollMax.y);
                    child->Scroll.y = restoredScrollY;
                    ImGui::SetScrollY(child, restoredScrollY);
                    state.ScrollY = restoredScrollY;
                }
            }
            state.CursorOffset = std::min(state.CursorOffset, value.size());
            state.SelectionBegin = std::min(state.SelectionBegin, value.size());
            state.SelectionEnd = std::min(state.SelectionEnd, value.size());
            if (ImGui::IsItemVisible())
            {
                DrawCodeEditorText(value, state, id, *parent, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                   defaultTextColor);
            }
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
