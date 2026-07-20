#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"

#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    const Keire::UiLayoutInfo* ActiveLayout(const std::vector<Keire::UiLayoutInfo>& layouts)
    {
        const auto found = std::ranges::find(layouts, true, &Keire::UiLayoutInfo::Active);
        return found == layouts.end() ? nullptr : &*found;
    }

    const Keire::UiThemeInfo* ActiveTheme(const std::vector<Keire::UiThemeInfo>& themes)
    {
        const auto found = std::ranges::find(themes, true, &Keire::UiThemeInfo::Active);
        return found == themes.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open input action asset: " + path.string());
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

    template <typename Range, typename Projection>
    [[nodiscard]] std::string UniqueInputName(const Range& values, std::string base, Projection projection)
    {
        std::string candidate = base;
        for (std::size_t copy = 2; std::ranges::any_of(values, [&](const auto& value)
                                                       { return std::invoke(projection, value) == candidate; });
             ++copy)
            candidate = base + " " + std::to_string(copy);
        return candidate;
    }

    void WriteBytesAtomically(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        const std::string text =
            bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(path, text);
    }

    [[nodiscard]] Keire::Ref<Keire::Scene> RenderedScene(const Keire::Ref<Keire::Scene>& editing,
                                                         const Keire::Ref<Keire::SceneRuntimeSession>& play)
    {
        if (play && play->State() != Keire::ScenePlayState::Stopped)
            return play->RuntimeScene();
        return editing;
    }

    struct SceneCamera final
    {
        Keire::Entity Entity;
        Keire::Ref<Keire::CameraComponent> Camera;
        Keire::Ref<Keire::TransformComponent> Transform;
    };

    [[nodiscard]] std::optional<SceneCamera> SelectGameCamera(const Keire::Ref<Keire::Scene>& scene)
    {
        if (!scene)
            return std::nullopt;
        std::optional<SceneCamera> selected;
        bool selectedPrimary = false;
        for (const auto& entity : scene->Query<Keire::CameraComponent>())
        {
            const auto camera = entity.GetComponent<Keire::CameraComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (!selected || (camera->Primary() && !selectedPrimary) ||
                (camera->Primary() == selectedPrimary && (camera->Priority() > selected->Camera->Priority() ||
                                                          (camera->Priority() == selected->Camera->Priority() &&
                                                           entity.Id().Value() < selected->Entity.Id().Value()))))
            {
                selected = SceneCamera{entity, camera, transform};
                selectedPrimary = camera->Primary();
            }
        }
        return selected;
    }

    [[nodiscard]] Keire::UiSize PrepareRenderSurface(const Keire::Ref<Keire::RenderView>& view,
                                                     const Keire::UiSize logicalSize, const float displayScale)
    {
        if (!view || !view->Surface())
            return {};
        const float width = std::max(logicalSize.Width, 1.0F);
        const float height = std::max(logicalSize.Height, 1.0F);
        const auto pixelWidth =
            static_cast<std::uint32_t>(std::round(std::clamp(width * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        const auto pixelHeight =
            static_cast<std::uint32_t>(std::round(std::clamp(height * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        view->Surface()->RequestSize(pixelWidth, pixelHeight);
        return {width, height};
    }

    struct SceneBounds
    {
        Keire::Vector3 Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()};
        Keire::Vector3 Maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::lowest()};
        bool Valid = false;

        void Include(const Keire::Vector3 point) noexcept
        {
            Minimum.X = std::min(Minimum.X, point.X);
            Minimum.Y = std::min(Minimum.Y, point.Y);
            Minimum.Z = std::min(Minimum.Z, point.Z);
            Maximum.X = std::max(Maximum.X, point.X);
            Maximum.Y = std::max(Maximum.Y, point.Y);
            Maximum.Z = std::max(Maximum.Z, point.Z);
            Valid = true;
        }

        [[nodiscard]] Keire::Vector3 Center() const noexcept
        {
            return {(Minimum.X + Maximum.X) * 0.5F, (Minimum.Y + Maximum.Y) * 0.5F, (Minimum.Z + Maximum.Z) * 0.5F};
        }

        [[nodiscard]] float Radius() const noexcept
        {
            const auto center = Center();
            const float x = Maximum.X - center.X;
            const float y = Maximum.Y - center.Y;
            const float z = Maximum.Z - center.Z;
            return std::max(std::sqrt(x * x + y * y + z * z), 0.25F);
        }
    };

    void IncludeEntityBounds(const Keire::Entity entity, SceneBounds& bounds)
    {
        if (!entity)
            return;
        if (const auto transform = entity.GetComponent<Keire::TransformComponent>())
        {
            const auto world = transform->WorldMatrix();
            const float extent = entity.HasComponent<Keire::MeshRendererComponent>() ? 0.5F : 0.25F;
            for (const float x : {-extent, extent})
            {
                for (const float y : {-extent, extent})
                {
                    for (const float z : {-extent, extent})
                        bounds.Include(Keire::Math::TransformPoint(world, {x, y, z}));
                }
            }
        }
        for (const auto child : entity.Children())
            IncludeEntityBounds(child, bounds);
    }

    [[nodiscard]] Keire::Vector3 Unproject(const Keire::Matrix4& inverseViewProjection, const float x, const float y,
                                           const float z)
    {
        const auto& value = inverseViewProjection.Elements;
        const float resultX = value[0] * x + value[4] * y + value[8] * z + value[12];
        const float resultY = value[1] * x + value[5] * y + value[9] * z + value[13];
        const float resultZ = value[2] * x + value[6] * y + value[10] * z + value[14];
        const float resultW = value[3] * x + value[7] * y + value[11] * z + value[15];
        if (std::abs(resultW) <= 0.000001F)
            throw std::runtime_error("Scene picking produced an invalid homogeneous position.");
        return {resultX / resultW, resultY / resultW, resultZ / resultW};
    }

    [[nodiscard]] Keire::Vector3 NormalizedDirection(const Keire::Vector3 from, const Keire::Vector3 to)
    {
        const Keire::Vector3 difference{to.X - from.X, to.Y - from.Y, to.Z - from.Z};
        const float length =
            std::sqrt(difference.X * difference.X + difference.Y * difference.Y + difference.Z * difference.Z);
        if (length <= 0.000001F)
            throw std::runtime_error("Scene picking produced a zero-length ray.");
        return {difference.X / length, difference.Y / length, difference.Z / length};
    }

    [[nodiscard]] std::optional<float> IntersectUnitCube(const Keire::Matrix4& world, const Keire::Vector3 rayOrigin,
                                                         const Keire::Vector3 rayDirection)
    {
        const auto inverse = Keire::Math::Inverse(world);
        const auto origin = Keire::Math::TransformPoint(inverse, rayOrigin);
        const auto direction = Keire::Math::TransformDirection(inverse, rayDirection);
        float minimum = 0.0F;
        float maximum = std::numeric_limits<float>::max();
        const auto testAxis = [&](const float position, const float axisDirection)
        {
            if (std::abs(axisDirection) <= 0.000001F)
                return position >= -0.5F && position <= 0.5F;
            float first = (-0.5F - position) / axisDirection;
            float second = (0.5F - position) / axisDirection;
            if (first > second)
                std::swap(first, second);
            minimum = std::max(minimum, first);
            maximum = std::min(maximum, second);
            return maximum >= minimum;
        };
        if (!testAxis(origin.X, direction.X) || !testAxis(origin.Y, direction.Y) || !testAxis(origin.Z, direction.Z))
            return std::nullopt;
        return minimum;
    }

    [[nodiscard]] Keire::Entity PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, const Keire::UiItemRect rect,
                                                const Keire::UiPosition pointer, const Keire::RenderCamera& camera)
    {
        if (!scene || !rect.Contains(pointer))
            return {};
        const auto size = rect.Size();
        if (size.Width <= 1.0F || size.Height <= 1.0F)
            return {};
        const float x = ((pointer.X - rect.Minimum.X) / size.Width) * 2.0F - 1.0F;
        const float y = 1.0F - ((pointer.Y - rect.Minimum.Y) / size.Height) * 2.0F;
        const auto inverse = Keire::Math::Inverse(Keire::Math::Multiply(camera.Projection, camera.View));
        const auto nearPoint = Unproject(inverse, x, y, 0.0F);
        const auto farPoint = Unproject(inverse, x, y, 1.0F);
        const auto direction = NormalizedDirection(nearPoint, farPoint);
        float closest = std::numeric_limits<float>::max();
        Keire::Entity selected;
        for (const auto& entity : scene->Query<Keire::MeshRendererComponent>())
        {
            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!renderer || !renderer->Enabled() || !renderer->Visible() || !transform || !entity.ActiveInHierarchy())
                continue;
            const auto distance = IntersectUnitCube(transform->WorldMatrix(), nearPoint, direction);
            if (distance && *distance < closest)
            {
                closest = *distance;
                selected = entity;
            }
        }
        return selected;
    }

    class ContinuousUndoCommand final : public Keire::UndoCommand
    {
      public:
        ContinuousUndoCommand(std::string name, std::string mergeKey, Keire::UndoOperation redo,
                              Keire::UndoOperation undo, const std::size_t estimatedBytes,
                              Keire::UndoAvailability available)
            : m_Name(std::move(name)), m_MergeKey(std::move(mergeKey)), m_Redo(std::move(redo)),
              m_Undo(std::move(undo)), m_EstimatedBytes(std::max<std::size_t>(estimatedBytes, 1)),
              m_Available(std::move(available))
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_EstimatedBytes; }
        [[nodiscard]] bool Available() const noexcept override
        {
            try
            {
                return !m_Available || m_Available();
            }
            catch (...)
            {
                return false;
            }
        }
        void Redo() override { m_Redo(); }
        void Undo() override { m_Undo(); }
        [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
        {
            const auto* command = dynamic_cast<const ContinuousUndoCommand*>(&newer);
            return command && !m_MergeKey.empty() && command->m_MergeKey == m_MergeKey;
        }

      private:
        std::string m_Name;
        std::string m_MergeKey;
        Keire::UndoOperation m_Redo;
        Keire::UndoOperation m_Undo;
        std::size_t m_EstimatedBytes = 1;
        Keire::UndoAvailability m_Available;
    };

    class InspectorPropertyEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        InspectorPropertyEditor(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord> assets,
                                const Keire::Ref<Keire::Scene>& scene)
            : m_Ui(ui), m_Assets(assets), m_Scene(scene)
        {
        }

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
            return m_Ui.DragScalar(label, value, step, minimum, maximum);
        }

        bool EditText(const std::string_view label, std::string& value) override
        {
            return m_Ui.InputText(label, value);
        }
        bool EditVector2(const std::string_view label, Keire::Vector2& value, const double step) override
        {
            return m_Ui.DragVector2(label, value, static_cast<float>(step));
        }
        bool EditVector3(const std::string_view label, Keire::Vector3& value, const double step) override
        {
            return m_Ui.DragVector3(label, value, static_cast<float>(step));
        }
        bool EditVector4(const std::string_view label, Keire::Vector4& value, const double step) override
        {
            return m_Ui.DragVector4(label, value, static_cast<float>(step));
        }
        bool EditQuaternion(const std::string_view label, Keire::Quaternion& value, const double step) override
        {
            return m_Ui.DragQuaternion(label, value, static_cast<float>(step));
        }
        bool EditColor(const std::string_view label, Keire::Color& value) override
        {
            Keire::UiColor color{value.Red, value.Green, value.Blue, value.Alpha};
            if (!m_Ui.ColorEdit(label, color))
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
        Keire::UiFrame& m_Ui;
        std::span<const Keire::AssetSourceRecord> m_Assets;
        Keire::Ref<Keire::Scene> m_Scene;
    };
} // namespace

EditorWorkspaceLayer::EditorWorkspaceLayer(const bool smoke, const bool initializeProject)
    : Layer("EditorWorkspaceLayer"), m_AssetBrowserPanel(std::make_unique<KeireEditor::AssetBrowserPanel>()),
      m_ConsolePanel(std::make_unique<KeireEditor::ConsolePanel>()),
      m_DiagnosticsPanel(std::make_unique<KeireEditor::DiagnosticsPanel>()),
      m_SceneGizmos(std::make_unique<KeireEditor::SceneGizmoController>()),
      m_SceneDocument(std::make_unique<KeireEditor::SceneDocument>()),
      m_InputActionsDocument(std::make_unique<KeireEditor::InputActionsDocument>()),
      m_CommandRouter(std::make_unique<KeireEditor::EditorCommandRouter>()),
      m_SceneViewportPanel(std::make_unique<KeireEditor::SceneViewportPanel>(
          static_cast<KeireEditor::ISceneViewportController&>(*this))),
      m_HierarchyPanel(
          std::make_unique<KeireEditor::HierarchyPanel>(static_cast<KeireEditor::IHierarchyController&>(*this))),
      m_InspectorPanel(
          std::make_unique<KeireEditor::InspectorPanel>(static_cast<KeireEditor::IInspectorController&>(*this))),
      m_InputActionsPanel(
          std::make_unique<KeireEditor::InputActionsPanel>(static_cast<KeireEditor::IInputActionsController&>(*this))),
      m_ProjectSettingsPanel(std::make_unique<KeireEditor::ProjectSettingsPanel>(
          static_cast<KeireEditor::IProjectSettingsController&>(*this))),
      m_PropertyDrawers(std::make_unique<KeireEditor::PropertyDrawerRegistry>()),
      m_InputAsset(m_InputActionsDocument->AssetStorage()), m_SelectedInputMap(m_InputActionsDocument->MapStorage()),
      m_SelectedInputScheme(m_InputActionsDocument->SchemeStorage()),
      m_SelectedInputAction(m_InputActionsDocument->ActionStorage()),
      m_SelectedInputBinding(m_InputActionsDocument->BindingStorage()),
      m_InputDocument(m_InputActionsDocument->DefinitionStorage()),
      m_InputUndoContext(m_InputActionsDocument->UndoStorage()), m_EditingScene(m_SceneDocument->SceneStorage()),
      m_PlaySession(m_SceneDocument->PlaySessionStorage()), m_SceneLoad(m_SceneDocument->LoadOperationStorage()),
      m_SaveSceneDialog(m_SceneDocument->SaveDialogStorage()), m_SceneAsset(m_SceneDocument->AssetStorage()),
      m_SelectedSceneObject(m_SceneDocument->SelectionStorage()), m_SceneUndoContext(m_SceneDocument->UndoStorage()),
      m_SceneSource(m_SceneDocument->SourceStorage()), m_SceneRecovery(m_SceneDocument->RecoveryPathStorage()),
      m_SceneStatus(m_SceneDocument->StatusStorage()),
      m_EditorCamera(std::make_unique<Keire::Detail::EditorCameraController>()),
      m_SceneRecoverySeconds(m_SceneDocument->RecoverySecondsStorage()),
      m_InputDirty(m_InputActionsDocument->DirtyStorage()),
      m_SceneRecoveryAvailable(m_SceneDocument->RecoveryAvailableStorage()), m_Smoke(smoke),
      m_InitializeProject(initializeProject)
{
    m_PropertyDrawers->RegisterOverride(
        Keire::TransformComponent::StaticType(), "rotation",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
           Keire::ComponentPropertyValue& value)
        {
            auto* rotation = std::get_if<Keire::Quaternion>(&value);
            if (!rotation)
                throw std::invalid_argument("Transform rotation metadata must serialize a Quaternion.");
            auto euler = Keire::Math::QuaternionToEulerDegrees(*rotation);
            if (!editor.EditVector3(property.DisplayName, euler, std::max(property.Step, 0.25)))
                return false;
            *rotation = Keire::Math::EulerDegreesToQuaternion(euler);
            return true;
        });
    m_PropertyDrawers->RegisterOverride(
        Keire::TransformComponent::StaticType(), "scale",
        [this](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
               Keire::ComponentPropertyValue& value)
        {
            auto* scale = std::get_if<Keire::Vector3>(&value);
            if (!scale)
                throw std::invalid_argument("Transform scale metadata must serialize a Vector3.");
            const auto previous = *scale;
            if (!editor.EditVector3(property.DisplayName, *scale, std::max(property.Step, 0.01)))
                return false;
            if (!m_UniformScale)
                return true;
            const auto propagate = [](const float oldValue, const float newValue, const float other)
            { return std::abs(oldValue) > 0.0001F ? other * (newValue / oldValue) : other + newValue - oldValue; };
            if (scale->X != previous.X)
            {
                scale->Y = propagate(previous.X, scale->X, previous.Y);
                scale->Z = propagate(previous.X, scale->X, previous.Z);
            }
            else if (scale->Y != previous.Y)
            {
                scale->X = propagate(previous.Y, scale->Y, previous.X);
                scale->Z = propagate(previous.Y, scale->Y, previous.Z);
            }
            else if (scale->Z != previous.Z)
            {
                scale->X = propagate(previous.Z, scale->Z, previous.X);
                scale->Y = propagate(previous.Z, scale->Z, previous.Y);
            }
            return true;
        });
    m_PropertyDrawers->RegisterOverride(
        Keire::CameraComponent::StaticType(), "projection",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
           Keire::ComponentPropertyValue& value)
        {
            auto* projection = std::get_if<std::int64_t>(&value);
            if (!projection)
                throw std::invalid_argument("Camera projection metadata must serialize an Integer.");
            constexpr std::array choices{std::string_view("Perspective"), std::string_view("Orthographic")};
            return editor.EditChoice(property.DisplayName, *projection, choices);
        });
    m_PropertyDrawers->RegisterOverride(
        Keire::DirectionalLightComponent::StaticType(), "shadows",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
           Keire::ComponentPropertyValue& value)
        {
            auto* shadows = std::get_if<std::int64_t>(&value);
            if (!shadows)
                throw std::invalid_argument("Directional Light shadow metadata must serialize an Integer.");
            constexpr std::array choices{std::string_view("None"), std::string_view("Hard"), std::string_view("Soft")};
            return editor.EditChoice(property.DisplayName, *shadows, choices);
        });
    const auto registerAssetPicker = [this](const std::string_view key, const Keire::AssetTypeId type)
    {
        m_PropertyDrawers->RegisterOverride(
            Keire::MeshRendererComponent::StaticType(), std::string(key),
            [type](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
                   Keire::ComponentPropertyValue& value)
            {
                auto* asset = std::get_if<Keire::AssetId>(&value);
                if (!asset)
                    throw std::invalid_argument("Mesh Renderer asset metadata must serialize an AssetId.");
                return editor.EditAsset(property.DisplayName, *asset, type);
            });
    };
    registerAssetPicker("mesh", Keire::MeshAsset::StaticType());
    registerAssetPicker("material", Keire::MaterialAsset::StaticType());

    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::NewScene, [this] { RequestCreateScene(); },
        [this] { return static_cast<bool>(m_AssetDatabase); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SaveScene, [this] { SaveScene(); },
        [this] { return m_EditingScene && m_EditingScene->Dirty(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SaveSceneAs, [this] { SaveSceneAs(); },
        [this] { return m_EditingScene && !m_SaveSceneDialog; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CloseScene, [this] { RequestCloseScene(); },
        [this] { return static_cast<bool>(m_EditingScene); });
    m_CommandRouter->Bind(KeireEditor::EditorCommand::Exit,
                          [this]
                          {
                              if (m_EditingScene && m_EditingScene->Dirty())
                              {
                                  m_PendingSceneAction = PendingSceneAction::Exit;
                                  OpenDialog(Dialog::DirtyScene);
                              }
                              else
                                  Owner().RequestExit();
                          });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::Undo, [this] { ApplyActiveUndo(false); },
        [this] { return m_ActiveUndoContext && m_ActiveUndoContext->CanUndo(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::Redo, [this] { ApplyActiveUndo(true); },
        [this] { return m_ActiveUndoContext && m_ActiveUndoContext->CanRedo(); });
}

EditorWorkspaceLayer::~EditorWorkspaceLayer() = default;

void EditorWorkspaceLayer::OnAttach()
{
    auto& workspace = Owner().GetUiWorkspace();
    m_Scene = workspace.RegisterPanel({"editor.scene", "Scene"});
    m_Game = workspace.RegisterPanel({"editor.game", "Game"});
    m_Hierarchy = workspace.RegisterPanel({"editor.hierarchy", "Hierarchy"});
    m_Inspector = workspace.RegisterPanel({"editor.inspector", "Inspector"});
    m_Project = workspace.RegisterPanel({"editor.project", "Project"});
    m_Console = workspace.RegisterPanel({"editor.console", "Console"});
    m_Diagnostics = workspace.RegisterPanel({"editor.diagnostics", "Diagnostics"});
    m_ThemeEditor = workspace.RegisterPanel({"editor.theme", "Theme Editor", false});
    m_InputActionsEditor = workspace.RegisterPanel({"editor.input-actions", "Input Actions", false});
    m_InputDebugger = workspace.RegisterPanel({"editor.input-debugger", "Input Debugger", false});
    m_ProjectSettings = workspace.RegisterPanel({"editor.project-settings", "Project Settings", false});
    if (const auto undo = Owner().Undo())
        m_ThemeUndoContext = undo->CreateContext({.Name = "Theme Authoring"});
    if (const auto renderer = Owner().Renderer(); renderer && renderer->Mode() != Keire::RenderMode::Disabled)
    {
        Keire::RenderSurfaceSpecification sceneSurface;
        sceneSurface.Name = "Scene View";
        sceneSurface.ClearColor = {0.075F, 0.085F, 0.105F, 1.0F};
        m_SceneRenderView = renderer->CreateView(sceneSurface);

        Keire::RenderSurfaceSpecification gameSurface;
        gameSurface.Name = "Game View";
        gameSurface.ClearColor = {0.10F, 0.12F, 0.16F, 1.0F};
        m_GameRenderView = renderer->CreateView(gameSurface);
    }
    LoadTheme(workspace, workspace.ActiveTheme());
    if (!m_Smoke || m_InitializeProject)
    {
        try
        {
            Keire::AssetDatabaseSpecification databaseSpecification;
            const auto project = Owner().GetProject();
            if (!project)
                throw std::runtime_error("Editor workspace requires an active project.");
            databaseSpecification.ProjectRoot = project->Root();
            m_AssetBrowserPanel->SetProjectRoot(project->Root());
            try
            {
                m_RenderEnvironment = Keire::LoadRenderEnvironmentSettings(project->Root());
            }
            catch (const std::exception& error)
            {
                m_RenderEnvironment = {};
                ReportError("Rendering",
                            std::string("Invalid project rendering settings; using defaults: ") + error.what());
            }
            m_SceneGizmos->Load(project->Root());
            if (const auto undo = Owner().Undo())
                m_AssetBrowserPanel->SetUndoContext(undo->CreateContext({.Name = "Project Assets"}));
            LoadSceneCamera();
            databaseSpecification.Importers = {
                Keire::CreateInputActionAssetImporter(), Keire::CreateSceneAssetImporter(),
                Keire::CreateShaderAssetImporter(),      Keire::CreateMaterialAssetImporter(),
                Keire::CreateMeshAssetImporter(),        Keire::CreateTexture2DAssetImporter()};
            m_AssetDatabase = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            ImportAssets();
            if (const auto input = Owner().Input())
            {
                m_EditorInputUser = input->CreateUser("Editor");
                (void)input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(1));
                (void)input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(2));
            }
            Listen<Keire::InputDeviceConnectedEvent>(
                [this](const auto& event)
                {
                    AddConsoleMessage("Input", "Connected " + event.Device.Name, m_Theme.Success);
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::InputDeviceDisconnectedEvent>(
                [this](const auto& event)
                {
                    AddConsoleMessage("Input", "Disconnected device " + std::to_string(event.Device.Value()),
                                      m_Theme.Warning, Keire::LogLevel::Warn);
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::WindowCloseRequestedEvent>(
                [this](const auto& event)
                {
                    if (event.Header.Window == Owner().MainWindow()->Id() && m_EditingScene && m_EditingScene->Dirty())
                    {
                        m_PendingSceneAction = PendingSceneAction::Exit;
                        OpenDialog(Dialog::DirtyScene);
                        return Keire::EventFlow::Handled;
                    }
                    return Keire::EventFlow::Continue;
                });
            if (project->Descriptor().StartupScene)
                OpenScene(project->Descriptor().StartupScene);
            if (project->Descriptor().DefaultInput)
            {
                OpenInputActions(project->Descriptor().DefaultInput);
                m_InputActionsEditor.SetVisible(false);
            }
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Asset database initialization failed: ") + error.what());
        }
    }
}

void EditorWorkspaceLayer::OnDetach() noexcept
{
    SaveSceneCamera();
    if (m_RenderEnvironmentDirty)
    {
        try
        {
            if (const auto project = Owner().GetProject(); project && project->Writable())
                Keire::SaveRenderEnvironmentSettings(project->Root(), m_RenderEnvironment);
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Rendering] Could not save project settings during shutdown: {}", error.what());
        }
        m_RenderEnvironmentDirty = false;
    }
    if (const auto project = Owner().GetProject(); project && m_SceneGizmos)
        m_SceneGizmos->Save(project->Root());
    if (m_SceneCameraCapturing)
    {
        try
        {
            Owner().Windows()->SetCursorMode(Owner().MainWindow()->Id(), Keire::CursorMode::Normal);
        }
        catch (...)
        {
        }
        m_SceneCameraCapturing = false;
    }
    EndInputTest();
    if (m_PlaySession)
        m_PlaySession->Stop();
    m_PlaySession.Reset();
    m_GameRenderView.Reset();
    m_SceneRenderView.Reset();
    m_Rebind.Reset();
    m_InputContext.Reset();
    if (m_InputUndoContext)
        m_InputUndoContext->Close();
    if (m_SceneUndoContext)
        m_SceneUndoContext->Close();
    if (m_ThemeUndoContext)
        m_ThemeUndoContext->Close();
    m_ActiveUndoContext.Reset();
    m_InputUndoContext.Reset();
    m_SceneUndoContext.Reset();
    m_ThemeUndoContext.Reset();
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        try
        {
            WriteSceneRecovery();
        }
        catch (...)
        {
        }
    }
    if (m_EditingScene)
        m_EditingScene->Close();
    m_EditingScene.Reset();
    m_AssetBrowserPanel->Close();
    m_AssetDatabase.Reset();
}

void EditorWorkspaceLayer::OnFixedUpdate(const Keire::Time& time)
{
    if (m_PlaySession)
        m_PlaySession->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
}

void EditorWorkspaceLayer::OnUpdate(const Keire::Time& time)
{
    if (m_Smoke && ++m_FrameCount >= 8)
        Owner().RequestExit();
    if (m_PlaySession)
    {
        m_PlaySession->Update(static_cast<float>(time.DeltaTime().Seconds()));
        if (m_PlaySession->State() == Keire::ScenePlayState::Faulted && !m_PlayFaultReported)
        {
            const auto diagnostic = m_PlaySession->Diagnostic();
            m_SceneStatus = diagnostic.Callback + " failed: " + diagnostic.Message;
            ReportError("Play Mode", m_SceneStatus);
            m_PlayFaultReported = true;
        }
    }
    CompleteSaveSceneAs();
    if (!m_AssetDatabase)
        return;
    if (m_SceneLoad && m_SceneLoad->State() == Keire::SceneLoadState::Failed)
    {
        m_SceneStatus = "Scene runtime load failed: " + m_SceneLoad->Diagnostic().Message;
        m_SceneLoad.Reset();
    }
    else if (m_SceneLoad && m_SceneLoad->State() == Keire::SceneLoadState::Ready)
    {
        m_SceneStatus = "Scene loaded and activated.";
        m_SceneLoad.Reset();
    }
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_SceneRecoverySeconds += time.UnscaledDeltaTime().Seconds();
        if (m_SceneRecoverySeconds >= 30.0)
        {
            m_SceneRecoverySeconds = 0.0;
            try
            {
                WriteSceneRecovery();
            }
            catch (const std::exception& error)
            {
                m_SceneStatus = std::string("Scene recovery save failed: ") + error.what();
                ReportError("Scene", m_SceneStatus);
            }
        }
    }
    else
        m_SceneRecoverySeconds = 0.0;
    m_AssetPollSeconds += time.UnscaledDeltaTime().Seconds();
    if (m_AssetPollSeconds < 0.25)
        return;
    m_AssetPollSeconds = 0.0;
    try
    {
        const auto changed = m_AssetDatabase->PollChangedAssets();
        if (!changed.empty())
        {
            ImportAssets();
            if (const auto assets = Owner().Assets())
            {
                for (const auto id : changed)
                    (void)assets->Reload(id);
            }
        }
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset hot reload failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::OnUi(Keire::UiFrame& ui)
{
    if (!m_ActiveUndoContext || !m_ActiveUndoContext->IsOpen())
    {
        if (m_SceneUndoContext && m_SceneUndoContext->IsOpen())
            m_ActiveUndoContext = m_SceneUndoContext;
        else if (m_InputUndoContext && m_InputUndoContext->IsOpen())
            m_ActiveUndoContext = m_InputUndoContext;
        else if (m_AssetBrowserPanel)
            m_ActiveUndoContext = m_AssetBrowserPanel->UndoContext();
    }
    if (ui.Shortcut({.Key = Keire::UiKey::Z, .Shift = true, .Primary = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    else if (ui.Shortcut({.Key = Keire::UiKey::Z, .Primary = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Undo);
    else if (ui.Shortcut({.Key = Keire::UiKey::Y, .Primary = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    else if (ui.Shortcut({.Key = Keire::UiKey::R, .Primary = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    if (ui.Shortcut({Keire::UiKey::S, true, true}) && m_EditingScene && !m_SaveSceneDialog)
        SaveSceneAs();
    else if (ui.Shortcut({Keire::UiKey::S, true}))
    {
        if (m_InputDirty && m_InputActionsEditor.Visible())
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
                ReportError("Input", m_InputMessage);
            }
        }
        else if (m_EditingScene && m_EditingScene->Dirty())
            SaveScene();
    }
    auto& workspace = Owner().GetUiWorkspace();
    DrawMainMenu(ui, workspace);
    OpenPendingDialog(ui);
    DrawNotices(ui, workspace);
    DrawDialogs(ui, workspace);

    m_SceneViewportPanel->Draw(ui);
    DrawGame(ui);
    m_HierarchyPanel->Draw(ui);
    m_InspectorPanel->Draw(ui);
    DrawProject(ui);
    if (m_AssetBrowserPanel && m_AssetBrowserPanel->Focused())
        m_ActiveUndoContext = m_AssetBrowserPanel->UndoContext();
    DrawConsole(ui);
    DrawDiagnostics(ui);
    DrawThemeEditor(ui, workspace);
    m_InputActionsPanel->Draw(ui);
    DrawInputDebugger(ui);
    m_ProjectSettingsPanel->Draw(ui);
}

void EditorWorkspaceLayer::DrawEmptyState(Keire::UiFrame& ui, const std::string_view heading,
                                          const std::string_view primary, const std::string_view detail)
{
    ui.TextColored({0.30F, 0.55F, 1.0F, 1.0F}, heading);
    ui.Separator();
    ui.Spacing();
    ui.Text(primary);
    ui.Spacing();
    ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F}, detail);
}

void EditorWorkspaceLayer::DrawPanelMenuItem(Keire::UiFrame& ui, Keire::UiPanelRegistration& panel)
{
    if (ui.MenuItem(panel.Title(), panel.Visible()))
        panel.SetVisible(!panel.Visible());
}

void EditorWorkspaceLayer::DrawMainMenu(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    if (auto menuBar = ui.BeginMainMenuBar(); menuBar)
    {
        if (auto file = ui.BeginMenu("File"); file)
        {
            if (ui.MenuItem("New Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::NewScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::NewScene);
            if (ui.MenuItem("Save Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::SaveScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveScene);
            if (ui.MenuItem("Save Scene As...", false,
                            m_CommandRouter->Available(KeireEditor::EditorCommand::SaveSceneAs)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveSceneAs);
            if (ui.MenuItem("Close Scene", false, m_CommandRouter->Available(KeireEditor::EditorCommand::CloseScene)))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::CloseScene);
            ui.Separator();
            if (ui.MenuItem("Exit"))
            {
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Exit);
            }
        }
        if (auto edit = ui.BeginMenu("Edit"); edit)
        {
            const bool canUndo = m_ActiveUndoContext && m_ActiveUndoContext->CanUndo();
            const bool canRedo = m_ActiveUndoContext && m_ActiveUndoContext->CanRedo();
            const auto undoLabel = canUndo ? "Undo " + m_ActiveUndoContext->UndoName() : "Undo";
            const auto redoLabel = canRedo ? "Redo " + m_ActiveUndoContext->RedoName() : "Redo";
            if (ui.MenuItem(undoLabel, false, canUndo))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Undo);
            if (ui.MenuItem(redoLabel, false, canRedo))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
            ui.Separator();
            ui.TextColored(m_Theme.MutedText,
                           m_ActiveUndoContext ? std::string(m_ActiveUndoContext->Name()) : "No active history");
            ui.Separator();
            if (ui.MenuItem("Project Settings..."))
                m_ProjectSettings.SetVisible(true);
        }
        if (auto entity = ui.BeginMenu("Entity", static_cast<bool>(m_EditingScene)); entity)
        {
            if (ui.MenuItem("Create Empty"))
            {
                RecordSceneUndo();
                m_SelectedSceneObject = m_EditingScene->CreateEntity().Id().Value();
            }
            if (ui.MenuItem("Create Child", false, static_cast<bool>(m_SelectedSceneObject)))
            {
                RecordSceneUndo();
                const auto parent = m_EditingScene->FindEntity(Keire::EntityId(m_SelectedSceneObject));
                m_SelectedSceneObject = m_EditingScene->CreateEntity("GameObject", parent).Id().Value();
            }
            if (ui.MenuItem("Directional Light"))
            {
                RecordSceneUndo();
                auto created = m_EditingScene->CreateEntity("Directional Light");
                (void)created.AddComponent<Keire::DirectionalLightComponent>();
                m_SelectedSceneObject = created.Id().Value();
            }
            if (ui.MenuItem("Main Camera"))
            {
                RecordSceneUndo();
                auto created = m_EditingScene->CreateEntity("Main Camera");
                created.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.0F, -10.0F});
                (void)created.AddComponent<Keire::CameraComponent>();
                m_SelectedSceneObject = created.Id().Value();
            }
            if (ui.MenuItem("3D Object/Cube"))
            {
                RecordSceneUndo();
                auto created = m_EditingScene->CreateEntity("Cube");
                auto renderer = created.AddComponent<Keire::MeshRendererComponent>();
                renderer->SetMesh(Keire::MeshAsset::CubeId());
                m_SelectedSceneObject = created.Id().Value();
            }
            ui.Separator();
            if (ui.MenuItem("Duplicate", false, static_cast<bool>(m_SelectedSceneObject)))
            {
                RecordSceneUndo();
                m_SelectedSceneObject =
                    m_EditingScene->DuplicateEntity(Keire::EntityId(m_SelectedSceneObject)).Id().Value();
            }
            if (ui.MenuItem("Delete", false, static_cast<bool>(m_SelectedSceneObject)))
            {
                RecordSceneUndo();
                (void)m_EditingScene->DestroyEntity(Keire::EntityId(m_SelectedSceneObject));
                m_SelectedSceneObject = {};
            }
        }
        if (auto layoutsMenu = ui.BeginMenu("Layout"); layoutsMenu)
        {
            const auto layouts = workspace.Layouts();
            for (const auto& layout : layouts)
            {
                std::string label = layout.Name + (layout.Modified ? " *" : "");
                if (ui.MenuItem(label, layout.Active))
                    workspace.LoadLayout(layout.Id);
            }
            ui.Separator();
            if (ui.MenuItem("Save As..."))
                OpenDialog(Dialog::SaveLayout);
            const auto* active = ActiveLayout(layouts);
            const bool editable = active && !active->BuiltIn;
            if (ui.MenuItem("Rename...", false, editable))
            {
                m_ProfileName = active->Name;
                OpenDialog(Dialog::RenameLayout);
            }
            if (ui.MenuItem("Delete...", false, editable))
                OpenDialog(Dialog::DeleteLayout);
            if (ui.MenuItem("Reset to Default"))
                workspace.ResetFactoryLayout();
            ui.Separator();
            if (ui.MenuItem("Import..."))
                workspace.ShowImportLayoutDialog();
            if (ui.MenuItem("Export..."))
                workspace.ShowExportLayoutDialog(workspace.ActiveLayout());
        }
        if (auto themesMenu = ui.BeginMenu("Theme"); themesMenu)
        {
            const auto themes = workspace.Themes();
            for (const auto& theme : themes)
            {
                if (ui.MenuItem(theme.Name, theme.Active))
                    RequestTheme(workspace, theme.Id);
            }
            ui.Separator();
            if (ui.MenuItem("Theme Editor", m_ThemeEditor.Visible()))
                m_ThemeEditor.SetVisible(!m_ThemeEditor.Visible());
            if (ui.MenuItem("Import..."))
                workspace.ShowImportThemeDialog();
            if (ui.MenuItem("Export..."))
                workspace.ShowExportThemeDialog(workspace.ActiveTheme());
        }
        if (auto assetsMenu = ui.BeginMenu("Assets"); assetsMenu)
        {
            if (ui.MenuItem("Create/Scene", false, static_cast<bool>(m_AssetDatabase)))
                RequestCreateScene();
            if (ui.MenuItem("Create/Unlit Shader", false, static_cast<bool>(m_AssetDatabase)))
                CreateUnlitShader();
            if (ui.MenuItem("Create/Material", false, static_cast<bool>(m_AssetDatabase)))
                CreateMaterial();
            if (ui.MenuItem("Create/Input Actions/Empty", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions({.SchemaVersion = 1, .Name = "InputActions"}, "InputActions");
            if (ui.MenuItem("Create/Input Actions/Default", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::DefaultDefinition(), "DefaultInput");
            if (ui.MenuItem("Create/Input Actions/3D Gameplay", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::GameplayDefinition(), "GameplayInput");
            if (ui.MenuItem("Create/Input Actions/UI Navigation", false, static_cast<bool>(m_AssetDatabase)))
                CreateInputActions(Keire::InputActionAsset::UiDefinition(), "UiInput");
            ui.Separator();
            if (ui.MenuItem("Refresh and Import", false, static_cast<bool>(m_AssetDatabase)))
            {
                try
                {
                    ImportAssets();
                }
                catch (const std::exception& error)
                {
                    SetAssetError(std::string("Asset import failed: ") + error.what());
                }
            }
            if (ui.MenuItem("Cook Dist Build", false, static_cast<bool>(m_AssetDatabase)))
                CookAssets();
        }
        if (auto window = ui.BeginMenu("Window"); window)
        {
            DrawPanelMenuItem(ui, m_Scene);
            DrawPanelMenuItem(ui, m_Game);
            DrawPanelMenuItem(ui, m_Hierarchy);
            DrawPanelMenuItem(ui, m_Inspector);
            DrawPanelMenuItem(ui, m_Project);
            DrawPanelMenuItem(ui, m_Console);
            DrawPanelMenuItem(ui, m_Diagnostics);
            DrawPanelMenuItem(ui, m_ThemeEditor);
            DrawPanelMenuItem(ui, m_InputActionsEditor);
            DrawPanelMenuItem(ui, m_InputDebugger);
            DrawPanelMenuItem(ui, m_ProjectSettings);
        }
    }
}

void EditorWorkspaceLayer::DrawNotices(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    if (const auto notice = workspace.ConsumeNotice())
    {
        m_Notice = notice->Message;
        m_NoticeColor = notice->Severity == Keire::UiWorkspaceNoticeSeverity::Error     ? m_Theme.Error
                        : notice->Severity == Keire::UiWorkspaceNoticeSeverity::Warning ? m_Theme.Warning
                                                                                        : m_Theme.Success;
        ui.OpenPopup("Workspace Notice");
    }
    if (auto popup = ui.BeginPopupModal("Workspace Notice"); popup)
    {
        ui.TextColored(m_NoticeColor, m_Notice);
        if (ui.Button("OK"))
            ui.CloseCurrentPopup();
    }
}

void EditorWorkspaceLayer::OpenDialog(const Dialog dialog)
{
    m_Dialog = dialog;
    if (dialog == Dialog::SaveLayout || dialog == Dialog::SaveTheme)
        m_ProfileName.clear();
    m_Error.clear();
    m_OpenDialog = true;
}

void EditorWorkspaceLayer::OpenPendingDialog(Keire::UiFrame& ui)
{
    if (!m_OpenDialog)
        return;
    m_OpenDialog = false;
    switch (m_Dialog)
    {
    case Dialog::SaveLayout:
        ui.OpenPopup("Save Layout As");
        break;
    case Dialog::RenameLayout:
        ui.OpenPopup("Rename Layout");
        break;
    case Dialog::DeleteLayout:
        ui.OpenPopup("Delete Layout");
        break;
    case Dialog::SaveTheme:
        ui.OpenPopup("Save Theme As");
        break;
    case Dialog::RenameTheme:
        ui.OpenPopup("Rename Theme");
        break;
    case Dialog::DeleteTheme:
        ui.OpenPopup("Delete Theme");
        break;
    case Dialog::DirtyTheme:
        ui.OpenPopup("Unsaved Theme Changes");
        break;
    case Dialog::DirtyScene:
        ui.OpenPopup("Unsaved Scene Changes");
        break;
    case Dialog::RenameEntity:
        ui.OpenPopup("Rename Entity");
        break;
    case Dialog::None:
    default:
        break;
    }
}

void EditorWorkspaceLayer::DrawDialogs(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    DrawNameDialog(ui, workspace, "Save Layout As", Dialog::SaveLayout);
    DrawNameDialog(ui, workspace, "Rename Layout", Dialog::RenameLayout);
    DrawNameDialog(ui, workspace, "Save Theme As", Dialog::SaveTheme);
    DrawNameDialog(ui, workspace, "Rename Theme", Dialog::RenameTheme);
    DrawNameDialog(ui, workspace, "Rename Entity", Dialog::RenameEntity);
    DrawDeleteDialog(ui, workspace, "Delete Layout", false);
    DrawDeleteDialog(ui, workspace, "Delete Theme", true);
    DrawDirtyThemeDialog(ui, workspace);
    DrawDirtySceneDialog(ui);
}

void EditorWorkspaceLayer::DrawNameDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace,
                                          const std::string_view title, const Dialog dialog)
{
    if (auto popup = ui.BeginPopupModal(title); popup)
    {
        ui.Text(dialog == Dialog::RenameEntity ? "Entity name" : "Profile name");
        (void)ui.InputText("##ProfileName", m_ProfileName);
        if (auto disabled = ui.BeginDisabled(m_ProfileName.empty()); disabled)
        {
            if (ui.Button(dialog == Dialog::SaveTheme ? "Save" : "Confirm"))
            {
                try
                {
                    if (dialog == Dialog::SaveLayout)
                        workspace.SaveLayoutAs(m_ProfileName);
                    else if (dialog == Dialog::RenameLayout)
                        workspace.RenameLayout(workspace.ActiveLayout(), m_ProfileName);
                    else if (dialog == Dialog::SaveTheme)
                    {
                        (void)workspace.SaveThemeAs(m_ProfileName, m_Theme);
                        m_ThemeDirty = false;
                        if (m_PendingTheme)
                        {
                            workspace.ApplyTheme(m_PendingTheme);
                            LoadTheme(workspace, m_PendingTheme);
                        }
                        if (m_CloseThemeAfterDecision)
                            m_ThemeEditor.SetVisible(false);
                        m_PendingTheme = {};
                        m_CloseThemeAfterDecision = false;
                    }
                    else if (dialog == Dialog::RenameTheme)
                        workspace.RenameTheme(workspace.ActiveTheme(), m_ProfileName);
                    else if (dialog == Dialog::RenameEntity && m_EditingScene && m_SelectedSceneObject)
                        (void)m_EditingScene->RenameObject(m_SelectedSceneObject, m_ProfileName);
                    m_Dialog = Dialog::None;
                    ui.CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                    ReportError("Editor", m_Error);
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        if (!m_Error.empty())
            ui.TextColored(m_Theme.Error, m_Error);
    }
}

void EditorWorkspaceLayer::DrawDeleteDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace,
                                            const std::string_view title, const bool theme)
{
    if (auto popup = ui.BeginPopupModal(title); popup)
    {
        ui.Text(theme ? "Delete the active custom theme?" : "Delete the active custom layout?");
        ui.TextColored(m_Theme.Warning, "This cannot be undone.");
        if (ui.Button("Delete"))
        {
            if (theme)
            {
                workspace.DeleteTheme(workspace.ActiveTheme());
                LoadTheme(workspace, workspace.ActiveTheme());
            }
            else
                workspace.DeleteLayout(workspace.ActiveLayout());
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
    }
}

void EditorWorkspaceLayer::RequestTheme(Keire::UiWorkspace& workspace, const Keire::UiThemeId id)
{
    if (id == workspace.ActiveTheme())
        return;
    if (!m_ThemeDirty)
    {
        workspace.ApplyTheme(id);
        LoadTheme(workspace, id);
        return;
    }
    m_PendingTheme = id;
    m_CloseThemeAfterDecision = false;
    m_Dialog = Dialog::DirtyTheme;
    m_OpenDialog = true;
}

void EditorWorkspaceLayer::DrawDirtyThemeDialog(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Theme Changes"); popup)
    {
        ui.Text("Save changes before switching or closing the editor?");
        const auto themes = workspace.Themes();
        const auto* active = ActiveTheme(themes);
        const bool canOverwrite = active && !active->BuiltIn;
        if (ui.Button(canOverwrite ? "Save" : "Save As..."))
        {
            if (canOverwrite)
            {
                workspace.UpdateTheme(workspace.ActiveTheme(), m_Theme);
                m_ThemeDirty = false;
                if (m_PendingTheme)
                {
                    workspace.ApplyTheme(m_PendingTheme);
                    LoadTheme(workspace, m_PendingTheme);
                }
                if (m_CloseThemeAfterDecision)
                    m_ThemeEditor.SetVisible(false);
                m_PendingTheme = {};
                m_CloseThemeAfterDecision = false;
                m_Dialog = Dialog::None;
                ui.CloseCurrentPopup();
            }
            else
            {
                m_Dialog = Dialog::SaveTheme;
                m_ProfileName.clear();
                ui.CloseCurrentPopup();
                m_OpenDialog = true;
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            workspace.CancelThemePreview();
            m_ThemeDirty = false;
            if (m_PendingTheme)
            {
                workspace.ApplyTheme(m_PendingTheme);
                LoadTheme(workspace, m_PendingTheme);
            }
            else
                LoadTheme(workspace, workspace.ActiveTheme());
            if (m_CloseThemeAfterDecision)
                m_ThemeEditor.SetVisible(false);
            m_PendingTheme = {};
            m_CloseThemeAfterDecision = false;
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_PendingTheme = {};
            m_CloseThemeAfterDecision = false;
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        if (!canOverwrite)
            ui.TextColored(m_Theme.MutedText, "Built-in themes must be preserved with Save As.");
    }
}

void EditorWorkspaceLayer::DrawDirtySceneDialog(Keire::UiFrame& ui)
{
    if (auto popup = ui.BeginPopupModal("Unsaved Scene Changes"); popup)
    {
        ui.Text("Save the active scene before continuing?");
        if (ui.Button("Save"))
        {
            SaveScene();
            if (!m_EditingScene || !m_EditingScene->Dirty())
            {
                ui.CloseCurrentPopup();
                ExecutePendingSceneAction();
            }
        }
        ui.SameLine();
        if (ui.Button("Discard"))
        {
            ui.CloseCurrentPopup();
            ExecutePendingSceneAction();
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_PendingSceneAction = PendingSceneAction::None;
            m_PendingSceneAsset = {};
            m_Dialog = Dialog::None;
            ui.CloseCurrentPopup();
        }
        ui.TextColored(m_Theme.MutedText, "Save is atomic; Cancel leaves the scene and selection unchanged.");
    }
}

void EditorWorkspaceLayer::LoadTheme(Keire::UiWorkspace& workspace, const Keire::UiThemeId id)
{
    m_Theme = workspace.ThemeDefinition(id);
    m_ThemeDirty = false;
}

void EditorWorkspaceLayer::ImportAssets()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        const auto result = m_AssetDatabase->ImportAll(Keire::AssetImportPolicy::KeepLastGood);
        m_AssetRecords = m_AssetDatabase->Records();
        if (!result.CatalogPath.empty())
        {
            if (const auto assets = Owner().Assets())
            {
                (void)assets->Unmount(result.CatalogPath);
                assets->Mount({result.CatalogPath, 0, true});
            }
        }
        for (const auto& importStatus : result.Statuses)
        {
            for (const auto& diagnostic : importStatus.Diagnostics)
            {
                const auto message = FormatAssetDiagnostic(diagnostic);
                switch (diagnostic.Severity)
                {
                case Keire::AssetDiagnosticSeverity::Information:
                    AddConsoleMessage("Asset Import", message, m_Theme.MutedText);
                    break;
                case Keire::AssetDiagnosticSeverity::Warning:
                    AddConsoleMessage("Asset Import", message, m_Theme.Warning, Keire::LogLevel::Warn);
                    break;
                case Keire::AssetDiagnosticSeverity::Error:
                    ReportError("Asset Import", message);
                    break;
                }
            }
        }
        const auto failures =
            std::ranges::count(result.Statuses, Keire::AssetImportState::Failed, &Keire::AssetImportStatus::State);
        std::ostringstream status;
        status << "Imported " << result.Imported << " asset(s); " << result.CacheHits << " cache hit(s).";
        if (failures > 0)
            status << ' ' << failures
                   << " asset(s) kept their last-good revision; select an asset for full diagnostics.";
        m_AssetStatus = status.str();
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset import failed: ") + error.what());
    }
    catch (...)
    {
        SetAssetError("Asset import failed with an unknown error.");
    }
}

void EditorWorkspaceLayer::CookAssets()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        (void)m_AssetDatabase->Refresh();
        Keire::AssetBuildProfile profile;
        profile.Name = "Dist";
        profile.Strict = true;
        const auto project = Owner().GetProject();
        const auto output =
            project ? project->Root() / "Build/CookedAssets/Dist" : std::filesystem::path("Build/CookedAssets/Dist");
        const auto result = Keire::AssetCooker::Cook(*m_AssetDatabase, profile, output);
        Keire::AssetCooker::Validate(result.CatalogPath);
        m_AssetStatus = "Cooked and validated " + std::to_string(result.AssetCount) + " asset(s) into " +
                        std::to_string(result.PackCount) + " pack(s).";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Asset cook failed: ") + error.what());
    }
    catch (...)
    {
        SetAssetError("Asset cook failed with an unknown error.");
    }
}

void EditorWorkspaceLayer::CreateInputActions(Keire::InputActionAssetDefinition definition,
                                              const std::string_view baseName)
{
    if (!m_AssetDatabase)
        return;
    try
    {
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / (std::string(baseName) + ".keireinput");
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = directory / (std::string(baseName) + " " + std::to_string(copy) + ".keireinput");
        definition.Name = destination.stem().string();
        const auto bytes = Keire::InputActionAsset::Encode(definition);
        const auto id = m_AssetDatabase->CreateAsset(destination, Keire::CreateInputActionAssetImporter(), bytes);
        m_AssetRecords = m_AssetDatabase->Records();
        if (m_AssetBrowserPanel)
        {
            m_AssetBrowserPanel->RecordCreatedAsset(m_AssetDatabase, id, "Create Input Actions");
            m_AssetBrowserPanel->RevealAsset(id, *this);
        }
        ImportAssets();
        m_SelectedAsset = id;
        OpenInputActions(id);
        m_AssetStatus = "Created " + destination.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Input asset creation failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CreateUnlitShader()
{
    if (!m_AssetDatabase)
        return;
    std::filesystem::path manifest;
    std::filesystem::path hlsl;
    bool committed = false;
    try
    {
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        std::string baseName = "UnlitShader";
        for (std::size_t copy = 2;; ++copy)
        {
            manifest = directory / (baseName + ".keireshader");
            hlsl = directory / (baseName + ".hlsl");
            if (!m_AssetDatabase->Find(manifest) &&
                !std::filesystem::exists(m_AssetDatabase->Specification().ProjectRoot / "Assets" / hlsl))
                break;
            baseName = "UnlitShader " + std::to_string(copy);
        }

        const std::string shaderSource = R"(struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

struct VertexOutput
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

cbuffer CameraObjectConstants : register(b0, space1)
{
    float4x4 ModelViewProjection;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.Color = float4(input.Color, 1.0F);
    output.Position = mul(ModelViewProjection, float4(input.Position, 1.0F));
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return input.Color;
}
)";
        const auto projectSource = (std::filesystem::path("Assets") / hlsl).generic_string();
        const auto includeRoot =
            (std::filesystem::path("Assets") / (directory.empty() ? std::filesystem::path{} : directory))
                .generic_string();
        const std::string manifestSource =
            "{\n  \"schemaVersion\": 1,\n  \"source\": \"" + projectSource +
            "\",\n  \"stages\": { \"vertex\": \"VSMain\", \"fragment\": \"PSMain\" },\n"
            "  \"defines\": {},\n  \"includeRoots\": [\"" +
            includeRoot +
            "\"],\n"
            "  \"renderState\": { \"topology\": \"TriangleList\", \"culling\": \"Back\", "
            "\"depthTest\": true, \"depthWrite\": true, \"blend\": false },\n"
            "  \"properties\": [{ \"name\": \"Tint\", \"type\": \"Color\", "
            "\"default\": [0.25, 0.55, 1.0, 1.0] }]\n}\n";
        const auto sourcePath = m_AssetDatabase->Specification().ProjectRoot / "Assets" / hlsl;
        Keire::Detail::WriteTextFileAtomically(sourcePath, shaderSource);
        const auto bytes = std::as_bytes(std::span(manifestSource));
        const auto id = m_AssetDatabase->CreateAsset(manifest, Keire::CreateShaderAssetImporter(), bytes);
        committed = true;
        m_AssetRecords = m_AssetDatabase->Records();
        if (m_AssetBrowserPanel)
        {
            m_AssetBrowserPanel->RecordCreatedAsset(m_AssetDatabase, id, "Create Unlit Shader");
            m_AssetBrowserPanel->RevealAsset(id, *this);
        }
        ImportAssets();
        m_SelectedAsset = id;
        m_AssetStatus = "Created and compiled " + manifest.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        if (!committed && !hlsl.empty())
        {
            std::error_code ignored;
            const auto sourcePath = m_AssetDatabase->Specification().ProjectRoot / "Assets" / hlsl;
            std::filesystem::remove(sourcePath, ignored);
            std::filesystem::remove(std::filesystem::path(sourcePath.string() + ".keiremeta"), ignored);
        }
        SetAssetError(std::string("Shader creation failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::CreateMaterial()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        Keire::AssetId shader;
        if (const auto selected = m_AssetDatabase->Find(m_SelectedAsset);
            selected && selected->Type == Keire::ShaderAsset::StaticType())
            shader = selected->Id;
        if (!shader)
        {
            const auto records = m_AssetDatabase->Records();
            const auto found =
                std::ranges::find(records, Keire::ShaderAsset::StaticType(), &Keire::AssetSourceRecord::Type);
            if (found != records.end())
                shader = found->Id;
        }

        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / "Material.keirematerial";
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = directory / ("Material " + std::to_string(copy) + ".keirematerial");
        const std::string source =
            "{\n  \"schemaVersion\": 1,\n  \"shader\": " + (shader ? "\"" + shader.ToString() + "\"" : "null") +
            ",\n  \"properties\": { \"Tint\": [0.25, 0.55, 1.0, 1.0] }\n}\n";
        const auto id = m_AssetDatabase->CreateAsset(destination, Keire::CreateMaterialAssetImporter(),
                                                     std::as_bytes(std::span(source)));
        m_AssetRecords = m_AssetDatabase->Records();
        if (m_AssetBrowserPanel)
        {
            m_AssetBrowserPanel->RecordCreatedAsset(m_AssetDatabase, id, "Create Material");
            m_AssetBrowserPanel->RevealAsset(id, *this);
        }
        ImportAssets();
        m_SelectedAsset = id;
        m_AssetStatus = "Created " + destination.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material creation failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::OpenInputActions(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keireinput")
        throw std::invalid_argument("Only .keireinput assets can be opened in the Input Actions editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    m_InputDocument = Keire::InputActionAsset::Decode(ReadBytes(source))->Definition();
    m_InputAsset = asset;
    m_SelectedInputMap = m_InputDocument.ActionMaps.empty() ? Keire::AssetId{} : m_InputDocument.ActionMaps.front().Id;
    m_SelectedInputScheme = {};
    m_SelectedInputAction = {};
    m_SelectedInputBinding = {};
    if (m_InputUndoContext)
        m_InputUndoContext->Close();
    if (const auto undo = Owner().Undo())
        m_InputUndoContext = undo->CreateContext(
            {.Name = "Input Actions: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    m_ActiveUndoContext = m_InputUndoContext;
    m_InputDirty = false;
    m_InputMessage = "Loaded " + record->RelativePath.generic_string() + ".";
    m_Rebind.Reset();
    m_InputContext.Reset();
    if (const auto input = Owner().Input(); input && m_EditorInputUser)
        m_InputContext = input->CreateActionContext(asset, m_EditorInputUser);
    m_InputActionsEditor.SetVisible(true);
}

void EditorWorkspaceLayer::SaveInputActions()
{
    if (!m_AssetDatabase || !m_InputAsset)
        return;
    const auto record = m_AssetDatabase->Find(m_InputAsset);
    if (!record)
        throw std::runtime_error("The edited input asset no longer exists.");
    const auto bytes = Keire::InputActionAsset::Encode(m_InputDocument);
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_InputAsset);
    m_InputDirty = false;
    m_InputMessage = "Saved and imported " + record->RelativePath.generic_string() + ".";
}

void EditorWorkspaceLayer::RecordInputUndo(const std::string_view name)
{
    if (!m_InputUndoContext || !m_InputUndoContext->IsOpen())
        return;
    auto before = m_InputDocument;
    auto after = std::make_shared<std::optional<Keire::InputActionAssetDefinition>>();
    const auto asset = m_InputAsset;
    m_InputUndoContext->RecordApplied(Keire::CreateUndoCommand(
        std::string(name),
        [this, after]
        {
            if (after->has_value())
            {
                m_InputDocument = **after;
                m_InputDirty = true;
            }
        },
        [this, before = std::move(before), after]() mutable
        {
            if (!after->has_value())
                *after = m_InputDocument;
            m_InputDocument = before;
            m_InputDirty = true;
        },
        Keire::InputActionAsset::Encode(m_InputDocument).size(), [this, asset] { return m_InputAsset == asset; }));
    m_InputDirty = true;
}

void EditorWorkspaceLayer::UndoInputEdit()
{
    if (m_InputUndoContext)
        (void)m_InputUndoContext->Undo();
}

void EditorWorkspaceLayer::RedoInputEdit()
{
    if (m_InputUndoContext)
        (void)m_InputUndoContext->Redo();
}

void EditorWorkspaceLayer::CreateScene()
{
    if (!m_AssetDatabase)
        return;
    try
    {
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        auto destination = directory / "Untitled.keirescene";
        for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
            destination = directory / ("Untitled " + std::to_string(copy) + ".keirescene");
        auto definition = Keire::SceneAsset::EmptyDefinition(destination.stem().string());
        const auto bytes = Keire::SceneAsset::Encode(definition);
        const auto id = m_AssetDatabase->CreateAsset(destination, Keire::CreateSceneAssetImporter(), bytes);
        m_AssetRecords = m_AssetDatabase->Records();
        if (m_AssetBrowserPanel)
        {
            m_AssetBrowserPanel->RecordCreatedAsset(m_AssetDatabase, id, "Create Scene");
            m_AssetBrowserPanel->RevealAsset(id, *this);
        }
        ImportAssets();
        OpenScene(id);
        m_AssetStatus = "Created " + destination.generic_string() + ".";
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Scene creation failed: ") + error.what());
    }
}

void EditorWorkspaceLayer::RequestCreateScene()
{
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Create;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    CreateScene();
}

void EditorWorkspaceLayer::OpenScene(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        throw std::logic_error("Asset database is unavailable.");
    if (m_EditingScene && m_EditingScene->Dirty())
        throw std::runtime_error("Save or revert the current scene before opening another scene.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keirescene")
        throw std::invalid_argument("Only .keirescene assets can be opened as scenes.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto definition = Keire::SceneAsset::Decode(ReadBytes(source))->Definition();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(asset, definition);
    m_EditingScene->MarkSaved();
    m_SceneAsset = asset;
    m_SceneSource = source;
    if (const auto project = Owner().GetProject())
        m_SceneRecovery = project->SceneRecoveryDirectory() / (asset.ToString() + ".keirescene.recovery");
    m_SceneRecoveryAvailable = !m_SceneRecovery.empty() && std::filesystem::is_regular_file(m_SceneRecovery);
    m_SelectedAsset = asset;
    m_SelectedSceneObject = {};
    if (m_SceneUndoContext)
        m_SceneUndoContext->Close();
    if (const auto undo = Owner().Undo())
        m_SceneUndoContext = undo->CreateContext({.Name = "Scene: " + record->RelativePath.stem().string()});
    m_ActiveUndoContext = m_SceneUndoContext;
    if (const auto scenes = Owner().Scenes())
        m_SceneLoad = scenes->Load(asset, Keire::SceneLoadMode::Single);
    m_SceneStatus = "Opening " + record->RelativePath.generic_string() + ".";
}

void EditorWorkspaceLayer::RequestOpenScene(const Keire::AssetId asset)
{
    if (asset == m_SceneAsset)
        return;
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Open;
        m_PendingSceneAsset = asset;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    OpenScene(asset);
}

void EditorWorkspaceLayer::SaveScene()
{
    if (!m_EditingScene || !m_AssetDatabase || !m_SceneAsset)
        return;
    try
    {
        const auto bytes = Keire::SceneAsset::Encode(m_EditingScene->Snapshot());
        WriteBytesAtomically(m_SceneSource, bytes);
        ImportAssets();
        if (const auto assets = Owner().Assets())
            (void)assets->Reload(m_SceneAsset);
        if (const auto scenes = Owner().Scenes())
            m_SceneLoad = scenes->Load(m_SceneAsset, Keire::SceneLoadMode::Single);
        m_EditingScene->MarkSaved();
        DiscardSceneRecovery();
        m_SceneStatus = "Scene saved atomically.";
        AddConsoleMessage("Scene", "Saved " + m_SceneSource.filename().string(), m_Theme.Success);
    }
    catch (const std::exception& error)
    {
        m_SceneStatus = std::string("Scene save failed: ") + error.what();
        ReportError("Scene", m_SceneStatus);
    }
}

void EditorWorkspaceLayer::SaveSceneAs()
{
    if (!m_EditingScene || !m_AssetDatabase || m_SaveSceneDialog)
        return;
    const auto assets = m_AssetDatabase->Specification().ProjectRoot / m_AssetDatabase->Specification().SourceDirectory;
    Keire::SaveFileDialogSpecification dialog;
    dialog.Title = "Save Scene As";
    dialog.DefaultLocation = assets / "Scenes";
    dialog.DefaultName = m_EditingScene->Name() + " Copy.keirescene";
    dialog.FilterName = "Kéire Scene";
    dialog.Extension = "keirescene";
    m_SaveSceneDialog = Owner().Windows()->ShowSaveFileDialog(Owner().MainWindow()->Id(), dialog);
    m_SceneStatus = "Choose a new scene path under this project's Assets directory.";
}

void EditorWorkspaceLayer::CompleteSaveSceneAs()
{
    if (!m_SaveSceneDialog || m_SaveSceneDialog->Status() == Keire::SaveFileDialogStatus::Pending)
        return;
    const auto operation = std::move(m_SaveSceneDialog);
    m_SaveSceneDialog.Reset();
    if (operation->Status() == Keire::SaveFileDialogStatus::Cancelled)
        return;
    if (operation->Status() == Keire::SaveFileDialogStatus::Failed)
    {
        m_SceneStatus = "Save As dialog failed: " + operation->Diagnostic();
        return;
    }
    try
    {
        auto destination = operation->SelectedPath();
        if (destination.extension() != ".keirescene")
            destination += ".keirescene";
        const auto assets = std::filesystem::weakly_canonical(m_AssetDatabase->Specification().ProjectRoot /
                                                              m_AssetDatabase->Specification().SourceDirectory);
        const auto parent = std::filesystem::weakly_canonical(destination.parent_path());
        const auto relativeParent = std::filesystem::relative(parent, assets);
        if (relativeParent.empty() || relativeParent.native().starts_with(std::filesystem::path("..").native()) ||
            destination.filename().empty())
            throw std::invalid_argument("Scene Save As must remain inside the project's Assets directory.");
        if (std::filesystem::exists(destination))
            throw std::invalid_argument("Scene Save As requires a new path and will not overwrite an existing asset.");
        auto definition = m_EditingScene->Snapshot();
        definition.Name = destination.stem().string();
        const auto bytes = Keire::SceneAsset::Encode(definition);
        const auto relative = relativeParent / destination.filename();
        const auto id = m_AssetDatabase->CreateAsset(relative, Keire::CreateSceneAssetImporter(), bytes);
        const auto components = m_EditingScene->Components();
        m_EditingScene = Keire::CreateRef<Keire::Scene>(id, std::move(definition), components);
        m_EditingScene->MarkSaved();
        m_SceneAsset = id;
        m_SceneSource = destination;
        if (const auto project = Owner().GetProject())
            m_SceneRecovery = project->SceneRecoveryDirectory() / (id.ToString() + ".keirescene.recovery");
        m_SelectedSceneObject = {};
        if (m_SceneUndoContext)
            m_SceneUndoContext->Close();
        if (const auto undo = Owner().Undo())
            m_SceneUndoContext = undo->CreateContext({.Name = "Scene: " + relative.stem().string()});
        m_ActiveUndoContext = m_SceneUndoContext;
        ImportAssets();
        if (const auto scenes = Owner().Scenes())
            m_SceneLoad = scenes->Load(id, Keire::SceneLoadMode::Single);
        m_SceneStatus = "Saved a new scene asset with a new stable identity.";
        AddConsoleMessage("Scene", "Saved As " + relative.generic_string(), m_Theme.Success);
    }
    catch (const std::exception& error)
    {
        m_SceneStatus = std::string("Scene Save As failed: ") + error.what();
        ReportError("Scene", m_SceneStatus);
    }
}

void EditorWorkspaceLayer::RequestCloseScene()
{
    if (m_EditingScene && m_EditingScene->Dirty())
    {
        m_PendingSceneAction = PendingSceneAction::Close;
        OpenDialog(Dialog::DirtyScene);
        return;
    }
    CloseScene();
}

void EditorWorkspaceLayer::CloseScene()
{
    if (const auto scenes = Owner().Scenes(); scenes && m_SceneAsset)
        (void)scenes->Unload(m_SceneAsset);
    if (m_EditingScene)
        m_EditingScene->Close();
    m_EditingScene.Reset();
    m_SceneLoad.Reset();
    m_SceneAsset = {};
    m_SelectedSceneObject = {};
    m_ComponentExpansion.clear();
    m_SceneSource.clear();
    DiscardSceneRecovery();
    m_SceneRecovery.clear();
    if (m_SceneUndoContext)
        m_SceneUndoContext->Close();
    m_SceneUndoContext.Reset();
    if (m_ActiveUndoContext && !m_ActiveUndoContext->IsOpen())
        m_ActiveUndoContext.Reset();
    m_SceneStatus = "No scene is open.";
}

void EditorWorkspaceLayer::WriteSceneRecovery()
{
    if (!m_EditingScene || !m_EditingScene->Dirty() || m_SceneRecovery.empty())
        return;
    std::filesystem::create_directories(m_SceneRecovery.parent_path());
    const auto bytes = Keire::SceneAsset::Encode(m_EditingScene->Snapshot());
    WriteBytesAtomically(m_SceneRecovery, bytes);
    m_SceneRecoveryAvailable = true;
    m_SceneStatus = "Scene recovery snapshot updated.";
}

void EditorWorkspaceLayer::RestoreSceneRecovery()
{
    if (!m_SceneRecoveryAvailable || !m_SceneAsset)
        return;
    const auto definition = Keire::SceneAsset::Decode(ReadBytes(m_SceneRecovery))->Definition();
    m_EditingScene = Keire::CreateRef<Keire::Scene>(m_SceneAsset, definition);
    m_EditingScene->MarkDirty();
    m_SelectedSceneObject = {};
    if (m_SceneUndoContext)
        m_SceneUndoContext->Clear();
    m_SceneRecoveryAvailable = false;
    m_SceneStatus = "Recovered unsaved scene changes. Save to commit them to the project.";
}

void EditorWorkspaceLayer::DiscardSceneRecovery() noexcept
{
    if (!m_SceneRecovery.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(m_SceneRecovery, ignored);
    }
    m_SceneRecoveryAvailable = false;
}

void EditorWorkspaceLayer::ExecutePendingSceneAction()
{
    const auto action = std::exchange(m_PendingSceneAction, PendingSceneAction::None);
    const auto asset = std::exchange(m_PendingSceneAsset, Keire::AssetId{});
    m_Dialog = Dialog::None;
    if (action == PendingSceneAction::Exit)
    {
        CloseScene();
        Owner().RequestExit();
        return;
    }
    CloseScene();
    try
    {
        if (action == PendingSceneAction::Create)
            CreateScene();
        else if (action == PendingSceneAction::Open)
            OpenScene(asset);
    }
    catch (const std::exception& error)
    {
        m_SceneStatus = std::string("Scene operation failed: ") + error.what();
        ReportError("Scene", m_SceneStatus);
    }
}

void EditorWorkspaceLayer::RecordSceneUndo(const std::string_view name, std::string mergeKey)
{
    if (!m_EditingScene || !m_SceneUndoContext || !m_SceneUndoContext->IsOpen())
        return;
    auto before = m_EditingScene->Snapshot();
    auto after = std::make_shared<std::optional<Keire::SceneDefinition>>();
    const auto asset = m_SceneAsset;
    const auto apply = [this, asset](const Keire::SceneDefinition& definition)
    {
        if (!m_EditingScene || m_SceneAsset != asset)
            return;
        const auto components = m_EditingScene->Components();
        m_EditingScene = Keire::CreateRef<Keire::Scene>(asset, definition, components);
        m_EditingScene->MarkDirty();
        m_SelectedSceneObject = {};
    };
    const auto estimatedBytes = Keire::SceneAsset::Encode(before).size();
    Keire::UndoOperation redo = [after, apply]
    {
        if (after->has_value())
            apply(**after);
    };
    Keire::UndoOperation undo = [this, before = std::move(before), after, apply]() mutable
    {
        if (!after->has_value() && m_EditingScene)
            *after = m_EditingScene->Snapshot();
        apply(before);
    };
    Keire::UndoAvailability available = [this, asset] { return m_EditingScene && m_SceneAsset == asset; };
    if (mergeKey.empty())
    {
        m_SceneUndoContext->RecordApplied(Keire::CreateUndoCommand(std::string(name), std::move(redo), std::move(undo),
                                                                   estimatedBytes, std::move(available)));
    }
    else
    {
        m_SceneUndoContext->RecordApplied(
            std::make_unique<ContinuousUndoCommand>(std::string(name), std::move(mergeKey), std::move(redo),
                                                    std::move(undo), estimatedBytes, std::move(available)));
    }
}

void EditorWorkspaceLayer::UndoSceneEdit()
{
    if (m_SceneUndoContext)
        (void)m_SceneUndoContext->Undo();
}

void EditorWorkspaceLayer::RedoSceneEdit()
{
    if (m_SceneUndoContext)
        (void)m_SceneUndoContext->Redo();
}

void EditorWorkspaceLayer::ApplyActiveUndo(const bool redo)
{
    if (!m_ActiveUndoContext)
        return;
    try
    {
        if (redo)
            (void)m_ActiveUndoContext->Redo();
        else
            (void)m_ActiveUndoContext->Undo();
    }
    catch (const std::exception& error)
    {
        m_Notice = std::string(redo ? "Redo failed: " : "Undo failed: ") + error.what();
        m_NoticeColor = m_Theme.Error;
        ReportError("Undo", m_Notice);
    }
}

void EditorWorkspaceLayer::DrawScene(Keire::UiFrame& ui)
{
    if (auto scenePanel = ui.BeginPanel(m_Scene); scenePanel)
    {
        if (ui.WindowFocused())
            m_ActiveUndoContext = m_SceneUndoContext;
        ui.TextColored(m_Theme.Accent, "SCENE");
        ui.Separator();
        if (!m_EditingScene)
        {
            DrawEmptyState(ui, "SCENE", "No scene is loaded.",
                           "Create or double-click a .keirescene asset in the Project panel.");
            return;
        }
        ui.Text(m_EditingScene->Name() + (m_EditingScene->Dirty() ? " *" : ""));
        ui.SameLine();
        if (ui.Button("Save"))
            SaveScene();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_SceneUndoContext || !m_SceneUndoContext->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
                UndoSceneEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_SceneUndoContext || !m_SceneUndoContext->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
                RedoSceneEdit();
        }
        ui.SameLine();
        const auto playState = m_PlaySession ? m_PlaySession->State() : Keire::ScenePlayState::Stopped;
        if (ui.Button(playState == Keire::ScenePlayState::Stopped ? "Play" : "Stop"))
        {
            if (playState == Keire::ScenePlayState::Stopped)
            {
                m_PlaySession = Keire::CreateRef<Keire::SceneRuntimeSession>(m_EditingScene);
                m_PlayFaultReported = false;
                m_PlaySession->Play();
            }
            else
            {
                m_PlaySession->Stop();
                m_PlaySession.Reset();
                m_PlayFaultReported = false;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(playState == Keire::ScenePlayState::Stopped ||
                                             playState == Keire::ScenePlayState::Faulted);
            disabled)
        {
            if (ui.Button(playState == Keire::ScenePlayState::Paused ? "Resume" : "Pause"))
                m_PlaySession->TogglePause();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(playState != Keire::ScenePlayState::Paused); disabled)
        {
            if (ui.Button("Step"))
                (void)m_PlaySession->Step(static_cast<float>(Owner().GetTime().FixedDeltaTime().Seconds()));
        }
        ui.SameLine();
        m_SceneGizmos->DrawToolbar(ui);
        ui.SameLine();
        if (ui.Button(m_EditorCamera->State().Projection == Keire::Detail::EditorCameraProjection::Perspective
                          ? "Persp"
                          : "Ortho"))
        {
            m_EditorCamera->ToggleProjection();
            m_SceneCameraDirty = true;
        }
        ui.SameLine();
        if (ui.Button("X"))
        {
            m_EditorCamera->Snap(Keire::Detail::EditorCameraAxis::PositiveX);
            m_SceneCameraDirty = true;
        }
        ui.SameLine();
        if (ui.Button("Y"))
        {
            m_EditorCamera->Snap(Keire::Detail::EditorCameraAxis::PositiveY);
            m_SceneCameraDirty = true;
        }
        ui.SameLine();
        if (ui.Button("Z"))
        {
            m_EditorCamera->Snap(Keire::Detail::EditorCameraAxis::PositiveZ);
            m_SceneCameraDirty = true;
        }
        ui.TextColored(m_Theme.MutedText, std::to_string(m_EditingScene->ObjectCount()) + " object(s)");
        if (m_SceneRecoveryAvailable)
        {
            ui.TextColored(m_Theme.Warning, "A recovery snapshot is available for this scene.");
            if (ui.Button("Restore Recovery"))
            {
                try
                {
                    RestoreSceneRecovery();
                }
                catch (const std::exception& error)
                {
                    m_SceneStatus = std::string("Scene recovery failed: ") + error.what();
                    ReportError("Scene", m_SceneStatus);
                }
            }
            ui.SameLine();
            if (ui.Button("Discard Recovery"))
                DiscardSceneRecovery();
        }
        if (!m_SceneStatus.empty())
            ui.TextColored(m_Theme.MutedText, m_SceneStatus);
        ui.Spacing();
        if (!m_SceneRenderView)
        {
            DrawEmptyState(ui, "SCENE", "The renderer is disabled.",
                           "Enable rendered or headless rendering in the application specification.");
            return;
        }

        const auto available = ui.ContentAvailable();
        const auto size = PrepareRenderSurface(m_SceneRenderView, available, Owner().MainWindow()->DisplayScale());
        const float aspect = size.Width / std::max(size.Height, 1.0F);
        Keire::RenderCamera camera;
        camera.View = m_EditorCamera->ViewMatrix();
        camera.Projection = m_EditorCamera->ProjectionMatrix(aspect);
        const auto renderScene = RenderedScene(m_EditingScene, m_PlaySession);
        if (const auto sceneCamera = SelectGameCamera(renderScene))
            camera.ClearColor = sceneCamera->Camera->ClearColor();
        else
            camera.ClearColor = {0.075F, 0.085F, 0.105F, 1.0F};
        m_SceneRenderView->SetCamera(camera);

        if (renderScene)
            Owner().Renderer()->Submit({renderScene, m_SceneRenderView, true, m_RenderEnvironment});
        ui.Image(m_SceneRenderView->Surface(), size);
        const auto imageState = ui.LastItemState();
        const auto imageRect = ui.LastItemRect();
        if (auto target = ui.BeginDragTarget(); target)
        {
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
            {
                try
                {
                    const auto assets = KeireEditor::AssetBrowserPanel::DecodeDragPayload(payload);
                    for (const auto asset : assets)
                    {
                        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
                        if (!record)
                            throw std::runtime_error("A dropped asset no longer exists in the project database.");
                        const auto extension = record->RelativePath.extension();
                        if (extension == ".keirescene")
                        {
                            RequestOpenScene(asset);
                            m_SceneStatus = "Opening " + record->RelativePath.generic_string() + ".";
                            continue;
                        }
                        if (extension == ".keireinput")
                        {
                            OpenInputActions(asset);
                            m_InputActionsEditor.SetVisible(true);
                            continue;
                        }
                        if (extension != ".keirematerial")
                        {
                            m_SceneStatus = "No Scene view drop action is registered for " +
                                            record->RelativePath.filename().string() + ".";
                            continue;
                        }
                        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
                            throw std::runtime_error("Stop Play mode before assigning authored scene materials.");
                        const auto hit = PickSceneEntity(m_EditingScene, imageRect, ui.PointerState().Position, camera);
                        const auto destination = hit ? m_EditingScene->FindEntity(hit.Id()) : Keire::Entity{};
                        const auto renderer = destination.GetComponent<Keire::MeshRendererComponent>();
                        if (!renderer)
                            throw std::runtime_error("Drop a material directly over an entity with a Mesh Renderer.");
                        const auto source = m_AssetDatabase->Specification().ProjectRoot /
                                            m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
                        const auto importer = Keire::CreateMaterialAssetImporter();
                        const auto material = Keire::MaterialAsset::Decode(importer.Import(ReadBytes(source)));
                        std::optional<Keire::Color> materialTint;
                        if (const auto tint = material->Definition().Properties.find("Tint");
                            tint != material->Definition().Properties.end())
                        {
                            if (const auto* color = std::get_if<Keire::Color>(&tint->second))
                                materialTint = *color;
                        }
                        RecordSceneUndo("Assign Material");
                        renderer->SetMaterial(asset);
                        if (materialTint)
                            renderer->SetTint(*materialTint);
                        m_SelectedSceneObject = destination.Id().Value();
                        m_SelectedAsset = {};
                        m_SceneStatus =
                            "Assigned " + record->RelativePath.stem().string() + " to " + destination.Name() + ".";
                    }
                }
                catch (const std::exception& error)
                {
                    m_SceneStatus = std::string("Scene asset drop failed: ") + error.what();
                    ReportError("Scene", m_SceneStatus);
                }
            }
        }
        if (renderScene)
        {
            const bool allowManipulation = !m_PlaySession || m_PlaySession->State() == Keire::ScenePlayState::Stopped;
            m_SelectedSceneObject =
                m_SceneGizmos
                    ->UpdateAndDraw(ui, renderScene, Keire::EntityId(m_SelectedSceneObject), camera, imageRect,
                                    allowManipulation, [this](const std::string_view name) { RecordSceneUndo(name); })
                    .Value();
        }
        UpdateSceneCamera(ui, imageState);
    }
}

void EditorWorkspaceLayer::UpdateSceneCamera(Keire::UiFrame& ui, const Keire::UiItemState& imageState)
{
    const auto pointer = ui.PointerState();
    const bool navigationRegion = imageState.Hovered || m_SceneCameraCapturing;
    bool changed = false;

    if (m_EditorCameraLockedEntity && m_EditingScene)
    {
        const auto locked = m_EditingScene->FindEntity(m_EditorCameraLockedEntity);
        if (locked)
        {
            SceneBounds bounds;
            IncludeEntityBounds(locked, bounds);
            if (bounds.Valid)
                m_EditorCamera->SetFocus(bounds.Center());
        }
        else
            m_EditorCameraLockedEntity = {};
    }

    if (imageState.Hovered && m_EditingScene && m_SelectedSceneObject)
    {
        const auto selected = m_EditingScene->FindEntity(Keire::EntityId(m_SelectedSceneObject));
        if (ui.Shortcut({.Key = Keire::UiKey::F, .Shift = true}))
        {
            m_EditorCameraLockedEntity =
                m_EditorCameraLockedEntity == selected.Id() ? Keire::EntityId{} : selected.Id();
            changed = true;
        }
        else if (ui.Shortcut({Keire::UiKey::F}))
        {
            SceneBounds bounds;
            IncludeEntityBounds(selected, bounds);
            if (bounds.Valid)
            {
                m_EditorCamera->Frame(bounds.Center(), bounds.Radius());
                changed = true;
            }
        }
    }

    if (imageState.Hovered && pointer.RightPressed && !ui.AltDown() && !m_SceneCameraCapturing)
    {
        Owner().Windows()->SetCursorMode(Owner().MainWindow()->Id(), Keire::CursorMode::RelativeLocked);
        m_SceneCameraCapturing = true;
    }
    else if (m_SceneCameraCapturing && (!pointer.RightDown || !ui.WindowFocused()))
    {
        Owner().Windows()->SetCursorMode(Owner().MainWindow()->Id(), Keire::CursorMode::Normal);
        m_SceneCameraCapturing = false;
        SaveSceneCamera();
    }

    Keire::Detail::EditorCameraInput input;
    input.PointerDelta = {pointer.Delta.X, pointer.Delta.Y};
    input.Wheel = navigationRegion ? pointer.Wheel : 0.0F;
    input.DeltaSeconds = static_cast<float>(Owner().GetTime().UnscaledDeltaTime().Seconds());
    input.Orbit = navigationRegion && ui.AltDown() && pointer.LeftDown;
    input.Pan = navigationRegion && pointer.MiddleDown;
    input.Zoom = navigationRegion && ui.AltDown() && pointer.RightDown;
    input.Fly = m_SceneCameraCapturing;
    input.Fast = ui.ShiftDown();
    if (m_SceneCameraCapturing || imageState.Hovered)
    {
        input.MoveForward = (ui.KeyDown(Keire::UiKey::W) || ui.KeyDown(Keire::UiKey::Up) ? 1.0F : 0.0F) -
                            (ui.KeyDown(Keire::UiKey::S) || ui.KeyDown(Keire::UiKey::Down) ? 1.0F : 0.0F);
        input.MoveRight = (ui.KeyDown(Keire::UiKey::D) || ui.KeyDown(Keire::UiKey::Right) ? 1.0F : 0.0F) -
                          (ui.KeyDown(Keire::UiKey::A) || ui.KeyDown(Keire::UiKey::Left) ? 1.0F : 0.0F);
        if (m_SceneCameraCapturing)
            input.MoveUp = (ui.KeyDown(Keire::UiKey::E) ? 1.0F : 0.0F) - (ui.KeyDown(Keire::UiKey::Q) ? 1.0F : 0.0F);
    }
    changed = m_EditorCamera->Update(input) || changed;

    if (changed)
    {
        if (input.Orbit || input.Pan || input.Zoom || input.Fly)
            m_EditorCameraLockedEntity = {};
        m_SceneCameraDirty = true;
    }
}

void EditorWorkspaceLayer::LoadSceneCamera()
{
    const auto project = Owner().GetProject();
    if (!project)
        return;
    std::ifstream input(project->Root() / "Library/Editor/SceneCamera.state");
    std::uint32_t version = 0;
    Keire::Detail::EditorCameraState state;
    if (!(input >> version >> state.Focus.X >> state.Focus.Y >> state.Focus.Z >> state.YawDegrees >>
          state.PitchDegrees >> state.Distance) ||
        (version != 1 && version != 2))
        return;
    if (version == 2)
    {
        std::uint32_t projection = 0;
        std::string locked;
        if (!(input >> state.OrthographicSize >> state.MoveSpeed >> projection >> locked) || projection > 1)
            return;
        state.Projection = static_cast<Keire::Detail::EditorCameraProjection>(projection);
        if (locked != "-")
            m_EditorCameraLockedEntity = Keire::EntityId::Parse(locked);
    }
    try
    {
        m_EditorCamera->SetState(state);
    }
    catch (...)
    {
        m_EditorCameraLockedEntity = {};
        return;
    }
    m_SceneCameraDirty = false;
}

void EditorWorkspaceLayer::SaveSceneCamera() noexcept
{
    if (!m_SceneCameraDirty)
        return;
    try
    {
        const auto project = Owner().GetProject();
        if (!project)
            return;
        std::ostringstream output;
        output.precision(9);
        const auto& state = m_EditorCamera->State();
        output << "2\n"
               << state.Focus.X << ' ' << state.Focus.Y << ' ' << state.Focus.Z << '\n'
               << state.YawDegrees << ' ' << state.PitchDegrees << ' ' << state.Distance << '\n'
               << state.OrthographicSize << ' ' << state.MoveSpeed << ' '
               << static_cast<std::uint32_t>(state.Projection) << ' '
               << (m_EditorCameraLockedEntity ? m_EditorCameraLockedEntity.ToString() : "-") << '\n';
        const auto path = project->Root() / "Library/Editor/SceneCamera.state";
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteTextFileAtomically(path, output.str());
        m_SceneCameraDirty = false;
    }
    catch (...)
    {
    }
}

void EditorWorkspaceLayer::DrawGame(Keire::UiFrame& ui)
{
    if (auto gamePanel = ui.BeginPanel(m_Game); gamePanel)
    {
        ui.TextColored(m_Theme.Accent, "GAME");
        ui.Separator();
        const auto scene = RenderedScene(m_EditingScene, m_PlaySession);
        if (!scene)
        {
            DrawEmptyState(ui, "GAME", "No scene is loaded.", "Open a scene to preview its active primary camera.");
            return;
        }
        if (!m_GameRenderView)
        {
            DrawEmptyState(ui, "GAME", "The renderer is disabled.",
                           "Enable rendered or headless rendering in the application specification.");
            return;
        }

        const auto selected = SelectGameCamera(scene);
        if (!selected)
        {
            DrawEmptyState(ui, "GAME", "No active primary camera.",
                           "Add an enabled Camera component and mark it Primary.");
            return;
        }

        ui.TextColored(
            m_Theme.MutedText,
            "Camera: " + selected->Entity.Name() +
                (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped ? " (Play)" : " (Edit)"));

        const auto available = ui.ContentAvailable();
        const auto size = PrepareRenderSurface(m_GameRenderView, available, Owner().MainWindow()->DisplayScale());
        const float aspect = size.Width / std::max(size.Height, 1.0F);
        Keire::RenderCamera camera;
        camera.View = Keire::Math::Inverse(selected->Transform->WorldMatrix());
        camera.Projection = selected->Camera->ProjectionMatrix(aspect);
        camera.ClearColor = selected->Camera->ClearColor();
        m_GameRenderView->SetCamera(camera);
        Owner().Renderer()->Submit({scene, m_GameRenderView, false, m_RenderEnvironment});
        ui.Image(m_GameRenderView->Surface(), size);
    }
}

void EditorWorkspaceLayer::DrawHierarchy(Keire::UiFrame& ui)
{
    if (auto hierarchy = ui.BeginPanel(m_Hierarchy); hierarchy)
    {
        if (ui.WindowFocused())
            m_ActiveUndoContext = m_SceneUndoContext;
        ui.TextColored(m_Theme.Accent, "HIERARCHY");
        ui.Separator();
        if (!m_EditingScene)
        {
            ui.Text("No scene entities.");
            return;
        }
        if (ui.Shortcut({Keire::UiKey::Delete}) && m_SelectedSceneObject)
        {
            RecordSceneUndo();
            (void)m_EditingScene->DestroyEntity(Keire::EntityId(m_SelectedSceneObject));
            m_SelectedSceneObject = {};
        }
        if (ui.Shortcut({Keire::UiKey::D, true}) && m_SelectedSceneObject)
        {
            RecordSceneUndo();
            m_SelectedSceneObject =
                m_EditingScene->DuplicateEntity(Keire::EntityId(m_SelectedSceneObject)).Id().Value();
        }
        if (ui.Shortcut({Keire::UiKey::F2}) && m_SelectedSceneObject)
        {
            const auto selected = m_EditingScene->Find(m_SelectedSceneObject).Snapshot();
            if (selected)
            {
                RecordSceneUndo();
                m_ProfileName = selected->Name;
                OpenDialog(Dialog::RenameEntity);
            }
        }
        const auto objects = m_EditingScene->Objects();
        const auto drawEntity = [&](const auto& self, const Keire::SceneObjectDefinition& object) -> void
        {
            auto id = ui.PushId(object.Id.ToString());
            const auto label = (object.Active ? std::string{} : std::string("[inactive] ")) + object.Name + "##tree";
            auto node = ui.BeginTreeNode(label);
            const auto state = ui.LastItemState();
            if (state.Activated)
                m_SelectedSceneObject = object.Id;
            if (auto context = ui.BeginItemContextMenu(); context)
            {
                m_SelectedSceneObject = object.Id;
                if (ui.MenuItem("Create Child"))
                {
                    RecordSceneUndo();
                    auto parent = m_EditingScene->FindEntity(Keire::EntityId(object.Id));
                    m_SelectedSceneObject = m_EditingScene->CreateEntity("GameObject", parent).Id().Value();
                }
                if (ui.MenuItem("Directional Light Child"))
                {
                    RecordSceneUndo();
                    auto parent = m_EditingScene->FindEntity(Keire::EntityId(object.Id));
                    auto created = m_EditingScene->CreateEntity("Directional Light", parent);
                    (void)created.AddComponent<Keire::DirectionalLightComponent>();
                    m_SelectedSceneObject = created.Id().Value();
                }
                ui.Separator();
                if (ui.MenuItem("Duplicate"))
                {
                    RecordSceneUndo();
                    m_SelectedSceneObject = m_EditingScene->DuplicateEntity(Keire::EntityId(object.Id)).Id().Value();
                }
                if (ui.MenuItem("Rename"))
                {
                    RecordSceneUndo();
                    m_ProfileName = object.Name;
                    OpenDialog(Dialog::RenameEntity);
                }
                if (ui.MenuItem("Delete"))
                {
                    RecordSceneUndo();
                    (void)m_EditingScene->DestroyEntity(Keire::EntityId(object.Id));
                    m_SelectedSceneObject = {};
                }
            }
            if (auto source = ui.BeginDragSource(); source)
            {
                const auto value = object.Id.ToString();
                ui.SetDragPayload("KEIRE_SCENE_OBJECT", std::as_bytes(std::span(value.data(), value.size())));
                ui.Text(object.Name);
            }
            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_SCENE_OBJECT", payload))
                {
                    const std::string value(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto child = Keire::AssetId::Parse(value);
                    if (child != object.Id)
                    {
                        RecordSceneUndo();
                        auto childEntity = m_EditingScene->FindEntity(Keire::EntityId(child));
                        childEntity.SetParent(m_EditingScene->FindEntity(Keire::EntityId(object.Id)), true);
                    }
                }
            }
            if (node)
                for (const auto& child : objects)
                    if (child.Parent == object.Id)
                        self(self, child);
        };
        for (const auto& object : objects)
            if (!object.Parent)
                drawEntity(drawEntity, object);
        if (auto context = ui.BeginWindowContextMenu("HierarchyBlank"); context)
        {
            if (ui.MenuItem("Create Empty"))
            {
                RecordSceneUndo();
                m_SelectedSceneObject = m_EditingScene->CreateEntity().Id().Value();
            }
            if (ui.MenuItem("Directional Light"))
            {
                RecordSceneUndo();
                auto created = m_EditingScene->CreateEntity("Directional Light");
                (void)created.AddComponent<Keire::DirectionalLightComponent>();
                m_SelectedSceneObject = created.Id().Value();
            }
        }
    }
}

void EditorWorkspaceLayer::AddConsoleMessage(std::string category, std::string message, const Keire::UiColor color,
                                             const Keire::LogLevel level) noexcept
{
    try
    {
        const auto logger = Keire::Log::GetClientLogger();
        logger.Write(level, '[' + category + "] " + message);
    }
    catch (...)
    {
        std::fprintf(stderr, "[%s] %s\n", category.c_str(), message.c_str());
    }
    try
    {
        constexpr std::size_t maximumMessages = 10'000;
        if (m_ConsoleMessages.size() == maximumMessages)
            m_ConsoleMessages.pop_front();
        m_ConsoleMessages.push_back({std::move(category), std::move(message), color, Owner().GetTime().FrameCount()});
    }
    catch (...)
    {
        std::fputs("Editor Console could not retain a log entry.\n", stderr);
    }
}

void EditorWorkspaceLayer::ReportError(std::string category, std::string message) noexcept
{
    AddConsoleMessage(std::move(category), std::move(message), m_Theme.Error, Keire::LogLevel::Error);
}

void EditorWorkspaceLayer::SetAssetError(std::string message) noexcept
{
    try
    {
        m_AssetStatus = message;
    }
    catch (...)
    {
    }
    ReportError("Assets", std::move(message));
}

void EditorWorkspaceLayer::DrawConsole(Keire::UiFrame& ui) { m_ConsolePanel->Draw(ui, *this); }

void EditorWorkspaceLayer::BeginInputTest()
{
    if (!m_InputContext || m_InputDocument.ActionMaps.empty())
        throw std::logic_error("Open an imported input action asset before starting Input Test Mode.");
    EndInputTest();
    std::vector<Keire::InputActionSubscription> subscriptions;
    std::vector<Keire::InputCaptureOverride> captureOverrides;
    try
    {
        for (const auto& map : m_InputDocument.ActionMaps)
        {
            if (!m_InputContext->EnableMap(map.Id))
                throw std::runtime_error("Input action context is still loading; try again next frame.");
            captureOverrides.push_back(m_InputContext->OverrideUiCapture(map.Id));
            for (const auto& action : map.Actions)
            {
                subscriptions.push_back(m_InputContext->Subscribe(
                    action.Id,
                    [this, actionId = action.Id, valueType = action.ValueType, mapName = map.Name,
                     actionName = action.Name](const Keire::InputActionEvent& event)
                    {
                        try
                        {
                            constexpr float inputEpsilon = 0.01F;
                            constexpr std::uint64_t coalescingWindowNanoseconds = 50'000'000;
                            constexpr std::size_t maximumHistoryEntries = 2048;
                            const auto magnitude =
                                std::sqrt(event.Value.X * event.Value.X + event.Value.Y * event.Value.Y);
                            const bool canceled = event.Phase == Keire::InputActionPhase::Canceled;
                            if (canceled && !m_InputRecordReleases)
                                return;
                            if (valueType != Keire::InputValueType::Boolean && magnitude <= inputEpsilon)
                                return;
                            const auto phase = event.Phase == Keire::InputActionPhase::Started     ? "Started"
                                               : event.Phase == Keire::InputActionPhase::Performed ? "Performed"
                                               : event.Phase == Keire::InputActionPhase::Canceled  ? "Canceled"
                                                                                                   : "Waiting";
                            if (event.Phase == Keire::InputActionPhase::Waiting)
                                return;
                            std::string scheme = "Automatic";
                            if (const auto input = Owner().Input())
                            {
                                const auto users = input->Users();
                                const auto user = std::ranges::find(users, event.User, &Keire::InputUserDescriptor::Id);
                                if (user != users.end() && !user->ControlScheme.empty())
                                    scheme = user->ControlScheme;
                            }
                            std::ostringstream message;
                            message << mapName << '/' << actionName << ' ' << phase << " value=[" << event.Value.X
                                    << ", " << event.Value.Y << "] user=" << event.User.Value()
                                    << " device=" << event.Device.Value() << " scheme=" << scheme
                                    << " duration=" << event.DurationSeconds
                                    << "s timestamp=" << event.TimestampNanoseconds << "ns";
                            if (!m_InputHistory.empty())
                            {
                                auto& previous = m_InputHistory.back();
                                if (previous.Action == actionId && previous.Phase == phase &&
                                    event.TimestampNanoseconds >= previous.TimestampNanoseconds &&
                                    event.TimestampNanoseconds - previous.TimestampNanoseconds <=
                                        coalescingWindowNanoseconds)
                                {
                                    previous.Value = event.Value;
                                    previous.User = event.User;
                                    previous.Device = event.Device;
                                    previous.TimestampNanoseconds = event.TimestampNanoseconds;
                                    ++previous.Repetitions;
                                    if (m_InputForwardToConsole)
                                        AddConsoleMessage("Input", message.str(), m_Theme.Accent);
                                    return;
                                }
                            }
                            if (m_InputHistory.size() == maximumHistoryEntries)
                                m_InputHistory.pop_front();
                            m_InputHistory.push_back({actionId, mapName, actionName, phase, event.Value, event.User,
                                                      event.Device, event.TimestampNanoseconds});
                            if (m_InputForwardToConsole)
                                AddConsoleMessage("Input", message.str(), m_Theme.Accent);
                        }
                        catch (...)
                        {
                            ReportError("Input", "Input debugger event processing failed with an unknown error.");
                        }
                    }));
            }
        }
    }
    catch (...)
    {
        m_InputContext->DisableAll();
        throw;
    }
    m_InputSubscriptions = std::move(subscriptions);
    m_InputCaptureOverrides = std::move(captureOverrides);
    m_InputTesting = true;
    m_InputMessage = "Input Test Mode is active. Events stay in debugger history unless Console forwarding is enabled.";
}

void EditorWorkspaceLayer::EndInputTest() noexcept
{
    m_InputSubscriptions.clear();
    m_InputCaptureOverrides.clear();
    if (m_InputContext)
        m_InputContext->DisableAll();
    m_InputTesting = false;
}

void EditorWorkspaceLayer::DrawInputDebugger(Keire::UiFrame& ui)
{
    if (auto debugger = ui.BeginPanel(m_InputDebugger); debugger)
    {
        ui.TextColored(m_Theme.Accent, "INPUT DEBUGGER");
        ui.Separator();
        if (!m_InputAsset)
        {
            ui.Text("No input action asset is attached.");
            if (const auto project = Owner().GetProject(); project && project->Descriptor().DefaultInput)
            {
                if (ui.Button("Attach Project Default Input"))
                {
                    try
                    {
                        OpenInputActions(project->Descriptor().DefaultInput);
                    }
                    catch (const std::exception& error)
                    {
                        m_InputMessage = error.what();
                        ReportError("Input", m_InputMessage);
                    }
                }
            }
            return;
        }
        ui.Text(m_InputDocument.Name);
        if (!m_InputTesting)
        {
            if (ui.Button("Start Input Test"))
            {
                try
                {
                    BeginInputTest();
                }
                catch (const std::exception& error)
                {
                    m_InputMessage = error.what();
                    ReportError("Input", m_InputMessage);
                }
            }
        }
        else if (ui.Button("Stop Input Test"))
            EndInputTest();
        ui.SameLine();
        if (ui.Button("Clear History"))
            m_InputHistory.clear();
        (void)ui.Checkbox("Forward to Console", m_InputForwardToConsole);
        ui.SameLine();
        (void)ui.Checkbox("Record releases", m_InputRecordReleases);
        if (!m_InputMessage.empty())
            ui.TextColored(m_Theme.MutedText, m_InputMessage);
        if (const auto input = Owner().Input())
        {
            ui.Separator();
            ui.Text("DEVICES");
            for (const auto& device : input->Devices())
            {
                ui.Text(device.Name + "  id=" + std::to_string(device.Id.Value()) +
                        (device.Connected ? "  connected" : "  disconnected") +
                        (device.Paired ? "  paired" : "  unpaired"));
            }
            ui.Text("USERS");
            for (const auto& user : input->Users())
                ui.Text(user.Name + "  scheme=" + (user.ControlScheme.empty() ? "Automatic" : user.ControlScheme));
        }
        ui.Separator();
        ui.Text("EVENT HISTORY");
        if (m_InputHistory.empty())
        {
            ui.TextColored(m_Theme.MutedText, "Press a bound control to record an event. Idle input is filtered.");
        }
        else
        {
            for (auto entry = m_InputHistory.rbegin(); entry != m_InputHistory.rend(); ++entry)
            {
                std::ostringstream text;
                text << entry->Map << '/' << entry->Name << "  " << entry->Phase << "  [" << entry->Value.X << ", "
                     << entry->Value.Y << "]  user=" << entry->User.Value() << " device=" << entry->Device.Value();
                if (entry->Repetitions > 1)
                    text << "  x" << entry->Repetitions;
                ui.Text(text.str());
            }
        }
    }
}

void EditorWorkspaceLayer::DrawInputActionsEditor(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_InputActionsEditor); panel)
    {
        if (ui.WindowFocused())
            m_ActiveUndoContext = m_InputUndoContext;
        if (!m_InputAsset)
        {
            DrawEmptyState(ui, "INPUT ACTIONS", "No input action asset is open.",
                           "Select a .keireinput asset and choose Edit Input Actions in the Inspector.");
            return;
        }

        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(m_InputAsset) : std::nullopt;
        ui.TextColored(m_Theme.Accent, "INPUT ACTIONS");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (m_InputDirty ? " *" : "") : "Missing asset");
        ui.Separator();
        if (ui.Shortcut({Keire::UiKey::S, true}) && m_InputDirty)
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
                ReportError("Input", m_InputMessage);
            }
        }
        if (ui.Button("Save"))
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
                ReportError("Input", m_InputMessage);
            }
        }
        ui.SameLine();
        if (ui.Button("Revert"))
        {
            try
            {
                OpenInputActions(m_InputAsset);
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
                ReportError("Input", m_InputMessage);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_InputUndoContext || !m_InputUndoContext->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
                UndoInputEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_InputUndoContext || !m_InputUndoContext->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
                RedoInputEdit();
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::InputActionAsset::Validate(m_InputDocument);
                m_InputMessage = "Validation passed.";
            }
            catch (const std::exception& error)
            {
                m_InputMessage = error.what();
                ReportError("Input", m_InputMessage);
            }
        }
        ui.SameLine();
        (void)ui.Checkbox("Live Monitor", m_InputLiveMonitor);
        (void)ui.InputText("Search", m_InputSearch);
        if (!m_InputMessage.empty())
            ui.TextColored(m_Theme.MutedText, m_InputMessage);
        ui.Separator();

        auto findMap = [&]() -> Keire::InputActionMapDefinition*
        {
            const auto found =
                std::ranges::find(m_InputDocument.ActionMaps, m_SelectedInputMap, &Keire::InputActionMapDefinition::Id);
            return found == m_InputDocument.ActionMaps.end() ? nullptr : &*found;
        };
        auto map = findMap();
        if (auto maps = ui.BeginChild("InputMaps", {230.0F, 0.0F}, true); maps)
        {
            ui.TextColored(m_Theme.Accent, "ACTION MAPS");
            for (const auto& candidate : m_InputDocument.ActionMaps)
            {
                if (!m_InputSearch.empty() && candidate.Name.find(m_InputSearch) == std::string::npos)
                    continue;
                if (ui.Selectable(candidate.Name, candidate.Id == m_SelectedInputMap))
                {
                    m_SelectedInputMap = candidate.Id;
                    m_SelectedInputScheme = {};
                    m_SelectedInputAction = {};
                    m_SelectedInputBinding = {};
                }
            }
            ui.Separator();
            ui.TextColored(m_Theme.MutedText, "CONTROL SCHEMES");
            for (const auto& scheme : m_InputDocument.ControlSchemes)
            {
                if (ui.Selectable(scheme.Name + "  [" + scheme.BindingGroup + "]", scheme.Id == m_SelectedInputScheme))
                {
                    m_SelectedInputScheme = scheme.Id;
                    m_SelectedInputMap = {};
                    m_SelectedInputAction = {};
                    m_SelectedInputBinding = {};
                }
            }
            if (ui.Button("+ Map"))
            {
                RecordInputUndo();
                Keire::InputActionMapDefinition added;
                added.Id = Keire::AssetId::Generate();
                added.Name =
                    UniqueInputName(m_InputDocument.ActionMaps, "New Map", &Keire::InputActionMapDefinition::Name);
                m_SelectedInputMap = added.Id;
                m_SelectedInputScheme = {};
                m_InputDocument.ActionMaps.push_back(std::move(added));
            }
            ui.SameLine();
            if (ui.Button("+ Scheme"))
            {
                RecordInputUndo("Add Control Scheme");
                Keire::InputControlSchemeDefinition added;
                added.Id = Keire::AssetId::Generate();
                added.Name = UniqueInputName(m_InputDocument.ControlSchemes, "New Scheme",
                                             &Keire::InputControlSchemeDefinition::Name);
                added.BindingGroup = UniqueInputName(m_InputDocument.ControlSchemes, "NewScheme",
                                                     &Keire::InputControlSchemeDefinition::BindingGroup);
                added.Devices.push_back({"Keyboard", false});
                m_SelectedInputScheme = added.Id;
                m_SelectedInputMap = {};
                m_SelectedInputAction = {};
                m_SelectedInputBinding = {};
                m_InputDocument.ControlSchemes.push_back(std::move(added));
            }
            if (m_SelectedInputMap && ui.Button("Duplicate Map"))
            {
                const auto source = std::ranges::find(m_InputDocument.ActionMaps, m_SelectedInputMap,
                                                      &Keire::InputActionMapDefinition::Id);
                if (source != m_InputDocument.ActionMaps.end())
                {
                    RecordInputUndo("Duplicate Action Map");
                    auto copy = *source;
                    copy.Id = Keire::AssetId::Generate();
                    copy.Name = UniqueInputName(m_InputDocument.ActionMaps, copy.Name + " Copy",
                                                &Keire::InputActionMapDefinition::Name);
                    std::vector<std::pair<Keire::AssetId, Keire::AssetId>> actionIds;
                    for (auto& action : copy.Actions)
                    {
                        const auto previous = action.Id;
                        action.Id = Keire::AssetId::Generate();
                        actionIds.emplace_back(previous, action.Id);
                    }
                    for (auto& binding : copy.Bindings)
                    {
                        binding.Id = Keire::AssetId::Generate();
                        const auto action = std::ranges::find_if(actionIds, [&](const auto& identities)
                                                                 { return identities.first == binding.Action; });
                        if (action != actionIds.end())
                            binding.Action = action->second;
                    }
                    m_SelectedInputMap = copy.Id;
                    m_InputDocument.ActionMaps.push_back(std::move(copy));
                }
            }
            ui.SameLine();
            if (m_SelectedInputMap && ui.Button("Delete Map"))
            {
                RecordInputUndo("Delete Action Map");
                std::erase_if(m_InputDocument.ActionMaps,
                              [&](const auto& actionMap) { return actionMap.Id == m_SelectedInputMap; });
                m_SelectedInputMap =
                    m_InputDocument.ActionMaps.empty() ? Keire::AssetId{} : m_InputDocument.ActionMaps.front().Id;
                m_SelectedInputAction = {};
                m_SelectedInputBinding = {};
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto actions = ui.BeginChild("InputActions", {430.0F, 0.0F}, true); actions)
        {
            ui.TextColored(m_Theme.Accent, map ? map->Name : "ACTIONS");
            if (!map)
                ui.TextColored(m_Theme.MutedText, "Select or create an action map.");
            else
            {
                if (m_InputContext)
                    (void)m_InputContext->EnableMap(map->Id);
                for (const auto& action : map->Actions)
                {
                    auto actionId = ui.PushId(action.Id.ToString());
                    if (ui.Selectable(action.Name, action.Id == m_SelectedInputAction))
                    {
                        m_SelectedInputAction = action.Id;
                        m_SelectedInputBinding = {};
                    }
                    for (const auto& binding : map->Bindings)
                    {
                        if (binding.Action != action.Id)
                            continue;
                        auto bindingId = ui.PushId(binding.Id.ToString());
                        const auto detail = !binding.Composite.empty() ? std::string("[") + binding.Composite + "]"
                                            : !binding.CompositePart.empty()
                                                ? binding.CompositePart + ": " + binding.Path
                                            : binding.Name.empty() ? binding.Path
                                                                   : binding.Name + ": " + binding.Path;
                        const auto label = "   " + detail;
                        if (ui.Selectable(label, binding.Id == m_SelectedInputBinding))
                        {
                            m_SelectedInputAction = action.Id;
                            m_SelectedInputBinding = binding.Id;
                        }
                    }
                }
                if (ui.Button("+ Action"))
                {
                    RecordInputUndo();
                    Keire::InputActionDefinition action;
                    action.Id = Keire::AssetId::Generate();
                    action.Name = UniqueInputName(map->Actions, "New Action", &Keire::InputActionDefinition::Name);
                    m_SelectedInputAction = action.Id;
                    map->Actions.push_back(std::move(action));
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(!m_SelectedInputAction); disabled)
                {
                    if (ui.Button("+ Binding"))
                    {
                        RecordInputUndo();
                        Keire::InputBindingDefinition binding;
                        binding.Id = Keire::AssetId::Generate();
                        binding.Action = m_SelectedInputAction;
                        binding.Path = "<Keyboard>/space";
                        m_SelectedInputBinding = binding.Id;
                        map->Bindings.push_back(std::move(binding));
                    }
                    ui.SameLine();
                    if (ui.Button("+ 1D Axis"))
                    {
                        RecordInputUndo("Add 1D Composite");
                        if (const auto action = std::ranges::find(map->Actions, m_SelectedInputAction,
                                                                  &Keire::InputActionDefinition::Id);
                            action != map->Actions.end())
                        {
                            action->Type = Keire::InputActionType::Value;
                            action->ValueType = Keire::InputValueType::Axis1D;
                        }
                        Keire::InputBindingDefinition root;
                        root.Id = Keire::AssetId::Generate();
                        root.Action = m_SelectedInputAction;
                        root.Name = "Axis";
                        root.Composite = "Axis1D";
                        map->Bindings.push_back(root);
                        for (const auto& [part, path] :
                             {std::pair{"Negative", "<Keyboard>/a"}, std::pair{"Positive", "<Keyboard>/d"}})
                        {
                            Keire::InputBindingDefinition child;
                            child.Id = Keire::AssetId::Generate();
                            child.Action = m_SelectedInputAction;
                            child.Name = part;
                            child.Path = path;
                            child.CompositePart = part;
                            map->Bindings.push_back(std::move(child));
                        }
                        m_SelectedInputBinding = root.Id;
                    }
                    ui.SameLine();
                    if (ui.Button("+ 2D Vector"))
                    {
                        RecordInputUndo("Add 2D Composite");
                        if (const auto action = std::ranges::find(map->Actions, m_SelectedInputAction,
                                                                  &Keire::InputActionDefinition::Id);
                            action != map->Actions.end())
                        {
                            action->Type = Keire::InputActionType::Value;
                            action->ValueType = Keire::InputValueType::Axis2D;
                        }
                        Keire::InputBindingDefinition root;
                        root.Id = Keire::AssetId::Generate();
                        root.Action = m_SelectedInputAction;
                        root.Name = "Vector";
                        root.Composite = "Vector2";
                        map->Bindings.push_back(root);
                        for (const auto& [part, path] :
                             {std::pair{"Up", "<Keyboard>/w"}, std::pair{"Down", "<Keyboard>/s"},
                              std::pair{"Left", "<Keyboard>/a"}, std::pair{"Right", "<Keyboard>/d"}})
                        {
                            Keire::InputBindingDefinition child;
                            child.Id = Keire::AssetId::Generate();
                            child.Action = m_SelectedInputAction;
                            child.Name = part;
                            child.Path = path;
                            child.CompositePart = part;
                            map->Bindings.push_back(std::move(child));
                        }
                        m_SelectedInputBinding = root.Id;
                    }
                }
                if (m_SelectedInputBinding && ui.Button("Delete Binding"))
                {
                    RecordInputUndo("Delete Binding");
                    auto binding =
                        std::ranges::find(map->Bindings, m_SelectedInputBinding, &Keire::InputBindingDefinition::Id);
                    if (binding != map->Bindings.end())
                    {
                        if (!binding->Composite.empty())
                        {
                            auto end = std::next(binding);
                            while (end != map->Bindings.end() && !end->CompositePart.empty())
                                ++end;
                            map->Bindings.erase(binding, end);
                        }
                        else
                            map->Bindings.erase(binding);
                    }
                    m_SelectedInputBinding = {};
                }
                else if (m_SelectedInputAction && ui.Button("Duplicate Action"))
                {
                    const auto source =
                        std::ranges::find(map->Actions, m_SelectedInputAction, &Keire::InputActionDefinition::Id);
                    if (source != map->Actions.end())
                    {
                        RecordInputUndo("Duplicate Action");
                        auto copy = *source;
                        const auto sourceId = copy.Id;
                        copy.Id = Keire::AssetId::Generate();
                        copy.Name =
                            UniqueInputName(map->Actions, copy.Name + " Copy", &Keire::InputActionDefinition::Name);
                        m_SelectedInputAction = copy.Id;
                        map->Actions.push_back(copy);
                        for (const auto& binding : std::vector<Keire::InputBindingDefinition>(map->Bindings))
                        {
                            if (binding.Action != sourceId)
                                continue;
                            auto bindingCopy = binding;
                            bindingCopy.Id = Keire::AssetId::Generate();
                            bindingCopy.Action = copy.Id;
                            if (!bindingCopy.Name.empty())
                                bindingCopy.Name = UniqueInputName(map->Bindings, bindingCopy.Name + " Copy",
                                                                   &Keire::InputBindingDefinition::Name);
                            map->Bindings.push_back(std::move(bindingCopy));
                        }
                    }
                }
                ui.SameLine();
                if (m_SelectedInputAction && ui.Button("Delete Action"))
                {
                    RecordInputUndo("Delete Action");
                    std::erase_if(map->Bindings,
                                  [&](const auto& binding) { return binding.Action == m_SelectedInputAction; });
                    std::erase_if(map->Actions, [&](const auto& action) { return action.Id == m_SelectedInputAction; });
                    m_SelectedInputAction = {};
                    m_SelectedInputBinding = {};
                }
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto properties = ui.BeginChild("InputProperties", {}, true); properties)
        {
            ui.TextColored(m_Theme.Accent, "PROPERTIES");
            const auto drawBehaviors = [&](const std::string_view heading,
                                           std::vector<Keire::InputBehaviorDefinition>& behaviors,
                                           const bool interactions)
            {
                ui.Separator();
                ui.TextColored(m_Theme.Accent, heading);
                for (std::size_t index = 0; index < behaviors.size(); ++index)
                {
                    auto behaviorId = ui.PushId(std::string(heading) + std::to_string(index));
                    auto& behavior = behaviors[index];
                    ui.Text(behavior.Name);
                    ui.SameLine();
                    if (ui.Button("Remove"))
                    {
                        RecordInputUndo("Remove " + behavior.Name);
                        behaviors.erase(behaviors.begin() + static_cast<std::ptrdiff_t>(index));
                        --index;
                        continue;
                    }
                    for (auto& parameter : behavior.Parameters)
                    {
                        if (behavior.Name == "Invert" && (parameter.Name == "x" || parameter.Name == "y"))
                        {
                            bool enabled = parameter.Value != 0.0;
                            if (ui.Checkbox("Invert " + std::string(parameter.Name), enabled))
                            {
                                RecordInputUndo("Change Invert Axis");
                                parameter.Value = enabled ? 1.0 : 0.0;
                            }
                            continue;
                        }
                        float minimum = -1000.0F;
                        float maximum = 1000.0F;
                        if (parameter.Name == "pressPoint" || parameter.Name == "minimum" ||
                            parameter.Name == "maximum")
                        {
                            minimum = 0.0F;
                            maximum = 1.0F;
                        }
                        else if (parameter.Name == "duration" || parameter.Name == "delay")
                        {
                            minimum = 0.01F;
                            maximum = behavior.Name == "Hold" ? 60.0F : 10.0F;
                        }
                        else if (parameter.Name == "count")
                        {
                            minimum = 2.0F;
                            maximum = 16.0F;
                        }
                        float value = static_cast<float>(parameter.Value);
                        if (ui.SliderFloat(parameter.Name, value, minimum, maximum))
                        {
                            RecordInputUndo("Change " + behavior.Name + " Parameter");
                            parameter.Value = parameter.Name == "count" ? std::round(value) : value;
                        }
                    }
                }
                if (behaviors.size() >= 8)
                    return;
                if (auto add = ui.BeginCombo("Add " + std::string(heading), "Choose..."); add)
                {
                    const auto addBehavior =
                        [&](const std::string_view name, std::initializer_list<Keire::InputParameter> parameters)
                    {
                        if (std::ranges::find(behaviors, name, &Keire::InputBehaviorDefinition::Name) !=
                            behaviors.end())
                            return;
                        RecordInputUndo("Add " + std::string(name));
                        behaviors.push_back({std::string(name), parameters});
                    };
                    if (interactions)
                    {
                        if (ui.MenuItem("Press"))
                            addBehavior("Press", {{"pressPoint", 0.5}});
                        if (ui.MenuItem("Tap"))
                            addBehavior("Tap", {{"duration", 0.2}, {"pressPoint", 0.5}});
                        if (ui.MenuItem("Hold"))
                            addBehavior("Hold", {{"duration", 0.4}, {"pressPoint", 0.5}});
                        if (ui.MenuItem("Multi Tap"))
                            addBehavior("MultiTap",
                                        {{"duration", 0.2}, {"delay", 0.75}, {"count", 2.0}, {"pressPoint", 0.5}});
                    }
                    else
                    {
                        if (ui.MenuItem("Deadzone"))
                            addBehavior("Deadzone", {{"minimum", 0.125}, {"maximum", 0.925}});
                        if (ui.MenuItem("Scale"))
                            addBehavior("Scale", {{"x", 1.0}, {"y", 1.0}});
                        if (ui.MenuItem("Invert"))
                            addBehavior("Invert", {{"x", 1.0}, {"y", 1.0}});
                        if (ui.MenuItem("Normalize"))
                            addBehavior("Normalize", {});
                    }
                }
            };
            if (!map)
            {
                const auto scheme = std::ranges::find(m_InputDocument.ControlSchemes, m_SelectedInputScheme,
                                                      &Keire::InputControlSchemeDefinition::Id);
                if (scheme == m_InputDocument.ControlSchemes.end())
                {
                    ui.TextColored(m_Theme.MutedText, "Select an action map, action, binding, or control scheme.");
                    return;
                }
                auto name = scheme->Name;
                if (ui.InputText("Scheme Name", name))
                {
                    RecordInputUndo("Rename Control Scheme");
                    scheme->Name = std::move(name);
                }
                auto group = scheme->BindingGroup;
                if (ui.InputText("Binding Group", group))
                {
                    RecordInputUndo("Change Binding Group");
                    const auto previous = scheme->BindingGroup;
                    scheme->BindingGroup = std::move(group);
                    for (auto& actionMap : m_InputDocument.ActionMaps)
                        for (auto& binding : actionMap.Bindings)
                            std::ranges::replace(binding.Groups, previous, scheme->BindingGroup);
                }
                ui.Separator();
                ui.TextColored(m_Theme.Accent, "DEVICES");
                for (const std::string_view family : {"Keyboard", "Mouse", "Gamepad"})
                {
                    auto device = std::ranges::find(scheme->Devices, family, &Keire::InputDeviceRequirement::Device);
                    bool required = device != scheme->Devices.end();
                    if (ui.Checkbox(std::string(family), required))
                    {
                        if (required)
                        {
                            RecordInputUndo("Add Scheme Device");
                            scheme->Devices.push_back({std::string(family), false});
                        }
                        else if (scheme->Devices.size() > 1)
                        {
                            RecordInputUndo("Remove Scheme Device");
                            scheme->Devices.erase(device);
                        }
                        else
                            m_InputMessage = "A control scheme requires at least one device family.";
                    }
                    device = std::ranges::find(scheme->Devices, family, &Keire::InputDeviceRequirement::Device);
                    if (device != scheme->Devices.end())
                    {
                        ui.SameLine();
                        bool optional = device->Optional;
                        if (ui.Checkbox("Optional##" + std::string(family), optional))
                        {
                            RecordInputUndo("Change Device Requirement");
                            device->Optional = optional;
                        }
                    }
                }
                ui.Separator();
                if (ui.Button("Delete Control Scheme"))
                {
                    RecordInputUndo("Delete Control Scheme");
                    const auto removedGroup = scheme->BindingGroup;
                    m_InputDocument.ControlSchemes.erase(scheme);
                    for (auto& actionMap : m_InputDocument.ActionMaps)
                        for (auto& binding : actionMap.Bindings)
                            std::erase(binding.Groups, removedGroup);
                    m_SelectedInputScheme = {};
                    if (!m_InputDocument.ActionMaps.empty())
                        m_SelectedInputMap = m_InputDocument.ActionMaps.front().Id;
                }
                return;
            }
            if (!m_SelectedInputAction)
            {
                auto name = map->Name;
                if (ui.InputText("Map Name", name))
                {
                    RecordInputUndo();
                    map->Name = std::move(name);
                }
                bool alwaysReceive = map->CapturePolicy == Keire::InputCapturePolicy::AlwaysReceive;
                if (ui.Checkbox("Always Receive", alwaysReceive))
                {
                    RecordInputUndo();
                    map->CapturePolicy = alwaysReceive ? Keire::InputCapturePolicy::AlwaysReceive
                                                       : Keire::InputCapturePolicy::RespectUiCapture;
                }
            }
            else
            {
                auto action = std::ranges::find(map->Actions, m_SelectedInputAction, &Keire::InputActionDefinition::Id);
                if (action != map->Actions.end())
                {
                    auto name = action->Name;
                    if (ui.InputText("Action Name", name))
                    {
                        RecordInputUndo();
                        action->Name = std::move(name);
                    }
                    const auto actionTypeName = [](const Keire::InputActionType type)
                    {
                        switch (type)
                        {
                        case Keire::InputActionType::Button:
                            return "Button";
                        case Keire::InputActionType::Value:
                            return "Value";
                        case Keire::InputActionType::PassThrough:
                            return "Pass Through";
                        }
                        return "Unknown";
                    };
                    if (auto combo = ui.BeginCombo("Action Type", actionTypeName(action->Type)); combo)
                    {
                        for (const auto type : {Keire::InputActionType::Button, Keire::InputActionType::Value,
                                                Keire::InputActionType::PassThrough})
                        {
                            if (ui.Selectable(actionTypeName(type), action->Type == type) && action->Type != type)
                            {
                                RecordInputUndo("Change Action Type");
                                action->Type = type;
                                if (type == Keire::InputActionType::Button)
                                {
                                    action->ValueType = Keire::InputValueType::Boolean;
                                    std::erase_if(map->Bindings,
                                                  [&](const auto& binding)
                                                  {
                                                      return binding.Action == action->Id &&
                                                             (!binding.Composite.empty() ||
                                                              !binding.CompositePart.empty());
                                                  });
                                }
                                if (type == Keire::InputActionType::PassThrough)
                                {
                                    action->Interactions.clear();
                                    for (auto& binding : map->Bindings)
                                        if (binding.Action == action->Id)
                                            binding.Interactions.clear();
                                }
                            }
                        }
                    }
                    const auto valueTypeName = [](const Keire::InputValueType type)
                    {
                        switch (type)
                        {
                        case Keire::InputValueType::Boolean:
                            return "Boolean";
                        case Keire::InputValueType::Axis1D:
                            return "Axis (1D)";
                        case Keire::InputValueType::Axis2D:
                            return "Vector (2D)";
                        }
                        return "Unknown";
                    };
                    if (auto disabled = ui.BeginDisabled(action->Type == Keire::InputActionType::Button); disabled)
                    {
                        if (auto combo = ui.BeginCombo("Value Type", valueTypeName(action->ValueType)); combo)
                        {
                            for (const auto type : {Keire::InputValueType::Boolean, Keire::InputValueType::Axis1D,
                                                    Keire::InputValueType::Axis2D})
                            {
                                if (ui.Selectable(valueTypeName(type), action->ValueType == type) &&
                                    action->ValueType != type)
                                {
                                    RecordInputUndo("Change Action Value Type");
                                    action->ValueType = type;
                                    std::erase_if(map->Bindings,
                                                  [&](const auto& binding)
                                                  {
                                                      if (binding.Action != action->Id)
                                                          return false;
                                                      return !binding.Composite.empty() ||
                                                             !binding.CompositePart.empty();
                                                  });
                                }
                            }
                        }
                    }
                    if (action->Type != Keire::InputActionType::PassThrough)
                        drawBehaviors("INTERACTIONS", action->Interactions, true);
                    drawBehaviors("PROCESSORS", action->Processors, false);
                }
                if (m_SelectedInputBinding)
                {
                    auto binding =
                        std::ranges::find(map->Bindings, m_SelectedInputBinding, &Keire::InputBindingDefinition::Id);
                    if (binding != map->Bindings.end())
                    {
                        auto name = binding->Name;
                        if (ui.InputText("Binding Name", name))
                        {
                            RecordInputUndo();
                            binding->Name = std::move(name);
                        }
                        if (!binding->Composite.empty())
                        {
                            ui.TextColored(m_Theme.MutedText, "Composite");
                            ui.Text(binding->Composite == "Axis1D" ? "1D Axis" : "2D Vector");
                        }
                        else
                        {
                            if (!binding->CompositePart.empty())
                            {
                                ui.TextColored(m_Theme.MutedText, "Composite Part");
                                ui.Text(binding->CompositePart);
                            }
                            if (auto controls = ui.BeginCombo(
                                    "Control Browser", binding->Path.empty() ? "Choose a control..." : binding->Path);
                                controls)
                            {
                                for (const std::string_view path :
                                     {"<Keyboard>/space", "<Keyboard>/enter", "<Keyboard>/escape", "<Keyboard>/w",
                                      "<Keyboard>/a", "<Keyboard>/s", "<Keyboard>/d", "<Mouse>/position",
                                      "<Mouse>/delta", "<Mouse>/leftButton", "<Mouse>/rightButton", "<Mouse>/scroll",
                                      "<Gamepad>/leftStick", "<Gamepad>/rightStick", "<Gamepad>/buttonSouth",
                                      "<Gamepad>/buttonEast", "<Gamepad>/leftTrigger", "<Gamepad>/rightTrigger"})
                                {
                                    if (ui.Selectable(path, binding->Path == path))
                                    {
                                        RecordInputUndo("Choose Control Path");
                                        binding->Path = path;
                                    }
                                }
                            }
                            auto path = binding->Path;
                            if (ui.InputText("Control Path", path))
                            {
                                RecordInputUndo("Change Control Path");
                                binding->Path = std::move(path);
                            }
                        }
                        ui.Separator();
                        ui.TextColored(m_Theme.Accent, "BINDING GROUPS");
                        for (const auto& scheme : m_InputDocument.ControlSchemes)
                        {
                            bool included =
                                std::ranges::find(binding->Groups, scheme.BindingGroup) != binding->Groups.end();
                            if (ui.Checkbox(scheme.Name + "##" + scheme.Id.ToString(), included))
                            {
                                RecordInputUndo("Change Binding Group");
                                if (included)
                                    binding->Groups.push_back(scheme.BindingGroup);
                                else
                                    std::erase(binding->Groups, scheme.BindingGroup);
                            }
                        }
                        if (action != map->Actions.end() && action->Type != Keire::InputActionType::PassThrough)
                            drawBehaviors("BINDING INTERACTIONS", binding->Interactions, true);
                        drawBehaviors("BINDING PROCESSORS", binding->Processors, false);
                        if (binding->Composite.empty() && ui.Button("Listen"))
                        {
                            try
                            {
                                if (!m_InputContext)
                                    throw std::runtime_error("The runtime input context is not ready.");
                                m_Rebind = Owner().Input()->BeginInteractiveRebind(m_InputContext, binding->Id);
                                m_InputMessage = "Listening for a control...";
                            }
                            catch (const std::exception& error)
                            {
                                m_InputMessage = error.what();
                                ReportError("Input", m_InputMessage);
                            }
                        }
                        if (m_Rebind)
                        {
                            const auto status = m_Rebind->Status();
                            if (status == Keire::RebindStatus::Listening)
                            {
                                ui.Text("Listening... " + std::to_string(m_Rebind->RemainingSeconds()) + "s");
                                ui.ProgressBar(static_cast<float>(1.0 - m_Rebind->RemainingSeconds() / 5.0),
                                               {0.0F, 4.0F}, {});
                            }
                            else if (status == Keire::RebindStatus::Candidate)
                            {
                                ui.TextColored(m_Theme.Success, "Candidate: " + m_Rebind->CandidatePath());
                                const auto conflicts = m_Rebind->Conflicts();
                                if (!conflicts.empty())
                                    ui.TextColored(m_Theme.Warning,
                                                   std::to_string(conflicts.size()) + " binding conflict(s).");
                                if (ui.Button(conflicts.empty() ? "Accept" : "Replace"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    RecordInputUndo();
                                    if (!conflicts.empty())
                                    {
                                        std::erase_if(map->Bindings,
                                                      [&](const auto& candidate)
                                                      {
                                                          return std::ranges::any_of(
                                                              conflicts, [&](const auto& conflict)
                                                              { return conflict.Binding == candidate.Id; });
                                                      });
                                    }
                                    binding = std::ranges::find(map->Bindings, targetBinding,
                                                                &Keire::InputBindingDefinition::Id);
                                    if (binding != map->Bindings.end())
                                        binding->Path = candidatePath;
                                    m_Rebind->Apply(conflicts.empty() ? Keire::RebindConflictResolution::KeepBoth
                                                                      : Keire::RebindConflictResolution::Replace);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                        m_Rebind.Reset();
                                }
                                ui.SameLine();
                                if (!conflicts.empty() && ui.Button("Keep Both"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    RecordInputUndo();
                                    const auto target = std::ranges::find(map->Bindings, targetBinding,
                                                                          &Keire::InputBindingDefinition::Id);
                                    if (target != map->Bindings.end())
                                        target->Path = candidatePath;
                                    m_Rebind->Apply(Keire::RebindConflictResolution::KeepBoth);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                        m_Rebind.Reset();
                                }
                                ui.SameLine();
                                if (ui.Button("Cancel"))
                                {
                                    m_Rebind->Cancel();
                                    m_Rebind.Reset();
                                }
                            }
                            else
                                m_Rebind.Reset();
                        }
                    }
                }
                if (m_InputLiveMonitor && m_InputContext)
                {
                    ui.Separator();
                    ui.TextColored(m_Theme.Accent, "LIVE VALUE");
                    const auto handle = m_InputContext->FindAction(m_SelectedInputAction);
                    if (handle)
                    {
                        const auto value = handle.Value();
                        ui.Text("Phase " + std::to_string(static_cast<int>(handle.Phase())) + "  [" +
                                std::to_string(value.X) + ", " + std::to_string(value.Y) + "]");
                    }
                    if (const auto input = Owner().Input())
                    {
                        ui.TextColored(m_Theme.MutedText, std::to_string(input->Devices().size()) + " device(s), " +
                                                              std::to_string(input->Users().size()) + " user(s)");
                    }
                }
            }
        }
    }
}

void EditorWorkspaceLayer::DrawProject(Keire::UiFrame& ui)
{
    if (m_AssetBrowserPanel)
        m_AssetBrowserPanel->Draw(ui, *this);
}

void EditorWorkspaceLayer::DrawInspector(Keire::UiFrame& ui)
{
    if (auto inspector = ui.BeginPanel(m_Inspector); inspector)
    {
        if (ui.WindowFocused() && m_EditingScene && m_SelectedSceneObject)
            m_ActiveUndoContext = m_SceneUndoContext;
        ui.TextColored(m_Theme.Accent, "INSPECTOR");
        ui.Separator();
        if (m_EditingScene && m_SelectedSceneObject)
        {
            auto entity = m_EditingScene->FindEntity(Keire::EntityId(m_SelectedSceneObject));
            if (entity)
            {
                auto name = entity.Name();
                if (ui.InputText("Entity Name", name))
                {
                    RecordSceneUndo();
                    entity.SetName(std::move(name));
                }
                auto active = entity.ActiveSelf();
                if (ui.Checkbox("Active", active))
                {
                    RecordSceneUndo();
                    entity.SetActive(active);
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
                        ui.TextColored(m_Theme.MutedText, "Required | Local space");
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
                            const auto propagate =
                                [](const float previous, const float current, const float otherPrevious)
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
                            RecordSceneUndo("Change Position", "transform.position." + entity.Id().ToString() + "." +
                                                                   std::to_string(m_ContinuousEditSerial));
                            transform->SetLocalPosition(position);
                        }
                        if (rotationChanged)
                        {
                            RecordSceneUndo("Change Rotation", "transform.rotation." + entity.Id().ToString() + "." +
                                                                   std::to_string(m_ContinuousEditSerial));
                            transform->SetLocalEulerAngles(rotation);
                        }
                        if (scaleChanged)
                        {
                            RecordSceneUndo("Change Scale", "transform.scale." + entity.Id().ToString() + "." +
                                                                std::to_string(m_ContinuousEditSerial));
                            transform->SetLocalScale(scale);
                        }
                        if (positionState.DeactivatedAfterEdit || rotationState.DeactivatedAfterEdit ||
                            scaleState.DeactivatedAfterEdit)
                            ++m_ContinuousEditSerial;
                        ui.Spacing();
                        (void)ui.Checkbox("Uniform scale", m_UniformScale);
                        ui.SameLine();
                        if (ui.Button("Reset"))
                        {
                            RecordSceneUndo();
                            transform->Reset();
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
                                RecordSceneUndo();
                                light->SetEnabled(enabled);
                            }
                            auto color = light->LightColor();
                            Keire::UiColor editorColor{color.Red, color.Green, color.Blue, color.Alpha};
                            if (ui.ColorEdit("Color", editorColor))
                            {
                                RecordSceneUndo();
                                light->SetLightColor(
                                    {editorColor.Red, editorColor.Green, editorColor.Blue, editorColor.Alpha});
                            }
                            auto intensity = light->Intensity();
                            if (ui.SliderFloat("Intensity", intensity, 0.0F, 100.0F))
                            {
                                RecordSceneUndo();
                                light->SetIntensity(intensity);
                            }
                            auto temperature = light->UseColorTemperature();
                            if (ui.Checkbox("Use Color Temperature", temperature))
                            {
                                RecordSceneUndo();
                                light->SetUseColorTemperature(temperature);
                            }
                            auto kelvin = light->ColorTemperatureKelvin();
                            if (ui.SliderFloat("Temperature (K)", kelvin, 1000.0F, 20000.0F))
                            {
                                RecordSceneUndo();
                                light->SetColorTemperatureKelvin(kelvin);
                            }
                            auto shadowStrength = light->ShadowStrength();
                            if (ui.SliderFloat("Shadow Strength", shadowStrength, 0.0F, 1.0F))
                            {
                                RecordSceneUndo();
                                light->SetShadowStrength(shadowStrength);
                            }
                            auto bias = light->ShadowBias();
                            if (ui.SliderFloat("Shadow Bias", bias, 0.0F, 1.0F))
                            {
                                RecordSceneUndo();
                                light->SetShadowBias(bias);
                            }
                            if (ui.Button("Reset Light"))
                            {
                                RecordSceneUndo();
                                light->Reset();
                            }
                            ui.SameLine();
                            if (ui.Button("Remove Component"))
                            {
                                RecordSceneUndo();
                                (void)entity.RemoveComponent<Keire::DirectionalLightComponent>();
                            }
                            ui.TextColored(m_Theme.MutedText, "Direct Lambert lighting | Shadows deferred");
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
                            ui.TextColored(m_Theme.MutedText, "Game view | Priority-selected");
                            ui.Separator();
                            auto enabled = camera->Enabled();
                            if (ui.Checkbox("Enabled##Camera", enabled))
                            {
                                RecordSceneUndo();
                                camera->SetEnabled(enabled);
                            }
                            auto primary = camera->Primary();
                            if (ui.Checkbox("Primary", primary))
                            {
                                RecordSceneUndo();
                                camera->SetPrimary(primary);
                            }
                            const auto projection = camera->Projection();
                            if (auto combo = ui.BeginCombo(
                                    "Projection", projection == Keire::CameraProjection::Perspective ? "Perspective"
                                                                                                     : "Orthographic");
                                combo)
                            {
                                if (ui.Selectable("Perspective", projection == Keire::CameraProjection::Perspective))
                                {
                                    RecordSceneUndo();
                                    camera->SetProjection(Keire::CameraProjection::Perspective);
                                }
                                if (ui.Selectable("Orthographic", projection == Keire::CameraProjection::Orthographic))
                                {
                                    RecordSceneUndo();
                                    camera->SetProjection(Keire::CameraProjection::Orthographic);
                                }
                            }
                            auto priority = camera->Priority();
                            if (ui.SliderInt("Priority", priority, -100, 100))
                            {
                                RecordSceneUndo();
                                camera->SetPriority(priority);
                            }
                            if (camera->Projection() == Keire::CameraProjection::Perspective)
                            {
                                auto fieldOfView = camera->VerticalFieldOfViewDegrees();
                                if (ui.SliderFloat("Vertical FOV", fieldOfView, 1.0F, 179.0F))
                                {
                                    RecordSceneUndo();
                                    camera->SetVerticalFieldOfViewDegrees(fieldOfView);
                                }
                            }
                            else
                            {
                                auto size = camera->OrthographicSize();
                                if (ui.SliderFloat("Orthographic Size", size, 0.01F, 100.0F))
                                {
                                    RecordSceneUndo();
                                    camera->SetOrthographicSize(size);
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
                                RecordSceneUndo();
                                camera->SetClipPlanes(nearPlane, farPlane);
                            }
                            auto clear = camera->ClearColor();
                            Keire::UiColor clearColor{clear.Red, clear.Green, clear.Blue, clear.Alpha};
                            if (ui.ColorEdit("Clear Color", clearColor))
                            {
                                RecordSceneUndo();
                                camera->SetClearColor(
                                    {clearColor.Red, clearColor.Green, clearColor.Blue, clearColor.Alpha});
                            }
                            if (ui.Button("Reset Camera"))
                            {
                                RecordSceneUndo();
                                camera->Reset();
                            }
                            ui.SameLine();
                            if (ui.Button("Remove Camera"))
                            {
                                RecordSceneUndo();
                                (void)entity.RemoveComponent<Keire::CameraComponent>();
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
                            ui.TextColored(m_Theme.MutedText, "Lit geometry submission");
                            ui.Separator();
                            auto enabled = renderer->Enabled();
                            if (ui.Checkbox("Enabled##MeshRenderer", enabled))
                            {
                                RecordSceneUndo();
                                renderer->SetEnabled(enabled);
                            }
                            auto visible = renderer->Visible();
                            if (ui.Checkbox("Visible", visible))
                            {
                                RecordSceneUndo();
                                renderer->SetVisible(visible);
                            }
                            auto tint = renderer->Tint();
                            Keire::UiColor tintColor{tint.Red, tint.Green, tint.Blue, tint.Alpha};
                            if (ui.ColorEdit("Tint", tintColor))
                            {
                                RecordSceneUndo("Change Tint", "mesh.tint." + entity.Id().ToString() + "." +
                                                                   std::to_string(m_ContinuousEditSerial));
                                renderer->SetTint({tintColor.Red, tintColor.Green, tintColor.Blue, tintColor.Alpha});
                            }
                            if (ui.LastItemState().DeactivatedAfterEdit)
                                ++m_ContinuousEditSerial;
                            if (const auto registration = m_EditingScene->Components()->Find(renderer->Type()))
                            {
                                InspectorPropertyEditor propertyEditor(ui, m_AssetRecords, m_EditingScene);
                                for (const auto& property : registration->Properties)
                                {
                                    if (property.Key != "mesh" && property.Key != "material")
                                        continue;
                                    try
                                    {
                                        if (m_PropertyDrawers->EditComponent(
                                                propertyEditor, *registration, *renderer, property,
                                                [this, &entity, &property]
                                                {
                                                    RecordSceneUndo("Change " + property.DisplayName,
                                                                    "mesh-renderer." + property.Key + "." +
                                                                        entity.Id().ToString());
                                                }))
                                            m_EditingScene->MarkDirty();
                                    }
                                    catch (const std::exception& error)
                                    {
                                        ui.TextColored(m_Theme.Error, error.what());
                                    }
                                }
                            }
                            if (ui.Button("Reset Renderer"))
                            {
                                RecordSceneUndo();
                                renderer->Reset();
                            }
                            ui.SameLine();
                            if (ui.Button("Remove Renderer"))
                            {
                                RecordSceneUndo();
                                (void)entity.RemoveComponent<Keire::MeshRendererComponent>();
                            }
                        }
                    }
                }
                InspectorPropertyEditor propertyEditor(ui, m_AssetRecords, m_EditingScene);
                for (const auto& component : entity.GetComponents())
                {
                    if (!component || component->Type() == Keire::TransformComponent::StaticType() ||
                        component->Type() == Keire::CameraComponent::StaticType() ||
                        component->Type() == Keire::DirectionalLightComponent::StaticType() ||
                        component->Type() == Keire::MeshRendererComponent::StaticType())
                        continue;
                    const auto registration = m_EditingScene->Components()->Find(component->Type());
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
                            RecordSceneUndo("Change " + registration->Name);
                            component->SetEnabled(enabled);
                        }
                        std::string activeGroup;
                        for (const auto& property : registration->Properties)
                        {
                            if (!property.Group.empty() && property.Group != activeGroup)
                            {
                                activeGroup = property.Group;
                                ui.TextColored(m_Theme.MutedText, activeGroup);
                            }
                            try
                            {
                                const bool changed = m_PropertyDrawers->EditComponent(
                                    propertyEditor, *registration, *component, property,
                                    [this, &entity, &registration, &property]
                                    {
                                        RecordSceneUndo("Change " + property.DisplayName,
                                                        registration->Type.ToString() + "." + property.Key + "." +
                                                            entity.Id().ToString() + "." +
                                                            std::to_string(m_ContinuousEditSerial));
                                    });
                                if (changed)
                                    m_EditingScene->MarkDirty();
                                if (changed && ui.LastItemState().DeactivatedAfterEdit)
                                    ++m_ContinuousEditSerial;
                            }
                            catch (const std::exception& error)
                            {
                                ui.TextColored(m_Theme.Error, error.what());
                            }
                        }
                        if (registration->Removable && ui.Button("Remove " + registration->Name))
                        {
                            RecordSceneUndo("Remove " + registration->Name);
                            (void)entity.RemoveComponent(registration->Type);
                        }
                    }
                }
                ui.Spacing();
                if (auto add = ui.BeginCombo("Add Component", "Search components..."); add)
                {
                    for (const auto& registration : m_EditingScene->Components()->Registrations())
                    {
                        const bool canAdd = registration.Removable &&
                                            (registration.AllowMultiple || !entity.HasComponent(registration.Type));
                        if (ui.MenuItem(registration.Category + "/" + registration.Name, false, canAdd))
                        {
                            RecordSceneUndo();
                            (void)entity.AddComponent(registration.Type);
                        }
                    }
                }
                ui.TextColored(m_Theme.MutedText, "Object ID");
                ui.Text(entity.Id().ToString());
                return;
            }
            m_SelectedSceneObject = {};
        }
        if (!m_SelectedAsset || !m_AssetDatabase)
        {
            ui.Text("Nothing selected");
            ui.TextColored(m_Theme.MutedText, "Select an asset in the Project panel.");
            return;
        }
        const auto record = m_AssetDatabase->Find(m_SelectedAsset);
        if (!record)
        {
            m_SelectedAsset = {};
            ui.TextColored(m_Theme.Warning, "The selected asset no longer exists.");
            return;
        }
        if (m_EditingAsset != record->Id)
        {
            m_EditingAsset = record->Id;
            m_AssetName = record->RelativePath.filename().string();
        }
        ui.Text(record->RelativePath.generic_string());
        ui.TextColored(m_Theme.MutedText, "Asset ID");
        ui.Text(record->Id.ToString());
        ui.TextColored(m_Theme.MutedText, "Importer");
        ui.Text(record->Importer + " v" + std::to_string(record->ImporterVersion));
        ui.TextColored(m_Theme.MutedText, "Content SHA-256");
        ui.Text(record->SourceDigest);
        if (record->RelativePath.extension() == ".keireinput")
        {
            ui.Separator();
            ui.TextColored(m_Theme.Accent, "INPUT ACTION ASSET");
            ui.Text("Action maps, bindings, control schemes, and runtime overrides.");
            if (ui.Button("Edit Input Actions"))
            {
                try
                {
                    if (m_InputDirty && m_InputAsset != record->Id)
                        throw std::runtime_error("Save or Revert the currently edited input asset before switching.");
                    OpenInputActions(record->Id);
                }
                catch (const std::exception& error)
                {
                    SetAssetError(std::string("Input editor failed to open: ") + error.what());
                }
            }
        }
        else if (record->RelativePath.extension() == ".keireshader")
        {
            ui.Separator();
            const auto importStatus = m_AssetDatabase->ImportStatus(record->Id);
            if (importStatus.State == Keire::AssetImportState::Failed)
            {
                ui.TextColored(m_Theme.Error, "SHADER IMPORT FAILED");
                ui.TextColored(m_Theme.Warning, "The last-good compiled revision remains active when available.");
                ui.Separator();
                ui.TextColored(m_Theme.MutedText, "Compiler diagnostics");
                constexpr std::size_t maximumVisibleDiagnostics = 64;
                const auto visibleDiagnostics = std::min(importStatus.Diagnostics.size(), maximumVisibleDiagnostics);
                for (std::size_t index = 0; index < visibleDiagnostics; ++index)
                {
                    const auto& diagnostic = importStatus.Diagnostics[index];
                    const auto color = diagnostic.Severity == Keire::AssetDiagnosticSeverity::Error ? m_Theme.Error
                                       : diagnostic.Severity == Keire::AssetDiagnosticSeverity::Warning
                                           ? m_Theme.Warning
                                           : m_Theme.MutedText;
                    ui.TextColored(color, FormatAssetDiagnostic(diagnostic));
                }
                if (importStatus.Diagnostics.size() > visibleDiagnostics)
                    ui.TextColored(m_Theme.MutedText,
                                   std::to_string(importStatus.Diagnostics.size() - visibleDiagnostics) +
                                       " additional diagnostic(s) are available in the log file.");
            }
            else
            {
                ui.TextColored(m_Theme.Accent, "SHADER");
                ui.TextColored(importStatus.State == Keire::AssetImportState::NotImported ? m_Theme.Warning
                                                                                          : m_Theme.Success,
                               importStatus.State == Keire::AssetImportState::NotImported ? "Waiting for first import"
                                                                                          : "Imported graphics shader");
            }
            ui.Text("Stages: Vertex, Fragment");
            ui.Text("Variants: DXIL, SPIR-V, MSL");
            ui.TextColored(m_Theme.MutedText, "Source dependencies");
            if (record->SourceDependencies.empty())
                ui.Text("No dependency records are available; reimport to refresh.");
            for (const auto& dependency : record->SourceDependencies)
                ui.Text(dependency.RelativePath.generic_string() + "  " + dependency.Digest.substr(0, 12));
            if (ui.Button("Reimport Shader"))
                ImportAssets();
            if (!m_AssetStatus.empty())
                ui.TextColored(m_Theme.MutedText, m_AssetStatus);
        }
        else if (record->RelativePath.extension() == ".keirematerial")
        {
            ui.Separator();
            ui.TextColored(m_Theme.Accent, "MATERIAL");
            ui.Text("Unlit material with validated shader property overrides.");
            ui.TextColored(m_Theme.MutedText, "Invalid shaders resolve to the error material at runtime.");
            if (ui.Button("Reimport Material"))
                ImportAssets();
        }
        ui.Separator();
        (void)ui.InputText("Name", m_AssetName);
        if (ui.Button("Rename") && !m_AssetName.empty())
        {
            try
            {
                m_AssetDatabase->Rename(record->Id, m_AssetName);
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Renamed asset and preserved its metadata identity.";
            }
            catch (const std::exception& error)
            {
                SetAssetError(std::string("Asset rename failed: ") + error.what());
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
                for (std::size_t copy = 2; m_AssetDatabase->Find(destination); ++copy)
                    destination =
                        record->RelativePath.parent_path() / (stem + " Copy " + std::to_string(copy) + extension);
                m_SelectedAsset = m_AssetDatabase->Duplicate(record->Id, destination);
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Duplicated asset with a new stable identity.";
            }
            catch (const std::exception& error)
            {
                SetAssetError(std::string("Asset duplication failed: ") + error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Move to Trash"))
        {
            try
            {
                const auto trash = m_AssetDatabase->MoveToTrash(record->Id);
                m_SelectedAsset = {};
                m_EditingAsset = {};
                m_AssetRecords = m_AssetDatabase->Records();
                m_AssetStatus = "Moved asset to recoverable trash: " + trash.string();
            }
            catch (const std::exception& error)
            {
                SetAssetError(std::string("Asset trash operation failed: ") + error.what());
            }
        }
    }
}

void EditorWorkspaceLayer::DrawProjectSettings(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_ProjectSettings); panel)
    {
        ui.TextColored(m_Theme.Accent, "PROJECT SETTINGS");
        ui.Separator();
        ui.Text("Rendering / Environment");
        ui.TextColored(m_Theme.MutedText, "These values are project-owned and light both the Scene and Game views.");
        ui.Spacing();

        Keire::UiColor ambient{m_RenderEnvironment.AmbientColor.Red, m_RenderEnvironment.AmbientColor.Green,
                               m_RenderEnvironment.AmbientColor.Blue, m_RenderEnvironment.AmbientColor.Alpha};
        bool changed = ui.ColorEdit("Ambient Color", ambient);
        bool save = ui.LastItemState().DeactivatedAfterEdit;

        changed |= ui.SliderFloat("Ambient Intensity", m_RenderEnvironment.AmbientIntensity, 0.0F, 8.0F);
        save |= ui.LastItemState().DeactivatedAfterEdit;
        changed |= ui.SliderFloat("Exposure", m_RenderEnvironment.Exposure, 0.1F, 4.0F);
        save |= ui.LastItemState().DeactivatedAfterEdit;
        if (ambient != Keire::UiColor{m_RenderEnvironment.AmbientColor.Red, m_RenderEnvironment.AmbientColor.Green,
                                      m_RenderEnvironment.AmbientColor.Blue, m_RenderEnvironment.AmbientColor.Alpha})
        {
            m_RenderEnvironment.AmbientColor = {ambient.Red, ambient.Green, ambient.Blue, ambient.Alpha};
            changed = true;
        }
        ui.Spacing();
        if (ui.Button("Reset Environment"))
        {
            m_RenderEnvironment = {};
            changed = true;
            save = true;
        }

        if (changed)
            m_RenderEnvironmentDirty = true;

        if (save && m_RenderEnvironmentDirty)
        {
            try
            {
                const auto project = Owner().GetProject();
                if (!project || !project->Writable())
                    throw std::runtime_error("The active project is not writable.");
                Keire::SaveRenderEnvironmentSettings(project->Root(), m_RenderEnvironment);
                m_RenderEnvironmentDirty = false;
            }
            catch (const std::exception& error)
            {
                ReportError("Rendering", std::string("Could not save project settings: ") + error.what());
            }
        }
    }
}

void EditorWorkspaceLayer::DrawThemeEditor(Keire::UiFrame& ui, Keire::UiWorkspace& workspace)
{
    const bool wasVisible = m_ThemeEditor.Visible();
    auto panel = ui.BeginPanel(m_ThemeEditor);
    if (wasVisible && !m_ThemeEditor.Visible() && m_ThemeDirty)
    {
        m_ThemeEditor.SetVisible(true);
        m_PendingTheme = {};
        m_CloseThemeAfterDecision = true;
        m_Dialog = Dialog::DirtyTheme;
        m_OpenDialog = true;
    }
    if (!panel)
        return;
    if (ui.WindowFocused())
        m_ActiveUndoContext = m_ThemeUndoContext;

    const auto themes = workspace.Themes();
    const auto* active = ActiveTheme(themes);
    if (auto combo = ui.BeginCombo("Theme", active ? active->Name : "Unknown"); combo)
    {
        for (const auto& theme : themes)
        {
            if (ui.Selectable(theme.Name, theme.Active))
                RequestTheme(workspace, theme.Id);
        }
    }
    ui.Separator();
    const auto themeBeforeEdit = m_Theme;
    bool changed = false;
    changed |= ui.ColorEdit("Canvas", m_Theme.Canvas);
    changed |= ui.ColorEdit("Panel", m_Theme.Panel);
    changed |= ui.ColorEdit("Raised panel", m_Theme.RaisedPanel);
    changed |= ui.ColorEdit("Border", m_Theme.Border);
    changed |= ui.ColorEdit("Text", m_Theme.Text);
    changed |= ui.ColorEdit("Muted text", m_Theme.MutedText);
    changed |= ui.ColorEdit("Accent", m_Theme.Accent);
    changed |= ui.ColorEdit("Accent hovered", m_Theme.AccentHovered);
    changed |= ui.ColorEdit("Accent active", m_Theme.AccentActive);
    changed |= ui.ColorEdit("Selection", m_Theme.Selection);
    changed |= ui.ColorEdit("Success", m_Theme.Success);
    changed |= ui.ColorEdit("Warning", m_Theme.Warning);
    changed |= ui.ColorEdit("Error", m_Theme.Error);
    ui.Separator();
    changed |= ui.SliderFloat("Window padding X", m_Theme.WindowPadding.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Window padding Y", m_Theme.WindowPadding.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Frame padding X", m_Theme.FramePadding.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Frame padding Y", m_Theme.FramePadding.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Item spacing X", m_Theme.ItemSpacing.Width, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Item spacing Y", m_Theme.ItemSpacing.Height, 0.0F, 32.0F);
    changed |= ui.SliderFloat("Window rounding", m_Theme.WindowRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Frame rounding", m_Theme.FrameRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Tab rounding", m_Theme.TabRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Scrollbar rounding", m_Theme.ScrollbarRounding, 0.0F, 24.0F);
    changed |= ui.SliderFloat("Window border", m_Theme.WindowBorderSize, 0.0F, 4.0F);
    changed |= ui.SliderFloat("Frame border", m_Theme.FrameBorderSize, 0.0F, 4.0F);
    if (changed)
    {
        m_ThemeDirty = true;
        workspace.PreviewTheme(m_Theme);
        if (m_ThemeUndoContext)
        {
            const auto themeAfterEdit = m_Theme;
            m_ThemeUndoContext->RecordApplied(Keire::CreateUndoCommand(
                "Edit Theme",
                [this, &workspace, themeAfterEdit]
                {
                    m_Theme = themeAfterEdit;
                    m_ThemeDirty = true;
                    workspace.PreviewTheme(m_Theme);
                },
                [this, &workspace, themeBeforeEdit]
                {
                    m_Theme = themeBeforeEdit;
                    m_ThemeDirty = true;
                    workspace.PreviewTheme(m_Theme);
                },
                sizeof(Keire::UiThemeDefinition), [this] { return m_ThemeEditor.Visible(); }));
        }
    }

    const bool canOverwrite = active && !active->BuiltIn;
    if (auto disabled = ui.BeginDisabled(!m_ThemeDirty || !canOverwrite); disabled)
    {
        if (ui.Button("Save"))
        {
            workspace.UpdateTheme(workspace.ActiveTheme(), m_Theme);
            m_ThemeDirty = false;
        }
    }
    ui.SameLine();
    if (ui.Button("Save As..."))
        OpenDialog(Dialog::SaveTheme);
    ui.SameLine();
    if (auto disabled = ui.BeginDisabled(!m_ThemeDirty); disabled)
    {
        if (ui.Button("Revert"))
        {
            workspace.CancelThemePreview();
            LoadTheme(workspace, workspace.ActiveTheme());
        }
    }
    if (active && !active->BuiltIn)
    {
        if (ui.Button("Rename..."))
        {
            m_ProfileName = active->Name;
            OpenDialog(Dialog::RenameTheme);
        }
        ui.SameLine();
        if (ui.Button("Delete..."))
            OpenDialog(Dialog::DeleteTheme);
    }
    else
        ui.TextColored(m_Theme.MutedText, "Built-in themes are immutable. Save As creates an editable copy.");
}

void EditorWorkspaceLayer::DrawDiagnostics(Keire::UiFrame& ui) { m_DiagnosticsPanel->Draw(ui, *this); }
