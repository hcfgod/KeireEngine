#pragma once

#include "Keire/Modules/EngineModule.h"

#include <vector>

namespace KeireProjectModules
{
    // Add statically constructed project modules here while engine contracts remain source-level.
    [[nodiscard]] std::vector<Keire::Ref<Keire::EngineModule>> CreateSourceModules();
} // namespace KeireProjectModules
