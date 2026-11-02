#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Water/WaterSurface.h"
#include "Keire/Water/WaterWaveField.h"

#include <array>
#include <optional>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        class WaterRuntimeAccess;
    }

    struct KEIRE_API WaterBodySettings
    {
        AssetId Material;
        /// Linear surface albedo and opacity; every channel is in [0, 1].
        Color SurfaceColor{0.02F, 0.18F, 0.22F, 0.85F};
        float Roughness = 0.15F;
        float CrestFoamStrength = 0.25F;
        /// Target mesh spacing. Rendering may coarsen this to enforce its hard geometry budget.
        double GridSpacingMetres = 1.0;
        double RenderDistanceMetres = 512.0;
        WaterSurfaceShape Shape = WaterSurfaceShape::Rectangle;
        double HalfExtentXMetres = 50.0;
        double HalfExtentZMetres = 50.0;
        double RadiusMetres = 6'371'000.0;
        double DepthMetres = 100.0;
        double DensityKilogramsPerCubicMetre = 1'000.0;
        Vector3d FlowVelocityLocal;
        /// Empty means still water. Directional waves are supported only on planar shapes.
        std::vector<WaterWaveDefinition> Waves;

        [[nodiscard]] bool operator==(const WaterBodySettings& other) const noexcept;
    };

    /// Authored metre dimensions are independent of Transform scale. Transform supplies pose only.
    /// Runtime frame resolution, rendering and buoyancy ownership remain with their respective consumers.
    class KEIRE_API WaterBodyComponent final : public Component
    {
      public:
        WaterBodyComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245574154ULL, 0x4552424f44590001ULL));
        }

        [[nodiscard]] const WaterBodySettings& Settings() const noexcept { return m_Settings; }

        /// Runtime fixed-step phase; edit-mode rendering falls back to the renderer clock.
        [[nodiscard]] std::optional<double> SimulationTimeSeconds() const noexcept { return m_SimulationTimeSeconds; }

        void Configure(const WaterBodySettings& settings);
        void Reset();
        static void Validate(const WaterBodySettings& settings);

        /// Caller resolves pose and velocities into one simulation frame, retaining double precision.
        [[nodiscard]] WaterSurfaceDefinition
        CreateSurfaceDefinition(Vector3d origin, std::array<double, 4> rotation = {0.0, 0.0, 0.0, 1.0},
                                Vector3d frameLinearVelocity = {}, Vector3d frameAngularVelocity = {}) const;

        /// Origin and all subsequent wave queries are in the same water-local X/Z frame.
        [[nodiscard]] WaterWaveField CreateWaveField(Vector3d localOrigin = {}) const;

      private:
        friend class Detail::WaterRuntimeAccess;
        WaterBodySettings m_Settings;
        std::optional<double> m_SimulationTimeSeconds;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateWaterBodyComponentRegistration();
} // namespace Keire
