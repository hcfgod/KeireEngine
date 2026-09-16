#include "Keire/Rendering/ProgramArtifact.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <vector>

TEST_CASE("compute graph output lowers to a bounded reflected storage kernel")
{
    auto graph = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute);
    const auto color = std::ranges::find(graph.Nodes.front().Pins, "Color", &Keire::ShaderGraphPin::Name);
    REQUIRE(color != graph.Nodes.front().Pins.end());
    color->DefaultValue = Keire::Color{0.25F, 0.5F, 0.75F, 1.0F};
    const auto artifact = Keire::CompileShaderGraphProgram(graph);
    REQUIRE(artifact.Succeeded());
    CHECK_NOTHROW(Keire::ValidateProgramArtifact(artifact));
    CHECK_THROWS_AS(Keire::ValidateCookedProgramArtifact(artifact), std::invalid_argument);
    REQUIRE(artifact.Reflection.Resources.size() == 1);
    CHECK(artifact.Reflection.Resources.front().Kind == Keire::ProgramResourceKind::StorageBuffer);
    CHECK(artifact.Reflection.Resources.front().Access == Keire::ProgramResourceAccess::ReadWrite);
    CHECK(artifact.Reflection.Resources.front().Space == 1);
    CHECK(artifact.Reflection.Resources.front().Binding == 0);
    CHECK(artifact.Reflection.Resources.front().StrideBytes == 16);
    const auto& hlsl = artifact.Variants.front().Hlsl;
    CHECK(hlsl.find("register(u0, space1)") != std::string::npos);
    CHECK(hlsl.find("[numthreads(64, 1, 1)]") != std::string::npos);
    CHECK(hlsl.find("dispatchThreadId.x >= elementCount") != std::string::npos);
    CHECK(hlsl.find("dispatchThreadId.y != 0 || dispatchThreadId.z != 0") != std::string::npos);
    CHECK(hlsl.find("VSMain") == std::string::npos);
    CHECK(hlsl.find("PSMain") == std::string::npos);
    CHECK(artifact.Variants.front().Manifest.find("\"compute\": \"CSMain\"") != std::string::npos);

    auto malformed = artifact;
    malformed.Reflection.ThreadGroupSizeX = 0;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(malformed), std::invalid_argument);
    malformed = artifact;
    malformed.Reflection.ThreadGroupSizeX = 1024;
    malformed.Reflection.ThreadGroupSizeY = 1024;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(malformed), std::invalid_argument);
    malformed = artifact;
    malformed.Reflection.Resources.front().StrideBytes = 3;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(malformed), std::invalid_argument);
    malformed = artifact;
    malformed.Reflection.Resources.front().Stages = Keire::ProgramStage::Fragment;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(malformed), std::invalid_argument);
    malformed = artifact;
    malformed.Reflection.EntryPoints.clear();
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(malformed), std::invalid_argument);
}

TEST_CASE("compute graph rejects unsupported dimensions and graphics inputs")
{
    auto graph = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute);
    graph.Target.ThreadGroupSizeY = 2;
    CHECK_FALSE(Keire::CompileShaderGraphProgram(graph).Succeeded());
    graph.Target.ThreadGroupSizeY = 1;
    graph.Nodes.push_back(Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldPosition));
    CHECK_FALSE(Keire::CompileShaderGraphProgram(graph).Succeeded());
}

TEST_CASE("compute binary validation rejects mismatched kernel reflection")
{
    auto artifact = Keire::CompileShaderGraphProgram(Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute));
    REQUIRE(artifact.Succeeded());
    // A digest-valid payload tests the container contract; it is never submitted as executable shader code.
    artifact.Variants.front().Binaries.push_back({"primary",
                                                  Keire::ProgramBackend::D3D12,
                                                  Keire::ProgramBinaryFormat::Dxil,
                                                  Keire::ProgramStage::Compute,
                                                  "CSMain",
                                                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                                  {std::byte{0x61}, std::byte{0x62}, std::byte{0x63}},
                                                  artifact.Reflection});
    CHECK_NOTHROW(Keire::ValidateCookedProgramArtifact(artifact));
    artifact.Variants.front().Binaries.front().Reflection.ThreadGroupSizeX = 32;
    CHECK_THROWS_AS(Keire::ValidateCookedProgramArtifact(artifact), std::invalid_argument);
}

TEST_CASE("standalone OpenPBR materials round trip and compile to reflected multi-pass programs")
{
    const auto material = Keire::CreateOpenPbrMaterial();
    CHECK_FALSE(material.Shader.Asset);
    CHECK(material.Domain == Keire::MaterialDomain::Surface);
    CHECK(material.ShadingModel == Keire::MaterialShadingModel::OpenPbrLit);
    CHECK(material.AuthoringMode == Keire::MaterialAuthoringMode::SimpleSurface);
    CHECK(material.MaximumClosures == Keire::MaximumMaterialClosureCount);
    CHECK(material.SurfaceGraph.Target.Target == Keire::ShaderGraphTarget::Material);
    CHECK(std::ranges::count(material.SurfaceGraph.Nodes, Keire::ShaderGraphNodeKind::Parameter,
                             &Keire::ShaderGraphNode::Kind) == 5);

    const auto source = Keire::MaterialGraphAsset::EncodeSource(material);
    CHECK(Keire::MaterialGraphAsset::DecodeSource(source) == material);

    const auto artifact = Keire::CompileMaterialProgram(material);
    REQUIRE(artifact.Succeeded());
    CHECK(artifact.Program.Target == Keire::ProgramTarget::Material);
    CHECK(Keire::HasProgramStage(artifact.Program.Stages, Keire::ProgramStage::Vertex));
    CHECK(Keire::HasProgramStage(artifact.Program.Stages, Keire::ProgramStage::Fragment));
    CHECK(artifact.Program.Reflection.Properties.size() == 5);
    CHECK(std::ranges::any_of(artifact.Passes, [](const Keire::MaterialPassContract& pass)
                              { return pass.Pass == Keire::MaterialPass::DeferredGBufferStandard; }));
    CHECK(std::ranges::none_of(artifact.Passes, [](const Keire::MaterialPassContract& pass)
                               { return pass.Pass == Keire::MaterialPass::DeferredGBufferExtended; }));
    CHECK(std::ranges::any_of(artifact.Passes, [](const Keire::MaterialPassContract& pass)
                              { return pass.Pass == Keire::MaterialPass::BakeSurface; }));
    CHECK(std::ranges::any_of(artifact.Passes, [](const Keire::MaterialPassContract& pass)
                              { return pass.Pass == Keire::MaterialPass::SelectionId; }));
}

TEST_CASE("program artifacts expose target stages entry points and variant policy diagnostics")
{
    auto fullscreen = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Fullscreen);
    const auto fullscreenArtifact = Keire::CompileShaderGraphProgram(fullscreen);
    REQUIRE(fullscreenArtifact.Succeeded());
    CHECK(fullscreenArtifact.Target == Keire::ProgramTarget::Fullscreen);
    CHECK(fullscreenArtifact.Reflection.EntryPoints ==
          std::vector<Keire::ProgramEntryPoint>{{Keire::ProgramStage::Vertex, "VSMain"},
                                                {Keire::ProgramStage::Fragment, "PSMain"}});

    auto variants = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Material);
    variants.Keywords.push_back({"DETAIL", {}, "false", true});
    Keire::ProgramCompileOptions options;
    options.VariantWarningThreshold = 1;
    options.MaximumVariants = 2;
    const auto variantArtifact = Keire::CompileShaderGraphProgram(variants, options);
    REQUIRE(variantArtifact.Succeeded());
    CHECK(variantArtifact.Variants.size() == 2);
    CHECK(std::ranges::any_of(variantArtifact.Diagnostics, [](const Keire::ShaderGraphDiagnostic& diagnostic)
                              { return diagnostic.Code == "PRG1001"; }));

    options.MaximumVariants = Keire::ProgramVariantHardLimit + 1;
    const auto rejected = Keire::CompileShaderGraphProgram(variants, options);
    CHECK_FALSE(rejected.Succeeded());
    REQUIRE(rejected.Diagnostics.size() == 1);
    CHECK(rejected.Diagnostics.front().Code == "PRG0001");
}

TEST_CASE("material pass contracts distinguish decals volumes opaque and transparent surfaces")
{
    const auto decal = Keire::BuildMaterialPassContract(
        Keire::MaterialDomain::Decal, Keire::MaterialShadingModel::OpenPbrLit, Keire::MaterialAlphaMode::Opaque);
    CHECK(decal == std::vector<Keire::MaterialPassContract>{{Keire::MaterialPass::DecalDBuffer},
                                                            {Keire::MaterialPass::SelectionId}});

    const auto volume =
        Keire::BuildMaterialPassContract(Keire::MaterialDomain::Volume, Keire::MaterialShadingModel::ParticipatingMedia,
                                         Keire::MaterialAlphaMode::Blend);
    CHECK(volume.front().Pass == Keire::MaterialPass::VolumeInject);
    CHECK(std::ranges::none_of(volume, [](const Keire::MaterialPassContract& pass)
                               { return pass.Pass == Keire::MaterialPass::DeferredGBufferStandard; }));

    const auto transparent = Keire::BuildMaterialPassContract(
        Keire::MaterialDomain::Surface, Keire::MaterialShadingModel::ThinTranslucent, Keire::MaterialAlphaMode::Opaque);
    CHECK(transparent.front().Pass == Keire::MaterialPass::ForwardTransparent);
    CHECK(transparent[1].Pass == Keire::MaterialPass::ShadowTransmittance);

    const auto hair = Keire::BuildMaterialPassContract(
        Keire::MaterialDomain::Surface, Keire::MaterialShadingModel::Hair, Keire::MaterialAlphaMode::Mask);
    CHECK(std::ranges::any_of(hair, [](const Keire::MaterialPassContract& pass)
                              { return pass.Pass == Keire::MaterialPass::ForwardOpaque; }));
    CHECK(std::ranges::none_of(hair,
                               [](const Keire::MaterialPassContract& pass)
                               {
                                   return pass.Pass == Keire::MaterialPass::DeferredGBufferStandard ||
                                          pass.Pass == Keire::MaterialPass::DeferredGBufferExtended;
                               }));

    const auto layered =
        Keire::BuildMaterialPassContract(Keire::MaterialDomain::Surface, Keire::MaterialShadingModel::OpenPbrLit,
                                         Keire::MaterialAlphaMode::Opaque, Keire::MaterialAuthoringMode::LayerStack);
    CHECK(std::ranges::any_of(layered, [](const Keire::MaterialPassContract& pass)
                              { return pass.Pass == Keire::MaterialPass::DeferredGBufferExtended; }));
    CHECK(std::ranges::none_of(layered, [](const Keire::MaterialPassContract& pass)
                               { return pass.Pass == Keire::MaterialPass::DeferredGBufferStandard; }));
}

TEST_CASE("material source contracts reject incompatible domains and closure bounds")
{
    CHECK_THROWS_AS(
        (void)Keire::CreateOpenPbrMaterial(Keire::MaterialShadingModel::OpenPbrLit, Keire::MaterialDomain::Volume),
        std::invalid_argument);
    auto material = Keire::CreateOpenPbrMaterial();
    material.MaximumClosures = Keire::MaximumMaterialClosureCount + 1;
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(material), std::invalid_argument);
}

TEST_CASE("cooked material programs require digest-verified complete pass stage backend lanes")
{
    auto artifact = Keire::CompileMaterialProgram(Keire::CreateOpenPbrMaterial());
    REQUIRE(artifact.Succeeded());
    REQUIRE(artifact.Program.Variants.size() == 1);
    constexpr std::string_view digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::vector<std::byte> bytes{std::byte{0x61}, std::byte{0x62}, std::byte{0x63}};
    for (const auto& pass : artifact.Passes)
    {
        for (const auto stage : {Keire::ProgramStage::Vertex, Keire::ProgramStage::Fragment})
        {
            Keire::ProgramReflection reflection;
            const auto entryPoint = stage == Keire::ProgramStage::Vertex ? "VSMain" : "PSMain";
            reflection.EntryPoints.push_back({stage, entryPoint});
            artifact.Program.Variants.front().Binaries.push_back({std::string(Keire::MaterialPassName(pass.Pass)),
                                                                  Keire::ProgramBackend::D3D12,
                                                                  Keire::ProgramBinaryFormat::Dxil, stage, entryPoint,
                                                                  std::string(digest), bytes, std::move(reflection)});
        }
    }
    CHECK_NOTHROW(Keire::ValidateCookedMaterialProgramArtifact(artifact));

    auto corrupted = artifact;
    corrupted.Program.Variants.front().Binaries.front().Bytes.front() = std::byte{0x7a};
    CHECK_THROWS_AS(Keire::ValidateCookedMaterialProgramArtifact(corrupted), std::invalid_argument);

    auto incomplete = artifact;
    incomplete.Program.Variants.front().Binaries.pop_back();
    CHECK_THROWS_AS(Keire::ValidateCookedMaterialProgramArtifact(incomplete), std::invalid_argument);
}

TEST_CASE("graphics program reflection rejects invalid stages entries and resources")
{
    const auto valid = Keire::CompileShaderGraphProgram(Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Ui));
    REQUIRE(valid.Succeeded());
    CHECK_NOTHROW(Keire::ValidateProgramArtifact(valid));
    auto invalid = valid;
    invalid.Stages = static_cast<Keire::ProgramStage>(0x83);
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = valid;
    invalid.Reflection.EntryPoints.pop_back();
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = valid;
    invalid.Reflection.EntryPoints.push_back(invalid.Reflection.EntryPoints.front());
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = valid;
    invalid.Reflection.EntryPoints.front().Name = "invalid entry";
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    auto withResource = valid;
    withResource.Reflection.Resources.push_back(
        {{}, "Texture", "Texture", Keire::ProgramResourceKind::SampledTexture2D});
    CHECK_NOTHROW(Keire::ValidateProgramArtifact(withResource));
    invalid = withResource;
    invalid.Reflection.Resources.back().Stages = Keire::ProgramStage::Compute;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = withResource;
    invalid.Reflection.Resources.back().Access = Keire::ProgramResourceAccess::WriteOnly;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = withResource;
    invalid.Reflection.Resources.back().Kind = static_cast<Keire::ProgramResourceKind>(255);
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
    invalid = withResource;
    invalid.Reflection.Resources.back().Stages = Keire::ProgramStage::None;
    CHECK_THROWS_AS(Keire::ValidateProgramArtifact(invalid), std::invalid_argument);
}

TEST_CASE("shader reflection preserves targets keywords and stable texture transform bindings")
{
    auto graph = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Vfx);
    auto transform = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Vector4);
    transform.Symbol = "AlbedoTransform";
    transform.Value = Keire::Vector4{1.0F, 1.0F, 0.0F, 0.0F};
    auto texture = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Texture2D);
    texture.Symbol = "Albedo";
    texture.TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    texture.ParameterMetadata.TextureTransformProperty = transform.Id;
    graph.Nodes.push_back(transform);
    graph.Nodes.push_back(texture);
    graph.Keywords.push_back({"DETAIL", {}, "false", true});
    const auto compiled = Keire::CompileShaderGraphProgram(graph);
    REQUIRE(compiled.Succeeded());
    const auto& manifest = compiled.Variants.front().Manifest;
    auto definition = Keire::ShaderAsset::DecodeManifest(std::as_bytes(std::span(manifest)));
    CHECK(definition.ProgramTarget == "VFX");
    CHECK(definition.Keywords == std::vector<std::string>{"DETAIL"});
    const auto reflected = std::ranges::find(definition.Properties, texture.Id, &Keire::ShaderPropertyDefinition::Id);
    REQUIRE(reflected != definition.Properties.end());
    CHECK(reflected->TextureTransformProperty == transform.Id);
    CHECK(reflected->TextureSemantic == Keire::ShaderTextureSemantic::BaseColor);
    for (const auto format : {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
        definition.Variants.push_back({format, {std::byte{1}}, {std::byte{2}}});
    const auto encoded = Keire::ShaderAsset::Encode(definition);
    const auto roundTrip = Keire::ShaderAsset::Decode(encoded);
    CHECK(roundTrip->Definition().Properties == definition.Properties);
    CHECK(roundTrip->Definition().ProgramTarget == definition.ProgramTarget);
    CHECK(roundTrip->Definition().Keywords == definition.Keywords);
    auto invalid = definition;
    invalid.Properties[static_cast<std::size_t>(reflected - definition.Properties.begin())].TextureTransformProperty = Keire::AssetId::Generate();
    CHECK_THROWS_AS(Keire::ShaderAsset::Encode(invalid), std::invalid_argument);
    invalid = definition;
    invalid.Keywords.push_back("DETAIL");
    CHECK_THROWS_AS(Keire::ShaderAsset::Encode(invalid), std::invalid_argument);
    invalid = definition;
    invalid.ProgramTarget = "Compute";
    CHECK_THROWS_AS(Keire::ShaderAsset::Encode(invalid), std::invalid_argument);
}
