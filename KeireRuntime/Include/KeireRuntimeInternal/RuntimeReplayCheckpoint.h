#pragma once

#include "Keire/Scenes/Scene.h"

#include <nlohmann/json_fwd.hpp>

namespace KeireRuntime
{
    [[nodiscard]] nlohmann::json EncodeAnimatorCheckpoint(const Keire::SceneAnimatorCheckpoint& animator);
    [[nodiscard]] Keire::SceneAnimatorCheckpoint DecodeAnimatorCheckpoint(const nlohmann::json& encoded);
} // namespace KeireRuntime
