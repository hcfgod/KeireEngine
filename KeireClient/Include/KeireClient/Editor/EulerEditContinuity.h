#pragma once

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

namespace KeireEditor
{
    [[nodiscard]] Keire::Vector3 ContinuousEulerAngles(Keire::Quaternion rotation, Keire::Vector3 referenceDegrees);
    [[nodiscard]] bool SameRotation(Keire::Quaternion left, Keire::Quaternion right, float tolerance = 0.00001F);

    class InspectorEulerState final
    {
      public:
        [[nodiscard]] Keire::Vector3 Read(const Keire::Ref<Keire::TransformComponent>& transform);
        void SetValue(Keire::Vector3 degrees, Keire::Quaternion orientation) noexcept;
        void SetEditing(bool editing) noexcept { m_Editing = editing; }
        void EndEdit(Keire::Quaternion orientation) noexcept;
        void Reset() noexcept;

      private:
        // Play clones retain entity IDs, but must not inherit the editing scene's display state.
        Keire::WeakRef<Keire::TransformComponent> m_Target;
        Keire::Vector3 m_Degrees;
        Keire::Quaternion m_Orientation;
        bool m_Editing = false;
    };
} // namespace KeireEditor
