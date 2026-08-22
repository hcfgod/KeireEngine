#include "KeireClient/Editor/InspectorPropertyEditor.h"

#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>
#include <sstream>
#include <string>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::int64_t InspectorIntegerBound(const double value)
        {
            return static_cast<std::int64_t>(std::clamp(
                static_cast<long double>(value), static_cast<long double>(std::numeric_limits<std::int64_t>::min()),
                static_cast<long double>(std::numeric_limits<std::int64_t>::max())));
        }

        [[nodiscard]] std::vector<Keire::AssetId> DecodeComponentOrderPayload(const std::span<const std::byte> bytes)
        {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::istringstream stream(text);
            std::vector<Keire::AssetId> result;
            std::string line;
            while (result.size() < 2 && std::getline(stream, line))
                if (!line.empty())
                    result.push_back(Keire::AssetId::Parse(line));
            return result;
        }
    } // namespace

    InspectorPropertyEditor::InspectorPropertyEditor(Keire::UiFrame& ui,
                                                     const std::span<const Keire::AssetSourceRecord> assets,
                                                     const Keire::Ref<Keire::AssetSystem>& assetSystem,
                                                     const Keire::Ref<Keire::Scene>& scene, AssetPicker& assetPicker)
        : m_Ui(ui), m_Assets(assets), m_AssetSystem(assetSystem), m_Scene(scene), m_AssetPicker(assetPicker)
    {
    }

    bool InspectorPropertyEditor::EditBoundary() const noexcept { return m_EditBoundary; }

    bool InspectorPropertyEditor::EditBoolean(const std::string_view label, bool& value)
    {
        return Track(m_Ui.Checkbox(label, value));
    }

    bool InspectorPropertyEditor::EditInteger(const std::string_view label, std::int64_t& value, const double step,
                                              const std::optional<double> minimum, const std::optional<double> maximum)
    {
        const auto lower = minimum ? std::optional<std::int64_t>(InspectorIntegerBound(*minimum)) : std::nullopt;
        const auto upper = maximum ? std::optional<std::int64_t>(InspectorIntegerBound(*maximum)) : std::nullopt;
        return Track(m_Ui.DragInteger(label, value, step, lower, upper));
    }

    bool InspectorPropertyEditor::EditIntegerSlider(const std::string_view label, std::int64_t& value,
                                                    const double minimum, const double maximum)
    {
        return Track(m_Ui.SliderInteger(label, value, InspectorIntegerBound(minimum), InspectorIntegerBound(maximum)));
    }

    bool InspectorPropertyEditor::EditChoice(const std::string_view label, std::int64_t& value,
                                             const std::span<const std::string_view> choices)
    {
        const auto preview = value >= 0 && static_cast<std::size_t>(value) < choices.size()
                                 ? choices[static_cast<std::size_t>(value)]
                                 : std::string_view("Invalid");
        bool changed = false;
        if (auto combo = m_Ui.BeginCombo(label, preview); combo)
        {
            for (std::size_t index = 0; index < choices.size(); ++index)
            {
                if (m_Ui.Selectable(choices[index], value == static_cast<std::int64_t>(index)))
                {
                    value = static_cast<std::int64_t>(index);
                    changed = true;
                }
            }
        }
        return Track(changed);
    }

    bool InspectorPropertyEditor::EditScalar(const std::string_view label, double& value, const double step,
                                             const std::optional<double> minimum, const std::optional<double> maximum)
    {
        return Track(m_Ui.DragScalar(label, value, step, minimum, maximum));
    }

    bool InspectorPropertyEditor::EditScalarSlider(const std::string_view label, double& value, const double minimum,
                                                   const double maximum)
    {
        return Track(m_Ui.SliderScalar(label, value, minimum, maximum));
    }

    bool InspectorPropertyEditor::EditText(const std::string_view label, std::string& value)
    {
        return Track(m_Ui.InputText(label, value));
    }

    bool InspectorPropertyEditor::EditTextMultiline(const std::string_view label, std::string& value,
                                                    const std::uint32_t visibleLines)
    {
        return Track(m_Ui.InputTextMultiline(label, value, visibleLines));
    }

    bool InspectorPropertyEditor::EditVector2(const std::string_view label, Keire::Vector2& value, const double step)
    {
        return Track(m_Ui.DragVector2(label, value, static_cast<float>(step)));
    }

    bool InspectorPropertyEditor::EditVector3(const std::string_view label, Keire::Vector3& value, const double step)
    {
        return Track(m_Ui.DragVector3(label, value, static_cast<float>(step)));
    }

    bool InspectorPropertyEditor::EditVector4(const std::string_view label, Keire::Vector4& value, const double step)
    {
        return Track(m_Ui.DragVector4(label, value, static_cast<float>(step)));
    }

    bool InspectorPropertyEditor::EditQuaternion(const std::string_view label, Keire::Quaternion& value,
                                                 const double step)
    {
        return Track(m_Ui.DragQuaternion(label, value, static_cast<float>(step)));
    }

    bool InspectorPropertyEditor::EditColor(const std::string_view label, Keire::Color& value)
    {
        Keire::UiColor color{value.Red, value.Green, value.Blue, value.Alpha};
        const bool changed = m_Ui.ColorEdit(label, color);
        (void)Track(changed);
        if (!changed)
            return false;
        value = {color.Red, color.Green, color.Blue, color.Alpha};
        return true;
    }

    bool InspectorPropertyEditor::EditCurve(const std::string_view label, Keire::Curve1D& value)
    {
        return Track(AuthoringValueEditors::Curve(m_Ui, label, value));
    }

    bool InspectorPropertyEditor::EditGradient(const std::string_view label, Keire::ColorGradient& value)
    {
        return Track(AuthoringValueEditors::Gradient(m_Ui, label, value));
    }

    bool InspectorPropertyEditor::EditAsset(const std::string_view label, Keire::AssetId& value,
                                            const std::optional<Keire::AssetTypeId> expectedType)
    {
        AssetPickerOptions options;
        options.Label = label;
        options.ExpectedType = expectedType;
        if (m_AssetSystem)
            options.ResolveType = [assets = m_AssetSystem](const Keire::AssetId id) { return assets->TryGetType(id); };
        const bool changed = m_AssetPicker.Draw(m_Ui, m_Assets, value, options);
        if (changed)
            m_EditBoundary = true;
        return Track(changed);
    }

    bool InspectorPropertyEditor::EditTextureAsset(const std::string_view label, Keire::AssetId& value,
                                                   const Keire::ShaderTextureSemantic semantic)
    {
        AssetPickerOptions options;
        options.Label = label;
        options.ExpectedType = Keire::Texture2DAsset::StaticType();
        options.Filter = [semantic](const Keire::AssetSourceRecord& record)
        { return MaterialInspectorPanel::AcceptsTexture(record, semantic); };
        const bool changed = m_AssetPicker.Draw(m_Ui, m_Assets, value, options);
        if (changed)
            m_EditBoundary = true;
        return changed;
    }

    bool InspectorPropertyEditor::EditEntity(const std::string_view label, Keire::EntityId& value)
    {
        const auto selected = m_Scene ? m_Scene->FindEntity(value) : Keire::Entity{};
        const auto preview = selected ? selected.Name() : (value ? "Missing entity" : "None");
        bool changed = false;
        if (auto combo = m_Ui.BeginCombo(label, preview); combo)
        {
            if (m_Ui.Selectable("None", !value))
            {
                value = {};
                changed = true;
            }
            if (m_Scene)
            {
                for (const auto& entity : SceneEntities())
                {
                    if (m_Ui.Selectable(entity.Name(), entity.Id() == value))
                    {
                        value = entity.Id();
                        changed = true;
                    }
                }
            }
        }
        if (auto target = m_Ui.BeginDragTarget(); target)
        {
            std::vector<std::byte> payload;
            if (m_Ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
            {
                const auto entities = DecodeAssetPayload(payload);
                if (entities.size() == 1 && m_Scene && m_Scene->FindEntity(Keire::EntityId(entities.front())))
                {
                    value = Keire::EntityId(entities.front());
                    changed = true;
                }
            }
        }
        return Track(changed);
    }

    bool InspectorPropertyEditor::EditComponentReference(const std::string_view label,
                                                         Keire::ComponentReferenceValue& value,
                                                         const Keire::ComponentProperty& property)
    {
        struct Candidate
        {
            Keire::EntityId Entity;
            Keire::ComponentTypeId Component;
            std::string Label;
        };
        std::vector<Candidate> candidates;
        const auto registry = m_Scene ? m_Scene->Components() : Keire::Ref<Keire::ComponentRegistry>{};
        if (registry)
        {
            for (const auto& entity : SceneEntities())
            {
                for (const auto componentType : property.CompatibleComponentTypes)
                {
                    if (!entity.HasComponent(componentType))
                        continue;
                    const auto registration = registry->Find(componentType);
                    candidates.push_back(
                        {entity.Id(), componentType,
                         entity.Name() + " / " + (registration ? registration->Name : componentType.ToString())});
                }
            }
        }

        const auto current = std::ranges::find_if(
            candidates, [&](const Candidate& candidate)
            { return candidate.Entity == value.Entity && candidate.Component == value.Component; });
        const std::string preview = current != candidates.end() ? current->Label
                                    : value.Entity              ? "Missing component"
                                                                : "None";
        bool changed = false;
        if (auto combo = m_Ui.BeginCombo(label, preview); combo)
        {
            if (m_Ui.Selectable("None", !value.Entity))
            {
                value = {};
                changed = true;
            }
            for (const auto& candidate : candidates)
            {
                if (m_Ui.Selectable(candidate.Label,
                                    candidate.Entity == value.Entity && candidate.Component == value.Component))
                {
                    value = {candidate.Entity, candidate.Component};
                    changed = true;
                }
            }
        }

        const auto assignDraggedEntity = [&](const Keire::EntityId entity)
        {
            const auto first = std::ranges::find(candidates, entity, &Candidate::Entity);
            if (first == candidates.end())
                return;
            const auto matches = std::ranges::count(candidates, entity, &Candidate::Entity);
            if (matches == 1)
            {
                value = {first->Entity, first->Component};
                changed = true;
                return;
            }
            m_Ui.OpenPopup(std::string(label) + "##ComponentReferenceChooser");
        };

        if (auto target = m_Ui.BeginDragTarget(); target)
        {
            std::vector<std::byte> payload;
            if (m_Ui.AcceptDragPayload("KEIRE_COMPONENT", payload))
            {
                const auto ids = DecodeAssetPayload(payload);
                if (ids.size() == 2)
                {
                    const Keire::EntityId entity(ids[0]);
                    const Keire::ComponentTypeId component(ids[1]);
                    if (std::ranges::any_of(candidates, [&](const Candidate& candidate)
                                            { return candidate.Entity == entity && candidate.Component == component; }))
                    {
                        value = {entity, component};
                        changed = true;
                    }
                }
            }
            payload.clear();
            if (m_Ui.AcceptDragPayload("KEIRE_COMPONENT_ORDER", payload))
            {
                const auto ids = DecodeComponentOrderPayload(payload);
                if (ids.size() == 2)
                {
                    const Keire::EntityId entity(ids[0]);
                    const Keire::ComponentTypeId component(ids[1]);
                    if (std::ranges::any_of(candidates, [&](const Candidate& candidate)
                                            { return candidate.Entity == entity && candidate.Component == component; }))
                    {
                        value = {entity, component};
                        changed = true;
                    }
                }
            }
            payload.clear();
            if (m_Ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
            {
                const auto ids = DecodeAssetPayload(payload);
                if (ids.size() == 1)
                    assignDraggedEntity(Keire::EntityId(ids.front()));
            }
        }

        const std::string chooser = std::string(label) + "##ComponentReferenceChooser";
        if (auto popup = m_Ui.BeginPopup(chooser); popup)
        {
            for (const auto& candidate : candidates)
            {
                if (m_Ui.Selectable(candidate.Label))
                {
                    value = {candidate.Entity, candidate.Component};
                    changed = true;
                }
            }
        }
        return Track(changed);
    }

    bool InspectorPropertyEditor::EditEvent(const std::string_view label, Keire::ComponentEventValue& value,
                                            const std::size_t argumentCount)
    {
        if (!m_Scene)
            return false;
        const auto registry = m_Scene->Components();
        auto eventId = m_Ui.PushId(label);
        bool changed = false;
        m_Ui.Text(label);
        m_Ui.TextColored({0.48F, 0.55F, 0.64F, 1.0F},
                         std::to_string(value.Listeners.size()) +
                             (value.Listeners.size() == 1 ? " persistent listener" : " persistent listeners"));

        for (std::size_t index = 0; index < value.Listeners.size(); ++index)
        {
            auto& listener = value.Listeners[index];
            const auto listenerId = std::to_string(index);
            auto id = m_Ui.PushId(listenerId);
            changed |= m_Ui.Checkbox("Enabled", listener.Enabled);

            auto target = m_Scene->FindEntity(listener.Target);
            const auto targetPreview = target ? target.Name() : std::string("None");
            if (auto combo = m_Ui.BeginCombo("Target", targetPreview); combo)
            {
                if (m_Ui.Selectable("None", !listener.Target))
                {
                    listener.Target = {};
                    listener.Component = {};
                    listener.Method.clear();
                    changed = true;
                }
                for (const auto& entity : SceneEntities())
                {
                    if (m_Ui.Selectable(entity.Name(), entity.Id() == listener.Target))
                    {
                        listener.Target = entity.Id();
                        listener.Component = {};
                        listener.Method.clear();
                        changed = true;
                    }
                }
            }

            target = m_Scene->FindEntity(listener.Target);
            std::optional<Keire::ComponentRegistration> selectedRegistration;
            if (listener.Component)
                selectedRegistration = registry->Find(listener.Component);
            const auto componentPreview = selectedRegistration ? selectedRegistration->Name : std::string("None");
            if (auto combo = m_Ui.BeginCombo("Component", componentPreview); combo)
            {
                if (m_Ui.Selectable("None", !listener.Component))
                {
                    listener.Component = {};
                    listener.Method.clear();
                    changed = true;
                }
                if (target)
                {
                    for (const auto& component : target.GetComponents())
                    {
                        const auto registration = registry->Find(component->Type());
                        if (!registration || !registration->Methods ||
                            std::ranges::none_of(*registration->Methods, [argumentCount](const auto& method)
                                                 { return method.ParameterTypes.size() == argumentCount; }))
                        {
                            continue;
                        }
                        if (m_Ui.Selectable(registration->Name, component->Type() == listener.Component))
                        {
                            listener.Component = component->Type();
                            listener.Method.clear();
                            changed = true;
                        }
                    }
                }
            }

            selectedRegistration = listener.Component ? registry->Find(listener.Component) : std::nullopt;
            const auto methodPreview = listener.Method.empty() ? std::string("No Function") : listener.Method;
            if (auto combo = m_Ui.BeginCombo("Function", methodPreview); combo)
            {
                if (m_Ui.Selectable("No Function", listener.Method.empty()))
                {
                    listener.Method.clear();
                    changed = true;
                }
                if (selectedRegistration && selectedRegistration->Methods)
                {
                    for (const auto& method : *selectedRegistration->Methods)
                    {
                        if (method.ParameterTypes.size() != argumentCount)
                            continue;
                        if (m_Ui.Selectable(method.DisplayName, method.Name == listener.Method))
                        {
                            listener.Method = method.Name;
                            changed = true;
                        }
                    }
                }
            }

            if (m_Ui.Button("Remove Listener"))
            {
                value.Listeners.erase(value.Listeners.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }
            m_Ui.Separator();
        }

        if (m_Ui.Button("Add Listener"))
        {
            value.Listeners.emplace_back();
            changed = true;
        }
        return changed;
    }

    const std::vector<Keire::Entity>& InspectorPropertyEditor::SceneEntities()
    {
        if (!m_EntityCache)
            m_EntityCache = m_Scene ? m_Scene->Entities() : std::vector<Keire::Entity>{};
        return *m_EntityCache;
    }

    bool InspectorPropertyEditor::Track(const bool changed)
    {
        const auto state = m_Ui.LastItemState();
        m_EditBoundary = m_EditBoundary || state.DeactivatedAfterEdit || (changed && !state.Active);
        return changed;
    }
} // namespace KeireEditor
