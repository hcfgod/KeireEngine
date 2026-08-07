#include "Keire/Ui.h"

#include <imgui.h>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string BuildUiErrorMessage(const std::string& operation, const std::string& diagnostic)
        {
            return "UI operation '" + operation + "' failed: " + diagnostic;
        }

        [[nodiscard]] bool ValidLayoutColor(const UiColor color) noexcept
        {
            const auto valid = [](const float component)
            { return std::isfinite(component) && component >= 0.0F && component <= 1.0F; };
            return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
        }

        [[nodiscard]] ImGuiCol ToImGuiStyleColor(const UiStyleColorRole role)
        {
            switch (role)
            {
            case UiStyleColorRole::Text:
                return ImGuiCol_Text;
            case UiStyleColorRole::WindowBackground:
                return ImGuiCol_WindowBg;
            case UiStyleColorRole::ChildBackground:
                return ImGuiCol_ChildBg;
            case UiStyleColorRole::PopupBackground:
                return ImGuiCol_PopupBg;
            case UiStyleColorRole::Border:
                return ImGuiCol_Border;
            case UiStyleColorRole::FrameBackground:
                return ImGuiCol_FrameBg;
            case UiStyleColorRole::FrameBackgroundHovered:
                return ImGuiCol_FrameBgHovered;
            case UiStyleColorRole::FrameBackgroundActive:
                return ImGuiCol_FrameBgActive;
            case UiStyleColorRole::Button:
                return ImGuiCol_Button;
            case UiStyleColorRole::ButtonHovered:
                return ImGuiCol_ButtonHovered;
            case UiStyleColorRole::ButtonActive:
                return ImGuiCol_ButtonActive;
            case UiStyleColorRole::Header:
                return ImGuiCol_Header;
            case UiStyleColorRole::HeaderHovered:
                return ImGuiCol_HeaderHovered;
            case UiStyleColorRole::HeaderActive:
                return ImGuiCol_HeaderActive;
            }
            throw std::invalid_argument("The UI style color role is invalid.");
        }

        [[nodiscard]] ImGuiStyleVar ToImGuiScalarStyleVariable(const UiStyleVariable variable)
        {
            switch (variable)
            {
            case UiStyleVariable::WindowRounding:
                return ImGuiStyleVar_WindowRounding;
            case UiStyleVariable::WindowBorderSize:
                return ImGuiStyleVar_WindowBorderSize;
            case UiStyleVariable::ChildRounding:
                return ImGuiStyleVar_ChildRounding;
            case UiStyleVariable::ChildBorderSize:
                return ImGuiStyleVar_ChildBorderSize;
            case UiStyleVariable::FrameRounding:
                return ImGuiStyleVar_FrameRounding;
            case UiStyleVariable::WindowPadding:
            case UiStyleVariable::FramePadding:
            case UiStyleVariable::ItemSpacing:
                break;
            }
            throw std::invalid_argument("The UI style variable requires a two-dimensional value.");
        }

        [[nodiscard]] ImGuiStyleVar ToImGuiVectorStyleVariable(const UiStyleVariable variable)
        {
            switch (variable)
            {
            case UiStyleVariable::WindowPadding:
                return ImGuiStyleVar_WindowPadding;
            case UiStyleVariable::FramePadding:
                return ImGuiStyleVar_FramePadding;
            case UiStyleVariable::ItemSpacing:
                return ImGuiStyleVar_ItemSpacing;
            case UiStyleVariable::WindowRounding:
            case UiStyleVariable::WindowBorderSize:
            case UiStyleVariable::ChildRounding:
            case UiStyleVariable::ChildBorderSize:
            case UiStyleVariable::FrameRounding:
                break;
            }
            throw std::invalid_argument("The UI style variable requires a scalar value.");
        }
    } // namespace

    UiScope::UiScope(UiFrame& frame, const Kind kind, const bool visible, const bool closeRequired) noexcept
        : m_Frame(&frame), m_Lifetime(frame.Lifetime()), m_Generation(frame.Generation()), m_Kind(kind),
          m_Visible(visible), m_CloseRequired(closeRequired)
    {
    }

    UiScope::UiScope(UiScope&& other) noexcept
        : m_Frame(std::exchange(other.m_Frame, nullptr)), m_Lifetime(std::move(other.m_Lifetime)),
          m_Generation(other.m_Generation), m_Kind(other.m_Kind), m_Visible(other.m_Visible),
          m_CloseRequired(std::exchange(other.m_CloseRequired, false))
    {
    }

    UiScope& UiScope::operator=(UiScope&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Frame = std::exchange(other.m_Frame, nullptr);
            m_Lifetime = std::move(other.m_Lifetime);
            m_Generation = other.m_Generation;
            m_Kind = other.m_Kind;
            m_Visible = other.m_Visible;
            m_CloseRequired = std::exchange(other.m_CloseRequired, false);
        }
        return *this;
    }

    UiScope::~UiScope() { Reset(); }

    void UiScope::Reset() noexcept
    {
        if (m_Frame && m_CloseRequired && !m_Lifetime.expired())
            m_Frame->CloseScope(m_Kind, m_Generation);
        m_Frame = nullptr;
        m_CloseRequired = false;
    }

    UiError::UiError(std::string operation, std::string diagnostic)
        : std::runtime_error(BuildUiErrorMessage(operation, diagnostic)), m_Operation(std::move(operation)),
          m_Diagnostic(std::move(diagnostic))
    {
    }

    void UiFrame::TableSetupColumn(const std::string_view label, const UiTableColumnSizing sizing,
                                   const float widthOrWeight)
    {
        (void)ContentAvailable();
        if (!std::isfinite(widthOrWeight) || widthOrWeight < 0.0F)
            throw std::invalid_argument("UI table column width or weight must be finite and non-negative.");
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
        if (sizing == UiTableColumnSizing::Fixed)
            flags = ImGuiTableColumnFlags_WidthFixed;
        else if (sizing == UiTableColumnSizing::Stretch)
            flags = ImGuiTableColumnFlags_WidthStretch;
        const std::string safeLabel(label);
        ImGui::TableSetupColumn(safeLabel.c_str(), flags, widthOrWeight);
    }

    void UiFrame::TableHeaderRow()
    {
        (void)ContentAvailable();
        ImGui::TableHeadersRow();
    }

    void UiFrame::Text(const std::string_view text)
    {
        (void)ContentAvailable();
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }

    void UiFrame::TextColored(const UiColor color, const std::string_view text)
    {
        (void)ContentAvailable();
        if (!ValidLayoutColor(color))
            throw std::invalid_argument("UI color components must be finite values in the range 0..1.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.Red, color.Green, color.Blue, color.Alpha));
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopStyleColor();
    }

    void UiFrame::TextWrapped(const std::string_view text)
    {
        (void)ContentAvailable();
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
    }

    void UiFrame::TextColoredWrapped(const UiColor color, const std::string_view text)
    {
        (void)ContentAvailable();
        if (!ValidLayoutColor(color))
            throw std::invalid_argument("UI color components must be finite values in the range 0..1.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.Red, color.Green, color.Blue, color.Alpha));
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    UiPosition UiFrame::CursorPosition() const
    {
        (void)ContentAvailable();
        const auto position = ImGui::GetCursorPos();
        return {position.x, position.y};
    }

    UiPosition UiFrame::CursorScreenPosition() const
    {
        (void)ContentAvailable();
        const auto position = ImGui::GetCursorScreenPos();
        return {position.x, position.y};
    }

    void UiFrame::SetCursorPosition(const UiPosition position)
    {
        (void)ContentAvailable();
        if (!std::isfinite(position.X) || !std::isfinite(position.Y))
            throw std::invalid_argument("UI cursor positions must be finite.");
        ImGui::SetCursorPos({position.X, position.Y});
    }

    void UiFrame::SetCursorScreenPosition(const UiPosition position)
    {
        (void)ContentAvailable();
        if (!std::isfinite(position.X) || !std::isfinite(position.Y))
            throw std::invalid_argument("UI cursor positions must be finite.");
        ImGui::SetCursorScreenPos({position.X, position.Y});
    }

    void UiFrame::SetNextItemWidth(const float width)
    {
        (void)ContentAvailable();
        if (!std::isfinite(width))
            throw std::invalid_argument("UI item width must be finite.");
        ImGui::SetNextItemWidth(width);
    }

    void UiFrame::RequestKeyboardFocus()
    {
        (void)ContentAvailable();
        ImGui::SetKeyboardFocusHere();
    }

    UiStyleColorScope UiFrame::PushStyleColor(const UiStyleColorRole role, const UiColor color)
    {
        (void)ContentAvailable();
        if (!ValidLayoutColor(color))
            throw std::invalid_argument("UI color components must be finite values in the range 0..1.");
        ImGui::PushStyleColor(ToImGuiStyleColor(role), ImVec4(color.Red, color.Green, color.Blue, color.Alpha));
        OpenScope(UiScope::Kind::StyleColor);
        return UiStyleColorScope(*this);
    }

    UiStyleVariableScope UiFrame::PushStyleVariable(const UiStyleVariable variable, const float value)
    {
        (void)ContentAvailable();
        if (!std::isfinite(value) || value < 0.0F || value > 4096.0F)
            throw std::invalid_argument("UI scalar style values must be finite values in the range 0..4096.");
        ImGui::PushStyleVar(ToImGuiScalarStyleVariable(variable), value);
        OpenScope(UiScope::Kind::StyleVariable);
        return UiStyleVariableScope(*this);
    }

    UiStyleVariableScope UiFrame::PushStyleVariable(const UiStyleVariable variable, const UiSize value)
    {
        (void)ContentAvailable();
        if (!std::isfinite(value.Width) || !std::isfinite(value.Height) || value.Width < 0.0F || value.Height < 0.0F ||
            value.Width > 4096.0F || value.Height > 4096.0F)
        {
            throw std::invalid_argument("UI vector style values must be finite values in the range 0..4096.");
        }
        ImGui::PushStyleVar(ToImGuiVectorStyleVariable(variable), ImVec2(value.Width, value.Height));
        OpenScope(UiScope::Kind::StyleVariable);
        return UiStyleVariableScope(*this);
    }
} // namespace Keire
