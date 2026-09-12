#include "KeireClient/Editor/ShaderGraphSourceView.h"

#include <doctest/doctest.h>

#include <ostream>
#include <string_view>

TEST_CASE("Shader Graph source pages preserve physical lines and bound displayed work")
{
    const std::string_view source = "first\r\n\r\nthird\nfourth";
    const auto page = KeireEditor::ShaderGraphSourcePage(source, 2, 2);
    REQUIRE(page.size() == 2);
    CHECK(page[0].Number == 2);
    CHECK(page[0].Text.empty());
    CHECK(page[1].Number == 3);
    CHECK(page[1].Text == "third");
    CHECK(page[1].Text.data() == source.data() + 9);
    CHECK(KeireEditor::ShaderGraphSourcePage(source, 0, 1).front().Number == 1);
    CHECK(KeireEditor::ShaderGraphSourcePage(source, 99).empty());
    CHECK(KeireEditor::ShaderGraphSourcePage(source, 1, 0).empty());
    CHECK(KeireEditor::ShaderGraphSourcePage({}, 1).empty());
    const auto final = KeireEditor::ShaderGraphSourcePage(source, 4);
    REQUIRE(final.size() == 1);
    CHECK(final.front().Text == "fourth");
    CHECK(KeireEditor::ShaderGraphSourcePage("one\n", 1).size() == 1);
}

TEST_CASE("Shader Graph source symbol navigation matches whole identifiers")
{
    const std::string_view source = "float RoughnessScale;\nfloat _Roughness;\nfloat Roughness = 0.5;\n";
    CHECK(KeireEditor::FindShaderGraphSourceSymbol(source, "Roughness") == 3);
    CHECK(KeireEditor::FindShaderGraphSourceSymbol(source, "RoughnessScale") == 1);
    CHECK(KeireEditor::FindShaderGraphSourceSymbol("Value", "Value") == 1);
    CHECK_FALSE(KeireEditor::FindShaderGraphSourceSymbol(source, "Missing"));
    CHECK_FALSE(KeireEditor::FindShaderGraphSourceSymbol(source, ""));
    CHECK_FALSE(KeireEditor::FindShaderGraphSourceSymbol("Value2 _Value Value_", "Value"));
}
