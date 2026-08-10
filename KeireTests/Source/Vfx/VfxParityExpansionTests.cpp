#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] std::optional<Keire::VfxParameterValue>
    Evaluate(const Keire::VfxValueOpcode opcode, const Keire::VfxValueType outputType,
             const std::vector<Keire::VfxParameterValue>& values, const std::uint32_t outputIndex = 0,
             const Keire::Internal::VfxExpressionEvaluationContext* context = nullptr)
    {
        std::vector<const Keire::VfxParameterValue*> inputs;
        inputs.reserve(values.size());
        for (const auto& value : values)
            inputs.push_back(&value);
        return Keire::Internal::EvaluateVfxExtendedExpression(opcode, inputs, outputType, outputIndex, context);
    }

    void CheckVector3(const Keire::Vector3 value, const Keire::Vector3 expected)
    {
        CHECK(value.X == doctest::Approx(expected.X));
        CHECK(value.Y == doctest::Approx(expected.Y));
        CHECK(value.Z == doctest::Approx(expected.Z));
    }
} // namespace

TEST_CASE("portable parity expansion catalog entries are canonical and constructible")
{
    static constexpr std::array<std::string_view, 76> typeIds{
        "keire.operator.area-circle",
        "keire.operator.attribute-angular-velocity",
        "keire.operator.attribute-direction",
        "keire.operator.attribute-map",
        "keire.operator.attribute-mass",
        "keire.operator.attribute-pivot",
        "keire.operator.attribute-scale",
        "keire.operator.attribute-target-position",
        "keire.operator.attribute-texture-index",
        "keire.operator.buffer-count",
        "keire.operator.change-space",
        "keire.operator.construct-matrix",
        "keire.operator.custom-attribute",
        "keire.operator.distance-line",
        "keire.operator.distance-plane",
        "keire.operator.distance-sphere",
        "keire.operator.inline-box",
        "keire.operator.inline-circle",
        "keire.operator.inline-cone",
        "keire.operator.inline-curve",
        "keire.operator.inline-cylinder",
        "keire.operator.inline-flipbook",
        "keire.operator.inline-gradient",
        "keire.operator.inline-line",
        "keire.operator.inline-matrix",
        "keire.operator.inline-mesh",
        "keire.operator.inline-plane",
        "keire.operator.inline-point-cache",
        "keire.operator.inline-sphere",
        "keire.operator.inline-texture-cube",
        "keire.operator.inline-texture-cube-array",
        "keire.operator.inline-texture2d",
        "keire.operator.inline-texture2d-array",
        "keire.operator.inline-texture3d",
        "keire.operator.inline-torus",
        "keire.operator.inline-transform",
        "keire.operator.invert-trs",
        "keire.operator.load-texture2d",
        "keire.operator.load-texture2d-array",
        "keire.operator.load-texture3d",
        "keire.operator.local-to-world",
        "keire.operator.look-at",
        "keire.operator.look-at-direction",
        "keire.operator.mesh-index-count",
        "keire.operator.mesh-triangle-count",
        "keire.operator.mesh-vertex-count",
        "keire.operator.sample-bezier",
        "keire.operator.sample-buffer",
        "keire.operator.sample-curve",
        "keire.operator.sample-gradient",
        "keire.operator.sample-mesh",
        "keire.operator.sample-mesh-index",
        "keire.operator.sample-sdf",
        "keire.operator.sample-texture-cube",
        "keire.operator.sample-texture-cube-array",
        "keire.operator.sample-texture2d",
        "keire.operator.sample-texture2d-array",
        "keire.operator.sample-texture3d",
        "keire.operator.spawn-state",
        "keire.operator.skinned-local-transform",
        "keire.operator.skinned-world-transform",
        "keire.operator.swizzle",
        "keire.operator.texture-dimensions",
        "keire.operator.transform-direction",
        "keire.operator.transform-matrix",
        "keire.operator.transform-position",
        "keire.operator.transform-vector",
        "keire.operator.transform-vector4",
        "keire.operator.transpose-matrix",
        "keire.operator.volume-box",
        "keire.operator.volume-cone",
        "keire.operator.volume-cylinder",
        "keire.operator.volume-sphere",
        "keire.operator.volume-torus",
        "keire.operator.weighted-select",
        "keire.operator.world-to-local",
    };

    CHECK(Keire::VfxNodeCatalog().size() >= 240);
    for (const auto typeId : typeIds)
    {
        CAPTURE(typeId);
        const auto* descriptor = Keire::FindVfxNodeDescriptor(typeId);
        REQUIRE(descriptor != nullptr);
        CHECK(descriptor->Class == Keire::VfxNodeClass::Operator);
        CHECK(descriptor->SupportTier == Keire::VfxNodeSupportTier::KeireEquivalent);
        CHECK(descriptor->Lowering.has_value());
        const auto node = Keire::CreateVfxGraphOperatorNode(typeId);
        CHECK(node.TypeId.View() == typeId);
        CHECK(node.Pins.size() == descriptor->Pins.size());
        CHECK(node.ResolvedSignature.size() == descriptor->Pins.size());
    }

    CHECK(Keire::FindVfxNodeDescriptor("keire.operator.spawn-state")->BackendTier ==
          Keire::VfxNodeBackendTier::CpuAndGpu);
    CHECK(Keire::FindVfxNodeDescriptor("keire.operator.sample-texture2d")->BackendTier ==
          Keire::VfxNodeBackendTier::CpuOnly);
}

TEST_CASE("portable parity numeric operators have deterministic bounded semantics")
{
    const auto area = Evaluate(Keire::VfxValueOpcode::AreaCircle, Keire::VfxValueType::Scalar, {2.0F});
    REQUIRE(area.has_value());
    CHECK(std::get<float>(*area) == doctest::Approx(12.5663706F));

    const auto lineDistance =
        Evaluate(Keire::VfxValueOpcode::DistanceLine, Keire::VfxValueType::Scalar,
                 {Keire::Vector3{1.0F, 1.0F, 0.0F}, Keire::Vector3{}, Keire::Vector3{2.0F, 0.0F, 0.0F}});
    REQUIRE(lineDistance.has_value());
    CHECK(std::get<float>(*lineDistance) == doctest::Approx(1.0F));

    const auto sphereVolume = Evaluate(Keire::VfxValueOpcode::VolumeSphere, Keire::VfxValueType::Scalar, {3.0F});
    REQUIRE(sphereVolume.has_value());
    CHECK(std::get<float>(*sphereVolume) == doctest::Approx(113.097336F));

    const auto bezier = Evaluate(Keire::VfxValueOpcode::SampleBezier, Keire::VfxValueType::Vector3,
                                 {Keire::Vector3{}, Keire::Vector3{0.0F, 1.0F, 0.0F}, Keire::Vector3{1.0F, 1.0F, 0.0F},
                                  Keire::Vector3{1.0F, 0.0F, 0.0F}, 0.5F});
    REQUIRE(bezier.has_value());
    CheckVector3(std::get<Keire::Vector3>(*bezier), {0.5F, 0.75F, 0.0F});

    const auto selected =
        Evaluate(Keire::VfxValueOpcode::WeightedSelect, Keire::VfxValueType::Vector3,
                 {Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 3.0F, 0.7F});
    REQUIRE(selected.has_value());
    CHECK((std::get<Keire::Vector3>(*selected) == Keire::Vector3{0.0F, 1.0F, 0.0F}));

    const auto swizzle = Evaluate(Keire::VfxValueOpcode::Swizzle, Keire::VfxValueType::Vector4,
                                  {Keire::Vector4{1.0F, 2.0F, 3.0F, 4.0F}, std::uint64_t{3}, std::uint64_t{2},
                                   std::uint64_t{1}, std::uint64_t{0}});
    REQUIRE(swizzle.has_value());
    CHECK((std::get<Keire::Vector4>(*swizzle) == Keire::Vector4{4.0F, 3.0F, 2.0F, 1.0F}));
}

TEST_CASE("portable parity context, matrix, curve, and gradient operators evaluate on CPU")
{
    Keire::Internal::VfxExpressionEvaluationContext context;
    context.EffectTime = 4.5F;
    context.SpawnIndex = 17;
    context.Position = {1.0F, 2.0F, 3.0F};
    context.Velocity = {0.0F, 0.0F, 2.0F};
    context.Size = 3.0F;
    context.EmitterPosition = {5.0F, 0.0F, 0.0F};

    const auto direction =
        Evaluate(Keire::VfxValueOpcode::AttributeDirection, Keire::VfxValueType::Vector3, {}, 0, &context);
    REQUIRE(direction.has_value());
    CHECK((std::get<Keire::Vector3>(*direction) == Keire::Vector3{0.0F, 0.0F, 1.0F}));

    const auto target =
        Evaluate(Keire::VfxValueOpcode::AttributeTargetPosition, Keire::VfxValueType::Vector3, {}, 0, &context);
    REQUIRE(target.has_value());
    CHECK((std::get<Keire::Vector3>(*target) == Keire::Vector3{1.0F, 2.0F, 5.0F}));

    const auto spawnTime = Evaluate(Keire::VfxValueOpcode::SpawnState, Keire::VfxValueType::Scalar, {}, 1, &context);
    REQUIRE(spawnTime.has_value());
    CHECK(std::get<float>(*spawnTime) == doctest::Approx(4.5F));

    const auto localToWorld =
        Evaluate(Keire::VfxValueOpcode::LocalToWorld, Keire::VfxValueType::Matrix, {}, 0, &context);
    REQUIRE(localToWorld.has_value());
    const auto transformed = Evaluate(Keire::VfxValueOpcode::TransformPosition, Keire::VfxValueType::Vector3,
                                      {std::get<Keire::Matrix4>(*localToWorld), Keire::Vector3{1.0F, 0.0F, 0.0F}});
    REQUIRE(transformed.has_value());
    CheckVector3(std::get<Keire::Vector3>(*transformed), {6.0F, 0.0F, 0.0F});

    const auto curve = Evaluate(Keire::VfxValueOpcode::SampleCurve, Keire::VfxValueType::Scalar,
                                {Keire::Curve1D::Linear(2.0F, 6.0F), 0.25F});
    REQUIRE(curve.has_value());
    CHECK(std::get<float>(*curve) == doctest::Approx(3.0F));

    const Keire::ColorGradient gradient({{0.0F, {1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, {0.0F, 0.0F, 1.0F, 1.0F}}});
    const auto color = Evaluate(Keire::VfxValueOpcode::SampleGradient, Keire::VfxValueType::Color, {gradient, 0.5F});
    REQUIRE(color.has_value());
    const auto sampled = std::get<Keire::Color>(*color);
    CHECK(sampled.Red == doctest::Approx(0.5F));
    CHECK(sampled.Blue == doctest::Approx(0.5F));
}

TEST_CASE("CPU resource sampling uses the bounded host callback and contains failures")
{
    const Keire::AssetId texture(0x100ULL, 0x200ULL);
    std::optional<Keire::VfxResourceQuery> observed;
    std::function<std::optional<Keire::VfxResourceQueryResult>(const Keire::VfxResourceQuery&)> query =
        [&observed](const Keire::VfxResourceQuery& value)
    {
        observed = value;
        Keire::VfxResourceQueryResult result;
        result.Values[0] = {0.25F, 0.5F, 0.75F, 1.0F};
        return result;
    };
    Keire::Internal::VfxExpressionEvaluationContext context;
    context.ResourceQuery = &query;

    const auto sampled = Evaluate(Keire::VfxValueOpcode::ResourceSampleTexture2D, Keire::VfxValueType::Color,
                                  {texture, Keire::Vector2{0.2F, 0.8F}, 2.0F}, 0, &context);
    REQUIRE(sampled.has_value());
    CHECK((std::get<Keire::Color>(*sampled) == Keire::Color{0.25F, 0.5F, 0.75F, 1.0F}));
    REQUIRE(observed.has_value());
    CHECK(observed->Kind == Keire::VfxResourceQueryKind::SampleTexture2D);
    CHECK(observed->Resource == texture);
    CHECK(observed->Coordinate.X == doctest::Approx(0.2F));
    CHECK(observed->Coordinate.Y == doctest::Approx(0.8F));
    CHECK(observed->Level == doctest::Approx(2.0F));

    const auto distance =
        Evaluate(Keire::VfxValueOpcode::ResourceSampleSignedDistanceField, Keire::VfxValueType::Scalar,
                 {texture, Keire::Vector3{1.0F, 2.0F, 3.0F}, 0.0F}, 0, &context);
    REQUIRE(distance.has_value());
    CHECK(std::get<float>(*distance) == doctest::Approx(0.25F));
    REQUIRE(observed.has_value());
    CHECK(observed->Kind == Keire::VfxResourceQueryKind::SampleSignedDistanceField);

    context.ResourceQuery = nullptr;
    CHECK_FALSE(Evaluate(Keire::VfxValueOpcode::ResourceSampleTexture2D, Keire::VfxValueType::Color,
                         {texture, Keire::Vector2{}, 0.0F}, 0, &context));

    query = [](const Keire::VfxResourceQuery&) -> std::optional<Keire::VfxResourceQueryResult> { throw 42; };
    context.ResourceQuery = &query;
    CHECK_FALSE(Evaluate(Keire::VfxValueOpcode::ResourceSampleTexture2D, Keire::VfxValueType::Color,
                         {texture, Keire::Vector2{}, 0.0F}, 0, &context));
}
