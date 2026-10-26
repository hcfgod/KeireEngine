#include "KeireClient/Editor/AnimatorInspectorDiagnostic.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/InspectorPropertyVisibility.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/UiBuilderInspector.h"
#include "KeireClient/Editor/VfxEmitterInspector.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"
#include "KeireClientInternal/Editor/InspectorReflectedComponentState.h"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace KeireEditor
{
    void
    InspectorPanel::EntityDrawContext::DrawReflectedComponent(const Keire::Ref<Keire::Component>& displayedComponent,
                                                              const bool mixedComponentValues)
    {
        const auto resolveManagedType = [this](const Keire::AssetId asset)
        { return m_Panel.m_Controller.InspectorManagedDataType(asset); };

        InspectorPropertyEditor propertyEditor(m_Ui, m_Records, m_Assets, m_Scene, *m_Panel.m_AssetPicker,
                                               m_ManagedAssetTypes, resolveManagedType, &m_Panel.m_ManagedGraphFocus);
        for (const auto& component : m_Entity.GetComponents())
        {
            if (component != displayedComponent)
            {
                continue;
            }
            if (!component || component->Type() == Keire::TransformComponent::StaticType() ||
                component->Type() == Keire::CameraComponent::StaticType() ||
                component->Type() == Keire::DirectionalLightComponent::StaticType() ||
                component->Type() == Keire::MeshRendererComponent::StaticType())
            {
                continue;
            }
            const auto registration = m_Scene->Components()->Find(component->Type());
            if (!registration)
            {
                continue;
            }
            auto vfxEmitter = Keire::Ref<Keire::VfxEmitterComponent>{};
            auto vfxEffect = Keire::Ref<const Keire::VfxEffectAsset>{};
            std::string vfxEffectDiagnostic;
            bool vfxEffectDiagnosticWarning = false;
            if (registration->Type == Keire::VfxEmitterComponent::StaticType())
            {
                vfxEmitter = Keire::DynamicRefCast<Keire::VfxEmitterComponent>(component);
                if (!vfxEmitter)
                {
                    vfxEffectDiagnostic = "The VFX Emitter registration returned an incompatible component.";
                    vfxEffectDiagnosticWarning = true;
                }
                else if (!vfxEmitter->Effect())
                {
                    vfxEffectDiagnostic = "Assign a VFX effect to edit exposed Blackboard parameters.";
                }
                else if (!m_Assets)
                {
                    vfxEffectDiagnostic = "The asset system is unavailable; serialized overrides are preserved.";
                    vfxEffectDiagnosticWarning = true;
                }
                else
                {
                    try
                    {
                        vfxEffect =
                            m_Assets->Load<Keire::VfxEffectAsset>(vfxEmitter->Effect(), Keire::AssetPriority::High)
                                .TryGetLoaded();
                        if (!vfxEffect)
                        {
                            vfxEffectDiagnostic = "Loading VFX Blackboard metadata...";
                        }
                    }
                    catch (const std::exception& error)
                    {
                        vfxEffectDiagnostic = std::string("VFX Blackboard metadata is unavailable: ") + error.what();
                        vfxEffectDiagnosticWarning = true;
                    }
                }
            }
            const auto audioSource = registration->Type == Keire::AudioSourceComponent::StaticType()
                                         ? Keire::DynamicRefCast<Keire::AudioSourceComponent>(component)
                                         : Keire::Ref<Keire::AudioSourceComponent>{};
            const auto reverbZone = registration->Type == Keire::AudioReverbZoneComponent::StaticType()
                                        ? Keire::DynamicRefCast<Keire::AudioReverbZoneComponent>(component)
                                        : Keire::Ref<Keire::AudioReverbZoneComponent>{};
            const auto collider = registration->Type == Keire::ColliderComponent::StaticType()
                                      ? Keire::DynamicRefCast<Keire::ColliderComponent>(component)
                                      : Keire::Ref<Keire::ColliderComponent>{};
            const auto animator = registration->Type == Keire::AnimatorComponent::StaticType()
                                      ? Keire::DynamicRefCast<Keire::AnimatorComponent>(component)
                                      : Keire::Ref<Keire::AnimatorComponent>{};
            const auto explicitMixer = audioSource  ? audioSource->Mixer()
                                       : reverbZone ? reverbZone->Mixer()
                                                    : Keire::AssetId{};
            const auto resolvedMixer =
                explicitMixer ? explicitMixer : m_Panel.m_Controller.InspectorDefaultAudioMixer();
            auto mixerAsset = Keire::Ref<const Keire::AudioMixerAsset>{};
            if (m_Assets && resolvedMixer)
            {
                try
                {
                    mixerAsset = m_Assets->Load<Keire::AudioMixerAsset>(resolvedMixer, Keire::AssetPriority::High)
                                     .TryGetLoaded();
                }
                catch (...)
                {
                    // The component card reports loading and validation state below without interrupting
                    // editing.
                }
            }
            m_Ui.Spacing();
            const auto ordinal = ComponentOrdinal(m_ComponentOrder, component);
            const auto componentKey = registration->Type.ToString() + "." + std::to_string(ordinal);
            auto& expanded = Expansion(componentKey);
            const auto cardId = "ComponentCard##" + componentKey;
            const float anchorPickerHeight = 0.0F;
            std::size_t groupRows = 0;
            std::size_t headerRows = 0;
            std::size_t additionalTextRows = 0;
            std::size_t visiblePropertyRows = 0;
            std::size_t stackedPropertyRows = 0;
            std::string_view previousGroup;
            for (const auto& property : registration->Properties)
            {
                if (!IsInspectorPropertyVisible(registration->Type, property.Key))
                {
                    continue;
                }
                if (collider && !ColliderPropertyVisible(collider->Shape(), property.Key))
                {
                    continue;
                }
                ++visiblePropertyRows;
                if (property.Kind != Keire::ComponentPropertyKind::Boolean)
                {
                    ++stackedPropertyRows;
                }
                if (!property.Group.empty() && property.Group != previousGroup)
                {
                    ++groupRows;
                    previousGroup = property.Group;
                }
                if (!property.Header.empty())
                {
                    ++headerRows;
                }
                if (property.TextLines > 1)
                {
                    additionalTextRows += property.TextLines - 1;
                }
            }
            const auto vfxEntries = vfxEffect ? KeireEditor::VfxEmitterInspector::VisibleEntryCount(
                                                    vfxEffect->Definition(), vfxEmitter->ParameterOverrides())
                                              : std::size_t{0};
            const float vfxInspectorHeight =
                vfxEmitter ? std::min(720.0F, 26.0F + static_cast<float>(std::max<std::size_t>(vfxEntries, 1U)) * 48.0F)
                           : 0.0F;
            const float audioSetupHeight = registration->Type == Keire::AudioSourceComponent::StaticType() ||
                                                   registration->Type == Keire::AudioReverbZoneComponent::StaticType()
                                               ? 72.0F
                                               : 0.0F;
            const float animatorDiagnosticsHeight =
                registration->Type == Keire::AnimatorComponent::StaticType()
                    ? (animator && animator->PoseSource() == Keire::AnimatorPoseSource::ProceduralHumanoid ? 196.0F
                                                                                                           : 58.0F)
                    : 0.0F;
            const float uiDocumentActionsHeight = UiDocumentInspectorActionsHeight(component);
            const bool compactFields = m_Ui.ContentAvailable().Width < KeireEditor::Detail::InspectorCompactFieldWidth;
            const float compactFieldHeight = compactFields ? static_cast<float>(stackedPropertyRows) * 12.0F : 0.0F;
            const float cardHeight =
                expanded
                    ? std::max(115.0F, 80.0F + anchorPickerHeight + static_cast<float>(visiblePropertyRows) * 34.0F +
                                           static_cast<float>(groupRows + headerRows) * 22.0F +
                                           static_cast<float>(additionalTextRows) * 20.0F + compactFieldHeight +
                                           vfxInspectorHeight + audioSetupHeight + animatorDiagnosticsHeight +
                                           uiDocumentActionsHeight)
                    : 38.0F;
            if (auto card = m_Ui.BeginChild(cardId, {0.0F, cardHeight}, true, false); card)
            {
                const bool removed =
                    DrawComponentHeader(component, *registration, expanded, registration->Name, mixedComponentValues);
                if (!expanded || removed)
                {
                    continue;
                }
                auto enabled = component->Enabled();
                if (m_Ui.Checkbox("Enabled##" + registration->Type.ToString(), enabled))
                {
                    m_Panel.m_Controller.RecordInspectorUndo("Change " + registration->Name);
                    SetComponentEnabled(registration->Type, enabled);
                }
                if (animator && animator->PoseSource() == Keire::AnimatorPoseSource::ProceduralHumanoid)
                {
                    const auto& state = animator->ProceduralState();
                    m_Ui.TextColored(m_Theme.Accent, "PROCEDURAL RUNTIME");
                    m_Ui.Text("State: " + std::string(Keire::ProceduralMotionStateName(state.State)));
                    m_Ui.Text("Phase: " + std::to_string(state.GaitPhase) + " | Speed: " + std::to_string(state.Speed));
                    m_Ui.Text(std::string("Contacts: L ") + (state.LeftFootPlanted ? "planted" : "free") + " | R " +
                              (state.RightFootPlanted ? "planted" : "free"));
                    m_Ui.Text("Quality: " +
                              std::string(state.Quality == Keire::ProceduralMotionQuality::High     ? "High"
                                          : state.Quality == Keire::ProceduralMotionQuality::Medium ? "Medium"
                                          : state.Quality == Keire::ProceduralMotionQuality::Low    ? "Low"
                                                                                                    : "Auto"));
                    m_Ui.Separator();
                }
                if (animator)
                {
                    DrawAnimatorInspectorDiagnostic(m_Ui, *animator, m_Theme.Warning);
                }
                DrawUiDocumentInspectorActions(m_Ui, component, m_Panel.m_Controller, m_Theme);

                const ReflectedComponentState state{component,     *registration,       vfxEmitter,
                                                    vfxEffect,     vfxEffectDiagnostic, vfxEffectDiagnosticWarning,
                                                    audioSource,   reverbZone,          collider,
                                                    resolvedMixer, mixerAsset};
                DrawReflectedProperties(propertyEditor, state);
                DrawAudioDiagnostics(state);
            }
        }
    }

} // namespace KeireEditor
