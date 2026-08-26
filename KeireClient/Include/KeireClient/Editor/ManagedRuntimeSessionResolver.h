#pragma once

#include "Keire/Scenes/SceneRuntimeWorld.h"

namespace KeireEditor
{
    [[nodiscard]] inline Keire::Ref<Keire::SceneRuntimeSession>
    ResolveManagedRuntimeSession(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                 const Keire::Ref<Keire::SceneRuntimeSession>& primary,
                                 const Keire::AssetId entity = {}) noexcept
    {
        if (world)
        {
            if (entity)
                if (const auto session = world->SessionForEntity(Keire::EntityId(entity)))
                    return session;
            if (const auto session = world->Session(world->Active()))
                return session;
        }
        return primary;
    }
} // namespace KeireEditor
