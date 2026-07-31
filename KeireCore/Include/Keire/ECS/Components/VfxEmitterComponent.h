#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Math/Math.h"
#include "Keire/Vfx/VfxSystem.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire
{
    /// Authored quality preference. The field is serialized for future policy integration and is not currently
    /// consumed by VfxWorld.
    enum class VfxQualityTier : std::uint8_t
    {
        Low,
        Medium,
        High,
        Cinematic
    };

    /// Authored culling preference. Runtime VFX culling does not currently consume this field.
    enum class VfxCullingMode : std::uint8_t
    {
        Automatic,
        FixedBounds,
        AlwaysSimulate
    };

    /// Scene component that configures one asset-backed VFX emitter.
    ///
    /// Play Mode creates the native VfxHandle asynchronously through SceneRuntimeSession. EditModePreview is an
    /// editor-only visualization switch and is independent of PlayOnAwake.
    class KEIRE_API VfxEmitterComponent final : public Component
    {
      public:
        VfxEmitterComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245564658ULL, 0x454d495454455201ULL));
        }

        /// Assigned .keirevfx asset, or an empty ID when unassigned.
        [[nodiscard]] AssetId Effect() const noexcept { return m_Effect; }
        /// Whether Play Mode starts the assigned effect automatically.
        [[nodiscard]] bool PlayOnAwake() const noexcept { return m_PlayOnAwake; }
        /// Whether completion of a non-looping runtime effect destroys the entire runtime entity.
        [[nodiscard]] bool AutoDestroy() const noexcept { return m_AutoDestroy; }
        /// Per-emitter 0..8 simulation multiplier. Zero pauses simulation.
        [[nodiscard]] float SimulationSpeed() const noexcept { return m_SimulationSpeed; }
        /// Deterministic variation combined with the asset seed.
        [[nodiscard]] std::uint32_t SeedOffset() const noexcept { return m_SeedOffset; }
        /// Serialized quality preference; not currently consumed by VfxWorld.
        [[nodiscard]] VfxQualityTier Quality() const noexcept { return m_Quality; }
        /// Serialized culling preference; not currently consumed by VfxWorld.
        [[nodiscard]] VfxCullingMode Culling() const noexcept { return m_Culling; }
        /// Authored local bounds reserved for future culling integration.
        [[nodiscard]] Vector3 BoundsCenter() const noexcept { return m_BoundsCenter; }
        [[nodiscard]] Vector3 BoundsExtent() const noexcept { return m_BoundsExtent; }
        /// Whether this emitter is visualized in the edit-scene viewport.
        [[nodiscard]] bool EditModePreview() const noexcept { return m_EditModePreview; }
        /// Per-emitter Blackboard overrides keyed by stable parameter ID.
        [[nodiscard]] std::span<const VfxParameterOverride> ParameterOverrides() const noexcept
        {
            return m_ParameterOverrides;
        }

        /// Assigns a VFX effect asset. An empty ID clears the assignment.
        void SetEffect(AssetId effect);
        /// Configures automatic playback when a runtime scene starts.
        void SetPlayOnAwake(bool value);
        /// Configures whole-entity destruction after a non-looping runtime effect finishes.
        void SetAutoDestroy(bool value);
        /// Sets the validated 0..8 simulation multiplier.
        void SetSimulationSpeed(float value);
        /// Sets deterministic per-emitter seed variation.
        void SetSeedOffset(std::uint32_t value);
        /// Stores the authored quality preference.
        void SetQuality(VfxQualityTier value);
        /// Stores the authored culling preference.
        void SetCulling(VfxCullingMode value);
        /// Stores finite local culling bounds with strictly positive extent.
        void SetBounds(Vector3 center, Vector3 extent);
        /// Enables or disables the transient editor Scene-view preview.
        void SetEditModePreview(bool value);
        /// Adds or replaces one serialized Blackboard override.
        void SetParameterOverride(VfxParameterOverride value);
        /// Removes one override and returns whether it existed.
        bool RemoveParameterOverride(AssetId parameter);
        /// Returns every parameter to its effect-asset default.
        void ClearParameterOverrides();

      private:
        friend ComponentRegistration CreateVfxEmitterComponentRegistration();

        AssetId m_Effect;
        bool m_PlayOnAwake = true;
        bool m_AutoDestroy = false;
        float m_SimulationSpeed = 1.0F;
        std::uint32_t m_SeedOffset = 0;
        VfxQualityTier m_Quality = VfxQualityTier::High;
        VfxCullingMode m_Culling = VfxCullingMode::Automatic;
        Vector3 m_BoundsCenter;
        Vector3 m_BoundsExtent{5.0F, 5.0F, 5.0F};
        bool m_EditModePreview = false;
        std::vector<VfxParameterOverride> m_ParameterOverrides;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateVfxEmitterComponentRegistration();
} // namespace Keire
