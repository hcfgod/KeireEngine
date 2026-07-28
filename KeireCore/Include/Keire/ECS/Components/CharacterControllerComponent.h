#pragma once

#include "Keire/ECS/Component.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Keire
{
    struct CharacterControllerRuntimeState
    {
        std::uint32_t Generation = 0;
        bool Grounded = false;
        Vector3 GroundNormal{0.0F, 1.0F, 0.0F};
        Vector3 Velocity;
        std::uint64_t DroppedMovementCommands = 0;

        [[nodiscard]] bool operator==(const CharacterControllerRuntimeState&) const noexcept = default;
    };

    class KEIRE_API CharacterControllerComponent final : public Component
    {
      public:
        static constexpr std::size_t MaximumQueuedMovementCommands = 64;

        CharacterControllerComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245434841ULL, 0x5241435445520001ULL));
        }

        [[nodiscard]] AssetId RuntimeId() const noexcept { return m_RuntimeId; }
        [[nodiscard]] float Radius() const noexcept { return m_Radius; }
        [[nodiscard]] float Height() const noexcept { return m_Height; }
        [[nodiscard]] float MaximumSlopeDegrees() const noexcept { return m_MaximumSlopeDegrees; }
        [[nodiscard]] float StepHeight() const noexcept { return m_StepHeight; }
        [[nodiscard]] float SkinWidth() const noexcept { return m_SkinWidth; }
        [[nodiscard]] std::uint32_t Layer() const noexcept { return m_Layer; }
        [[nodiscard]] std::uint32_t Mask() const noexcept { return m_Mask; }
        [[nodiscard]] std::size_t QueuedMovementCount() const noexcept { return m_MovementCount; }
        [[nodiscard]] CharacterControllerRuntimeState RuntimeState() const noexcept { return m_RuntimeState; }
        [[nodiscard]] bool Grounded() const noexcept { return m_RuntimeState.Grounded; }

        void SetRuntimeId(AssetId value);
        void Resize(float radius, float height);
        void ConfigureCapsule(float radius, float height, float stepHeight, float skinWidth);
        void SetMaximumSlopeDegrees(float value);
        void SetStepHeight(float value);
        void SetSkinWidth(float value);
        void SetLayer(std::uint32_t value);
        void SetMask(std::uint32_t value);

        [[nodiscard]] bool QueueDesiredMovement(Vector3 displacement);
        [[nodiscard]] Vector3 ConsumeDesiredMovement() noexcept;
        void ClearDesiredMovement() noexcept;
        void ApplyRuntimeState(std::uint32_t generation, bool grounded, Vector3 groundNormal, Vector3 velocity);
        void ResetRuntimeState() noexcept;

      private:
        AssetId m_RuntimeId = AssetId::Generate();
        float m_Radius = 0.5F;
        float m_Height = 2.0F;
        float m_MaximumSlopeDegrees = 45.0F;
        float m_StepHeight = 0.3F;
        float m_SkinWidth = 0.05F;
        std::uint32_t m_Layer = 1;
        std::uint32_t m_Mask = ~0U;
        std::array<Vector3, MaximumQueuedMovementCommands> m_MovementCommands;
        std::size_t m_MovementCount = 0;
        CharacterControllerRuntimeState m_RuntimeState;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateCharacterControllerComponentRegistration();
} // namespace Keire
