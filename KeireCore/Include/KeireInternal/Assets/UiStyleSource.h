#pragma once

#include "Keire/Ui/UiToolkit.h"
#include <cstddef>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace Keire::Detail::UiStyleSource
{
    [[nodiscard]] std::string Trim(std::string_view value);
    [[nodiscard]] UiStyleRuleDefinition ParseSelector(std::string selector);
    [[nodiscard]] std::string StyleSourceMessage(std::string_view source, std::size_t offset, std::string_view message);
    [[noreturn]] void ThrowStyleSourceError(std::string_view source, std::size_t offset, std::string_view message);
    [[nodiscard]] nlohmann::json EncodeSelectorPart(const UiStyleSelectorPart& part);
    [[nodiscard]] UiStyleSelectorPart DecodeSelectorPart(const nlohmann::json& source);
    [[nodiscard]] UiStyleMediaCondition ParseMediaCondition(std::string_view source);
    [[nodiscard]] std::string EncodeMediaCondition(const UiStyleMediaCondition& condition);
    [[nodiscard]] nlohmann::json EncodeMediaConditionJson(const UiStyleMediaCondition& condition);
    [[nodiscard]] UiStyleMediaCondition DecodeMediaConditionJson(const nlohmann::json& source);
} // namespace Keire::Detail::UiStyleSource
