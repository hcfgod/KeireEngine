#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <vector>

namespace Keire::Detail
{
    void AppendRuntimeUiDrawCommands(std::vector<RuntimeUiDrawCommand>& output, const RuntimeUiElementState& state,
                                     RuntimeUiElementId element, float scale);
} // namespace Keire::Detail
