#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Scripting/ScriptSystem.h"

#include <cstddef>
#include <cstdint>

namespace Keire::Detail
{
    inline constexpr std::size_t ManagedCallbackProfileCount =
        static_cast<std::size_t>(ManagedBehaviourCallback::AnimatorIk) + 1;

    struct NativeAnimatorState final
    {
        float Speed = 1.0F;
        float NormalizedTime = 0.0F;
        std::uint8_t Playing = 0;
        std::uint8_t Paused = 0;
    };

    struct NativeAudioSourceProperties final
    {
        std::uint64_t ClipHigh = 0;
        std::uint64_t ClipLow = 0;
        std::uint64_t MixerHigh = 0;
        std::uint64_t MixerLow = 0;
        std::uint64_t BusHigh = 0;
        std::uint64_t BusLow = 0;
        float Gain = 1.0F;
        float Pitch = 1.0F;
        float PositionSeconds = 0.0F;
        float DurationSeconds = 0.0F;
        float MinimumDistance = 1.0F;
        float MaximumDistance = 100.0F;
        std::uint32_t Priority = 128;
        std::uint8_t Loop = 0;
        std::uint8_t Spatial = 0;
        std::uint8_t PlayOnAwake = 0;
        std::uint8_t PlaybackState = 0;
    };

    struct NativeAudioListenerProperties final
    {
        float Gain = 1.0F;
        std::uint8_t Primary = 1;
    };

    struct NativeAudioReverbZoneProperties final
    {
        std::uint64_t MixerHigh = 0;
        std::uint64_t MixerLow = 0;
        std::uint64_t SnapshotHigh = 0;
        std::uint64_t SnapshotLow = 0;
        Vector3 BoxHalfExtent{5.0F, 3.0F, 5.0F};
        float SphereRadius = 5.0F;
        float BlendDistance = 1.0F;
        float ReverbSend = 1.0F;
        std::int32_t Priority = 0;
        std::uint8_t Shape = 0;
    };

    struct ManagedCallbackProfile final
    {
        std::uint64_t Invocations = 0;
        std::uint64_t SkippedInvocations = 0;
        double Milliseconds = 0.0;
        double MaximumMilliseconds = 0.0;
    };
} // namespace Keire::Detail
