#include "KeireInternal/Scripting/ScriptSystemInternal.h"

#include <cmath>

namespace Keire
{
    [[nodiscard]] Vector3 ScriptSystem::Impl::RuntimeGetLocalScale(const std::uint64_t world, const std::uint64_t high,
                                                                   const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->LocalScale() : Vector3{1.0F, 1.0F, 1.0F};
    }

    void ScriptSystem::Impl::RuntimeSetLocalScale(const std::uint64_t world, const std::uint64_t high,
                                                  const std::uint64_t low, const Vector3 value) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
        {
            try
            {
                transform->SetLocalScale(value);
            }
            catch (...)
            {
            }
        }
    }

    [[nodiscard]] Vector3 ScriptSystem::Impl::RuntimeGetWorldPosition(const std::uint64_t world,
                                                                      const std::uint64_t high,
                                                                      const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->WorldPosition() : Vector3{};
    }

    [[nodiscard]] Vector3 ScriptSystem::Impl::RuntimeGetPresentationWorldPosition(const std::uint64_t world,
                                                                                  const std::uint64_t high,
                                                                                  const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->PresentationWorldPosition() : Vector3{};
    }

    [[nodiscard]] Quaternion ScriptSystem::Impl::RuntimeGetPresentationWorldRotation(const std::uint64_t world,
                                                                                     const std::uint64_t high,
                                                                                     const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
        return transform ? transform->PresentationWorldRotation() : Quaternion{};
    }

    void ScriptSystem::Impl::RuntimeResetPresentationInterpolation(const std::uint64_t world, const std::uint64_t high,
                                                                   const std::uint64_t low) noexcept
    {
        const auto entity = ResolveRuntimeEntity(world, high, low);
        if (const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{})
            transform->ResetPresentationInterpolation();
    }

    void ScriptSystem::Impl::RuntimeCloneEntity(const std::uint64_t world, const std::uint64_t high,
                                                const std::uint64_t low, std::uint64_t* resultHigh,
                                                std::uint64_t* resultLow) noexcept
    {
        if (!resultHigh || !resultLow)
            return;
        *resultHigh = 0;
        *resultLow = 0;
        try
        {
            if (auto entity = ResolveRuntimeEntity(world, high, low))
            {
                const auto clone = entity.Clone();
                *resultHigh = clone.Id().Value().High();
                *resultLow = clone.Id().Value().Low();
            }
        }
        catch (...)
        {
        }
    }

    void ScriptSystem::Impl::RuntimeDestroyEntity(const std::uint64_t world, const std::uint64_t high,
                                                  const std::uint64_t low) noexcept
    {
        try
        {
            if (auto entity = ResolveRuntimeEntity(world, high, low))
                (void)entity.Destroy();
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimePlayAudio(const std::uint64_t world, const std::uint64_t entityHigh,
                                         const std::uint64_t entityLow, const std::uint64_t clipHigh,
                                         const std::uint64_t clipLow, const float gain) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(
                                 {.Entity = entity.Id().Value(), .Clip = AssetId(clipHigh, clipLow), .Gain = gain})
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimePlayAudioAdvanced(
        const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
        const std::uint64_t clipHigh, const std::uint64_t clipLow, const std::uint64_t mixerHigh,
        const std::uint64_t mixerLow, const std::uint64_t busHigh, const std::uint64_t busLow, const Coral::String bus,
        const float gain, const float pitch, const std::uint32_t priority, const std::uint8_t loop,
        const std::uint8_t spatial, const float minimumDistance, const float maximumDistance) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(
                                 {.Entity = entity.Id().Value(),
                                  .Clip = AssetId(clipHigh, clipLow),
                                  .Bus = static_cast<std::string>(bus),
                                  .Gain = gain,
                                  .Pitch = pitch,
                                  .Priority = priority,
                                  .Loop = loop != 0,
                                  .Spatial = spatial != 0,
                                  .MinimumDistance = minimumDistance,
                                  .MaximumDistance = maximumDistance,
                                  .Mixer = AssetId(mixerHigh, mixerLow),
                                  .BusId = AssetId(busHigh, busLow)})
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeStopAudio(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->StopManagedAudio(entity.Id().Value()) ? 1
                                                                                                                  : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimePlayAudioSource(const std::uint64_t world,
                                                                          const std::uint64_t entityHigh,
                                                                          const std::uint64_t entityLow) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source || !source->Clip())
                return 0;
            ManagedAudioPlayback playback;
            playback.Entity = entity.Id().Value();
            playback.Clip = source->Clip();
            playback.Bus = source->Bus();
            playback.Gain = source->Gain();
            playback.Pitch = source->Pitch();
            playback.Priority = source->Priority();
            playback.Loop = source->Loop();
            playback.Spatial = source->Spatial();
            playback.MinimumDistance = source->MinimumDistance();
            playback.MaximumDistance = source->MaximumDistance();
            playback.Mixer = source->Mixer();
            playback.BusId = source->BusId();
            playback.Attenuation = source->Attenuation();
            return CurrentRuntime->Specification.RuntimeServices->PlayManagedAudio(playback) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimePauseAudio(const std::uint64_t world,
                                                                     const std::uint64_t entityHigh,
                                                                     const std::uint64_t entityLow,
                                                                     const std::uint8_t paused) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->PauseManagedAudio(entity.Id().Value(),
                                                                                              paused != 0)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSeekAudio(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const float positionSeconds) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->SeekManagedAudio(entity.Id().Value(),
                                                                                             positionSeconds)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeGetAudioSourceProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                                        const std::uint64_t entityLow,
                                                        Detail::NativeAudioSourceProperties* properties) noexcept
    {
        if (!properties)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source)
                return 0;
            const auto clip = source->Clip();
            const auto mixer = source->Mixer();
            const auto bus = source->BusId();
            properties->ClipHigh = clip.High();
            properties->ClipLow = clip.Low();
            properties->MixerHigh = mixer.High();
            properties->MixerLow = mixer.Low();
            properties->BusHigh = bus.High();
            properties->BusLow = bus.Low();
            properties->Gain = source->Gain();
            properties->Pitch = source->Pitch();
            properties->MinimumDistance = source->MinimumDistance();
            properties->MaximumDistance = source->MaximumDistance();
            properties->Priority = source->Priority();
            properties->Loop = source->Loop() ? 1 : 0;
            properties->Spatial = source->Spatial() ? 1 : 0;
            properties->PlayOnAwake = source->PlayOnAwake() ? 1 : 0;
            if (CurrentRuntime && CurrentRuntime->Specification.RuntimeServices)
            {
                const auto status =
                    CurrentRuntime->Specification.RuntimeServices->ManagedAudioStatus(entity.Id().Value());
                properties->PositionSeconds = status.PositionSeconds;
                properties->DurationSeconds = status.DurationSeconds;
                properties->PlaybackState = static_cast<std::uint8_t>(status.State);
            }
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAudioSourceClip(const std::uint64_t world,
                                                                             const std::uint64_t entityHigh,
                                                                             const std::uint64_t entityLow,
                                                                             const std::uint64_t clipHigh,
                                                                             const std::uint64_t clipLow) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source)
                return 0;
            source->SetClip(AssetId(clipHigh, clipLow));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetAudioSourceRouting(const std::uint64_t world, const std::uint64_t entityHigh,
                                                     const std::uint64_t entityLow, const std::uint64_t mixerHigh,
                                                     const std::uint64_t mixerLow, const std::uint64_t busHigh,
                                                     const std::uint64_t busLow) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source)
                return 0;
            auto state = source->CaptureState();
            state.Mixer = AssetId(mixerHigh, mixerLow);
            state.BusId = AssetId(busHigh, busLow);
            source->ApplyState(std::move(state));
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAudioSourceScalar(const std::uint64_t world,
                                                                               const std::uint64_t entityHigh,
                                                                               const std::uint64_t entityLow,
                                                                               const std::uint8_t property,
                                                                               const float value) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source)
                return 0;
            if (property == 0)
                source->SetGain(value);
            else if (property == 1)
                source->SetPitch(value);
            else if (property == 2)
                source->SetMinimumDistance(value);
            else if (property == 3)
                source->SetMaximumDistance(value);
            else if (property == 4 && std::isfinite(value) && value >= 0.0F && value <= 255.0F &&
                     std::floor(value) == value)
                source->SetPriority(static_cast<std::uint32_t>(value));
            else
                return 0;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAudioSourceFlag(const std::uint64_t world,
                                                                             const std::uint64_t entityHigh,
                                                                             const std::uint64_t entityLow,
                                                                             const std::uint8_t property,
                                                                             const std::uint8_t value) noexcept
    {
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto source = entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
            if (!source)
                return 0;
            if (property == 0)
                source->SetLoop(value != 0);
            else if (property == 1)
                source->SetSpatial(value != 0);
            else if (property == 2)
                source->SetPlayOnAwake(value != 0);
            else
                return 0;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeGetAudioListenerProperties(const std::uint64_t world, const std::uint64_t entityHigh,
                                                          const std::uint64_t entityLow,
                                                          Detail::NativeAudioListenerProperties* properties) noexcept
    {
        if (!properties)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto listener =
                entity ? entity.GetComponent<AudioListenerComponent>() : Ref<AudioListenerComponent>{};
            if (!listener)
                return 0;
            properties->Gain = listener->Gain();
            properties->Primary = listener->Primary() ? 1 : 0;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAudioListenerProperties(
        const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
        const Detail::NativeAudioListenerProperties* properties) noexcept
    {
        if (!properties || !std::isfinite(properties->Gain) || properties->Gain < 0.0F || properties->Gain > 16.0F)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto listener =
                entity ? entity.GetComponent<AudioListenerComponent>() : Ref<AudioListenerComponent>{};
            if (!listener)
                return 0;
            listener->SetGain(properties->Gain);
            listener->SetPrimary(properties->Primary != 0);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeGetAudioReverbZoneProperties(
        const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
        Detail::NativeAudioReverbZoneProperties* properties) noexcept
    {
        if (!properties)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto zone =
                entity ? entity.GetComponent<AudioReverbZoneComponent>() : Ref<AudioReverbZoneComponent>{};
            if (!zone)
                return 0;
            const auto mixer = zone->Mixer();
            const auto snapshot = zone->SnapshotId();
            properties->MixerHigh = mixer.High();
            properties->MixerLow = mixer.Low();
            properties->SnapshotHigh = snapshot.High();
            properties->SnapshotLow = snapshot.Low();
            properties->BoxHalfExtent = zone->BoxHalfExtent();
            properties->SphereRadius = zone->SphereRadius();
            properties->BlendDistance = zone->BlendDistance();
            properties->ReverbSend = zone->ReverbSend();
            properties->Priority = zone->Priority();
            properties->Shape = static_cast<std::uint8_t>(zone->Shape());
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetAudioReverbZoneProperties(
        const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
        const Detail::NativeAudioReverbZoneProperties* properties) noexcept
    {
        if (!properties || properties->Shape > static_cast<std::uint8_t>(AudioReverbZoneShape::Sphere) ||
            !Math::IsFinite(properties->BoxHalfExtent) || properties->BoxHalfExtent.X <= 0.0F ||
            properties->BoxHalfExtent.Y <= 0.0F || properties->BoxHalfExtent.Z <= 0.0F ||
            properties->BoxHalfExtent.X > 100000.0F || properties->BoxHalfExtent.Y > 100000.0F ||
            properties->BoxHalfExtent.Z > 100000.0F || !std::isfinite(properties->SphereRadius) ||
            properties->SphereRadius <= 0.0F || properties->SphereRadius > 100000.0F ||
            !std::isfinite(properties->BlendDistance) || properties->BlendDistance < 0.0F ||
            properties->BlendDistance > 100000.0F || !std::isfinite(properties->ReverbSend) ||
            properties->ReverbSend < 0.0F || properties->ReverbSend > 1.0F || properties->Priority < -32768 ||
            properties->Priority > 32767)
        {
            return 0;
        }
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            const auto zone =
                entity ? entity.GetComponent<AudioReverbZoneComponent>() : Ref<AudioReverbZoneComponent>{};
            if (!zone)
                return 0;
            zone->SetMixer(AssetId(properties->MixerHigh, properties->MixerLow));
            zone->SetSnapshotId(AssetId(properties->SnapshotHigh, properties->SnapshotLow));
            zone->SetShape(static_cast<AudioReverbZoneShape>(properties->Shape));
            zone->SetBoxHalfExtent(properties->BoxHalfExtent);
            zone->SetSphereRadius(properties->SphereRadius);
            zone->SetPriority(properties->Priority);
            zone->SetBlendDistance(properties->BlendDistance);
            zone->SetReverbSend(properties->ReverbSend);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimePlayVfx(const std::uint64_t world, const std::uint64_t entityHigh,
                                       const std::uint64_t entityLow, const std::uint64_t effectHigh,
                                       const std::uint64_t effectLow, const std::uint8_t restart) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->PlayManagedVfx(
                                 entity.Id().Value(), AssetId(effectHigh, effectLow), restart != 0)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeStopVfx(const std::uint64_t world,
                                                                  const std::uint64_t entityHigh,
                                                                  const std::uint64_t entityLow) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->StopManagedVfx(entity.Id().Value()) ? 1 : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimePauseVfx(const std::uint64_t world,
                                                                   const std::uint64_t entityHigh,
                                                                   const std::uint64_t entityLow,
                                                                   const std::uint8_t paused) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->PauseManagedVfx(entity.Id().Value(),
                                                                                            paused != 0)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeIsVfxAlive(const std::uint64_t world,
                                                                     const std::uint64_t entityHigh,
                                                                     const std::uint64_t entityLow) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->IsManagedVfxAlive(entity.Id().Value()) ? 1
                                                                                                                   : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSendVfxEvent(const std::uint64_t world,
                                                                       const std::uint64_t entityHigh,
                                                                       const std::uint64_t entityLow,
                                                                       const Coral::String eventName,
                                                                       const std::uint32_t spawnCount) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->SendManagedVfxEvent(
                                 entity.Id().Value(), static_cast<std::string>(eventName), spawnCount)
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxParameter(const std::uint64_t world, const std::uint64_t entityHigh,
                                               const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                               const std::uint64_t parameterLow, VfxParameterValue value) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->SetManagedVfxParameter(
                                 entity.Id().Value(), {AssetId(parameterHigh, parameterLow), std::move(value)})
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxScalarRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                                 const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                 const std::uint64_t parameterLow, const float minimum,
                                                 const float maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxScalarRange{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxIntegerRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                  const std::uint64_t parameterLow, const std::int64_t minimum,
                                                  const std::int64_t maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxIntegerRange{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetVfxUnsignedIntegerRange(
        const std::uint64_t world, const std::uint64_t entityHigh, const std::uint64_t entityLow,
        const std::uint64_t parameterHigh, const std::uint64_t parameterLow, const std::uint64_t minimum,
        const std::uint64_t maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxUnsignedIntegerRange{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxVector2Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                  const std::uint64_t parameterLow, const Vector2 minimum,
                                                  const Vector2 maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxVector2Range{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxVector3Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                  const std::uint64_t parameterLow, const Vector3 minimum,
                                                  const Vector3 maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxVector3Range{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxVector4Range(const std::uint64_t world, const std::uint64_t entityHigh,
                                                  const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                  const std::uint64_t parameterLow, const Vector4 minimum,
                                                  const Vector4 maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxVector4Range{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t
    ScriptSystem::Impl::RuntimeSetVfxColorRange(const std::uint64_t world, const std::uint64_t entityHigh,
                                                const std::uint64_t entityLow, const std::uint64_t parameterHigh,
                                                const std::uint64_t parameterLow, const Color minimum,
                                                const Color maximum) noexcept
    {
        return RuntimeSetVfxParameter(world, entityHigh, entityLow, parameterHigh, parameterLow,
                                      VfxColorRange{minimum, maximum});
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeSetUiText(const std::uint64_t world,
                                                                    const std::uint64_t entityHigh,
                                                                    const std::uint64_t entityLow,
                                                                    const Coral::String text) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->SetManagedUiText(
                                 entity.Id().Value(), static_cast<std::string>(text))
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeConsumeUiClick(const std::uint64_t world,
                                                                         const std::uint64_t entityHigh,
                                                                         const std::uint64_t entityLow) noexcept
    {
        if (!CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto entity = ResolveRuntimeEntity(world, entityHigh, entityLow);
            return entity && CurrentRuntime->Specification.RuntimeServices->ConsumeManagedUiClick(entity.Id().Value())
                       ? 1
                       : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] std::uint8_t ScriptSystem::Impl::RuntimeRaycast(const std::uint64_t world, const Vector3 origin,
                                                                  const Vector3 direction, const float maximumDistance,
                                                                  const std::uint32_t mask,
                                                                  const std::uint64_t ignoredHigh,
                                                                  const std::uint64_t ignoredLow,
                                                                  RuntimeRaycastResult* result) noexcept
    {
        if (!result || !CurrentRuntime || !CurrentRuntime->Specification.RuntimeServices)
            return 0;
        try
        {
            const auto hit = CurrentRuntime->Specification.RuntimeServices->RaycastManaged(
                {.World = world,
                 .Origin = origin,
                 .Direction = direction,
                 .MaximumDistance = maximumDistance,
                 .Mask = mask,
                 .IgnoredEntity = AssetId(ignoredHigh, ignoredLow),
                 .IncludeTriggers = false});
            if (!hit)
                return 0;
            result->EntityHigh = hit->Entity.High();
            result->EntityLow = hit->Entity.Low();
            result->Point = hit->Point;
            result->Normal = hit->Normal;
            result->Distance = hit->Distance;
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }
} // namespace Keire
