#pragma once
#include "Keire/Audio/AudioAssets.h"
#include "Keire/Core.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include <string>

namespace KeireEditor
{
    // Call-scoped views of the component card's resolved metadata; never retained by property drawers.
    struct ReflectedComponentState final
    {
        const Keire::Ref<Keire::Component>& Component;
        const Keire::ComponentRegistration& Registration;
        const Keire::Ref<Keire::VfxEmitterComponent>& VfxEmitter;
        const Keire::Ref<const Keire::VfxEffectAsset>& VfxEffect;
        const std::string& VfxDiagnostic;
        bool VfxDiagnosticWarning;
        const Keire::Ref<Keire::AudioSourceComponent>& AudioSource;
        const Keire::Ref<Keire::AudioReverbZoneComponent>& ReverbZone;
        const Keire::Ref<Keire::ColliderComponent>& Collider;
        Keire::AssetId ResolvedMixer;
        const Keire::Ref<const Keire::AudioMixerAsset>& MixerAsset;
    };
} // namespace KeireEditor
