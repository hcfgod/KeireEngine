#include "KeireClient/Editor/MaterialDocument.h"

#include <doctest/doctest.h>

TEST_CASE("material undo revisions resolve graph shaders and retain last-good on unavailable references")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.Shader.Keywords.emplace("DETAIL", "true");
    source.Properties.emplace("Roughness", 0.2F);
    source.Surface.DoubleSided = true;
    source.ContributeEmissionToGI = false;
    source.EmissiveGIIntensity = 2.0F;
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    const auto runtimeShader = Keire::AssetId::Generate();
    const auto revision =
        KeireEditor::MaterialDocument::ResolveRuntimeRevision(bytes,
                                                              [&](const Keire::MaterialShaderReference& reference)
                                                              {
                                                                  CHECK(reference == source.Shader);
                                                                  return runtimeShader;
                                                              });
    REQUIRE(revision);
    CHECK(revision->Shader == runtimeShader);
    CHECK(revision->Shader != source.Shader.Asset);
    CHECK(revision->Properties == source.Properties);
    CHECK(revision->Surface == source.Surface);
    CHECK_FALSE(revision->ContributeEmissionToGI);
    CHECK(revision->EmissiveGIIntensity == 2.0F);
    CHECK_FALSE(
        KeireEditor::MaterialDocument::ResolveRuntimeRevision(bytes, [](const auto&) { return Keire::AssetId{}; }));
    source.Shader = {};
    CHECK(KeireEditor::MaterialDocument::ResolveRuntimeRevision(
        Keire::MaterialAsset::EncodeAuthoringSource(source),
        [](const auto&) -> Keire::AssetId
        {
            FAIL("An unassigned shader must not invoke its resolver.");
            return {};
        }));
}
