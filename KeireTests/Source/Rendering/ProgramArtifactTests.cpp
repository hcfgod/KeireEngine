#include "Keire/Rendering/ProgramArtifact.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <vector>

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
