#include "KeireClient/Editor/AssetBrowserFolderCache.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/EditorWindowPlacement.h"
#include "KeireClient/Editor/ExternalEditorProfiles.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphCreationPicker.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/NamedAssetCreation.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/SceneTransitionCoordinator.h"
#include "KeireClient/Editor/SelectionRange.h"
#include "KeireClient/Editor/ThumbnailService.h"
#include "KeireClient/Editor/VfxEmitterInspector.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireClient/Editor/ViewportInputRouting.h"

#include <doctest/doctest.h>

#include <KeireEditorTests/EditorTestSupport.h>

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void SetTestWorkerMode(const char* value)
    {
#if defined(_WIN32)
        if (_putenv_s("KEIRE_EDITOR_TEST_WORKER_MODE", value ? value : "") != 0)
            throw std::runtime_error("Could not configure the editor-test worker mode.");
#else
        const auto result =
            value ? setenv("KEIRE_EDITOR_TEST_WORKER_MODE", value, 1) : unsetenv("KEIRE_EDITOR_TEST_WORKER_MODE");
        if (result != 0)
            throw std::runtime_error("Could not configure the editor-test worker mode.");
#endif
    }

    class TestPropertyEditor : public KeireEditor::IPropertyEditor
    {
      public:
        bool EditBoolean(std::string_view, bool& value) override
        {
            EditKinds.emplace_back("boolean");
            value = !value;
            return true;
        }
        bool EditInteger(std::string_view, std::int64_t& value, double, std::optional<double>,
                         std::optional<double>) override
        {
            EditKinds.emplace_back("integer");
            ++value;
            return true;
        }
        bool EditChoice(std::string_view, std::int64_t& value, std::span<const std::string_view>) override
        {
            EditKinds.emplace_back("choice");
            ++value;
            return true;
        }
        bool EditScalar(std::string_view, double& value, double, std::optional<double>, std::optional<double>) override
        {
            EditKinds.emplace_back("scalar");
            value = Scalar;
            return true;
        }
        bool EditText(std::string_view, std::string& value) override
        {
            EditKinds.emplace_back("text");
            value = "edited";
            return true;
        }
        bool EditVector2(std::string_view, Keire::Vector2& value, double) override
        {
            EditKinds.emplace_back("vector2");
            value.X += 1.0F;
            return true;
        }
        bool EditVector3(std::string_view, Keire::Vector3& value, double) override
        {
            EditKinds.emplace_back("vector3");
            value.X += 1.0F;
            return true;
        }
        bool EditVector4(std::string_view, Keire::Vector4& value, double) override
        {
            EditKinds.emplace_back("vector4");
            value.X += 1.0F;
            return true;
        }
        bool EditQuaternion(std::string_view, Keire::Quaternion& value, double) override
        {
            EditKinds.emplace_back("quaternion");
            value = {};
            return true;
        }
        bool EditColor(std::string_view, Keire::Color& value) override
        {
            EditKinds.emplace_back("color");
            value.Red = 0.5F;
            return true;
        }
        bool EditAsset(std::string_view, Keire::AssetId& value, std::optional<Keire::AssetTypeId> expectedType) override
        {
            EditKinds.emplace_back("asset");
            ExpectedAssetType = expectedType;
            ExpectedAssetTypes.push_back(expectedType);
            value = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000010");
            return true;
        }
        bool EditTextureAsset(std::string_view label, Keire::AssetId& value,
                              const Keire::ShaderTextureSemantic semantic) override
        {
            TextureSemantics.push_back(semantic);
            return EditAsset(label, value, Keire::Texture2DAsset::StaticType());
        }
        bool EditEntity(std::string_view, Keire::EntityId& value) override
        {
            value = Keire::EntityId(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000011"));
            return true;
        }
        bool EditEvent(std::string_view, Keire::ComponentEventValue&, std::size_t) override { return false; }

        double Scalar = 3.0;
        std::optional<Keire::AssetTypeId> ExpectedAssetType;
        std::vector<std::optional<Keire::AssetTypeId>> ExpectedAssetTypes;
        std::vector<std::string> EditKinds;
        std::vector<Keire::ShaderTextureSemantic> TextureSemantics;
    };

    class CustomComponent final : public Keire::Component
    {
      public:
        CustomComponent() : Component(StaticType()) {}
        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0xed17000000004000ULL, 0x8000000000000012ULL));
        }

        double Value = 1.0;
    };

    Keire::ComponentRegistration CustomRegistration()
    {
        Keire::ComponentRegistration result;
        result.Type = CustomComponent::StaticType();
        result.Name = "Custom";
        result.Properties = {
            {"value", "Value", "Custom", Keire::ComponentPropertyKind::Scalar, false, 0.0, 10.0, 0.25}};
        result.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<CustomComponent>()); };
        result.Serialize = [](const Keire::Component& component)
        { return Keire::ComponentPropertyBag{{"value", dynamic_cast<const CustomComponent&>(component).Value}}; };
        result.Deserialize =
            [](Keire::Component& component, const Keire::ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported custom component version.");
            const auto value = std::get<double>(values.at("value"));
            if (value < 0.0 || value > 10.0)
                throw std::invalid_argument("Custom value is outside its supported range.");
            dynamic_cast<CustomComponent&>(component).Value = value;
        };
        return result;
    }
} // namespace

TEST_CASE("asset browser folder snapshots avoid steady-state filesystem traversal")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-AssetBrowserFolderCache-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Materials", error);
    REQUIRE_FALSE(error);
    std::filesystem::create_directories(root / "Scripts" / "Runtime", error);
    REQUIRE_FALSE(error);

    KeireEditor::AssetBrowserFolderCache cache;
    REQUIRE(cache.Refresh(root));
    const std::vector<std::filesystem::path> expected{std::filesystem::path("Materials"),
                                                      std::filesystem::path("Scripts"),
                                                      std::filesystem::path("Scripts") / "Runtime"};
    CHECK(std::ranges::equal(cache.Folders(), expected));

    std::filesystem::remove_all(root / "Materials", error);
    REQUIRE_FALSE(error);
    CHECK(std::ranges::equal(cache.Folders(), expected));
    REQUIRE(cache.Refresh(root));
    const std::vector<std::filesystem::path> afterRemoval{std::filesystem::path("Scripts"),
                                                          std::filesystem::path("Scripts") / "Runtime"};
    CHECK(std::ranges::equal(cache.Folders(), afterRemoval));

    std::filesystem::remove_all(root, error);
    REQUIRE_FALSE(error);
    CHECK_FALSE(cache.Refresh(root));
    CHECK(std::ranges::equal(cache.Folders(), afterRemoval));
}

TEST_CASE("editor command router centralizes availability and execution")
{
    KeireEditor::EditorCommandRouter router;
    bool available = false;
    unsigned int executions = 0;
    router.Bind(
        KeireEditor::EditorCommand::SaveScene, [&executions] { ++executions; }, [&available] { return available; });

    CHECK_FALSE(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 0);
    available = true;
    CHECK(router.Available(KeireEditor::EditorCommand::SaveScene));
    CHECK(router.Execute(KeireEditor::EditorCommand::SaveScene));
    CHECK(executions == 1);
    CHECK_FALSE(router.Execute(KeireEditor::EditorCommand::Exit));
    CHECK_THROWS_AS(router.Bind(KeireEditor::EditorCommand::Exit, {}), std::invalid_argument);

    constexpr std::array routedCommands{
        KeireEditor::EditorCommand::CreateEntity, KeireEditor::EditorCommand::DeleteSelection,
        KeireEditor::EditorCommand::SelectAll,    KeireEditor::EditorCommand::ClearSelection,
        KeireEditor::EditorCommand::Play,         KeireEditor::EditorCommand::Pause,
        KeireEditor::EditorCommand::Stop,         KeireEditor::EditorCommand::Undo,
        KeireEditor::EditorCommand::Redo,
    };
    for (const auto command : routedCommands)
        router.Bind(command, [&executions] { ++executions; });
    for (const auto command : routedCommands)
        CHECK(router.Execute(command));
    CHECK(executions == 1 + routedCommands.size());
}

TEST_CASE("Range selection follows display order and retains the clicked item as active")
{
    const std::array order{Keire::AssetId::Generate(), Keire::AssetId::Generate(), Keire::AssetId::Generate(),
                           Keire::AssetId::Generate(), Keire::AssetId::Generate()};

    const auto forward = KeireEditor::BuildRangeSelection(order, order[1], order[4]);
    REQUIRE(forward.size() == 4);
    CHECK(forward[0] == order[1]);
    CHECK(forward[1] == order[2]);
    CHECK(forward[2] == order[3]);
    CHECK(forward[3] == order[4]);

    const auto reverse = KeireEditor::BuildRangeSelection(order, order[4], order[1]);
    REQUIRE(reverse.size() == 4);
    CHECK(reverse.back() == order[1]);

    const std::array existing{order[0]};
    const auto additive = KeireEditor::BuildRangeSelection(order, order[2], order[4], existing, true);
    REQUIRE(additive.size() == 4);
    CHECK(additive.front() == order[0]);
    CHECK(additive.back() == order[4]);
}

TEST_CASE("Asset Browser double-click routes material and shader authoring assets internally")
{
    using enum KeireEditor::AssetBrowserOpenAction;

    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerial") == Material);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerialgraph") == MaterialGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerialinstance") == MaterialInstance);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Shaders/Surface.keireshadergraph") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Shaders/Surface.KEIRESHADERGRAPH") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Common.keirematerialfunction") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Layer.keiremateriallayer") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Globals.keirematerialcollection") ==
          MaterialParameterCollection);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Textures/Surface.png") == External);

    Keire::AssetSourceRecord instance;
    instance.RelativePath = "Materials/Surface.keirematerialinstance";
    CHECK(KeireEditor::AssetTypeName(instance) == "Material Instance");
    instance.RelativePath = "Materials/Legacy.keireshadergraphinstance";
    CHECK(KeireEditor::AssetTypeName(instance) == "Legacy Shader Graph Instance");
    instance.RelativePath = "Materials/Common.keirematerialfunction";
    CHECK(KeireEditor::AssetTypeName(instance) == "Material Function");
    instance.RelativePath = "Materials/Globals.keirematerialcollection";
    CHECK(KeireEditor::AssetTypeName(instance) == "Material Parameter Collection");
}

TEST_CASE("Asset creation labels keep Shader Graph and Material Graph workflows distinct")
{
    using KeireEditor::NamedAssetCreationDisplayName;
    using KeireEditor::NamedAssetCreationKind;

    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ShaderGraph) == "shader graph");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialGraph) == "material graph");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialInstance) == "material instance");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialFunction) == "material function");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialLayer) == "material layer");
}

TEST_CASE("Material Graph creation only preselects compatible shader sources")
{
    Keire::AssetSourceRecord material;
    material.Id = Keire::AssetId::Generate();
    material.Type = Keire::MaterialGraphAsset::StaticType();
    Keire::AssetSourceRecord shaderGraph;
    shaderGraph.Id = Keire::AssetId::Generate();
    shaderGraph.Type = Keire::ShaderGraphAsset::StaticType();
    Keire::AssetSourceRecord rawShader;
    rawShader.Id = Keire::AssetId::Generate();
    rawShader.Type = Keire::ShaderAsset::StaticType();
    const std::array records{material, shaderGraph, rawShader};

    KeireEditor::MaterialGraphCreationPicker picker;
    picker.Begin(material.Id, records);
    CHECK_FALSE(picker.Shader());
    picker.Begin(shaderGraph.Id, records);
    CHECK(picker.Shader() == shaderGraph.Id);
    picker.Begin(rawShader.Id, records);
    CHECK(picker.Shader() == rawShader.Id);
    picker.Reset();
    CHECK_FALSE(picker.Shader());
}

TEST_CASE("Play changes review remains pending until it is explicitly resolved")
{
    KeireEditor::ScenePlayChangesPanel panel;
    CHECK_FALSE(panel.Pending());

    panel.Open();
    CHECK(panel.Pending());

    panel.Open();
    CHECK(panel.Pending());

    panel.Close();
    CHECK_FALSE(panel.Pending());
}

TEST_CASE("scene document owns selection and deterministic close state")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000001"),
                                                Keire::SceneAsset::EmptyDefinition("Document test"));
    auto entity = scene->CreateEntity("Selected");
    auto second = scene->CreateEntity("Also selected");
    document.Open(scene);
    document.Select(entity.Id().Value());
    CHECK(document.Selection() == entity.Id().Value());
    document.Select(second.Id().Value(), true);
    CHECK(document.Selection() == second.Id().Value());
    CHECK(document.Selections().size() == 2);
    CHECK(document.IsSelected(entity.Id().Value()));
    document.Select(entity.Id().Value(), true);
    CHECK_FALSE(document.IsSelected(entity.Id().Value()));
    CHECK(document.Selection() == second.Id().Value());
    document.Select(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000099"));
    CHECK_FALSE(document.Selection());
    document.SetRecoveryAvailable(true);

    document.Close();
    CHECK_FALSE(document.EditingScene());
    CHECK_FALSE(document.Asset());
    CHECK_FALSE(document.RecoveryAvailable());
    CHECK_FALSE(scene->IsOpen());
}

TEST_CASE("created prefabs connect existing scene objects and can be unpacked")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Prefab connection"));
    const auto root = scene->CreateEntity("Root");
    const auto child = scene->CreateEntity("Child", root);
    auto definition = scene->Snapshot();
    const std::array roots{root.Id().Value()};
    const auto prefab = KeireEditor::CreatePrefabFromSelection(definition, roots, "Connected");
    const auto prefabAsset = Keire::AssetId::Generate();

    const auto instance =
        KeireEditor::ConnectPrefabInstance(definition, prefabAsset, prefab.Template, root.Id().Value());
    CHECK(instance.Prefab == prefabAsset);
    CHECK(instance.Root == root.Id().Value());
    CHECK(instance.Objects.size() == 2);
    CHECK(std::ranges::all_of(instance.Objects, [](const Keire::PrefabObjectMapping& mapping)
                              { return mapping.Source == mapping.Instance; }));
    CHECK(KeireEditor::UnpackPrefab(definition, instance.Root));
    CHECK(definition.PrefabInstances.empty());
    CHECK(std::ranges::any_of(definition.Objects, [&](const Keire::SceneObjectDefinition& object)
                              { return object.Id == child.Id().Value(); }));
}

TEST_CASE("scene rectangle selection returns every active projected entity in the marquee")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000031"),
                                                Keire::SceneAsset::EmptyDefinition("Rectangle picking"));
    auto left = scene->CreateEntity("Left");
    auto right = scene->CreateEntity("Right");
    right.GetComponent<Keire::TransformComponent>()->SetLocalPosition({100.0F, 0.0F, 0.0F});
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};

    auto selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected == std::vector<Keire::EntityId>{left.Id()});
    right.GetComponent<Keire::TransformComponent>()->SetLocalPosition({1.0F, 0.0F, 0.0F});
    selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected.size() == 2);
    right.SetActive(false);
    selected = KeireEditor::SelectSceneEntitiesInRectangle(scene, viewport, viewport, camera);
    CHECK(selected == std::vector<Keire::EntityId>{left.Id()});
}

TEST_CASE("scene gizmo controller owns tool shortcuts and visualization settings")
{
    KeireEditor::SceneGizmoController controller;
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::Q));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::View);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::W));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Translate);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::E));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Rotate);
    CHECK(controller.ApplyToolShortcut(Keire::UiKey::R));
    CHECK(controller.ActiveTool() == KeireEditor::SceneTool::Scale);
    CHECK_FALSE(controller.ApplyToolShortcut(Keire::UiKey::F));

    controller.SetSnapping(true);
    controller.SetShowCameraFrustums(false);
    controller.SetShowLightDirections(false);
    CHECK(controller.Settings().Snapping);
    CHECK_FALSE(controller.Settings().ShowCameraFrustums);
    CHECK_FALSE(controller.Settings().ShowLightDirections);
}

TEST_CASE("scene transform groups move every selected root once and restore the drag baseline")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000032"),
                                                Keire::SceneAsset::EmptyDefinition("Group transform"));
    auto parent = scene->CreateEntity("Parent");
    auto child = scene->CreateEntity("Selected child");
    auto independent = scene->CreateEntity("Independent");
    child.SetParent(parent, false);
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({1.0F, 0.0F, 0.0F});
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({2.0F, 0.0F, 0.0F});
    independent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({5.0F, 0.0F, 0.0F});

    const std::array selections{parent.Id().Value(), child.Id().Value(), independent.Id().Value()};
    const auto targets = KeireEditor::SceneTransformGroup::Capture(scene, selections, independent.Id());
    REQUIRE(targets.size() == 2);
    KeireEditor::SceneTransformGroup::Apply(targets, KeireEditor::SceneTool::Translate,
                                            KeireEditor::SceneTransformAxis::X, 3.0F, {1.0F, 0.0F, 0.0F},
                                            independent.GetComponent<Keire::TransformComponent>()->WorldPosition(), {});
    CHECK(parent.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(4.0F));
    CHECK(child.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(6.0F));
    CHECK(independent.GetComponent<Keire::TransformComponent>()->WorldPosition().X == doctest::Approx(8.0F));

    KeireEditor::SceneTransformGroup::Restore(targets);
    CHECK(parent.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(1.0F));
    CHECK(child.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(2.0F));
    CHECK(independent.GetComponent<Keire::TransformComponent>()->LocalPosition().X == doctest::Approx(5.0F));
}

TEST_CASE("scene document targets the isolated runtime scene while playing")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000060"),
                                                Keire::SceneAsset::EmptyDefinition("Play document"));
    auto authored = scene->CreateEntity("Authored");
    auto undo = Keire::CreateRef<Keire::UndoService>();
    auto editingHistory = undo->CreateContext({.Name = "Edit"});
    auto playHistory = undo->CreateContext({.Name = "Play"});
    document.Open(scene, {}, {}, editingHistory);
    CHECK(document.History() == editingHistory);
    document.BeginPlay(playHistory);
    CHECK(document.History() == playHistory);
    REQUIRE(document.ActiveScene());
    CHECK(document.ActiveScene() != document.EditingScene());
    document.ActiveScene()->FindEntity(authored.Id()).SetName("Runtime");
    CHECK(scene->FindEntity(authored.Id()).Name() == "Authored");
    CHECK(document.ActiveScene()->FindEntity(authored.Id()).Name() == "Runtime");
    document.EndPlay();
    CHECK_FALSE(playHistory->IsOpen());
    CHECK(document.History() == editingHistory);
    document.Close();
}

TEST_CASE("scene document commands validate and target the active scene")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000070"),
                                                Keire::SceneAsset::EmptyDefinition("Document commands"));
    document.Open(scene);
    const auto parent = document.CreateEntity("Parent");
    const auto child = document.CreateEntity("Child", parent, Keire::PointLightComponent::StaticType());
    document.Select(child.Value());
    CHECK_FALSE(KeireEditor::SceneDocument::IsValidEntityName(""));
    CHECK(KeireEditor::SceneDocument::IsValidEntityName(std::string(256, 'a')));
    CHECK_FALSE(KeireEditor::SceneDocument::IsValidEntityName(std::string(257, 'a')));
    CHECK_THROWS_AS(document.RenameEntity(child, ""), std::invalid_argument);
    CHECK(scene->FindEntity(child).Name() == "Child");
    document.RenameEntity(child, "Authored light");
    document.SetEntityActive(child, false);
    document.SetTransform(child, {.Position = Keire::Vector3{1.0F, 2.0F, 3.0F}});
    document.SetComponentProperty(child, Keire::PointLightComponent::StaticType(), "intensity", 12.0);

    auto entity = scene->FindEntity(child);
    REQUIRE(entity);
    const auto transform = entity.GetComponent<Keire::TransformComponent>();
    REQUIRE(transform);
    const auto previousScale = transform->LocalScale();
    const auto previousPosition = transform->LocalPosition();
    CHECK_THROWS_AS(document.SetTransform(child, {.Position = Keire::Vector3{9.0F, 9.0F, 9.0F},
                                                  .Scale = Keire::Vector3{1.0F, 0.0F, 1.0F}}),
                    std::invalid_argument);
    CHECK(transform->LocalPosition() == previousPosition);
    CHECK(transform->LocalScale() == previousScale);
    CHECK_NOTHROW(document.SetTransform(child, {.Scale = Keire::Vector3{2.0F, 3.0F, 4.0F}}));
    CHECK(entity.Name() == "Authored light");
    CHECK_FALSE(entity.ActiveSelf());
    CHECK(entity.Parent().Id() == parent);
    CHECK(transform->LocalPosition() == Keire::Vector3{1.0F, 2.0F, 3.0F});
    CHECK(transform->LocalScale() == Keire::Vector3{2.0F, 3.0F, 4.0F});
    REQUIRE(entity.GetComponent<Keire::PointLightComponent>());
    CHECK(entity.GetComponent<Keire::PointLightComponent>()->Intensity() == doctest::Approx(12.0F));

    const auto rendered = document.CreateEntity("Rendered", {}, Keire::MeshRendererComponent::StaticType());
    scene->MarkSaved();
    const auto material = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071");
    document.SetMeshRendererMaterial(rendered, 0, material);
    REQUIRE(scene->FindEntity(rendered).GetComponent<Keire::MeshRendererComponent>());
    CHECK(scene->FindEntity(rendered).GetComponent<Keire::MeshRendererComponent>()->Material() == material);
    CHECK(scene->Dirty());

    document.BeginPlay();
    document.RenameEntity(child, "Runtime light");
    CHECK(document.ActiveScene()->FindEntity(child).Name() == "Runtime light");
    CHECK(scene->FindEntity(child).Name() == "Authored light");
    document.DeleteEntity(child);
    CHECK_FALSE(document.Selection());
    CHECK_FALSE(document.ActiveScene()->FindEntity(child));
    CHECK(scene->FindEntity(child));
    document.EndPlay();

    CHECK_THROWS_AS(document.SetComponentProperty(parent, Keire::PointLightComponent::StaticType(), "intensity", 1.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(document.SetComponentProperty(child, Keire::PointLightComponent::StaticType(), "unknown", 1.0),
                    std::invalid_argument);
    document.Close();
}

TEST_CASE("scene document moves hierarchy selections as one validated ordered transaction")
{
    KeireEditor::SceneDocument document;
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071"),
                                                Keire::SceneAsset::EmptyDefinition("Hierarchy multi-move"));
    document.Open(scene);

    const auto destination = document.CreateEntity("Destination");
    const auto first = document.CreateEntity("First");
    const auto second = document.CreateEntity("Second");
    const std::array selection{first, second};
    const auto moved = document.MoveEntities(selection, destination);
    REQUIRE(moved.size() == 2);
    CHECK(moved[0] == first);
    CHECK(moved[1] == second);
    CHECK(scene->FindEntity(first).Parent().Id() == destination);
    CHECK(scene->FindEntity(second).Parent().Id() == destination);

    std::vector<Keire::EntityId> destinationOrder;
    for (const auto& object : scene->HierarchySnapshot().Objects)
        if (object.Parent == destination.Value())
            destinationOrder.emplace_back(object.Id);
    REQUIRE(destinationOrder.size() == 2);
    CHECK(destinationOrder[0] == first);
    CHECK(destinationOrder[1] == second);

    const auto group = document.CreateEntity("Group");
    const auto nested = document.CreateEntity("Nested", group);
    const std::array nestedSelection{group, nested};
    const auto movedRoots = document.MoveEntities(nestedSelection, destination);
    REQUIRE(movedRoots.size() == 1);
    CHECK(movedRoots.front() == group);
    CHECK(scene->FindEntity(group).Parent().Id() == destination);
    CHECK(scene->FindEntity(nested).Parent().Id() == group);

    CHECK_THROWS_AS((void)document.MoveEntities(std::span{&group, std::size_t{1}}, nested), std::invalid_argument);
    CHECK(scene->FindEntity(group).Parent().Id() == destination);
    CHECK(scene->FindEntity(nested).Parent().Id() == group);
    CHECK_THROWS_AS((void)document.MoveEntities(selection, destination, second), std::invalid_argument);
    CHECK(scene->FindEntity(first).Parent().Id() == destination);
    CHECK(scene->FindEntity(second).Parent().Id() == destination);
    document.Close();
}

TEST_CASE("scene document owns atomic save and recovery lifecycle")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-SceneDocument-" + Keire::AssetId::Generate().ToString());
    const auto source = root / "Assets/Scenes/Test.keirescene";
    const auto recovery = root / "Library/SceneRecovery/Test.keirescene.recovery";
    std::filesystem::create_directories(source.parent_path());
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000066");
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Document save"));
    auto entity = scene->CreateEntity("Recovered name");
    KeireEditor::SceneDocument document;
    document.Open(scene, asset, source);
    document.SetRecoveryPath(recovery);
    CHECK(document.WriteRecovery());
    REQUIRE(std::filesystem::is_regular_file(recovery));
    scene->FindEntity(entity.Id()).SetName("Later name");
    document.RestoreRecovery();
    CHECK(document.EditingScene()->FindEntity(entity.Id()).Name() == "Recovered name");
    CHECK(document.Dirty());
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(std::filesystem::exists(recovery));
    std::ifstream input(source, std::ios::binary);
    const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto bytes = std::as_bytes(std::span(characters.data(), characters.size()));
    CHECK(Keire::SceneAsset::Decode(bytes)->Definition().Objects.front().Name == "Recovered name");
    input.close();

    const auto renamed = source.parent_path() / "Renamed.keirescene";
    std::filesystem::rename(source, renamed);
    document.SetIdentity(asset, renamed);
    document.EditingScene()->FindEntity(entity.Id()).SetName("Saved after rename");
    document.Save();
    CHECK_FALSE(std::filesystem::exists(source));
    REQUIRE(std::filesystem::is_regular_file(renamed));
    std::ifstream renamedInput(renamed, std::ios::binary);
    const std::vector<char> renamedCharacters{std::istreambuf_iterator<char>(renamedInput),
                                              std::istreambuf_iterator<char>()};
    CHECK(Keire::SceneAsset::Decode(std::as_bytes(std::span(renamedCharacters)))->Definition().Objects.front().Name ==
          "Saved after rename");
    document.Close();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("play changes apply selected property and structural edits transactionally")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(CustomRegistration());
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000061");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Play changes"), registry);
    auto original = editing->CreateEntity("Original");
    auto custom = Keire::DynamicRefCast<CustomComponent>(original.AddComponent(CustomComponent::StaticType()));
    REQUIRE(custom);
    editing->MarkSaved();
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    auto runtimeEntity = session->RuntimeScene()->FindEntity(original.Id());
    runtimeEntity.SetName("Edited in Play");
    runtimeEntity.SetLayer(7);
    Keire::DynamicRefCast<CustomComponent>(runtimeEntity.GetComponent(CustomComponent::StaticType()))->Value = 4.0;
    const auto created = session->RuntimeScene()->CreateEntity("Created in Play");

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(),
                                            {original.Id().Value(), created.Id().Value()});
    CHECK_FALSE(changes.Empty());
    CHECK(changes.HasSelectedChanges());
    const auto applied = changes.BuildAppliedDefinition();
    auto restored = Keire::CreateRef<Keire::Scene>(asset, applied, registry);
    CHECK(restored->FindEntity(original.Id()).Name() == "Edited in Play");
    CHECK(restored->FindEntity(original.Id()).Layer() == 7);
    const auto restoredCustom = Keire::DynamicRefCast<CustomComponent>(
        restored->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()));
    REQUIRE(restoredCustom);
    CHECK(restoredCustom->Value == doctest::Approx(4.0));
    CHECK(restored->FindEntity(created.Id()));

    changes.SetAllSelected(false);
    const auto discarded = changes.BuildAppliedDefinition();
    CHECK(Keire::SceneAsset::Encode(discarded) == Keire::SceneAsset::Encode(editing->Snapshot()));
}

TEST_CASE("play change tracker distinguishes mixed values and enforces created-parent dependencies")
{
    const auto registry = Keire::ComponentRegistry::CreateDefault();
    registry->Register(CustomRegistration());
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000062");
    auto editing = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Tracked Play"), registry);
    auto original = editing->CreateEntity("Original");
    REQUIRE(original.AddComponent(CustomComponent::StaticType()));
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    KeireEditor::ScenePlayChangeTracker tracker;

    auto before = session->RuntimeScene()->Snapshot();
    auto runtimeCustom = Keire::DynamicRefCast<CustomComponent>(
        session->RuntimeScene()->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()));
    REQUIRE(runtimeCustom);
    runtimeCustom->Value = 4.0;
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());
    runtimeCustom->Value = 6.0;

    before = session->RuntimeScene()->Snapshot();
    auto parent = session->RuntimeScene()->CreateEntity("Created Parent");
    auto child = session->RuntimeScene()->CreateEntity("Created Child");
    child.SetParent(parent, false);
    tracker.RecordMutation(before, session->RuntimeScene()->Snapshot());

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(), tracker);
    const auto all = changes.Changes();
    const auto property =
        std::ranges::find_if(all,
                             [&](const auto& change)
                             {
                                 return change.Entity == original.Id().Value() &&
                                        change.Kind == KeireEditor::ScenePlayChangeKind::ComponentProperty;
                             });
    REQUIRE(property != all.end());
    CHECK(property->Origin == KeireEditor::ScenePlayChangeOrigin::Mixed);
    CHECK(property->Selected);
    auto parentChange = std::ranges::find_if(all,
                                             [&](const auto& change)
                                             {
                                                 return change.Entity == parent.Id().Value() &&
                                                        change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
                                             });
    const auto childChange = std::ranges::find_if(
        all,
        [&](const auto& change)
        {
            return change.Entity == child.Id().Value() && change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
        });
    REQUIRE(parentChange != all.end());
    REQUIRE(childChange != all.end());
    CHECK(parentChange->Locked);
    CHECK_FALSE(parentChange->LockReason.empty());

    changes.KeepCreatedEntityAtRoot(child.Id().Value(), true);
    parentChange = std::ranges::find_if(changes.Changes(),
                                        [&](const auto& change)
                                        {
                                            return change.Entity == parent.Id().Value() &&
                                                   change.Kind == KeireEditor::ScenePlayChangeKind::CreateEntity;
                                        });
    REQUIRE(parentChange != changes.Changes().end());
    changes.SetSelected(parentChange->Id, false);
    const auto applied = changes.BuildAppliedDefinition();
    auto restored = Keire::CreateRef<Keire::Scene>(asset, applied, registry);
    CHECK_FALSE(restored->FindEntity(parent.Id()));
    REQUIRE(restored->FindEntity(child.Id()));
    CHECK_FALSE(restored->FindEntity(child.Id()).Parent());
    CHECK(Keire::DynamicRefCast<CustomComponent>(
              restored->FindEntity(original.Id()).GetComponent(CustomComponent::StaticType()))
              ->Value == doctest::Approx(6.0));
}

TEST_CASE("play changes preserve selected unavailable component replacements")
{
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000063");
    auto definition = Keire::SceneAsset::EmptyDefinition("Unknown component");
    const auto entityId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000064");
    const auto unknownType = Keire::ComponentTypeId::Parse("ed170000-0000-4000-8000-000000000065");
    definition.Objects.push_back(
        {.Id = entityId,
         .Name = "Unknown",
         .Components = {{.Type = unknownType, .SchemaVersion = 7, .Enabled = true, .Data = "{\"value\":1}"}}});
    auto editing = Keire::CreateRef<Keire::Scene>(asset, definition);
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(editing);
    session->Play();
    KeireEditor::ScenePlayChangeTracker tracker;
    const auto before = session->RuntimeScene()->Snapshot();
    auto after = before;
    after.Objects.front().Components.back().Data = "{\"value\":2}";
    session->ReplaceRuntime(after);
    tracker.RecordMutation(before, after);

    KeireEditor::ScenePlayChangeSet changes(editing, session->RuntimeScene(), tracker);
    REQUIRE(changes.HasSelectedChanges());
    const auto applied = changes.BuildAppliedDefinition();
    const auto object = std::ranges::find(applied.Objects, entityId, &Keire::SceneObjectDefinition::Id);
    REQUIRE(object != applied.Objects.end());
    const auto component = std::ranges::find(object->Components, unknownType, &Keire::SceneComponentDefinition::Type);
    REQUIRE(component != object->Components.end());
    CHECK(component->SchemaVersion == 7);
    CHECK(component->Data == "{\"value\":2}");
}

TEST_CASE("input actions document owns authoring state and dirty lifecycle")
{
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Input document test"});
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-InputDocument-" + Keire::AssetId::Generate().ToString());
    const auto source = root / "Assets/Input/Test.keireinput";
    std::filesystem::create_directories(source.parent_path());
    KeireEditor::InputActionsDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000002"),
                  Keire::InputActionAsset::DefaultDefinition(), undo, source);
    const auto originalName = document.Definition().Name;
    auto edited = document.Definition();
    document.RecordApplied("Rename input actions", edited);
    edited.Name = "Edited actions";
    document.ReplaceDefinition(std::move(edited));
    CHECK(document.Dirty());
    CHECK(document.Definition().Name == "Edited actions");
    CHECK(document.Definition().ActionMaps.size() > 0);
    CHECK(document.Undo());
    CHECK(document.Definition().Name == originalName);
    CHECK(document.Redo());
    CHECK(document.Definition().Name == "Edited actions");
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(std::filesystem::is_regular_file(source));
    document.Close();
    CHECK_FALSE(document.Asset());
    CHECK(document.Definition().ActionMaps.empty());
    undo->Close();
    undoService->Close();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("scene document owns asynchronous operations and replacement lifecycle")
{
    KeireEditor::SceneDocument document;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000003");
    auto scene = Keire::CreateRef<Keire::Scene>(asset, Keire::SceneAsset::EmptyDefinition("Original"));
    const auto selected = scene->CreateEntity("Selected").Id().Value();
    document.Open(scene, asset, "Assets/Scenes/Original.keirescene");
    document.Select(selected);
    document.SetStatus("Opening");
    document.SetRecoveryPath("Library/SceneRecovery/original.recovery");
    document.AdvanceRecovery(2.5);
    CHECK(document.Status() == "Opening");
    CHECK(document.RecoverySeconds() == doctest::Approx(2.5));

    auto replacement = Keire::CreateRef<Keire::Scene>(asset, scene->Snapshot());
    document.ReplaceEditingScene(replacement);
    CHECK(document.EditingScene() == replacement);
    CHECK(document.Selection() == selected);
    CHECK(scene->IsOpen());

    document.BeginPlay();
    CHECK(document.PlaySession());
    CHECK(document.ActiveScene() != document.EditingScene());
    document.EndPlay();
    CHECK_FALSE(document.PlaySession());
    CHECK(document.ActiveScene() == replacement);
    document.Close();
}

TEST_CASE("project settings document validates saves and owns one-step edit history")
{
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-ProjectSettings-" + Keire::AssetId::Generate().ToString());
    std::filesystem::remove_all(root);
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Project Settings"});
    KeireEditor::ProjectSettingsDocument document;
    document.Open(root, {}, undo);

    auto edited = document.Settings();
    edited.AmbientIntensity = 2.5F;
    document.Update(edited);
    edited.Exposure = 1.5F;
    document.Update(edited);
    document.CommitEdit();
    CHECK(document.Dirty());
    CHECK(undo->UndoCount() == 1);
    CHECK(undo->Undo());
    CHECK(document.Settings().AmbientIntensity == doctest::Approx(0.75F));
    CHECK(undo->Redo());
    CHECK(document.Settings().Exposure == doctest::Approx(1.5F));

    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(Keire::LoadRenderEnvironmentSettings(root) == document.Settings());
    CHECK(Keire::LoadProjectAuthoringSettings(root) == document.AuthoringSettings());
    auto authoring = document.AuthoringSettings();
    authoring.DefaultMixer = Keire::AssetId::Parse("18000000-0000-4000-8000-000000000001");
    authoring.ExternalEditorId = "custom";
    authoring.ExternalEditorExecutable = "Tools/Editors/cursor";
    authoring.PhysicsLayerNames[7] = "Effects";
    authoring.PhysicsCollisionMatrix[1] &= ~(1U << 7U);
    authoring.PhysicsCollisionMatrix[7] &= ~(1U << 1U);
    document.UpdateAuthoring(authoring);
    document.CommitEdit("Edit Audio and Physics");
    CHECK(document.Dirty());
    CHECK(undo->Undo());
    CHECK_FALSE(document.AuthoringSettings().DefaultMixer);
    CHECK(undo->Redo());
    CHECK(document.AuthoringSettings().DefaultMixer == authoring.DefaultMixer);
    document.Save();
    CHECK(Keire::LoadProjectAuthoringSettings(root) == authoring);
    edited = document.Settings();
    edited.Exposure = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(document.Update(edited), std::invalid_argument);

    document.Close();
    undo->Close();
    undoService->Close();
    std::filesystem::remove_all(root);
}

TEST_CASE("external editor discovery is cross-platform stable and duplicate free")
{
    const auto profiles = KeireEditor::DiscoverExternalEditorProfiles();
    REQUIRE_FALSE(profiles.empty());
    CHECK(profiles.front().Id == "system");
    CHECK(profiles.front().SystemDefault);
    CHECK(profiles.front().Installed);
    for (auto first = profiles.begin(); first != profiles.end(); ++first)
        for (auto second = std::next(first); second != profiles.end(); ++second)
            CHECK(first->Id != second->Id);
    CHECK(std::ranges::any_of(profiles, [](const auto& profile) { return profile.Id == "vscode"; }));
    CHECK(std::ranges::any_of(profiles, [](const auto& profile) { return profile.Id == "neovim"; }));
}

TEST_CASE("custom registered component properties edit transactionally and restore")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    const auto components = Keire::ComponentRegistry::CreateDefault();
    components->Register(CustomRegistration());
    const auto registration = components->Find(CustomComponent::StaticType());
    REQUIRE(registration);
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000013"),
                                                Keire::SceneAsset::EmptyDefinition("Custom Inspector"), components);
    auto entity = scene->CreateEntity("Custom");
    auto component = entity.AddComponent(CustomComponent::StaticType());
    REQUIRE(component);
    const auto original = registration->Serialize(*component);
    unsigned int commits = 0;

    CHECK(drawers.EditComponent(editor, *registration, *component, registration->Properties.front(),
                                [&commits] { ++commits; }));
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(3.0));
    CHECK(commits == 1);

    registration->Deserialize(*component, original, registration->SchemaVersion);
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(1.0));

    editor.Scalar = 20.0;
    CHECK_THROWS_AS((void)drawers.EditComponent(editor, *registration, *component, registration->Properties.front()),
                    std::invalid_argument);
    CHECK(std::get<double>(registration->Serialize(*component).at("value")) == doctest::Approx(1.0));
}

TEST_CASE("component and property-specific drawers override generic kinds")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    auto registration = CustomRegistration();
    auto component = registration.Factory();
    drawers.RegisterOverride(
        registration.Type, "value",
        [](KeireEditor::IPropertyEditor&, const Keire::ComponentProperty&, Keire::ComponentPropertyValue& value)
        {
            value = 7.0;
            return true;
        });

    CHECK(drawers.EditComponent(editor, registration, *component, registration.Properties.front()));
    CHECK(std::get<double>(registration.Serialize(*component).at("value")) == doctest::Approx(7.0));
}

TEST_CASE("generic property drawers cover every component property kind")
{
    KeireEditor::PropertyDrawerRegistry drawers;
    TestPropertyEditor editor;
    const auto assetType = Keire::MeshAsset::StaticType();
    const std::vector<std::pair<Keire::ComponentProperty, Keire::ComponentPropertyValue>> properties = {
        {{"boolean", "Boolean", {}, Keire::ComponentPropertyKind::Boolean}, false},
        {{"integer", "Integer", {}, Keire::ComponentPropertyKind::Integer}, std::int64_t{1}},
        {{"scalar", "Scalar", {}, Keire::ComponentPropertyKind::Scalar}, 1.0},
        {{"text", "Text", {}, Keire::ComponentPropertyKind::Text}, std::string("original")},
        {{"vector2", "Vector2", {}, Keire::ComponentPropertyKind::Vector2}, Keire::Vector2{}},
        {{"vector3", "Vector3", {}, Keire::ComponentPropertyKind::Vector3}, Keire::Vector3{}},
        {{"vector4", "Vector4", {}, Keire::ComponentPropertyKind::Vector4}, Keire::Vector4{}},
        {{"quaternion", "Quaternion", {}, Keire::ComponentPropertyKind::Quaternion}, Keire::Quaternion{}},
        {{"color", "Color", {}, Keire::ComponentPropertyKind::Color}, Keire::Color{}},
        {{"asset", "Asset", {}, Keire::ComponentPropertyKind::Asset, false, {}, {}, 0.1, assetType}, Keire::AssetId{}},
        {{"entity", "Entity", {}, Keire::ComponentPropertyKind::Entity}, Keire::EntityId{}}};

    for (auto [property, value] : properties)
        CHECK(drawers.Draw(editor, CustomComponent::StaticType(), property, value));
    CHECK(editor.ExpectedAssetType == assetType);
}

TEST_CASE("VFX Emitter inspector edits exposed typed parameters and removes stale overrides")
{
    const auto id = [](const std::uint64_t value) { return Keire::AssetId(0x564658494e535045ULL, value); };
    Keire::VfxEffectDefinition effect;
    effect.Blackboard = {
        {id(1), "Enabled", Keire::VfxValueType::Boolean, false, true},
        {id(2), "Count", Keire::VfxValueType::Integer, std::int64_t{2}, true},
        {id(3), "Rate", Keire::VfxValueType::Scalar, 2.0F, true},
        {id(4), "Offset", Keire::VfxValueType::Vector2, Keire::Vector2{}, true},
        {id(5), "Direction", Keire::VfxValueType::Vector3, Keire::Vector3{}, true},
        {id(6), "Tint", Keire::VfxValueType::Color, Keire::Color{}, true},
        {id(7), "Texture", Keire::VfxValueType::Texture, Keire::AssetId{}, true},
        {id(8), "Mesh", Keire::VfxValueType::Mesh, Keire::AssetId{}, true},
        {id(9), "Asset", Keire::VfxValueType::Asset, Keire::AssetId{}, true},
        {id(10), "Internal", Keire::VfxValueType::Scalar, 1.0F, false},
    };
    std::vector<Keire::VfxParameterOverride> overrides{
        {id(99), 5.0F},
        {id(10), 4.0F},
        {id(3), 7.0F},
    };
    CHECK(KeireEditor::VfxEmitterInspector::VisibleEntryCount(effect, overrides) == 11);

    std::vector<Keire::AssetId> staleRemoved;
    std::vector<std::pair<std::string, bool>> statuses;
    KeireEditor::VfxEmitterInspectorCallbacks callbacks;
    callbacks.Status = [&statuses](const Keire::AssetId, const std::string_view message, const bool warning)
    { statuses.emplace_back(message, warning); };
    callbacks.Reset = [rate = id(3)](const Keire::AssetId parameter) { return parameter == rate; };
    callbacks.RemoveStale = [&staleRemoved](const Keire::AssetId parameter)
    {
        staleRemoved.push_back(parameter);
        return true;
    };

    TestPropertyEditor editor;
    REQUIRE(KeireEditor::VfxEmitterInspector{}.Draw(editor, effect, overrides, callbacks));
    const std::vector<std::string> expectedKinds{
        "boolean", "integer", "scalar", "vector2", "vector3", "color", "asset", "asset", "asset",
    };
    CHECK(editor.EditKinds == expectedKinds);
    REQUIRE(editor.ExpectedAssetTypes.size() == 3);
    CHECK(editor.ExpectedAssetTypes[0] == Keire::Texture2DAsset::StaticType());
    CHECK(editor.ExpectedAssetTypes[1] == Keire::MeshAsset::StaticType());
    CHECK_FALSE(editor.ExpectedAssetTypes[2].has_value());
    CHECK(std::ranges::none_of(overrides, [rate = id(3)](const auto& value) { return value.Parameter == rate; }));
    CHECK(std::ranges::none_of(overrides, [hidden = id(10)](const auto& value) { return value.Parameter == hidden; }));
    CHECK(
        std::ranges::none_of(overrides, [unknown = id(99)](const auto& value) { return value.Parameter == unknown; }));
    CHECK(std::ranges::is_sorted(overrides, {}, &Keire::VfxParameterOverride::Parameter));
    CHECK(staleRemoved == std::vector<Keire::AssetId>{id(10), id(99)});
    CHECK(std::ranges::count(statuses, true, &std::pair<std::string, bool>::second) == 2);
}

TEST_CASE("VFX Emitter inspector reports and removes an exposed type-mismatched override")
{
    class PassiveIntegerEditor final : public TestPropertyEditor
    {
      public:
        bool EditInteger(std::string_view, std::int64_t&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
    };

    const auto parameter = Keire::AssetId(0x564658494e535045ULL, 0x200);
    Keire::VfxEffectDefinition effect;
    effect.Blackboard = {{parameter, "Count", Keire::VfxValueType::Integer, std::int64_t{2}, true}};
    std::vector<Keire::VfxParameterOverride> overrides{{parameter, 5.0F}};
    std::size_t warnings = 0;
    KeireEditor::VfxEmitterInspectorCallbacks callbacks;
    callbacks.Status = [&warnings](const Keire::AssetId, const std::string_view, const bool warning)
    {
        if (warning)
            ++warnings;
    };
    callbacks.RemoveStale = [parameter](const Keire::AssetId value) { return value == parameter; };

    PassiveIntegerEditor editor;
    REQUIRE(KeireEditor::VfxEmitterInspector{}.Draw(editor, effect, overrides, callbacks));
    CHECK(overrides.empty());
    CHECK(warnings == 1);
}

TEST_CASE("VFX Emitter inspector values commit through the scene undo history")
{
    const auto parameter = Keire::AssetId(0x564658494e535045ULL, 0x100);
    Keire::VfxEffectDefinition effect;
    effect.Blackboard = {{parameter, "Rate", Keire::VfxValueType::Scalar, 2.0F, true}};

    const auto components = Keire::ComponentRegistry::CreateDefault();
    const auto registration = components->Find(Keire::VfxEmitterComponent::StaticType());
    REQUIRE(registration);
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId(0x564658494e535045ULL, 0x101),
                                                Keire::SceneAsset::EmptyDefinition("VFX Inspector Undo"), components);
    auto entity = scene->CreateEntity("Emitter");
    const auto component = entity.AddComponent(Keire::VfxEmitterComponent::StaticType());
    REQUIRE(component);

    std::vector<Keire::VfxParameterOverride> overrides;
    TestPropertyEditor editor;
    editor.Scalar = 6.0;
    REQUIRE(KeireEditor::VfxEmitterInspector{}.Draw(editor, effect, overrides, {}));
    const auto values = registration->Serialize(*component);
    const auto before = std::get<std::string>(values.at("parameterOverrides"));
    const auto after = KeireEditor::VfxEmitterInspector::SerializeOverrides(*registration, values, overrides);

    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto history = undoService->CreateContext({.Name = "VFX Inspector"});
    KeireEditor::SceneDocument document;
    document.Open(scene, scene->Asset(), {}, history);
    const auto apply = [&document, entityId = entity.Id()](const std::string& serialized)
    {
        document.SetComponentProperty(entityId, Keire::VfxEmitterComponent::StaticType(), "parameterOverrides",
                                      serialized);
    };
    history->RecordApplied(Keire::CreateUndoCommand(
        "Change Parameter Overrides", [apply, after] { apply(after); }, [apply, before] { apply(before); },
        before.size() + after.size()));
    apply(after);

    const auto currentEmitter = [&document, entityId = entity.Id()]
    { return document.ActiveScene()->FindEntity(entityId).GetComponent<Keire::VfxEmitterComponent>(); };
    REQUIRE(currentEmitter());
    REQUIRE(currentEmitter()->ParameterOverrides().size() == 1);
    CHECK(std::get<float>(currentEmitter()->ParameterOverrides().front().Value) == doctest::Approx(6.0F));
    CHECK(history->UndoCount() == 1);
    REQUIRE(history->Undo());
    CHECK(currentEmitter()->ParameterOverrides().empty());
    REQUIRE(history->Redo());
    REQUIRE(currentEmitter()->ParameterOverrides().size() == 1);
    CHECK(std::get<float>(currentEmitter()->ParameterOverrides().front().Value) == doctest::Approx(6.0F));

    document.Close();
    history->Close();
    undoService->Close();
}

TEST_CASE("material documents expose every shader texture property without hardcoded slots")
{
    const auto shader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000020");
    const auto baseColor = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000021");
    const auto normal = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000022");
    Keire::ShaderAssetDefinition shaderDefinition;
    shaderDefinition.Source = "Assets/Shaders/Material.hlsl";
    shaderDefinition.Properties = {{"Tint", Keire::ShaderPropertyType::Color, {1.0F, 1.0F, 1.0F, 1.0F}},
                                   {"Roughness", Keire::ShaderPropertyType::Scalar, {1.0F, 0.0F, 0.0F, 0.0F}},
                                   {"BaseColorTexture", Keire::ShaderPropertyType::Texture2D, {}, baseColor},
                                   {"NormalTexture", Keire::ShaderPropertyType::Texture2D},
                                   {"EmissiveTexture", Keire::ShaderPropertyType::Texture2D},
                                   {"MaskTexture", Keire::ShaderPropertyType::Texture2D}};
    shaderDefinition.Properties[2].TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    shaderDefinition.Properties[3].TextureSemantic = Keire::ShaderTextureSemantic::Normal;
    shaderDefinition.Properties[4].TextureSemantic = Keire::ShaderTextureSemantic::Emissive;
    shaderDefinition.Properties[5].TextureSemantic = Keire::ShaderTextureSemantic::MetallicRoughness;
    const auto resolveShader = [&](const Keire::AssetId id) -> std::optional<Keire::ShaderAssetDefinition>
    { return id == shader ? std::optional(shaderDefinition) : std::nullopt; };

    Keire::MaterialAssetDefinition sourceDefinition;
    sourceDefinition.Shader = shader;
    const auto source = Keire::MaterialAsset::EncodeSource(sourceDefinition);
    KeireEditor::MaterialDocument document;
    document.Open(source, resolveShader);
    REQUIRE(document.TextureProperties().size() == 4);
    CHECK(document.Texture("BaseColorTexture") == baseColor);
    CHECK(document.SetTexture("NormalTexture", normal));
    CHECK(document.LastChangedProperty() == "NormalTexture");
    CHECK_FALSE(document.SetTexture("NormalTexture", normal));
    CHECK_THROWS_AS((void)document.SetTexture("NotDeclared", normal), std::invalid_argument);
    CHECK(std::get<float>(document.Property("Roughness")) == doctest::Approx(1.0F));
    CHECK(document.SetProperty("Roughness", 0.35F));
    CHECK(document.LastChangedProperty() == "Roughness");
    CHECK_FALSE(document.SetProperty("Roughness", 0.35F));
    CHECK_THROWS_AS((void)document.SetProperty("Roughness", Keire::Color{}), std::invalid_argument);

    KeireEditor::MaterialDocument restored;
    restored.Open(document.SaveSource(), resolveShader);
    CHECK(restored.Texture("NormalTexture") == normal);
    CHECK(restored.Definition().Texture("NormalTexture") == normal);
    CHECK(std::get<float>(restored.Property("Roughness")) == doctest::Approx(0.35F));
    TestPropertyEditor editor;
    editor.Scalar = 0.6;
    CHECK(KeireEditor::MaterialInspectorPanel{}.Draw(editor, restored));
    CHECK(std::get<float>(restored.Property("Roughness")) == doctest::Approx(0.6F));
    CHECK(editor.ExpectedAssetType == Keire::Texture2DAsset::StaticType());
    CHECK(editor.TextureSemantics == std::vector<Keire::ShaderTextureSemantic>{
                                         Keire::ShaderTextureSemantic::BaseColor, Keire::ShaderTextureSemantic::Normal,
                                         Keire::ShaderTextureSemantic::Emissive,
                                         Keire::ShaderTextureSemantic::MetallicRoughness});

    Keire::AssetSourceRecord textureRecord;
    textureRecord.Type = Keire::Texture2DAsset::StaticType();
    textureRecord.ImportSettings = {{"semantic", std::string("color")}, {"colorSpace", std::string("srgb")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::BaseColor));
    CHECK_FALSE(
        KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Normal));
    textureRecord.ImportSettings = {{"semantic", std::string("normal")}, {"colorSpace", std::string("linear")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Normal));
    textureRecord.ImportSettings = {{"semantic", std::string("data")}, {"colorSpace", std::string("linear")}};
    CHECK(KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::Roughness));
    CHECK_FALSE(
        KeireEditor::MaterialInspectorPanel::AcceptsTexture(textureRecord, Keire::ShaderTextureSemantic::BaseColor));
}

TEST_CASE("material document owns draft and committed source state")
{
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000023");
    const auto shader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000024");
    Keire::ShaderAssetDefinition shaderDefinition;
    shaderDefinition.Source = "Assets/Shaders/Material.hlsl";
    shaderDefinition.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {1.0F, 0.0F, 0.0F, 0.0F}}};
    const auto resolver = [&](const Keire::AssetId id) -> std::optional<Keire::ShaderAssetDefinition>
    { return id == shader ? std::optional(shaderDefinition) : std::nullopt; };
    Keire::MaterialAssetDefinition definition;
    definition.Shader = shader;
    const auto source = Keire::MaterialAsset::EncodeSource(definition);

    KeireEditor::MaterialDocument document;
    document.OpenAsset(asset, "Assets/Materials/Test.keirematerial", source, resolver);
    CHECK(document.IsOpen(asset));
    CHECK_FALSE(document.Dirty());
    CHECK(document.SetProperty("Roughness", 0.25F));
    document.CaptureDraft();
    CHECK(document.Dirty());
    CHECK_FALSE(std::ranges::equal(document.DraftSource(), document.BaselineSource()));
    const std::vector<std::byte> committed(document.DraftSource().begin(), document.DraftSource().end());
    document.AcceptSavedSource(committed);
    CHECK_FALSE(document.Dirty());
    CHECK(std::ranges::equal(document.DraftSource(), document.BaselineSource()));

    document.RequestCatalogRefresh(asset);
    CHECK_FALSE(document.PendingCatalogRefresh());
    document.AdvanceCatalogRefresh(0.15);
    const auto firstRefresh = document.PendingCatalogRefresh();
    REQUIRE(firstRefresh);
    CHECK(firstRefresh->Asset == asset);
    CHECK(firstRefresh->Generation == 1);
    document.MarkCatalogRefreshQueued(firstRefresh->Generation);
    CHECK_FALSE(document.PendingCatalogRefresh(true));
    document.RequestCatalogRefresh(asset);
    document.RequestCatalogRefresh(Keire::AssetId::Generate());
    const auto coalesced = document.PendingCatalogRefresh(true);
    REQUIRE(coalesced);
    CHECK_FALSE(coalesced->Asset);
    CHECK(coalesced->Generation == 3);
    document.MarkCatalogRefreshQueued(coalesced->Generation);
    document.MarkCatalogRefreshApplied(coalesced->Generation);
    document.ResetCatalogRefresh();
    CHECK_FALSE(document.PendingCatalogRefresh(true));
}

TEST_CASE("material documents preserve Shader Graph references while publishing resolved runtime shaders")
{
    const auto graph = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000025");
    const auto runtimeShader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000026");
    Keire::ShaderAssetDefinition shaderDefinition;
    shaderDefinition.Source = "Assets/Generated/ShaderGraphs/Test.hlsl";
    shaderDefinition.Properties = {{"Roughness", Keire::ShaderPropertyType::Scalar, {0.5F, 0.0F, 0.0F, 0.0F}}};

    Keire::MaterialAuthoringDefinition sourceDefinition;
    sourceDefinition.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    sourceDefinition.Shader.Asset = graph;
    sourceDefinition.Shader.Keywords.emplace("USE_DETAIL", "true");
    sourceDefinition.Properties.emplace("Roughness", 0.25F);
    const auto source = Keire::MaterialAsset::EncodeAuthoringSource(sourceDefinition);
    const KeireEditor::MaterialDocument::ShaderReferenceResolver resolver =
        [&](const Keire::MaterialShaderReference& reference)
        -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
    {
        if (reference != sourceDefinition.Shader)
            return std::nullopt;
        return KeireEditor::MaterialDocument::ResolvedShader{runtimeShader, shaderDefinition};
    };

    KeireEditor::MaterialDocument document;
    document.Open(source, resolver);
    CHECK(document.Shader() == graph);
    CHECK(document.Definition().Shader == runtimeShader);
    CHECK(document.ShaderReference().Keywords == sourceDefinition.Shader.Keywords);
    CHECK(document.SetProperty("Roughness", 0.75F));

    const auto saved = Keire::MaterialAsset::DecodeAuthoringSource(document.SaveSource());
    CHECK(saved.Shader == sourceDefinition.Shader);
    CHECK(std::get<float>(saved.Properties.at("Roughness")) == doctest::Approx(0.75F));
}

TEST_CASE("Material Graph documents separate the surface canvas from compatibility template defaults")
{
    const auto graph = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000032");
    const auto runtimeShader = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000033");
    Keire::ShaderPropertyDefinition roughness;
    roughness.Id = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000034");
    roughness.Name = "Roughness";
    roughness.Type = Keire::ShaderPropertyType::Scalar;
    roughness.DefaultValue.X = 0.5F;
    Keire::ShaderInterfaceDefinition shaderInterface;
    shaderInterface.Properties.push_back(roughness);
    Keire::MaterialShaderReference shaderReference;
    shaderReference.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    shaderReference.Asset = graph;
    auto shaderTemplate = Keire::CreateDefaultShaderGraph();
    auto roughnessParameter =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughnessParameter.Id = roughness.Id;
    roughnessParameter.Name = "Roughness";
    roughnessParameter.Symbol = "Roughness";
    const auto parameterPin = roughnessParameter.Pins.front().Id;
    const auto masterRoughness =
        std::ranges::find(shaderTemplate.Nodes.front().Pins, "Roughness", &Keire::ShaderGraphPin::Name);
    REQUIRE(masterRoughness != shaderTemplate.Nodes.front().Pins.end());
    const auto masterRoughnessPin = masterRoughness->Id;
    const auto parameterNode = roughnessParameter.Id;
    shaderTemplate.Nodes.push_back(std::move(roughnessParameter));
    shaderTemplate.Connections.push_back({Keire::AssetId::Generate(),
                                          {parameterNode, parameterPin},
                                          {shaderTemplate.Nodes.front().Id, masterRoughnessPin}});
    auto definition = Keire::CreateMaterialGraph(shaderReference, shaderInterface);
    definition.SurfaceGraph = Keire::CreateMaterialSurfaceGraph(shaderTemplate);
    std::vector<std::byte> persisted;
    std::optional<Keire::MaterialAssetDefinition> preview;
    KeireEditor::MaterialGraphDocument document({
        .ResolveInterface = [&](const Keire::MaterialShaderReference& reference)
        { return reference == shaderReference ? std::optional(shaderInterface) : std::nullopt; },
        .ResolveTemplate = [&](const Keire::MaterialShaderReference& reference)
        { return reference == shaderReference ? std::optional(shaderTemplate) : std::nullopt; },
        .ResolveShader = [&](const Keire::MaterialShaderReference& reference)
        { return reference == shaderReference ? runtimeShader : Keire::AssetId{}; },
        .Preview = [&](const Keire::AssetId, const Keire::MaterialAssetDefinition& material) { preview = material; },
        .StopPreview = [](const Keire::AssetId) {},
        .Persist = [&](const Keire::AssetId, const std::span<const std::byte> bytes)
        { persisted.assign(bytes.begin(), bytes.end()); },
    });
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000035");
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Material Graph"});
    document.Open(asset, Keire::MaterialGraphAsset::EncodeSource(definition), 1, undo);
    const auto initial = document.BuildCanvasModel();
    REQUIRE(initial.Nodes.size() == 1);
    CHECK(initial.Nodes.front().Label == "Material Output");
    CHECK(std::ranges::any_of(initial.Nodes.front().Pins,
                              [](const KeireEditor::NodeGraphPin& pin) { return pin.Label == "Roughness"; }));
    const auto compatibility = document.BuildCanvasModel(true);
    REQUIRE(compatibility.Nodes.size() == 2);
    const auto defaults =
        std::ranges::find(compatibility.Nodes, "Template Defaults", &KeireEditor::NodeGraphNode::Label);
    REQUIRE(defaults != compatibility.Nodes.end());
    REQUIRE(defaults->Pins.size() == 1);
    CHECK(defaults->Pins.front().Label == "Roughness");

    auto value = Keire::CreateMaterialGraphValueNode(Keire::ShaderPropertyType::Scalar, 0.85F);
    const auto node = value.Id;
    const auto outputPin = value.OutputPin;
    CHECK(document.AddNode(std::move(value)));
    CHECK(document.AddConnection(
        {{}, {node, outputPin}, {document.Definition().OutputNode, document.Definition().Properties.front().Pin}}));
    REQUIRE(preview);
    CHECK(preview->Shader == runtimeShader);
    CHECK(std::get<float>(preview->Properties.at("Roughness")) == doctest::Approx(0.85F));
    CHECK(document.Dirty());
    CHECK(document.Undo());
    CHECK(document.Definition().Connections.empty());
    CHECK(document.Redo());
    CHECK(document.Definition().Connections.size() == 1);
    document.Save();
    CHECK_FALSE(persisted.empty());
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("scene picker selects transform-only and rendered entities by nearest viewport hit")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000030"),
                                                Keire::SceneAsset::EmptyDefinition("Picking"));
    auto transformOnly = scene->CreateEntity("Transform only");
    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};

    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == transformOnly.Id());
    CHECK_FALSE(KeireEditor::PickSceneEntity(scene, viewport, {250.0F, 100.0F}, camera));

    auto rendered = scene->CreateEntity("Rendered");
    REQUIRE(rendered.AddComponent<Keire::MeshRendererComponent>());
    rendered.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, 2.0F});
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == rendered.Id());
    rendered.SetActive(false);
    CHECK(KeireEditor::PickSceneEntity(scene, viewport, {100.0F, 100.0F}, camera) == transformOnly.Id());
}

TEST_CASE("scene material drops use imported bounds and expand model roots to rendered descendants")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000033"),
                                                Keire::SceneAsset::EmptyDefinition("Material drop targets"));
    auto root = scene->CreateEntity("Model root");
    auto rendered = scene->CreateEntity("Offset mesh");
    rendered.SetParent(root, false);
    auto renderer = rendered.AddComponent<Keire::MeshRendererComponent>();
    REQUIRE(renderer);
    const auto mesh = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000034");
    renderer->SetMesh(mesh);
    auto nested = scene->CreateEntity("Nested mesh");
    nested.SetParent(root, false);
    REQUIRE(nested.AddComponent<Keire::MeshRendererComponent>());

    Keire::RenderCamera camera;
    camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
    camera.Projection = Keire::Math::Perspective(60.0F, 1.0F, 0.1F, 100.0F);
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {200.0F, 200.0F}};
    CHECK_FALSE(KeireEditor::PickSceneEntity(scene, viewport, {145.0F, 100.0F}, camera));
    const auto picked =
        KeireEditor::PickSceneEntity(scene, viewport, {145.0F, 100.0F}, camera,
                                     [mesh](const Keire::AssetId requested) -> std::optional<Keire::MeshBounds>
                                     {
                                         if (requested == mesh)
                                             return Keire::MeshBounds{{-4.0F, -0.5F, -0.5F}, {4.0F, 0.5F, 0.5F}};
                                         return std::nullopt;
                                     });
    CHECK(picked == rendered.Id());

    auto targets = KeireEditor::ResolveMaterialDropTargets(root);
    std::ranges::sort(targets);
    std::vector expected{rendered.Id(), nested.Id()};
    std::ranges::sort(expected);
    CHECK(targets == expected);
    CHECK(KeireEditor::ResolveMaterialDropTargets(rendered) == std::vector{rendered.Id()});
}

TEST_CASE("scene framing bounds use imported mesh metadata and transformed descendants")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000031"),
                                                Keire::SceneAsset::EmptyDefinition("Framing"));
    auto parent = scene->CreateEntity("Imported mesh");
    auto renderer = parent.AddComponent<Keire::MeshRendererComponent>();
    REQUIRE(renderer);
    const auto mesh = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000032");
    renderer->SetMesh(mesh);
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({10.0F, 2.0F, -3.0F});

    auto child = scene->CreateEntity("Child");
    child.SetParent(parent, false);
    child.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 8.0F, 0.0F});

    const auto bounds = KeireEditor::CalculateSceneEntityBounds(
        parent,
        [mesh](const Keire::AssetId requested) -> std::optional<Keire::MeshBounds>
        {
            if (requested == mesh)
                return Keire::MeshBounds{{-4.0F, -2.0F, -1.0F}, {4.0F, 2.0F, 1.0F}};
            return std::nullopt;
        });
    REQUIRE(bounds.Valid);
    CHECK(bounds.Minimum == (Keire::Vector3{6.0F, 0.0F, -4.0F}));
    CHECK(bounds.Maximum == (Keire::Vector3{14.0F, 10.15F, -2.0F}));
    CHECK(bounds.Center() == (Keire::Vector3{10.0F, 5.075F, -3.0F}));
    CHECK(bounds.Radius() > 6.5F);

    auto second = scene->CreateEntity("Second selection");
    second.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-12.0F, 0.0F, 0.0F});
    const std::array group{parent, second};
    const auto groupBounds = KeireEditor::CalculateSceneEntityBounds(group, {});
    REQUIRE(groupBounds.Valid);
    CHECK(groupBounds.Minimum.X < -12.0F);
    CHECK(groupBounds.Maximum.X >= 10.15F);
    second.SetActive(false);
    const auto activeBounds = KeireEditor::CalculateSceneEntityBounds(group, {});
    CHECK(activeBounds.Minimum.X >= 9.5F);

    KeireEditor::SceneCameraController camera;
    camera.Frame(bounds.Center(), bounds.Radius(), 1.0F);
    const auto viewProjection = Keire::Math::Multiply(camera.ProjectionMatrix(1.0F), camera.ViewMatrix());
    for (int corner = 0; corner < 8; ++corner)
    {
        const Keire::Vector3 point{corner & 1 ? bounds.Maximum.X : bounds.Minimum.X,
                                   corner & 2 ? bounds.Maximum.Y : bounds.Minimum.Y,
                                   corner & 4 ? bounds.Maximum.Z : bounds.Minimum.Z};
        const auto& elements = viewProjection.Elements;
        const float clipX = elements[0] * point.X + elements[4] * point.Y + elements[8] * point.Z + elements[12];
        const float clipY = elements[1] * point.X + elements[5] * point.Y + elements[9] * point.Z + elements[13];
        const float clipW = elements[3] * point.X + elements[7] * point.Y + elements[11] * point.Z + elements[15];
        REQUIRE(clipW > 0.0F);
        CHECK(std::abs(clipX / clipW) <= 0.8F);
        CHECK(std::abs(clipY / clipW) <= 0.8F);
    }
}

TEST_CASE("viewport asset drops dispatch through narrow typed commands")
{
    class Commands final : public KeireEditor::IViewportAssetDropCommands
    {
      public:
        void OpenDroppedScene(const Keire::AssetId asset) override { Scene = asset; }
        void OpenDroppedInputActions(const Keire::AssetId asset) override { Input = asset; }
        void InstantiateDroppedPrefab(const Keire::AssetId asset) override { Prefab = asset; }
        void CreateDroppedMeshEntity(const Keire::AssetId asset) override { Mesh = asset; }
        void AssignDroppedMaterial(const Keire::EntityId entity, const Keire::AssetId asset) override
        {
            Target = entity;
            Material = asset;
        }

        Keire::AssetId Scene;
        Keire::AssetId Input;
        Keire::AssetId Prefab;
        Keire::AssetId Mesh;
        Keire::AssetId Material;
        Keire::EntityId Target;
    } commands;
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000040");
    const auto target = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000041");
    KeireEditor::ViewportAssetDropRouter router;
    router.Route(Keire::SceneAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Scene == asset);
    router.Route(Keire::InputActionAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Input == asset);
    router.Route(Keire::PrefabAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Prefab == asset);
    router.Route(Keire::MeshAsset::StaticType(), asset, {}, commands);
    CHECK(commands.Mesh == asset);
    router.Route(Keire::MaterialAsset::StaticType(), asset, target, commands);
    CHECK(commands.Material == asset);
    CHECK(commands.Target == target);
    commands.Material = {};
    router.Route(Keire::MaterialGraphAsset::StaticType(), asset, target, commands);
    CHECK(commands.Material == asset);
    CHECK(commands.Target == target);
    commands.Material = {};
    router.Route(Keire::MaterialInstanceAsset::StaticType(), asset, target, commands);
    CHECK(commands.Material == asset);
    CHECK(commands.Target == target);
    commands.Material = {};
    router.Route(Keire::ShaderGraphInstanceAsset::StaticType(), asset, target, commands);
    CHECK(commands.Material == asset);
    CHECK(commands.Target == target);
    CHECK_THROWS_AS(router.Route(Keire::MaterialAsset::StaticType(), asset, {}, commands), std::invalid_argument);
    CHECK_THROWS_AS(router.Route(Keire::MaterialGraphAsset::StaticType(), asset, {}, commands), std::invalid_argument);
    CHECK_THROWS_AS(router.Route(Keire::MaterialInstanceAsset::StaticType(), asset, {}, commands),
                    std::invalid_argument);
    CHECK_THROWS_AS(router.Route(Keire::ShaderGraphAsset::StaticType(), asset, target, commands),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        router.Route(Keire::AssetTypeId::Parse("ed170000-0000-4000-8000-000000000042"), asset, {}, commands),
        std::invalid_argument);
}

TEST_CASE("scene camera state and entity locking persist without a workspace layer")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-SceneCameraController-Test";
    const auto path = root / "SceneCamera.state";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    KeireEditor::SceneCameraController camera;
    auto state = camera.State();
    state.Focus = {4.0F, 5.0F, 6.0F};
    state.YawDegrees = 35.0F;
    state.PitchDegrees = -15.0F;
    state.Distance = 12.0F;
    state.OrthographicSize = 7.0F;
    state.MoveSpeed = 3.0F;
    state.Projection = Keire::Detail::EditorCameraProjection::Orthographic;
    camera.SetState(state);
    const std::array locked{Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000050"),
                            Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000053")};
    camera.SetLockedEntities(locked);
    camera.MarkDirty();
    REQUIRE(camera.Save(path));

    KeireEditor::SceneCameraController restored;
    REQUIRE(restored.Load(path));
    CHECK(restored.State().Focus == state.Focus);
    CHECK(restored.State().YawDegrees == doctest::Approx(state.YawDegrees));
    CHECK(restored.State().Projection == Keire::Detail::EditorCameraProjection::Orthographic);
    CHECK(std::ranges::equal(restored.LockedEntities(), locked));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("scene and game viewports keep camera and input ownership separate during play")
{
    KeireEditor::SceneCameraController camera;
    auto state = camera.State();
    state.Focus = {3.0F, 4.0F, 5.0F};
    state.YawDegrees = 25.0F;
    state.PitchDegrees = -10.0F;
    camera.SetState(state);

    constexpr float aspect = 16.0F / 9.0F;
    const auto renderCamera = camera.RenderCamera(aspect);
    const auto expectedView = camera.ViewMatrix();
    const auto expectedProjection = camera.ProjectionMatrix(aspect);
    for (std::size_t index = 0; index < renderCamera.View.Elements.size(); ++index)
    {
        CHECK(renderCamera.View.Elements[index] == doctest::Approx(expectedView.Elements[index]));
        CHECK(renderCamera.Projection.Elements[index] == doctest::Approx(expectedProjection.Elements[index]));
    }

    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, true, false, false, false));
    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, false, true, false, false));
    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, false, false, false, true, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(false, true, true, true, true, true, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, false, true, true, true, true, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, true, true, true, true));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, false, true, false, false, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, false, false, false, false));
}

TEST_CASE("scene camera single F frames and double F locks the selected entity")
{
    KeireEditor::SceneCameraController camera;
    const auto first = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000051");
    const auto second = Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000052");

    CHECK(camera.ApplyFocusShortcut(first, Keire::TimeStep::FromSeconds(1.0)) ==
          KeireEditor::SceneFocusShortcutAction::Frame);
    CHECK(camera.ApplyFocusShortcut(first, Keire::TimeStep::FromSeconds(1.2)) ==
          KeireEditor::SceneFocusShortcutAction::Lock);
    CHECK(camera.LockedEntity() == first);
    CHECK(camera.ApplyFocusShortcut(second, Keire::TimeStep::FromSeconds(2.0)) ==
          KeireEditor::SceneFocusShortcutAction::Frame);
    CHECK(camera.ApplyFocusShortcut(second, Keire::TimeStep::FromSeconds(2.5)) ==
          KeireEditor::SceneFocusShortcutAction::Frame);
    CHECK(camera.LockedEntity() == first);
    CHECK(camera.ApplyFocusShortcut(Keire::EntityId{}, Keire::TimeStep::FromSeconds(2.6)) ==
          KeireEditor::SceneFocusShortcutAction::None);
}

TEST_CASE("scene camera double F preserves an ordered multiselection lock and follows framing smoothly")
{
    KeireEditor::SceneCameraController camera;
    const std::array selection{Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000054"),
                               Keire::EntityId::Parse("ed170000-0000-4000-8000-000000000055")};
    CHECK(camera.ApplyFocusShortcut(selection, Keire::TimeStep::FromSeconds(1.0)) ==
          KeireEditor::SceneFocusShortcutAction::Frame);
    CHECK(camera.ApplyFocusShortcut(selection, Keire::TimeStep::FromSeconds(1.2)) ==
          KeireEditor::SceneFocusShortcutAction::Lock);
    CHECK(camera.LockedTo(selection));
    const auto before = camera.State();
    camera.FollowFrame({20.0F, 0.0F, 0.0F}, 4.0F, 16.0F / 9.0F, 1.0F / 60.0F);
    CHECK(camera.State().Focus.X > before.Focus.X);
    CHECK(camera.State().Focus.X < 20.0F);
}

TEST_CASE("editor window placement persists windowed bounds and display state")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-EditorWindowPlacement-Test";
    const auto path = root / "editor-window.state";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    KeireEditor::EditorWindowPlacement placement;
    placement.Position = {-1440, 90};
    placement.WindowedSize = {1560, 940};
    placement.Mode = Keire::WindowMode::BorderlessFullscreen;
    REQUIRE(KeireEditor::SaveEditorWindowPlacement(path, placement));
    const auto restored = KeireEditor::LoadEditorWindowPlacement(path);
    REQUIRE(restored);
    CHECK(restored->Position == placement.Position);
    CHECK(restored->WindowedSize == placement.WindowedSize);
    CHECK(restored->Mode == placement.Mode);

    Keire::WindowSpecification specification;
    KeireEditor::PrepareEditorWindow(*restored, specification);
    CHECK(specification.Width == placement.WindowedSize.Width);
    CHECK(specification.Height == placement.WindowedSize.Height);
    CHECK_FALSE(specification.Visible);
    CHECK_FALSE(specification.Maximized);
    CHECK(specification.Mode == Keire::WindowMode::Windowed);

    std::ofstream(path, std::ios::trunc) << "KEIRE_EDITOR_WINDOW 1\n0 0 10 10\n0 0\n";
    CHECK_FALSE(KeireEditor::LoadEditorWindowPlacement(path));
    std::filesystem::remove_all(root, error);
}

TEST_CASE("editor window restoration selects visible displays and clamps removed-monitor bounds")
{
    const std::array displays{
        Keire::DisplayInformation{0, "Left", {-1920, 0, 1920, 1080}, {-1920, 0, 1920, 1040}, 1.0F, false},
        Keire::DisplayInformation{1, "Primary", {0, 0, 2560, 1440}, {0, 0, 2560, 1400}, 1.5F, true}};
    KeireEditor::EditorWindowPlacement visible;
    visible.Position = {-1500, 80};
    visible.WindowedSize = {1200, 800};
    const auto kept = KeireEditor::CorrectEditorWindowPlacement(visible, displays);
    CHECK(kept.Position == visible.Position);
    CHECK(kept.WindowedSize == visible.WindowedSize);

    KeireEditor::EditorWindowPlacement removed;
    removed.Position = {5000, -3000};
    removed.WindowedSize = {4000, 2000};
    const auto corrected = KeireEditor::CorrectEditorWindowPlacement(removed, displays);
    CHECK(corrected.WindowedSize == Keire::LogicalExtent{2560, 1400});
    CHECK(corrected.Position == Keire::WindowPosition{0, 0});

    const std::array smallDisplay{
        Keire::DisplayInformation{0, "Small", {0, 0, 800, 600}, {0, 0, 800, 560}, 2.0F, true}};
    const auto small = KeireEditor::CorrectEditorWindowPlacement(removed, smallDisplay);
    CHECK(small.WindowedSize == Keire::LogicalExtent{800, 560});
    CHECK(small.Position == Keire::WindowPosition{0, 0});
}

TEST_CASE("content previews use immutable loaded assets without blocking shutdown")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-ThumbnailService-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    KeireEditor::ThumbnailService thumbnails(root);
    const auto await = [&thumbnails]
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            auto completed = thumbnails.DrainCompleted();
            if (!completed.empty())
                return std::move(completed.front());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return KeireEditor::ThumbnailResult{};
    };

    Keire::TextureImportSettings textureSettings;
    textureSettings.Mips = Keire::TextureMipPolicy::None;
    Keire::TextureMipLevel mip;
    mip.Width = 2;
    mip.Height = 1;
    mip.Pixels = {std::byte{255}, std::byte{0},   std::byte{0}, std::byte{255},
                  std::byte{0},   std::byte{255}, std::byte{0}, std::byte{0}};
    auto texture = Keire::CreateRef<Keire::Texture2DAsset>(textureSettings, std::vector{mip});
    const auto textureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000070");
    REQUIRE(thumbnails.Request({.Asset = textureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Preview.png",
                                .Digest = "texture-preview"}));
    const auto textureResult = await();
    REQUIRE(textureResult.Pixels.size() == 96U * 96U * 4U);
    const auto redOffset = (48U * 96U + 24U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[redOffset]) >
          std::to_integer<unsigned>(textureResult.Pixels[redOffset + 1]));
    const auto alphaOffset = (48U * 96U + 72U) * 4U;
    CHECK(std::to_integer<unsigned>(textureResult.Pixels[alphaOffset]) ==
          std::to_integer<unsigned>(textureResult.Pixels[alphaOffset + 1]));

    const auto recoveredTextureId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000073");
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview",
                                .Missing = true}));
    REQUIRE(await().Pixels.size() == 96U * 96U * 4U);
    REQUIRE(thumbnails.Request({.Asset = recoveredTextureId,
                                .Type = Keire::Texture2DAsset::StaticType(),
                                .PreviewAsset = texture,
                                .RelativePath = "Recovered.png",
                                .Digest = "recovered-texture-preview"}));
    const auto recoveredTexture = await();
    REQUIRE(recoveredTexture.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset]) >
          std::to_integer<unsigned>(recoveredTexture.Pixels[redOffset + 1]));

    Keire::MaterialAssetDefinition materialDefinition;
    materialDefinition.Properties.emplace("Tint", Keire::Color{1.0F, 0.05F, 0.05F, 1.0F});
    auto material = Keire::CreateRef<Keire::MaterialAsset>(materialDefinition);
    const auto materialId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000071");
    REQUIRE(thumbnails.Request({.Asset = materialId,
                                .Type = Keire::MaterialAsset::StaticType(),
                                .PreviewAsset = material,
                                .RelativePath = "Preview.keirematerial",
                                .Digest = "material-preview"}));
    const auto materialResult = await();
    REQUIRE(materialResult.Pixels.size() == 96U * 96U * 4U);
    const auto center = (48U * 96U + 48U) * 4U;
    CHECK(std::to_integer<unsigned>(materialResult.Pixels[center]) >
          std::to_integer<unsigned>(materialResult.Pixels[center + 1]));

    const auto materialGraphId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000075");
    REQUIRE(thumbnails.Request({.Asset = materialGraphId,
                                .Type = Keire::ShaderGraphAsset::StaticType(),
                                .PreviewAsset = material,
                                .RelativePath = "Preview.keireshadergraph",
                                .Digest = "material-graph-preview"}));
    const auto materialGraphResult = await();
    REQUIRE(materialGraphResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[center]) >
          std::to_integer<unsigned>(materialGraphResult.Pixels[center + 1]));
    const auto badgeBorder = (79U * 96U + 71U) * 4U;
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[badgeBorder]) > 200U);
    CHECK(std::to_integer<unsigned>(materialGraphResult.Pixels[badgeBorder + 1]) > 150U);

    Keire::MaterialAssetDefinition generatedMaterialDefinition;
    generatedMaterialDefinition.Properties.emplace("MG_SurfaceTint", Keire::Color{0.05F, 0.8F, 0.15F, 1.0F});
    auto generatedMaterial = Keire::CreateRef<Keire::MaterialAsset>(std::move(generatedMaterialDefinition));
    Keire::ShaderAssetDefinition generatedShaderDefinition;
    Keire::ShaderPropertyDefinition surfaceTint;
    surfaceTint.Name = "MG_SurfaceTint";
    surfaceTint.Type = Keire::ShaderPropertyType::Color;
    surfaceTint.Category = "Surface";
    generatedShaderDefinition.Properties.push_back(std::move(surfaceTint));
    auto generatedShader = Keire::CreateRef<Keire::ShaderAsset>(std::move(generatedShaderDefinition));
    const auto generatedMaterialGraphId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000077");
    REQUIRE(thumbnails.Request({.Asset = generatedMaterialGraphId,
                                .Type = Keire::MaterialGraphAsset::StaticType(),
                                .PreviewAsset = generatedMaterial,
                                .PreviewShader = generatedShader,
                                .RelativePath = "Generated.keirematerialgraph",
                                .Digest = "generated-material-graph-preview"}));
    const auto generatedMaterialGraphResult = await();
    REQUIRE(generatedMaterialGraphResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 1]) >
          std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center]));
    CHECK(std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 1]) >
          std::to_integer<unsigned>(generatedMaterialGraphResult.Pixels[center + 2]));

    const auto vfxId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000076");
    REQUIRE(thumbnails.Request({.Asset = vfxId,
                                .Type = Keire::VfxEffectAsset::StaticType(),
                                .PreviewAsset = Keire::VfxEffectAsset::Default(),
                                .RelativePath = "Preview.keirevfx",
                                .Digest = "vfx-preview"}));
    const auto vfxResult = await();
    REQUIRE(vfxResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::to_integer<unsigned>(vfxResult.Pixels[badgeBorder]) > 200U);

    const auto materialGraphFallback =
        KeireEditor::MakeAssetFallbackThumbnail(Keire::ShaderGraphAsset::StaticType(), 96, 96);
    const auto materialInstanceFallback =
        KeireEditor::MakeAssetFallbackThumbnail(Keire::MaterialInstanceAsset::StaticType(), 96, 96);
    const auto vfxFallback = KeireEditor::MakeAssetFallbackThumbnail(Keire::VfxEffectAsset::StaticType(), 96, 96);
    const auto mixerFallback = KeireEditor::MakeAssetFallbackThumbnail(Keire::AudioMixerAsset::StaticType(), 96, 96);
    CHECK(materialGraphFallback != materialInstanceFallback);
    CHECK(materialGraphFallback != vfxFallback);
    CHECK(materialInstanceFallback != vfxFallback);
    CHECK(mixerFallback != materialGraphFallback);
    CHECK(mixerFallback != vfxFallback);

    const auto meshId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000072");
    REQUIRE(thumbnails.Request({.Asset = meshId,
                                .Type = Keire::MeshAsset::StaticType(),
                                .PreviewAsset = Keire::MeshAsset::Cube(),
                                .RelativePath = "Preview.obj",
                                .Digest = "mesh-preview"}));
    const auto meshResult = await();
    REQUIRE(meshResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::ranges::any_of(meshResult.Pixels,
                              [](const std::byte value) { return std::to_integer<unsigned>(value) > 210U; }));

    const auto prefabId = Keire::AssetId::Parse("00000000-0000-0000-0000-000000000074");
    KeireEditor::ThumbnailRequest prefabRequest{
        .Asset = prefabId,
        .Type = Keire::PrefabAsset::StaticType(),
        .RelativePath = "Preview.keireprefab",
        .Digest = "prefab-preview",
    };
    prefabRequest.PreviewMeshes.push_back(
        KeireEditor::ThumbnailMeshInstance{.Mesh = Keire::MeshAsset::Cube(), .Transform = Keire::Matrix4{}});

    REQUIRE(thumbnails.Request(std::move(prefabRequest)));
    const auto prefabResult = await();
    REQUIRE(prefabResult.Pixels.size() == 96U * 96U * 4U);
    CHECK(std::ranges::any_of(prefabResult.Pixels,
                              [](const std::byte value) { return std::to_integer<unsigned>(value) > 180U; }));

    thumbnails.CancelAll();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("Asset operation service runs the isolated worker and publishes a source index")
{
    const auto location = std::filesystem::temp_directory_path() / std::filesystem::path(u8"Kéire-资产-Worker-Test");
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    const auto worker = KeireEditor::AssetOperationService::ResolveWorkerExecutable(KeireEditorTests::ExecutablePath);
    REQUIRE(std::filesystem::is_regular_file(worker));
    {
        auto project = Keire::Project::Create(
            {.Location = location, .Name = "Worker Project", .Template = Keire::ProjectTemplate::Empty});
        REQUIRE(project);
        const auto interrupted = project->Root() / "Library/AssetOperations" / Keire::AssetId::Generate().ToString();
        std::filesystem::create_directories(interrupted);
        std::filesystem::create_directories(project->Root() / "Assets/Shaders");
        Keire::Detail::WriteTextFileAtomically(project->Root() / "Assets/Shaders/StaleAuxiliary.hlsl", "stale");
        Keire::Detail::WriteTextFileAtomically(interrupted / "create-auxiliary.journal",
                                               "Scenes/Missing.keirescene\nShaders/StaleAuxiliary.hlsl\n");
        KeireEditor::AssetOperationService operations(worker, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        CHECK_FALSE(operations.Busy());
        const auto completion = operations.TakeCompletion();
        REQUIRE(completion);
        INFO(completion->Result.Diagnostic);
        CHECK(completion->Result.Success);
        CHECK(std::filesystem::is_regular_file(completion->SourceIndexPath));
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Shaders/StaleAuxiliary.hlsl"));
        CHECK_FALSE(std::filesystem::exists(interrupted / "create-auxiliary.journal"));

        const std::string auxiliaryText = "worker auxiliary source";
        const auto auxiliaryBytes = std::as_bytes(std::span(auxiliaryText));
        operations.QueueCreateAssetWithAuxiliary(
            "Scenes/WorkerCreated.keirescene",
            Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Worker Created")), {}, {},
            {{"Shaders/WorkerAuxiliary.hlsl", std::vector<std::byte>(auxiliaryBytes.begin(), auxiliaryBytes.end())}});
        const auto createDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < createDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto created = operations.TakeCompletion();
        REQUIRE(created);
        INFO(created->Result.Diagnostic);
        CHECK(created->Result.Success);
        CHECK(created->Kind == Keire::Detail::AssetWorkerOperationKind::CreateAsset);
        CHECK(created->Result.CreatedAsset);
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerCreated.keirescene"));
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Shaders/WorkerAuxiliary.hlsl"));

        operations.QueueMutation({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                  .Asset = created->Result.CreatedAsset,
                                  .Destination = "Scenes/WorkerRenamed.keirescene"});
        const auto mutationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < mutationDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto mutated = operations.TakeCompletion();
        REQUIRE(mutated);
        INFO(mutated->Result.Diagnostic);
        CHECK(mutated->Result.Success);
        CHECK(mutated->Kind == Keire::Detail::AssetWorkerOperationKind::Mutate);
        CHECK(mutated->Result.MutatedAssets == std::vector{created->Result.CreatedAsset});
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/WorkerCreated.keirescene"));
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));

        operations.QueueMutation(
            {.Kind = Keire::Detail::AssetWorkerMutationKind::TrashAsset, .Asset = created->Result.CreatedAsset});
        const auto trashDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < trashDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto trashed = operations.TakeCompletion();
        REQUIRE(trashed);
        INFO(trashed->Result.Diagnostic);
        REQUIRE(trashed->Result.Success);
        REQUIRE(trashed->Result.Trash);
        CHECK_FALSE(std::filesystem::exists(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));

        operations.QueueMutation(
            {.Kind = Keire::Detail::AssetWorkerMutationKind::RestoreTrash, .Trash = trashed->Result.Trash});
        const auto restoreDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (operations.Busy() && std::chrono::steady_clock::now() < restoreDeadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto restored = operations.TakeCompletion();
        REQUIRE(restored);
        INFO(restored->Result.Diagnostic);
        CHECK(restored->Result.Success);
        CHECK(std::filesystem::is_regular_file(project->Root() / "Assets/Scenes/WorkerRenamed.keirescene"));
    }
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Asset operation service reports malformed worker completion and bounds forced shutdown")
{
    const auto location =
        std::filesystem::temp_directory_path() / ("Keire-Worker-Failure-" + Keire::AssetId::Generate().ToString());
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    auto project = Keire::Project::Create(
        {.Location = location, .Name = "Worker Failure Project", .Template = Keire::ProjectTemplate::Empty});

    SetTestWorkerMode("malformed");
    {
        KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        operations.Update();
        SetTestWorkerMode(nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (operations.Busy() && std::chrono::steady_clock::now() < deadline)
        {
            operations.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        operations.Update();
        const auto completion = operations.TakeCompletion();
        REQUIRE(completion);
        CHECK_FALSE(completion->Result.Success);
        CHECK_FALSE(completion->Result.Diagnostic.empty());
    }

    SetTestWorkerMode("hang");
    {
        KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
        operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
        operations.Update();
        SetTestWorkerMode(nullptr);
        REQUIRE(operations.Busy());
        const auto started = std::chrono::steady_clock::now();
        operations.Shutdown();
        const auto shutdownDuration = std::chrono::steady_clock::now() - started;
        CAPTURE(std::chrono::duration_cast<std::chrono::milliseconds>(shutdownDuration).count());
        CHECK(shutdownDuration < std::chrono::seconds(2));
        CHECK_FALSE(operations.Busy());
    }
    SetTestWorkerMode(nullptr);
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Asset operation service coalesces material refresh generations before dispatch")
{
    const auto location =
        std::filesystem::temp_directory_path() / ("Keire-Worker-Queue-" + Keire::AssetId::Generate().ToString());
    std::error_code cleanupError;
    std::filesystem::remove_all(location, cleanupError);
    std::filesystem::create_directories(location);
    auto project = Keire::Project::Create(
        {.Location = location, .Name = "Worker Queue Project", .Template = Keire::ProjectTemplate::Empty});
    KeireEditor::AssetOperationService operations(KeireEditorTests::ExecutablePath, project->Root());
    operations.QueueImport(KeireEditor::AssetOperationPriority::MaterialRefresh,
                           {.ReloadAsset = Keire::AssetId::Generate(), .Generation = 1});
    operations.QueueImport(KeireEditor::AssetOperationPriority::MaterialRefresh,
                           {.ReloadAsset = Keire::AssetId::Generate(), .Generation = 2});
    operations.QueueCook({.Name = "Test"}, project->Root() / "Build/Cooked");
    operations.QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction);
    CHECK(operations.QueuedCount() == 3);
    operations.Shutdown();
    std::filesystem::remove_all(location, cleanupError);
}

TEST_CASE("Scene transition coordinator serializes requests and retains failure diagnostics")
{
    KeireEditor::SceneTransitionCoordinator transitions;
    const auto scene = Keire::AssetId::Generate();
    CHECK(transitions.Request({KeireEditor::SceneTransitionKind::Open, scene}));
    CHECK(transitions.Pending());
    CHECK_FALSE(transitions.Request({KeireEditor::SceneTransitionKind::Close, {}}));

    const auto request = transitions.BeginCommit();
    REQUIRE(request);
    CHECK(request->Kind == KeireEditor::SceneTransitionKind::Open);
    CHECK(request->Asset == scene);
    CHECK_FALSE(transitions.BeginCommit());
    transitions.Fail("decode failed");
    CHECK_FALSE(transitions.Pending());
    CHECK(transitions.State() == KeireEditor::SceneTransitionState::Failed);
    CHECK(transitions.Diagnostic() == "decode failed");

    CHECK(transitions.Request({KeireEditor::SceneTransitionKind::Create, {}}));
    CHECK(transitions.BeginCommit());
    transitions.Complete();
    CHECK(transitions.State() == KeireEditor::SceneTransitionState::Idle);
}

TEST_CASE("Scene document views become inert after the underlying scene closes")
{
    KeireEditor::SceneDocument document;
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Transient"));
    (void)scene->CreateEntity("Transient");
    document.Open(scene);
    CHECK(document.ActiveScene());
    scene->Close();
    CHECK_FALSE(document.ActiveScene());
    CHECK_FALSE(document.EditingScene());
    CHECK_FALSE(document.Dirty());
    CHECK_NOTHROW(document.SynchronizeSelection());
}

TEST_CASE("Asset picker filters environment textures without exposing raw asset IDs")
{
    Keire::AssetSourceRecord hdr;
    hdr.Id = Keire::AssetId::Generate();
    hdr.Type = Keire::Texture2DAsset::StaticType();
    hdr.RelativePath = "Sky/Studio.hdr";
    CHECK(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(hdr));

    auto color = hdr;
    color.Id = Keire::AssetId::Generate();
    color.RelativePath = "Textures/Albedo.png";
    CHECK_FALSE(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(color));

    color.ImportSettings["semantic"] = std::string("environment");
    CHECK(KeireEditor::AssetPicker::AcceptsEnvironmentTexture(color));

    KeireEditor::AssetPickerOptions options;
    options.Label = "Skybox";
    options.ExpectedType = Keire::Texture2DAsset::StaticType();
    options.Filter = &KeireEditor::AssetPicker::AcceptsEnvironmentTexture;
    CHECK(KeireEditor::AssetPicker::Accepts(hdr, options));
    CHECK(KeireEditor::AssetPicker::Accepts(color, options));

    auto mesh = hdr;
    mesh.Type = Keire::MeshAsset::StaticType();
    CHECK_FALSE(KeireEditor::AssetPicker::Accepts(mesh, options));
}

TEST_CASE("Asset picker resolves material authoring sources but hides Shader Graph preview materials")
{
    Keire::AssetSourceRecord shaderGraph;
    shaderGraph.Id = Keire::AssetId::Generate();
    shaderGraph.Type = Keire::ShaderGraphAsset::StaticType();
    shaderGraph.RelativePath = "Shaders/Layered.keireshadergraph";
    const auto compiledShader = Keire::AssetId::Generate();
    const auto previewMaterial = Keire::AssetId::Generate();
    shaderGraph.SubAssets = {compiledShader, previewMaterial};
    Keire::AssetSourceRecord graph;
    graph.Id = Keire::AssetId::Generate();
    graph.Type = Keire::MaterialGraphAsset::StaticType();
    graph.RelativePath = "Materials/Layered.keirematerialgraph";
    const auto runtimeMaterial = Keire::AssetId::Generate();
    graph.SubAssets = {runtimeMaterial};
    Keire::AssetSourceRecord instance;
    instance.Id = Keire::AssetId::Generate();
    instance.Type = Keire::MaterialInstanceAsset::StaticType();
    instance.RelativePath = "Materials/LayeredInstance.keirematerialinstance";
    const auto instanceMaterial = Keire::AssetId::Generate();
    instance.SubAssets = {instanceMaterial};
    const std::array records{shaderGraph, graph, instance};

    KeireEditor::AssetPickerOptions materialOptions;
    materialOptions.Label = "Material";
    materialOptions.ExpectedType = Keire::MaterialAsset::StaticType();
    materialOptions.ResolveType = [previewMaterial, runtimeMaterial,
                                   instanceMaterial](const Keire::AssetId asset) -> std::optional<Keire::AssetTypeId>
    {
        return asset == previewMaterial || asset == runtimeMaterial || asset == instanceMaterial
                   ? std::optional{Keire::MaterialAsset::StaticType()}
                   : std::nullopt;
    };
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, shaderGraph.Id, materialOptions));
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, previewMaterial, materialOptions));
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, graph.Id, materialOptions) == runtimeMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, runtimeMaterial, materialOptions) ==
          runtimeMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, instance.Id, materialOptions) == instanceMaterial);
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, instanceMaterial, materialOptions) ==
          instanceMaterial);
    materialOptions.ResolveType = {};
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset(records, graph.Id, materialOptions));

    KeireEditor::AssetPickerOptions meshOptions;
    meshOptions.Label = "Mesh";
    meshOptions.ExpectedType = Keire::MeshAsset::StaticType();
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::MeshAsset::CubeId(), meshOptions) ==
          Keire::MeshAsset::CubeId());
    CHECK(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::MeshAsset::TorusId(), meshOptions) ==
          Keire::MeshAsset::TorusId());
    CHECK_FALSE(KeireEditor::AssetPicker::ResolveCompatibleAsset({}, Keire::AssetId::Generate(), meshOptions));
}
