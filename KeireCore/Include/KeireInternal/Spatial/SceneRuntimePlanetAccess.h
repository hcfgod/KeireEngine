#pragma once

#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Keire::Detail
{
    struct ScenePlanetOwner
    {
        AssetId Instance;
        std::uint64_t Frame = 1;
    };

    struct PlanetRuntimeCheckpoint
    {
        PlanetProjectSettings Settings;
        double SimulationTime = 0.0;
        EnvironmentClockState Clock;
        std::vector<ScenePlanetOwner> SceneOwners;
    };

    class SceneRuntimePlanetAccess final
    {
      public:
        // Startup-only transaction. Invalidates borrowed registry references/handles on success.
        // Runtime scene attachment changes are deliberately unavailable until solver migration is transactional.
        static KEIRE_API void Configure(SceneRuntimeWorld& world, std::vector<PlanetDefinition> planets);

        static KEIRE_API void ConfigureProject(SceneRuntimeWorld& world, PlanetProjectSettings settings);

        [[nodiscard]] static KEIRE_API PlanetProjectSettings Settings(const SceneRuntimeWorld& world);

        [[nodiscard]] static KEIRE_API std::optional<ResolvedPlanetFrame>
        ResolveSelected(SceneRuntimeWorld& world, const SectorPosition& rootObserverPosition);

        static KEIRE_API void InitializeGravity(SceneRuntimeWorld& world);

        [[nodiscard]] static KEIRE_API PlanetRuntimeCheckpoint CaptureCheckpoint(const SceneRuntimeWorld& world);

        static KEIRE_API void RestoreCheckpoint(SceneRuntimeWorld& world, const PlanetRuntimeCheckpoint& checkpoint);

        [[nodiscard]] static KEIRE_API std::vector<PlanetDefinition> Definitions(const SceneRuntimeWorld& world);

        [[nodiscard]] static KEIRE_API FrameRotation Rotation(SceneRuntimeWorld& world, const LiveFrame& source,
                                                              const LiveFrame& target, double snapshotTime);

        // Publishes one consistent set of frame evaluations for this synchronous view request.
        [[nodiscard]] static KEIRE_API ResolvedPlanetFrame Resolve(SceneRuntimeWorld& world, AssetId planet,
                                                                   const LiveFrame& observerFrame,
                                                                   const SectorPosition& observerPosition,
                                                                   double snapshotTime);

        // Gravity remains expressed in the scene's existing coordinate frame. Rejects unrepresentable conversion.
        // Call after session Play and before the first simulation tick; this does not change uniform gravity.
        static KEIRE_API void ApplyGravity(SceneRuntimeWorld& world, SceneHandle scene, double snapshotTime);
    };
} // namespace Keire::Detail
