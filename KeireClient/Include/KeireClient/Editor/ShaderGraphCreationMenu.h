#pragma once

#include "Keire/Core.h"

#include <optional>

namespace KeireEditor
{
    [[nodiscard]] std::optional<Keire::ShaderGraphTemplate> DrawShaderGraphCreationMenu(Keire::UiFrame& ui);
}
