#include "KeireInternal/Scripting/ScriptSystemInternal.h"

#include <algorithm>

namespace Keire
{
    [[nodiscard]] Ref<AnimatorComponent> ScriptSystem::Impl::RuntimeAnimator(const std::uint64_t world,
                                                                             const std::uint64_t high,
                                                                             const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        return entity ? entity.GetComponent<AnimatorComponent>() : Ref<AnimatorComponent>{};
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorFloat(const std::uint64_t world,
                                                                           const std::uint64_t high,
                                                                           const std::uint64_t low,
                                                                           const Coral::String parameter,
                                                                           const float value) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetFloat(static_cast<std::string>(parameter), value);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorInteger(const std::uint64_t world,
                                                                             const std::uint64_t high,
                                                                             const std::uint64_t low,
                                                                             const Coral::String parameter,
                                                                             const std::int32_t value) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetInteger(static_cast<std::string>(parameter), value);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorBoolean(const std::uint64_t world,
                                                                             const std::uint64_t high,
                                                                             const std::uint64_t low,
                                                                             const Coral::String parameter,
                                                                             const std::uint8_t value) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetBool(static_cast<std::string>(parameter), value != 0);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorTrigger(const std::uint64_t world,
                                                                             const std::uint64_t high,
                                                                             const std::uint64_t low,
                                                                             const Coral::String parameter,
                                                                             const std::uint8_t set) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            if (set != 0)
                animator->SetTrigger(static_cast<std::string>(parameter));
            else
                animator->ResetTrigger(static_cast<std::string>(parameter));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorLayerWeight(const std::uint64_t world,
                                                                                 const std::uint64_t high,
                                                                                 const std::uint64_t low,
                                                                                 const Coral::String layer,
                                                                                 const float value) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetLayerWeight(static_cast<std::string>(layer), value);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimePlayAnimator(const std::uint64_t world, const std::uint64_t high,
                                            const std::uint64_t low, const Coral::String state,
                                            const Coral::String layer, const float normalizedTime) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->Play(static_cast<std::string>(state), static_cast<std::string>(layer), normalizedTime);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeCrossFadeAnimator(
        const std::uint64_t world, const std::uint64_t high, const std::uint64_t low, const Coral::String state,
        const Coral::String layer, const float duration, const float normalizedTime) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->CrossFade(static_cast<std::string>(state), duration, static_cast<std::string>(layer),
                                normalizedTime);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimePauseAnimator(const std::uint64_t world,
                                                                        const std::uint64_t high,
                                                                        const std::uint64_t low,
                                                                        const std::uint8_t paused) noexcept
    {
        const auto animator = RuntimeAnimator(world, high, low);
        if (!animator)
            return 0;
        animator->SetPaused(paused != 0);
        return 1;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeStopAnimator(const std::uint64_t world,
                                                                       const std::uint64_t high,
                                                                       const std::uint64_t low) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->Stop();
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorSpeed(const std::uint64_t world,
                                                                           const std::uint64_t high,
                                                                           const std::uint64_t low,
                                                                           const float speed) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetSpeed(speed);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorFootGroundingWeight(const std::uint64_t world,
                                                                                         const std::uint64_t high,
                                                                                         const std::uint64_t low,
                                                                                         const float weight) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetRuntimeFootGroundingWeight(weight);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetProceduralLocomotion(
        const std::uint64_t world, const std::uint64_t high, const std::uint64_t low,
        const Vector3 desiredWorldVelocity, const Vector3 facingWorldDirection, const Vector3 lookWorldDirection,
        const float crouchAmount, const float runBlend, const std::uint8_t jumpRequested) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            animator->SetProceduralLocomotion({desiredWorldVelocity, facingWorldDirection, lookWorldDirection,
                                               crouchAmount, runBlend, jumpRequested != 0});
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeGetProceduralLocomotionState(const std::uint64_t world, const std::uint64_t high,
                                                            const std::uint64_t low,
                                                            Detail::NativeProceduralLocomotionState* result) noexcept
    {
        if (!result)
            return 0;
        const auto animator = RuntimeAnimator(world, high, low);
        if (!animator)
            return 0;
        const auto& state = animator->ProceduralState();
        result->ActualWorldVelocity = state.ActualWorldVelocity;
        result->GroundNormal = state.GroundNormal;
        result->GaitPhase = state.GaitPhase;
        result->Speed = state.Speed;
        result->VerticalSpeed = state.VerticalSpeed;
        result->LandingIntensity = state.LandingIntensity;
        result->State = static_cast<std::uint8_t>(state.State);
        result->Quality = static_cast<std::uint8_t>(state.Quality);
        result->Grounded = state.Grounded ? 1 : 0;
        result->LeftFootPlanted = state.LeftFootPlanted ? 1 : 0;
        result->RightFootPlanted = state.RightFootPlanted ? 1 : 0;
        return 1;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetAnimatorState(const std::uint64_t world,
                                                                           const std::uint64_t high,
                                                                           const std::uint64_t low,
                                                                           Detail::NativeAnimatorState* state) noexcept
    {
        if (!state)
            return 0;
        const auto animator = RuntimeAnimator(world, high, low);
        if (!animator)
            return 0;
        state->Speed = animator->Speed();
        state->NormalizedTime = animator->NormalizedTime();
        state->Playing = animator->RuntimePlaying() ? 1 : 0;
        state->Paused = animator->Paused() ? 1 : 0;
        return 1;
    }

    [[nodiscard]] std::int32_t ScriptSystem::Impl::RuntimeGetAnimatorStateName(const std::uint64_t world,
                                                                               const std::uint64_t high,
                                                                               const std::uint64_t low,
                                                                               char* destination,
                                                                               const std::int32_t capacity) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator)
                return 0;
            const auto name = animator->CurrentState();
            if (destination && capacity > 0)
                std::copy_n(name.begin(), std::min(name.size(), static_cast<std::size_t>(capacity)), destination);
            return static_cast<std::int32_t>(
                std::min<std::size_t>(name.size(), std::numeric_limits<std::int32_t>::max()));
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorTwoBoneIk(
        const std::uint64_t world, const std::uint64_t high, const std::uint64_t low, const Coral::String goal,
        const Coral::String root, const Coral::String middle, const Coral::String end, const Vector3 target,
        const Vector3 pole, const float weight, const std::uint8_t space) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator || space > static_cast<std::uint8_t>(AnimatorIkSpace::World))
                return 0;
            animator->SetTwoBoneIk(static_cast<std::string>(goal), static_cast<std::string>(root),
                                   static_cast<std::string>(middle), static_cast<std::string>(end), target, pole,
                                   weight, static_cast<AnimatorIkSpace>(space));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAnimatorFabrikIk(
        const std::uint64_t world, const std::uint64_t high, const std::uint64_t low, const Coral::String goal,
        const Coral::String encodedBones, const Vector3 target, const float weight,
        const std::uint32_t maximumIterations, const float tolerance, const std::uint8_t space) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            if (!animator || space > static_cast<std::uint8_t>(AnimatorIkSpace::World))
                return 0;
            const auto encoded = static_cast<std::string>(encodedBones);
            std::vector<std::string> bones;
            std::size_t offset = 0;
            while (offset <= encoded.size())
            {
                const auto next = encoded.find('\x1f', offset);
                bones.emplace_back(encoded.substr(offset, next == std::string::npos ? next : next - offset));
                if (next == std::string::npos)
                    break;
                offset = next + 1;
            }
            animator->SetFabrikIk(static_cast<std::string>(goal), std::move(bones), target, weight, maximumIterations,
                                  tolerance, static_cast<AnimatorIkSpace>(space));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeClearAnimatorIk(const std::uint64_t world,
                                                                          const std::uint64_t high,
                                                                          const std::uint64_t low,
                                                                          const Coral::String goal) noexcept
    {
        try
        {
            const auto animator = RuntimeAnimator(world, high, low);
            return animator && animator->ClearIk(static_cast<std::string>(goal)) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeTryGetAnimatorFloat(const std::uint64_t world,
                                                                              const std::uint64_t high,
                                                                              const std::uint64_t low,
                                                                              const Coral::String parameter,
                                                                              float* value) noexcept
    {
        try
        {
            if (!value)
                return 0;
            const auto animator = RuntimeAnimator(world, high, low);
            const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
            const auto name = static_cast<std::string>(parameter);
            if (!snapshot)
                return 0;
            const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                    { return candidate.Name == name || candidate.Id == name; });
            if (found == snapshot->Parameters.end() || found->Type != AnimationParameterType::Float)
                return 0;
            *value = found->FloatValue;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeTryGetAnimatorInteger(const std::uint64_t world,
                                                                                const std::uint64_t high,
                                                                                const std::uint64_t low,
                                                                                const Coral::String parameter,
                                                                                std::int32_t* value) noexcept
    {
        try
        {
            if (!value)
                return 0;
            const auto animator = RuntimeAnimator(world, high, low);
            const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
            const auto name = static_cast<std::string>(parameter);
            if (!snapshot)
                return 0;
            const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                    { return candidate.Name == name || candidate.Id == name; });
            if (found == snapshot->Parameters.end() || found->Type != AnimationParameterType::Integer)
                return 0;
            *value = found->IntegerValue;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeTryGetAnimatorBoolean(const std::uint64_t world,
                                                                                const std::uint64_t high,
                                                                                const std::uint64_t low,
                                                                                const Coral::String parameter,
                                                                                std::uint8_t* value) noexcept
    {
        try
        {
            if (!value)
                return 0;
            const auto animator = RuntimeAnimator(world, high, low);
            const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
            const auto name = static_cast<std::string>(parameter);
            if (!snapshot)
                return 0;
            const auto found = std::ranges::find_if(snapshot->Parameters, [&](const auto& candidate)
                                                    { return candidate.Name == name || candidate.Id == name; });
            if (found == snapshot->Parameters.end() ||
                (found->Type != AnimationParameterType::Boolean && found->Type != AnimationParameterType::Trigger))
                return 0;
            *value = found->BooleanValue ? 1 : 0;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeTryGetAnimatorLayerWeight(const std::uint64_t world,
                                                                                    const std::uint64_t high,
                                                                                    const std::uint64_t low,
                                                                                    const Coral::String layer,
                                                                                    float* value) noexcept
    {
        try
        {
            if (!value)
                return 0;
            const auto animator = RuntimeAnimator(world, high, low);
            const auto snapshot = animator ? animator->RuntimeDebugSnapshot() : nullptr;
            const auto name = static_cast<std::string>(layer);
            if (!snapshot)
                return 0;
            const auto found = std::ranges::find_if(snapshot->Layers, [&](const auto& candidate)
                                                    { return candidate.Name == name || candidate.Id == name; });
            if (found == snapshot->Layers.end())
                return 0;
            *value = found->Weight;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeEntityExists(const std::uint64_t world,
                                                                       const std::uint64_t high,
                                                                       const std::uint64_t low) noexcept
    {
        return ResolveRuntimeEntity(world, high, low) ? 1 : 0;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetEntityActive(const std::uint64_t world,
                                                                          const std::uint64_t high,
                                                                          const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        return entity && entity.ActiveSelf() ? 1 : 0;
    }

    void ScriptSystem::Impl::RuntimeSetEntityActive(const std::uint64_t world, const std::uint64_t high,
                                                    const std::uint64_t low, const std::uint8_t active) noexcept
    {
        try
        {
            if (auto entity = ResolveRuntimeEntity(world, high, low))
                entity.SetActive(active != 0);
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] std::uint32_t ScriptSystem::Impl::RuntimeGetEntityLayer(const std::uint64_t world,
                                                                          const std::uint64_t high,
                                                                          const std::uint64_t low) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity ? entity.Layer() : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetEntityLayer(const std::uint64_t world,
                                                                         const std::uint64_t high,
                                                                         const std::uint64_t low,
                                                                         const std::uint32_t layer) noexcept
    {
        try
        {
            auto entity = ResolveRuntimeEntity(world, high, low);
            if (!entity)
                return 0;
            entity.SetLayer(layer);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetEntityActiveInHierarchy(const std::uint64_t world,
                                                                                     const std::uint64_t high,
                                                                                     const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        return entity && entity.ActiveInHierarchy() ? 1 : 0;
    }

    [[nodiscard]] std::int32_t ScriptSystem::Impl::RuntimeGetEntityName(const std::uint64_t world,
                                                                        const std::uint64_t high,
                                                                        const std::uint64_t low, char* destination,
                                                                        const std::int32_t capacity) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            if (!entity)
                return 0;
            const auto name = entity.Name();
            if (destination && capacity > 0)
                std::copy_n(name.begin(), std::min(name.size(), static_cast<std::size_t>(capacity)), destination);
            return static_cast<std::int32_t>(
                std::min<std::size_t>(name.size(), std::numeric_limits<std::int32_t>::max()));
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetEntityName(const std::uint64_t world,
                                                                        const std::uint64_t high,
                                                                        const std::uint64_t low,
                                                                        const Coral::String name) noexcept
    {
        try
        {
            auto entity = ResolveRuntimeEntity(world, high, low);
            if (!entity)
                return 0;
            entity.SetName(static_cast<std::string>(name));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetEntityParent(const std::uint64_t world,
                                                                          const std::uint64_t high,
                                                                          const std::uint64_t low,
                                                                          std::uint64_t* parentHigh,
                                                                          std::uint64_t* parentLow) noexcept
    {
        if (!parentHigh || !parentLow)
            return 0;
        *parentHigh = 0;
        *parentLow = 0;
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto parent = entity ? entity.Parent() : Entity{};
        if (!parent)
            return 0;
        *parentHigh = parent.Id().Value().High();
        *parentLow = parent.Id().Value().Low();
        return 1;
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetEntityParent(const std::uint64_t world, const std::uint64_t high,
                                               const std::uint64_t low, const std::uint64_t parentHigh,
                                               const std::uint64_t parentLow, const std::uint8_t preserveWorld) noexcept
    {
        try
        {
            auto entity = ResolveRuntimeEntity(world, high, low);
            const auto parent = parentHigh || parentLow ? ResolveRuntimeEntity(world, parentHigh, parentLow) : Entity{};
            if (!entity || ((parentHigh || parentLow) && !parent))
                return 0;
            entity.SetParent(parent, preserveWorld != 0);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::int32_t ScriptSystem::Impl::RuntimeGetEntityChildCount(const std::uint64_t world,
                                                                              const std::uint64_t high,
                                                                              const std::uint64_t low) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            return entity ? static_cast<std::int32_t>(std::min<std::size_t>(entity.Children().size(),
                                                                            std::numeric_limits<std::int32_t>::max()))
                          : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeGetEntityChild(const std::uint64_t world, const std::uint64_t high,
                                              const std::uint64_t low, const std::int32_t index,
                                              std::uint64_t* childHigh, std::uint64_t* childLow) noexcept
    {
        if (!childHigh || !childLow || index < 0)
            return 0;
        *childHigh = 0;
        *childLow = 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, high, low);
            const auto children = entity ? entity.Children() : std::vector<Entity>{};
            if (static_cast<std::size_t>(index) >= children.size())
                return 0;
            *childHigh = children[static_cast<std::size_t>(index)].Id().Value().High();
            *childLow = children[static_cast<std::size_t>(index)].Id().Value().Low();
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeComponentExists(const std::uint64_t world,
                                                                          const std::uint64_t entityHigh,
                                                                          const std::uint64_t entityLow,
                                                                          const std::uint64_t typeHigh,
                                                                          const std::uint64_t typeLow) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
        return entity && entity.HasComponent(ComponentTypeId(AssetId(typeHigh, typeLow))) ? 1 : 0;
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeAddComponent(const std::uint64_t world,
                                                                       const std::uint64_t entityHigh,
                                                                       const std::uint64_t entityLow,
                                                                       const std::uint64_t typeHigh,
                                                                       const std::uint64_t typeLow) noexcept
    {
        try
        {
            auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto type = ComponentTypeId(AssetId(typeHigh, typeLow));
            if (!entity || !type)
                return 0;
            return (entity.GetComponent(type) || entity.AddComponent(type)) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeRemoveComponent(const std::uint64_t world,
                                                                          const std::uint64_t entityHigh,
                                                                          const std::uint64_t entityLow,
                                                                          const std::uint64_t typeHigh,
                                                                          const std::uint64_t typeLow) noexcept
    {
        try
        {
            auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && entity.RemoveComponent(ComponentTypeId(AssetId(typeHigh, typeLow))) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetComponentEnabled(const std::uint64_t world,
                                                                              const std::uint64_t entityHigh,
                                                                              const std::uint64_t entityLow,
                                                                              const std::uint64_t typeHigh,
                                                                              const std::uint64_t typeLow) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
        const auto component =
            entity ? entity.GetComponent(ComponentTypeId(AssetId(typeHigh, typeLow))) : Ref<Component>{};
        return component && component->Enabled() ? 1 : 0;
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetComponentEnabled(const std::uint64_t world, const std::uint64_t entityHigh,
                                                   const std::uint64_t entityLow, const std::uint64_t typeHigh,
                                                   const std::uint64_t typeLow, const std::uint8_t enabled) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto component =
                entity ? entity.GetComponent(ComponentTypeId(AssetId(typeHigh, typeLow))) : Ref<Component>{};
            if (!component)
                return 0;
            component->SetEnabled(enabled != 0);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
} // namespace Keire
