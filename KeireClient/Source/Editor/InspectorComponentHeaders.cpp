#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClientInternal/Editor/InspectorEntityDrawContext.h"
#include <optional>
#include <span>
#include <utility>

namespace KeireEditor
{
    bool InspectorPanel::EntityDrawContext::IsCommonComponent(const Keire::Ref<Keire::Component>& component) const
    {
        if (!m_MultiEditing)
        {
            return true;
        }
        const auto ordinal = ComponentOrdinal(m_ComponentOrder, component);
        for (const auto targetId : m_EditTargets)
        {
            const auto target = m_Scene->FindEntity(Keire::EntityId(targetId));
            if (!target)
            {
                return false;
            }
            std::size_t matches = 0;
            for (const auto& candidate : target.GetComponents())
            {
                if (candidate && candidate->Type() == component->Type())
                {
                    ++matches;
                }
            }
            if (matches <= ordinal)
            {
                return false;
            }
        }
        return true;
    }

    void InspectorPanel::EntityDrawContext::SetComponentEnabled(const Keire::ComponentTypeId type, const bool enabled)
    {
        m_Document.SetComponentsEnabled(m_EditTargets, type, enabled, m_EditingComponentOrdinal);
    }

    void InspectorPanel::EntityDrawContext::SetComponentProperty(const Keire::ComponentTypeId type,
                                                                 const std::string_view property,
                                                                 Keire::ComponentPropertyValue value)
    {
        // A single inspected entity may also contain several instances of this type.
        m_Document.SetComponentsProperty(m_EditTargets, type, property, value, m_EditingComponentOrdinal);
    }

    void InspectorPanel::EntityDrawContext::ResetComponent(const Keire::ComponentTypeId type)
    {
        m_Document.ResetComponents(m_EditTargets, type, m_EditingComponentOrdinal);
    }

    bool InspectorPanel::EntityDrawContext::DrawComponentHeader(const Keire::Ref<Keire::Component>& component,
                                                                const Keire::ComponentRegistration& registration,
                                                                bool& expanded, const std::string_view title,
                                                                const bool mixedValues)
    {
        m_CurrentHeaderExpanded = &expanded;
        m_CurrentHeaderTitle = title;
        bool removed = false;
        const auto ordinal = ComponentOrdinal(m_ComponentOrder, component);
        const auto unique = registration.Type.ToString() + "." + std::to_string(ordinal);
        const auto popupId = "ComponentMenu##" + unique;
        const auto tableId = "ComponentHeader##" + unique;
        bool openMenu = false;
        if (auto table = m_Ui.BeginTable(tableId, 2,
                                         {.Sizing = Keire::UiTableSizing::Proportional,
                                          .Borders = false,
                                          .Resizable = false,
                                          .RowBackground = false,
                                          .PersistSettings = false});
            table)
        {
            m_Ui.TableSetupColumn("Component", Keire::UiTableColumnSizing::Stretch, 1.0F);
            m_Ui.TableSetupColumn("Menu", Keire::UiTableColumnSizing::Fixed, 28.0F);
            m_Ui.TableNextRow();
            (void)m_Ui.TableNextColumn();
            if (m_Ui.Selectable((expanded ? "v  " : ">  ") + std::string(title)))
            {
                expanded = !expanded;
            }
            const bool hoveredComponent = m_Ui.LastItemState().Hovered;
            const auto headerRect = m_Ui.LastItemRect();
            if (!m_MultiEditing)
            {
                if (auto source = m_Ui.BeginDragSource(); source)
                {
                    const auto payload = EncodeComponentOrderPayload({m_Entity.Id(), registration.Type, ordinal});
                    m_Ui.SetDragPayload("KEIRE_COMPONENT_ORDER",
                                        std::as_bytes(std::span(payload.data(), payload.size())));
                    m_Ui.Text(m_Entity.Name() + " / " + registration.Name);
                }
                if (auto target = m_Ui.BeginDragTarget(headerRect, "ComponentDrop##" + unique); target)
                {
                    std::vector<std::byte> bytes;
                    if (m_Ui.AcceptDragPayload("KEIRE_COMPONENT_ORDER", bytes))
                    {
                        const auto payload = DecodeComponentOrderPayload(bytes);
                        const auto source =
                            payload ? ResolveComponentOrderPayload(m_ComponentOrder, m_Entity.Id(), *payload)
                                    : Keire::Ref<Keire::Component>{};
                        if (source && source != component)
                        {
                            m_Panel.m_Controller.RecordInspectorUndo("Reorder Components");
                            m_Document.MoveComponentBefore(m_Entity.Id(), source, component);
                        }
                    }
                }
            }
            if (hoveredComponent && m_Ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
            {
                m_Panel.m_ComponentClipboard =
                    InspectorPanel::ComponentClipboard{registration.Type, registration.Serialize(*component), true};
            }
            if (auto context = m_Ui.BeginItemContextMenu("ComponentContext##" + unique); context)
            {
                removed = m_Panel.DrawComponentMenu(m_Ui, m_Entity, component, registration, m_Document, m_Scene);
            }
            (void)m_Ui.TableNextColumn();
            if (m_Ui.IconButton("ComponentMore##" + unique, Keire::UiIcon::More, false, {26.0F, 22.0F}))
            {
                openMenu = true;
            }
            if (m_Ui.LastItemState().Hovered)
            {
                m_Ui.SetTooltip(m_MultiEditing
                                    ? (mixedValues ? "Selection actions (mixed values)" : "Selection actions")
                                    : "Component actions",
                                {.Delayed = true});
            }
        }
        if (openMenu)
        {
            m_Ui.OpenPopup(popupId);
        }
        if (auto popup = m_Ui.BeginPopup(popupId); popup)
        {
            removed =
                m_Panel.DrawComponentMenu(m_Ui, m_Entity, component, registration, m_Document, m_Scene) || removed;
        }
        return removed;
    }

    bool& InspectorPanel::EntityDrawContext::Expansion(const std::string_view type)
    {
        const auto key = m_Entity.Id().ToString() + "." + std::string(type);
        return m_Panel.m_ComponentExpansion.try_emplace(key, true).first->second;
    }

} // namespace KeireEditor
