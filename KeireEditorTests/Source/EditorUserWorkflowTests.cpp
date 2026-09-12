#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/NamedAssetCreation.h"
#include "KeireClient/Editor/ShaderGraphPanelLayout.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>

TEST_CASE("Shader Graph panes fit narrow docks and reserve preview space only when it fits")
{
    for (const float width : {1.0F, 120.0F, 228.0F, 319.0F, 619.0F, 620.0F, 1040.0F})
    {
        const auto layout = KeireEditor::ResolveShaderGraphPaneLayout(width, true);
        CHECK(layout.CanvasWidth > 0.0F);
        CHECK(layout.CanvasWidth + layout.PreviewWidth <= width);
        if (width < 620.0F)
        {
            CHECK(layout.CanvasWidth == width);
            CHECK(layout.PreviewWidth == 0.0F);
        }
        else
        {
            CHECK(layout.CanvasWidth >= 320.0F);
            CHECK(layout.PreviewWidth == 248.0F);
            CHECK(layout.CanvasWidth + layout.PreviewWidth + 8.0F == width);
        }
        const auto hidden = KeireEditor::ResolveShaderGraphPaneLayout(width, false);
        CHECK(hidden.CanvasWidth == width);
        CHECK(hidden.PreviewWidth == 0.0F);
    }
    CHECK(KeireEditor::ResolveShaderGraphPaneLayout(0.0F, true).CanvasWidth == 1.0F);
}

TEST_CASE("asset browser displays and searches Unicode filenames as UTF-8")
{
    const std::string name = "Workshop Caf\xc3\xa9";
    const auto path = Keire::Detail::PathFromUtf8("Scenes/" + name + ".keirescene");
    CHECK(KeireEditor::DisplayName(path) == name);
    std::array<Keire::AssetSourceRecord, 2> records;
    records[0].RelativePath = path;
    records[1].RelativePath = "Scenes/Plain.keirescene";
    KeireEditor::AssetBrowserRecordViewCache cache;
    REQUIRE(cache.Refresh(records, 1, "Scenes", "Caf\xc3\xa9"));
    REQUIRE(cache.Records().size() == 1);
    CHECK(cache.Records().front() == &records[0]);
    CHECK(cache.Refresh(records, 1, "Scenes", "Missing"));
    CHECK(cache.Records().empty());
}

TEST_CASE("asset browser reveal makes newly created assets visible through an existing search")
{
    std::array<Keire::AssetSourceRecord, 3> records;
    records[0].RelativePath = "Shaders/FullscreenAcceptance.keireshadergraph";
    records[1].RelativePath = "Shaders/VfxAcceptance.keireshadergraph";
    records[2].RelativePath = Keire::Detail::PathFromUtf8("Caf\xc3\xa9/Custom.keireshadergraph");
    std::filesystem::path folder = "Shaders";
    std::string search = "Fullscreen";
    KeireEditor::AssetBrowserRecordViewCache cache;
    REQUIRE(cache.Refresh(records, 1, folder, search));
    REQUIRE(cache.Records().size() == 1);
    REQUIRE(cache.Records().front() == &records[0]);

    KeireEditor::PrepareAssetBrowserReveal(records[1], folder, search);
    REQUIRE(cache.Refresh(records, 1, folder, search));
    CHECK(std::ranges::find(cache.Records(), &records[1]) != cache.Records().end());
    CHECK(folder == "Shaders");
    CHECK(search.empty());

    search = "Missing";
    KeireEditor::PrepareAssetBrowserReveal(records[2], folder, search);
    REQUIRE(cache.Refresh(records, 1, folder, search));
    REQUIRE(cache.Records().size() == 1);
    CHECK(cache.Records().front() == &records[2]);
    KeireEditor::PrepareAssetBrowserReveal(records[2], folder, search);
    CHECK_FALSE(cache.Refresh(records, 1, folder, search));
}

TEST_CASE("asset browser reveal waits for its widget and requests scrolling only once")
{
    const auto target = Keire::AssetId::Generate();
    auto pending = target;
    for (int index = 0; index < 100; ++index)
        CHECK_FALSE(KeireEditor::ConsumeAssetBrowserReveal(pending, Keire::AssetId::Generate()));
    CHECK(pending == target);
    CHECK(KeireEditor::ConsumeAssetBrowserReveal(pending, target));
    CHECK_FALSE(pending);
    CHECK_FALSE(KeireEditor::ConsumeAssetBrowserReveal(pending, target));
    CHECK_FALSE(KeireEditor::ConsumeAssetBrowserReveal(pending, {}));
    pending = target;
    CHECK(KeireEditor::ConsumeAssetBrowserReveal(pending, target));
}

TEST_CASE("New Material Graph documents focus the canvas on their OpenPBR surface")
{
    KeireEditor::MaterialGraphDocument document({
        .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {},
    });
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000031");
    document.Open(asset, Keire::MaterialGraphAsset::EncodeSource(Keire::CreateOpenPbrMaterial()), 1);
    const auto canvas = document.BuildCanvasModel(true);
    CHECK(std::ranges::none_of(canvas.Nodes, [](const KeireEditor::NodeGraphNode& node)
                               { return node.Label == "Template Defaults"; }));
}

TEST_CASE("Asset Browser double-click routes material and shader authoring assets internally")
{
    using enum KeireEditor::AssetBrowserOpenAction;

    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerial") == MaterialGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerialgraph") == MaterialGraph);
    CHECK(KeireEditor::IsMaterialGraphSourcePath("Materials/Surface.KEIREMATERIAL"));
    CHECK(KeireEditor::IsMaterialGraphSourcePath("Materials/Surface.KEIREMATERIALGRAPH"));
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keiremateriallegacy") == Material);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Surface.keirematerialinstance") == MaterialInstance);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Shaders/Surface.keireshadergraph") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Shaders/Surface.KEIRESHADERGRAPH") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Common.keiresubgraph") == ShaderGraph);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Materials/Globals.keireparametercollection") ==
          MaterialParameterCollection);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.keireui") == UiDocument);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.KEIREUI") == UiDocument);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.keirestyle") == UiStyleSheet);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("Textures/Surface.png") == External);

    Keire::AssetSourceRecord instance;
    instance.RelativePath = "Materials/Surface.keirematerialinstance";
    CHECK(KeireEditor::AssetTypeName(instance) == "Material Instance");
    instance.RelativePath = "Materials/Legacy.keireshadergraphinstance";
    CHECK(KeireEditor::AssetTypeName(instance) == "Legacy Shader Graph Instance");
    instance.RelativePath = "Materials/Common.keiresubgraph";
    CHECK(KeireEditor::AssetTypeName(instance) == "Shader Subgraph");
    instance.RelativePath = "Materials/Globals.keireparametercollection";
    CHECK(KeireEditor::AssetTypeName(instance) == "Material Parameter Collection");
    instance.RelativePath = "UI/Hud.keireui";
    CHECK(KeireEditor::AssetTypeName(instance) == "UI Document");
    instance.RelativePath = "UI/Hud.keirestyle";
    CHECK(KeireEditor::AssetTypeName(instance) == "UI Style Sheet");
}

TEST_CASE("Asset creation labels keep Shader Graph and Material Graph workflows distinct")
{
    using KeireEditor::NamedAssetCreationDisplayName;
    using KeireEditor::NamedAssetCreationKind;

    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ShaderGraph) == "shader graph");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialGraph) == "material");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialInstance) == "material instance");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialFunction) == "material function");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialLayer) == "material layer");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ManagedData) == "ScriptableObject");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ScriptableObjectScript) == "C# ScriptableObject class");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::UiDocument) == "UI document");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::UiStyleSheet) == "UI style sheet");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::UiFontFamily) == "UI font family");
}

TEST_CASE("managed script creation stays in the selected folder and extends runtime source coverage")
{
    Keire::ManagedAssemblyDefinition gameplay;
    gameplay.Name = "Gameplay";
    gameplay.RootNamespace = "Game";
    gameplay.SourceRoots = {"Assets/Scripts/Gameplay"};
    const auto gameplayId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000101");
    const std::array assemblies{KeireEditor::ManagedScriptAssemblyCandidate{gameplayId, gameplay}};

    const auto nested = KeireEditor::ResolveManagedScriptPlacement(assemblies, "Scripts/Gameplay/Characters");
    CHECK(nested.Assembly == gameplayId);
    CHECK(nested.RootNamespace == "Game");
    CHECK(nested.SourceRootToAdd.empty());

    const auto sibling = KeireEditor::ResolveManagedScriptPlacement(assemblies, "Characters/Enemies");
    CHECK(sibling.Assembly == gameplayId);
    CHECK(sibling.RootNamespace == "Game");
    CHECK(sibling.SourceRootToAdd == std::filesystem::path("Assets/Characters/Enemies"));
    CHECK(KeireEditor::ExtendManagedAssemblySourceRoots(gameplay, sibling.SourceRootToAdd));
    CHECK(std::ranges::find(gameplay.SourceRoots, sibling.SourceRootToAdd) != gameplay.SourceRoots.end());
    CHECK_FALSE(KeireEditor::ExtendManagedAssemblySourceRoots(gameplay, "Assets/Characters/Enemies/Nested"));

    const auto generated = KeireEditor::ResolveManagedScriptPlacement(assemblies, "Scripts/Generated");
    CHECK(generated.Assembly == gameplayId);
    CHECK(generated.SourceRootToAdd == std::filesystem::path("Assets/Scripts/Generated"));

    CHECK(KeireEditor::ExtendManagedAssemblySourceRoots(gameplay, "Assets/Scripts"));
    CHECK(std::ranges::find(gameplay.SourceRoots, std::filesystem::path("Assets/Scripts/Gameplay")) ==
          gameplay.SourceRoots.end());

    const auto stableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000103");
    const auto behaviour = KeireEditor::BuildManagedScriptSource(KeireEditor::ManagedScriptTemplateKind::Behaviour,
                                                                 "Game", "PlayerController", stableId);
    CHECK(behaviour.find("[StableComponentId(\"" + stableId.ToString() + "\")]") != std::string::npos);
    CHECK(behaviour.find("public sealed class PlayerController : Behaviour") != std::string::npos);

    const auto scriptableObject = KeireEditor::BuildManagedScriptSource(
        KeireEditor::ManagedScriptTemplateKind::ScriptableObject, "Game", "WeaponTuning", stableId);
    CHECK(scriptableObject.find("[StableAssetTypeId(\"" + stableId.ToString() + "\")]") != std::string::npos);
    CHECK(scriptableObject.find("[CreateAssetMenu(\"WeaponTuning\", \"WeaponTuning\")]") != std::string::npos);
    CHECK(scriptableObject.find("public sealed class WeaponTuning : ScriptableObject") != std::string::npos);
    CHECK(scriptableObject.find("public int Value = 0;") != std::string::npos);
}

TEST_CASE("hierarchy prefab payloads decode the encoder terminator before UUID parsing")
{
    const auto entity = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000102");
    const std::array entities{entity};
    const auto encoded = KeireEditor::EncodeAssetPayload(entities);
    REQUIRE(encoded.ends_with('\n'));
    CHECK(KeireEditor::DecodeSingleAssetPayload(std::as_bytes(std::span(encoded.data(), encoded.size()))) == entity);
}
