#pragma once

#include "Keire/ECS/Components/CharacterControllerComponent.h"

#include <array>
#include <stdexcept>

namespace Keire::Detail
{
    class CharacterMovementAccess
    {
      public:
        struct Snapshot
        {
            AssetId Controller;
            std::array<Vector3, CharacterControllerComponent::MaximumQueuedMovementCommands> Commands{};
            std::size_t Count = 0;
            Vector3 Displacement;
        };

        [[nodiscard]] static Snapshot Capture(const CharacterControllerComponent& controller) noexcept
        {
            Snapshot result;
            result.Controller = controller.m_RuntimeId;
            result.Count = controller.m_MovementCount;
            for (std::size_t index = 0; index < result.Count; ++index)
            {
                const auto command = controller.m_MovementCommands[index];
                result.Commands[index] = command;
                result.Displacement.X += command.X;
                result.Displacement.Y += command.Y;
                result.Displacement.Z += command.Z;
            }
            return result;
        }

        static void Validate(const CharacterControllerComponent& controller, const Snapshot& snapshot)
        {
            if (snapshot.Controller != controller.m_RuntimeId || snapshot.Count != controller.m_MovementCount)
            {
                throw std::logic_error("Prepared character movement queue changed before commit.");
            }
            for (std::size_t index = 0; index < snapshot.Count; ++index)
            {
                if (snapshot.Commands[index] != controller.m_MovementCommands[index])
                {
                    throw std::logic_error("Prepared character movement command changed before commit.");
                }
            }
        }

        // Validate every queue in a prepared batch first. No callbacks may intervene during its commit.
        static void Commit(CharacterControllerComponent& controller, const Snapshot& snapshot)
        {
            Validate(controller, snapshot);
            controller.m_MovementCount = 0;
        }
    };
} // namespace Keire::Detail
