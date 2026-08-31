#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/NamedAssetCreation.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <span>

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
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.keireui") == UiDocument);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.KEIREUI") == UiDocument);
    CHECK(KeireEditor::ResolveAssetBrowserOpenAction("UI/Hud.keirestyle") == UiStyleSheet);
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
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialGraph) == "material graph");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialInstance) == "material instance");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialFunction) == "material function");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::MaterialLayer) == "material layer");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ManagedData) == "ScriptableObject");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::ScriptableObjectScript) == "C# ScriptableObject class");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::UiDocument) == "UI document");
    CHECK(NamedAssetCreationDisplayName(NamedAssetCreationKind::UiStyleSheet) == "UI style sheet");
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
