#include "Keire/ECS/Components/AudioComponents.h"

#include "Keire/Audio/AudioAssets.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadAudioProperty(const ComponentPropertyBag& values, const std::string_view key,
                                          const T& fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Audio component property has an incompatible type.");
        }

        [[nodiscard]] AssetId ReadLocalId(const ComponentPropertyBag& values, const std::string_view key)
        {
            const auto value = ReadAudioProperty(values, key, std::string{});
            return value.empty() ? AssetId{} : AssetId::Parse(value);
        }

        [[nodiscard]] bool ValidAttenuation(const Curve1D& curve) noexcept
        {
            const auto keys = curve.Keys();
            return !keys.empty() && keys.front().Time >= 0.0F && keys.back().Time <= 1.0F &&
                   std::ranges::all_of(keys,
                                       [](const CurveKey& key) { return key.Value >= 0.0F && key.Value <= 1.0F; });
        }
    } // namespace

    AudioSourceComponent::AudioSourceComponent() : Component(StaticType()) {}

    AudioSourceComponentState AudioSourceComponent::CaptureState() const
    {
        return {
            .Clip = m_Clip,
            .Mixer = m_Mixer,
            .BusId = m_BusId,
            .Bus = m_Bus,
            .Gain = m_Gain,
            .Pitch = m_Pitch,
            .Priority = m_Priority,
            .MinimumDistance = m_MinimumDistance,
            .MaximumDistance = m_MaximumDistance,
            .Attenuation = m_Attenuation,
            .Loop = m_Loop,
            .Spatial = m_Spatial,
            .PlayOnAwake = m_PlayOnAwake,
        };
    }

    void AudioSourceComponent::ValidateState(const AudioSourceComponentState& state)
    {
        if (state.Bus.empty() || state.Bus.size() > 128)
            throw std::invalid_argument("Audio Source bus name must contain between 1 and 128 bytes.");
        if (!std::isfinite(state.Gain) || state.Gain < 0.0F || state.Gain > 16.0F)
            throw std::invalid_argument("Audio Source gain must be between zero and sixteen.");
        if (!std::isfinite(state.Pitch) || state.Pitch <= 0.01F || state.Pitch > 8.0F)
            throw std::invalid_argument("Audio Source pitch must be greater than 0.01 and at most eight.");
        if (state.Priority > 255)
            throw std::invalid_argument("Audio Source priority must be between zero and 255.");
        if (!std::isfinite(state.MinimumDistance) || !std::isfinite(state.MaximumDistance) ||
            state.MinimumDistance < 0.0F || state.MaximumDistance <= state.MinimumDistance)
            throw std::invalid_argument("Audio Source maximum distance must exceed its non-negative minimum distance.");
        if (!ValidAttenuation(state.Attenuation))
            throw std::invalid_argument("Audio Source attenuation curve must cover normalized, non-negative gain.");
    }

    void AudioSourceComponent::ApplyState(AudioSourceComponentState state)
    {
        ValidateState(state);
        m_Clip = state.Clip;
        m_Mixer = state.Mixer;
        m_BusId = state.BusId;
        m_Bus = std::move(state.Bus);
        m_Gain = state.Gain;
        m_Pitch = state.Pitch;
        m_Priority = state.Priority;
        m_MinimumDistance = state.MinimumDistance;
        m_MaximumDistance = state.MaximumDistance;
        m_Attenuation = std::move(state.Attenuation);
        m_Loop = state.Loop;
        m_Spatial = state.Spatial;
        m_PlayOnAwake = state.PlayOnAwake;
        NotifyChanged();
    }

    void AudioSourceComponent::SetClip(const AssetId value)
    {
        m_Clip = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetMixer(const AssetId value)
    {
        m_Mixer = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetBusId(const AssetId value)
    {
        m_BusId = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetBus(std::string value)
    {
        if (value.empty() || value.size() > 128)
            throw std::invalid_argument("Audio Source bus name must contain between 1 and 128 bytes.");
        m_Bus = std::move(value);
        NotifyChanged();
    }

    void AudioSourceComponent::SetGain(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 16.0F)
            throw std::invalid_argument("Audio Source gain must be between zero and sixteen.");
        m_Gain = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetPitch(const float value)
    {
        if (!std::isfinite(value) || value <= 0.01F || value > 8.0F)
            throw std::invalid_argument("Audio Source pitch must be greater than 0.01 and at most eight.");
        m_Pitch = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetPriority(const std::uint32_t value)
    {
        if (value > 255)
            throw std::invalid_argument("Audio Source priority must be between zero and 255.");
        m_Priority = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetMinimumDistance(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value >= m_MaximumDistance)
            throw std::invalid_argument("Audio Source minimum distance must be below its maximum distance.");
        m_MinimumDistance = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetMaximumDistance(const float value)
    {
        if (!std::isfinite(value) || value <= m_MinimumDistance)
            throw std::invalid_argument("Audio Source maximum distance must exceed its minimum distance.");
        m_MaximumDistance = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetAttenuation(Curve1D value)
    {
        if (!ValidAttenuation(value))
            throw std::invalid_argument("Audio Source attenuation curve must cover normalized, non-negative gain.");
        m_Attenuation = std::move(value);
        NotifyChanged();
    }

    void AudioSourceComponent::SetLoop(const bool value)
    {
        m_Loop = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetSpatial(const bool value)
    {
        m_Spatial = value;
        NotifyChanged();
    }

    void AudioSourceComponent::SetPlayOnAwake(const bool value)
    {
        m_PlayOnAwake = value;
        NotifyChanged();
    }

    AudioPlaybackRequest AudioSourceComponent::PlaybackRequest(std::shared_ptr<const AudioClipData> clip,
                                                               const Vector3 position) const
    {
        AudioPlaybackRequest result;
        result.Clip = std::move(clip);
        result.Mixer = m_Mixer;
        result.BusId = m_BusId;
        result.Bus = m_Bus;
        result.Gain = m_Gain;
        result.Pitch = m_Pitch;
        result.Priority = m_Priority;
        result.Loop = m_Loop;
        result.Spatial = m_Spatial;
        result.Position = position;
        result.MinimumDistance = m_MinimumDistance;
        result.MaximumDistance = m_MaximumDistance;
        result.Attenuation = m_Attenuation;
        return result;
    }

    AudioReverbZoneComponent::AudioReverbZoneComponent() : Component(StaticType()) {}

    void AudioReverbZoneComponent::SetMixer(const AssetId value)
    {
        m_Mixer = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetSnapshotId(const AssetId value)
    {
        m_SnapshotId = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetShape(const AudioReverbZoneShape value)
    {
        if (value != AudioReverbZoneShape::Box && value != AudioReverbZoneShape::Sphere)
            throw std::invalid_argument("Audio Reverb Zone shape is unsupported.");
        m_Shape = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetBoxHalfExtent(const Vector3 value)
    {
        if (!Math::IsFinite(value) || value.X <= 0.0F || value.Y <= 0.0F || value.Z <= 0.0F || value.X > 100000.0F ||
            value.Y > 100000.0F || value.Z > 100000.0F)
            throw std::invalid_argument("Audio Reverb Zone box half extent must be positive and finite.");
        m_BoxHalfExtent = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetSphereRadius(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F || value > 100000.0F)
            throw std::invalid_argument("Audio Reverb Zone sphere radius must be positive and finite.");
        m_SphereRadius = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetPriority(const std::int32_t value)
    {
        if (value < -32768 || value > 32767)
            throw std::invalid_argument("Audio Reverb Zone priority must fit a signed 16-bit range.");
        m_Priority = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetBlendDistance(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 100000.0F)
            throw std::invalid_argument("Audio Reverb Zone blend distance must be finite and non-negative.");
        m_BlendDistance = value;
        NotifyChanged();
    }

    void AudioReverbZoneComponent::SetReverbSend(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Audio Reverb Zone send must be between zero and one.");
        m_ReverbSend = value;
        NotifyChanged();
    }

    AudioListenerComponent::AudioListenerComponent() : Component(StaticType()) {}

    void AudioListenerComponent::SetPrimary(const bool value)
    {
        m_Primary = value;
        NotifyChanged();
    }

    void AudioListenerComponent::SetGain(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 16.0F)
            throw std::invalid_argument("Audio Listener gain must be between zero and sixteen.");
        m_Gain = value;
        NotifyChanged();
    }

    ComponentRegistration CreateAudioSourceComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = AudioSourceComponent::StaticType();
        result.Name = "Audio Source";
        result.Category = "Audio";
        result.SchemaVersion = 2;
        result.Properties = {
            {"clip",
             "Audio Clip",
             "Source",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             AudioClipAsset::StaticType()},
            {"mixer",
             "Mixer",
             "Mixing",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             AudioMixerAsset::StaticType()},
            {"busId",
             "Stable Bus ID",
             "Mixing",
             ComponentPropertyKind::Text,
             false,
             {},
             {},
             0.1,
             {},
             "Stable bus identity used by mixer assets; the legacy bus name remains the runtime fallback."},
            {"bus", "Bus", "Mixing", ComponentPropertyKind::Text},
            {"gain", "Gain", "Mixing", ComponentPropertyKind::Scalar, false, 0.0, 16.0, 0.01},
            {"pitch", "Pitch", "Mixing", ComponentPropertyKind::Scalar, false, 0.01, 8.0, 0.01},
            {"priority", "Priority", "Mixing", ComponentPropertyKind::Integer, false, 0.0, 255.0, 1.0},
            {"minimumDistance", "Minimum Distance", "Spatial", ComponentPropertyKind::Scalar, false, 0.0, 100000.0,
             0.1},
            {"maximumDistance", "Maximum Distance", "Spatial", ComponentPropertyKind::Scalar, false, 0.01, 100000.0,
             0.1},
            {"attenuation",
             "Attenuation Curve",
             "Spatial",
             ComponentPropertyKind::Curve,
             false,
             {},
             {},
             0.01,
             {},
             "Normalized distance curve multiplied with the legacy distance attenuation."},
            {"loop", "Loop", "Playback", ComponentPropertyKind::Boolean},
            {"spatial", "Spatial", "Playback", ComponentPropertyKind::Boolean},
            {"playOnAwake", "Play On Awake", "Playback", ComponentPropertyKind::Boolean},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<AudioSourceComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& source = dynamic_cast<const AudioSourceComponent&>(component);
            return ComponentPropertyBag{
                {"clip", source.m_Clip},
                {"mixer", source.m_Mixer},
                {"busId", source.m_BusId ? source.m_BusId.ToString() : std::string{}},
                {"bus", source.m_Bus},
                {"gain", static_cast<double>(source.m_Gain)},
                {"pitch", static_cast<double>(source.m_Pitch)},
                {"priority", static_cast<std::int64_t>(source.m_Priority)},
                {"minimumDistance", static_cast<double>(source.m_MinimumDistance)},
                {"maximumDistance", static_cast<double>(source.m_MaximumDistance)},
                {"attenuation", source.m_Attenuation},
                {"loop", source.m_Loop},
                {"spatial", source.m_Spatial},
                {"playOnAwake", source.m_PlayOnAwake},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 2)
                throw std::invalid_argument("Unsupported Audio Source component schema version.");
            auto& source = dynamic_cast<AudioSourceComponent&>(component);
            source.SetClip(ReadAudioProperty(values, "clip", AssetId{}));
            source.SetMixer(ReadAudioProperty(values, "mixer", AssetId{}));
            source.SetBusId(ReadLocalId(values, "busId"));
            source.SetBus(ReadAudioProperty(values, "bus", std::string{"SFX"}));
            source.SetGain(static_cast<float>(ReadAudioProperty(values, "gain", 1.0)));
            source.SetPitch(static_cast<float>(ReadAudioProperty(values, "pitch", 1.0)));
            const auto priority = ReadAudioProperty(values, "priority", std::int64_t{128});
            if (priority < 0 || priority > 255)
                throw std::invalid_argument("Audio Source priority is outside the supported range.");
            source.SetPriority(static_cast<std::uint32_t>(priority));
            const auto minimum = static_cast<float>(ReadAudioProperty(values, "minimumDistance", 1.0));
            const auto maximum = static_cast<float>(ReadAudioProperty(values, "maximumDistance", 100.0));
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0.0F || maximum <= minimum)
                throw std::invalid_argument("Audio Source attenuation range is invalid.");
            source.m_MinimumDistance = minimum;
            source.m_MaximumDistance = maximum;
            source.SetAttenuation(ReadAudioProperty(values, "attenuation", Curve1D::Constant(1.0F)));
            source.SetLoop(ReadAudioProperty(values, "loop", false));
            source.SetSpatial(ReadAudioProperty(values, "spatial", true));
            source.SetPlayOnAwake(ReadAudioProperty(values, "playOnAwake", true));
        };
        result.Migrate = [](ComponentPropertyBag values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Audio Source component schema migration.");
            values.insert_or_assign("mixer", AssetId{});
            values.insert_or_assign("busId", std::string{});
            values.insert_or_assign("attenuation", Curve1D::Constant(1.0F));
            return values;
        };
        return result;
    }

    ComponentRegistration CreateAudioReverbZoneComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = AudioReverbZoneComponent::StaticType();
        result.Name = "Audio Reverb Zone";
        result.Category = "Audio";
        result.Properties = {
            {"mixer",
             "Mixer",
             "Reverb",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             AudioMixerAsset::StaticType()},
            {"snapshotId",
             "Snapshot Stable ID",
             "Reverb",
             ComponentPropertyKind::Text,
             false,
             {},
             {},
             0.1,
             {},
             "Stable local snapshot identity from the selected mixer."},
            {"shape", "Shape", "Volume", ComponentPropertyKind::Integer, false, 0.0, 1.0, 1.0},
            {"boxHalfExtent", "Box Half Extent", "Volume", ComponentPropertyKind::Vector3},
            {"sphereRadius", "Sphere Radius", "Volume", ComponentPropertyKind::Scalar, false, 0.01, 100000.0, 0.1},
            {"priority", "Priority", "Blending", ComponentPropertyKind::Integer, false, -32768.0, 32767.0, 1.0},
            {"blendDistance", "Blend Distance", "Blending", ComponentPropertyKind::Scalar, false, 0.0, 100000.0, 0.1},
            {"reverbSend", "Reverb Send", "Blending", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<AudioReverbZoneComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& zone = dynamic_cast<const AudioReverbZoneComponent&>(component);
            return ComponentPropertyBag{
                {"mixer", zone.m_Mixer},
                {"snapshotId", zone.m_SnapshotId ? zone.m_SnapshotId.ToString() : std::string{}},
                {"shape", static_cast<std::int64_t>(zone.m_Shape)},
                {"boxHalfExtent", zone.m_BoxHalfExtent},
                {"sphereRadius", static_cast<double>(zone.m_SphereRadius)},
                {"priority", static_cast<std::int64_t>(zone.m_Priority)},
                {"blendDistance", static_cast<double>(zone.m_BlendDistance)},
                {"reverbSend", static_cast<double>(zone.m_ReverbSend)},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Audio Reverb Zone component schema version.");
            auto& zone = dynamic_cast<AudioReverbZoneComponent&>(component);
            zone.SetMixer(ReadAudioProperty(values, "mixer", AssetId{}));
            zone.SetSnapshotId(ReadLocalId(values, "snapshotId"));
            const auto shape = ReadAudioProperty(values, "shape", std::int64_t{0});
            if (shape < 0 || shape > 1)
                throw std::invalid_argument("Audio Reverb Zone shape is outside the supported range.");
            zone.SetShape(static_cast<AudioReverbZoneShape>(shape));
            zone.SetBoxHalfExtent(ReadAudioProperty(values, "boxHalfExtent", Vector3{5.0F, 3.0F, 5.0F}));
            zone.SetSphereRadius(static_cast<float>(ReadAudioProperty(values, "sphereRadius", 5.0)));
            const auto priority = ReadAudioProperty(values, "priority", std::int64_t{0});
            if (priority < -32768 || priority > 32767)
                throw std::invalid_argument("Audio Reverb Zone priority is outside the supported range.");
            zone.SetPriority(static_cast<std::int32_t>(priority));
            zone.SetBlendDistance(static_cast<float>(ReadAudioProperty(values, "blendDistance", 1.0)));
            zone.SetReverbSend(static_cast<float>(ReadAudioProperty(values, "reverbSend", 1.0)));
        };
        return result;
    }

    ComponentRegistration CreateAudioListenerComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = AudioListenerComponent::StaticType();
        result.Name = "Audio Listener";
        result.Category = "Audio";
        result.Properties = {
            {"primary", "Primary Listener", "Listener", ComponentPropertyKind::Boolean},
            {"gain", "Gain", "Listener", ComponentPropertyKind::Scalar, false, 0.0, 16.0, 0.01},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<AudioListenerComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& listener = dynamic_cast<const AudioListenerComponent&>(component);
            return ComponentPropertyBag{
                {"primary", listener.m_Primary},
                {"gain", static_cast<double>(listener.m_Gain)},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Audio Listener component schema version.");
            auto& listener = dynamic_cast<AudioListenerComponent&>(component);
            listener.SetPrimary(ReadAudioProperty(values, "primary", true));
            listener.SetGain(static_cast<float>(ReadAudioProperty(values, "gain", 1.0)));
        };
        return result;
    }
} // namespace Keire
