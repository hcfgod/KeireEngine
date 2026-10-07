#include "KeireClient/Editor/EulerEditContinuity.h"

#include <cmath>

namespace KeireEditor
{
    Keire::Vector3 InspectorEulerState::Read(const Keire::Ref<Keire::TransformComponent>& transform)
    {
        if (!transform)
        {
            Reset();
            return {};
        }
        const bool sameTarget = m_Target.Lock() == transform;
        const auto orientation = transform->LocalRotation();
        if (!sameTarget || (!m_Editing && !SameRotation(orientation, m_Orientation)))
        {
            const auto reference = sameTarget ? m_Degrees : transform->LocalEulerAngles();
            m_Degrees = ContinuousEulerAngles(orientation, reference);
            m_Orientation = orientation;
            m_Target = transform;
            if (!sameTarget)
                m_Editing = false;
        }
        return m_Degrees;
    }

    void InspectorEulerState::SetValue(const Keire::Vector3 degrees, const Keire::Quaternion orientation) noexcept
    {
        m_Degrees = degrees;
        m_Orientation = orientation;
    }

    void InspectorEulerState::EndEdit(const Keire::Quaternion orientation) noexcept
    {
        m_Editing = false;
        m_Orientation = orientation;
    }

    void InspectorEulerState::Reset() noexcept
    {
        m_Target.Reset();
        m_Degrees = {};
        m_Orientation = {};
        m_Editing = false;
    }

    namespace
    {
        [[nodiscard]] float NearestEquivalentAngle(const float degrees, const float reference) noexcept
        {
            return degrees + std::round((reference - degrees) / 360.0F) * 360.0F;
        }

        [[nodiscard]] Keire::Vector3 NearestEquivalent(Keire::Vector3 value, const Keire::Vector3 reference) noexcept
        {
            value.X = NearestEquivalentAngle(value.X, reference.X);
            value.Y = NearestEquivalentAngle(value.Y, reference.Y);
            value.Z = NearestEquivalentAngle(value.Z, reference.Z);
            return value;
        }

        [[nodiscard]] float DistanceSquared(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
        {
            const float x = left.X - right.X;
            const float y = left.Y - right.Y;
            const float z = left.Z - right.Z;
            return x * x + y * y + z * z;
        }
    } // namespace

    Keire::Vector3 ContinuousEulerAngles(const Keire::Quaternion rotation, const Keire::Vector3 referenceDegrees)
    {
        const auto canonical = Keire::Math::QuaternionToEulerDegrees(rotation);
        const auto primary = NearestEquivalent(canonical, referenceDegrees);
        const auto alternate =
            NearestEquivalent({canonical.X + 180.0F, 180.0F - canonical.Y, canonical.Z + 180.0F}, referenceDegrees);
        return DistanceSquared(alternate, referenceDegrees) < DistanceSquared(primary, referenceDegrees) ? alternate
                                                                                                         : primary;
    }

    bool SameRotation(const Keire::Quaternion left, const Keire::Quaternion right, const float tolerance)
    {
        const auto normalizedLeft = Keire::Math::Normalize(left);
        const auto normalizedRight = Keire::Math::Normalize(right);
        const float alignment = std::abs(normalizedLeft.X * normalizedRight.X + normalizedLeft.Y * normalizedRight.Y +
                                         normalizedLeft.Z * normalizedRight.Z + normalizedLeft.W * normalizedRight.W);
        return 1.0F - alignment <= tolerance;
    }
} // namespace KeireEditor
