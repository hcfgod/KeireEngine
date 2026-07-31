#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        struct VfxNodeCatalogContractEntry
        {
            std::string_view TypeId;
            std::string_view Label;
            VfxNodeClass Class;
            VfxNodeSupportTier SupportTier;
            VfxNodeBackendTier BackendTier;
        };

        constexpr VfxNodeCatalogContractEntry VfxNodeCatalogContract[] = {
#define KEIRE_VFX_NODE(typeId, label, nodeClass, support, backend)                                                     \
    {typeId, label, VfxNodeClass::nodeClass, VfxNodeSupportTier::support, VfxNodeBackendTier::backend},
#include "VfxNodeCatalogContract.inc"
#undef KEIRE_VFX_NODE
        };

        [[nodiscard]] std::vector<VfxContextType> ValueContexts()
        {
            return {VfxContextType::Spawn, VfxContextType::Initialize, VfxContextType::Update, VfxContextType::Output,
                    VfxContextType::Event};
        }

        [[nodiscard]] std::vector<VfxContextType> ParticleContexts()
        {
            return {VfxContextType::Initialize, VfxContextType::Update, VfxContextType::Output, VfxContextType::Event};
        }

        [[nodiscard]] constexpr bool IsGpuInterpretedValueType(const VfxValueType type) noexcept
        {
            return IsVfxGpuExpressionValueType(type);
        }

        [[nodiscard]] constexpr bool HasValidatedGpuSemantics(const VfxValueOpcode opcode) noexcept
        {
            switch (opcode)
            {
            case VfxValueOpcode::Random:
            case VfxValueOpcode::RandomRange:
            case VfxValueOpcode::DeltaTime:
            case VfxValueOpcode::Lifetime:
            case VfxValueOpcode::ParticleId:
            case VfxValueOpcode::SpawnIndex:
            case VfxValueOpcode::ToFloat:
            case VfxValueOpcode::Power:
            case VfxValueOpcode::Lerp:
                // These opcodes are structurally available in the shader interpreter, but exact CPU/GPU identity,
                // edge-case, or conversion parity still requires differential acceptance coverage.
                return false;
            default:
                return true;
            }
        }

        [[nodiscard]] bool IsGpuInterpretedOperator(const VfxNodeDescriptor& descriptor) noexcept
        {
            constexpr auto MaximumGpuOpcode = VfxValueOpcode::Sign;
            static_assert(static_cast<std::uint8_t>(MaximumGpuOpcode) == 56);
            const auto executable = descriptor.SupportTier == VfxNodeSupportTier::Supported ||
                                    descriptor.SupportTier == VfxNodeSupportTier::KeireEquivalent;
            return descriptor.Class == VfxNodeClass::Operator && executable && descriptor.Lowering &&
                   static_cast<std::uint8_t>(*descriptor.Lowering) <= static_cast<std::uint8_t>(MaximumGpuOpcode) &&
                   HasValidatedGpuSemantics(*descriptor.Lowering) &&
                   std::ranges::all_of(descriptor.Pins, [](const VfxNodePinDescriptor& pin)
                                       { return IsGpuInterpretedValueType(pin.Type); });
        }

        [[nodiscard]] VfxNodePinDescriptor Input(std::string name, std::string semantic, const VfxValueType type,
                                                 VfxParameterValue defaultValue)
        {
            return {std::move(name), std::move(semantic), type, true, std::move(defaultValue), {type}};
        }

        [[nodiscard]] VfxNodePinDescriptor Output(std::string name, std::string semantic, const VfxValueType type)
        {
            return {std::move(name), std::move(semantic), type, false, std::nullopt, {type}};
        }

        [[nodiscard]] VfxNodeDescriptor ScalarBinary(std::string id, std::string label, const VfxValueOpcode opcode,
                                                     std::vector<std::string> synonyms = {})
        {
            return {{std::move(id)},
                    std::move(label),
                    "Operator/Math/Arithmetic",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    {Input("A", "a", VfxValueType::Scalar, 0.0F), Input("B", "b", VfxValueType::Scalar, 0.0F),
                     Output("Out", "out", VfxValueType::Scalar)},
                    {},
                    opcode};
        }

        [[nodiscard]] VfxNodeDescriptor ScalarUnary(std::string id, std::string label, const VfxValueOpcode opcode,
                                                    std::vector<std::string> synonyms = {})
        {
            return {{std::move(id)},
                    std::move(label),
                    "Operator/Math",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                    {},
                    opcode};
        }

        [[nodiscard]] VfxNodeDescriptor FixedOperator(std::string id, std::string label, std::string category,
                                                      std::vector<VfxNodePinDescriptor> pins,
                                                      const VfxValueOpcode opcode,
                                                      std::vector<std::string> synonyms = {})
        {
            return {{std::move(id)},
                    std::move(label),
                    std::move(category),
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    std::move(pins),
                    {},
                    opcode};
        }

        [[nodiscard]] VfxNodeDescriptor BuiltIn(std::string id, std::string label, const VfxValueType type,
                                                const VfxValueOpcode opcode,
                                                std::vector<VfxContextType> contexts = ValueContexts(),
                                                std::vector<std::string> synonyms = {})
        {
            auto pinName = label;
            return {{std::move(id)},
                    std::move(label),
                    "Operator/Built-In",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    std::move(contexts),
                    {Output(std::move(pinName), "out", type)},
                    {},
                    opcode};
        }

        [[nodiscard]] VfxNodeDescriptor TypedRange(std::string id, std::vector<std::string> synonyms,
                                                   const VfxValueType valueType, const VfxValueType rangeType,
                                                   VfxParameterValue minimum, VfxParameterValue maximum)
        {
            return {{std::move(id)},
                    "Range",
                    "Operator/Random",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    {Input("Min", "minimum", valueType, std::move(minimum)),
                     Input("Max", "maximum", valueType, std::move(maximum)), Output("Range", "range", rangeType)},
                    {},
                    VfxValueOpcode::Range};
        }

        [[nodiscard]] VfxNodeDescriptor TypedRandom(std::string id, std::vector<std::string> synonyms,
                                                    const VfxValueType valueType, VfxParameterValue minimum,
                                                    VfxParameterValue maximum)
        {
            return {{std::move(id)},
                    "Random Number",
                    "Operator/Random",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    {Input("Min", "minimum", valueType, std::move(minimum)),
                     Input(valueType == VfxValueType::Integer || valueType == VfxValueType::UnsignedInteger
                               ? "Max (Exclusive)"
                               : "Max",
                           "maximum", valueType, std::move(maximum)),
                     Output("Out", "out", valueType)},
                    {{"Scope", std::uint64_t{0}}, {"Constant", false}, {"Independent Channels", true}},
                    VfxValueOpcode::Random};
        }

        [[nodiscard]] VfxNodeDescriptor BooleanRandom()
        {
            return {{"keire.operator.random-boolean"},
                    "Random Number",
                    "Operator/Random",
                    {"random boolean", "rng bool", "probability"},
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::Supported,
                    {},
                    ValueContexts(),
                    {Output("Out", "out", VfxValueType::Boolean)},
                    {{"Scope", std::uint64_t{0}}, {"Constant", false}, {"Independent Channels", true}},
                    VfxValueOpcode::Random};
        }

        [[nodiscard]] VfxNodeDescriptor TypedRandomRange(std::string id, std::vector<std::string> synonyms,
                                                         const VfxValueType valueType, const VfxValueType rangeType,
                                                         VfxParameterValue range, const bool inclusiveMaximum)
        {
            return {{std::move(id)},
                    "Random Range",
                    "Operator/Random",
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::KeireEquivalent,
                    {},
                    ValueContexts(),
                    {Input("Range", "range", rangeType, std::move(range)), Output("Out", "out", valueType)},
                    {{"Scope", std::uint64_t{0}},
                     {"Constant", false},
                     {"Inclusive Maximum", inclusiveMaximum},
                     {"Independent Channels", true}},
                    VfxValueOpcode::RandomRange};
        }

        [[nodiscard]] bool ValidTypeId(const std::string_view value) noexcept
        {
            if (value.empty() || value.front() == '.' || value.back() == '.')
                return false;
            return std::ranges::all_of(
                value, [](const unsigned char character)
                { return std::islower(character) || std::isdigit(character) || character == '.' || character == '-'; });
        }

        [[nodiscard]] std::vector<VfxNodeDescriptor> BuildCatalog()
        {
            std::vector<VfxNodeDescriptor> result;
            result.reserve(96);
            result.push_back({{"keire.operator.range"},
                              "Range",
                              "Operator/Random",
                              {"min max", "interval"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::Supported,
                              {},
                              ValueContexts(),
                              {Input("Min", "minimum", VfxValueType::Scalar, 0.0F),
                               Input("Max", "maximum", VfxValueType::Scalar, 1.0F),
                               Output("Range", "range", VfxValueType::ScalarRange)},
                              {},
                              VfxValueOpcode::Range});
            result.push_back({{"keire.operator.integer-range"},
                              "Range",
                              "Operator/Random",
                              {"integer min max", "integer interval"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::Supported,
                              {},
                              ValueContexts(),
                              {Input("Min", "minimum", VfxValueType::Integer, std::int64_t{0}),
                               Input("Max", "maximum", VfxValueType::Integer, std::int64_t{1}),
                               Output("Range", "range", VfxValueType::IntegerRange)},
                              {},
                              VfxValueOpcode::Range});
            result.push_back(TypedRange("keire.operator.unsigned-integer-range",
                                        {"unsigned integer min max", "uint interval"}, VfxValueType::UnsignedInteger,
                                        VfxValueType::UnsignedIntegerRange, std::uint64_t{0}, std::uint64_t{1}));
            result.push_back(TypedRange("keire.operator.vector2-range", {"vector2 min max", "float2 interval"},
                                        VfxValueType::Vector2, VfxValueType::Vector2Range, Vector2{},
                                        Vector2{1.0F, 1.0F}));
            result.push_back(TypedRange("keire.operator.vector3-range", {"vector3 min max", "float3 interval"},
                                        VfxValueType::Vector3, VfxValueType::Vector3Range, Vector3{},
                                        Vector3{1.0F, 1.0F, 1.0F}));
            result.push_back(TypedRange("keire.operator.vector4-range", {"vector4 min max", "float4 interval"},
                                        VfxValueType::Vector4, VfxValueType::Vector4Range, Vector4{},
                                        Vector4{1.0F, 1.0F, 1.0F, 1.0F}));
            result.push_back(TypedRange("keire.operator.color-range", {"color min max", "rgba interval"},
                                        VfxValueType::Color, VfxValueType::ColorRange, Color{0.0F, 0.0F, 0.0F, 0.0F},
                                        Color{}));
            result.push_back(
                {{"keire.operator.random"},
                 "Random Number",
                 "Operator/Random",
                 {"random", "rng", "uniform"},
                 VfxNodeClass::Operator,
                 VfxNodeTypeBehavior::Fixed,
                 VfxNodeSupportTier::Supported,
                 {},
                 ValueContexts(),
                 {Input("Min", "minimum", VfxValueType::Scalar, 0.0F),
                  Input("Max", "maximum", VfxValueType::Scalar, 1.0F), Output("Out", "out", VfxValueType::Scalar)},
                 {{"Scope", std::uint64_t{0}}, {"Constant", false}, {"Independent Channels", true}},
                 VfxValueOpcode::Random});
            result.push_back({{"keire.operator.random-integer"},
                              "Random Number",
                              "Operator/Random",
                              {"random integer", "rng int", "maximum exclusive"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::Supported,
                              {},
                              ValueContexts(),
                              {Input("Min", "minimum", VfxValueType::Integer, std::int64_t{0}),
                               Input("Max (Exclusive)", "maximum", VfxValueType::Integer, std::int64_t{2}),
                               Output("Out", "out", VfxValueType::Integer)},
                              {{"Scope", std::uint64_t{0}}, {"Constant", false}, {"Independent Channels", true}},
                              VfxValueOpcode::Random});
            result.push_back(TypedRandom("keire.operator.random-unsigned-integer",
                                         {"random unsigned integer", "rng uint", "maximum exclusive"},
                                         VfxValueType::UnsignedInteger, std::uint64_t{0}, std::uint64_t{6}));
            result.push_back(TypedRandom("keire.operator.random-vector2", {"random vector2", "rng float2"},
                                         VfxValueType::Vector2, Vector2{}, Vector2{1.0F, 1.0F}));
            result.push_back(TypedRandom("keire.operator.random-vector3", {"random vector3", "rng float3"},
                                         VfxValueType::Vector3, Vector3{}, Vector3{1.0F, 1.0F, 1.0F}));
            result.push_back(TypedRandom("keire.operator.random-vector4", {"random vector4", "rng float4"},
                                         VfxValueType::Vector4, Vector4{}, Vector4{1.0F, 1.0F, 1.0F, 1.0F}));
            result.push_back(TypedRandom("keire.operator.random-color", {"random color", "rng rgba"},
                                         VfxValueType::Color, Color{0.0F, 0.0F, 0.0F, 0.0F}, Color{}));
            result.push_back(BooleanRandom());
            result.push_back({{"keire.operator.random-range"},
                              "Random Range",
                              "Operator/Random",
                              {"between", "sample range"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::KeireEquivalent,
                              {},
                              ValueContexts(),
                              {Input("Range", "range", VfxValueType::ScalarRange, VfxScalarRange{0.0F, 1.0F}),
                               Output("Out", "out", VfxValueType::Scalar)},
                              {{"Scope", std::uint64_t{0}},
                               {"Constant", false},
                               {"Inclusive Maximum", false},
                               {"Independent Channels", true}},
                              VfxValueOpcode::RandomRange});
            result.push_back({{"keire.operator.random-integer-range"},
                              "Random Range",
                              "Operator/Random",
                              {"integer between", "sample integer range"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::KeireEquivalent,
                              {},
                              ValueContexts(),
                              {Input("Range", "range", VfxValueType::IntegerRange,
                                     VfxIntegerRange{std::int64_t{0}, std::int64_t{1}}),
                               Output("Out", "out", VfxValueType::Integer)},
                              {{"Scope", std::uint64_t{0}},
                               {"Constant", false},
                               {"Inclusive Maximum", true},
                               {"Independent Channels", true}},
                              VfxValueOpcode::RandomRange});
            result.push_back(TypedRandomRange("keire.operator.random-unsigned-integer-range",
                                              {"unsigned integer between", "sample unsigned integer range"},
                                              VfxValueType::UnsignedInteger, VfxValueType::UnsignedIntegerRange,
                                              VfxUnsignedIntegerRange{std::uint64_t{0}, std::uint64_t{1}}, true));
            result.push_back(TypedRandomRange("keire.operator.random-vector2-range",
                                              {"vector2 between", "sample vector2 range"}, VfxValueType::Vector2,
                                              VfxValueType::Vector2Range,
                                              VfxVector2Range{Vector2{}, Vector2{1.0F, 1.0F}}, false));
            result.push_back(TypedRandomRange("keire.operator.random-vector3-range",
                                              {"vector3 between", "sample vector3 range"}, VfxValueType::Vector3,
                                              VfxValueType::Vector3Range,
                                              VfxVector3Range{Vector3{}, Vector3{1.0F, 1.0F, 1.0F}}, false));
            result.push_back(TypedRandomRange("keire.operator.random-vector4-range",
                                              {"vector4 between", "sample vector4 range"}, VfxValueType::Vector4,
                                              VfxValueType::Vector4Range,
                                              VfxVector4Range{Vector4{}, Vector4{1.0F, 1.0F, 1.0F, 1.0F}}, false));
            result.push_back(TypedRandomRange(
                "keire.operator.random-color-range", {"color between", "sample color range"}, VfxValueType::Color,
                VfxValueType::ColorRange, VfxColorRange{Color{0.0F, 0.0F, 0.0F, 0.0F}, Color{}}, false));
            result.push_back(
                {{"keire.operator.remap"},
                 "Remap",
                 "Operator/Math/Interpolation",
                 {"map range", "rescale"},
                 VfxNodeClass::Operator,
                 VfxNodeTypeBehavior::Fixed,
                 VfxNodeSupportTier::Supported,
                 {},
                 ValueContexts(),
                 {Input("Input", "input", VfxValueType::Scalar, 0.0F),
                  Input("Source", "source", VfxValueType::ScalarRange, VfxScalarRange{0.0F, 1.0F}),
                  Input("Destination", "destination", VfxValueType::ScalarRange, VfxScalarRange{0.0F, 1.0F}),
                  Output("Out", "out", VfxValueType::Scalar)},
                 {{"Clamp", false}},
                 VfxValueOpcode::Remap});

            result.push_back(ScalarBinary("keire.operator.add", "Add", VfxValueOpcode::Add, {"plus", "+"}));
            result.push_back(
                ScalarBinary("keire.operator.subtract", "Subtract", VfxValueOpcode::Subtract, {"minus", "-"}));
            result.push_back(
                ScalarBinary("keire.operator.multiply", "Multiply", VfxValueOpcode::Multiply, {"product", "*"}));
            result.push_back(
                ScalarBinary("keire.operator.divide", "Divide", VfxValueOpcode::Divide, {"quotient", "/"}));
            result.push_back(ScalarBinary("keire.operator.minimum", "Minimum", VfxValueOpcode::Minimum, {"min"}));
            result.push_back(ScalarBinary("keire.operator.maximum", "Maximum", VfxValueOpcode::Maximum, {"max"}));
            result.push_back(ScalarUnary("keire.operator.absolute", "Absolute", VfxValueOpcode::Absolute, {"abs"}));
            result.push_back(
                ScalarUnary("keire.operator.saturate", "Saturate", VfxValueOpcode::Saturate, {"clamp 0 1"}));
            result.push_back(
                {{"keire.operator.clamp"},
                 "Clamp",
                 "Operator/Math",
                 {"limit"},
                 VfxNodeClass::Operator,
                 VfxNodeTypeBehavior::Fixed,
                 VfxNodeSupportTier::Supported,
                 {},
                 ValueContexts(),
                 {Input("Input", "input", VfxValueType::Scalar, 0.0F),
                  Input("Min", "minimum", VfxValueType::Scalar, 0.0F),
                  Input("Max", "maximum", VfxValueType::Scalar, 1.0F), Output("Out", "out", VfxValueType::Scalar)},
                 {},
                 VfxValueOpcode::Clamp});

            result.push_back(FixedOperator(
                "keire.operator.sine", "Sine", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Sine, {"sin"}));
            result.push_back(FixedOperator(
                "keire.operator.cosine", "Cosine", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Cosine, {"cos"}));
            result.push_back(FixedOperator(
                "keire.operator.tangent", "Tangent", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Tangent, {"tan"}));
            result.push_back(FixedOperator(
                "keire.operator.asin", "Asin", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::ArcSine, {"arc sine", "inverse sine"}));
            result.push_back(FixedOperator(
                "keire.operator.acos", "Acos", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::ArcCosine, {"arc cosine", "inverse cosine"}));
            result.push_back(FixedOperator(
                "keire.operator.atan", "Atan", "Operator/Math/Trigonometry",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::ArcTangent, {"arc tangent", "inverse tangent"}));
            result.push_back(
                FixedOperator("keire.operator.atan2", "Atan2", "Operator/Math/Trigonometry",
                              {Input("Y", "y", VfxValueType::Scalar, 0.0F), Input("X", "x", VfxValueType::Scalar, 1.0F),
                               Output("Out", "out", VfxValueType::Scalar)},
                              VfxValueOpcode::Atan2, {"arc tangent 2", "angle"}));

            result.push_back(FixedOperator("keire.operator.power", "Power", "Operator/Math/Arithmetic",
                                           {Input("Base", "base", VfxValueType::Scalar, 1.0F),
                                            Input("Exponent", "exponent", VfxValueType::Scalar, 1.0F),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::Power, {"pow", "exponentiate"}));
            result.push_back(FixedOperator(
                "keire.operator.square-root", "Square Root", "Operator/Math/Arithmetic",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::SquareRoot, {"sqrt", "root"}));
            result.push_back(FixedOperator(
                "keire.operator.exponential", "Exp", "Operator/Math",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Exponential, {"exponential", "e power"}));
            result.push_back(FixedOperator(
                "keire.operator.logarithm", "Log", "Operator/Math",
                {Input("Input", "input", VfxValueType::Scalar, 1.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Logarithm, {"logarithm", "natural log", "ln"}));
            result.push_back(FixedOperator(
                "keire.operator.log2", "Log2", "Operator/Math",
                {Input("Input", "input", VfxValueType::Scalar, 1.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::LogarithmBase2, {"base 2 logarithm", "binary logarithm"}));
            result.push_back(FixedOperator(
                "keire.operator.log10", "Log10", "Operator/Math",
                {Input("Input", "input", VfxValueType::Scalar, 1.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::LogarithmBase10, {"base 10 logarithm", "common logarithm"}));

            result.push_back(FixedOperator(
                "keire.operator.ceiling", "Ceiling", "Operator/Math/Clamp",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Ceiling, {"ceil", "round up"}));
            result.push_back(FixedOperator(
                "keire.operator.floor", "Floor", "Operator/Math/Clamp",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Floor, {"round down"}));
            result.push_back(FixedOperator(
                "keire.operator.round", "Round", "Operator/Math/Clamp",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Round, {"nearest integer", "bankers round"}));
            result.push_back(FixedOperator(
                "keire.operator.fractional", "Fractional", "Operator/Math/Arithmetic",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Fractional, {"frac", "fraction"}));
            result.push_back(
                FixedOperator("keire.operator.lerp", "Lerp", "Operator/Math/Arithmetic",
                              {Input("A", "a", VfxValueType::Scalar, 0.0F), Input("B", "b", VfxValueType::Scalar, 1.0F),
                               Input("T", "t", VfxValueType::Scalar, 0.5F), Output("Out", "out", VfxValueType::Scalar)},
                              VfxValueOpcode::Lerp, {"linear interpolation", "mix"}));
            result.push_back(FixedOperator("keire.operator.smoothstep", "Smoothstep", "Operator/Math/Arithmetic",
                                           {Input("Edge 1", "edge1", VfxValueType::Scalar, 0.0F),
                                            Input("Edge 2", "edge2", VfxValueType::Scalar, 1.0F),
                                            Input("Input", "input", VfxValueType::Scalar, 0.5F),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::Smoothstep, {"smooth step", "hermite"}));
            result.push_back(FixedOperator("keire.operator.step", "Step", "Operator/Math/Arithmetic",
                                           {Input("Edge", "edge", VfxValueType::Scalar, 0.0F),
                                            Input("Input", "input", VfxValueType::Scalar, 0.0F),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::Step, {"threshold"}));
            result.push_back(FixedOperator(
                "keire.operator.negate", "Negate (-x)", "Operator/Math/Arithmetic",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Negate, {"negative", "minus x"}));
            result.push_back(FixedOperator(
                "keire.operator.sign", "Sign", "Operator/Math/Arithmetic",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Sign, {"signum"}));

            const auto wavePins = []
            {
                return std::vector{
                    Input("Input", "input", VfxValueType::Scalar, 0.5F),
                    Input("Frequency", "frequency", VfxValueType::Scalar, 1.0F),
                    Input("Min", "minimum", VfxValueType::Scalar, 0.0F),
                    Input("Max", "maximum", VfxValueType::Scalar, 1.0F),
                    Output("Out", "out", VfxValueType::Scalar),
                };
            };
            const auto scalarWave = [&wavePins](std::string id, std::string label, std::vector<std::string> synonyms)
            {
                auto descriptor = FixedOperator(std::move(id), std::move(label), "Operator/Math/Wave", wavePins(),
                                                VfxValueOpcode::Lerp, std::move(synonyms));
                // Unity's Wave Operators are unified across scalar and vector signatures. The current expression ABI
                // executes the scalar signature exactly, so expose it as an explicit Kéire equivalent until adaptive
                // signatures can preserve Unity's full contract.
                descriptor.SupportTier = VfxNodeSupportTier::KeireEquivalent;
                return descriptor;
            };
            // Wave nodes lower to primitive expression recipes whose terminal operation is Lerp. This keeps the
            // compiled ABI limited to reusable arithmetic opcodes and makes a future typed GPU interpreter sufficient.
            result.push_back(
                scalarWave("keire.operator.sawtooth-wave", "Sawtooth Wave", {"saw wave", "ramp oscillator"}));
            result.push_back(scalarWave("keire.operator.sine-wave", "Sine Wave", {"sin wave", "smooth oscillator"}));
            result.push_back(
                scalarWave("keire.operator.square-wave", "Square Wave", {"pulse wave", "binary oscillator"}));
            result.push_back(
                scalarWave("keire.operator.triangle-wave", "Triangle Wave", {"tri wave", "triangle oscillator"}));
            result.push_back({{"keire.operator.compare"},
                              "Compare",
                              "Operator/Logic",
                              {"less greater equal"},
                              VfxNodeClass::Operator,
                              VfxNodeTypeBehavior::Fixed,
                              VfxNodeSupportTier::Supported,
                              {},
                              ValueContexts(),
                              {Input("A", "a", VfxValueType::Scalar, 0.0F), Input("B", "b", VfxValueType::Scalar, 0.0F),
                               Output("Out", "out", VfxValueType::Boolean)},
                              {{"Condition", std::string("Less")}},
                              VfxValueOpcode::Compare});
            result.push_back(
                {{"keire.operator.branch"},
                 "Branch",
                 "Operator/Logic",
                 {"select", "if"},
                 VfxNodeClass::Operator,
                 VfxNodeTypeBehavior::Fixed,
                 VfxNodeSupportTier::Supported,
                 {},
                 ValueContexts(),
                 {Input("Predicate", "predicate", VfxValueType::Boolean, false),
                  Input("True", "true", VfxValueType::Scalar, 1.0F),
                  Input("False", "false", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Scalar)},
                 {},
                 VfxValueOpcode::Select});
            result.push_back(FixedOperator("keire.operator.and", "And", "Operator/Logic",
                                           {Input("A", "a", VfxValueType::Boolean, false),
                                            Input("B", "b", VfxValueType::Boolean, false),
                                            Output("Out", "out", VfxValueType::Boolean)},
                                           VfxValueOpcode::BooleanAnd, {"boolean and", "&&"}));
            result.push_back(FixedOperator("keire.operator.or", "Or", "Operator/Logic",
                                           {Input("A", "a", VfxValueType::Boolean, false),
                                            Input("B", "b", VfxValueType::Boolean, false),
                                            Output("Out", "out", VfxValueType::Boolean)},
                                           VfxValueOpcode::BooleanOr, {"boolean or", "||"}));
            result.push_back(FixedOperator(
                "keire.operator.not", "Not", "Operator/Logic",
                {Input("Input", "input", VfxValueType::Boolean, false), Output("Out", "out", VfxValueType::Boolean)},
                VfxValueOpcode::BooleanNot, {"boolean not", "!"}));

            result.push_back(FixedOperator(
                "keire.operator.combine-vector3", "Combine", "Operator/Math/Vector",
                {Input("X", "x", VfxValueType::Scalar, 0.0F), Input("Y", "y", VfxValueType::Scalar, 0.0F),
                 Input("Z", "z", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Vector3)},
                VfxValueOpcode::Combine, {"construct vector", "make vector3"}));
            result.push_back(FixedOperator(
                "keire.operator.split-vector3", "Split", "Operator/Math/Vector",
                {Input("Input", "input", VfxValueType::Vector3, Vector3{}), Output("X", "x", VfxValueType::Scalar),
                 Output("Y", "y", VfxValueType::Scalar), Output("Z", "z", VfxValueType::Scalar)},
                VfxValueOpcode::Split, {"break vector", "components"}));
            result.push_back(FixedOperator("keire.operator.dot-product", "Dot Product", "Operator/Math/Vector",
                                           {Input("A", "a", VfxValueType::Vector3, Vector3{}),
                                            Input("B", "b", VfxValueType::Vector3, Vector3{}),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::Dot, {"dot"}));
            result.push_back(FixedOperator("keire.operator.cross-product", "Cross Product", "Operator/Math/Vector",
                                           {Input("A", "a", VfxValueType::Vector3, Vector3{}),
                                            Input("B", "b", VfxValueType::Vector3, Vector3{}),
                                            Output("Out", "out", VfxValueType::Vector3)},
                                           VfxValueOpcode::Cross, {"cross"}));
            result.push_back(FixedOperator("keire.operator.normalize", "Normalize", "Operator/Math/Vector",
                                           {Input("Input", "input", VfxValueType::Vector3, Vector3{}),
                                            Output("Out", "out", VfxValueType::Vector3)},
                                           VfxValueOpcode::Normalize, {"unit vector"}));
            result.push_back(FixedOperator(
                "keire.operator.length", "Length", "Operator/Math/Vector",
                {Input("Input", "input", VfxValueType::Vector3, Vector3{}), Output("Out", "out", VfxValueType::Scalar)},
                VfxValueOpcode::Length, {"magnitude"}));
            result.push_back(FixedOperator("keire.operator.distance", "Distance", "Operator/Math/Vector",
                                           {Input("A", "a", VfxValueType::Vector3, Vector3{}),
                                            Input("B", "b", VfxValueType::Vector3, Vector3{}),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::Distance));

            result.push_back(FixedOperator("keire.operator.integer-to-float", "To Float", "Operator/Math/Cast",
                                           {Input("Input", "input", VfxValueType::Integer, std::int64_t{0}),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::ToFloat, {"cast float", "convert float"}));
            result.push_back(FixedOperator("keire.operator.unsigned-integer-to-float", "To Float", "Operator/Math/Cast",
                                           {Input("Input", "input", VfxValueType::UnsignedInteger, std::uint64_t{0}),
                                            Output("Out", "out", VfxValueType::Scalar)},
                                           VfxValueOpcode::ToFloat, {"cast unsigned float", "convert uint float"}));
            result.push_back(FixedOperator(
                "keire.operator.float-to-integer", "To Integer", "Operator/Math/Cast",
                {Input("Input", "input", VfxValueType::Scalar, 0.0F), Output("Out", "out", VfxValueType::Integer)},
                VfxValueOpcode::ToInteger, {"cast int", "convert integer"}));
            result.push_back(FixedOperator("keire.operator.float-to-unsigned-integer", "To Unsigned Integer",
                                           "Operator/Math/Cast",
                                           {Input("Input", "input", VfxValueType::Scalar, 0.0F),
                                            Output("Out", "out", VfxValueType::UnsignedInteger)},
                                           VfxValueOpcode::ToUnsignedInteger, {"cast uint", "convert unsigned"}));

            result.push_back(BuiltIn("keire.operator.time", "Total Time", VfxValueType::Scalar, VfxValueOpcode::Time,
                                     ValueContexts(), {"time", "effect time"}));
            result.push_back(BuiltIn("keire.operator.delta-time", "Delta Time", VfxValueType::Scalar,
                                     VfxValueOpcode::DeltaTime, ValueContexts(), {"dt", "frame time"}));
            result.push_back(BuiltIn("keire.operator.age", "Age", VfxValueType::Scalar, VfxValueOpcode::Age,
                                     ParticleContexts(), {"particle age"}));
            result.push_back(BuiltIn("keire.operator.lifetime", "Lifetime", VfxValueType::Scalar,
                                     VfxValueOpcode::Lifetime, ParticleContexts(), {"particle lifetime"}));
            result.push_back(BuiltIn("keire.operator.particle-id", "Particle ID", VfxValueType::UnsignedInteger,
                                     VfxValueOpcode::ParticleId, ParticleContexts(), {"particle index", "id"}));
            result.push_back(BuiltIn("keire.operator.spawn-index", "Spawn Index", VfxValueType::UnsignedInteger,
                                     VfxValueOpcode::SpawnIndex, ValueContexts(), {"spawn id", "spawn count"}));

            std::set<std::string> typeIds;
            for (auto& descriptor : result)
            {
                if (IsGpuInterpretedOperator(descriptor))
                    descriptor.BackendTier = VfxNodeBackendTier::CpuAndGpu;
                if (!ValidTypeId(descriptor.TypeId.View()) || !typeIds.insert(descriptor.TypeId.Value).second ||
                    descriptor.Label.empty() || descriptor.Category.empty() || descriptor.Pins.empty() ||
                    (descriptor.SupportTier == VfxNodeSupportTier::Disabled && descriptor.DisabledReason.empty()) ||
                    (descriptor.SupportTier != VfxNodeSupportTier::Disabled && !descriptor.Lowering))
                {
                    throw std::logic_error("The built-in VFX node catalog is invalid.");
                }
            }
            if (result.size() != std::size(VfxNodeCatalogContract))
                throw std::logic_error("The built-in VFX node catalog does not match its tooling contract.");
            for (const auto& expected : VfxNodeCatalogContract)
            {
                const auto descriptor = std::ranges::find(result, expected.TypeId,
                                                          [](const VfxNodeDescriptor& candidate) -> std::string_view
                                                          { return candidate.TypeId.View(); });
                if (descriptor == result.end() || descriptor->Label != expected.Label ||
                    descriptor->Class != expected.Class || descriptor->SupportTier != expected.SupportTier ||
                    descriptor->BackendTier != expected.BackendTier)
                {
                    throw std::logic_error("The built-in VFX node catalog does not match its tooling contract.");
                }
            }
            return result;
        }
    } // namespace

    std::span<const VfxNodeDescriptor> VfxNodeCatalog()
    {
        static const auto catalog = BuildCatalog();
        return catalog;
    }

    const VfxNodeDescriptor* FindVfxNodeDescriptor(const std::string_view typeId)
    {
        const auto catalog = VfxNodeCatalog();
        const auto found = std::ranges::find(catalog, typeId, [](const VfxNodeDescriptor& descriptor)
                                             { return descriptor.TypeId.View(); });
        return found == catalog.end() ? nullptr : std::addressof(*found);
    }

    bool VfxValueMatchesType(const VfxValueType type, const VfxParameterValue& value) noexcept
    {
        switch (type)
        {
        case VfxValueType::Boolean:
            return std::holds_alternative<bool>(value);
        case VfxValueType::Integer:
            return std::holds_alternative<std::int64_t>(value);
        case VfxValueType::UnsignedInteger:
            return std::holds_alternative<std::uint64_t>(value);
        case VfxValueType::Scalar:
            return std::holds_alternative<float>(value);
        case VfxValueType::Vector2:
            return std::holds_alternative<Vector2>(value);
        case VfxValueType::Vector3:
            return std::holds_alternative<Vector3>(value);
        case VfxValueType::Vector4:
            return std::holds_alternative<Vector4>(value);
        case VfxValueType::Quaternion:
            return std::holds_alternative<Quaternion>(value);
        case VfxValueType::Color:
            return std::holds_alternative<Color>(value);
        case VfxValueType::Matrix:
            return std::holds_alternative<Matrix4>(value);
        case VfxValueType::Curve:
            return std::holds_alternative<Curve1D>(value);
        case VfxValueType::Gradient:
            return std::holds_alternative<ColorGradient>(value);
        case VfxValueType::ScalarRange:
            return std::holds_alternative<VfxScalarRange>(value);
        case VfxValueType::IntegerRange:
            return std::holds_alternative<VfxIntegerRange>(value);
        case VfxValueType::UnsignedIntegerRange:
            return std::holds_alternative<VfxUnsignedIntegerRange>(value);
        case VfxValueType::Vector2Range:
            return std::holds_alternative<VfxVector2Range>(value);
        case VfxValueType::Vector3Range:
            return std::holds_alternative<VfxVector3Range>(value);
        case VfxValueType::Vector4Range:
            return std::holds_alternative<VfxVector4Range>(value);
        case VfxValueType::ColorRange:
            return std::holds_alternative<VfxColorRange>(value);
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            return std::holds_alternative<AssetId>(value);
        case VfxValueType::ParticleStream:
            return false;
        }
        return false;
    }

    bool IsFiniteVfxValue(const VfxParameterValue& value) noexcept
    {
        return std::visit(
            [](const auto& item) noexcept
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<T, float>)
                    return std::isfinite(item);
                else if constexpr (std::same_as<T, Vector2> || std::same_as<T, Vector3> || std::same_as<T, Vector4> ||
                                   std::same_as<T, Quaternion> || std::same_as<T, Color>)
                    return Math::IsFinite(item);
                else if constexpr (std::same_as<T, Matrix4>)
                    return Math::IsFinite(item);
                else if constexpr (std::same_as<T, VfxScalarRange>)
                    return std::isfinite(item.Minimum) && std::isfinite(item.Maximum);
                else if constexpr (std::same_as<T, VfxVector2Range> || std::same_as<T, VfxVector3Range> ||
                                   std::same_as<T, VfxVector4Range> || std::same_as<T, VfxColorRange>)
                    return Math::IsFinite(item.Minimum) && Math::IsFinite(item.Maximum);
                else if constexpr (std::same_as<T, Curve1D>)
                {
                    return std::ranges::all_of(item.Keys(),
                                               [](const CurveKey& key)
                                               {
                                                   return std::isfinite(key.Time) && std::isfinite(key.Value) &&
                                                          std::isfinite(key.InTangent) && std::isfinite(key.OutTangent);
                                               });
                }
                else if constexpr (std::same_as<T, ColorGradient>)
                {
                    return std::ranges::all_of(item.Keys(), [](const ColorGradientKey& key)
                                               { return std::isfinite(key.Time) && Math::IsFinite(key.Value); });
                }
                else
                    return true;
            },
            value);
    }

    VfxParameterValue DefaultVfxValue(const VfxValueType type)
    {
        switch (type)
        {
        case VfxValueType::Boolean:
            return false;
        case VfxValueType::Integer:
            return std::int64_t{0};
        case VfxValueType::UnsignedInteger:
            return std::uint64_t{0};
        case VfxValueType::Scalar:
            return 0.0F;
        case VfxValueType::Vector2:
            return Vector2{};
        case VfxValueType::Vector3:
            return Vector3{};
        case VfxValueType::Vector4:
            return Vector4{};
        case VfxValueType::Quaternion:
            return Quaternion{};
        case VfxValueType::Color:
            return Color{};
        case VfxValueType::Matrix:
            return Matrix4{};
        case VfxValueType::Curve:
            return Curve1D::Constant(0.0F);
        case VfxValueType::Gradient:
            return ColorGradient::Constant(Color{});
        case VfxValueType::ScalarRange:
            return VfxScalarRange{};
        case VfxValueType::IntegerRange:
            return VfxIntegerRange{};
        case VfxValueType::UnsignedIntegerRange:
            return VfxUnsignedIntegerRange{};
        case VfxValueType::Vector2Range:
            return VfxVector2Range{};
        case VfxValueType::Vector3Range:
            return VfxVector3Range{};
        case VfxValueType::Vector4Range:
            return VfxVector4Range{};
        case VfxValueType::ColorRange:
            return VfxColorRange{};
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            return AssetId{};
        case VfxValueType::ParticleStream:
            break;
        }
        throw std::invalid_argument("Particle streams do not have literal values.");
    }

    VfxGraphNode CreateVfxGraphOperatorNode(const std::string_view typeId, const Vector2 editorPosition)
    {
        const auto* descriptor = FindVfxNodeDescriptor(typeId);
        if (!descriptor)
            throw std::invalid_argument("VFX operator type ID is unknown.");
        if (descriptor->Class != VfxNodeClass::Operator)
            throw std::invalid_argument("VFX catalog entry is not an Operator.");
        if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
            throw std::invalid_argument("VFX operator is disabled: " + descriptor->DisabledReason);

        VfxGraphNode result;
        result.Id = AssetId::Generate();
        result.Type = descriptor->Label;
        result.TypeId = descriptor->TypeId;
        result.Context = VfxContextType::Update;
        result.EditorPosition = editorPosition;
        result.Kind = VfxGraphNodeKind::Operator;
        result.DefinitionVersion = descriptor->DefinitionVersion;
        result.Pins.reserve(descriptor->Pins.size());
        result.ResolvedSignature.reserve(descriptor->Pins.size());
        for (const auto& specification : descriptor->Pins)
        {
            const auto id = AssetId::Generate();
            result.Pins.push_back({id, specification.Name, specification.Type, specification.Input,
                                   specification.Semantic, specification.DefaultValue});
            result.ResolvedSignature.push_back(specification.Type);
            if (descriptor->TypeBehavior == VfxNodeTypeBehavior::Cascaded && specification.Input)
                result.DynamicPinOrder.push_back(id);
        }
        result.Properties.reserve(descriptor->Settings.size());
        for (const auto& setting : descriptor->Settings)
            result.Properties.push_back({setting.Name, setting.DefaultValue});
        return result;
    }
} // namespace Keire
