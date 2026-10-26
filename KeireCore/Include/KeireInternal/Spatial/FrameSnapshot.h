#pragma once

#include "KeireInternal/Spatial/FrameRotation.h"
#include "KeireInternal/Spatial/SectorPosition.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    struct SpatialMotion final
    {
        SectorPosition Position;
        std::array<double, 3> Velocity;
        std::array<double, 3> Acceleration{};
    };

    // Immutable evaluation of one child-to-parent edge at an explicit time/revision.
    // Linear/angular velocity and acceleration use metres, seconds and radians in the parent axes.
    class FrameSnapshot final
    {
      public:
        FrameSnapshot(const SpatialIdentity child, SectorPosition parentOrigin, const FrameRotation rotation,
                      const std::array<double, 3>& linearVelocity, const std::array<double, 3>& angularVelocity,
                      const double time, const std::uint64_t revision,
                      const std::array<double, 3>& linearAcceleration = {},
                      const std::array<double, 3>& angularAcceleration = {})
            : m_ChildOrigin(child, {}, {}), m_ParentOrigin(std::move(parentOrigin)), m_Rotation(rotation),
              m_LinearVelocity(linearVelocity), m_AngularVelocity(angularVelocity),
              m_LinearAcceleration(linearAcceleration), m_AngularAcceleration(angularAcceleration), m_Time(time),
              m_Revision(revision)
        {
            if (child.World != m_ParentOrigin.Identity().World || child.Frame == m_ParentOrigin.Identity().Frame ||
                !std::isfinite(time) || revision == 0)
            {
                throw std::invalid_argument(
                    "Frame snapshot requires distinct same-world frames, finite time and revision.");
            }
            Validate(linearVelocity);
            Validate(angularVelocity);
            Validate(linearAcceleration);
            Validate(angularAcceleration);
        }

        [[nodiscard]] double Time() const noexcept { return m_Time; }

        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }

        [[nodiscard]] SpatialIdentity ChildIdentity() const noexcept { return m_ChildOrigin.Identity(); }

        [[nodiscard]] SpatialIdentity ParentIdentity() const noexcept { return m_ParentOrigin.Identity(); }

        [[nodiscard]] SpatialMotion ToParent(const SpatialMotion& child) const
        {
            Validate(child.Velocity);
            Validate(child.Acceleration);
            const auto radius = m_Rotation.Apply(child.Position.RelativeTo(m_ChildOrigin));
            const auto rotatedVelocity = m_Rotation.Apply(child.Velocity);
            const auto spinVelocity = Cross(m_AngularVelocity, radius);
            std::array<double, 3> velocity{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                velocity[axis] = m_LinearVelocity[axis] + spinVelocity[axis] + rotatedVelocity[axis];
            }
            Validate(velocity);
            const auto rotatedAcceleration = m_Rotation.Apply(child.Acceleration);
            const auto frameAcceleration = AccelerationAt(radius, rotatedVelocity);
            std::array<double, 3> acceleration{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                acceleration[axis] = frameAcceleration[axis] + rotatedAcceleration[axis];
            }
            Validate(acceleration);
            return {m_ParentOrigin.Translated(radius), velocity, acceleration};
        }

        [[nodiscard]] SpatialMotion ToChild(const SpatialMotion& parent) const
        {
            Validate(parent.Velocity);
            Validate(parent.Acceleration);
            const auto radius = parent.Position.RelativeTo(m_ParentOrigin);
            const auto spinVelocity = Cross(m_AngularVelocity, radius);
            std::array<double, 3> relativeVelocity{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                relativeVelocity[axis] = parent.Velocity[axis] - m_LinearVelocity[axis] - spinVelocity[axis];
            }
            Validate(relativeVelocity);
            const auto frameAcceleration = AccelerationAt(radius, relativeVelocity);
            std::array<double, 3> relativeAcceleration{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                relativeAcceleration[axis] = parent.Acceleration[axis] - frameAcceleration[axis];
            }
            Validate(relativeAcceleration);
            const auto inverse = m_Rotation.Inverse();
            return {m_ChildOrigin.Translated(inverse.Apply(radius)), inverse.Apply(relativeVelocity),
                    inverse.Apply(relativeAcceleration)};
        }

      private:
        [[nodiscard]] std::array<double, 3> AccelerationAt(const std::array<double, 3>& radius,
                                                           const std::array<double, 3>& relativeVelocity) const
        {
            const auto angular = Cross(m_AngularAcceleration, radius);
            const auto centripetal = Cross(m_AngularVelocity, Cross(m_AngularVelocity, radius));
            const auto coriolis = Cross(m_AngularVelocity, relativeVelocity);
            std::array<double, 3> result{};
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                result[axis] = m_LinearAcceleration[axis] + angular[axis] + centripetal[axis] + 2.0 * coriolis[axis];
            }
            Validate(result);
            return result;
        }

        static void Validate(const std::array<double, 3>& vector)
        {
            for (const auto value : vector)
            {
                if (!std::isfinite(value))
                {
                    throw std::invalid_argument("Frame motion must be finite and representable.");
                }
            }
        }

        [[nodiscard]] static std::array<double, 3> Cross(const std::array<double, 3>& a, const std::array<double, 3>& b)
        {
            const std::array result{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
            Validate(result);
            return result;
        }

        SectorPosition m_ChildOrigin;
        SectorPosition m_ParentOrigin;
        FrameRotation m_Rotation;
        std::array<double, 3> m_LinearVelocity;
        std::array<double, 3> m_AngularVelocity;
        std::array<double, 3> m_LinearAcceleration;
        std::array<double, 3> m_AngularAcceleration;
        double m_Time;
        std::uint64_t m_Revision;
    };
} // namespace Keire::Detail
