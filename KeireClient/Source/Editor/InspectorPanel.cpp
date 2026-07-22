#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
namespace
{
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

    class InspectorPropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        InspectorPropertyEditor(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord> assets,
                                const Keire::Ref<Keire::Scene>& scene)
            : m_Ui(ui), m_Assets(assets), m_Scene(scene)
        {
        }

        [[nodiscard]] bool EditBoundary() const noexcept { return m_EditBoundary; }

        bool EditBoolean(const std::string_view label, bool& value) override { return m_Ui.Checkbox(label, value); }

        bool EditInteger(const std::string_view label, std::int64_t& value, const double step,
                         const std::optional<double> minimum, const std::optional<double> maximum) override
        {
            const auto lower =
                minimum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*minimum)) : std::nullopt;
            const auto upper =
                maximum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*maximum)) : std::nullopt;
            return m_Ui.DragInteger(label, value, step, lower, upper);
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
            return changed;
        }

        bool EditScalar(const std::string_view label, double& value, const double step,
                        const std::optional<double> minimum, const std::optional<double> maximum) override
        {
            return Track(m_Ui.DragScalar(label, value, step, minimum, maximum));
        }

        bool EditText(const std::string_view label, std::string& value) override
        {
            return m_Ui.InputText(label, value);
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
            return m_Ui.DragQuaternion(label, value, static_cast<float>(step));
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
        bool EditAsset(const std::string_view label, Keire::AssetId& value,
                       const std::optional<Keire::AssetTypeId> expectedType) override
        {
            const auto selected = std::ranges::find(m_Assets, value, &Keire::AssetSourceRecord::Id);
            const auto preview = selected == m_Assets.end() ? (value ? "Missing asset" : "None")
                                                            : selected->RelativePath.filename().string();
            bool changed = false;
            if (auto combo = m_Ui.BeginCombo(label, preview); combo)
            {
                if (m_Ui.Selectable("None", !value))
                {
                    value = {};
                    changed = true;
                }
                for (const auto& asset : m_Assets)
                {
                    if (expectedType && asset.Type != *expectedType)
                        continue;
                    if (m_Ui.Selectable(asset.RelativePath.generic_string(), asset.Id == value))
                    {
                        value = asset.Id;
                        changed = true;
                    }
                }
            }
            if (changed)
                m_EditBoundary = true;
            return changed;
        }
        bool EditTextureAsset(const std::string_view label, Keire::AssetId& value,
                              const Keire::ShaderTextureSemantic semantic) override
        {
            const auto selected = std::ranges::find(m_Assets, value, &Keire::AssetSourceRecord::Id);
            const auto compatible =
                selected == m_Assets.end() || KeireEditor::MaterialInspectorPanel::AcceptsTexture(*selected, semantic);
            const auto preview = selected == m_Assets.end() ? (value ? "Missing texture" : "None")
                                 : compatible               ? selected->RelativePath.filename().string()
                                                            : "Incompatible texture";
            bool changed = false;
            if (auto combo = m_Ui.BeginCombo(label, preview); combo)
            {
                if (m_Ui.Selectable("None", !value))
                {
                    value = {};
                    changed = true;
                }
                for (const auto& asset : m_Assets)
                {
                    if (!KeireEditor::MaterialInspectorPanel::AcceptsTexture(asset, semantic))
                        continue;
                    if (m_Ui.Selectable(asset.RelativePath.generic_string(), asset.Id == value))
                    {
                        value = asset.Id;
                        changed = true;
                    }
                }
            }
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
                    for (const auto& entity : m_Scene->Entities())
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

      private:
        bool Track(const bool changed)
        {
            const auto state = m_Ui.LastItemState();
            m_EditBoundary = m_EditBoundary || state.DeactivatedAfterEdit || (changed && !state.Active);
            return changed;
        }

        Keire::UiFrame& m_Ui;
        std::span<const Keire::AssetSourceRecord> m_Assets;
        Keire::Ref<Keire::Scene> m_Scene;
        bool m_EditBoundary = false;
    };
} // namespace

KeireEditor::InspectorPanel::InspectorPanel(IInspectorController& controller)
    : m_Controller(controller), m_AssetInspector(std::make_unique<AssetInspectorPanel>(controller))
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
    const auto scene = sceneDocument.ActiveScene();
    if (ui.WindowFocused() && scene && sceneDocument.Selection())
        m_Controller.ActivateInspectorHistory();
    ui.TextColored(theme.Accent, "INSPECTOR");
    ui.Separator();
    if (scene && sceneDocument.Selection())
    {
        if (sceneDocument.Selections().size() > 1)
            ui.TextColored(theme.MutedText, std::to_string(sceneDocument.Selections().size()) +
                                                " entities selected; editing the primary selection.");
        auto entity = scene->FindEntity(Keire::EntityId(sceneDocument.Selection()));
        if (entity)
        {
            auto name = entity.Name();
            if (ui.InputText("Entity Name", name))
            {
                m_Controller.RecordInspectorUndo();
                sceneDocument.RenameEntity(entity.Id(), std::move(name));
            }
            auto active = entity.ActiveSelf();
            if (ui.Checkbox("Active", active))
            {
                m_Controller.RecordInspectorUndo();
                sceneDocument.SetEntityActive(entity.Id(), active);
            }
            ui.Separator();
            const auto expansion = [&](const std::string_view type) -> bool&
            {
                const auto key = entity.Id().ToString() + "." + std::string(type);
                return m_ComponentExpansion.try_emplace(key, true).first->second;
            };
            auto& transformExpanded = expansion("transform");
            const float transformCardHeight =
                transformExpanded ? (ui.ContentAvailable().Width < 325.0F ? 245.0F : 190.0F) : 38.0F;
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
                    if (scaleChanged)
                    {
                        m_Controller.RecordInspectorUndo("Change Scale", "transform.scale." + entity.Id().ToString() +
                                                                             "." + std::to_string(m_EditSerial));
                        sceneDocument.SetTransform(entity.Id(), {.Scale = scale});
                    }
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
                        ui.TextColored(theme.MutedText, "Direct Lambert lighting | Shadows deferred");
                    }
                }
            }
            if (const auto camera = entity.GetComponent<Keire::CameraComponent>())
            {
                ui.Spacing();
                auto& cameraExpanded = expansion("camera");
                if (auto card = ui.BeginChild("CameraCard", {0.0F, cameraExpanded ? 405.0F : 38.0F}, true); card)
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
                        auto clear = camera->ClearColor();
                        Keire::UiColor clearColor{clear.Red, clear.Green, clear.Blue, clear.Alpha};
                        if (ui.ColorEdit("Clear Color", clearColor))
                        {
                            m_Controller.RecordInspectorUndo();
                            sceneDocument.SetComponentProperty(
                                entity.Id(), camera->Type(), "clearColor",
                                Keire::Color{clearColor.Red, clearColor.Green, clearColor.Blue, clearColor.Alpha});
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
                            InspectorPropertyEditor propertyEditor(ui, records, scene);
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
                        InspectorPropertyEditor propertyEditor(ui, records, scene);
                        if (assets && renderer->Mesh())
                        {
                            const auto mesh =
                                assets->Load<Keire::MeshAsset>(renderer->Mesh(), Keire::AssetPriority::High)
                                    .TryGetLoaded();
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
                                        m_Controller.RecordInspectorUndo(
                                            "Change Material Slot", "mesh-renderer.material." + std::to_string(slot) +
                                                                        "." + entity.Id().ToString());
                                        sceneDocument.SetMeshRendererMaterial(entity.Id(), slot, material);
                                    }
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
            InspectorPropertyEditor propertyEditor(ui, records, scene);
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
                ui.Spacing();
                auto& expanded = expansion(registration->Type.ToString());
                const auto cardId = "ComponentCard##" + registration->Type.ToString();
                const float cardHeight =
                    expanded ? std::max(115.0F, 80.0F + registration->Properties.size() * 34.0F) : 38.0F;
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
                    std::string activeGroup;
                    for (const auto& property : registration->Properties)
                    {
                        if (!property.Group.empty() && property.Group != activeGroup)
                        {
                            activeGroup = property.Group;
                            ui.TextColored(theme.MutedText, activeGroup);
                        }
                        try
                        {
                            const auto values = registration->Serialize(*component);
                            const auto found = values.find(property.Key);
                            if (found == values.end())
                                throw std::invalid_argument("The component omitted a declared property.");
                            auto candidate = found->second;
                            const bool changed =
                                propertyDrawers.Draw(propertyEditor, registration->Type, property, candidate);
                            if (changed)
                            {
                                m_Controller.RecordInspectorUndo("Change " + property.DisplayName,
                                                                 registration->Type.ToString() + "." + property.Key +
                                                                     "." + entity.Id().ToString() + "." +
                                                                     std::to_string(m_EditSerial));
                                sceneDocument.SetComponentProperty(entity.Id(), registration->Type, property.Key,
                                                                   std::move(candidate));
                            }
                            if (changed && ui.LastItemState().DeactivatedAfterEdit)
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
                for (const auto& registration : scene->Components()->Registrations())
                {
                    const bool canAdd = registration.Removable &&
                                        (registration.AllowMultiple || !entity.HasComponent(registration.Type));
                    if (ui.MenuItem(registration.Category + "/" + registration.Name, false, canAdd))
                    {
                        m_Controller.RecordInspectorUndo();
                        (void)sceneDocument.AddComponent(entity.Id(), registration.Type);
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Object ID");
            ui.Text(entity.Id().ToString());
            return;
        }
        sceneDocument.ClearSelection();
    }
    m_AssetInspector->Draw(ui);
}

void KeireEditor::AssetInspectorPanel::Draw(Keire::UiFrame& ui)
{
    auto& inputDocument = m_Controller.InspectorInputDocument();
    auto& materialDocument = m_Controller.InspectorMaterialDocument();
    const auto& theme = m_Controller.InspectorTheme();
    const auto database = m_Controller.InspectorAssetDatabase();
    const auto assets = m_Controller.InspectorAssetSystem();
    const auto records = m_Controller.InspectorAssetRecords();
    auto selectedAsset = m_Controller.InspectorSelectedAsset();
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
            InspectorPropertyEditor editor(ui, records, scene);
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
            auto destination = record->RelativePath.parent_path() / (stem + " Copy" + extension);
            for (std::size_t copy = 2; database->Find(destination); ++copy)
                destination = record->RelativePath.parent_path() / (stem + " Copy " + std::to_string(copy) + extension);
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
