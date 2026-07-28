#pragma once

#include "Keire/Audio/AudioSystem.h"
#include "Keire/ECS/Component.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Keire
{
    struct AudioSourceComponentState
    {
        AssetId Clip;
        AssetId Mixer;
        AssetId BusId;
        std::string Bus = "SFX";
        float Gain = 1.0F;
        float Pitch = 1.0F;
        std::uint32_t Priority = 128;
        float MinimumDistance = 1.0F;
        float MaximumDistance = 100.0F;
        Curve1D Attenuation = Curve1D::Constant(1.0F);
        bool Loop = false;
        bool Spatial = true;
        bool PlayOnAwake = true;

        [[nodiscard]] bool operator==(const AudioSourceComponentState&) const = default;
    };

    class KEIRE_API AudioSourceComponent final : public Component
    {
      public:
        AudioSourceComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245415544ULL, 0x494f535243000001ULL));
        }

        [[nodiscard]] AssetId Clip() const noexcept { return m_Clip; }
        [[nodiscard]] AssetId Mixer() const noexcept { return m_Mixer; }
        [[nodiscard]] AssetId BusId() const noexcept { return m_BusId; }
        [[nodiscard]] const std::string& Bus() const noexcept { return m_Bus; }
        [[nodiscard]] float Gain() const noexcept { return m_Gain; }
        [[nodiscard]] float Pitch() const noexcept { return m_Pitch; }
        [[nodiscard]] std::uint32_t Priority() const noexcept { return m_Priority; }
        [[nodiscard]] float MinimumDistance() const noexcept { return m_MinimumDistance; }
        [[nodiscard]] float MaximumDistance() const noexcept { return m_MaximumDistance; }
        [[nodiscard]] const Curve1D& Attenuation() const noexcept { return m_Attenuation; }
        [[nodiscard]] bool Loop() const noexcept { return m_Loop; }
        [[nodiscard]] bool Spatial() const noexcept { return m_Spatial; }
        [[nodiscard]] bool PlayOnAwake() const noexcept { return m_PlayOnAwake; }
        [[nodiscard]] AudioSourceComponentState CaptureState() const;

        static void ValidateState(const AudioSourceComponentState& state);
        void ApplyState(AudioSourceComponentState state);
        void SetClip(AssetId value);
        void SetMixer(AssetId value);
        void SetBusId(AssetId value);
        void SetBus(std::string value);
        void SetGain(float value);
        void SetPitch(float value);
        void SetPriority(std::uint32_t value);
        void SetMinimumDistance(float value);
        void SetMaximumDistance(float value);
        void SetAttenuation(Curve1D value);
        void SetLoop(bool value);
        void SetSpatial(bool value);
        void SetPlayOnAwake(bool value);
        [[nodiscard]] AudioPlaybackRequest PlaybackRequest(std::shared_ptr<const AudioClipData> clip,
                                                           Vector3 position = {}) const;

      private:
        friend ComponentRegistration CreateAudioSourceComponentRegistration();
        AssetId m_Clip;
        AssetId m_Mixer;
        AssetId m_BusId;
        std::string m_Bus = "SFX";
        float m_Gain = 1.0F;
        float m_Pitch = 1.0F;
        std::uint32_t m_Priority = 128;
        float m_MinimumDistance = 1.0F;
        float m_MaximumDistance = 100.0F;
        Curve1D m_Attenuation = Curve1D::Constant(1.0F);
        bool m_Loop = false;
        bool m_Spatial = true;
        bool m_PlayOnAwake = true;
    };

    enum class AudioReverbZoneShape : std::uint8_t
    {
        Box,
        Sphere
    };

    class KEIRE_API AudioReverbZoneComponent final : public Component
    {
      public:
        AudioReverbZoneComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245415544ULL, 0x494f52565a4f0001ULL));
        }

        [[nodiscard]] AssetId Mixer() const noexcept { return m_Mixer; }
        [[nodiscard]] AssetId SnapshotId() const noexcept { return m_SnapshotId; }
        [[nodiscard]] AudioReverbZoneShape Shape() const noexcept { return m_Shape; }
        [[nodiscard]] Vector3 BoxHalfExtent() const noexcept { return m_BoxHalfExtent; }
        [[nodiscard]] float SphereRadius() const noexcept { return m_SphereRadius; }
        [[nodiscard]] std::int32_t Priority() const noexcept { return m_Priority; }
        [[nodiscard]] float BlendDistance() const noexcept { return m_BlendDistance; }
        [[nodiscard]] float ReverbSend() const noexcept { return m_ReverbSend; }

        void SetMixer(AssetId value);
        void SetSnapshotId(AssetId value);
        void SetShape(AudioReverbZoneShape value);
        void SetBoxHalfExtent(Vector3 value);
        void SetSphereRadius(float value);
        void SetPriority(std::int32_t value);
        void SetBlendDistance(float value);
        void SetReverbSend(float value);

      private:
        friend ComponentRegistration CreateAudioReverbZoneComponentRegistration();
        AssetId m_Mixer;
        AssetId m_SnapshotId;
        AudioReverbZoneShape m_Shape = AudioReverbZoneShape::Box;
        Vector3 m_BoxHalfExtent{5.0F, 3.0F, 5.0F};
        float m_SphereRadius = 5.0F;
        std::int32_t m_Priority = 0;
        float m_BlendDistance = 1.0F;
        float m_ReverbSend = 1.0F;
    };

    class KEIRE_API AudioListenerComponent final : public Component
    {
      public:
        AudioListenerComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245415544ULL, 0x494f4c4953540001ULL));
        }

        [[nodiscard]] bool Primary() const noexcept { return m_Primary; }
        [[nodiscard]] float Gain() const noexcept { return m_Gain; }

        void SetPrimary(bool value);
        void SetGain(float value);

      private:
        friend ComponentRegistration CreateAudioListenerComponentRegistration();
        bool m_Primary = true;
        float m_Gain = 1.0F;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateAudioSourceComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateAudioReverbZoneComponentRegistration();
    [[nodiscard]] KEIRE_API ComponentRegistration CreateAudioListenerComponentRegistration();
} // namespace Keire
