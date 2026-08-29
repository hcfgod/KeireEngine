#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <span>
#include <string>

namespace
{
    [[nodiscard]] nlohmann::json Parse(const std::span<const std::byte> bytes)
    {
        return nlohmann::json::parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
} // namespace

TEST_CASE("Shader Graph schema three migrates without disturbing topology and current authoring")
{
    auto definition = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    const auto nodeCount = definition.Nodes.size();
    const auto connectionCount = definition.Connections.size();
    auto source = Parse(Keire::ShaderGraphAsset::EncodeSource(definition));
    source["schemaVersion"] = 3;
    source.erase("authoring");
    const auto legacy = source.dump();

    const auto migrated = Keire::ShaderGraphAsset::DecodeSource(std::as_bytes(std::span(legacy)));
    CHECK(migrated.SchemaVersion == Keire::ShaderGraphSourceSchemaVersion);
    CHECK(migrated.Nodes.size() == nodeCount);
    CHECK(migrated.Connections.size() == connectionCount);
    CHECK(migrated.Authoring == Keire::GraphAuthoringMetadata{});
    const auto saved = Parse(Keire::ShaderGraphAsset::EncodeSource(migrated));
    CHECK(saved.at("schemaVersion") == 6);
    CHECK(saved.at("target") == static_cast<std::uint8_t>(Keire::ShaderGraphTarget::LegacySurface));
    CHECK(saved.at("maximumWorldPositionDisplacementRadius").get<float>() == doctest::Approx(0.0F));
    CHECK(saved.contains("authoring"));
}

TEST_CASE("Shader Graph legacy fullscreen sources infer the explicit fullscreen target")
{
    auto source = Parse(
        Keire::ShaderGraphAsset::EncodeSource(Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Fullscreen)));
    source["schemaVersion"] = 5U;
    source.erase("target");
    source.erase("stages");
    source.erase("fullscreenInjectionPoint");
    source.erase("threadGroupSize");
    const auto legacy = source.dump();

    const auto migrated = Keire::ShaderGraphAsset::DecodeSource(std::as_bytes(std::span(legacy)));
    CHECK(migrated.SchemaVersion == Keire::ShaderGraphSourceSchemaVersion);
    CHECK(migrated.Target.Target == Keire::ShaderGraphTarget::Fullscreen);
    CHECK(migrated.Target.Stages == static_cast<Keire::ShaderGraphShaderStage>(
                                        static_cast<std::uint8_t>(Keire::ShaderGraphShaderStage::Vertex) |
                                        static_cast<std::uint8_t>(Keire::ShaderGraphShaderStage::Fragment)));
}

TEST_CASE("Material Graph future schemas fail before decoding required payload fields")
{
    const auto future = nlohmann::json{{"schemaVersion", Keire::MaterialGraphSourceSchemaVersion + 1U}}.dump();
    CHECK_THROWS_WITH_AS((void)Keire::MaterialGraphAsset::DecodeSource(std::as_bytes(std::span(future))),
                         "Material Graph source schema is unsupported.", std::invalid_argument);
}
