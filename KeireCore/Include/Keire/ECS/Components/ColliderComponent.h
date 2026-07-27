#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Physics/PhysicsSystem.h"

namespace Keire
{
    class KEIRE_API ColliderComponent final : public Component
    {
      public:
        ColliderComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245434f4cULL, 0x4c49444552000001ULL));
        }

        [[nodiscard]] ColliderShape Shape() const noexcept { return m_Shape; }
        [[nodiscard]] Vector3 Center() const noexcept { return m_Center; }
        [[nodiscard]] Vector3 HalfExtent() const noexcept { return m_HalfExtent; }
        [[nodiscard]] float Radius() const noexcept { return m_Radius; }
        [[nodiscard]] float Height() const noexcept { return m_Height; }
        [[nodiscard]] std::uint32_t Layer() const noexcept { return m_Layer; }
        [[nodiscard]] std::uint32_t Mask() const noexcept { return m_Mask; }
        [[nodiscard]] bool Trigger() const noexcept { return m_Trigger; }

        void SetShape(ColliderShape value);
        void SetCenter(Vector3 value);
        void SetHalfExtent(Vector3 value);
        void SetRadius(float value);
        void SetHeight(float value);
        void SetLayer(std::uint32_t value);
        void SetMask(std::uint32_t value);
        void SetTrigger(bool value);

      private:
        friend ComponentRegistration CreateColliderComponentRegistration();
        ColliderShape m_Shape = ColliderShape::Box;
        Vector3 m_Center;
        Vector3 m_HalfExtent{0.5F, 0.5F, 0.5F};
        float m_Radius = 0.5F;
        float m_Height = 1.0F;
        std::uint32_t m_Layer = 1;
        std::uint32_t m_Mask = ~0U;
        bool m_Trigger = false;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateColliderComponentRegistration();
} // namespace Keire
