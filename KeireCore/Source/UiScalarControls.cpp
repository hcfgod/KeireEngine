#include "Keire/Ui.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace Keire
{
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
} // namespace Keire
