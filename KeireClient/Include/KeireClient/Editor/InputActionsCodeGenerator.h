#pragma once

#include "Keire/Core.h"

#include <string>
#include <string_view>

namespace KeireEditor
{
    [[nodiscard]] std::string GenerateInputActionsCSharp(const Keire::InputActionAssetDefinition& definition,
                                                         std::string_view className, std::string_view nameSpace);
} // namespace KeireEditor
