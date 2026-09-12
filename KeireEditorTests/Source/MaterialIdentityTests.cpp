#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"

#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

TEST_CASE("material keyword selection preserves values and rejects unavailable revisions atomically")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Keyword.hlsl";
    shader.Properties = {{"Amount", Keire::ShaderPropertyType::Scalar, {0.5F}}};
    shader.Properties.front().Id = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Amount", 0.8F);
    const auto defaultRuntime = Keire::AssetId::Generate();
    const auto alternateRuntime = Keire::AssetId::Generate();
    bool available = true;
    std::size_t resolutions = 0;
    const KeireEditor::MaterialDocument::ShaderReferenceResolver resolve =
        [&](const Keire::MaterialShaderReference& reference)
        -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
    {
        ++resolutions;
        if (!available)
            return std::nullopt;
        return KeireEditor::MaterialDocument::ResolvedShader{
            reference.Keywords.empty() ? defaultRuntime : alternateRuntime, shader};
    };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    const auto original = document.SaveSource();
    const Keire::ShaderGraphKeyword boolean{"UseDetail", {}, "false"};
    const Keire::ShaderGraphKeyword choice{"Quality", {"Low", "High"}, "Low"};
    REQUIRE(document.SetKeyword(boolean, "true", resolve));
    CHECK(document.Definition().Shader == alternateRuntime);
    CHECK(std::get<float>(document.Property("Amount")) == doctest::Approx(0.8F));
    CHECK(document.LastChangedProperty() == "$shader");
    const auto calls = resolutions;
    CHECK_FALSE(document.SetKeyword(boolean, "true", resolve));
    CHECK(resolutions == calls);
    CHECK_THROWS_AS((void)document.SetKeyword(boolean, "invalid", resolve), std::invalid_argument);
    CHECK_THROWS_AS((void)document.SetKeyword(choice, "Unknown", resolve), std::invalid_argument);
    CHECK_THROWS_AS((void)document.SetKeyword({"Private", {}, "false", false}, "true", resolve), std::invalid_argument);
    CHECK(resolutions == calls);
    REQUIRE(document.SetKeyword(choice, "High", resolve));
    const auto selected = document.SaveSource();
    available = false;
    CHECK_THROWS_AS((void)document.SetKeyword(choice, "Low", resolve), std::invalid_argument);
    CHECK(document.SaveSource() == selected);
    CHECK(document.Definition().Shader == alternateRuntime);
    available = true;
    document.Open(selected, resolve);
    CHECK(document.ShaderReference().Keywords.at("Quality") == "High");
    CHECK(document.SetKeyword(boolean, std::nullopt, resolve));
    CHECK_FALSE(document.ShaderReference().Keywords.contains("UseDetail"));
    CHECK(document.ShaderReference().Keywords.at("Quality") == "High");
    REQUIRE(document.ResetKeywords(resolve));
    CHECK(document.Definition().Shader == defaultRuntime);
    CHECK(document.SaveSource() == original);
    CHECK_FALSE(document.ResetKeywords(resolve));
    available = false;
    document.RequestKeyword(boolean, "true");
    CHECK(document.HasPendingKeywords());
    CHECK_FALSE(document.ApplyPendingKeywords(resolve));
    CHECK(document.SaveSource() == original);
    CHECK(document.Definition().Shader == defaultRuntime);
    document.RequestKeyword(choice, "High");
    document.RequestKeyword(choice, "Low");
    REQUIRE(document.SetProperty("Amount", 0.7F));
    available = true;
    REQUIRE(document.ApplyPendingKeywords(resolve));
    CHECK_FALSE(document.HasPendingKeywords());
    CHECK(document.ShaderReference().Keywords.at("UseDetail") == "true");
    CHECK(document.ShaderReference().Keywords.at("Quality") == "Low");
    CHECK(std::get<float>(document.Property("Amount")) == doctest::Approx(0.7F));
    document.RequestKeywordReset();
    CHECK(document.RequestedShaderReference().Keywords.empty());
    document.CancelPendingKeywords();
    CHECK_FALSE(document.ApplyPendingKeywords(resolve));
    CHECK(document.ShaderReference().Keywords.size() == 2);
    document.RequestKeyword(choice, "High");
    REQUIRE(document.ResetKeywords(resolve));
    CHECK_FALSE(document.ApplyPendingKeywords(resolve));
    CHECK_FALSE(document.HasPendingKeywords());
    CHECK(document.ShaderReference().Keywords.empty());
    document.RequestKeyword(boolean, "true");
    document.OpenAsset(Keire::AssetId::Generate(), "Other.keirematerial", original, resolve);
    CHECK_FALSE(document.HasPendingKeywords());
}

TEST_CASE("material shader picker hides generated code only while its authoring graph exists")
{
    Keire::AssetSourceRecord graph;
    graph.Id = Keire::AssetId::Generate();
    graph.Type = Keire::ShaderGraphAsset::StaticType();
    graph.RelativePath = "Shaders/Surface.keireshadergraph";
    Keire::AssetSourceRecord code;
    code.Id = Keire::AssetId::Generate();
    code.Type = Keire::ShaderAsset::StaticType();
    code.RelativePath =
        std::filesystem::path("Generated/ShaderGraphs") / graph.Id.ToString() / "ShaderGraph.keireshader";
    const std::vector records{graph};
    CHECK(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(code, records));
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(code, {}));
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(graph, records));
    code.RelativePath = "Shaders/Handwritten.keireshader";
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(code, records));
    code.RelativePath =
        std::filesystem::path("GENERATED/SHADERGRAPHS") / graph.Id.ToString() / "ShaderGraph.keireshader";
    CHECK(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(code, records));
    code.RelativePath = std::filesystem::path("Generated/ShaderGraphs") / Keire::AssetId::Generate().ToString() /
                        "ShaderGraph.keireshader";
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel::IsGeneratedShaderSource(code, records));
}

TEST_CASE("material reset and edits retain incompatible type and range history")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/ResetHistory.hlsl";
    shader.Properties = {{"Amount", Keire::ShaderPropertyType::Scalar, {0.5F}}};
    auto& property = shader.Properties.front();
    property.Id = Keire::AssetId::Generate();
    property.Minimum = 0.0F;
    property.Maximum = 1.0F;
    Keire::MaterialAuthoringDefinition source;
    source.SchemaVersion = 5;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.PropertyOverrides = {{property.Id, "OldName", Keire::Vector2{1.0F, 2.0F}},
                                {property.Id, "OldName", 4.0F},
                                {property.Id, "Amount", 0.8F}};
    KeireEditor::MaterialDocument document;
    const auto resolve = [&](Keire::AssetId) { return std::optional(shader); };
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    CHECK(document.InactiveProperties().size() == 2);
    CHECK(document.SetProperty("Amount", 0.7F));
    CHECK(document.InactiveProperties().size() == 2);
    CHECK(document.ResetProperty("Amount"));
    CHECK(std::get<float>(document.Property("Amount")) == 0.5F);
    CHECK(document.InactiveProperties().size() == 2);
    CHECK(document.SetProperty("Amount", 0.6F));
    CHECK(document.ResetProperties());
    document.Open(document.SaveSource(), resolve);
    CHECK(document.InactiveProperties().size() == 2);
    CHECK(document.RemoveInactiveProperties());
    CHECK(document.InactiveProperties().empty());
}

TEST_CASE("material clipboard follows renamed identities skips incompatibilities and never resolves shaders")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Clipboard.hlsl";
    shader.Properties = {{"Amount", Keire::ShaderPropertyType::Scalar, {0.25F}},
                         {"Other", Keire::ShaderPropertyType::Scalar, {0.5F}}};
    for (auto& property : shader.Properties)
        property.Id = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    int resolutions = 0;
    const auto resolve = [&](Keire::AssetId)
    {
        ++resolutions;
        return std::optional(shader);
    };
    KeireEditor::MaterialDocument original;
    original.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    REQUIRE(original.SetProperty("Amount", 0.8F));
    const auto clipboard = original.CopyProperties();
    shader.Properties[0].Name = "Renamed";
    shader.Properties[1].Maximum = 0.4F;
    shader.Properties[1].DefaultValue.X = 0.2F;
    KeireEditor::MaterialDocument destination;
    destination.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    const auto calls = resolutions;
    CHECK(destination.PasteProperties(clipboard) == 1);
    CHECK(std::get<float>(destination.Property("Renamed")) == doctest::Approx(0.8F));
    CHECK(std::get<float>(destination.Property("Other")) == doctest::Approx(0.2F));
    CHECK(destination.InactiveProperties().empty());
    CHECK(destination.PasteProperties(clipboard) == 0);
    CHECK(resolutions == calls);
    shader.Properties[0].Id = Keire::AssetId::Generate();
    destination.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    CHECK(destination.PasteProperties(clipboard) == 0);
    KeireEditor::MaterialDocument unresolved;
    CHECK_THROWS_AS((void)unresolved.PasteProperties(clipboard), std::logic_error);
    CHECK_THROWS_AS((void)unresolved.PastePropertiesByName(clipboard), std::logic_error);
    shader.Properties[0].Name = "Amount";
    destination.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolve);
    const auto namePasteResolutions = resolutions;
    CHECK(destination.PasteProperties(clipboard) == 0);
    CHECK(destination.PastePropertiesByName(clipboard) == 1);
    CHECK(std::get<float>(destination.Property("Amount")) == doctest::Approx(0.8F));
    CHECK(destination.InactiveProperties().empty());
    CHECK(destination.PastePropertiesByName(clipboard) == 0);
    const auto namedSource = Keire::MaterialAsset::DecodeAuthoringSource(destination.SaveSource());
    REQUIRE(namedSource.PropertyOverrides.size() == 1);
    CHECK(namedSource.PropertyOverrides[0].Property == shader.Properties[0].Id);
    CHECK(namedSource.PropertyOverrides[0].Property != clipboard[0].Property);
    CHECK(resolutions == namePasteResolutions);
}

TEST_CASE("material property identities survive renames and never bind a reused symbol")
{
    const auto identity = Keire::AssetId::Generate();
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Identity.hlsl";
    shader.Properties = {{"OldName", Keire::ShaderPropertyType::Scalar}};
    shader.Properties.front().Id = identity;
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("OldName", 0.25F);
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    auto saved = Keire::MaterialAsset::DecodeAuthoringSource(document.SaveSource());
    REQUIRE(saved.SchemaVersion == 5);
    CHECK(saved.Properties.empty());
    REQUIRE(saved.PropertyOverrides.size() == 1);
    CHECK(saved.PropertyOverrides.front().Property == identity);
    shader.Properties.front().Name = "Renamed";
    shader.Properties.front().DisplayName = "A friendly display name";
    shader.Properties.push_back({"OldName", Keire::ShaderPropertyType::Scalar});
    shader.Properties.back().Id = Keire::AssetId::Generate();
    document.Open(document.SaveSource(), resolver);
    CHECK(std::get<float>(document.Property("Renamed")) == 0.25F);
    CHECK(std::get<float>(document.Property("OldName")) == 0.0F);
    CHECK(document.SetProperty("Renamed", 0.75F));
    document.Open(document.SaveSource(), resolver);
    CHECK(std::get<float>(document.Property("Renamed")) == 0.75F);
    CHECK(document.ResetProperty("Renamed"));
    document.Open(document.SaveSource(), resolver);
    CHECK(std::get<float>(document.Property("Renamed")) == 0.0F);
}

TEST_CASE("material stable property history survives incompatible declarations and missing shader recovery")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Identity.hlsl";
    shader.Properties = {{"Value", Keire::ShaderPropertyType::Scalar}};
    shader.Properties.front().Id = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Properties.emplace("Value", 0.25F);
    const KeireEditor::MaterialDocument::ShaderResolver resolver =
        [&](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return shader; };
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeAuthoringSource(source), resolver);
    shader.Properties.front().Type = Keire::ShaderPropertyType::Vector2;
    document.Open(document.SaveSource(), resolver);
    CHECK(document.InactiveProperties().size() == 1);
    CHECK(document.Definition().Properties.empty());
    CHECK(document.SetProperty("Value", Keire::Vector2{0.5F, 0.75F}));
    const auto snapshot = document.SaveSource();
    shader.Properties.front().Type = Keire::ShaderPropertyType::Scalar;
    document.Open(snapshot, resolver);
    CHECK(std::get<float>(document.Property("Value")) == 0.25F);
    CHECK(document.InactiveProperties().size() == 1);
    CHECK(document.RemoveInactiveProperties());
    document.Open(document.SaveSource(), resolver);
    CHECK(std::get<float>(document.Property("Value")) == 0.25F);
    CHECK(document.InactiveProperties().empty());
    const KeireEditor::MaterialDocument::ShaderResolver missing =
        [](Keire::AssetId) -> std::optional<Keire::ShaderAssetDefinition> { return std::nullopt; };
    document.Open(snapshot, missing);
    CHECK_FALSE(document.HasResolvedShader());
    CHECK(document.SaveSource() == snapshot);
    CHECK_FALSE(KeireEditor::MaterialDocument::ResolveRuntimeRevision(
        snapshot, [](const Keire::MaterialShaderReference& value) { return value.Asset; }));
}

TEST_CASE("material stable runtime revisions resolve current symbols without modifying source snapshots")
{
    const auto identity = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.SchemaVersion = 5;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.PropertyOverrides.push_back({identity, "OldName", 0.75F});
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Identity.hlsl";
    shader.Properties = {{"NewName", Keire::ShaderPropertyType::Scalar}};
    shader.Properties.front().Id = identity;
    const auto before = Keire::MaterialAsset::EncodeAuthoringSource(source);
    const KeireEditor::MaterialDocument::ShaderReferenceResolver resolver =
        [&](const Keire::MaterialShaderReference& reference)
        -> std::optional<KeireEditor::MaterialDocument::ResolvedShader>
    { return KeireEditor::MaterialDocument::ResolvedShader{reference.Asset, shader}; };
    const auto runtime = KeireEditor::MaterialDocument::ResolveRuntimeRevision(before, resolver);
    REQUIRE(runtime);
    CHECK(runtime->Properties.contains("NewName"));
    CHECK_FALSE(runtime->Properties.contains("OldName"));
    CHECK(std::get<float>(runtime->Properties.at("NewName")) == 0.75F);
    CHECK(Keire::MaterialAsset::EncodeAuthoringSource(source) == before);
    source.PropertyOverrides.front().Property = {};
    CHECK_THROWS_AS((void)Keire::MaterialAsset::EncodeAuthoringSource(source), std::invalid_argument);
}

TEST_CASE("material Inspector properties with duplicate display names remain independently editable")
{
    class SelectiveEditor final : public KeireEditor::IPropertyEditor
    {
      public:
        std::vector<std::string> PropertyLabels;
        std::vector<std::string> Categories;
        std::vector<std::string> Tooltips;
        int HdrColors = 0;
        void PropertyCategory(std::string_view label) override { Categories.emplace_back(label); }
        void PropertyTooltip(std::string_view description) override { Tooltips.emplace_back(description); }
        bool EditHdrColor(std::string_view, Keire::Color&) override
        {
            ++HdrColors;
            return false;
        }
        std::string SelectedLabel;
        bool EditInteger(std::string_view, std::int64_t&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditText(std::string_view, std::string&) override { return false; }
        bool EditVector2(std::string_view, Keire::Vector2&, double) override { return false; }
        bool EditVector3(std::string_view, Keire::Vector3&, double) override { return false; }
        bool EditVector4(std::string_view, Keire::Vector4&, double) override { return false; }
        bool EditQuaternion(std::string_view, Keire::Quaternion&, double) override { return false; }
        bool EditColor(std::string_view, Keire::Color&) override { return false; }
        bool EditAsset(std::string_view, Keire::AssetId&, std::optional<Keire::AssetTypeId>, std::string_view) override
        {
            return false;
        }
        bool EditEntity(std::string_view, Keire::EntityId&) override { return false; }
        bool EditEvent(std::string_view, Keire::ComponentEventValue&, std::size_t) override { return false; }
        bool EditChoice(std::string_view, std::int64_t&, std::span<const std::string_view>) override { return false; }
        bool EditBoolean(std::string_view, bool&) override { return false; }
        bool EditScalar(std::string_view label, double& value, double, std::optional<double>,
                        std::optional<double>) override
        {
            PropertyLabels.emplace_back(label);
            if (label != SelectedLabel)
                return false;
            value = 0.8;
            return true;
        }
    } editor;
    const auto shader = Keire::AssetId::Generate();
    Keire::ShaderAssetDefinition reflection;
    reflection.Source = "Assets/InspectorIdentity.hlsl";
    reflection.Properties = {{"First", Keire::ShaderPropertyType::Scalar, {0.2F}},
                             {"Second", Keire::ShaderPropertyType::Scalar, {0.3F}}};
    for (auto& property : reflection.Properties)
    {
        property.Id = Keire::AssetId::Generate();
        property.DisplayName = "Amount";
    }
    const auto resolve = [&](Keire::AssetId) { return std::optional(reflection); };
    Keire::MaterialAssetDefinition source;
    source.Shader = shader;
    KeireEditor::MaterialDocument document;
    document.Open(Keire::MaterialAsset::EncodeSource(source), resolve);
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    REQUIRE(editor.PropertyLabels.size() == 2);
    CHECK(editor.PropertyLabels[0] != editor.PropertyLabels[1]);
    editor.SelectedLabel = editor.PropertyLabels[1];
    CHECK(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    CHECK(std::get<float>(document.Property("First")) == doctest::Approx(0.2F));
    CHECK(std::get<float>(document.Property("Second")) == doctest::Approx(0.8F));
    const auto previousLabel = editor.SelectedLabel;
    reflection.Properties[1].DisplayName = "Renamed Amount";
    document.Open(document.SaveSource(), resolve);
    editor.PropertyLabels.clear();
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    const auto& renamedLabel = editor.PropertyLabels[1];
    REQUIRE(previousLabel.find("###") != std::string::npos);
    REQUIRE(renamedLabel.find("###") != std::string::npos);
    CHECK(previousLabel.substr(previousLabel.find("###")) == renamedLabel.substr(renamedLabel.find("###")));
    CHECK(std::get<float>(document.Property("Second")) == doctest::Approx(0.8F));
    reflection.Properties[0].Category = "Z Surface";
    reflection.Properties[1].Category = "A Surface";
    document.Open(document.SaveSource(), resolve);
    editor.PropertyLabels.clear();
    editor.Categories.clear();
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    REQUIRE(editor.Categories.size() == 2);
    CHECK(editor.Categories[0] == "A Surface");
    CHECK(editor.Categories[1] == "Z Surface");
    CHECK(editor.PropertyLabels[0].starts_with("Renamed Amount###"));
    Keire::ShaderPropertyDefinition radiance;
    radiance.Name = "Radiance";
    radiance.Type = Keire::ShaderPropertyType::Color;
    radiance.DefaultValue = {4.0F, 2.0F, 0.5F, 1.0F};
    radiance.HighDynamicRange = true;
    radiance.Description = "Linear radiance";
    reflection.Properties.push_back(radiance);
    document.Open(document.SaveSource(), resolve);
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    CHECK(editor.HdrColors == 1);
    CHECK(std::ranges::find(editor.Tooltips, "Linear radiance") != editor.Tooltips.end());
    reflection.Properties.back().HighDynamicRange = false;
    document.Open(document.SaveSource(), resolve);
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, document));
    CHECK(editor.HdrColors == 2);
}

TEST_CASE("material shader picker rejects compute and non-surface graph targets before assignment")
{
    for (const auto target :
         {Keire::ShaderGraphTarget::Material, Keire::ShaderGraphTarget::Ui, Keire::ShaderGraphTarget::Fullscreen,
          Keire::ShaderGraphTarget::Vfx, Keire::ShaderGraphTarget::CustomGraphics, Keire::ShaderGraphTarget::Compute})
    {
        const auto graph = Keire::CreateTargetShaderGraph(target);
        CHECK(KeireEditor::MaterialInspectorPanel::AcceptsSurfaceShaderGraph(
                  Keire::ShaderGraphAsset::EncodeSource(graph)) == (target == Keire::ShaderGraphTarget::Material));
    }
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel::AcceptsSurfaceShaderGraph({}));
}
