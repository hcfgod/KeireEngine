#include "Keire/ECS/Components/AudioComponents.h"

#include "Keire/Audio/AudioAssets.h"

#include <cmath>
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
    } // namespace

    AudioSourceComponent::AudioSourceComponent() : Component(StaticType()) {}

    void AudioSourceComponent::SetClip(const AssetId value)
    {
        m_Clip = value;
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
            {"bus", "Bus", "Mixing", ComponentPropertyKind::Text},
            {"gain", "Gain", "Mixing", ComponentPropertyKind::Scalar, false, 0.0, 16.0, 0.01},
            {"pitch", "Pitch", "Mixing", ComponentPropertyKind::Scalar, false, 0.01, 8.0, 0.01},
            {"priority", "Priority", "Mixing", ComponentPropertyKind::Integer, false, 0.0, 255.0, 1.0},
            {"minimumDistance", "Minimum Distance", "Spatial", ComponentPropertyKind::Scalar, false, 0.0, 100000.0,
             0.1},
            {"maximumDistance", "Maximum Distance", "Spatial", ComponentPropertyKind::Scalar, false, 0.01, 100000.0,
             0.1},
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
                {"bus", source.m_Bus},
                {"gain", static_cast<double>(source.m_Gain)},
                {"pitch", static_cast<double>(source.m_Pitch)},
                {"priority", static_cast<std::int64_t>(source.m_Priority)},
                {"minimumDistance", static_cast<double>(source.m_MinimumDistance)},
                {"maximumDistance", static_cast<double>(source.m_MaximumDistance)},
                {"loop", source.m_Loop},
                {"spatial", source.m_Spatial},
                {"playOnAwake", source.m_PlayOnAwake},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Audio Source component schema version.");
            auto& source = dynamic_cast<AudioSourceComponent&>(component);
            source.SetClip(ReadAudioProperty(values, "clip", AssetId{}));
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
            source.SetLoop(ReadAudioProperty(values, "loop", false));
            source.SetSpatial(ReadAudioProperty(values, "spatial", true));
            source.SetPlayOnAwake(ReadAudioProperty(values, "playOnAwake", true));
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
