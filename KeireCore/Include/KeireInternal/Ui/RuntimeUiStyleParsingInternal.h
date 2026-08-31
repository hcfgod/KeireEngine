#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] bool TryApplyRuntimeUiStyleV2Property(RuntimeUiStyle& style, std::string_view name,
                                                        std::string_view value);
} // namespace Keire::Detail
