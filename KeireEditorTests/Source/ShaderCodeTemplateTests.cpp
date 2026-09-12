#include "KeireClient/Editor/ShaderCodeTemplate.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

TEST_CASE("Shader Code Unlit template exposes a used Tint property with the common graphics contract")
{
    const auto shader = KeireEditor::CreateUnlitShaderCodeTemplate("Assets/Shaders/Unicode \"Tint\".hlsl");
    const auto manifest = nlohmann::json::parse(shader.Manifest);
    CHECK(manifest.at("source") == "Assets/Shaders/Unicode \"Tint\".hlsl");
    REQUIRE(manifest.at("properties").size() == 1);
    const auto& tint = manifest.at("properties").front();
    CHECK(tint.at("name") == "Tint");
    CHECK(tint.at("type") == "Color");
    CHECK(tint.at("default") == nlohmann::json::array({0.25, 0.55F, 1.0, 1.0}));
    CHECK(shader.Hlsl.find("Tint") != std::string::npos);
    CHECK(shader.Hlsl.find("graphBaseColor = _KeireMaterial_Tint") != std::string::npos);
    CHECK(shader.Hlsl.find("MaterialData") != std::string::npos);
    CHECK(manifest.at("stages").at("vertex") == "VSMain");
    CHECK(manifest.at("stages").at("fragment") == "PSMain");
    CHECK(manifest.contains("passes"));
}

TEST_CASE("Shader Code Unlit template rejects invalid source locations before authoring")
{
    CHECK_THROWS_AS(KeireEditor::CreateUnlitShaderCodeTemplate({}), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::CreateUnlitShaderCodeTemplate("../Escape.hlsl"), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::CreateUnlitShaderCodeTemplate("Assets/../../Escape.hlsl"), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::CreateUnlitShaderCodeTemplate("Assets/Shader.txt"), std::invalid_argument);
    CHECK_THROWS_AS(KeireEditor::CreateUnlitShaderCodeTemplate(std::filesystem::current_path() / "Shader.hlsl"),
                    std::invalid_argument);
}
