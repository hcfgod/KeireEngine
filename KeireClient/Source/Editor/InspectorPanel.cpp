#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/ManagedDataInspectorPanel.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/VfxEmitterInspector.h"

#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
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
                result.push_back(Keire::AssetId::Parse(line));
        }
        return result;
    }
    [[nodiscard]] bool ContainsCaseInsensitive(const std::string_view value, const std::string_view query)
    {
        if (query.empty())
            return true;
        const auto found = std::ranges::search(value, query,
                                               [](const char left, const char right)
                                               {
                                                   return std::tolower(static_cast<unsigned char>(left)) ==
                                                          std::tolower(static_cast<unsigned char>(right));
                                               });
        return !found.empty();
    }
    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open asset: " + path.string());
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::vector<std::byte> bytes(characters.size());
        std::ranges::transform(characters, bytes.begin(), [](const char value) { return std::byte(value); });
        return bytes;
    }
    [[nodiscard]] std::string FormatAssetDiagnostic(const Keire::AssetImportDiagnostic& diagnostic)
    {
        auto result = diagnostic.RelativePath.generic_string();
        if (diagnostic.Line != 0)
        {
            result += ':' + std::to_string(diagnostic.Line);
            if (diagnostic.Column != 0)
                result += ':' + std::to_string(diagnostic.Column);
        }
        if (!result.empty())
            result += ": ";
        result += diagnostic.Message;
        return result;
    }

    struct AnchorPreset final
    {
        std::string_view Name;
        Keire::Vector2 Minimum;
        Keire::Vector2 Maximum;
    };

    struct AnchorPresetSelection final
    {
        AnchorPreset Preset;
        bool FitStretch = false;
    };

    constexpr std::array AnchorPresets{
        AnchorPreset{"Top Left", {0.0F, 0.0F}, {0.0F, 0.0F}},
        AnchorPreset{"Top Center", {0.5F, 0.0F}, {0.5F, 0.0F}},
        AnchorPreset{"Top Right", {1.0F, 0.0F}, {1.0F, 0.0F}},
        AnchorPreset{"Top Stretch", {0.0F, 0.0F}, {1.0F, 0.0F}},
        AnchorPreset{"Middle Left", {0.0F, 0.5F}, {0.0F, 0.5F}},
        AnchorPreset{"Middle Center", {0.5F, 0.5F}, {0.5F, 0.5F}},
        AnchorPreset{"Middle Right", {1.0F, 0.5F}, {1.0F, 0.5F}},
        AnchorPreset{"Middle Stretch", {0.0F, 0.5F}, {1.0F, 0.5F}},
        AnchorPreset{"Bottom Left", {0.0F, 1.0F}, {0.0F, 1.0F}},
        AnchorPreset{"Bottom Center", {0.5F, 1.0F}, {0.5F, 1.0F}},
        AnchorPreset{"Bottom Right", {1.0F, 1.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Bottom Stretch", {0.0F, 1.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Stretch Left", {0.0F, 0.0F}, {0.0F, 1.0F}},
        AnchorPreset{"Stretch Center", {0.5F, 0.0F}, {0.5F, 1.0F}},
        AnchorPreset{"Stretch Right", {1.0F, 0.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Stretch Both", {0.0F, 0.0F}, {1.0F, 1.0F}},
    };

    [[nodiscard]] bool ApproximatelyEqual(const Keire::Vector2 left, const Keire::Vector2 right) noexcept
    {
        constexpr float Epsilon = 0.0001F;
        return std::abs(left.X - right.X) <= Epsilon && std::abs(left.Y - right.Y) <= Epsilon;
    }

    [[nodiscard]] std::string_view AnchorPresetName(const Keire::Vector2 minimum, const Keire::Vector2 maximum) noexcept
    {
        const auto found = std::ranges::find_if(
            AnchorPresets, [minimum, maximum](const AnchorPreset& preset)
            { return ApproximatelyEqual(preset.Minimum, minimum) && ApproximatelyEqual(preset.Maximum, maximum); });
        return found == AnchorPresets.end() ? std::string_view("Custom") : found->Name;
    }

    void DrawAnchorPresetGlyph(Keire::UiFrame& ui, const Keire::UiItemRect button, const AnchorPreset& preset,
                               const bool selected, const Keire::UiThemeDefinition& theme)
    {
        const Keire::UiItemRect screen{{button.Minimum.X + 8.0F, button.Minimum.Y + 7.0F},
                                       {button.Maximum.X - 8.0F, button.Maximum.Y - 7.0F}};
        ui.DrawRectangle(screen, selected ? theme.Accent : theme.MutedText, selected ? 2.0F : 1.0F, 2.0F);

        const auto point = [&screen](const Keire::Vector2 anchor)
        {
            return Keire::UiPosition{screen.Minimum.X + 3.0F + anchor.X * (screen.Maximum.X - screen.Minimum.X - 6.0F),
                                     screen.Minimum.Y + 3.0F + anchor.Y * (screen.Maximum.Y - screen.Minimum.Y - 6.0F)};
        };
        const auto minimum = point(preset.Minimum);
        const auto maximum = point(preset.Maximum);
        const auto color = selected ? theme.Accent : theme.Text;
        if (ApproximatelyEqual(preset.Minimum, preset.Maximum))
        {
            ui.DrawFilledCircle(minimum, selected ? 3.5F : 3.0F, color);
            return;
        }
        if (std::abs(minimum.X - maximum.X) <= 0.0001F || std::abs(minimum.Y - maximum.Y) <= 0.0001F)
        {
            ui.DrawLine(minimum, maximum, color, selected ? 3.0F : 2.0F);
            ui.DrawFilledCircle(minimum, 2.5F, color);
            ui.DrawFilledCircle(maximum, 2.5F, color);
            return;
        }
        ui.DrawRectangle({minimum, maximum}, color, selected ? 3.0F : 2.0F, 1.0F);
    }

    [[nodiscard]] std::optional<AnchorPresetSelection> DrawAnchorPresetPicker(Keire::UiFrame& ui,
                                                                              const Keire::Vector2 minimum,
                                                                              const Keire::Vector2 maximum,
                                                                              const Keire::UiThemeDefinition& theme)
    {
        std::optional<AnchorPresetSelection> result;
        ui.TextColored(theme.MutedText, "Anchor Preset");
        if (auto combo = ui.BeginCombo("##RectTransformAnchorPreset", AnchorPresetName(minimum, maximum)); combo)
        {
            ui.TextColored(theme.MutedText, "Choose the parent-screen region this element follows.");
            ui.TextColored(theme.MutedText, "Ctrl-click a stretch preset to fit the stretched axes.");
            for (std::size_t index = 0; index < AnchorPresets.size(); ++index)
            {
                const auto& preset = AnchorPresets[index];
                const bool selected =
                    ApproximatelyEqual(minimum, preset.Minimum) && ApproximatelyEqual(maximum, preset.Maximum);
                if (ui.Button("##AnchorPreset" + std::to_string(index), {46.0F, 42.0F}))
                    result = AnchorPresetSelection{preset, ui.ControlDown()};
                const auto state = ui.LastItemState();
                DrawAnchorPresetGlyph(ui, ui.LastItemRect(), preset, selected, theme);
                if (state.Hovered)
                    ui.SetTooltip(preset.Name);
                if (index % 4 != 3)
                    ui.SameLine();
            }
        }
        return result;
    }

    class InspectorPropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        InspectorPropertyEditor(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord> assets,
                                const Keire::Ref<Keire::AssetSystem>& assetSystem,
                                const Keire::Ref<Keire::Scene>& scene, KeireEditor::AssetPicker& assetPicker)
            : m_Ui(ui), m_Assets(assets), m_AssetSystem(assetSystem), m_Scene(scene), m_AssetPicker(assetPicker)
        {
        }

        [[nodiscard]] bool EditBoundary() const noexcept { return m_EditBoundary; }

        bool EditBoolean(const std::string_view label, bool& value) override
        {
            return Track(m_Ui.Checkbox(label, value));
        }

        bool EditInteger(const std::string_view label, std::int64_t& value, const double step,
                         const std::optional<double> minimum, const std::optional<double> maximum) override
        {
            const auto lower =
                minimum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*minimum)) : std::nullopt;
            const auto upper =
                maximum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*maximum)) : std::nullopt;
            return Track(m_Ui.DragInteger(label, value, step, lower, upper));
        }

        bool EditChoice(const std::string_view label, std::int64_t& value,
                        const std::span<const std::string_view> choices) override
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

        bool EditScalar(const std::string_view label, double& value, const double step,
                        const std::optional<double> minimum, const std::optional<double> maximum) override
        {
            return Track(m_Ui.DragScalar(label, value, step, minimum, maximum));
        }

        bool EditText(const std::string_view label, std::string& value) override
        {
            return Track(m_Ui.InputText(label, value));
        }
        bool EditVector2(const std::string_view label, Keire::Vector2& value, const double step) override
        {
            return Track(m_Ui.DragVector2(label, value, static_cast<float>(step)));
        }
        bool EditVector3(const std::string_view label, Keire::Vector3& value, const double step) override
        {
            return Track(m_Ui.DragVector3(label, value, static_cast<float>(step)));
        }
        bool EditVector4(const std::string_view label, Keire::Vector4& value, const double step) override
        {
            return Track(m_Ui.DragVector4(label, value, static_cast<float>(step)));
        }
        bool EditQuaternion(const std::string_view label, Keire::Quaternion& value, const double step) override
        {
            return Track(m_Ui.DragQuaternion(label, value, static_cast<float>(step)));
        }
        bool EditColor(const std::string_view label, Keire::Color& value) override
        {
            Keire::UiColor color{value.Red, value.Green, value.Blue, value.Alpha};
            const bool changed = m_Ui.ColorEdit(label, color);
            (void)Track(changed);
            if (!changed)
                return false;
            value = {color.Red, color.Green, color.Blue, color.Alpha};
            return true;
        }
        bool EditCurve(const std::string_view label, Keire::Curve1D& value) override
        {
            return Track(KeireEditor::AuthoringValueEditors::Curve(m_Ui, label, value));
        }
        bool EditGradient(const std::string_view label, Keire::ColorGradient& value) override
        {
            return Track(KeireEditor::AuthoringValueEditors::Gradient(m_Ui, label, value));
        }
        bool EditAsset(const std::string_view label, Keire::AssetId& value,
                       const std::optional<Keire::AssetTypeId> expectedType) override
        {
            KeireEditor::AssetPickerOptions options;
            options.Label = label;
            options.ExpectedType = expectedType;
            if (m_AssetSystem)
                options.ResolveType = [assets = m_AssetSystem](const Keire::AssetId id)
                { return assets->TryGetType(id); };
            const bool changed = m_AssetPicker.Draw(m_Ui, m_Assets, value, options);
            if (changed)
                m_EditBoundary = true;
            return Track(changed);
        }
        bool EditTextureAsset(const std::string_view label, Keire::AssetId& value,
                              const Keire::ShaderTextureSemantic semantic) override
        {
            KeireEditor::AssetPickerOptions options;
            options.Label = label;
            options.ExpectedType = Keire::Texture2DAsset::StaticType();
            options.Filter = [semantic](const Keire::AssetSourceRecord& record)
            { return KeireEditor::MaterialInspectorPanel::AcceptsTexture(record, semantic); };
            const bool changed = m_AssetPicker.Draw(m_Ui, m_Assets, value, options);
            if (changed)
                m_EditBoundary = true;
            return changed;
        }
        bool EditEntity(const std::string_view label, Keire::EntityId& value) override
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
            return changed;
        }

        bool EditEvent(const std::string_view label, Keire::ComponentEventValue& value,
                       const std::size_t argumentCount) override
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

      private:
        const std::vector<Keire::Entity>& SceneEntities()
        {
            if (!m_EntityCache)
                m_EntityCache = m_Scene ? m_Scene->Entities() : std::vector<Keire::Entity>{};
            return *m_EntityCache;
        }

        bool Track(const bool changed)
        {
            const auto state = m_Ui.LastItemState();
            m_EditBoundary = m_EditBoundary || state.DeactivatedAfterEdit || (changed && !state.Active);
            return changed;
        }

        Keire::UiFrame& m_Ui;
        std::span<const Keire::AssetSourceRecord> m_Assets;
        Keire::Ref<Keire::AssetSystem> m_AssetSystem;
        Keire::Ref<Keire::Scene> m_Scene;
        KeireEditor::AssetPicker& m_AssetPicker;
        std::optional<std::vector<Keire::Entity>> m_EntityCache;
        bool m_EditBoundary = false;
    };
} // namespace

KeireEditor::InspectorPanel::InspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetInspector(std::make_unique<AssetInspectorPanel>(controller)),
      m_AssetPicker(std::make_unique<AssetPicker>())
{
}

KeireEditor::InspectorPanel::~InspectorPanel() = default;

void KeireEditor::InspectorPanel::Draw(Keire::UiFrame& ui)
{
    auto panel = ui.BeginPanel(m_Registration);
    if (!panel)
        return;
    auto& sceneDocument = m_Controller.InspectorSceneDocument();
    auto& propertyDrawers = m_Controller.InspectorPropertyDrawers();
    const auto& theme = m_Controller.InspectorTheme();
    const auto records = m_Controller.InspectorAssetRecords();
    const auto assets = m_Controller.InspectorAssetSystem();
    const auto database = m_Controller.InspectorAssetDatabase();
    const auto scene = sceneDocument.ActiveScene();
    const auto selectedAsset = m_Controller.InspectorSelectedAsset();
    if (!m_Registration.Locked())
    {
        m_LockedEntity = {};
        m_LockedAsset = {};
    }
    else if (!m_LockedEntity && !m_LockedAsset)
    {
        if (scene && sceneDocument.Selection())
            m_LockedEntity = sceneDocument.Selection();
        else
            m_LockedAsset = selectedAsset;
    }
    if (m_Registration.Locked() &&
        ((!m_LockedEntity && !m_LockedAsset) ||
         (m_LockedEntity && (!scene || !scene->FindEntity(Keire::EntityId(m_LockedEntity)))) ||
         (m_LockedAsset && (!database || !database->Find(m_LockedAsset)))))
    {
        m_Registration.SetLocked(false);
        m_LockedEntity = {};
        m_LockedAsset = {};
    }
    const auto inspectedEntity = m_Registration.Locked() ? m_LockedEntity : sceneDocument.Selection();
    const auto inspectedAsset = m_Registration.Locked() ? m_LockedAsset : selectedAsset;
    if (ui.WindowFocused() && scene && inspectedEntity)
        m_Controller.ActivateInspectorHistory();
    ui.TextColored(theme.Accent, "INSPECTOR");
    ui.Separator();
    if (scene && inspectedEntity)
    {
        if (sceneDocument.Selections().size() > 1)
            ui.TextColored(theme.MutedText, std::to_string(sceneDocument.Selections().size()) +
                                                " entities selected; editing the primary selection.");
        auto entity = scene->FindEntity(Keire::EntityId(inspectedEntity));
        if (entity)
        {
            const auto currentName = entity.Name();
            if (m_EntityNameTarget != entity.Id().Value() || !m_EntityNameEditing)
            {
                m_EntityNameTarget = entity.Id().Value();
                m_EntityNameDraft = currentName;
            }
            (void)ui.InputText("Entity Name", m_EntityNameDraft);
            const auto nameState = ui.LastItemState();
            const bool validEntityName = SceneDocument::IsValidEntityName(m_EntityNameDraft);
            if (nameState.DeactivatedAfterEdit)
            {
                if (validEntityName && m_EntityNameDraft != currentName)
                {
                    m_Controller.RecordInspectorUndo();
                    sceneDocument.RenameEntity(entity.Id(), m_EntityNameDraft);
                }
                else if (!validEntityName)
                    m_EntityNameDraft = currentName;
            }
            if (nameState.Active && !validEntityName)
                ui.TextColored(theme.Error, m_EntityNameDraft.empty() ? "Entity name cannot be empty."
                                                                      : "Entity name cannot exceed 256 UTF-8 bytes.");
            m_EntityNameEditing = nameState.Active;
            auto active = entity.ActiveSelf();
            if (ui.Checkbox("Active", active))
            {
                m_Controller.RecordInspectorUndo();
                sceneDocument.SetEntityActive(entity.Id(), active);
            }
            const auto layerNames = m_Controller.InspectorLayerNames();
            auto layer = entity.Layer();
            bool mixedLayers = false;
            if (!m_Registration.Locked() && sceneDocument.Selections().size() > 1)
            {
                for (const auto selected : sceneDocument.Selections())
                {
                    const auto selectedEntity = scene->FindEntity(Keire::EntityId(selected));
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
            if (auto layerCombo = ui.BeginCombo("Layer", layerPreview); layerCombo)
            {
                for (std::uint32_t candidate = 0; candidate < Keire::EntityLayerCount; ++candidate)
                {
                    const auto label = candidate < layerNames.size() && !layerNames[candidate].empty()
                                           ? layerNames[candidate]
                                           : "Layer " + std::to_string(candidate);
                    if (ui.Selectable(label, !mixedLayers && candidate == layer))
                    {
                        m_Controller.RecordInspectorUndo("Change Layer");
                        if (!m_Registration.Locked() && sceneDocument.Selections().size() > 1)
                            sceneDocument.SetEntitiesLayer(sceneDocument.Selections(), candidate);
                        else
                            sceneDocument.SetEntityLayer(entity.Id(), candidate);
                        layer = candidate;
                        mixedLayers = false;
                    }
                }
            }
            ui.Separator();
            const auto expansion = [&](const std::string_view type) -> bool&
            {
                const auto key = entity.Id().ToString() + "." + std::string(type);
                return m_ComponentExpansion.try_emplace(key, true).first->second;
            };
            auto& transformExpanded = expansion("transform");
            const float transformCardHeight =
                transformExpanded ? (ui.ContentAvailable().Width < 325.0F ? 267.0F : 212.0F) : 38.0F;
            if (auto card = ui.BeginChild("TransformCard", {0.0F, transformCardHeight}, true); card)
            {
                if (ui.Selectable(transformExpanded ? "v  TRANSFORM" : ">  TRANSFORM"))
                    transformExpanded = !transformExpanded;
                if (transformExpanded)
                {
                    ui.TextColored(theme.MutedText, "Required | Local space");
                    ui.Separator();
                    const auto transform = entity.GetComponent<Keire::TransformComponent>();
                    auto position = transform->LocalPosition();
                    auto rotation = transform->LocalEulerAngles();
                    auto scale = transform->LocalScale();
                    const bool positionChanged = ui.DragVector3("Position", position, 0.05F);
                    const auto positionState = ui.LastItemState();
                    const bool rotationChanged = ui.DragVector3("Rotation", rotation, 0.25F);
                    const auto rotationState = ui.LastItemState();
                    const auto previousScale = scale;
                    const bool scaleChanged = ui.DragVector3("Scale", scale, 0.01F);
                    const auto scaleState = ui.LastItemState();
                    if (m_UniformScale && scaleChanged)
                    {
                        const auto propagate = [](const float previous, const float current, const float otherPrevious)
                        {
                            if (std::abs(previous) > 0.0001F)
                                return otherPrevious * (current / previous);
                            return otherPrevious + (current - previous);
                        };
                        if (scale.X != previousScale.X)
                        {
                            scale.Y = propagate(previousScale.X, scale.X, previousScale.Y);
                            scale.Z = propagate(previousScale.X, scale.X, previousScale.Z);
                        }
                        else if (scale.Y != previousScale.Y)
                        {
                            scale.X = propagate(previousScale.Y, scale.Y, previousScale.X);
                            scale.Z = propagate(previousScale.Y, scale.Y, previousScale.Z);
                        }
                        else if (scale.Z != previousScale.Z)
                        {
                            scale.X = propagate(previousScale.Z, scale.Z, previousScale.X);
                            scale.Y = propagate(previousScale.Z, scale.Z, previousScale.Y);
                        }
                    }
                    const bool validScale = Keire::TransformComponent::IsValidLocalScale(scale);
                    if (positionChanged)
                    {
                        m_Controller.RecordInspectorUndo("Change Position", "transform.position." +
                                                                                entity.Id().ToString() + "." +
                                                                                std::to_string(m_EditSerial));
                        sceneDocument.SetTransform(entity.Id(), {.Position = position});
                    }
                    if (rotationChanged)
                    {
                        m_Controller.RecordInspectorUndo("Change Rotation", "transform.rotation." +
                                                                                entity.Id().ToString() + "." +
                                                                                std::to_string(m_EditSerial));
                        sceneDocument.SetTransform(entity.Id(), {.EulerDegrees = rotation});
                    }
                    if (scaleChanged && validScale)
                    {
                        m_Controller.RecordInspectorUndo("Change Scale", "transform.scale." + entity.Id().ToString() +
                                                                             "." + std::to_string(m_EditSerial));
                        sceneDocument.SetTransform(entity.Id(), {.Scale = scale});
                    }
                    if (scaleState.Active && !validScale)
                        ui.TextColored(theme.Error,
                                       "Scale axes need magnitude 0.000001 or greater. Finish typing to apply.");
                    if (positionState.DeactivatedAfterEdit || rotationState.DeactivatedAfterEdit ||
                        scaleState.DeactivatedAfterEdit)
                        ++m_EditSerial;
                    ui.Spacing();
                    (void)ui.Checkbox("Uniform scale", m_UniformScale);
                    ui.SameLine();
                    if (ui.Button("Reset"))
                    {
                        m_Controller.RecordInspectorUndo();
                        sceneDocument.SetTransform(entity.Id(), {.Position = Keire::Vector3{},
                                                                 .EulerDegrees = Keire::Vector3{},
                                                                 .Scale = Keire::Vector3{1.0F, 1.0F, 1.0F}});
                    }
                    if (ui.LastItemState().Hovered)
                        ui.SetTooltip("Reset local position, rotation, and scale.");
                }
            }
            if (const auto light = entity.GetComponent<Keire::DirectionalLightComponent>())
            {
                ui.Spacing();
                auto& lightExpanded = expansion("directional-light");
                if (auto card = ui.BeginChild("DirectionalLightCard", {0.0F, lightExpanded ? 360.0F : 38.0F}, true);
                    card)
                {
                    if (ui.Selectable(lightExpanded ? "v  DIRECTIONAL LIGHT" : ">  DIRECTIONAL LIGHT"))
                        lightExpanded = !lightExpanded;
                    if (lightExpanded)
                    {
                        auto enabled = light->Enabled();
                        if (ui.Checkbox("Enabled", enabled))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentEnabled(entity.Id(), light->Type(), enabled);
                        }
                        auto color = light->LightColor();
                        Keire::UiColor editorColor{color.Red, color.Green, color.Blue, color.Alpha};
                        if (ui.ColorEdit("Color", editorColor))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(
                                entity.Id(), light->Type(), "color",
                                Keire::Color{editorColor.Red, editorColor.Green, editorColor.Blue, editorColor.Alpha});
                        }
                        auto intensity = light->Intensity();
                        if (ui.SliderFloat("Intensity", intensity, 0.0F, 100.0F))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "intensity",
                                                               static_cast<double>(intensity));
                        }
                        auto temperature = light->UseColorTemperature();
                        if (ui.Checkbox("Use Color Temperature", temperature))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "useTemperature",
                                                               temperature);
                        }
                        auto kelvin = light->ColorTemperatureKelvin();
                        if (ui.SliderFloat("Temperature (K)", kelvin, 1000.0F, 20000.0F))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "temperature",
                                                               static_cast<double>(kelvin));
                        }
                        const auto shadows = light->Shadows();
                        const auto shadowLabel = shadows == Keire::ShadowQuality::Disabled ? "Disabled"
                                                 : shadows == Keire::ShadowQuality::Hard   ? "Hard"
                                                                                           : "Soft";
                        if (auto shadowMode = ui.BeginCombo("Shadows", shadowLabel); shadowMode)
                        {
                            constexpr std::array modes{Keire::ShadowQuality::Disabled, Keire::ShadowQuality::Hard,
                                                       Keire::ShadowQuality::Soft};
                            constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                            for (std::size_t index = 0; index < modes.size(); ++index)
                            {
                                if (ui.Selectable(labels[index], shadows == modes[index]))
                                {
                                    m_Controller.RecordInspectorUndo();
                                    sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "shadows",
                                                                       static_cast<std::int64_t>(modes[index]));
                                }
                            }
                        }
                        auto shadowStrength = light->ShadowStrength();
                        if (ui.SliderFloat("Shadow Strength", shadowStrength, 0.0F, 1.0F))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "shadowStrength",
                                                               static_cast<double>(shadowStrength));
                        }
                        auto bias = light->ShadowBias();
                        if (ui.SliderFloat("Shadow Bias", bias, 0.0F, 1.0F))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), light->Type(), "shadowBias",
                                                               static_cast<double>(bias));
                        }
                        if (ui.Button("Reset Light"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.ResetComponent(entity.Id(), light->Type());
                        }
                        ui.SameLine();
                        if (ui.Button("Remove Component"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.RemoveComponent(entity.Id(), Keire::DirectionalLightComponent::StaticType());
                        }
                        ui.TextColored(theme.MutedText, "Direct lighting | Directional shadows");
                    }
                }
            }
            if (const auto camera = entity.GetComponent<Keire::CameraComponent>())
            {
                ui.Spacing();
                auto& cameraExpanded = expansion("camera");
                if (auto card = ui.BeginChild("CameraCard", {0.0F, cameraExpanded ? 440.0F : 38.0F}, true); card)
                {
                    if (ui.Selectable(cameraExpanded ? "v  CAMERA" : ">  CAMERA"))
                        cameraExpanded = !cameraExpanded;
                    if (cameraExpanded)
                    {
                        ui.TextColored(theme.MutedText, "Game view | Priority-selected");
                        ui.Separator();
                        auto enabled = camera->Enabled();
                        if (ui.Checkbox("Enabled##Camera", enabled))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentEnabled(entity.Id(), camera->Type(), enabled);
                        }
                        auto primary = camera->Primary();
                        if (ui.Checkbox("Primary", primary))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "primary", primary);
                        }
                        const auto projection = camera->Projection();
                        if (auto combo = ui.BeginCombo("Projection", projection == Keire::CameraProjection::Perspective
                                                                         ? "Perspective"
                                                                         : "Orthographic");
                            combo)
                        {
                            if (ui.Selectable("Perspective", projection == Keire::CameraProjection::Perspective))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "projection",
                                                                   std::int64_t{0});
                            }
                            if (ui.Selectable("Orthographic", projection == Keire::CameraProjection::Orthographic))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "projection",
                                                                   std::int64_t{1});
                            }
                        }
                        const auto clearMode = camera->ClearMode();
                        if (auto combo = ui.BeginCombo(
                                "Background", clearMode == Keire::CameraClearMode::Skybox ? "Skybox" : "Solid Color");
                            combo)
                        {
                            if (ui.Selectable("Skybox", clearMode == Keire::CameraClearMode::Skybox))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "clearMode",
                                                                   std::int64_t{0});
                            }
                            if (ui.Selectable("Solid Color", clearMode == Keire::CameraClearMode::SolidColor))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "clearMode",
                                                                   std::int64_t{1});
                            }
                        }
                        auto priority = camera->Priority();
                        if (ui.SliderInt("Priority", priority, -100, 100))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "priority",
                                                               static_cast<std::int64_t>(priority));
                        }
                        if (camera->Projection() == Keire::CameraProjection::Perspective)
                        {
                            auto fieldOfView = camera->VerticalFieldOfViewDegrees();
                            if (ui.SliderFloat("Vertical FOV", fieldOfView, 1.0F, 179.0F))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "fieldOfView",
                                                                   static_cast<double>(fieldOfView));
                            }
                        }
                        else
                        {
                            auto size = camera->OrthographicSize();
                            if (ui.SliderFloat("Orthographic Size", size, 0.01F, 100.0F))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "orthographicSize",
                                                                   static_cast<double>(size));
                            }
                        }
                        auto nearPlane = camera->NearPlane();
                        auto farPlane = camera->FarPlane();
                        const bool nearChanged =
                            ui.SliderFloat("Near Plane", nearPlane, 0.01F, std::min(farPlane - 0.01F, 100.0F));
                        const bool farChanged =
                            ui.SliderFloat("Far Plane", farPlane, std::max(nearPlane + 0.01F, 1.0F), 10000.0F);
                        if (nearChanged || farChanged)
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "nearPlane",
                                                               static_cast<double>(nearPlane));
                            sceneDocument.SetComponentProperty(entity.Id(), camera->Type(), "farPlane",
                                                               static_cast<double>(farPlane));
                        }
                        if (camera->ClearMode() == Keire::CameraClearMode::SolidColor)
                        {
                            auto clear = camera->ClearColor();
                            Keire::UiColor clearColor{clear.Red, clear.Green, clear.Blue, clear.Alpha};
                            if (ui.ColorEdit("Clear Color", clearColor))
                            {
                                m_Controller.RecordInspectorUndo();
                                sceneDocument.SetComponentProperty(
                                    entity.Id(), camera->Type(), "clearColor",
                                    Keire::Color{clearColor.Red, clearColor.Green, clearColor.Blue, clearColor.Alpha});
                            }
                        }
                        if (ui.Button("Reset Camera"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.ResetComponent(entity.Id(), camera->Type());
                        }
                        ui.SameLine();
                        if (ui.Button("Remove Camera"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.RemoveComponent(entity.Id(), Keire::CameraComponent::StaticType());
                        }
                    }
                }
            }
            if (const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>())
            {
                ui.Spacing();
                auto& rendererExpanded = expansion("mesh-renderer");
                if (auto card = ui.BeginChild("MeshRendererCard", {0.0F, rendererExpanded ? 260.0F : 38.0F}, true);
                    card)
                {
                    if (ui.Selectable(rendererExpanded ? "v  MESH RENDERER" : ">  MESH RENDERER"))
                        rendererExpanded = !rendererExpanded;
                    if (rendererExpanded)
                    {
                        ui.TextColored(theme.MutedText, "Lit geometry submission");
                        ui.Separator();
                        auto enabled = renderer->Enabled();
                        if (ui.Checkbox("Enabled##MeshRenderer", enabled))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentEnabled(entity.Id(), renderer->Type(), enabled);
                        }
                        auto visible = renderer->Visible();
                        if (ui.Checkbox("Visible", visible))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(entity.Id(), renderer->Type(), "visible", visible);
                        }
                        auto tint = renderer->Tint();
                        Keire::UiColor tintColor{tint.Red, tint.Green, tint.Blue, tint.Alpha};
                        if (ui.ColorEdit("Tint", tintColor))
                        {
                            m_Controller.RecordInspectorUndo("Change Tint", "mesh.tint." + entity.Id().ToString() +
                                                                                "." + std::to_string(m_EditSerial));
                            sceneDocument.SetComponentProperty(
                                entity.Id(), renderer->Type(), "tint",
                                Keire::Color{tintColor.Red, tintColor.Green, tintColor.Blue, tintColor.Alpha});
                        }
                        if (ui.LastItemState().DeactivatedAfterEdit)
                            ++m_EditSerial;
                        if (const auto registration = scene->Components()->Find(renderer->Type()))
                        {
                            InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker);
                            for (const auto& property : registration->Properties)
                            {
                                if (property.Key != "mesh")
                                    continue;
                                try
                                {
                                    const auto values = registration->Serialize(*renderer);
                                    const auto found = values.find(property.Key);
                                    if (found == values.end())
                                        throw std::invalid_argument("The Mesh Renderer omitted a declared property.");
                                    auto candidate = found->second;
                                    if (propertyDrawers.Draw(propertyEditor, registration->Type, property, candidate))
                                    {
                                        m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                         "mesh-renderer." + property.Key + "." +
                                                                             entity.Id().ToString());
                                        sceneDocument.SetComponentProperty(entity.Id(), registration->Type,
                                                                           property.Key, std::move(candidate));
                                    }
                                }
                                catch (const std::exception& error)
                                {
                                    ui.TextColored(theme.Error, error.what());
                                }
                            }
                        }
                        InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker);
                        Keire::Ref<const Keire::MeshAsset> mesh =
                            renderer->Mesh() ? Keire::MeshAsset::ResolveBuiltin(renderer->Mesh())
                                             : Keire::MeshAsset::Cube();
                        if (!mesh && assets)
                        {
                            mesh = assets->Load<Keire::MeshAsset>(renderer->Mesh(), Keire::AssetPriority::High)
                                       .TryGetLoaded();
                        }
                        if (mesh)
                        {
                            ui.TextColored(theme.MutedText, "Material Slots");
                            for (std::size_t slot = 0; slot < mesh->MaterialSlots().size(); ++slot)
                            {
                                auto material = renderer->Material(slot);
                                if (!material)
                                    material = mesh->MaterialSlots()[slot].DefaultMaterial;
                                const auto label =
                                    mesh->MaterialSlots()[slot].Name + "##material-slot-" + std::to_string(slot);
                                if (propertyEditor.EditAsset(label, material, Keire::MaterialAsset::StaticType()))
                                {
                                    m_Controller.RecordInspectorUndo("Change Material Slot",
                                                                     "mesh-renderer.material." + std::to_string(slot) +
                                                                         "." + entity.Id().ToString());
                                    sceneDocument.SetMeshRendererMaterial(entity.Id(), slot, material);
                                    m_Controller.NotifyInspectorMaterialAssigned(material);
                                }
                            }
                        }
                        auto castShadows = renderer->CastShadows();
                        if (ui.Checkbox("Cast Shadows", castShadows))
                        {
                            m_Controller.RecordInspectorUndo("Change Shadow Casting");
                            sceneDocument.SetComponentProperty(entity.Id(), renderer->Type(), "castShadows",
                                                               castShadows);
                        }
                        auto receiveShadows = renderer->ReceiveShadows();
                        if (ui.Checkbox("Receive Shadows", receiveShadows))
                        {
                            m_Controller.RecordInspectorUndo("Change Shadow Receiving");
                            sceneDocument.SetComponentProperty(entity.Id(), renderer->Type(), "receiveShadows",
                                                               receiveShadows);
                        }
                        if (ui.Button("Reset Renderer"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.ResetComponent(entity.Id(), renderer->Type());
                        }
                        ui.SameLine();
                        if (ui.Button("Remove Renderer"))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.RemoveComponent(entity.Id(), Keire::MeshRendererComponent::StaticType());
                        }
                    }
                }
            }
            InspectorPropertyEditor propertyEditor(ui, records, assets, scene, *m_AssetPicker);
            for (const auto& component : entity.GetComponents())
            {
                if (!component || component->Type() == Keire::TransformComponent::StaticType() ||
                    component->Type() == Keire::CameraComponent::StaticType() ||
                    component->Type() == Keire::DirectionalLightComponent::StaticType() ||
                    component->Type() == Keire::MeshRendererComponent::StaticType())
                    continue;
                const auto registration = scene->Components()->Find(component->Type());
                if (!registration)
                    continue;
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
                    else if (!assets)
                    {
                        vfxEffectDiagnostic = "The asset system is unavailable; serialized overrides are preserved.";
                        vfxEffectDiagnosticWarning = true;
                    }
                    else
                    {
                        try
                        {
                            vfxEffect =
                                assets->Load<Keire::VfxEffectAsset>(vfxEmitter->Effect(), Keire::AssetPriority::High)
                                    .TryGetLoaded();
                            if (!vfxEffect)
                                vfxEffectDiagnostic = "Loading VFX Blackboard metadata...";
                        }
                        catch (const std::exception& error)
                        {
                            vfxEffectDiagnostic =
                                std::string("VFX Blackboard metadata is unavailable: ") + error.what();
                            vfxEffectDiagnosticWarning = true;
                        }
                    }
                }
                ui.Spacing();
                auto& expanded = expansion(registration->Type.ToString());
                const auto cardId = "ComponentCard##" + registration->Type.ToString();
                const bool rectTransform = registration->Type == Keire::RectTransformComponent::StaticType();
                const float anchorPickerHeight = rectTransform ? 64.0F : 0.0F;
                std::size_t groupRows = 0;
                std::string_view previousGroup;
                for (const auto& property : registration->Properties)
                {
                    if (!property.Group.empty() && property.Group != previousGroup)
                    {
                        ++groupRows;
                        previousGroup = property.Group;
                    }
                }
                const auto vfxEntries = vfxEffect ? KeireEditor::VfxEmitterInspector::VisibleEntryCount(
                                                        vfxEffect->Definition(), vfxEmitter->ParameterOverrides())
                                                  : std::size_t{0};
                const float vfxInspectorHeight =
                    vfxEmitter
                        ? std::min(720.0F, 26.0F + static_cast<float>(std::max<std::size_t>(vfxEntries, 1U)) * 48.0F)
                        : 0.0F;
                const float cardHeight =
                    expanded ? std::max(115.0F, 80.0F + anchorPickerHeight +
                                                    static_cast<float>(registration->Properties.size()) * 34.0F +
                                                    static_cast<float>(groupRows) * 22.0F + vfxInspectorHeight)
                             : 38.0F;
                if (auto card = ui.BeginChild(cardId, {0.0F, cardHeight}, true); card)
                {
                    const auto heading = (expanded ? "v  " : ">  ") + registration->Name;
                    if (ui.Selectable(heading))
                        expanded = !expanded;
                    if (!expanded)
                        continue;
                    auto enabled = component->Enabled();
                    if (ui.Checkbox("Enabled##" + registration->Type.ToString(), enabled))
                    {
                        m_Controller.RecordInspectorUndo("Change " + registration->Name);
                        sceneDocument.SetComponentEnabled(entity.Id(), registration->Type, enabled);
                    }
                    if (rectTransform)
                    {
                        const auto rect = entity.GetComponent<Keire::RectTransformComponent>();
                        if (rect)
                        {
                            if (const auto preset =
                                    DrawAnchorPresetPicker(ui, rect->AnchorMinimum(), rect->AnchorMaximum(), theme))
                            {
                                auto anchoredPosition = rect->AnchoredPosition();
                                auto sizeDelta = rect->SizeDelta();
                                const bool stretchHorizontal =
                                    std::abs(preset->Preset.Minimum.X - preset->Preset.Maximum.X) > 0.0001F;
                                const bool stretchVertical =
                                    std::abs(preset->Preset.Minimum.Y - preset->Preset.Maximum.Y) > 0.0001F;
                                m_Controller.RecordInspectorUndo(
                                    "Change Anchor Preset", "rect-transform.anchor-preset." + entity.Id().ToString());
                                sceneDocument.SetComponentProperty(entity.Id(), registration->Type, "anchorMinimum",
                                                                   preset->Preset.Minimum);
                                sceneDocument.SetComponentProperty(entity.Id(), registration->Type, "anchorMaximum",
                                                                   preset->Preset.Maximum);
                                if (preset->FitStretch && (stretchHorizontal || stretchVertical))
                                {
                                    if (stretchHorizontal)
                                    {
                                        anchoredPosition.X = 0.0F;
                                        sizeDelta.X = 0.0F;
                                    }
                                    if (stretchVertical)
                                    {
                                        anchoredPosition.Y = 0.0F;
                                        sizeDelta.Y = 0.0F;
                                    }
                                    sceneDocument.SetComponentProperty(entity.Id(), registration->Type,
                                                                       "anchoredPosition", anchoredPosition);
                                    sceneDocument.SetComponentProperty(entity.Id(), registration->Type, "sizeDelta",
                                                                       sizeDelta);
                                }
                            }
                        }
                    }
                    std::string activeGroup;
                    Keire::ComponentPropertyBag values;
                    bool serialized = false;
                    try
                    {
                        values = registration->Serialize(*component);
                        serialized = true;
                    }
                    catch (const std::exception& error)
                    {
                        ui.TextColored(theme.Error, error.what());
                    }
                    for (const auto& property : registration->Properties)
                    {
                        if (!serialized)
                            break;
                        if (!property.Group.empty() && property.Group != activeGroup)
                        {
                            activeGroup = property.Group;
                            ui.TextColored(theme.MutedText, activeGroup);
                        }
                        try
                        {
                            const auto found = values.find(property.Key);
                            if (found == values.end())
                                throw std::invalid_argument("The component omitted a declared property.");
                            auto candidate = found->second;
                            bool changed = false;
                            bool editBoundary = false;
                            const bool localLight = registration->Type == Keire::PointLightComponent::StaticType() ||
                                                    registration->Type == Keire::SpotLightComponent::StaticType();
                            const bool vfxOverrides = registration->Type == Keire::VfxEmitterComponent::StaticType() &&
                                                      property.Key == "parameterOverrides";
                            if (vfxOverrides)
                            {
                                if (!vfxEffect)
                                {
                                    ui.TextColored(vfxEffectDiagnosticWarning ? theme.Warning : theme.MutedText,
                                                   vfxEffectDiagnostic);
                                }
                                else
                                {
                                    auto overrides = std::vector<Keire::VfxParameterOverride>(
                                        vfxEmitter->ParameterOverrides().begin(),
                                        vfxEmitter->ParameterOverrides().end());
                                    if (KeireEditor::VfxEmitterInspector::VisibleEntryCount(vfxEffect->Definition(),
                                                                                            overrides) == 0)
                                    {
                                        ui.TextColored(theme.MutedText,
                                                       "This effect has no exposed Blackboard parameters.");
                                    }
                                    bool actionBoundary = false;
                                    KeireEditor::VfxEmitterInspectorCallbacks callbacks;
                                    callbacks.Status = [&ui, &theme](const Keire::AssetId,
                                                                     const std::string_view message, const bool warning)
                                    { ui.TextColored(warning ? theme.Warning : theme.MutedText, message); };
                                    callbacks.Reset = [&ui, &actionBoundary](const Keire::AssetId parameter)
                                    {
                                        ui.SameLine();
                                        auto id = ui.PushId("VfxBlackboardReset-" + parameter.ToString());
                                        if (!ui.Button("Reset"))
                                            return false;
                                        actionBoundary = true;
                                        return true;
                                    };
                                    callbacks.RemoveStale = [&ui, &actionBoundary](const Keire::AssetId parameter)
                                    {
                                        ui.SameLine();
                                        auto id = ui.PushId("VfxBlackboardRemoveStale-" + parameter.ToString());
                                        if (!ui.Button("Remove"))
                                            return false;
                                        actionBoundary = true;
                                        return true;
                                    };
                                    InspectorPropertyEditor vfxPropertyEditor(ui, records, assets, scene,
                                                                              *m_AssetPicker);
                                    changed = KeireEditor::VfxEmitterInspector{}.Draw(
                                        vfxPropertyEditor, vfxEffect->Definition(), overrides, callbacks);
                                    editBoundary = actionBoundary || vfxPropertyEditor.EditBoundary();
                                    if (changed)
                                    {
                                        candidate = KeireEditor::VfxEmitterInspector::SerializeOverrides(
                                            *registration, values, overrides);
                                    }
                                }
                            }
                            else if (localLight && property.Key == "shadows")
                            {
                                const auto* current = std::get_if<std::int64_t>(&candidate);
                                if (!current || *current < 0 || *current > 2)
                                    throw std::invalid_argument("The local-light shadow mode is invalid.");
                                constexpr std::array<std::string_view, 3> labels{"Disabled", "Hard", "Soft"};
                                if (auto shadowMode =
                                        ui.BeginCombo(property.DisplayName, labels[static_cast<std::size_t>(*current)]);
                                    shadowMode)
                                {
                                    for (std::int64_t index = 0; index < static_cast<std::int64_t>(labels.size());
                                         ++index)
                                    {
                                        if (ui.Selectable(labels[static_cast<std::size_t>(index)], *current == index))
                                        {
                                            candidate = index;
                                            changed = true;
                                        }
                                    }
                                }
                            }
                            else
                                changed = propertyDrawers.Draw(propertyEditor, registration->Type, property, candidate);
                            if (!vfxOverrides && !property.Tooltip.empty() && ui.LastItemState().Hovered)
                                ui.SetTooltip(property.Tooltip, {.Delayed = true});
                            if (changed)
                            {
                                m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                 registration->Type.ToString() + "." + property.Key +
                                                                     "." + entity.Id().ToString() + "." +
                                                                     std::to_string(m_EditSerial));
                                sceneDocument.SetComponentProperty(entity.Id(), registration->Type, property.Key,
                                                                   std::move(candidate));
                            }
                            if (changed && (editBoundary || ui.LastItemState().DeactivatedAfterEdit))
                                ++m_EditSerial;
                        }
                        catch (const std::exception& error)
                        {
                            ui.TextColored(theme.Error, error.what());
                        }
                    }
                    if (registration->Removable && ui.Button("Remove " + registration->Name))
                    {
                        m_Controller.RecordInspectorUndo("Remove " + registration->Name);
                        sceneDocument.RemoveComponent(entity.Id(), registration->Type);
                    }
                }
            }
            ui.Spacing();
            if (auto add = ui.BeginCombo("Add Component", "Search components..."); add)
            {
                (void)ui.InputTextWithHint("##ComponentSearch", "Search scripts and components", m_ComponentSearch);
                for (const auto& registration : scene->Components()->Registrations())
                {
                    if (!ContainsCaseInsensitive(registration.Name, m_ComponentSearch) &&
                        !ContainsCaseInsensitive(registration.Category, m_ComponentSearch))
                        continue;
                    const bool canAdd = registration.Removable &&
                                        (registration.AllowMultiple || !entity.HasComponent(registration.Type));
                    if (ui.MenuItem(registration.Category + "/" + registration.Name, false, canAdd))
                    {
                        m_Controller.RecordInspectorUndo();
                        (void)sceneDocument.AddComponent(entity.Id(), registration.Type);
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Drop a C# script here to attach it");
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        for (const auto script : DecodeAssetPayload(payload))
                            m_Controller.AddScriptToEntity(entity.Id(), script);
                    }
                    catch (const std::exception& error)
                    {
                        m_Controller.ReportInspectorAssetError(error.what());
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Object ID");
            ui.Text(entity.Id().ToString());
            return;
        }
        sceneDocument.ClearSelection();
    }
    m_AssetInspector->Draw(ui, inspectedAsset, m_Registration.Locked());
}

KeireEditor::AssetInspectorPanel::AssetInspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetPicker(std::make_unique<AssetPicker>()),
      m_ManagedDataInspector(std::make_unique<ManagedDataInspectorPanel>(controller))
{
}

KeireEditor::AssetInspectorPanel::~AssetInspectorPanel() = default;

void KeireEditor::AssetInspectorPanel::ClearState() noexcept
{
    m_EditingAsset = {};
    m_AssetName.clear();
    m_ManagedDataInspector->Clear();
}

void KeireEditor::AssetInspectorPanel::Draw(Keire::UiFrame& ui, Keire::AssetId selectedAsset, const bool pinned)
{
    auto& inputDocument = m_Controller.InspectorInputDocument();
    auto& materialDocument = m_Controller.InspectorMaterialDocument();
    const auto& theme = m_Controller.InspectorTheme();
    const auto database = m_Controller.InspectorAssetDatabase();
    const auto assets = m_Controller.InspectorAssetSystem();
    const auto records = m_Controller.InspectorAssetRecords();
    const auto assetStatus = m_Controller.InspectorAssetStatus();
    const auto scene = m_Controller.InspectorSceneDocument().ActiveScene();
    if (!selectedAsset || !database)
    {
        ui.Text("Nothing selected");
        ui.TextColored(theme.MutedText, "Select an asset in the Project panel.");
        return;
    }
    const auto record = database->Find(selectedAsset);
    if (!record)
    {
        selectedAsset = {};
        if (!pinned)
            m_Controller.SetInspectorSelectedAsset({});
        ui.TextColored(theme.Warning, "The selected asset no longer exists.");
        return;
    }
    if (m_EditingAsset != record->Id)
    {
        m_EditingAsset = record->Id;
        m_AssetName = record->RelativePath.filename().string();
    }
    ui.Text(record->RelativePath.generic_string());
    ui.TextColored(theme.MutedText, "Asset ID");
    ui.Text(record->Id.ToString());
    ui.TextColored(theme.MutedText, "Importer");
    ui.Text(record->Importer + " v" + std::to_string(record->ImporterVersion));
    ui.TextColored(theme.MutedText, "Content SHA-256");
    ui.Text(record->SourceDigest);
    if (record->RelativePath.extension() == ".keireinput")
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "INPUT ACTION ASSET");
        ui.Text("Action maps, bindings, control schemes, and runtime overrides.");
        if (ui.Button("Edit Input Actions"))
        {
            try
            {
                if (inputDocument.Dirty() && inputDocument.Asset() != record->Id)
                    throw std::runtime_error("Save or Revert the currently edited input asset before switching.");
                m_Controller.OpenInspectorInputActions(record->Id);
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Input editor failed to open: ") + error.what());
            }
        }
    }
    else if (record->Type == Keire::AudioClipAsset::StaticType())
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "AUDIO CLIP");
        if (assets)
        {
            const auto handle = assets->Load<Keire::AudioClipAsset>(record->Id, Keire::AssetPriority::High);
            if (const auto clip = handle.TryGetLoaded())
            {
                const auto& data = *clip->Clip();
                ui.Text("Duration: " + std::to_string(clip->DurationSeconds()) + " seconds");
                ui.Text("Sample rate: " + std::to_string(data.SampleRate) + " Hz");
                ui.Text("Channels: " + std::to_string(data.Channels));
                ui.Text("Frames: " + std::to_string(clip->FrameCount()));
                ui.TextColored(theme.MutedText,
                               data.Streaming ? "Storage: streamed encoded source" : "Storage: resident PCM");
            }
            else
            {
                ui.TextColored(theme.MutedText, "Loading decoded audio metadata...");
            }
        }
        if (ui.Button("Preview"))
        {
            try
            {
                m_Controller.PreviewInspectorAudio(record->Id);
                m_Controller.SetInspectorAssetStatus("Playing audio preview through the EditorPreview bus.");
            }
            catch (const std::exception& error)
            {
                m_Controller.ReportInspectorAssetError(std::string("Audio preview failed: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Stop Preview"))
            m_Controller.StopInspectorAudioPreview();
        ui.SameLine();
        if (ui.Button("Reimport Audio"))
            m_Controller.ImportInspectorAssets();
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    else if (record->Type == Keire::ManagedDataAsset::StaticType())
    {
        m_ManagedDataInspector->Draw(ui, *record);
    }
    else if (record->RelativePath.extension() == ".keireshader")
    {
        ui.Separator();
        const auto importStatus = database->ImportStatus(record->Id);
        if (importStatus.State == Keire::AssetImportState::Failed)
        {
            ui.TextColored(theme.Error, "SHADER IMPORT FAILED");
            ui.TextColored(theme.Warning, "The last-good compiled revision remains active when available.");
            ui.Separator();
            ui.TextColored(theme.MutedText, "Compiler diagnostics");
            constexpr std::size_t maximumVisibleDiagnostics = 64;
            const auto visibleDiagnostics = std::min(importStatus.Diagnostics.size(), maximumVisibleDiagnostics);
            for (std::size_t index = 0; index < visibleDiagnostics; ++index)
            {
                const auto& diagnostic = importStatus.Diagnostics[index];
                const auto color = diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error     ? theme.Error
                                   : diagnostic.Severity == Keire::AssetDiagnosticSeverity::Warning ? theme.Warning
                                                                                                    : theme.MutedText;
                ui.TextColored(color, FormatAssetDiagnostic(diagnostic));
            }
            if (importStatus.Diagnostics.size() > visibleDiagnostics)
                ui.TextColored(theme.MutedText, std::to_string(importStatus.Diagnostics.size() - visibleDiagnostics) +
                                                    " additional diagnostic(s) are available in the log file.");
        }
        else
        {
            ui.TextColored(theme.Accent, "SHADER");
            ui.TextColored(importStatus.State == Keire::AssetImportState::NotImported ? theme.Warning : theme.Success,
                           importStatus.State == Keire::AssetImportState::NotImported ? "Waiting for first import"
                                                                                      : "Imported graphics shader");
        }
        ui.Text("Stages: Vertex, Fragment");
        ui.Text("Variants: DXIL, SPIR-V, MSL");
        ui.TextColored(theme.MutedText, "Source dependencies");
        if (record->SourceDependencies.empty())
            ui.Text("No dependency records are available; reimport to refresh.");
        for (const auto& dependency : record->SourceDependencies)
            ui.Text(dependency.RelativePath.generic_string() + "  " + dependency.Digest.substr(0, 12));
        if (ui.Button("Reimport Shader"))
            m_Controller.ImportInspectorAssets();
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    else if (record->RelativePath.extension() == ".keirematerial")
    {
        ui.Separator();
        ui.TextColored(theme.Accent, "MATERIAL");
        ui.Text("Shader-driven texture assignments");
        try
        {
            const auto sourceRoot = database->Specification().ProjectRoot / database->Specification().SourceDirectory;
            const auto resolveShader = [&](const Keire::AssetId shader) -> std::optional<Keire::ShaderAssetDefinition>
            {
                const auto shaderRecord = database->Find(shader);
                if (!shaderRecord || shaderRecord->Type != Keire::ShaderAsset::StaticType())
                    return std::nullopt;
                try
                {
                    return Keire::ShaderAsset::DecodeManifest(ReadBytes(sourceRoot / shaderRecord->RelativePath));
                }
                catch (...)
                {
                    return std::nullopt;
                }
            };

            const auto sourcePath = sourceRoot / record->RelativePath;
            if (!materialDocument.IsOpen(record->Id))
            {
                m_Controller.CommitInspectorMaterial();
                const auto source = ReadBytes(sourcePath);
                materialDocument.OpenAsset(record->Id, sourcePath, source, resolveShader);
            }
            else
                materialDocument.Open(materialDocument.DraftSource(), resolveShader);
            auto& document = materialDocument;
            InspectorPropertyEditor editor(ui, records, assets, scene, *m_AssetPicker);
            bool changed = false;
            auto shader = document.Shader();
            if (editor.EditAsset("Shader", shader, Keire::ShaderAsset::StaticType()))
                changed = document.SetShader(shader, resolveShader) || changed;

            if (document.Properties().empty())
                ui.TextColored(theme.MutedText, "The selected shader declares no material properties.");
            else
            {
                ui.Separator();
                ui.TextColored(theme.MutedText, "SHADER PROPERTIES");
                changed = KeireEditor::MaterialInspectorPanel{}.Draw(editor, document) || changed;
            }
            if (changed)
            {
                document.CaptureDraft();
                if (assets)
                {
                    (void)assets->PublishDevelopmentAsset(
                        record->Id, Keire::CreateRef<Keire::MaterialAsset>(document.Definition()));
                }
                m_Controller.SetInspectorAssetStatus("Previewing material changes live.");
            }
            if (editor.EditBoundary())
                m_Controller.CommitInspectorMaterial();
            ui.TextColored(theme.MutedText,
                           "Names, ranges, categories, texture semantics, and defaults come from the shader.");
        }
        catch (const std::exception& error)
        {
            ui.TextColored(theme.Error, std::string("Material editor unavailable: ") + error.what());
        }
        ui.TextColored(theme.MutedText, "Invalid shaders resolve to the error material at runtime.");
        if (ui.Button("Reimport Material"))
        {
            m_Controller.CommitInspectorMaterial();
            m_Controller.ImportInspectorAssets();
        }
        if (!assetStatus.empty())
            ui.TextColored(theme.MutedText, assetStatus);
    }
    ui.Separator();
    (void)ui.InputText("Name", m_AssetName);
    if (ui.Button("Rename") && !m_AssetName.empty())
    {
        try
        {
            m_Controller.RenameInspectorAsset(record->Id, m_AssetName);
            m_Controller.SetInspectorAssetStatus("Renamed asset and preserved its metadata identity.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset rename failed: ") + error.what());
        }
    }
    ui.SameLine();
    if (ui.Button("Duplicate"))
    {
        try
        {
            const auto stem = record->RelativePath.stem().string();
            const auto extension = record->RelativePath.extension().string();
            auto copyName = stem;
            copyName.append(" Copy").append(extension);
            auto destination = record->RelativePath.parent_path() / copyName;
            for (std::size_t copy = 2; database->Find(destination); ++copy)
            {
                copyName = stem;
                copyName.append(" Copy ").append(std::to_string(copy)).append(extension);
                destination = record->RelativePath.parent_path() / copyName;
            }
            m_Controller.DuplicateInspectorAsset(record->Id, destination);
            m_Controller.SetInspectorAssetStatus("Duplicating asset in the isolated asset worker.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset duplication failed: ") + error.what());
        }
    }
    ui.SameLine();
    if (ui.Button("Move to Trash"))
    {
        try
        {
            m_Controller.TrashInspectorAsset(record->Id);
            selectedAsset = {};
            if (!pinned)
                m_Controller.SetInspectorSelectedAsset({});
            m_EditingAsset = {};
            m_Controller.SetInspectorAssetStatus("Moving asset to recoverable trash in the isolated asset worker.");
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Asset trash operation failed: ") + error.what());
        }
    }
}
