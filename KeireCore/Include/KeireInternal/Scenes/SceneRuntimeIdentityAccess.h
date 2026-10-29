#pragma once

#include "Keire/Scenes/SceneRuntimeWorld.h"

#include <span>
#include <vector>

namespace Keire::Detail
{
    struct SceneRuntimeIdentityBinding
    {
        SceneHandle Handle;
        AssetId Instance;

        bool operator==(const SceneRuntimeIdentityBinding&) const = default;
    };

    // Checkpoint identity is independent of the source asset and the lifetime-qualified runtime handle.
    // These bindings are an internal restore boundary, not a serialized world checkpoint by themselves.
    class SceneRuntimeIdentityAccess final
    {
      public:
        [[nodiscard]] static KEIRE_API std::vector<SceneRuntimeIdentityBinding> Capture(const SceneRuntimeWorld& world);

        // Rebind all instances after reconstructing a checkpoint's scene set. Validate before changing any ID.
        // Owner-thread only; unavailable during world processing or simulation callbacks.
        static KEIRE_API void Rebind(SceneRuntimeWorld& world, std::span<const SceneRuntimeIdentityBinding> bindings);
    };
} // namespace Keire::Detail
