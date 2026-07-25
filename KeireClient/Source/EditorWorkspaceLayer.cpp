#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/SceneTransitionCoordinator.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"

#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
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
#include <unordered_set>
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

    [[nodiscard]] bool FileIsNewerThan(const std::filesystem::path& path,
                                       const std::filesystem::file_time_type reference) noexcept
    {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(path, error);
        return error || modified > reference;
    }

    [[nodiscard]] bool AssetSourcesAreNewerThanCatalog(const std::filesystem::path& assetsRoot,
                                                       const std::filesystem::path& catalog) noexcept
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(catalog, error) || error)
            return true;
        const auto catalogTime = std::filesystem::last_write_time(catalog, error);
        if (error)
            return true;
        for (std::filesystem::recursive_directory_iterator
                 iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             iterator != end; iterator.increment(error))
        {
            if (error)
                return true;
            if (iterator->is_regular_file(error) && !error && FileIsNewerThan(iterator->path(), catalogTime))
                return true;
            error.clear();
        }
        return false;
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

EditorWorkspaceLayer::EditorWorkspaceLayer(const bool smoke, const bool initializeProject,
                                           std::filesystem::path executable)
    : Layer("EditorWorkspaceLayer"), m_AssetBrowserPanel(std::make_unique<KeireEditor::AssetBrowserPanel>(
                                         static_cast<KeireEditor::IAssetBrowserController&>(*this))),
      m_ConsolePanel(std::make_unique<KeireEditor::ConsolePanel>()),
      m_DiagnosticsPanel(std::make_unique<KeireEditor::DiagnosticsPanel>()),
      m_SceneDocument(std::make_unique<KeireEditor::SceneDocument>()),
      m_InputActionsDocument(std::make_unique<KeireEditor::InputActionsDocument>()),
      m_ProjectSettingsDocument(std::make_unique<KeireEditor::ProjectSettingsDocument>()),
      m_MaterialDocument(std::make_unique<KeireEditor::MaterialDocument>()),
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
          *m_ProjectSettingsDocument, static_cast<KeireEditor::IProjectSettingsController&>(*this))),
      m_PropertyDrawers(std::make_unique<KeireEditor::PropertyDrawerRegistry>()),
      m_ViewportAssetDropRouter(std::make_unique<KeireEditor::ViewportAssetDropRouter>()),
      m_PlayChangesPanel(std::make_unique<KeireEditor::ScenePlayChangesPanel>()),
      m_SceneTransitions(std::make_unique<KeireEditor::SceneTransitionCoordinator>()),
      m_ExternalAssetImport(std::make_unique<KeireEditor::ExternalAssetImportController>()),
      m_ExecutablePath(std::move(executable)), m_Smoke(smoke), m_InitializeProject(initializeProject)
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
            if (!m_InspectorPanel->UniformScale())
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
        Keire::CameraComponent::StaticType(), "clearMode",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
           Keire::ComponentPropertyValue& value)
        {
            auto* mode = std::get_if<std::int64_t>(&value);
            if (!mode)
                throw std::invalid_argument("Camera clear mode metadata must serialize an Integer.");
            constexpr std::array choices{std::string_view("Skybox"), std::string_view("Solid Color")};
            return editor.EditChoice(property.DisplayName, *mode, choices);
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
        [this] { return m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SaveSceneAs, [this] { SaveSceneAs(); },
        [this] { return m_SceneDocument->EditingScene() && !m_SceneDocument->SaveDialog(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CloseScene, [this] { RequestCloseScene(); },
        [this] { return static_cast<bool>(m_SceneDocument->EditingScene()); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CreateEntity,
        [this]
        {
            RecordSceneUndo("Create Entity");
            const auto created = m_SceneDocument->CreateEntity("GameObject");
            m_SceneDocument->Select(created.Value());
            MarkPlayEditorEntity(created.Value());
        },
        [this] { return static_cast<bool>(ActiveScene()) && !m_PlayChanges; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::DeleteSelection,
        [this]
        {
            RecordSceneUndo("Delete Entities");
            const auto selected = m_SceneDocument->Selections();
            const std::vector entities(selected.begin(), selected.end());
            for (const auto entity : entities)
            {
                MarkPlayEditorEntity(entity);
                m_SceneDocument->DeleteEntity(Keire::EntityId(entity));
            }
            m_SceneDocument->ClearSelection();
        },
        [this] { return ActiveScene() && !m_SceneDocument->Selections().empty() && !m_PlayChanges; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::DuplicateSelection,
        [this]
        {
            const auto scene = ActiveScene();
            const auto selected = m_SceneDocument->Selections();
            const std::unordered_set<Keire::AssetId> selectedSet(selected.begin(), selected.end());
            std::vector<Keire::AssetId> roots;
            for (const auto entity : selected)
            {
                auto parent = scene->FindEntity(Keire::EntityId(entity)).Parent();
                bool ancestorSelected = false;
                while (parent)
                {
                    if (selectedSet.contains(parent.Id().Value()))
                    {
                        ancestorSelected = true;
                        break;
                    }
                    parent = parent.Parent();
                }
                if (!ancestorSelected)
                    roots.push_back(entity);
            }

            RecordSceneUndo("Duplicate Entities");
            std::vector<Keire::AssetId> duplicates;
            duplicates.reserve(roots.size());
            for (const auto entity : roots)
            {
                const auto duplicate = m_SceneDocument->DuplicateEntity(Keire::EntityId(entity)).Value();
                duplicates.push_back(duplicate);
                MarkPlayEditorEntity(duplicate);
            }
            m_SceneDocument->SetSelections(duplicates);
        },
        [this] { return ActiveScene() && !m_SceneDocument->Selections().empty() && !m_PlayChanges; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SelectAll,
        [this]
        {
            std::vector<Keire::AssetId> entities;
            for (const auto& entity : ActiveScene()->Entities())
                entities.push_back(entity.Id().Value());
            m_SceneDocument->SetSelections(entities);
        },
        [this] { return static_cast<bool>(ActiveScene()); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::ClearSelection, [this] { m_SceneDocument->ClearSelection(); },
        [this] { return !m_SceneDocument->Selections().empty(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::Play, [this] { BeginPlayMode(); },
        [this] { return m_SceneDocument->EditingScene() && !m_SceneDocument->PlaySession() && !m_PlayChanges; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::Pause, [this] { m_SceneDocument->PlaySession()->TogglePause(); },
        [this]
        {
            return m_SceneDocument->PlaySession() &&
                   (m_SceneDocument->PlaySession()->State() == Keire::ScenePlayState::Playing ||
                    m_SceneDocument->PlaySession()->State() == Keire::ScenePlayState::Paused);
        });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::Stop, [this] { RequestStopPlayMode(); },
        [this] { return static_cast<bool>(m_SceneDocument->PlaySession()) && !m_PlayChanges; });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SaveInputActions, [this] { SaveInputActions(); },
        [this] { return m_InputActionsDocument->Dirty() && static_cast<bool>(m_InputActionsDocument->Asset()); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::SaveProjectSettings,
        [this]
        {
            try
            {
                m_ProjectSettingsDocument->Save();
            }
            catch (const std::exception& error)
            {
                ReportError("Rendering", std::string("Could not save project settings: ") + error.what());
            }
        },
        [this] { return m_ProjectSettingsDocument->Dirty(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::ImportAssets, [this] { ImportAssets(); },
        [this] { return m_AssetDatabase && m_AssetOperations && !m_AssetOperations->Busy(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CookAssets, [this] { CookAssets(); },
        [this] { return m_AssetDatabase && m_AssetOperations && !m_AssetOperations->Busy(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CancelAssetOperation,
        [this]
        {
            if (m_AssetOperations)
                m_AssetOperations->CancelCurrent();
        },
        [this] { return m_AssetOperations && m_AssetOperations->Busy(); });
    m_CommandRouter->Bind(KeireEditor::EditorCommand::Exit,
                          [this]
                          {
                              if (m_SceneDocument->PlaySession())
                              {
                                  m_PendingSceneAction = PendingSceneAction::Exit;
                                  RequestStopPlayMode();
                                  return;
                              }
                              if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
                              {
                                  m_PendingSceneAction = PendingSceneAction::Exit;
                                  OpenDialog(Dialog::DirtyScene);
                              }
                              else
                                  QueueSceneTransition(PendingSceneAction::Exit);
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
    m_SceneViewportPanel->Attach(workspace);
    m_Game = workspace.RegisterPanel({"editor.game", "Game"});
    m_HierarchyPanel->Attach(workspace);
    m_InspectorPanel->Attach(workspace);
    m_AssetBrowserPanel->Attach(workspace);
    m_ConsolePanel->Attach(workspace);
    m_DiagnosticsPanel->Attach(workspace);
    m_ThemeEditor = workspace.RegisterPanel({"editor.theme", "Theme Editor", false});
    m_InputActionsPanel->Attach(workspace);
    m_InputDebugger = workspace.RegisterPanel({"editor.input-debugger", "Input Debugger", false});
    m_ProjectSettingsPanel->Attach(workspace);
    m_PrefabOverrides = workspace.RegisterPanel({"editor.prefab-overrides", "Prefab Overrides", false});
    m_BuildSettings = workspace.RegisterPanel({"editor.build-settings", "Build Settings", false});
    m_Profiler = workspace.RegisterPanel({"editor.profiler", "Profiler", false});
    if (const auto undo = Owner().Undo())
        m_ThemeUndoContext = undo->CreateContext({.Name = "Theme Authoring"});
    if (const auto renderer = Owner().Renderer(); renderer && renderer->Mode() != Keire::RenderMode::Disabled)
    {
        Keire::RenderSurfaceSpecification gameSurface;
        gameSurface.Name = "Game View";
        gameSurface.ClearColor = {0.10F, 0.12F, 0.16F, 1.0F};
        m_GameRenderView = renderer->CreateView(gameSurface);
    }
    m_SceneViewportPanel->Initialize(Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path{});
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
            Keire::RenderEnvironmentSettings renderEnvironment;
            try
            {
                renderEnvironment = Keire::LoadRenderEnvironmentSettings(project->Root());
            }
            catch (const std::exception& error)
            {
                ReportError("Rendering",
                            std::string("Invalid project rendering settings; using defaults: ") + error.what());
            }
            Keire::Ref<Keire::UndoContext> projectSettingsUndo;
            if (const auto undo = Owner().Undo())
                projectSettingsUndo = undo->CreateContext({.Name = "Project Settings"});
            m_ProjectSettingsDocument->Open(project->Root(), renderEnvironment, std::move(projectSettingsUndo));
            if (const auto undo = Owner().Undo())
                m_AssetBrowserPanel->SetUndoContext(undo->CreateContext({.Name = "Project Assets"}));
            databaseSpecification.Importers = {
                Keire::CreateInputActionAssetImporter(),   Keire::CreateSceneAssetImporter(),
                Keire::CreatePrefabAssetImporter(),        Keire::CreateManagedAssemblyAssetImporter(),
                Keire::CreateShaderAssetImporter(),        Keire::CreateMaterialAssetImporter(),
                Keire::CreateMeshAssetImporter(),          Keire::CreateTexture2DAssetImporter(),
                Keire::CreateAnimationGraphAssetImporter()};
            m_AssetDatabase = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            m_AssetOperations = std::make_unique<KeireEditor::AssetOperationService>(
                KeireEditor::AssetOperationService::ResolveWorkerExecutable(m_ExecutablePath), project->Root());
            m_AssetRecords = m_AssetDatabase->Records();
            const auto catalog = project->AssetCatalog();
            std::error_code catalogError;
            bool requiresSynchronousImport = !std::filesystem::is_regular_file(catalog, catalogError) || catalogError;
            if (!requiresSynchronousImport && project->Descriptor().StartupScene)
            {
                const auto startup = m_AssetDatabase->Find(project->Descriptor().StartupScene);
                const auto catalogTime = std::filesystem::last_write_time(catalog, catalogError);
                requiresSynchronousImport =
                    catalogError || !startup ||
                    FileIsNewerThan(project->AssetsDirectory() / startup->RelativePath, catalogTime) ||
                    FileIsNewerThan(startup->MetadataPath, catalogTime);
            }
            if (requiresSynchronousImport)
            {
                m_PendingStartupScene = project->Descriptor().StartupScene;
                ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
            }
            else if (AssetSourcesAreNewerThanCatalog(project->AssetsDirectory(), catalog))
            {
                m_AssetStatus =
                    "Opened the cached catalog. Source changes are pending; use Refresh and Import when ready.";
            }
            else
                m_AssetStatus = "Opened the current development catalog without rebuilding it.";
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
                    if (event.Header.Window != Owner().MainWindow()->Id())
                        return Keire::EventFlow::Continue;
                    if (m_PendingSceneAction != PendingSceneAction::None ||
                        (m_SceneTransitions && m_SceneTransitions->Pending()))
                        return Keire::EventFlow::Handled;
                    if (m_SceneDocument->PlaySession())
                    {
                        m_PendingSceneAction = PendingSceneAction::Exit;
                        RequestStopPlayMode();
                        return Keire::EventFlow::Handled;
                    }
                    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
                    {
                        m_PendingSceneAction = PendingSceneAction::Exit;
                        OpenDialog(Dialog::DirtyScene);
                        return Keire::EventFlow::Handled;
                    }
                    QueueSceneTransition(PendingSceneAction::Exit);
                    return Keire::EventFlow::Handled;
                });
            Listen<Keire::WindowFileDropEvent>(
                [this](const auto& event)
                {
                    if (event.Header.Window != Owner().MainWindow()->Id())
                        return Keire::EventFlow::Continue;
                    try
                    {
                        HandleExternalAssetDrop(event);
                    }
                    catch (const std::exception& error)
                    {
                        SetAssetError(std::string("External asset drop failed: ") + error.what());
                    }
                    return Keire::EventFlow::Handled;
                });
            if (project->Descriptor().StartupScene && !m_PendingStartupScene)
                RequestOpenScene(project->Descriptor().StartupScene);
            if (project->Descriptor().DefaultInput)
            {
                OpenInputActions(project->Descriptor().DefaultInput);
                m_InputActionsPanel->Registration().SetVisible(false);
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
    CommitMaterialDraft();
    CancelMaterialCatalogRefresh();
    const auto projectRoot = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path{};
    m_SceneViewportPanel->Shutdown(projectRoot);
    if (m_ProjectSettingsDocument && m_ProjectSettingsDocument->Dirty())
    {
        try
        {
            if (const auto project = Owner().GetProject(); project && project->Writable())
                m_ProjectSettingsDocument->Save();
        }
        catch (const std::exception& error)
        {
            KEIRE_CLIENT_ERROR("[Rendering] Could not save project settings during shutdown: {}", error.what());
        }
    }
    if (m_ProjectSettingsDocument)
        m_ProjectSettingsDocument->Close();
    EndInputTest();
    m_SceneDocument->EndPlay();
    m_GameRenderView.Reset();
    m_InputActionsPanel->ResetTransientState();
    m_InputContext.Reset();
    if (m_InputActionsDocument->UndoContext())
        m_InputActionsDocument->UndoContext()->Close();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Close();
    if (m_ThemeUndoContext)
        m_ThemeUndoContext->Close();
    m_ActiveUndoContext.Reset();
    m_ThemeUndoContext.Reset();
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        try
        {
            WriteSceneRecovery();
        }
        catch (...)
        {
        }
    }
    m_InputActionsDocument->Close();
    m_SceneDocument->Close();
    if (m_PrefabReturnDocument)
        m_PrefabReturnDocument->Close();
    m_PrefabReturnDocument.reset();
    m_PrefabEditingStage.reset();
    m_AssetBrowserPanel->Close();
    m_AssetDatabase.Reset();
}

void EditorWorkspaceLayer::OnFixedUpdate(const Keire::Time& time)
{
    if (m_SceneDocument->PlaySession())
        m_SceneDocument->PlaySession()->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
}

void EditorWorkspaceLayer::OnUpdate(const Keire::Time& time)
{
    ProcessSceneTransition();
    FinalizePendingPlayEditorMutation();
    if (m_PendingPlayTransition != PendingPlayTransition::None)
    {
        const auto transition = std::exchange(m_PendingPlayTransition, PendingPlayTransition::None);
        FinishPlayMode(transition == PendingPlayTransition::Apply);
    }
    if (m_Smoke && ++m_FrameCount >= 8)
        Owner().RequestExit();
    if (m_SceneDocument->PlaySession())
    {
        m_SceneDocument->PlaySession()->Update(static_cast<float>(time.DeltaTime().Seconds()));
        if (m_SceneDocument->PlaySession()->State() == Keire::ScenePlayState::Faulted && !m_PlayFaultReported)
        {
            const auto diagnostic = m_SceneDocument->PlaySession()->Diagnostic();
            m_SceneDocument->SetStatus(diagnostic.Callback + " failed: " + diagnostic.Message);
            ReportError("Play Mode", m_SceneDocument->Status());
            m_PlayFaultReported = true;
        }
    }
    CompleteSaveSceneAs();
    UpdateManagedBuild();
    if (!m_AssetDatabase)
        return;
    UpdateAssetOperations();
    if (!m_PendingAssetMutations.empty() && m_AssetOperations && !m_AssetOperations->Busy())
    {
        auto pending = std::move(m_PendingAssetMutations.front());
        m_PendingAssetMutations.erase(m_PendingAssetMutations.begin());
        try
        {
            QueueAssetMutation(std::move(pending.State), pending.Phase);
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Queued asset trash operation failed: ") + error.what());
        }
    }
    if (!m_PendingPrefabCreations.empty() && m_AssetOperations && !m_AssetOperations->Busy())
    {
        auto pending = std::move(m_PendingPrefabCreations.front());
        m_PendingPrefabCreations.erase(m_PendingPrefabCreations.begin());
        try
        {
            CreatePrefabFromObject(pending.Object, pending.Folder);
        }
        catch (const std::exception& error)
        {
            SetAssetError(std::string("Queued prefab creation failed: ") + error.what());
        }
    }
    if (m_MaterialDocument->Dirty() && m_SelectedAsset != m_MaterialDocument->Asset())
        CommitMaterialDraft();
    UpdateMaterialCatalogRefresh(time);
    if (m_SceneDocument->LoadOperation() && m_SceneDocument->LoadOperation()->State() == Keire::SceneLoadState::Failed)
    {
        m_SceneDocument->SetStatus("Scene runtime load failed: " +
                                   m_SceneDocument->LoadOperation()->Diagnostic().Message);
        m_SceneDocument->SetLoadOperation({});
    }
    else if (m_SceneDocument->LoadOperation() &&
             m_SceneDocument->LoadOperation()->State() == Keire::SceneLoadState::Ready)
    {
        m_SceneDocument->SetStatus("Scene loaded and activated.");
        m_SceneDocument->SetLoadOperation({});
    }
    if (m_SceneDocument->EditingScene() && m_SceneDocument->EditingScene()->Dirty())
    {
        m_SceneDocument->AdvanceRecovery(time.UnscaledDeltaTime().Seconds());
        if (m_SceneDocument->RecoverySeconds() >= 30.0)
        {
            m_SceneDocument->ResetRecoveryTimer();
            try
            {
                WriteSceneRecovery();
            }
            catch (const std::exception& error)
            {
                m_SceneDocument->SetStatus(std::string("Scene recovery save failed: ") + error.what());
                ReportError("Scene", m_SceneDocument->Status());
            }
        }
    }
    else
        m_SceneDocument->ResetRecoveryTimer();
    if ((m_ExternalAssetImport && m_ExternalAssetImport->Pending()) || (m_AssetOperations && m_AssetOperations->Busy()))
        return;
    m_AssetPollSeconds += time.UnscaledDeltaTime().Seconds();
    if (m_AssetPollSeconds < 0.25)
        return;
    m_AssetPollSeconds = 0.0;
    try
    {
        const auto changed = m_AssetDatabase->PollChangedAssets();
        if (!changed.empty())
        {
            ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
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
    if (m_SceneDocument)
        m_SceneDocument->SynchronizeSelection();
    if (!m_ActiveUndoContext || !m_ActiveUndoContext->IsOpen())
    {
        if (m_SceneDocument->UndoContext() && m_SceneDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_SceneDocument->UndoContext();
        else if (m_InputActionsDocument->UndoContext() && m_InputActionsDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_InputActionsDocument->UndoContext();
        else if (m_AssetBrowserPanel)
            m_ActiveUndoContext = m_AssetBrowserPanel->UndoContext();
    }
    if (ui.Shortcut({.Key = Keire::UiKey::Z, .Shift = true, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    else if (ui.Shortcut({.Key = Keire::UiKey::Z, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Undo);
    else if (ui.Shortcut({.Key = Keire::UiKey::Y, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    else if (ui.Shortcut({.Key = Keire::UiKey::R, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    if (m_CommandRouter->Available(KeireEditor::EditorCommand::DuplicateSelection) &&
        m_ActiveUndoContext == m_SceneDocument->History() &&
        ui.Shortcut({.Key = Keire::UiKey::D, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::DuplicateSelection);
    if (ui.Shortcut({.Key = Keire::UiKey::S, .Shift = true, .Primary = true, .Global = true}) &&
        m_SceneDocument->EditingScene() && !m_SceneDocument->SaveDialog())
        SaveSceneAs();
    else if (ui.Shortcut({.Key = Keire::UiKey::S, .Primary = true, .Global = true}))
    {
        if (m_InputActionsDocument->Dirty() && m_InputActionsPanel->Registration().Visible())
        {
            try
            {
                SaveInputActions();
            }
            catch (const std::exception& error)
            {
                m_InputActionsPanel->SetMessage(error.what());
                ReportError("Input", error.what());
            }
        }
        else
            (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveScene);
    }
    auto& workspace = Owner().GetUiWorkspace();
    DrawMainMenu(ui, workspace);
    DrawMainToolbar(ui);
    DrawMainStatusBar(ui);
    OpenPendingDialog(ui);
    DrawNotices(ui, workspace);
    DrawDialogs(ui, workspace);
    DrawExternalAssetImport(ui);

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
    m_ProjectSettingsPanel->Draw(ui, m_Theme);
    DrawPrefabOverrides(ui);
    DrawBuildSettings(ui);
    DrawProfiler(ui);
    DrawPlayChanges(ui);
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
        m_ConsolePanel->Add(std::move(category), std::move(message), color, Owner().GetTime().FrameCount(), level);
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

void EditorWorkspaceLayer::DrawConsole(Keire::UiFrame& ui) { m_ConsolePanel->Draw(ui, m_Theme); }

void EditorWorkspaceLayer::DrawDiagnostics(Keire::UiFrame& ui)
{
    const auto& time = Owner().GetTime();
    const auto size = Owner().MainWindow()->LogicalSize();
    m_DiagnosticsPanel->Draw(ui, m_Theme, time.FrameCount(), time.UnscaledDeltaTime().Milliseconds(),
                             {static_cast<float>(size.Width), static_cast<float>(size.Height)}, Owner().UiCapture());
}
