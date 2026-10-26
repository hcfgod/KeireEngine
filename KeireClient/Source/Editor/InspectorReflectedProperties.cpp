#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/InspectorPropertyVisibility.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/VfxEmitterInspector.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include "KeireClientInternal/Editor/InspectorReflectedComponentState.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace KeireEditor
{
    void InspectorPanel::EntityDrawContext::DrawReflectedProperties(InspectorPropertyEditor& propertyEditor,
                                                                    const ReflectedComponentState& state)
    {
        std::string activeGroup;
        Keire::ComponentPropertyBag values;
        bool serialized = false;
        try
        {
            values = state.Registration.Serialize(*state.Component);
            serialized = true;
        }
        catch (const std::exception& error)
        {
            m_Ui.TextColored(m_Theme.Error, error.what());
        }
        for (const auto& property : state.Registration.Properties)
        {
            if (!serialized)
            {
                break;
            }
            if (!IsInspectorPropertyVisible(state.Registration.Type, property.Key))
            {
                continue;
            }
            if (state.Collider && !ColliderPropertyVisible(state.Collider->Shape(), property.Key))
            {
                continue;
            }
            if (!property.Group.empty() && property.Group != activeGroup)
            {
                activeGroup = property.Group;
                m_Ui.TextColored(m_Theme.MutedText, activeGroup);
            }
            if (!property.Header.empty())
            {
                m_Ui.TextColored(m_Theme.Accent, property.Header);
            }
            const auto propertyDisabled = m_Ui.BeginDisabled(property.ReadOnly);
            try
            {
                const auto found = values.find(property.Key);
                if (found == values.end())
                {
                    throw std::invalid_argument("The component omitted a declared property.");
                }
                auto candidate = found->second;
                std::optional<std::string> audioBusFallback;
                bool changed = false;
                bool editBoundary = false;
                const bool localLight = state.Registration.Type == Keire::PointLightComponent::StaticType() ||
                                        state.Registration.Type == Keire::SpotLightComponent::StaticType();
                const bool vfxOverrides = state.Registration.Type == Keire::VfxEmitterComponent::StaticType() &&
                                          property.Key == "parameterOverrides";
                if (state.Collider && property.Key == "shape")
                {
                    const auto* current = std::get_if<std::int64_t>(&candidate);
                    constexpr std::array<std::string_view, 5> labels{"Box", "Sphere", "Capsule", "Convex Mesh",
                                                                     "Triangle Mesh"};
                    if (!current || *current < 0 || static_cast<std::size_t>(*current) >= labels.size())
                    {
                        throw std::invalid_argument("The collider shape is invalid.");
                    }
                    if (auto shape =
                            m_Ui.BeginCombo("Shape##ColliderShape", labels[static_cast<std::size_t>(*current)]);
                        shape)
                    {
                        for (std::int64_t index = 0; index < static_cast<std::int64_t>(labels.size()); ++index)
                        {
                            if (m_Ui.Selectable(labels[static_cast<std::size_t>(index)], *current == index))
                            {
                                candidate = index;
                                changed = true;
                                editBoundary = true;
                            }
                        }
                    }
                }
                else if (state.AudioSource && property.Key == "busId")
                {
                    const auto current = state.AudioSource->BusId();
                    std::span<const Keire::AudioMixerBusDefinition> buses;
                    std::string preview = !state.ResolvedMixer ? "Select a Mixer" : "Loading mixer buses...";
                    if (state.MixerAsset)
                    {
                        buses = state.MixerAsset->Definition().Buses;
                        if (const auto selected =
                                std::ranges::find(buses, current, &Keire::AudioMixerBusDefinition::Id);
                            selected != buses.end())
                        {
                            preview = selected->Name;
                        }
                        else
                        {
                            preview = "Missing Bus";
                        }
                    }
                    if (auto disabled = m_Ui.BeginDisabled(!state.MixerAsset); disabled)
                    {
                        if (auto busCombo = m_Ui.BeginCombo("Bus", preview); busCombo)
                        {
                            for (const auto& bus : buses)
                            {
                                if (m_Ui.Selectable(bus.Name, bus.Id == current))
                                {
                                    candidate = bus.Id.ToString();
                                    audioBusFallback = bus.Name;
                                    changed = true;
                                    editBoundary = true;
                                }
                            }
                        }
                    }
                }
                else if (state.ReverbZone && property.Key == "snapshotId")
                {
                    const auto current = state.ReverbZone->SnapshotId();
                    std::span<const Keire::AudioMixerSnapshotDefinition> snapshots;
                    std::string preview = !current ? "None (wet blend only)" : "Missing Snapshot";
                    if (state.MixerAsset && current)
                    {
                        snapshots = state.MixerAsset->Definition().Snapshots;
                        if (const auto selected =
                                std::ranges::find(snapshots, current, &Keire::AudioMixerSnapshotDefinition::Id);
                            selected != snapshots.end())
                        {
                            preview = selected->Name;
                        }
                    }
                    else if (state.MixerAsset)
                    {
                        snapshots = state.MixerAsset->Definition().Snapshots;
                    }
                    if (auto disabled = m_Ui.BeginDisabled(!state.MixerAsset); disabled)
                    {
                        if (auto snapshotCombo = m_Ui.BeginCombo("Snapshot", preview); snapshotCombo)
                        {
                            if (m_Ui.Selectable("None (wet blend only)", !current))
                            {
                                candidate = std::string{};
                                changed = true;
                                editBoundary = true;
                            }
                            for (const auto& snapshot : snapshots)
                            {
                                if (m_Ui.Selectable(snapshot.Name, snapshot.Id == current))
                                {
                                    candidate = snapshot.Id.ToString();
                                    changed = true;
                                    editBoundary = true;
                                }
                            }
                        }
                    }
                }
                else if (vfxOverrides)
                {
                    if (!state.VfxEffect)
                    {
                        m_Ui.TextColored(state.VfxDiagnosticWarning ? m_Theme.Warning : m_Theme.MutedText,
                                         state.VfxDiagnostic);
                    }
                    else
                    {
                        auto overrides =
                            std::vector<Keire::VfxParameterOverride>(state.VfxEmitter->ParameterOverrides().begin(),
                                                                     state.VfxEmitter->ParameterOverrides().end());
                        if (KeireEditor::VfxEmitterInspector::VisibleEntryCount(state.VfxEffect->Definition(),
                                                                                overrides) == 0)
                        {
                            m_Ui.TextColored(m_Theme.MutedText, "This effect has no exposed Blackboard parameters.");
                        }
                        bool actionBoundary = false;
                        KeireEditor::VfxEmitterInspectorCallbacks callbacks;
                        callbacks.Status =
                            [this](const Keire::AssetId, const std::string_view message, const bool warning)
                        { m_Ui.TextColored(warning ? m_Theme.Warning : m_Theme.MutedText, message); };
                        callbacks.Reset = [this, &actionBoundary](const Keire::AssetId parameter)
                        {
                            m_Ui.SameLine();
                            auto id = m_Ui.PushId("VfxBlackboardReset-" + parameter.ToString());
                            if (!m_Ui.Button("Reset"))
                            {
                                return false;
                            }
                            actionBoundary = true;
                            return true;
                        };
                        callbacks.RemoveStale = [this, &actionBoundary](const Keire::AssetId parameter)
                        {
                            m_Ui.SameLine();
                            auto id = m_Ui.PushId("VfxBlackboardRemoveStale-" + parameter.ToString());
                            if (!m_Ui.Button("Remove"))
                            {
                                return false;
                            }
                            actionBoundary = true;
                            return true;
                        };
                        InspectorPropertyEditor vfxPropertyEditor(m_Ui, m_Records, m_Assets, m_Scene,
                                                                  *m_Panel.m_AssetPicker);
                        changed = KeireEditor::VfxEmitterInspector{}.Draw(
                            vfxPropertyEditor, state.VfxEffect->Definition(), overrides, callbacks);
                        editBoundary = actionBoundary || vfxPropertyEditor.EditBoundary();
                        if (changed)
                        {
                            candidate = KeireEditor::VfxEmitterInspector::SerializeOverrides(state.Registration, values,
                                                                                             overrides);
                        }
                    }
                }
                else if (localLight && property.Key == "shadows")
                {
                    const auto* current = std::get_if<std::int64_t>(&candidate);
                    if (!current || *current < 0 || *current > 2)
                    {
                        throw std::invalid_argument("The local-light shadow mode is invalid.");
                    }
                    constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                    if (auto shadowMode =
                            m_Ui.BeginCombo(property.DisplayName, labels[static_cast<std::size_t>(*current)]);
                        shadowMode)
                    {
                        for (std::int64_t index = 0; index < static_cast<std::int64_t>(labels.size()); ++index)
                        {
                            if (m_Ui.Selectable(labels[static_cast<std::size_t>(index)], *current == index))
                            {
                                candidate = index;
                                changed = true;
                            }
                        }
                    }
                }
                else
                {
                    changed = m_PropertyDrawers.Draw(propertyEditor, state.Registration.Type, property, candidate);
                }
                if (!vfxOverrides && !property.Tooltip.empty() && m_Ui.LastItemState().Hovered)
                {
                    m_Ui.SetTooltip(property.Tooltip, {.Delayed = true});
                }
                if (changed)
                {
                    const auto assignedUiToolkitAsset =
                        state.Registration.Type == Keire::UiDocumentComponent::StaticType() &&
                                (property.Key == "visualTree" || property.Key == "panelSettings")
                            ? std::get_if<Keire::AssetId>(&candidate)
                            : nullptr;
                    const auto uiToolkitAsset = assignedUiToolkitAsset ? *assignedUiToolkitAsset : Keire::AssetId{};
                    m_Panel.m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                             state.Registration.Type.ToString() + "." + property.Key +
                                                                 "." + m_Entity.Id().ToString() + "." +
                                                                 std::to_string(m_Panel.m_EditSerial));
                    SetComponentProperty(state.Registration.Type, property.Key, std::move(candidate));
                    if (uiToolkitAsset)
                    {
                        m_Panel.m_Controller.NotifyInspectorUiToolkitAssetAssigned(uiToolkitAsset);
                    }
                    if (audioBusFallback)
                    {
                        SetComponentProperty(state.Registration.Type, "bus", std::move(*audioBusFallback));
                    }
                }
                if (changed && (editBoundary || m_Ui.LastItemState().DeactivatedAfterEdit))
                {
                    ++m_Panel.m_EditSerial;
                }
            }
            catch (const std::exception& error)
            {
                m_Ui.TextColored(m_Theme.Error, FormatManagedInspectorError(error));
            }
        }
    }

    void InspectorPanel::EntityDrawContext::DrawAudioDiagnostics(const ReflectedComponentState& state)
    {
        if (state.Registration.Type == Keire::AudioReverbZoneComponent::StaticType())
        {
            const auto zone = Keire::DynamicRefCast<Keire::AudioReverbZoneComponent>(state.Component);
            if (!zone || !state.ResolvedMixer)
            {
                m_Ui.TextColoredWrapped(m_Theme.Warning,
                                        "Assign a Mixer here or configure the project's Default Mixer.");
            }
            else if (!m_Assets)
            {
                m_Ui.TextColored(m_Theme.Warning, "Audio assets are unavailable for setup validation.");
            }
            else if (state.MixerAsset)
            {
                const auto& definition = state.MixerAsset->Definition();
                const bool hasReverb = std::ranges::any_of(
                    definition.Buses,
                    [](const Keire::AudioMixerBusDefinition& bus)
                    {
                        return std::ranges::any_of(
                            bus.Effects,
                            [](const Keire::AudioMixerEffectDefinition& effect)
                            {
                                return effect.Type == Keire::AudioGraphNodeType::AlgorithmicReverb ||
                                       effect.Type == Keire::AudioGraphNodeType::ConvolutionReverb;
                            });
                    });
                const bool snapshotValid =
                    !zone->SnapshotId() ||
                    std::ranges::any_of(definition.Snapshots, [&](const Keire::AudioMixerSnapshotDefinition& snapshot)
                                        { return snapshot.Id == zone->SnapshotId(); });
                m_Ui.TextColored(hasReverb && snapshotValid ? m_Theme.Success : m_Theme.Warning,
                                 !hasReverb       ? "Mixer has no reverb effect. Use Create Reverb Return."
                                 : !snapshotValid ? "Snapshot Stable ID is not present in this mixer."
                                                  : "Zone setup valid: wet level follows listener overlap.");
                m_Ui.TextColored(m_Theme.MutedText,
                                 "Snapshot is optional; Reverb Send controls wet intensity inside the zone.");
                if (!zone->Mixer())
                {
                    m_Ui.TextColored(m_Theme.MutedText, "Routing through the Project Settings Default Mixer.");
                }
            }
            else
            {
                m_Ui.TextColored(m_Theme.MutedText, "Loading mixer setup validation...");
            }
        }
        else if (state.Registration.Type == Keire::AudioSourceComponent::StaticType())
        {
            const auto source = Keire::DynamicRefCast<Keire::AudioSourceComponent>(state.Component);
            if (source && state.ResolvedMixer && m_Assets)
            {
                if (state.MixerAsset)
                {
                    const auto& buses = state.MixerAsset->Definition().Buses;
                    const bool busValid =
                        source->BusId() ? std::ranges::any_of(buses, [&](const Keire::AudioMixerBusDefinition& bus)
                                                              { return bus.Id == source->BusId(); })
                                        : std::ranges::any_of(buses, [&](const Keire::AudioMixerBusDefinition& bus)
                                                              { return bus.Name == source->Bus(); });
                    m_Ui.TextColored(busValid ? m_Theme.Success : m_Theme.Warning,
                                     busValid ? "Mixer route is valid and active."
                                              : "Selected bus is missing; audio will fall back to Master.");
                    if (!source->Mixer())
                    {
                        m_Ui.TextColored(m_Theme.MutedText, "Routing through the Project Settings Default Mixer.");
                    }
                }
            }
            else if (source && !state.ResolvedMixer)
            {
                m_Ui.TextColoredWrapped(m_Theme.Warning,
                                        "Assign a Mixer here or configure the project's Default Mixer.");
            }
        }
    }
} // namespace KeireEditor
