#pragma once

#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Time.h"

namespace KeireRuntime
{
    inline void UpdateRuntimeWorldFrame(Keire::SceneRuntimeWorld& world, const Keire::Time& time)
    {
        // Keep render-frame presentation between the last two fixed simulation samples.
        world.Update(static_cast<float>(time.DeltaTime().Seconds()), static_cast<float>(time.InterpolationAlpha()));
    }
} // namespace KeireRuntime
