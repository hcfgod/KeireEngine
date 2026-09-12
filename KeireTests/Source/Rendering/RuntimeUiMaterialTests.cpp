#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"
#include "KeireInternal/Rendering/RuntimeUiMaterialInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>

TEST_CASE("Runtime UI material values bind defaults and typed overrides without shader compilation")
{
    Keire::ShaderAssetDefinition shader;
    shader.VertexLayoutVersion = Keire::UiShaderVertexLayoutVersion;
    shader.Properties = {
        {.Name = "Amount", .Type = Keire::ShaderPropertyType::Scalar, .DefaultValue = {0.25F, 0, 0, 0}},
        {.Name = "Tint", .Type = Keire::ShaderPropertyType::Color, .DefaultValue = {1, 1, 1, 1}},
        {.Name = "Image", .Type = Keire::ShaderPropertyType::Texture2D, .DefaultTexture = {1, 2}}};
    Keire::MaterialAssetDefinition material;
    const auto defaults = Keire::Detail::BuildRuntimeUiMaterialValues(shader, material);
    REQUIRE(defaults.Numeric.size() == 2U);
    CHECK(defaults.Numeric[0].X == 0.25F);
    REQUIRE(defaults.Textures.size() == 1U);
    CHECK(defaults.Textures[0] == Keire::AssetId(1, 2));
    material.Properties["Amount"] = 0.75F;
    material.Properties["Tint"] = Keire::Color{0.1F, 0.2F, 0.3F, 0.4F};
    material.Properties["Image"] = Keire::AssetId(3, 4);
    const auto overridden = Keire::Detail::BuildRuntimeUiMaterialValues(shader, material);
    CHECK(overridden.Numeric[0].X == 0.75F);
    CHECK(overridden.Numeric[1].W == 0.4F);
    CHECK(overridden.Textures[0] == Keire::AssetId(3, 4));
    CHECK(shader.Properties[0].DefaultValue.X == 0.25F);
    material.Properties["Amount"] = Keire::Vector2{};
    CHECK_THROWS_AS(Keire::Detail::BuildRuntimeUiMaterialValues(shader, material), std::invalid_argument);
    material.Properties["Amount"] = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(Keire::Detail::BuildRuntimeUiMaterialValues(shader, material), std::invalid_argument);
    material.Properties.clear();
    material.Properties["Missing"] = 1.0F;
    CHECK_THROWS_AS(Keire::Detail::BuildRuntimeUiMaterialValues(shader, material), std::invalid_argument);
}

TEST_CASE("Runtime UI materials reject incompatible resources and reserve the source image sampler")
{
    Keire::ShaderAssetDefinition shader;
    CHECK_THROWS_AS(Keire::Detail::ValidateRuntimeUiShader(shader), std::invalid_argument);
    shader.VertexLayoutVersion = Keire::UiShaderVertexLayoutVersion;
    CHECK_NOTHROW(Keire::Detail::ValidateRuntimeUiShader(shader));
    shader.UsesInstancing = true;
    CHECK_THROWS_AS(Keire::Detail::ValidateRuntimeUiShader(shader), std::invalid_argument);
    shader.UsesInstancing = false;
    shader.UserReadOnlyBuffers = 1;
    CHECK_THROWS_AS(Keire::Detail::ValidateRuntimeUiShader(shader), std::invalid_argument);
    shader.UserReadOnlyBuffers = 0;
    shader.Properties.resize(15);
    for (auto& property : shader.Properties)
        property.Type = Keire::ShaderPropertyType::Texture2D;
    CHECK_NOTHROW(Keire::Detail::ValidateRuntimeUiShader(shader));
    shader.Properties.push_back(shader.Properties.front());
    CHECK_THROWS_AS(Keire::Detail::ValidateRuntimeUiShader(shader), std::invalid_argument);
    shader.Properties.clear();
    const auto sentinel = Keire::Detail::BuildRuntimeUiMaterialValues(shader, {});
    REQUIRE(sentinel.Numeric.size() == 1U);
    CHECK(sentinel.Numeric.front() == Keire::Vector4{});
}

TEST_CASE("Runtime UI geometry preserves material boundaries and quad UV coordinates")
{
    Keire::RuntimeUiDrawCommand quad;
    quad.Rect = {0, 0, 20, 20};
    quad.ClipRect = quad.Rect;
    quad.Material = Keire::AssetId(1, 2);
    std::array commands{quad, quad, quad};
    commands[2].Material = Keire::AssetId(3, 4);
    const auto geometry = Keire::RenderBackend::BuildRuntimeUiGeometry(commands);
    REQUIRE(geometry.Batches.size() == 2U);
    CHECK(geometry.Batches[0].Material == quad.Material);
    CHECK(geometry.Batches[0].VertexCount == 12U);
    CHECK(geometry.Batches[1].Material == commands[2].Material);
    REQUIRE(geometry.Vertices.size() == 18U);
    CHECK(geometry.Vertices[0].UV == Keire::Vector2{});
    CHECK(geometry.Vertices[2].UV == Keire::Vector2{1, 1});
    const auto runs = Keire::RenderBackend::BuildRuntimeUiTextureRuns(commands);
    REQUIRE(runs.size() == 2U);
    CHECK(runs[0].CommandCount == 2U);
    CHECK(runs[1].Material == commands[2].Material);
    commands[2].Material = quad.Material;
    CHECK(Keire::RenderBackend::BuildRuntimeUiGeometry(commands).Batches.size() == 1U);
}
