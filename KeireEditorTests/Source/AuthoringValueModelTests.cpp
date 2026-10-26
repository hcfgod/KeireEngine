#include "Keire/Math/Curves.h"
#include "KeireClientInternal/Editor/AuthoringValueEditing.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

TEST_CASE("Authoring curve insertion samples values and fills the widest representable gap")
{
    auto curve = Keire::Curve1D::Linear(2.0F, 6.0F);
    REQUIRE(KeireEditor::Detail::InsertAuthoringCurveKey(curve));
    REQUIRE(curve.Keys().size() == 3);
    CHECK(curve.Keys()[1].Time == 0.5F);
    CHECK(curve.Keys()[1].Value == 4.0F);
    CHECK(curve.Evaluate(0.25F) == 3.0F);
    REQUIRE(KeireEditor::Detail::InsertAuthoringCurveKey(curve));
    CHECK(curve.Keys()[1].Time == 0.25F);
    CHECK(curve.Keys()[1].Value == 3.0F);

    Keire::Curve1D empty;
    REQUIRE(KeireEditor::Detail::InsertAuthoringCurveKey(empty, -2.0F, 2.0F));
    CHECK(empty.Keys().front().Time == -2.0F);
    REQUIRE(KeireEditor::Detail::InsertAuthoringCurveKey(empty, -2.0F, 2.0F));
    CHECK(empty.Keys().back().Time == 2.0F);
    CHECK(empty.Keys().back().Value == 0.0F);
}

TEST_CASE("Authoring gradient insertion retains interpolation and sampled colors")
{
    Keire::ColorGradient gradient({{0.0F, {1, 0, 0, 1}}, {1.0F, {0, 0, 1, 0}}});
    REQUIRE(KeireEditor::Detail::InsertAuthoringGradientKey(gradient));
    REQUIRE(gradient.Keys().size() == 3);
    CHECK(gradient.Keys()[1].Time == 0.5F);
    CHECK(gradient.Keys()[1].Value == Keire::Color{0.5F, 0, 0.5F, 0.5F});
    gradient.SetInterpolation(Keire::GradientInterpolation::Constant);
    REQUIRE(KeireEditor::Detail::InsertAuthoringGradientKey(gradient));
    CHECK(gradient.Interpolation() == Keire::GradientInterpolation::Constant);
    CHECK(gradient.Keys()[1].Time == 0.25F);
    CHECK(gradient.Keys()[1].Value == gradient.Keys().front().Value);

    Keire::ColorGradient empty;
    REQUIRE(KeireEditor::Detail::InsertAuthoringGradientKey(empty));
    CHECK(empty.Keys().front().Time == 0.0F);
    CHECK(empty.Keys().front().Value == Keire::Color{});
}

TEST_CASE("Authoring key insertion rejects exhausted capacity and unrepresentable times without mutation")
{
    std::vector<Keire::CurveKey> keys;
    for (std::size_t index = 0; index < Keire::Curve1D::MaximumKeys; ++index)
    {
        keys.push_back({.Time = static_cast<float>(index), .Value = 1.0F});
    }
    Keire::Curve1D full(std::move(keys));
    const auto original = full;
    CHECK_FALSE(KeireEditor::Detail::InsertAuthoringCurveKey(full));
    CHECK(full == original);

    std::vector<Keire::ColorGradientKey> gradientKeys;
    for (std::size_t index = 0; index < Keire::ColorGradient::MaximumKeys; ++index)
    {
        gradientKeys.push_back({.Time = static_cast<float>(index)});
    }
    Keire::ColorGradient fullGradient(std::move(gradientKeys), Keire::GradientInterpolation::Constant);
    const auto gradientOriginal = fullGradient;
    CHECK_FALSE(KeireEditor::Detail::InsertAuthoringGradientKey(fullGradient));
    CHECK(fullGradient == gradientOriginal);

    const float minimum = 1.0F;
    const float maximum = std::nextafter(minimum, 2.0F);
    Keire::Curve1D adjacent({{minimum, 1}, {maximum, 2}});
    const auto adjacentOriginal = adjacent;
    CHECK_FALSE(KeireEditor::Detail::InsertAuthoringCurveKey(adjacent, minimum, maximum));
    CHECK(adjacent == adjacentOriginal);
    CHECK_THROWS_AS((void)KeireEditor::Detail::InsertAuthoringCurveKey(adjacent, 1, 1), std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::Detail::InsertAuthoringCurveKey(adjacent, 2, 1), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)KeireEditor::Detail::InsertAuthoringCurveKey(adjacent, 0, std::numeric_limits<float>::infinity()),
        std::invalid_argument);
    CHECK(adjacent == adjacentOriginal);

    const float largest = std::numeric_limits<float>::max();
    Keire::Curve1D overflowing({{0, largest, 0, largest, Keire::CurveInterpolation::Cubic}, {1, largest, -largest, 0}});
    const auto overflowOriginal = overflowing;
    CHECK_FALSE(KeireEditor::Detail::InsertAuthoringCurveKey(overflowing));
    CHECK(overflowing == overflowOriginal);
}
