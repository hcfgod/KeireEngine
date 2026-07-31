#pragma once

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace KeireEditor
{
    struct EditModeVfxEmitterSnapshot
    {
        Keire::EntityId Entity;
        Keire::AssetId Effect;
        std::uint32_t SeedOffset = 0;
        float SimulationSpeed = 1.0F;
        Keire::Vector3 Position;
        Keire::Quaternion Rotation;

        bool operator==(const EditModeVfxEmitterSnapshot&) const = default;
    };

    // Scene transforms can contain harmless floating-point decomposition noise. Preview reconciliation uses this
    // comparison to distinguish that noise from an authored gizmo move. Quaternion signs are treated as equivalent
    // because q and -q encode the same rotation.
    [[nodiscard]] inline bool EditModeVfxTransformChanged(const Keire::Vector3 previousPosition,
                                                          const Keire::Quaternion previousRotation,
                                                          const Keire::Vector3 position,
                                                          const Keire::Quaternion rotation) noexcept
    {
        constexpr float positionEpsilon = 0.0001F;
        if (std::abs(previousPosition.X - position.X) > positionEpsilon ||
            std::abs(previousPosition.Y - position.Y) > positionEpsilon ||
            std::abs(previousPosition.Z - position.Z) > positionEpsilon)
            return true;

        const auto lengthSquared = [](const Keire::Quaternion value) noexcept
        { return value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W; };
        const float previousLengthSquared = lengthSquared(previousRotation);
        const float lengthSquaredValue = lengthSquared(rotation);
        if (previousLengthSquared <= 0.0F || lengthSquaredValue <= 0.0F)
            return true;

        const float dot = previousRotation.X * rotation.X + previousRotation.Y * rotation.Y +
                          previousRotation.Z * rotation.Z + previousRotation.W * rotation.W;
        const float cosine = std::abs(dot) / std::sqrt(previousLengthSquared * lengthSquaredValue);
        constexpr float rotationEpsilon = 0.0001F;
        return 1.0F - cosine > rotationEpsilon;
    }

    // Local-space particles already inherit their emitter transform. World-space particles do not, so the editor
    // restarts that preview after a gizmo relocation to avoid displaying particle history at the old authoring
    // position. Runtime worlds retain the authored World-space behavior.
    [[nodiscard]] inline bool EditModeVfxPreviewRequiresRestart(const Keire::VfxSimulationSpace space,
                                                                const Keire::Vector3 previousPosition,
                                                                const Keire::Quaternion previousRotation,
                                                                const Keire::Vector3 position,
                                                                const Keire::Quaternion rotation) noexcept
    {
        return space == Keire::VfxSimulationSpace::World &&
               EditModeVfxTransformChanged(previousPosition, previousRotation, position, rotation);
    }

    // A live draft must be reactivated after its presentation host changes. A naturally completed draft is
    // reactivated only when Loop Preview is enabled; moving an already-idle host does not override that transport
    // choice.
    [[nodiscard]] inline bool EditModeVfxDraftShouldActivate(const bool alive, const bool presentationChanged,
                                                             const bool autoRestart) noexcept
    {
        return (alive && presentationChanged) || (!alive && autoRestart);
    }

    [[nodiscard]] inline std::vector<EditModeVfxEmitterSnapshot>
    CollectEditModeVfxEmitters(const Keire::Ref<Keire::Scene>& scene)
    {
        std::vector<EditModeVfxEmitterSnapshot> result;
        if (!scene)
            return result;

        const auto entities = scene->Query<Keire::VfxEmitterComponent>();
        result.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto emitter = entity.GetComponent<Keire::VfxEmitterComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!emitter || !transform || !entity.ActiveInHierarchy() || !emitter->Enabled() ||
                !emitter->EditModePreview() || !emitter->Effect())
                continue;

            Keire::Vector3 position;
            Keire::Quaternion rotation;
            Keire::Vector3 scale;
            if (!Keire::Math::DecomposeTransform(transform->WorldMatrix(), position, rotation, scale))
                continue;

            result.push_back({entity.Id(), emitter->Effect(), emitter->SeedOffset(), emitter->SimulationSpeed(),
                              position, rotation});
        }
        return result;
    }

    // The transient VFX document preview replaces one matching scene preview so unsaved authoring changes remain
    // visible without drawing a second copy at world origin. Selection wins; otherwise the lowest stable entity ID
    // provides a deterministic fallback. Other matching and unrelated emitters continue using their own handles.
    [[nodiscard]] inline std::optional<EditModeVfxEmitterSnapshot>
    SelectEditModeVfxDraftHost(const std::span<const EditModeVfxEmitterSnapshot> emitters, const Keire::AssetId effect,
                               const Keire::EntityId preferred = {}) noexcept
    {
        if (!effect)
            return std::nullopt;
        if (preferred)
        {
            for (const auto& emitter : emitters)
                if (emitter.Entity == preferred && emitter.Effect == effect)
                    return emitter;
        }
        const EditModeVfxEmitterSnapshot* fallback = nullptr;
        for (const auto& emitter : emitters)
        {
            if (emitter.Effect == effect && (!fallback || emitter.Entity < fallback->Entity))
                fallback = &emitter;
        }
        return fallback ? std::optional<EditModeVfxEmitterSnapshot>(*fallback) : std::nullopt;
    }
} // namespace KeireEditor
