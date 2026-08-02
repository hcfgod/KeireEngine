#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace Keire::Internal
{
    namespace
    {
        constexpr auto InvalidIndex = ~std::uint32_t{0};
        constexpr std::uint32_t MaximumValueRegisters = 4096;

        enum class WaveOperatorKind : std::uint8_t
        {
            Sawtooth,
            Sine,
            Square,
            Triangle
        };

        [[nodiscard]] std::optional<WaveOperatorKind> WaveOperator(const std::string_view typeId) noexcept
        {
            if (typeId == "keire.operator.sawtooth-wave")
                return WaveOperatorKind::Sawtooth;
            if (typeId == "keire.operator.sine-wave")
                return WaveOperatorKind::Sine;
            if (typeId == "keire.operator.square-wave")
                return WaveOperatorKind::Square;
            if (typeId == "keire.operator.triangle-wave")
                return WaveOperatorKind::Triangle;
            return std::nullopt;
        }

        [[nodiscard]] const VfxGraphProperty* FindProperty(const VfxGraphNode& node, const std::string_view name)
        {
            const auto found = std::ranges::find(node.Properties, name, &VfxGraphProperty::Name);
            return found == node.Properties.end() ? nullptr : std::addressof(*found);
        }

        template <typename T> [[nodiscard]] T Property(const VfxGraphNode& node, const std::string_view name)
        {
            const auto* property = FindProperty(node, name);
            if (!property || !std::holds_alternative<T>(property->Value))
                throw std::invalid_argument("VFX operator property '" + std::string(name) + "' is missing or invalid.");
            return std::get<T>(property->Value);
        }

        [[nodiscard]] VfxEvaluationDomain DomainForContext(const VfxContextType context) noexcept
        {
            switch (context)
            {
            case VfxContextType::Spawn:
            case VfxContextType::Initialize:
                return VfxEvaluationDomain::PerSpawn;
            case VfxContextType::Update:
                return VfxEvaluationDomain::PerParticleUpdate;
            case VfxContextType::Output:
            case VfxContextType::Event:
                return VfxEvaluationDomain::PerOutputEvent;
            }
            return VfxEvaluationDomain::PerEffect;
        }

        [[nodiscard]] VfxEvaluationDomain
        SourceDomain(const VfxCompiledValueSource& source,
                     const std::map<std::uint32_t, VfxEvaluationDomain>& registerDomains) noexcept
        {
            switch (source.Kind)
            {
            case VfxCompiledValueSourceKind::Literal:
                return VfxEvaluationDomain::CompileTimeConstant;
            case VfxCompiledValueSourceKind::Parameter:
                return VfxEvaluationDomain::PerEffect;
            case VfxCompiledValueSourceKind::Register:
            {
                const auto found = registerDomains.find(source.Index);
                return found == registerDomains.end() ? VfxEvaluationDomain::PerParticleUpdate : found->second;
            }
            }
            return VfxEvaluationDomain::PerParticleUpdate;
        }

        [[nodiscard]] float FiniteOrZero(const float value) noexcept { return std::isfinite(value) ? value : 0.0F; }

        [[nodiscard]] float RoundToEven(const float value) noexcept
        {
            if (!std::isfinite(value))
                return 0.0F;
            const auto lower = std::floor(value);
            const auto fraction = value - lower;
            if (fraction < 0.5F)
                return lower;
            if (fraction > 0.5F)
                return lower + 1.0F;
            return std::fmod(std::abs(lower), 2.0F) == 0.0F ? lower : lower + 1.0F;
        }

        template <typename T>
        [[nodiscard]] std::optional<VfxParameterValue>
        ConstructRange(const std::span<const VfxParameterValue* const> inputs) noexcept
        {
            if (inputs.size() != 2 || !inputs[0] || !inputs[1] || !std::holds_alternative<T>(*inputs[0]) ||
                !std::holds_alternative<T>(*inputs[1]))
            {
                return std::nullopt;
            }
            return VfxRange<T>{std::get<T>(*inputs[0]), std::get<T>(*inputs[1])};
        }

        [[nodiscard]] std::optional<VfxParameterValue>
        ExecutePure(const VfxValueOpcode opcode, const std::span<const VfxParameterValue* const> inputs,
                    const VfxValueType outputType, const bool clampRemap, const VfxComparisonCondition comparison,
                    const std::uint32_t outputIndex = 0) noexcept
        {
            const auto scalar = [&inputs](const std::size_t index) -> std::optional<float>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<float>(*inputs[index]))
                    return std::nullopt;
                return std::get<float>(*inputs[index]);
            };
            const auto boolean = [&inputs](const std::size_t index) -> std::optional<bool>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<bool>(*inputs[index]))
                    return std::nullopt;
                return std::get<bool>(*inputs[index]);
            };
            const auto vector3 = [&inputs](const std::size_t index) -> std::optional<Vector3>
            {
                if (index >= inputs.size() || !inputs[index] || !std::holds_alternative<Vector3>(*inputs[index]))
                    return std::nullopt;
                return std::get<Vector3>(*inputs[index]);
            };
            switch (opcode)
            {
            case VfxValueOpcode::Range:
            {
                if (const auto result = ConstructRange<float>(inputs))
                    return result;
                if (const auto result = ConstructRange<std::int64_t>(inputs))
                    return result;
                if (const auto result = ConstructRange<std::uint64_t>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector2>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector3>(inputs))
                    return result;
                if (const auto result = ConstructRange<Vector4>(inputs))
                    return result;
                if (const auto result = ConstructRange<Color>(inputs))
                    return result;
                return std::nullopt;
            }
            case VfxValueOpcode::Remap:
            {
                const auto input = scalar(0);
                if (!input || inputs.size() < 3 || !std::holds_alternative<VfxScalarRange>(*inputs[1]) ||
                    !std::holds_alternative<VfxScalarRange>(*inputs[2]))
                {
                    return std::nullopt;
                }
                const auto source = std::get<VfxScalarRange>(*inputs[1]);
                const auto destination = std::get<VfxScalarRange>(*inputs[2]);
                const auto width = source.Maximum - source.Minimum;
                auto factor = width == 0.0F ? 0.0F : (*input - source.Minimum) / width;
                if (clampRemap)
                    factor = std::clamp(factor, 0.0F, 1.0F);
                return FiniteOrZero(destination.Minimum + factor * (destination.Maximum - destination.Minimum));
            }
            case VfxValueOpcode::Add:
            case VfxValueOpcode::Subtract:
            case VfxValueOpcode::Multiply:
            case VfxValueOpcode::Divide:
            case VfxValueOpcode::Minimum:
            case VfxValueOpcode::Maximum:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                if (!left || !right)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Add)
                    return FiniteOrZero(*left + *right);
                if (opcode == VfxValueOpcode::Subtract)
                    return FiniteOrZero(*left - *right);
                if (opcode == VfxValueOpcode::Multiply)
                    return FiniteOrZero(*left * *right);
                if (opcode == VfxValueOpcode::Divide)
                    return *right == 0.0F ? 0.0F : FiniteOrZero(*left / *right);
                if (opcode == VfxValueOpcode::Minimum)
                    return std::min(*left, *right);
                return std::max(*left, *right);
            }
            case VfxValueOpcode::Clamp:
            {
                const auto input = scalar(0);
                const auto minimum = scalar(1);
                const auto maximum = scalar(2);
                if (!input || !minimum || !maximum)
                    return std::nullopt;
                const auto low = std::min(*minimum, *maximum);
                const auto high = std::max(*minimum, *maximum);
                return std::clamp(*input, low, high);
            }
            case VfxValueOpcode::Saturate:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(std::clamp(*input, 0.0F, 1.0F)) : std::nullopt;
            }
            case VfxValueOpcode::Absolute:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(std::abs(*input)) : std::nullopt;
            }
            case VfxValueOpcode::Sine:
            case VfxValueOpcode::Cosine:
            case VfxValueOpcode::Tangent:
            case VfxValueOpcode::ArcSine:
            case VfxValueOpcode::ArcCosine:
            case VfxValueOpcode::ArcTangent:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if ((opcode == VfxValueOpcode::ArcSine || opcode == VfxValueOpcode::ArcCosine) &&
                    (*input < -1.0F || *input > 1.0F))
                {
                    return 0.0F;
                }
                if (opcode == VfxValueOpcode::Sine)
                    return FiniteOrZero(std::sin(*input));
                if (opcode == VfxValueOpcode::Cosine)
                    return FiniteOrZero(std::cos(*input));
                if (opcode == VfxValueOpcode::Tangent)
                    return FiniteOrZero(std::tan(*input));
                if (opcode == VfxValueOpcode::ArcSine)
                    return FiniteOrZero(std::asin(*input));
                if (opcode == VfxValueOpcode::ArcCosine)
                    return FiniteOrZero(std::acos(*input));
                return FiniteOrZero(std::atan(*input));
            }
            case VfxValueOpcode::Atan2:
            {
                const auto y = scalar(0);
                const auto x = scalar(1);
                if (!y || !x)
                    return std::nullopt;
                return *y == 0.0F && *x == 0.0F ? 0.0F : FiniteOrZero(std::atan2(*y, *x));
            }
            case VfxValueOpcode::Power:
            {
                const auto base = scalar(0);
                const auto exponent = scalar(1);
                return base && exponent ? std::optional<VfxParameterValue>(FiniteOrZero(std::pow(*base, *exponent)))
                                        : std::nullopt;
            }
            case VfxValueOpcode::SquareRoot:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                return *input < 0.0F ? 0.0F : FiniteOrZero(std::sqrt(*input));
            }
            case VfxValueOpcode::Exponential:
            {
                const auto input = scalar(0);
                return input ? std::optional<VfxParameterValue>(FiniteOrZero(std::exp(*input))) : std::nullopt;
            }
            case VfxValueOpcode::Logarithm:
            case VfxValueOpcode::LogarithmBase2:
            case VfxValueOpcode::LogarithmBase10:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (*input <= 0.0F)
                    return 0.0F;
                if (opcode == VfxValueOpcode::Logarithm)
                    return FiniteOrZero(std::log(*input));
                if (opcode == VfxValueOpcode::LogarithmBase2)
                    return FiniteOrZero(std::log2(*input));
                return FiniteOrZero(std::log10(*input));
            }
            case VfxValueOpcode::Ceiling:
            case VfxValueOpcode::Floor:
            case VfxValueOpcode::Round:
            case VfxValueOpcode::Fractional:
            case VfxValueOpcode::Negate:
            case VfxValueOpcode::Sign:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Ceiling)
                    return FiniteOrZero(std::ceil(*input));
                if (opcode == VfxValueOpcode::Floor)
                    return FiniteOrZero(std::floor(*input));
                if (opcode == VfxValueOpcode::Round)
                    return RoundToEven(*input);
                if (opcode == VfxValueOpcode::Fractional)
                    return FiniteOrZero(*input - std::floor(*input));
                if (opcode == VfxValueOpcode::Negate)
                    return FiniteOrZero(-*input);
                return *input > 0.0F ? 1.0F : *input < 0.0F ? -1.0F : 0.0F;
            }
            case VfxValueOpcode::Lerp:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                const auto factor = scalar(2);
                return left && right && factor
                           ? std::optional<VfxParameterValue>(FiniteOrZero(std::lerp(*left, *right, *factor)))
                           : std::nullopt;
            }
            case VfxValueOpcode::Smoothstep:
            {
                const auto edge1 = scalar(0);
                const auto edge2 = scalar(1);
                const auto input = scalar(2);
                if (!edge1 || !edge2 || !input)
                    return std::nullopt;
                const auto width = *edge2 - *edge1;
                if (width == 0.0F || !std::isfinite(width))
                    return 0.0F;
                const auto factor = std::clamp(FiniteOrZero((*input - *edge1) / width), 0.0F, 1.0F);
                return FiniteOrZero(factor * factor * (3.0F - 2.0F * factor));
            }
            case VfxValueOpcode::Step:
            {
                const auto edge = scalar(0);
                const auto input = scalar(1);
                return edge && input ? std::optional<VfxParameterValue>(*input < *edge ? 0.0F : 1.0F) : std::nullopt;
            }
            case VfxValueOpcode::Compare:
            {
                const auto left = scalar(0);
                const auto right = scalar(1);
                if (!left || !right)
                    return std::nullopt;
                switch (comparison)
                {
                case VfxComparisonCondition::Less:
                    return *left < *right;
                case VfxComparisonCondition::LessOrEqual:
                    return *left <= *right;
                case VfxComparisonCondition::Equal:
                    return *left == *right;
                case VfxComparisonCondition::NotEqual:
                    return *left != *right;
                case VfxComparisonCondition::GreaterOrEqual:
                    return *left >= *right;
                case VfxComparisonCondition::Greater:
                    return *left > *right;
                }
                return std::nullopt;
            }
            case VfxValueOpcode::Select:
                if (inputs.size() == 3 && inputs[0] && std::holds_alternative<bool>(*inputs[0]) && inputs[1] &&
                    inputs[2])
                {
                    return std::get<bool>(*inputs[0]) ? *inputs[1] : *inputs[2];
                }
                return std::nullopt;
            case VfxValueOpcode::BooleanAnd:
            case VfxValueOpcode::BooleanOr:
            {
                const auto left = boolean(0);
                const auto right = boolean(1);
                if (!left || !right)
                    return std::nullopt;
                return opcode == VfxValueOpcode::BooleanAnd ? *left && *right : *left || *right;
            }
            case VfxValueOpcode::BooleanNot:
            {
                const auto input = boolean(0);
                return input ? std::optional<VfxParameterValue>(!*input) : std::nullopt;
            }
            case VfxValueOpcode::Combine:
            {
                const auto x = scalar(0);
                const auto y = scalar(1);
                if (!x || !y)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector2 && inputs.size() == 2)
                    return Vector2{*x, *y};
                const auto z = scalar(2);
                if (!z)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector3 && inputs.size() == 3)
                    return Vector3{*x, *y, *z};
                const auto w = scalar(3);
                if (!w || inputs.size() != 4)
                    return std::nullopt;
                if (outputType == VfxValueType::Vector4)
                    return Vector4{*x, *y, *z, *w};
                if (outputType == VfxValueType::Color)
                    return Color{*x, *y, *z, *w};
                return std::nullopt;
            }
            case VfxValueOpcode::Split:
            {
                if (outputType != VfxValueType::Scalar || inputs.size() != 1 || !inputs[0])
                    return std::nullopt;
                if (const auto* input = std::get_if<Vector2>(inputs[0]); input && outputIndex < 2)
                    return outputIndex == 0 ? input->X : input->Y;
                if (const auto* input = std::get_if<Vector3>(inputs[0]); input && outputIndex < 3)
                {
                    const std::array values{input->X, input->Y, input->Z};
                    return values[outputIndex];
                }
                if (const auto* input = std::get_if<Vector4>(inputs[0]); input && outputIndex < 4)
                {
                    const std::array values{input->X, input->Y, input->Z, input->W};
                    return values[outputIndex];
                }
                if (const auto* input = std::get_if<Color>(inputs[0]); input && outputIndex < 4)
                {
                    const std::array values{input->Red, input->Green, input->Blue, input->Alpha};
                    return values[outputIndex];
                }
                return std::nullopt;
            }
            case VfxValueOpcode::Dot:
            case VfxValueOpcode::Cross:
            case VfxValueOpcode::Distance:
            {
                const auto left = vector3(0);
                const auto right = vector3(1);
                if (!left || !right)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::Dot)
                    return FiniteOrZero(left->X * right->X + left->Y * right->Y + left->Z * right->Z);
                if (opcode == VfxValueOpcode::Cross)
                {
                    return Vector3{FiniteOrZero(left->Y * right->Z - left->Z * right->Y),
                                   FiniteOrZero(left->Z * right->X - left->X * right->Z),
                                   FiniteOrZero(left->X * right->Y - left->Y * right->X)};
                }
                const auto x = left->X - right->X;
                const auto y = left->Y - right->Y;
                const auto z = left->Z - right->Z;
                return FiniteOrZero(std::sqrt(x * x + y * y + z * z));
            }
            case VfxValueOpcode::Normalize:
            case VfxValueOpcode::Length:
            {
                const auto input = vector3(0);
                if (!input)
                    return std::nullopt;
                const auto length =
                    FiniteOrZero(std::sqrt(input->X * input->X + input->Y * input->Y + input->Z * input->Z));
                if (opcode == VfxValueOpcode::Length)
                    return length;
                if (length <= std::numeric_limits<float>::epsilon())
                    return Vector3{};
                return Vector3{FiniteOrZero(input->X / length), FiniteOrZero(input->Y / length),
                               FiniteOrZero(input->Z / length)};
            }
            case VfxValueOpcode::ToFloat:
                if (inputs.size() == 1 && inputs[0])
                {
                    if (std::holds_alternative<std::int64_t>(*inputs[0]))
                        return FiniteOrZero(static_cast<float>(std::get<std::int64_t>(*inputs[0])));
                    if (std::holds_alternative<std::uint64_t>(*inputs[0]))
                        return FiniteOrZero(static_cast<float>(std::get<std::uint64_t>(*inputs[0])));
                }
                return std::nullopt;
            case VfxValueOpcode::ToInteger:
            {
                const auto input = scalar(0);
                if (!input)
                    return std::nullopt;
                if (*input <= static_cast<float>(std::numeric_limits<std::int64_t>::lowest()))
                    return std::numeric_limits<std::int64_t>::lowest();
                if (*input >= static_cast<float>(std::numeric_limits<std::int64_t>::max()))
                    return std::numeric_limits<std::int64_t>::max();
                return static_cast<std::int64_t>(*input);
            }
            case VfxValueOpcode::ToUnsignedInteger:
            {
                const auto input = scalar(0);
                if (!input || *input <= 0.0F)
                    return std::uint64_t{0};
                if (*input >= static_cast<float>(std::numeric_limits<std::uint64_t>::max()))
                    return std::numeric_limits<std::uint64_t>::max();
                return static_cast<std::uint64_t>(*input);
            }
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] VfxComparisonCondition ParseComparison(const VfxGraphNode& node)
        {
            const auto value = Property<std::string>(node, "Condition");
            if (value == "Less")
                return VfxComparisonCondition::Less;
            if (value == "Less Or Equal")
                return VfxComparisonCondition::LessOrEqual;
            if (value == "Equal")
                return VfxComparisonCondition::Equal;
            if (value == "Not Equal")
                return VfxComparisonCondition::NotEqual;
            if (value == "Greater Or Equal")
                return VfxComparisonCondition::GreaterOrEqual;
            if (value == "Greater")
                return VfxComparisonCondition::Greater;
            throw std::invalid_argument("VFX Compare operator condition is unsupported.");
        }

        [[nodiscard]] std::uint32_t HashWord(std::uint32_t value) noexcept
        {
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            value ^= value >> 16U;
            return value;
        }

        void Mix(std::uint32_t& state, const std::uint64_t value) noexcept
        {
            state = HashWord(state ^ static_cast<std::uint32_t>(value));
            state = HashWord(state ^ static_cast<std::uint32_t>(value >> 32U));
        }

        [[nodiscard]] std::uint32_t RandomWord(const VfxCompiledValueInstruction& instruction,
                                               const VfxExpressionEvaluationContext& context,
                                               const std::uint32_t componentChannel = 0) noexcept
        {
            auto state = HashWord(context.EffectSeed ^ context.SeedOffset ^ 0x9e3779b9U);
            Mix(state, instruction.Node.High());
            Mix(state, instruction.Node.Low());
            Mix(state, context.System.High());
            Mix(state, context.System.Low());
            Mix(state, static_cast<std::uint64_t>(instruction.ChannelSalt) + componentChannel);
            if (!instruction.ConstantRandom)
            {
                Mix(state, static_cast<std::uint32_t>(instruction.Context));
                switch (instruction.RandomScope)
                {
                case VfxRandomScope::PerParticle:
                    Mix(state, context.ParticleId);
                    Mix(state, context.SpawnIndex);
                    break;
                case VfxRandomScope::PerVfxComponent:
                    break;
                case VfxRandomScope::PerParticleStrip:
                    Mix(state, context.StripId);
                    break;
                }
                Mix(state, context.SimulationStep);
            }
            return HashWord(state);
        }

        [[nodiscard]] float RandomUnit(const VfxCompiledValueInstruction& instruction,
                                       const VfxExpressionEvaluationContext& context, const bool inclusiveMaximum,
                                       const std::uint32_t componentChannel = 0) noexcept
        {
            const auto sample = RandomWord(instruction, context, componentChannel) >> 8U;
            return static_cast<float>(sample) * (inclusiveMaximum ? (1.0F / 16'777'215.0F) : (1.0F / 16'777'216.0F));
        }

        [[nodiscard]] std::uint32_t RandomComponentChannel(const VfxCompiledValueInstruction& instruction,
                                                           const std::uint32_t component) noexcept
        {
            return instruction.IndependentRandomChannels ? component : 0;
        }

        [[nodiscard]] float RandomFloat(const VfxCompiledValueInstruction& instruction,
                                        const VfxExpressionEvaluationContext& context, const float minimum,
                                        const float maximum, const bool inclusiveMaximum,
                                        const std::uint32_t component) noexcept
        {
            const auto unit =
                RandomUnit(instruction, context, inclusiveMaximum, RandomComponentChannel(instruction, component));
            return FiniteOrZero(minimum + (maximum - minimum) * unit);
        }

        [[nodiscard]] std::int64_t RandomInteger(const VfxCompiledValueInstruction& instruction,
                                                 const VfxExpressionEvaluationContext& context, std::int64_t minimum,
                                                 std::int64_t maximum, const bool inclusiveMaximum) noexcept
        {
            if (minimum > maximum)
                std::swap(minimum, maximum);
            constexpr auto sign = std::uint64_t{1} << 63U;
            const auto orderedMinimum = static_cast<std::uint64_t>(minimum) ^ sign;
            const auto orderedMaximum = static_cast<std::uint64_t>(maximum) ^ sign;
            const auto span = orderedMaximum - orderedMinimum + (inclusiveMaximum ? 1U : 0U);
            if (span == 0 && !inclusiveMaximum)
                return minimum;
            const auto first = static_cast<std::uint64_t>(RandomWord(instruction, context));
            const auto second = static_cast<std::uint64_t>(HashWord(static_cast<std::uint32_t>(first) ^ 0xa511e9b3U));
            const auto sample = (first << 32U) | second;
            const auto offset = span == 0 ? sample : sample % span;
            return static_cast<std::int64_t>((orderedMinimum + offset) ^ sign);
        }

        [[nodiscard]] std::uint64_t RandomUnsignedInteger(const VfxCompiledValueInstruction& instruction,
                                                          const VfxExpressionEvaluationContext& context,
                                                          std::uint64_t minimum, std::uint64_t maximum,
                                                          const bool inclusiveMaximum) noexcept
        {
            if (minimum > maximum)
                std::swap(minimum, maximum);
            const auto span = maximum - minimum + (inclusiveMaximum ? 1U : 0U);
            if (span == 0 && !inclusiveMaximum)
                return minimum;
            const auto first = static_cast<std::uint64_t>(RandomWord(instruction, context));
            const auto second = static_cast<std::uint64_t>(HashWord(static_cast<std::uint32_t>(first) ^ 0xa511e9b3U));
            const auto sample = (first << 32U) | second;
            return minimum + (span == 0 ? sample : sample % span);
        }

        template <typename T>
        [[nodiscard]] std::optional<VfxRange<T>>
        RandomBounds(const VfxCompiledValueInstruction& instruction,
                     const std::span<const VfxParameterValue* const> inputs) noexcept
        {
            if (instruction.Opcode == VfxValueOpcode::Random)
            {
                if (inputs.size() != 2 || !inputs[0] || !inputs[1] || !std::holds_alternative<T>(*inputs[0]) ||
                    !std::holds_alternative<T>(*inputs[1]))
                {
                    return std::nullopt;
                }
                return VfxRange<T>{std::get<T>(*inputs[0]), std::get<T>(*inputs[1])};
            }
            if (inputs.size() != 1 || !inputs[0] || !std::holds_alternative<VfxRange<T>>(*inputs[0]))
                return std::nullopt;
            return std::get<VfxRange<T>>(*inputs[0]);
        }
    } // namespace

    VfxExpressionCompilation CompileVfxExpressions(const VfxGraphSystem& system,
                                                   const std::map<AssetId, std::uint32_t>& parameterSlots,
                                                   const std::span<const AssetId> requiredOutputPins)
    {
        std::map<AssetId, const VfxGraphNode*> nodes;
        std::map<AssetId, std::pair<const VfxGraphNode*, const VfxGraphPin*>> pins;
        std::map<AssetId, const VfxGraphConnection*> inputDrivers;
        for (const auto& node : system.Nodes)
        {
            nodes.emplace(node.Id, std::addressof(node));
            for (const auto& pin : node.Pins)
                pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
            for (const auto& block : node.Blocks)
                for (const auto& pin : block.Pins)
                    pins.emplace(pin.Id, std::pair{std::addressof(node), std::addressof(pin)});
        }
        for (const auto& connection : system.Connections)
            inputDrivers.emplace(connection.InputPin, std::addressof(connection));

        VfxExpressionCompilation result;
        std::map<std::uint32_t, VfxEvaluationDomain> registerDomains;
        std::set<AssetId> compiling;
        std::function<VfxCompiledValueSource(AssetId)> compileOutput;
        compileOutput = [&](const AssetId outputPin) -> VfxCompiledValueSource
        {
            if (const auto cached = result.SourcesByOutputPin.find(outputPin);
                cached != result.SourcesByOutputPin.end())
            {
                return cached->second;
            }
            const auto located = pins.find(outputPin);
            if (located == pins.end() || located->second.second->Input)
                throw std::invalid_argument("VFX value expression references an invalid output pin.");
            const auto& node = *located->second.first;
            const auto& pin = *located->second.second;
            if (node.Kind == VfxGraphNodeKind::Parameter)
            {
                const auto slot = parameterSlots.find(node.Reference);
                if (slot == parameterSlots.end())
                    throw std::invalid_argument("VFX value expression references an unknown Blackboard parameter.");
                VfxCompiledValueSource source{VfxCompiledValueSourceKind::Parameter, pin.Type, slot->second, 0.0F};
                result.SourcesByOutputPin.emplace(outputPin, source);
                return source;
            }
            if (node.Kind != VfxGraphNodeKind::Operator)
                throw std::invalid_argument("VFX data inputs require a Parameter or executable Operator source.");
            if (!compiling.insert(node.Id).second)
                throw std::invalid_argument("VFX value expression contains a cycle.");

            const auto* descriptor = FindVfxNodeDescriptor(node.TypeId.View());
            if (!descriptor || descriptor->Class != VfxNodeClass::Operator || !descriptor->Lowering)
                throw std::invalid_argument("VFX graph contains an unknown executable Operator type ID.");
            if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
                throw std::invalid_argument("VFX Operator is disabled: " + descriptor->DisabledReason);
            if (node.DefinitionVersion != descriptor->DefinitionVersion || node.Pins.size() != descriptor->Pins.size())
                throw std::invalid_argument("VFX Operator node does not match its catalog definition version.");

            std::vector<VfxCompiledValueSource> inputs;
            const VfxGraphPin* output = nullptr;
            std::uint32_t outputIndex = 0;
            std::uint32_t nextOutputIndex = 0;
            for (std::size_t index = 0; index < descriptor->Pins.size(); ++index)
            {
                const auto& expected = descriptor->Pins[index];
                const auto& actual = node.Pins[index];
                if (actual.Name != expected.Name || actual.Semantic != expected.Semantic ||
                    actual.Type != expected.Type || actual.Input != expected.Input)
                {
                    throw std::invalid_argument("VFX Operator node pin signature is not canonical.");
                }
                if (!actual.Input)
                {
                    if (actual.Id == outputPin)
                    {
                        output = std::addressof(actual);
                        outputIndex = nextOutputIndex;
                    }
                    ++nextOutputIndex;
                    continue;
                }
                const auto driver = inputDrivers.find(actual.Id);
                if (driver == inputDrivers.end())
                {
                    if (!actual.DefaultValue || !VfxValueMatchesType(actual.Type, *actual.DefaultValue) ||
                        !IsFiniteVfxValue(*actual.DefaultValue))
                    {
                        throw std::invalid_argument("VFX Operator input has no cable or compatible inline value.");
                    }
                    inputs.push_back({VfxCompiledValueSourceKind::Literal, actual.Type, 0, *actual.DefaultValue});
                }
                else
                {
                    auto source = compileOutput(driver->second->OutputPin);
                    if (source.Type != actual.Type)
                        throw std::invalid_argument("VFX Operator input source type is incompatible.");
                    inputs.push_back(std::move(source));
                }
            }
            if (!output)
                throw std::invalid_argument("VFX Operator has no output pin.");

            if (const auto wave = WaveOperator(node.TypeId.View()))
            {
                if (inputs.size() != 4 || outputIndex != 0 || output->Type != VfxValueType::Scalar)
                    throw std::invalid_argument("VFX Wave Operator signature is not canonical.");

                const auto literal = [](const float value)
                { return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0, value}; };
                const auto emitPrimitive =
                    [&](const VfxValueOpcode opcode, std::vector<VfxCompiledValueSource> primitiveInputs)
                {
                    VfxCompiledValueInstruction primitive;
                    primitive.Node = node.Id;
                    primitive.Opcode = opcode;
                    primitive.Type = VfxValueType::Scalar;
                    primitive.Context = node.Context;
                    primitive.Inputs = std::move(primitiveInputs);
                    primitive.Domain = VfxEvaluationDomain::CompileTimeConstant;
                    for (const auto& input : primitive.Inputs)
                        primitive.Domain = std::max(primitive.Domain, SourceDomain(input, registerDomains));

                    std::optional<VfxParameterValue> folded;
                    if (primitive.Domain == VfxEvaluationDomain::CompileTimeConstant)
                    {
                        std::array<const VfxParameterValue*, 4> values{};
                        if (primitive.Inputs.size() <= values.size() &&
                            std::ranges::all_of(primitive.Inputs, [](const VfxCompiledValueSource& source)
                                                { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                        {
                            for (std::size_t index = 0; index < primitive.Inputs.size(); ++index)
                                values[index] = std::addressof(primitive.Inputs[index].Literal);
                            folded = ExecutePure(
                                primitive.Opcode,
                                std::span<const VfxParameterValue* const>(values.data(), primitive.Inputs.size()),
                                VfxValueType::Scalar, false, VfxComparisonCondition::Less);
                        }
                    }
                    if (folded)
                    {
                        return VfxCompiledValueSource{VfxCompiledValueSourceKind::Literal, VfxValueType::Scalar, 0,
                                                      std::move(*folded)};
                    }

                    if (result.RegisterCount >= MaximumValueRegisters)
                        throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                    primitive.OutputRegister = result.RegisterCount++;
                    registerDomains.emplace(primitive.OutputRegister, primitive.Domain);
                    const VfxCompiledValueSource source{VfxCompiledValueSourceKind::Register, VfxValueType::Scalar,
                                                        primitive.OutputRegister, 0.0F};
                    result.Instructions.push_back(std::move(primitive));
                    return source;
                };

                const auto phase = emitPrimitive(VfxValueOpcode::Multiply, {inputs[0], inputs[1]});
                VfxCompiledValueSource factor;
                switch (*wave)
                {
                case WaveOperatorKind::Sawtooth:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Absolute, {fractional});
                    break;
                }
                case WaveOperatorKind::Sine:
                {
                    constexpr float Tau = 6.28318530717958647692F;
                    const auto angle = emitPrimitive(VfxValueOpcode::Multiply, {phase, literal(Tau)});
                    const auto cosine = emitPrimitive(VfxValueOpcode::Cosine, {angle});
                    const auto numerator = emitPrimitive(VfxValueOpcode::Subtract, {literal(1.0F), cosine});
                    factor = emitPrimitive(VfxValueOpcode::Divide, {numerator, literal(2.0F)});
                    break;
                }
                case WaveOperatorKind::Square:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    factor = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    break;
                }
                case WaveOperatorKind::Triangle:
                {
                    const auto fractional = emitPrimitive(VfxValueOpcode::Fractional, {phase});
                    const auto slope = emitPrimitive(VfxValueOpcode::Round, {fractional});
                    const auto delta = emitPrimitive(VfxValueOpcode::Subtract, {slope, fractional});
                    const auto distance = emitPrimitive(VfxValueOpcode::Absolute, {delta});
                    factor = emitPrimitive(VfxValueOpcode::Multiply, {literal(2.0F), distance});
                    break;
                }
                }

                auto source = emitPrimitive(VfxValueOpcode::Lerp, {inputs[2], inputs[3], factor});
                result.SourcesByOutputPin.emplace(output->Id, source);
                compiling.erase(node.Id);
                return source;
            }

            VfxCompiledValueInstruction instruction;
            instruction.Node = node.Id;
            instruction.Opcode = *descriptor->Lowering;
            instruction.Type = output->Type;
            instruction.Context = node.Context;
            instruction.OutputIndex = outputIndex;
            instruction.Inputs = inputs;
            instruction.ClampRemap = instruction.Opcode == VfxValueOpcode::Remap && Property<bool>(node, "Clamp");
            if (instruction.Opcode == VfxValueOpcode::Compare)
                instruction.Comparison = ParseComparison(node);
            if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                const auto scope = Property<std::uint64_t>(node, "Scope");
                if (scope > static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip))
                    throw std::invalid_argument("VFX Random scope is unsupported.");
                instruction.RandomScope = static_cast<VfxRandomScope>(scope);
                instruction.ConstantRandom = Property<bool>(node, "Constant");
                instruction.IndependentRandomChannels = Property<bool>(node, "Independent Channels");
                if (instruction.Opcode == VfxValueOpcode::RandomRange)
                    instruction.InclusiveMaximum = Property<bool>(node, "Inclusive Maximum");
            }

            instruction.Domain = VfxEvaluationDomain::CompileTimeConstant;
            for (const auto& input : inputs)
                instruction.Domain = std::max(instruction.Domain, SourceDomain(input, registerDomains));
            if (instruction.Opcode == VfxValueOpcode::Time || instruction.Opcode == VfxValueOpcode::DeltaTime)
                instruction.Domain = VfxEvaluationDomain::PerFrame;
            else if (instruction.Opcode == VfxValueOpcode::Age || instruction.Opcode == VfxValueOpcode::Lifetime ||
                     instruction.Opcode == VfxValueOpcode::ParticleId ||
                     instruction.Opcode == VfxValueOpcode::SpawnIndex)
            {
                instruction.Domain = DomainForContext(node.Context);
            }
            else if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
            {
                instruction.Domain =
                    instruction.ConstantRandom ? VfxEvaluationDomain::PerEffect : DomainForContext(node.Context);
            }

            std::optional<VfxParameterValue> folded;
            if (instruction.Domain == VfxEvaluationDomain::CompileTimeConstant)
            {
                std::array<const VfxParameterValue*, 4> values{};
                if (inputs.size() <= values.size() &&
                    std::ranges::all_of(inputs, [](const VfxCompiledValueSource& source)
                                        { return source.Kind == VfxCompiledValueSourceKind::Literal; }))
                {
                    for (std::size_t index = 0; index < inputs.size(); ++index)
                        values[index] = std::addressof(inputs[index].Literal);
                    folded = ExecutePure(
                        instruction.Opcode, std::span<const VfxParameterValue* const>(values.data(), inputs.size()),
                        instruction.Type, instruction.ClampRemap, instruction.Comparison, instruction.OutputIndex);
                }
            }

            VfxCompiledValueSource source;
            if (folded)
                source = {VfxCompiledValueSourceKind::Literal, output->Type, 0, std::move(*folded)};
            else
            {
                if (result.RegisterCount >= MaximumValueRegisters)
                    throw std::invalid_argument("VFX expression exceeds the 4096-register compiler safety limit.");
                instruction.OutputRegister = result.RegisterCount++;
                registerDomains.emplace(instruction.OutputRegister, instruction.Domain);
                source = {VfxCompiledValueSourceKind::Register, output->Type, instruction.OutputRegister, 0.0F};
                result.Instructions.push_back(std::move(instruction));
            }
            result.SourcesByOutputPin.emplace(output->Id, source);
            compiling.erase(node.Id);
            return source;
        };

        for (const auto outputPin : requiredOutputPins)
            (void)compileOutput(outputPin);
        return result;
    }

    const VfxParameterValue* ResolveVfxValueSource(const VfxCompiledValueSource& source,
                                                   const std::span<const VfxParameterValue> parameters,
                                                   const std::span<const VfxParameterValue> registers) noexcept
    {
        switch (source.Kind)
        {
        case VfxCompiledValueSourceKind::Literal:
            return std::addressof(source.Literal);
        case VfxCompiledValueSourceKind::Parameter:
            return source.Index < parameters.size() ? std::addressof(parameters[source.Index]) : nullptr;
        case VfxCompiledValueSourceKind::Register:
            return source.Index < registers.size() ? std::addressof(registers[source.Index]) : nullptr;
        }
        return nullptr;
    }

    bool PackVfxGpuValue(const VfxValueType type, const VfxParameterValue& value, VfxGpuValue& packed) noexcept
    {
        if (!VfxValueMatchesType(type, value) || !IsFiniteVfxValue(value))
            return false;

        packed = {};
        const auto scalar = [](std::array<std::uint32_t, 4>& lane, const float component)
        { lane[0] = std::bit_cast<std::uint32_t>(component); };
        const auto integer = [](std::array<std::uint32_t, 4>& lane, const std::uint64_t component)
        {
            lane[0] = static_cast<std::uint32_t>(component);
            lane[1] = static_cast<std::uint32_t>(component >> 32U);
        };
        const auto vector2 = [](std::array<std::uint32_t, 4>& lane, const Vector2 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
        };
        const auto vector3 = [](std::array<std::uint32_t, 4>& lane, const Vector3 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
        };
        const auto vector4 = [](std::array<std::uint32_t, 4>& lane, const Vector4 component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto quaternion = [](std::array<std::uint32_t, 4>& lane, const Quaternion component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.X);
            lane[1] = std::bit_cast<std::uint32_t>(component.Y);
            lane[2] = std::bit_cast<std::uint32_t>(component.Z);
            lane[3] = std::bit_cast<std::uint32_t>(component.W);
        };
        const auto color = [](std::array<std::uint32_t, 4>& lane, const Color component)
        {
            lane[0] = std::bit_cast<std::uint32_t>(component.Red);
            lane[1] = std::bit_cast<std::uint32_t>(component.Green);
            lane[2] = std::bit_cast<std::uint32_t>(component.Blue);
            lane[3] = std::bit_cast<std::uint32_t>(component.Alpha);
        };
        const auto asset = [](std::array<std::uint32_t, 4>& lane, const AssetId component)
        {
            lane[0] = static_cast<std::uint32_t>(component.High());
            lane[1] = static_cast<std::uint32_t>(component.High() >> 32U);
            lane[2] = static_cast<std::uint32_t>(component.Low());
            lane[3] = static_cast<std::uint32_t>(component.Low() >> 32U);
        };

        switch (type)
        {
        case VfxValueType::Boolean:
            packed.Primary[0] = std::get<bool>(value) ? 1U : 0U;
            return true;
        case VfxValueType::Integer:
            integer(packed.Primary, std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value)));
            return true;
        case VfxValueType::Scalar:
            scalar(packed.Primary, std::get<float>(value));
            return true;
        case VfxValueType::Vector2:
            vector2(packed.Primary, std::get<Vector2>(value));
            return true;
        case VfxValueType::Vector3:
            vector3(packed.Primary, std::get<Vector3>(value));
            return true;
        case VfxValueType::Color:
            color(packed.Primary, std::get<Color>(value));
            return true;
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            asset(packed.Primary, std::get<AssetId>(value));
            return true;
        case VfxValueType::UnsignedInteger:
            integer(packed.Primary, std::get<std::uint64_t>(value));
            return true;
        case VfxValueType::Vector4:
            vector4(packed.Primary, std::get<Vector4>(value));
            return true;
        case VfxValueType::Quaternion:
            quaternion(packed.Primary, std::get<Quaternion>(value));
            return true;
        case VfxValueType::ScalarRange:
        {
            const auto range = std::get<VfxScalarRange>(value);
            scalar(packed.Primary, range.Minimum);
            scalar(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::IntegerRange:
        {
            const auto range = std::get<VfxIntegerRange>(value);
            integer(packed.Primary, std::bit_cast<std::uint64_t>(range.Minimum));
            integer(packed.Secondary, std::bit_cast<std::uint64_t>(range.Maximum));
            return true;
        }
        case VfxValueType::UnsignedIntegerRange:
        {
            const auto range = std::get<VfxUnsignedIntegerRange>(value);
            integer(packed.Primary, range.Minimum);
            integer(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector2Range:
        {
            const auto range = std::get<VfxVector2Range>(value);
            vector2(packed.Primary, range.Minimum);
            vector2(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector3Range:
        {
            const auto range = std::get<VfxVector3Range>(value);
            vector3(packed.Primary, range.Minimum);
            vector3(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::Vector4Range:
        {
            const auto range = std::get<VfxVector4Range>(value);
            vector4(packed.Primary, range.Minimum);
            vector4(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ColorRange:
        {
            const auto range = std::get<VfxColorRange>(value);
            color(packed.Primary, range.Minimum);
            color(packed.Secondary, range.Maximum);
            return true;
        }
        case VfxValueType::ParticleStream:
        case VfxValueType::Matrix:
        case VfxValueType::Curve:
        case VfxValueType::Gradient:
            return false;
        }
        return false;
    }

    bool EvaluateVfxExpressions(const VfxCompiledProgram& program, const std::span<const VfxParameterValue> parameters,
                                const VfxExpressionEvaluationContext& context,
                                const std::span<VfxParameterValue> registers) noexcept
    {
        if (registers.size() < program.ValueRegisterCount)
            return false;
        try
        {
            for (const auto& instruction : program.ValueInstructions)
            {
                if (instruction.Context != context.Context)
                    continue;
                if (instruction.OutputRegister >= registers.size() || instruction.Inputs.size() > 4)
                    return false;
                std::array<const VfxParameterValue*, 4> inputs{};
                for (std::size_t index = 0; index < instruction.Inputs.size(); ++index)
                {
                    inputs[index] = ResolveVfxValueSource(instruction.Inputs[index], parameters, registers);
                    if (!inputs[index])
                        return false;
                }

                std::optional<VfxParameterValue> output;
                if (instruction.Opcode == VfxValueOpcode::Random || instruction.Opcode == VfxValueOpcode::RandomRange)
                {
                    const auto inputValues =
                        std::span<const VfxParameterValue* const>(inputs.data(), instruction.Inputs.size());
                    const auto inclusiveMaximum =
                        instruction.Opcode == VfxValueOpcode::RandomRange && instruction.InclusiveMaximum;
                    switch (instruction.Type)
                    {
                    case VfxValueType::Boolean:
                        if (instruction.Opcode != VfxValueOpcode::Random || !instruction.Inputs.empty())
                            return false;
                        output = RandomUnit(instruction, context, false) > 0.5F;
                        break;
                    case VfxValueType::Integer:
                    {
                        const auto bounds = RandomBounds<std::int64_t>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output =
                            RandomInteger(instruction, context, bounds->Minimum, bounds->Maximum, inclusiveMaximum);
                        break;
                    }
                    case VfxValueType::UnsignedInteger:
                    {
                        const auto bounds = RandomBounds<std::uint64_t>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = RandomUnsignedInteger(instruction, context, bounds->Minimum, bounds->Maximum,
                                                       inclusiveMaximum);
                        break;
                    }
                    case VfxValueType::Scalar:
                    {
                        const auto bounds = RandomBounds<float>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output =
                            RandomFloat(instruction, context, bounds->Minimum, bounds->Maximum, inclusiveMaximum, 0);
                        break;
                    }
                    case VfxValueType::Vector2:
                    {
                        const auto bounds = RandomBounds<Vector2>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector2{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1)};
                        break;
                    }
                    case VfxValueType::Vector3:
                    {
                        const auto bounds = RandomBounds<Vector3>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector3{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1),
                                         RandomFloat(instruction, context, bounds->Minimum.Z, bounds->Maximum.Z,
                                                     inclusiveMaximum, 2)};
                        break;
                    }
                    case VfxValueType::Vector4:
                    {
                        const auto bounds = RandomBounds<Vector4>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Vector4{RandomFloat(instruction, context, bounds->Minimum.X, bounds->Maximum.X,
                                                     inclusiveMaximum, 0),
                                         RandomFloat(instruction, context, bounds->Minimum.Y, bounds->Maximum.Y,
                                                     inclusiveMaximum, 1),
                                         RandomFloat(instruction, context, bounds->Minimum.Z, bounds->Maximum.Z,
                                                     inclusiveMaximum, 2),
                                         RandomFloat(instruction, context, bounds->Minimum.W, bounds->Maximum.W,
                                                     inclusiveMaximum, 3)};
                        break;
                    }
                    case VfxValueType::Color:
                    {
                        const auto bounds = RandomBounds<Color>(instruction, inputValues);
                        if (!bounds)
                            return false;
                        output = Color{RandomFloat(instruction, context, bounds->Minimum.Red, bounds->Maximum.Red,
                                                   inclusiveMaximum, 0),
                                       RandomFloat(instruction, context, bounds->Minimum.Green, bounds->Maximum.Green,
                                                   inclusiveMaximum, 1),
                                       RandomFloat(instruction, context, bounds->Minimum.Blue, bounds->Maximum.Blue,
                                                   inclusiveMaximum, 2),
                                       RandomFloat(instruction, context, bounds->Minimum.Alpha, bounds->Maximum.Alpha,
                                                   inclusiveMaximum, 3)};
                        break;
                    }
                    default:
                        return false;
                    }
                }
                else if (instruction.Opcode == VfxValueOpcode::Time)
                    output = context.EffectTime;
                else if (instruction.Opcode == VfxValueOpcode::DeltaTime)
                    output = context.DeltaTime;
                else if (instruction.Opcode == VfxValueOpcode::Age)
                    output = context.Age;
                else if (instruction.Opcode == VfxValueOpcode::Lifetime)
                    output = context.Lifetime;
                else if (instruction.Opcode == VfxValueOpcode::ParticleId)
                    output = context.ParticleId;
                else if (instruction.Opcode == VfxValueOpcode::SpawnIndex)
                    output = context.SpawnIndex;
                else
                {
                    output = ExecutePure(
                        instruction.Opcode,
                        std::span<const VfxParameterValue* const>(inputs.data(), instruction.Inputs.size()),
                        instruction.Type, instruction.ClampRemap, instruction.Comparison, instruction.OutputIndex);
                }
                if (!output || !VfxValueMatchesType(instruction.Type, *output) || !IsFiniteVfxValue(*output))
                    return false;
                registers[instruction.OutputRegister] = std::move(*output);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace Keire::Internal
