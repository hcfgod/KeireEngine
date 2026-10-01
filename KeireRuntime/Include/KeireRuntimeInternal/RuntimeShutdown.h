#pragma once

#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Scenes/SceneSystem.h"

namespace KeireRuntime
{
    // Keep world lookup and managed services available through behaviour shutdown. The caller
    // unbinds services only after this function returns; world teardown afterward is idempotent.
    inline void StopRuntimeSessions(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                    const Keire::Ref<Keire::SceneRuntimeSession>& fallback) noexcept
    {
        if (world && world->IsOpen())
        {
            try
            {
                const auto sessions = world->Sessions();
                for (auto session = sessions.rbegin(); session != sessions.rend(); ++session)
                    if (*session)
                        (*session)->Stop();
            }
            catch (...)
            {
                // A failed snapshot allocation must not escape the layer's noexcept teardown.
                world->Close();
            }
        }
        if (fallback)
            fallback->Stop();
    }
} // namespace KeireRuntime
