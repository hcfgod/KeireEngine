#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <span>
#include <stdexcept>
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
