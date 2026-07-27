#pragma once

#include "Keire/ECS/Component.h"

#include <string>

namespace Keire
{
    class KEIRE_API AudioSourceComponent final : public Component
    {
      public:
        AudioSourceComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245415544ULL, 0x494f535243000001ULL));
        }

        [[nodiscard]] AssetId Clip() const noexcept { return m_Clip; }
        [[nodiscard]] const std::string& Bus() const noexcept { return m_Bus; }
        [[nodiscard]] float Gain() const noexcept { return m_Gain; }
        [[nodiscard]] float Pitch() const noexcept { return m_Pitch; }
        [[nodiscard]] std::uint32_t Priority() const noexcept { return m_Priority; }
        [[nodiscard]] float MinimumDistance() const noexcept { return m_MinimumDistance; }
        [[nodiscard]] float MaximumDistance() const noexcept { return m_MaximumDistance; }
        [[nodiscard]] bool Loop() const noexcept { return m_Loop; }
        [[nodiscard]] bool Spatial() const noexcept { return m_Spatial; }
        [[nodiscard]] bool PlayOnAwake() const noexcept { return m_PlayOnAwake; }

        void SetClip(AssetId value);
        void SetBus(std::string value);
        void SetGain(float value);
        void SetPitch(float value);
        void SetPriority(std::uint32_t value);
        void SetMinimumDistance(float value);
        void SetMaximumDistance(float value);
        void SetLoop(bool value);
        void SetSpatial(bool value);
        void SetPlayOnAwake(bool value);

      private:
        friend ComponentRegistration CreateAudioSourceComponentRegistration();
        AssetId m_Clip;
        std::string m_Bus = "SFX";
        float m_Gain = 1.0F;
        float m_Pitch = 1.0F;
        std::uint32_t m_Priority = 128;
        float m_MinimumDistance = 1.0F;
        float m_MaximumDistance = 100.0F;
        bool m_Loop = false;
        bool m_Spatial = true;
        bool m_PlayOnAwake = true;
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
    [[nodiscard]] KEIRE_API ComponentRegistration CreateAudioListenerComponentRegistration();
} // namespace Keire
