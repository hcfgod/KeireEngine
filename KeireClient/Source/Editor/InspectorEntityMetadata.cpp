#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
namespace
{
    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(const std::span<const std::byte> bytes)
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream stream(text);
        std::vector<Keire::AssetId> result;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty())
            {
                result.push_back(Keire::AssetId::Parse(line));
            }
        }
        return result;
    }

} // namespace

namespace KeireEditor
{
    void InspectorPanel::EntityDrawContext::DrawMetadata()
    {
        if (m_MultiEditing)
        {
            m_Ui.TextColored(m_Theme.MutedText, std::to_string(m_EditTargets.size()) +
                                                    " entities selected; common component changes apply to all.");
        }
        const auto currentName = m_Entity.Name();
        if (m_Panel.m_EntityNameTarget != m_Entity.Id().Value() || !m_Panel.m_EntityNameEditing)
        {
            m_Panel.m_EntityNameTarget = m_Entity.Id().Value();
            m_Panel.m_EntityNameDraft = currentName;
        }
        const auto entityNameLabel =
            m_MultiEditing ? std::string_view("Primary Entity Name") : std::string_view("Entity Name");
        const auto entityNameLayout = KeireEditor::Detail::ResolveInspectorFieldLayout(m_Ui.ContentAvailable().Width);
        if (entityNameLayout.Stacked)
        {
            m_Ui.TextWrapped(entityNameLabel);
            m_Ui.SetNextItemWidth(entityNameLayout.ControlWidth);
        }
        (void)m_Ui.InputText(entityNameLayout.Stacked ? "###InspectorEntityName"
                                                      : std::string(entityNameLabel) + "###InspectorEntityName",
                             m_Panel.m_EntityNameDraft);
        const auto nameState = m_Ui.LastItemState();
        const bool validEntityName = SceneDocument::IsValidEntityName(m_Panel.m_EntityNameDraft);
        if (nameState.DeactivatedAfterEdit)
        {
            if (validEntityName && m_Panel.m_EntityNameDraft != currentName)
            {
                m_Panel.m_Controller.RecordInspectorUndo();
                m_Document.RenameEntity(m_Entity.Id(), m_Panel.m_EntityNameDraft);
            }
            else if (!validEntityName)
            {
                m_Panel.m_EntityNameDraft = currentName;
            }
        }
        if (nameState.Active && !validEntityName)
        {
            m_Ui.TextColored(m_Theme.Error, m_Panel.m_EntityNameDraft.empty()
                                                ? "Entity name cannot be empty."
                                                : "Entity name cannot exceed 256 UTF-8 bytes.");
        }
        m_Panel.m_EntityNameEditing = nameState.Active;
        auto active = m_Entity.ActiveSelf();
        const bool mixedActive =
            m_MultiEditing && std::ranges::any_of(m_EditTargets,
                                                  [&](const Keire::AssetId id)
                                                  {
                                                      const auto selected = m_Scene->FindEntity(Keire::EntityId(id));
                                                      return selected && selected.ActiveSelf() != active;
                                                  });
        if (mixedActive)
        {
            m_Ui.TextColored(m_Theme.MutedText, "Active: Mixed");
        }
        if (m_Ui.Checkbox("Active", active))
        {
            m_Panel.m_Controller.RecordInspectorUndo();
            if (m_MultiEditing)
            {
                m_Document.SetEntitiesActive(m_EditTargets, active);
            }
            else
            {
                m_Document.SetEntityActive(m_Entity.Id(), active);
            }
        }
        const auto layerNames = m_Panel.m_Controller.InspectorLayerNames();
        auto layer = m_Entity.Layer();
        bool mixedLayers = false;
        if (!m_Panel.m_Registration.Locked() && m_Document.Selections().size() > 1)
        {
            for (const auto selected : m_Document.Selections())
            {
                const auto selectedEntity = m_Scene->FindEntity(Keire::EntityId(selected));
                if (selectedEntity && selectedEntity.Layer() != layer)
                {
                    mixedLayers = true;
                    break;
                }
            }
        }
        const auto layerPreview =
            mixedLayers ? std::string("Mixed")
                        : (layer < layerNames.size() ? layerNames[layer] : "Layer " + std::to_string(layer));
        if (auto layerCombo = m_Ui.BeginCombo("Layer", layerPreview); layerCombo)
        {
            for (std::uint32_t candidate = 0; candidate < Keire::EntityLayerCount; ++candidate)
            {
                const auto label = candidate < layerNames.size() && !layerNames[candidate].empty()
                                       ? layerNames[candidate]
                                       : "Layer " + std::to_string(candidate);
                if (m_Ui.Selectable(label, !mixedLayers && candidate == layer))
                {
                    m_Panel.m_Controller.RecordInspectorUndo("Change Layer");
                    if (!m_Panel.m_Registration.Locked() && m_Document.Selections().size() > 1)
                    {
                        m_Document.SetEntitiesLayer(m_Document.Selections(), candidate);
                    }
                    else
                    {
                        m_Document.SetEntityLayer(m_Entity.Id(), candidate);
                    }
                    layer = candidate;
                    mixedLayers = false;
                }
            }
        }
        const auto currentTags = m_Entity.Tags();
        if (m_Panel.m_EntityTagTarget != m_Entity.Id().Value() || !m_Panel.m_EntityTagEditing)
        {
            m_Panel.m_EntityTagTarget = m_Entity.Id().Value();
            m_Panel.m_EntityTagDraft = JoinEntityTags(currentTags);
        }
        (void)m_Ui.InputText(m_MultiEditing ? "Primary Entity Tags" : "Tags", m_Panel.m_EntityTagDraft);
        const auto tagState = m_Ui.LastItemState();
        const auto parsedTags = ParseEntityTags(m_Panel.m_EntityTagDraft);
        if (tagState.DeactivatedAfterEdit)
        {
            if (parsedTags && *parsedTags != currentTags)
            {
                m_Panel.m_Controller.RecordInspectorUndo("Change Tags");
                m_Document.SetEntityTags(m_Entity.Id(), *parsedTags);
            }
            else if (!parsedTags)
            {
                m_Panel.m_EntityTagDraft = JoinEntityTags(currentTags);
            }
        }
        if (tagState.Active)
        {
            m_Ui.TextColored(parsedTags ? m_Theme.MutedText : m_Theme.Error,
                             parsedTags ? "Comma-separated identifiers; supports letters, digits, _, -, and ."
                                        : "Use up to 16 unique tags beginning with a letter.");
        }
        m_Panel.m_EntityTagEditing = tagState.Active;
        m_Ui.Separator();
    }

    void InspectorPanel::EntityDrawContext::PasteComponent()
    {
        const auto copiedRegistration = m_Panel.m_ComponentClipboard && m_Panel.m_ComponentClipboard->WholeComponent
                                            ? m_Scene->Components()->Find(m_Panel.m_ComponentClipboard->Type)
                                            : std::nullopt;
        const bool canPasteComponent =
            copiedRegistration && copiedRegistration->Removable &&
            (copiedRegistration->AllowMultiple || !m_Entity.HasComponent(copiedRegistration->Type));
        if (m_Ui.WindowFocused() && m_Ui.Shortcut({.Key = Keire::UiKey::V, .Primary = true}) && canPasteComponent)
        {
            try
            {
                m_Panel.m_Controller.RecordInspectorUndo("Paste " + copiedRegistration->Name);
                const auto pasted = m_Document.AddComponent(m_Entity.Id(), copiedRegistration->Type);
                try
                {
                    m_Document.SetComponentValues(m_Entity.Id(), pasted, m_Panel.m_ComponentClipboard->Values);
                }
                catch (...)
                {
                    m_Document.RemoveComponent(m_Entity.Id(), pasted);
                    throw;
                }
            }
            catch (const std::exception& error)
            {
                m_Panel.m_Controller.ReportInspectorAssetError(std::string("Component operation failed: ") +
                                                               error.what());
            }
        }
    }

    void InspectorPanel::EntityDrawContext::DrawFooter()
    {
        m_Panel.DrawAddComponentMenu(m_Ui, m_Entity, m_Document, m_Scene);
        m_Ui.TextColored(m_Theme.MutedText, "Drop a C# script here to attach it");
        if (auto target = m_Ui.BeginDragTarget(); target)
        {
            std::vector<std::byte> payload;
            if (m_Ui.AcceptDragPayload("KEIRE_ASSETS", payload))
            {
                try
                {
                    for (const auto script : DecodeAssetPayload(payload))
                    {
                        m_Panel.m_Controller.AddScriptToEntity(m_Entity.Id(), script);
                    }
                }
                catch (const std::exception& error)
                {
                    m_Panel.m_Controller.ReportInspectorAssetError(error.what());
                }
            }
        }
        m_Ui.TextColored(m_Theme.MutedText, "Object ID");
        m_Ui.Text(m_Entity.Id().ToString());
    }

} // namespace KeireEditor
