#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

TEST_CASE("fullscreen effect presets compile their authored scene sampling through the production importer")
{
    for (const auto preset :
         {Keire::ShaderGraphTemplate::FullscreenBlur, Keire::ShaderGraphTemplate::FullscreenChromaticAberration,
          Keire::ShaderGraphTemplate::FullscreenDistortion, Keire::ShaderGraphTemplate::FullscreenVignette})
    {
        INFO(static_cast<int>(preset));
        const auto graph = Keire::CreateShaderGraphTemplate(preset);
        CHECK(graph.Target.Target == Keire::ShaderGraphTarget::Fullscreen);
        const auto roundtrip = Keire::ShaderGraphAsset::DecodeSource(Keire::ShaderGraphAsset::EncodeSource(graph));
        CHECK(roundtrip == graph);
        Keire::ShaderGraphCompileOptions options;
        options.GeneratedSource = "Assets/Generated/FullscreenPreset.hlsl";
        const auto compiled = Keire::CompileShaderGraph(roundtrip, options);
        for (const auto& diagnostic : compiled.Diagnostics)
            INFO(diagnostic.Message);
        REQUIRE(compiled.Succeeded());
        REQUIRE(compiled.Variants.size() == 1);
        const auto& variant = compiled.Variants.front();
        Keire::AssetImportContext context;
        context.ProjectRoot = std::filesystem::current_path();
        context.SourceRoot = context.ProjectRoot / "Assets";
        context.SourcePath = context.SourceRoot / "FullscreenPreset.keireshader";
        context.RelativePath = "FullscreenPreset.keireshader";
        context.ReadProjectFile = [&](const std::filesystem::path& path)
        {
            if (path.lexically_normal() != variant.GeneratedSource.lexically_normal())
                throw std::runtime_error("Unexpected fullscreen template include: " + path.generic_string());
            const auto bytes = std::as_bytes(std::span(variant.Hlsl));
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        };
        const auto imported =
            Keire::CreateShaderAssetImporter().ContextualImport(context, std::as_bytes(std::span(variant.Manifest)));
        CHECK(imported.Diagnostics.empty());
        const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
        CHECK(shader->Definition().ProgramTarget == "Fullscreen");
        CHECK_FALSE(shader->Definition().Properties.empty());
        for (const auto format :
             {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
            CHECK(shader->Variant(format, "primary") != nullptr);
    }
}

TEST_CASE("UI shader importer accepts perspective and legacy vertex inputs and rejects incompatible attributes")
{
    const auto compiled = Keire::CompileShaderGraph(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Ui));
    REQUIRE(compiled.Succeeded());
    REQUIRE(compiled.Variants.size() == 1);
    const auto& variant = compiled.Variants.front();
    std::string source = variant.Hlsl;
    Keire::AssetImportContext context;
    context.ProjectRoot = std::filesystem::current_path();
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.SourcePath = context.SourceRoot / "PerspectiveUi.keireshader";
    context.RelativePath = "PerspectiveUi.keireshader";
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        if (path.lexically_normal() != variant.GeneratedSource.lexically_normal())
            throw std::runtime_error("Unexpected UI shader include.");
        const auto bytes = std::as_bytes(std::span(source));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const auto import = [&]
    {
        return Keire::CreateShaderAssetImporter().ContextualImport(context, std::as_bytes(std::span(variant.Manifest)));
    };
    const auto perspective = Keire::ShaderAsset::Decode(import().Bytes);
    CHECK(perspective->Definition().VertexLayoutVersion == Keire::UiShaderVertexLayoutVersion);
    for (const auto format :
         {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
        CHECK(perspective->Variant(format, "primary") != nullptr);

    const std::string declaration = "float PerspectiveW : TEXCOORD3;";
    const std::string restoration = "output.Position *= input.PerspectiveW;";
    REQUIRE(source.find(declaration) != std::string::npos);
    REQUIRE(source.find(restoration) != std::string::npos);
    source.erase(source.find(restoration), restoration.size());
    source.erase(source.find(declaration), declaration.size());
    CHECK_NOTHROW(import());

    source = variant.Hlsl;
    source.replace(source.find(declaration), declaration.size(), "float2 PerspectiveW : TEXCOORD3;");
    source.replace(source.find(restoration), restoration.size(), "output.Position *= input.PerspectiveW.x;");
    CHECK_THROWS_WITH_AS(import(), "Shader vertex inputs do not match the fixed mesh ABI.", std::invalid_argument);
}
