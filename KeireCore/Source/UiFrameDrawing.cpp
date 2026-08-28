#include "Keire/Ui.h"

#include <imgui.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool ValidDrawingColor(const UiColor color) noexcept
        {
            const auto valid = [](const float component)
            { return std::isfinite(component) && component >= 0.0F && component <= 1.0F; };
            return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
        }

        [[nodiscard]] ImU32 ToImGuiDrawingColor(const UiColor color)
        {
            if (!ValidDrawingColor(color))
                throw std::invalid_argument("UI drawing colors must contain finite values in 0..1.");
            return ImGui::ColorConvertFloat4ToU32({color.Red, color.Green, color.Blue, color.Alpha});
        }

        void ValidateDrawing(const float thickness, const float rounding = 0.0F)
        {
            if (!std::isfinite(thickness) || thickness <= 0.0F || !std::isfinite(rounding) || rounding < 0.0F)
                throw std::invalid_argument("UI drawing dimensions must be finite and positive.");
        }
    } // namespace

    void UiFrame::DrawLine(const UiPosition start, const UiPosition end, const UiColor color, const float thickness)
    {
        RequireActive("DrawLine");
        ValidateDrawing(thickness);
        ImGui::GetWindowDrawList()->AddLine({start.X, start.Y}, {end.X, end.Y}, ToImGuiDrawingColor(color), thickness);
    }

    void UiFrame::DrawCircle(const UiPosition center, const float radius, const UiColor color, const float thickness)
    {
        RequireActive("DrawCircle");
        ValidateDrawing(thickness);
        if (!std::isfinite(radius) || radius <= 0.0F)
            throw std::invalid_argument("UI circle radius must be finite and positive.");
        ImGui::GetWindowDrawList()->AddCircle({center.X, center.Y}, radius, ToImGuiDrawingColor(color), 0, thickness);
    }

    void UiFrame::DrawFilledCircle(const UiPosition center, const float radius, const UiColor color)
    {
        RequireActive("DrawFilledCircle");
        if (!std::isfinite(radius) || radius <= 0.0F)
            throw std::invalid_argument("UI circle radius must be finite and positive.");
        ImGui::GetWindowDrawList()->AddCircleFilled({center.X, center.Y}, radius, ToImGuiDrawingColor(color));
    }

    void UiFrame::DrawRectangle(const UiItemRect rectangle, const UiColor color, const float thickness,
                                const float rounding)
    {
        RequireActive("DrawRectangle");
        ValidateDrawing(thickness, rounding);
        ImGui::GetWindowDrawList()->AddRect({rectangle.Minimum.X, rectangle.Minimum.Y},
                                            {rectangle.Maximum.X, rectangle.Maximum.Y}, ToImGuiDrawingColor(color),
                                            rounding, ImDrawFlags_None, thickness);
    }

    void UiFrame::DrawFilledRectangle(const UiItemRect rectangle, const UiColor color, const float rounding)
    {
        RequireActive("DrawFilledRectangle");
        ValidateDrawing(1.0F, rounding);
        ImGui::GetWindowDrawList()->AddRectFilled({rectangle.Minimum.X, rectangle.Minimum.Y},
                                                  {rectangle.Maximum.X, rectangle.Maximum.Y},
                                                  ToImGuiDrawingColor(color), rounding);
    }

    void UiFrame::DrawTriangle(const UiPosition first, const UiPosition second, const UiPosition third,
                               const UiColor color, const float thickness)
    {
        RequireActive("DrawTriangle");
        ValidateDrawing(thickness);
        ImGui::GetWindowDrawList()->AddTriangle({first.X, first.Y}, {second.X, second.Y}, {third.X, third.Y},
                                                ToImGuiDrawingColor(color), thickness);
    }

    void UiFrame::DrawFilledTriangle(const UiPosition first, const UiPosition second, const UiPosition third,
                                     const UiColor color)
    {
        RequireActive("DrawFilledTriangle");
        ImGui::GetWindowDrawList()->AddTriangleFilled({first.X, first.Y}, {second.X, second.Y}, {third.X, third.Y},
                                                      ToImGuiDrawingColor(color));
    }

    UiSize UiFrame::MeasureText(const std::string_view text, const float fontSize) const
    {
        RequireActive("MeasureText");
        const auto size = fontSize > 0.0F ? fontSize : ImGui::GetFontSize();
        const auto measured = ImGui::GetFont()->CalcTextSizeA(size, std::numeric_limits<float>::max(), 0.0F,
                                                              text.data(), text.data() + text.size());
        return {measured.x, measured.y};
    }

    void UiFrame::DrawOverlayText(const UiPosition position, const UiColor color, const std::string_view text,
                                  const float fontSize, const std::optional<UiItemRect> clip)
    {
        RequireActive("DrawOverlayText");
        const auto size = fontSize > 0.0F ? fontSize : ImGui::GetFontSize();
        ImVec4 clipRectangle;
        const ImVec4* clipPointer = nullptr;
        if (clip)
        {
            clipRectangle = {clip->Minimum.X, clip->Minimum.Y, clip->Maximum.X, clip->Maximum.Y};
            clipPointer = &clipRectangle;
        }
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, {position.X, position.Y},
                                            ToImGuiDrawingColor(color), text.data(), text.data() + text.size(), 0.0F,
                                            clipPointer);
    }
} // namespace Keire
