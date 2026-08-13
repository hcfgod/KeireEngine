#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/Scenes/PrefabAsset.h"
#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PackageManagerPanel.h"
#include "KeireClient/Editor/PlayerBuildService.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/SceneTransitionCoordinator.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
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

        bool EditEvent(const std::string_view label, Keire::ComponentEventValue& value,
                       const std::size_t argumentCount) override
        {
            (void)argumentCount;
            m_Ui.Text(label);
            m_Ui.TextColored({0.48F, 0.55F, 0.64F, 1.0F},
                             std::to_string(value.Listeners.size()) +
                                 (value.Listeners.size() == 1 ? " persistent listener" : " persistent listeners"));
            return false;
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

EditorWorkspaceLayer::EditorWorkspaceLayer(const bool smoke, const bool initializeProject, const bool smokePlay,
                                           std::filesystem::path executable)
    : Layer("EditorWorkspaceLayer"), m_AssetBrowserPanel(std::make_unique<KeireEditor::AssetBrowserPanel>(
                                         static_cast<KeireEditor::IAssetBrowserController&>(*this))),
      m_ConsolePanel(std::make_unique<KeireEditor::ConsolePanel>([this](const std::string_view text)
                                                                 { Owner().Windows()->SetClipboardText(text); })),
      m_DiagnosticsPanel(std::make_unique<KeireEditor::DiagnosticsPanel>()),
      m_SceneDocument(std::make_unique<KeireEditor::SceneDocument>()),
      m_InputActionsDocument(std::make_unique<KeireEditor::InputActionsDocument>()),
      m_AnimatorControllerDocument(std::make_unique<KeireEditor::AnimatorControllerDocument>()),
      m_AudioMixerDocument(
          std::make_unique<KeireEditor::AudioMixerDocument>(KeireEditor::AudioMixerDocumentSpecification{
              .Preview = [this](const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
              { PreviewAudioMixer(asset, definition); },
              .StopPreview = [this](const Keire::AssetId) { StopAudioMixerPreview(); },
              .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
              { PersistAudioMixer(asset, bytes); },
          })),
      m_VfxEffectDocument(std::make_unique<KeireEditor::VfxEffectDocument>(KeireEditor::VfxEffectDocumentSpecification{
          .Preview = [this](const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
          { PreviewVfxEffect(asset, definition); },
          .StopPreview = [this](const Keire::AssetId) { StopVfxEffectPreview(); },
          .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
          { PersistVfxEffect(asset, bytes); },
      })),
      m_ShaderGraphDocument(
          std::make_unique<KeireEditor::ShaderGraphDocument>(KeireEditor::ShaderGraphDocumentSpecification{
              .Preview =
                  [this](const Keire::AssetId, const Keire::ShaderGraphCompilation& compilation,
                         const KeireEditor::ShaderGraphPreviewSettings& settings)
              {
                  if (m_ShaderGraphPanel)
                      m_ShaderGraphPanel->UpdatePreview(compilation, settings);
              },
              .LiveApply = [this](const Keire::AssetId asset, const Keire::ShaderGraphDefinition& definition,
                                  const Keire::ShaderGraphCompilation& compilation,
                                  const std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders)
              { ApplyShaderGraphDevelopmentRevision(asset, definition, compilation, developmentShaders); },
              .StopPreview =
                  [this](const Keire::AssetId)
              {
                  if (m_ShaderGraphPanel)
                      m_ShaderGraphPanel->ClearPreview();
              },
              .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
              { PersistShaderGraph(asset, bytes); },
          })),
      m_MaterialGraphDocument(
          std::make_unique<KeireEditor::MaterialGraphDocument>(KeireEditor::MaterialGraphDocumentSpecification{
              .ResolveInterface = [this](const Keire::MaterialShaderReference& shader)
              { return ResolveMaterialGraphInterface(shader); },
              .ResolveTemplate = [this](const Keire::MaterialShaderReference& shader)
              { return ResolveMaterialGraphTemplate(shader); },
              .ResolveFunction = [this](const Keire::AssetId asset) { return ResolveReusableGraph(asset); },
              .ResolveShader = [this](const Keire::MaterialShaderReference& shader)
              { return ResolveMaterialGraphShader(shader); },
              .Preview = [this](const Keire::AssetId asset, const Keire::MaterialAssetDefinition& material)
              { ApplyMaterialGraphDevelopmentRevision(asset, material); },
              .StopPreview = [](const Keire::AssetId) {},
              .Persist = [this](const Keire::AssetId asset, const std::span<const std::byte> bytes)
              { PersistMaterialGraph(asset, bytes); },
          })),
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
      m_AnimatorControllerPanel(std::make_unique<KeireEditor::AnimatorControllerPanel>(
          static_cast<KeireEditor::IAnimatorControllerPanelController&>(*this))),
      m_RiggingStudioPanel(std::make_unique<KeireEditor::RiggingStudioPanel>(
          static_cast<KeireEditor::IRiggingStudioController&>(*this))),
      m_AudioMixerPanel(
          std::make_unique<KeireEditor::AudioMixerPanel>(static_cast<KeireEditor::IAudioMixerPanelController&>(*this))),
      m_VfxEffectPanel(
          std::make_unique<KeireEditor::VfxEffectPanel>(static_cast<KeireEditor::IVfxEffectPanelController&>(*this))),
      m_ShaderGraphPanel(std::make_unique<KeireEditor::ShaderGraphPanel>(
          static_cast<KeireEditor::IShaderGraphPanelController&>(*this))),
      m_MaterialGraphPanel(std::make_unique<KeireEditor::MaterialGraphPanel>(
          static_cast<KeireEditor::IMaterialGraphPanelController&>(*this))),
      m_ProjectSettingsPanel(std::make_unique<KeireEditor::ProjectSettingsPanel>(
          *m_ProjectSettingsDocument, static_cast<KeireEditor::IProjectSettingsController&>(*this))),
      m_LightingPanel(
          std::make_unique<KeireEditor::LightingPanel>(static_cast<KeireEditor::ILightingPanelController&>(*this))),
      m_PackageManagerPanel(std::make_unique<KeireEditor::PackageManagerPanel>()),
      m_PropertyDrawers(std::make_unique<KeireEditor::PropertyDrawerRegistry>()),
      m_ViewportAssetDropRouter(std::make_unique<KeireEditor::ViewportAssetDropRouter>()),
      m_PlayChangesPanel(std::make_unique<KeireEditor::ScenePlayChangesPanel>()),
      m_SceneTransitions(std::make_unique<KeireEditor::SceneTransitionCoordinator>()),
      m_ExternalAssetImport(std::make_unique<KeireEditor::ExternalAssetImportController>()),
      m_ExecutablePath(std::move(executable)), m_Smoke(smoke), m_InitializeProject(initializeProject),
      m_SmokePlay(smokePlay)
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
        Keire::AudioReverbZoneComponent::StaticType(), "shape",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty& property,
           Keire::ComponentPropertyValue& value)
        {
            auto* shape = std::get_if<std::int64_t>(&value);
            if (!shape)
                throw std::invalid_argument("Audio Reverb Zone shape metadata must serialize an Integer.");
            constexpr std::array choices{std::string_view("Box"), std::string_view("Sphere")};
            return editor.EditChoice(property.DisplayName, *shape, choices);
        });
    const auto registerDecibelGain = [this](const Keire::ComponentTypeId type, const std::string_view key)
    {
        m_PropertyDrawers->RegisterOverride(type, std::string(key),
                                            [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty&,
                                               Keire::ComponentPropertyValue& value)
                                            {
                                                auto* gain = std::get_if<double>(&value);
                                                if (!gain)
                                                    throw std::invalid_argument(
                                                        "Audio gain metadata must serialize a Scalar.");
                                                double decibels = Keire::LinearToDecibels(static_cast<float>(*gain));
                                                if (!editor.EditScalar("Volume (dB)", decibels, 0.1, -96.0, 24.0824))
                                                    return false;
                                                *gain = Keire::DecibelsToLinear(static_cast<float>(decibels));
                                                return true;
                                            });
    };
    registerDecibelGain(Keire::AudioSourceComponent::StaticType(), "gain");
    registerDecibelGain(Keire::AudioListenerComponent::StaticType(), "gain");
    m_PropertyDrawers->RegisterOverride(
        Keire::AudioReverbZoneComponent::StaticType(), "reverbSend",
        [](KeireEditor::IPropertyEditor& editor, const Keire::ComponentProperty&, Keire::ComponentPropertyValue& value)
        {
            auto* send = std::get_if<double>(&value);
            if (!send)
                throw std::invalid_argument("Audio Reverb Zone send metadata must serialize a Scalar.");
            double percentage = *send * 100.0;
            if (!editor.EditScalar("Reverb Amount (%)", percentage, 1.0, 0.0, 100.0))
                return false;
            *send = percentage / 100.0;
            return true;
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
            const auto scene = ActiveScene();
            const auto selected = m_SceneDocument->Selections();
            const std::unordered_set<Keire::AssetId> selectedSet(selected.begin(), selected.end());
            std::vector<Keire::AssetId> roots;
            roots.reserve(selected.size());
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

            RecordSceneUndo("Delete Entities");
            for (const auto entity : roots)
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
        [this]
        {
            return m_SceneDocument->EditingScene() && !m_SceneDocument->PlaySession() && !m_PlayChanges &&
                   !m_PlayStartPending;
        });
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
        [this] { return (m_PlayStartPending || m_SceneDocument->PlaySession()) && !m_PlayChanges; });
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
        KeireEditor::EditorCommand::BuildScripts,
        [this]
        {
            try
            {
                StartManagedBuild();
                AddConsoleMessage("Managed Build", "Script build started.", m_Theme.Accent);
            }
            catch (const std::exception& error)
            {
                ReportError("Managed Build", error.what());
            }
        },
        [this]
        {
            const auto scripts = Owner().Scripts();
            if (!scripts || !m_AssetDatabase)
                return false;
            const auto state = scripts->BuildStatus().State;
            return state != Keire::ManagedBuildState::Generating && state != Keire::ManagedBuildState::Compiling &&
                   state != Keire::ManagedBuildState::Publishing;
        });
    BindPlayerBuildCommands();
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CancelAssetOperation,
        [this]
        {
            if (m_AssetOperations)
                m_AssetOperations->CancelCurrent();
        },
        [this] { return m_AssetOperations && m_AssetOperations->Busy(); });
    m_CommandRouter->Bind(KeireEditor::EditorCommand::Exit, [this] { RequestEditorExit(); });
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
    m_AssetBrowserPanel->SetJobSystem(Owner().Jobs());
    m_ShaderGraphDocument->SetJobSystem(Owner().Jobs());
    m_ShaderGraphPanel->SetJobSystem(Owner().Jobs());
    m_MaterialGraphPanel->SetJobSystem(Owner().Jobs());
    m_SceneViewportPanel->Attach(workspace);
    m_Game = workspace.RegisterPanel({"editor.game", "Game"});
    m_HierarchyPanel->Attach(workspace);
    m_InspectorPanel->Attach(workspace);
    m_AssetBrowserPanel->Attach(workspace);
    m_ConsolePanel->Attach(workspace);
    m_DiagnosticsPanel->Attach(workspace);
    m_ThemeEditor = workspace.RegisterPanel({"editor.theme", "Theme Editor", false});
    m_InputActionsPanel->Attach(workspace);
    m_AnimatorControllerPanel->Attach(workspace);
    m_RiggingStudioPanel->Attach(workspace);
    m_AudioMixerPanel->Attach(workspace);
    m_VfxEffectPanel->Attach(workspace);
    m_ShaderGraphPanel->Attach(workspace);
    m_MaterialGraphPanel->Attach(workspace);
    m_InputDebugger = workspace.RegisterPanel({"editor.input-debugger", "Input Debugger", false});
    m_ProjectSettingsPanel->Attach(workspace);
    m_LightingPanel->Attach(workspace);
    m_PackageManagerPanel->Attach(workspace);
    m_PrefabOverrides = workspace.RegisterPanel({"editor.prefab-overrides", "Prefab Overrides", false});
    m_BuildSettings = workspace.RegisterPanel({"editor.build-settings", "Build Settings", false});
    m_Profiler = workspace.RegisterPanel({"editor.profiler", "Profiler", false});
    m_RenderGraph = workspace.RegisterPanel({"editor.render-graph", "Render Graph", false});
    m_ArchitectureDashboard = workspace.RegisterPanel({"editor.architecture", "Architecture", false});
    if (const auto undo = Owner().Undo())
    {
        m_ThemeUndoContext = undo->CreateContext({.Name = "Theme Authoring"});
        m_ManagedDataUndoContext = undo->CreateContext({.Name = "Managed Data Authoring"});
    }
    if (const auto renderer = Owner().Renderer(); renderer && renderer->Mode() != Keire::RenderMode::Disabled)
    {
        Keire::RenderSurfaceSpecification gameSurface;
        gameSurface.Name = "Game View";
        gameSurface.ClearColor = {0.10F, 0.12F, 0.16F, 1.0F};
        m_GameRenderView = renderer->CreateView(gameSurface);
        renderer->RequestGpuVfxPipelineWarmup();
    }
    m_SceneViewportPanel->Initialize(Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path{});
    if (const auto project = Owner().GetProject())
        m_PackageManagerPanel->Initialize(project->Root(), m_ExecutablePath);
    LoadTheme(workspace, workspace.ActiveTheme());
    m_ConsolePanel->CaptureEngineLogs(Owner().GetTime().FrameCount(), m_Theme);
    if (!m_Smoke || m_InitializeProject)
    {
        try
        {
            Keire::AssetDatabaseSpecification databaseSpecification;
            const auto project = Owner().GetProject();
            if (!project)
                throw std::runtime_error("Editor workspace requires an active project.");
            auto authoringSettings = Keire::DefaultProjectAuthoringSettings();
            try
            {
                authoringSettings = Keire::LoadProjectAuthoringSettings(project->Root());
                if (const auto physics = Owner().Physics())
                    physics->ConfigureCollisionMatrix(authoringSettings.PhysicsCollisionMatrix);
            }
            catch (const std::exception& error)
            {
                ReportError("Physics",
                            std::string("Invalid project authoring settings; using defaults: ") + error.what());
            }
            databaseSpecification.ProjectRoot = project->Root();
            databaseSpecification.ChangeDebounce = std::chrono::milliseconds(75);
            databaseSpecification.Jobs = Owner().Jobs();
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
            m_ProjectSettingsDocument->Open(project->Root(), renderEnvironment, std::move(authoringSettings),
                                            std::move(projectSettingsUndo));
            if (const auto undo = Owner().Undo())
                m_AssetBrowserPanel->SetUndoContext(undo->CreateContext({.Name = "Project Assets"}));
            ConfigureAssetImporters(databaseSpecification);
            m_AssetDatabase = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            m_AssetOperations = std::make_unique<KeireEditor::AssetOperationService>(
                KeireEditor::AssetOperationService::ResolveWorkerExecutable(m_ExecutablePath), project->Root());
            InitializePlayerBuild();
            RefreshAssetBrowserRecords();
            const auto catalog = project->AssetCatalog();
            std::error_code catalogError;
            bool requiresSynchronousImport = !std::filesystem::is_regular_file(catalog, catalogError) || catalogError;
            if (!requiresSynchronousImport)
            {
                requiresSynchronousImport =
                    std::ranges::any_of(m_AssetRecords,
                                        [this](const Keire::AssetSourceRecord& record)
                                        {
                                            const auto importer =
                                                m_AssetDatabase->FindImporterForPath(record.RelativePath);
                                            return importer && importer->Name == record.Importer &&
                                                   importer->Version > record.ImporterVersion;
                                        });
            }
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
                if (!input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(1)) ||
                    !input->PairDevice(m_EditorInputUser, Keire::InputDeviceId(2)))
                    throw std::runtime_error("The editor could not claim the keyboard and mouse input devices.");
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
                    RequestEditorExit();
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
    if (const auto scripts = Owner().Scripts())
        scripts->SetRuntimeServices(this);
    if (Owner().Scripts() && m_AssetDatabase)
        m_ManagedBuildDebounceSeconds = 0.0;
    if (Keire::Detail::IsCurrentProcessElevated())
    {
        m_AssetStatus = "External asset drag-and-drop is unavailable while the Editor is running as administrator.";
        m_Notice = "Windows blocks files dragged from Explorer into an elevated application. Close the Editor and "
                   "Kéire Hub, then reopen Kéire Hub normally. Your project and assets are unchanged.";
        m_NoticeColor = m_Theme.Warning;
    }
}

void EditorWorkspaceLayer::OnDetach() noexcept
{
    ShutdownPlayerBuild();
    m_PackageManagerPanel->Shutdown();
    if (m_AssetOperations)
        m_AssetOperations->Shutdown();
    try
    {
        if (const auto scripts = Owner().Scripts())
            scripts->SetRuntimeServices(nullptr);
    }
    catch (...)
    {
    }
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
    m_ManagedInputCaptureOverride.reset();
    m_GameplayInputContext.Reset();
    m_ManagedCursorLocked = false;
    m_ManagedCursorVisible = true;
    m_GameViewportCaptureSuspended = false;
    ApplyManagedCursorMode();
    StopInspectorAudioPreview();
    m_InspectorPanel->ClearSceneState();
    m_SceneDocument->EndPlay();
    m_GameEditPresentation.Reset();
    m_GameRenderView.Reset();
    m_InputActionsPanel->ResetTransientState();
    m_AudioMixerPanel->StopTransientPreview();
    m_VfxEffectPanel->StopTransientPreview();
    StopEditModeVfxPreviews();
    ResetEditorVfxPreviewWorld();
    m_InputContext.Reset();
    if (m_InputActionsDocument->UndoContext())
        m_InputActionsDocument->UndoContext()->Close();
    if (m_AudioMixerDocument->UndoContext())
        m_AudioMixerDocument->UndoContext()->Close();
    if (m_VfxEffectDocument->UndoContext())
        m_VfxEffectDocument->UndoContext()->Close();
    if (m_ShaderGraphDocument->UndoContext())
        m_ShaderGraphDocument->UndoContext()->Close();
    if (m_MaterialGraphDocument->UndoContext())
        m_MaterialGraphDocument->UndoContext()->Close();
    if (m_SceneDocument->UndoContext())
        m_SceneDocument->UndoContext()->Close();
    if (m_ThemeUndoContext)
        m_ThemeUndoContext->Close();
    if (m_ManagedDataUndoContext)
        m_ManagedDataUndoContext->Close();
    m_ActiveUndoContext.Reset();
    m_ThemeUndoContext.Reset();
    m_ManagedDataUndoContext.Reset();
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
    m_AudioMixerDocument->Close();
    m_VfxEffectDocument->Close();
    m_ShaderGraphDocument->Close();
    m_MaterialGraphDocument->Close();
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
    {
        Keire::ProfileScope playFixed(Owner().GetProfiler(), Keire::ProfileCategory::Physics, "Play fixed + physics");
        m_SceneDocument->PlaySession()->FixedUpdate(static_cast<float>(time.FixedDeltaTime().Seconds()));
    }
}

void EditorWorkspaceLayer::OnUpdate(const Keire::Time& time)
{
    m_ConsolePanel->CaptureEngineLogs(time.FrameCount(), m_Theme);
    {
        Keire::ProfileScope transitions(Owner().GetProfiler(), Keire::ProfileCategory::Application, "Transitions");
        ProcessSceneTransition();
        FinalizePendingPlayEditorMutation();
        if (m_PendingPlayTransition != PendingPlayTransition::None)
        {
            const auto transition = std::exchange(m_PendingPlayTransition, PendingPlayTransition::None);
            FinishPlayMode(transition == PendingPlayTransition::Apply);
        }
    }
    if (m_Smoke)
    {
        ++m_FrameCount;
        if (m_SmokePlay)
        {
            if (!m_SmokePlayRequested && m_SceneDocument->EditingScene())
            {
                m_SmokePlayRequested = true;
                BeginPlayMode();
            }
            if (m_SceneDocument->PlaySession())
            {
                if (++m_SmokePlayFrameCount >= 120)
                    Owner().RequestExit();
            }
            else if (m_FrameCount >= 3600)
            {
                throw std::runtime_error("Play Mode smoke timed out before a runtime scene became active.");
            }
        }
        else if (m_FrameCount >= 8)
            Owner().RequestExit();
    }
    if (m_SceneDocument->PlaySession())
    {
        Keire::ProfileScope playUpdate(Owner().GetProfiler(), Keire::ProfileCategory::Scripting, "Play update");
        m_SceneDocument->PlaySession()->Update(static_cast<float>(time.DeltaTime().Seconds()));
        if (m_SceneDocument->PlaySession()->State() == Keire::ScenePlayState::Faulted && !m_PlayFaultReported)
        {
            const auto diagnostic = m_SceneDocument->PlaySession()->Diagnostic();
            m_SceneDocument->SetStatus(diagnostic.Callback + " failed: " + diagnostic.Message);
            ReportError("Play Mode", m_SceneDocument->Status());
            m_PlayFaultReported = true;
        }
    }
    if (m_SceneDocument->PlaySession())
    {
        StopEditModeVfxPreviews();
    }
    else
    {
        try
        {
            SynchronizeEditModeVfxPreviews();
        }
        catch (const std::exception& error)
        {
            StopEditModeVfxPreviews();
            ReportError("VFX", std::string("Edit-mode VFX preview synchronization failed: ") + error.what());
        }
        if (m_VfxEffectPreviewWorld)
        {
            const auto deltaSeconds = static_cast<float>(std::clamp(time.UnscaledDeltaTime().Seconds(), 0.0, 0.1));
            m_VfxEffectPreviewWorld->Update(deltaSeconds);
            if (m_VfxEffectPreviewAutoRestart && m_VfxEffectPreviewEffect &&
                !m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
            {
                m_VfxEffectPreviewHandle = m_VfxEffectPreviewWorld->Activate(
                    {m_VfxEffectPreviewEffect, m_VfxEffectPreviewRevision, m_VfxEffectPreviewPosition,
                     m_VfxEffectPreviewRotation, m_VfxEffectPreviewSeedOffset});
                if (m_VfxEffectPreviewHandle)
                {
                    m_VfxEffectPreviewWorld->SetSimulationSpeed(
                        m_VfxEffectPreviewHandle, m_VfxEffectPreviewPaused ? 0.0F : m_VfxEffectPreviewSpeed);
                }
            }
        }
    }
    CompleteSaveSceneAs();
    {
        Keire::ProfileScope managedBuild(Owner().GetProfiler(), Keire::ProfileCategory::Scripting, "Managed build");
        UpdateManagedBuild(time);
        ContinuePendingPlayMode();
    }
    UpdatePlayerBuild();
    if (!m_AssetDatabase)
        return;
    Keire::ProfileScope assetWork(Owner().GetProfiler(), Keire::ProfileCategory::Assets, "Asset work");
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
    m_ShaderGraphDocument->AdvanceCompilation(time.UnscaledDeltaTime().Seconds());
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
    if (m_AssetPollSeconds < 0.1)
        return;
    m_AssetPollSeconds = 0.0;
    try
    {
        const auto changed = m_AssetDatabase->PollChangedAssets();
        if (!changed.empty())
        {
            bool requiresAssetImport = false;
            for (const auto id : changed)
            {
                const auto record = m_AssetDatabase->Find(id);
                const auto previous = std::ranges::find(m_AssetRecords, id, &Keire::AssetSourceRecord::Id);
                const auto path = record                             ? record->RelativePath
                                  : previous != m_AssetRecords.end() ? previous->RelativePath
                                                                     : std::filesystem::path{};
                if (path.extension() == ".cs" || path.extension() == ".keireasm")
                {
                    m_ManagedBuildDebounceSeconds = 0.1;
                }
                if (path.extension() != ".cs")
                    requiresAssetImport = true;
            }
            RefreshAssetBrowserRecords();
            if (requiresAssetImport)
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
        else if (m_AudioMixerDocument->UndoContext() && m_AudioMixerDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_AudioMixerDocument->UndoContext();
        else if (m_VfxEffectDocument->UndoContext() && m_VfxEffectDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_VfxEffectDocument->UndoContext();
        else if (m_ShaderGraphDocument->UndoContext() && m_ShaderGraphDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_ShaderGraphDocument->UndoContext();
        else if (m_MaterialGraphDocument->UndoContext() && m_MaterialGraphDocument->UndoContext()->IsOpen())
            m_ActiveUndoContext = m_MaterialGraphDocument->UndoContext();
        else if (m_AssetBrowserPanel)
            m_ActiveUndoContext = m_AssetBrowserPanel->UndoContext();
    }
    if (ui.Shortcut({.Key = Keire::UiKey::B, .Shift = true, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::BuildScripts);
    const bool shiftRedo = ui.Shortcut({.Key = Keire::UiKey::Z, .Shift = true, .Primary = true, .Global = true});
    const bool undo = !shiftRedo && ui.Shortcut({.Key = Keire::UiKey::Z, .Primary = true, .Global = true});
    const bool alternateRedo = !shiftRedo && !undo &&
                               (ui.Shortcut({.Key = Keire::UiKey::Y, .Primary = true, .Global = true}) ||
                                ui.Shortcut({.Key = Keire::UiKey::R, .Primary = true, .Global = true}));
    if (shiftRedo || alternateRedo)
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Redo);
    else if (undo)
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::Undo);
    if (m_CommandRouter->Available(KeireEditor::EditorCommand::DuplicateSelection) &&
        m_ActiveUndoContext == m_SceneDocument->History() &&
        ui.Shortcut({.Key = Keire::UiKey::D, .Primary = true, .Global = true}))
        (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::DuplicateSelection);
    if (ui.Shortcut({.Key = Keire::UiKey::S, .Shift = true, .Primary = true, .Global = true}) &&
        m_SceneDocument->EditingScene() && !m_SceneDocument->SaveDialog())
        SaveSceneAs();
    else if (ui.Shortcut({.Key = Keire::UiKey::S, .Primary = true, .Global = true}))
    {
        if (m_MaterialGraphDocument->Dirty() && m_MaterialGraphPanel->Registration().Visible() &&
            m_ActiveUndoContext == m_MaterialGraphDocument->UndoContext())
        {
            try
            {
                SaveMaterialGraph();
            }
            catch (const std::exception& error)
            {
                ReportError("Material Graph", error.what());
            }
        }
        else if (m_ShaderGraphDocument->Dirty() && m_ShaderGraphPanel->Registration().Visible() &&
                 m_ActiveUndoContext == m_ShaderGraphDocument->UndoContext())
        {
            try
            {
                SaveShaderGraph();
            }
            catch (const std::exception& error)
            {
                ReportError("Shader Graph", error.what());
            }
        }
        else if (m_VfxEffectDocument->Dirty() && m_VfxEffectPanel->Registration().Visible() &&
                 m_ActiveUndoContext == m_VfxEffectDocument->UndoContext())
        {
            try
            {
                SaveVfxEffect();
            }
            catch (const std::exception& error)
            {
                m_VfxEffectPanel->SetMessage(error.what());
                ReportError("VFX", error.what());
            }
        }
        else if (m_AudioMixerDocument->Dirty() && m_AudioMixerPanel->Registration().Visible())
        {
            try
            {
                SaveAudioMixer();
            }
            catch (const std::exception& error)
            {
                m_AudioMixerPanel->SetMessage(error.what());
                ReportError("Audio", error.what());
            }
        }
        else if (m_AnimatorControllerDocument->Dirty() && m_AnimatorControllerPanel->Registration().Visible())
        {
            try
            {
                SaveAnimationGraph();
            }
            catch (const std::exception& error)
            {
                m_AnimatorControllerPanel->SetMessage(error.what());
                ReportError("Animation", error.what());
            }
        }
        else if (m_InputActionsDocument->Dirty() && m_InputActionsPanel->Registration().Visible())
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
        else if (m_VfxEffectDocument->Dirty() && m_VfxEffectPanel->Registration().Visible())
        {
            try
            {
                SaveVfxEffect();
            }
            catch (const std::exception& error)
            {
                m_VfxEffectPanel->SetMessage(error.what());
                ReportError("VFX", error.what());
            }
        }
        else
            (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::SaveScene);
    }
    const auto profiler = Owner().GetProfiler();
    auto& workspace = Owner().GetUiWorkspace();
    {
        Keire::ProfileScope chrome(profiler, Keire::ProfileCategory::User, "Editor UI / Chrome");
        DrawMainMenu(ui, workspace);
        DrawMainToolbar(ui);
        DrawMainStatusBar(ui);
        OpenPendingDialog(ui);
        DrawNotices(ui, workspace);
        DrawDialogs(ui, workspace);
        DrawExternalAssetImport(ui);
    }

    const bool playActive =
        m_SceneDocument->PlaySession() && m_SceneDocument->PlaySession()->State() != Keire::ScenePlayState::Stopped;
    const auto windows = Owner().Windows();
    const auto mainWindow = Owner().MainWindow();
    const bool nativeCursorCaptured =
        windows && mainWindow && windows->GetCursorMode(mainWindow->Id()) != Keire::CursorMode::Normal;
    if (playActive && nativeCursorCaptured && ui.KeyDown(Keire::UiKey::Escape))
    {
        // This is deliberately independent of the project's input asset and managed scripts. If gameplay routing is
        // broken, Escape must still return control to the editor so the user can stop Play Mode.
        m_GameViewportCaptureSuspended = true;
        m_GameViewportInputActive = false;
        ApplyManagedCursorMode();
    }
    else if (!playActive)
    {
        if (windows && mainWindow && windows->GetCursorMode(mainWindow->Id()) != Keire::CursorMode::Normal &&
            (ui.Shortcut({.Key = Keire::UiKey::Escape, .Global = true}) ||
             ui.Shortcut({.Key = Keire::UiKey::Tab, .Global = true})))
        {
            const auto viewport = m_SceneViewportPanel->ViewportRect();
            windows->SetCursorMode(mainWindow->Id(), Keire::CursorMode::Normal);
            if (viewport.Size().Width > 0.0F && viewport.Size().Height > 0.0F)
            {
                windows->WarpCursor(mainWindow->Id(),
                                    {static_cast<std::int32_t>((viewport.Minimum.X + viewport.Maximum.X) * 0.5F),
                                     static_cast<std::int32_t>((viewport.Minimum.Y + viewport.Maximum.Y) * 0.5F)});
            }
        }
    }
    {
        Keire::ProfileScope sceneViewport(profiler, Keire::ProfileCategory::User, "Editor UI / Scene viewport");
        m_SceneViewportPanel->Draw(ui);
        if (!playActive)
            DrawPerformanceOverlay(ui, m_SceneViewportPanel->ViewportRect(), "SCENE");
    }
    {
        Keire::ProfileScope gameViewport(profiler, Keire::ProfileCategory::User, "Editor UI / Game viewport");
        DrawGame(ui);
    }
    {
        Keire::ProfileScope hierarchy(profiler, Keire::ProfileCategory::User, "Editor UI / Hierarchy");
        m_HierarchyPanel->Draw(ui);
    }
    {
        Keire::ProfileScope inspector(profiler, Keire::ProfileCategory::User, "Editor UI / Inspector");
        m_InspectorPanel->Draw(ui);
    }
    {
        Keire::ProfileScope project(profiler, Keire::ProfileCategory::User, "Editor UI / Project");
        DrawProject(ui);
        if (m_AssetBrowserPanel && m_AssetBrowserPanel->Focused())
            m_ActiveUndoContext = m_AssetBrowserPanel->UndoContext();
    }
    {
        Keire::ProfileScope diagnostics(profiler, Keire::ProfileCategory::User, "Editor UI / Diagnostics");
        DrawConsole(ui);
        DrawDiagnostics(ui);
    }
    {
        Keire::ProfileScope tools(profiler, Keire::ProfileCategory::User, "Editor UI / Tools");
        DrawThemeEditor(ui, workspace);
        m_InputActionsPanel->Draw(ui);
        m_AnimatorControllerPanel->Draw(ui);
        m_RiggingStudioPanel->Draw(ui);
        m_AudioMixerPanel->Draw(ui);
        m_VfxEffectPanel->Draw(ui);
        m_ShaderGraphPanel->Draw(ui);
        m_MaterialGraphPanel->Draw(ui);
        DrawInputDebugger(ui);
        m_ProjectSettingsPanel->Draw(ui, m_Theme);
        m_LightingPanel->Draw(ui, m_Theme);
        m_PackageManagerPanel->Draw(ui, m_Theme);
    }
    {
        Keire::ProfileScope production(profiler, Keire::ProfileCategory::User, "Editor UI / Production");
        DrawPrefabOverrides(ui);
        DrawBuildSettings(ui);
        DrawProfiler(ui);
        DrawRenderGraph(ui);
        DrawArchitectureDashboard(ui);
        DrawPlayChanges(ui);
    }
}

void EditorWorkspaceLayer::AddConsoleMessage(std::string category, std::string message, const Keire::UiColor color,
                                             const Keire::LogLevel level) noexcept
{
    m_ConsolePanel->LogAndCapture(std::move(category), std::move(message), color, Owner().GetTime().FrameCount(),
                                  m_Theme, level);
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
                             {static_cast<float>(size.Width), static_cast<float>(size.Height)}, Owner().UiCapture(),
                             Owner().DiagnosticDefinitions(), Owner().DiagnosticReports(), Owner().Windows());
}
