#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

TEST_CASE("Shader Graph graphics targets retain deliberate raster state")
{
    struct TargetCase
    {
        Keire::ShaderGraphTarget Target;
        std::string_view Culling;
        bool DepthTest;
        bool DepthWrite;
        bool Blend;
    };
    constexpr std::array cases{TargetCase{Keire::ShaderGraphTarget::Ui, "None", false, false, true},
                               TargetCase{Keire::ShaderGraphTarget::Vfx, "None", true, false, true},
                               TargetCase{Keire::ShaderGraphTarget::Fullscreen, "None", false, false, false},
                               TargetCase{Keire::ShaderGraphTarget::CustomGraphics, "Back", true, true, false},
                               TargetCase{Keire::ShaderGraphTarget::Material, "Back", true, true, false}};
    for (const auto& expected : cases)
    {
        INFO(Keire::ShaderGraphTargetName(expected.Target));
        const auto graph = Keire::CreateTargetShaderGraph(expected.Target);
        const auto compilation = Keire::CompileShaderGraph(graph);
        REQUIRE(compilation.Succeeded());
        REQUIRE(compilation.Variants.size() == 1U);
        const auto manifest = nlohmann::json::parse(compilation.Variants.front().Manifest);
        const auto& state = manifest.at("renderState");
        CHECK(state.at("culling").get<std::string>() == expected.Culling);
        CHECK(state.at("depthTest").get<bool>() == expected.DepthTest);
        CHECK(state.at("depthWrite").get<bool>() == expected.DepthWrite);
        CHECK(state.at("blend").get<bool>() == expected.Blend);
        if (expected.Target != Keire::ShaderGraphTarget::Material)
        {
            REQUIRE(manifest.at("passes").size() == (expected.Target == Keire::ShaderGraphTarget::Vfx ? 4U : 1U));
            CHECK(manifest.at("passes")[0].at("role") == "primary");
            if (expected.Target == Keire::ShaderGraphTarget::Vfx)
            {
                CHECK(manifest.at("passes")[1].at("role") == "vfxBillboard");
                CHECK(manifest.at("passes")[2].at("role") == "vfxRibbon");
                CHECK(manifest.at("passes")[3].at("role") == "vfxCpu");
            }
        }
    }
}

TEST_CASE("Shader Graph legacy transparent materials retain face culling")
{
    const auto graph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Transparent);
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 1U);
    const auto manifest = nlohmann::json::parse(compilation.Variants.front().Manifest);
    const auto& state = manifest.at("renderState");
    CHECK(state.at("culling") == "Back");
    CHECK(state.at("depthTest").get<bool>());
    CHECK_FALSE(state.at("depthWrite").get<bool>());
    CHECK(state.at("blend").get<bool>());
    CHECK(manifest.at("passes")[0].at("role") == "forwardTransparent");
}
