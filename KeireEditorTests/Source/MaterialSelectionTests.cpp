#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/MaterialSelectionDocument.h"
#include "KeireClient/Editor/MaterialSelectionEditing.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
    struct SelectionFixture
    {
        Keire::AssetId Shader = Keire::AssetId::Generate();
        Keire::ShaderPropertyDefinition Property{"Amount", Keire::ShaderPropertyType::Scalar, {0.2F}};
        SelectionFixture() { Property.Id = Keire::AssetId::Generate(); }
        KeireEditor::MaterialDocument Document(float value, std::string name = "Amount", float maximum = 1.0F) const
        {
            Keire::ShaderAssetDefinition shader;
            shader.Source = "Shared.hlsl";
            shader.Properties = {Property};
            shader.Properties.front().Name = name;
            shader.Properties.front().Maximum = maximum;
            Keire::MaterialAuthoringDefinition source;
            source.SchemaVersion = 5;
            source.Shader.Asset = Shader;
            source.PropertyOverrides = {{Property.Id, name, value}};
            KeireEditor::MaterialDocument document;
            document.OpenAsset(Keire::AssetId::Generate(), name + ".keirematerial",
                               Keire::MaterialAsset::EncodeAuthoringSource(source),
                               [shader](Keire::AssetId) { return std::optional(shader); });
            return document;
        }
    };
} // namespace

TEST_CASE("material selection mixed properties follow stable identities through symbol renames")
{
    SelectionFixture fixture;
    KeireEditor::MaterialSelectionDocument selection({fixture.Document(0.3F), fixture.Document(0.7F, "Renamed")});
    REQUIRE(selection.Property(fixture.Property));
    CHECK(selection.Property(fixture.Property)->Mixed);
    CHECK(selection.SetProperty(fixture.Property, 0.3F));
    CHECK_FALSE(selection.Property(fixture.Property)->Mixed);
    CHECK(std::get<float>(selection.Documents()[1].Property("Renamed")) == 0.3F);
    CHECK(selection.Undo());
    CHECK(selection.Property(fixture.Property)->Mixed);
    CHECK(std::get<float>(selection.Documents()[1].Property("Renamed")) == 0.7F);
    CHECK(selection.Redo());
    CHECK(selection.ResetProperty(fixture.Property));
    for (const auto& document : selection.Documents())
        CHECK(document.Definition().Properties.empty());
    CHECK(selection.Undo());
    CHECK_FALSE(selection.SetProperty(fixture.Property, 0.3F));
    CHECK(selection.CanRedo());
    auto unrelated = fixture.Property;
    unrelated.Id = Keire::AssetId::Generate();
    CHECK_FALSE(selection.Property(unrelated));
    CHECK_THROWS_AS((void)selection.SetProperty(unrelated, 0.8F), std::invalid_argument);
}

TEST_CASE("material selection validation and publisher failures leave every source and history unchanged")
{
    SelectionFixture fixture;
    bool fail = false;
    int publications = 0;
    KeireEditor::MaterialSelectionDocument selection({fixture.Document(0.3F), fixture.Document(0.4F, "Amount", 0.5F)},
                                                     [&](const auto before, const auto after)
                                                     {
                                                         CHECK(before.size() == 2);
                                                         CHECK(after.size() == 2);
                                                         if (fail)
                                                             throw std::runtime_error(
                                                                 "Publication failed before replacement.");
                                                         ++publications;
                                                     });
    const auto first = selection.Documents()[0].SaveSource();
    const auto second = selection.Documents()[1].SaveSource();
    CHECK_THROWS_AS((void)selection.SetProperty(fixture.Property, 0.8F), std::invalid_argument);
    CHECK(publications == 0);
    CHECK(selection.Documents()[0].SaveSource() == first);
    CHECK(selection.Documents()[1].SaveSource() == second);
    CHECK_FALSE(selection.CanUndo());
    fail = true;
    CHECK_THROWS_AS((void)selection.SetProperty(fixture.Property, 0.2F), std::runtime_error);
    CHECK(selection.Documents()[0].SaveSource() == first);
    CHECK_FALSE(selection.CanUndo());
    fail = false;
    REQUIRE(selection.SetProperty(fixture.Property, 0.2F));
    CHECK(publications == 1);
    const auto saved = selection.Documents()[0].SaveSource();
    CHECK(std::vector<std::byte>(selection.Documents()[0].BaselineSource().begin(),
                                 selection.Documents()[0].BaselineSource().end()) == saved);
    fail = true;
    CHECK_THROWS_AS((void)selection.Undo(), std::runtime_error);
    CHECK(selection.Documents()[0].SaveSource() == saved);
    CHECK(selection.CanUndo());
    CHECK_FALSE(selection.CanRedo());
    fail = false;
    REQUIRE(selection.Undo());
    fail = true;
    CHECK_THROWS_AS((void)selection.Redo(), std::runtime_error);
    CHECK(selection.Documents()[0].SaveSource() == first);
    CHECK(selection.CanRedo());
}

TEST_CASE("material selection surface fields preserve independent mixed state")
{
    SelectionFixture fixture;
    auto first = fixture.Document(0.3F);
    auto second = fixture.Document(0.4F);
    auto surface = second.Surface();
    surface.AlphaMode = Keire::MaterialAlphaMode::Mask;
    surface.AlphaCutoff = 0.7F;
    surface.DoubleSided = true;
    REQUIRE(second.SetSurface(surface));
    KeireEditor::MaterialSelectionDocument selection({first, second});
    REQUIRE(selection.SetSurface(std::nullopt, std::nullopt, false));
    CHECK_FALSE(selection.Documents()[1].Surface().DoubleSided);
    CHECK(selection.Documents()[1].Surface().AlphaMode == Keire::MaterialAlphaMode::Mask);
    CHECK(selection.Documents()[1].Surface().AlphaCutoff == 0.7F);
    REQUIRE(selection.Undo());
    CHECK(selection.Documents()[1].Surface() == surface);
    CHECK(selection.Documents()[0].Surface() == first.Surface());
    CHECK_THROWS_AS((KeireEditor::MaterialSelectionDocument{std::vector<KeireEditor::MaterialDocument>{}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((KeireEditor::MaterialSelectionDocument{{first, first}}), std::invalid_argument);
}

TEST_CASE("material selection history merges gestures and preserves failed undo snapshots")
{
    const auto first = Keire::AssetId::Generate();
    const auto second = Keire::AssetId::Generate();
    std::vector<KeireEditor::MaterialSourceSnapshot> source{{first, "Assets/First.keirematerial", {std::byte{0}}},
                                                            {second, "Assets/Second.keirematerial", {std::byte{1}}}};
    const auto original = source;
    bool fail = false;
    const auto apply = [&](const auto before, const auto after)
    {
        REQUIRE(std::ranges::equal(source, before));
        if (fail)
            throw std::runtime_error("Atomic source writer failed.");
        source.assign(after.begin(), after.end());
    };
    const auto service = Keire::CreateRef<Keire::UndoService>();
    const auto undo = service->CreateContext({.Name = "Selected materials"});
    for (int value = 2; value < 4; ++value)
    {
        auto replacement = source;
        for (auto& material : replacement)
            material.Source = {static_cast<std::byte>(value)};
        undo->Execute(KeireEditor::CreateMaterialSelectionEdit(source, replacement, 1, apply));
    }
    CHECK(undo->UndoCount() == 1);
    const auto edited = source;
    fail = true;
    CHECK_THROWS_AS((void)undo->Undo(), std::runtime_error);
    CHECK(source == edited);
    CHECK(undo->UndoCount() == 1);
    fail = false;
    REQUIRE(undo->Undo());
    CHECK(source == original);
    REQUIRE(undo->Redo());
    CHECK(source == edited);
    service->Close();
}

TEST_CASE("material selection source publication validates the complete set before writing and rejects stale undo")
{
    struct Directory
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-Material-Selection-" + Keire::AssetId::Generate().ToString());
        Directory() { std::filesystem::create_directories(Root / "Assets"); }
        ~Directory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } directory;
    SelectionFixture fixture;
    const auto a = fixture.Document(0.3F);
    const auto b = fixture.Document(0.4F);
    const std::vector<KeireEditor::MaterialSourceSnapshot> before{
        {a.Asset(), "Assets/First.keirematerial", a.SaveSource()},
        {b.Asset(), "Assets/Second.keirematerial", b.SaveSource()}};
    const Keire::Detail::AnchoredFileSystem fs(directory.Root);
    for (const auto& source : before)
        fs.WriteFileAtomically(source.RelativePath, source.Source);
    auto after = before;
    after[0].Source = fixture.Document(0.5F).SaveSource();
    after[1].Source = {std::byte{0}};
    CHECK_THROWS(KeireEditor::PublishMaterialSelection(directory.Root, before, after));
    CHECK(fs.Read(before[0].RelativePath, 65536) == before[0].Source);
    after[1].Source = after[0].Source;
    REQUIRE_NOTHROW(KeireEditor::PublishMaterialSelection(directory.Root, before, after));
    CHECK(fs.Read(before[0].RelativePath, 65536) == after[0].Source);
    CHECK(fs.Read(before[1].RelativePath, 65536) == after[1].Source);
    fs.WriteFileAtomically(before[1].RelativePath, before[1].Source);
    CHECK_THROWS(KeireEditor::PublishMaterialSelection(directory.Root, after, before));
    CHECK(fs.Read(before[0].RelativePath, 65536) == after[0].Source);
    fs.WriteFileAtomically(before[1].RelativePath, after[1].Source);
    REQUIRE_NOTHROW(KeireEditor::PublishMaterialSelection(directory.Root, after, before));
    CHECK(fs.Read(before[0].RelativePath, 65536) == before[0].Source);
    CHECK(fs.Read(before[1].RelativePath, 65536) == before[1].Source);
}

TEST_CASE("material selection Inspector displays mixed values and edits the first displayed value across selection")
{
    class Editor final : public KeireEditor::IPropertyEditor
    {
      public:
        bool Apply = false;
        bool ApplyTiling = false;
        bool ApplyOffset = false;
        std::vector<std::string> Labels;
        bool EditScalar(std::string_view label, double& value, double, std::optional<double>,
                        std::optional<double>) override
        {
            Labels.emplace_back(label);
            if (!Apply || !label.starts_with("Amount"))
                return false;
            value = 0.3;
            return true;
        }
        bool EditBoolean(std::string_view label, bool& value) override
        {
            Labels.emplace_back(label);
            if (!Apply)
                return false;
            value = false;
            return true;
        }
        bool EditChoice(std::string_view label, std::int64_t&, std::span<const std::string_view>) override
        {
            Labels.emplace_back(label);
            return false;
        }
        bool EditInteger(std::string_view, std::int64_t&, double, std::optional<double>, std::optional<double>) override
        {
            return false;
        }
        bool EditText(std::string_view, std::string&) override { return false; }
        bool EditVector2(std::string_view label, Keire::Vector2& value, double) override
        {
            Labels.emplace_back(label);
            if (ApplyTiling && label.starts_with("Tiling"))
                value = {2.0F, 3.0F};
            else if (ApplyOffset && label.starts_with("Offset"))
                value = {0.1F, 0.2F};
            else
                return false;
            return true;
        }
        bool EditVector3(std::string_view, Keire::Vector3&, double) override { return false; }
        bool EditVector4(std::string_view label, Keire::Vector4&, double) override
        {
            Labels.emplace_back(label);
            return false;
        }
        bool EditQuaternion(std::string_view, Keire::Quaternion&, double) override { return false; }
        bool EditColor(std::string_view, Keire::Color&) override { return false; }
        bool EditAsset(std::string_view, Keire::AssetId&, std::optional<Keire::AssetTypeId>, std::string_view) override
        {
            return false;
        }
        bool EditEntity(std::string_view, Keire::EntityId&) override { return false; }
        bool EditEvent(std::string_view, Keire::ComponentEventValue&, std::size_t) override { return false; }
    } editor;
    SelectionFixture fixture;
    auto first = fixture.Document(0.3F);
    auto second = fixture.Document(0.7F);
    auto surface = second.Surface();
    surface.DoubleSided = true;
    REQUIRE(second.SetSurface(surface));
    KeireEditor::MaterialSelectionDocument selection({first, second});
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, selection));
    const auto scalarLabel = "Amount (Mixed)###material-property-" + fixture.Property.Id.ToString();
    CHECK(std::ranges::find(editor.Labels, scalarLabel) != editor.Labels.end());
    CHECK(std::ranges::find(editor.Labels, "Double Sided (Mixed)###Double Sided") != editor.Labels.end());
    editor.Apply = true;
    REQUIRE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, selection));
    for (const auto& document : selection.Documents())
    {
        CHECK(std::get<float>(document.Property("Amount")) == 0.3F);
        CHECK_FALSE(document.Surface().DoubleSided);
    }
    editor.Apply = false;
    editor.Labels.clear();
    CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, selection));
    CHECK(std::ranges::find(editor.Labels, "Amount###material-property-" + fixture.Property.Id.ToString()) !=
          editor.Labels.end());

    SUBCASE("texture transform controls preserve independently mixed tiling and offset")
    {
        Keire::ShaderAssetDefinition shader;
        shader.Source = "TextureTransforms.hlsl";
        Keire::ShaderPropertyDefinition transform;
        transform.Name = "TextureST";
        transform.Type = Keire::ShaderPropertyType::Vector4;
        transform.Id = Keire::AssetId::Generate();
        transform.DefaultValue = {1.0F, 1.0F, 0.0F, 0.0F};
        Keire::ShaderPropertyDefinition texture;
        texture.Name = "Albedo";
        texture.Type = Keire::ShaderPropertyType::Texture2D;
        texture.Id = Keire::AssetId::Generate();
        texture.TextureTransformProperty = transform.Id;
        shader.Properties = {texture, transform};
        const auto create = [&](Keire::Vector4 value)
        {
            Keire::MaterialAuthoringDefinition source;
            source.SchemaVersion = 5;
            source.Shader.Asset = fixture.Shader;
            source.PropertyOverrides = {{transform.Id, transform.Name, value}};
            KeireEditor::MaterialDocument document;
            document.OpenAsset(Keire::AssetId::Generate(), "Texture.keirematerial",
                               Keire::MaterialAsset::EncodeAuthoringSource(source),
                               [&](Keire::AssetId) { return std::optional(shader); });
            return document;
        };
        SUBCASE("invalid transform edits are rejected before publishing any selected material")
        {
            int publications = 0;
            auto bounded = shader;
            bounded.Properties.back().Maximum = 1.0F;
            auto firstTexture = create({1.0F, 1.0F, 0.3F, 0.4F});
            auto secondTexture = create({1.0F, 1.0F, 0.6F, 0.7F});
            secondTexture.Open(secondTexture.SaveSource(), [&](Keire::AssetId) { return std::optional(bounded); });
            const auto original = firstTexture.SaveSource();
            KeireEditor::MaterialSelectionDocument boundedSelection({firstTexture, secondTexture},
                [&](const auto, const auto) { ++publications; });
            CHECK_THROWS_AS((void)boundedSelection.SetTextureTransform(transform, Keire::Vector2{2.0F, 3.0F},
                                                                       std::nullopt), std::invalid_argument);
            CHECK(publications == 0);
            CHECK(boundedSelection.Documents().front().SaveSource() == original);
            CHECK_FALSE(boundedSelection.CanUndo());
            CHECK_FALSE(boundedSelection.SetTextureTransform(transform, std::nullopt, std::nullopt));
        }
        KeireEditor::MaterialSelectionDocument textures(
            {create({1.0F, 1.0F, 0.3F, 0.4F}), create({4.0F, 5.0F, 0.6F, 0.7F})});
        editor.Labels.clear();
        CHECK_FALSE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, textures));
        CHECK(std::ranges::find(editor.Labels, "Tiling (Mixed)###material-tiling-" + texture.Id.ToString()) !=
              editor.Labels.end());
        CHECK(std::ranges::find(editor.Labels, "Offset (Mixed)###material-offset-" + texture.Id.ToString()) !=
              editor.Labels.end());
        CHECK_FALSE(std::ranges::any_of(editor.Labels, [](const auto& label) { return label.starts_with("TextureST"); }));
        editor.ApplyTiling = true;
        REQUIRE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, textures));
        CHECK(std::get<Keire::Vector4>(textures.Documents()[0].Property(transform.Name)) ==
              Keire::Vector4{2.0F, 3.0F, 0.3F, 0.4F});
        CHECK(std::get<Keire::Vector4>(textures.Documents()[1].Property(transform.Name)) ==
              Keire::Vector4{2.0F, 3.0F, 0.6F, 0.7F});
        REQUIRE(textures.Undo());
        CHECK(std::get<Keire::Vector4>(textures.Documents()[1].Property(transform.Name)).X == 4.0F);
        REQUIRE(textures.Redo());
        editor.ApplyTiling = false;
        editor.ApplyOffset = true;
        REQUIRE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, textures));
        CHECK_FALSE(textures.Property(transform)->Mixed);
        auto single = create({4.0F, 5.0F, 0.6F, 0.7F});
        REQUIRE(KeireEditor::MaterialInspectorPanel{}.Draw(editor, single));
        CHECK(std::get<Keire::Vector4>(single.Property(transform.Name)) == Keire::Vector4{4.0F, 5.0F, 0.1F, 0.2F});
        REQUIRE(single.ResetProperty(transform.Name));
        CHECK(std::get<Keire::Vector4>(single.Property(transform.Name)) == transform.DefaultValue);
    }
}
