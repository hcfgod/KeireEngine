#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateMathNode(const Keire::ShaderGraphNode& node,
                                                                          const Keire::ShaderGraphPin& outputPin)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::Add:
            result = Binary(node, std::plus<float>{});
            break;
        case Keire::ShaderGraphNodeKind::Subtract:
            result = Binary(node, std::minus<float>{});
            break;
        case Keire::ShaderGraphNodeKind::Multiply:
            result = Binary(node, std::multiplies<float>{});
            break;
        case Keire::ShaderGraphNodeKind::Divide:
            result = Binary(node, [](const float left, const float right) { return SafeDivide(left, right); });
            break;
        case Keire::ShaderGraphNodeKind::Power:
        {
            const auto base = Coerce(NamedInput(node, "Base"), node.ValueType);
            const auto exponent = Coerce(NamedInput(node, "Exponent"), node.ValueType);
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                SetComponent(result.Data, component,
                             std::pow(std::max(std::abs(Component(base.Data, component)), 1.0e-6F),
                                      Component(exponent.Data, component)));
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Minimum:
            result = Binary(node, [](const float left, const float right) { return std::min(left, right); });
            break;
        case Keire::ShaderGraphNodeKind::Maximum:
            result = Binary(node, [](const float left, const float right) { return std::max(left, right); });
            break;
        case Keire::ShaderGraphNodeKind::Lerp:
        {
            const auto left = Coerce(NamedInput(node, "A"), node.ValueType);
            const auto right = Coerce(NamedInput(node, "B"), node.ValueType);
            const float factor = Coerce(NamedInput(node, "T"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                SetComponent(result.Data, component,
                             std::lerp(Component(left.Data, component), Component(right.Data, component), factor));
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::OneMinus:
            result = Unary(node, [](const float value) { return 1.0F - value; });
            break;
        case Keire::ShaderGraphNodeKind::Clamp:
            result = Unary(node, [](const float value) { return std::clamp(value, 0.0F, 1.0F); });
            break;
        case Keire::ShaderGraphNodeKind::Absolute:
            result = Unary(node, [](const float value) { return std::abs(value); });
            break;
        case Keire::ShaderGraphNodeKind::Floor:
            result = Unary(node, [](const float value) { return std::floor(value); });
            break;
        case Keire::ShaderGraphNodeKind::Ceiling:
            result = Unary(node, [](const float value) { return std::ceil(value); });
            break;
        case Keire::ShaderGraphNodeKind::Fraction:
            result = Unary(node, [](const float value) { return Fraction(value); });
            break;
        case Keire::ShaderGraphNodeKind::Sine:
            result = Unary(node, [](const float value) { return std::sin(value); });
            break;
        case Keire::ShaderGraphNodeKind::Cosine:
            result = Unary(node, [](const float value) { return std::cos(value); });
            break;
        case Keire::ShaderGraphNodeKind::Normalize:
        {
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            float lengthSquared = 0.0F;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                lengthSquared += Component(value.Data, component) * Component(value.Data, component);
            }
            result.Type = node.ValueType;
            const float inverseLength = lengthSquared > 1.0e-12F ? 1.0F / std::sqrt(lengthSquared) : 0.0F;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                SetComponent(result.Data, component, Component(value.Data, component) * inverseLength);
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Length:
        {
            const auto value = NamedInput(node, "Value");
            float lengthSquared = 0.0F;
            for (std::size_t component = 0; component < ComponentCount(value.Type); ++component)
            {
                lengthSquared += Component(value.Data, component) * Component(value.Data, component);
            }
            result = {.Data = {std::sqrt(lengthSquared), 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Dot:
        {
            const auto left = NamedInput(node, "A");
            const auto right = Coerce(NamedInput(node, "B"), left.Type);
            float dot = 0.0F;
            for (std::size_t component = 0; component < ComponentCount(left.Type); ++component)
            {
                dot += Component(left.Data, component) * Component(right.Data, component);
            }
            result = {.Data = {dot, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Remap:
        {
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            const auto inputMinimum = Coerce(NamedInput(node, "In Min"), node.ValueType);
            const auto inputMaximum = Coerce(NamedInput(node, "In Max"), node.ValueType);
            const auto outputMinimum = Coerce(NamedInput(node, "Out Min"), node.ValueType);
            const auto outputMaximum = Coerce(NamedInput(node, "Out Max"), node.ValueType);
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                const float factor =
                    SafeDivide(Component(value.Data, component) - Component(inputMinimum.Data, component),
                               Component(inputMaximum.Data, component) - Component(inputMinimum.Data, component));
                SetComponent(result.Data, component,
                             std::lerp(Component(outputMinimum.Data, component),
                                       Component(outputMaximum.Data, component), factor));
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::SmoothStep:
        {
            const auto edgeMinimum = Coerce(NamedInput(node, "Edge Min"), node.ValueType);
            const auto edgeMaximum = Coerce(NamedInput(node, "Edge Max"), node.ValueType);
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                const float factor = std::clamp(
                    SafeDivide(Component(value.Data, component) - Component(edgeMinimum.Data, component),
                               Component(edgeMaximum.Data, component) - Component(edgeMinimum.Data, component)),
                    0.0F, 1.0F);
                SetComponent(result.Data, component, factor * factor * (3.0F - 2.0F * factor));
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Step:
        {
            const auto edge = Coerce(NamedInput(node, "Edge"), node.ValueType);
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            result.Type = node.ValueType;
            for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
            {
                SetComponent(result.Data, component,
                             Component(value.Data, component) >= Component(edge.Data, component) ? 1.0F : 0.0F);
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::Round:
            result = Unary(node, [](const float value) { return std::round(value); });
            break;
        case Keire::ShaderGraphNodeKind::Truncate:
            result = Unary(node, [](const float value) { return std::trunc(value); });
            break;
        case Keire::ShaderGraphNodeKind::Sign:
            result = Unary(node, [](const float value) { return value < 0.0F ? -1.0F : value > 0.0F ? 1.0F : 0.0F; });
            break;
        case Keire::ShaderGraphNodeKind::Modulo:
            result = Binary(node, [](const float left, const float right)
                            { return std::fmod(left, std::abs(right) > 1.0e-6F ? right : 1.0e-6F); });
            break;
        case Keire::ShaderGraphNodeKind::SquareRoot:
            result = Unary(node, [](const float value) { return std::sqrt(std::max(value, 0.0F)); });
            break;
        case Keire::ShaderGraphNodeKind::ReciprocalSquareRoot:
            result = Unary(node, [](const float value) { return 1.0F / std::sqrt(std::max(value, 1.0e-8F)); });
            break;
        case Keire::ShaderGraphNodeKind::Exponential2:
            result = Unary(node, [](const float value) { return std::exp2(value); });
            break;
        case Keire::ShaderGraphNodeKind::Logarithm2:
            result = Unary(node, [](const float value) { return std::log2(std::max(std::abs(value), 1.0e-8F)); });
            break;
        case Keire::ShaderGraphNodeKind::Tangent:
            result = Unary(node, [](const float value) { return std::tan(value); });
            break;
        case Keire::ShaderGraphNodeKind::ArcSine:
            result = Unary(node, [](const float value) { return std::asin(std::clamp(value, -1.0F, 1.0F)); });
            break;
        case Keire::ShaderGraphNodeKind::ArcCosine:
            result = Unary(node, [](const float value) { return std::acos(std::clamp(value, -1.0F, 1.0F)); });
            break;
        case Keire::ShaderGraphNodeKind::ArcTangent2:
            result = Binary(node, [](const float left, const float right) { return std::atan2(left, right); });
            break;
        case Keire::ShaderGraphNodeKind::DerivativeX:
        case Keire::ShaderGraphNodeKind::DerivativeY:
        case Keire::ShaderGraphNodeKind::FilterWidth:
            result = {.Type = node.ValueType};
            break;
        case Keire::ShaderGraphNodeKind::Cross:
        {
            const auto left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto value = Cross({left.X, left.Y, left.Z}, {right.X, right.Y, right.Z});
            result = {.Data = {value.X, value.Y, value.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::Distance:
        {
            const auto left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
            const float x = left.X - right.X;
            const float y = left.Y - right.Y;
            const float z = left.Z - right.Z;
            result = {.Data = {std::sqrt(x * x + y * y + z * z), 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Reflect:
        {
            const auto incident = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normalValue = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z});
            const float projection = 2.0F * Dot({incident.X, incident.Y, incident.Z}, normal);
            result = {.Data = {incident.X - projection * normal.X, incident.Y - projection * normal.Y,
                               incident.Z - projection * normal.Z, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::Refract:
        {
            const auto incidentValue = Coerce(NamedInput(node, "Incident"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto incident = Normalize({incidentValue.X, incidentValue.Y, incidentValue.Z});
            const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z});
            const float ior = std::max(
                std::abs(Coerce(NamedInput(node, "IOR"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
            const float eta = 1.0F / ior;
            const float noI = Dot(normal, incident);
            const float k = 1.0F - eta * eta * (1.0F - noI * noI);
            const auto refracted = k < 0.0F ? Keire::Vector3{}
                                            : Keire::Vector3{eta * incident.X - (eta * noI + std::sqrt(k)) * normal.X,
                                                             eta * incident.Y - (eta * noI + std::sqrt(k)) * normal.Y,
                                                             eta * incident.Z - (eta * noI + std::sqrt(k)) * normal.Z};
            result = {.Data = {refracted.X, refracted.Y, refracted.Z, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::AppendVector:
        {
            const auto xyz = Coerce(NamedInput(node, "XYZ"), Keire::ShaderGraphValueType::Vector3).Data;
            const float w = Coerce(NamedInput(node, "W"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Data = {xyz.X, xyz.Y, xyz.Z, w}, .Type = Keire::ShaderGraphValueType::Vector4};
            break;
        }
        case Keire::ShaderGraphNodeKind::ComponentMask:
        {
            const auto value = Coerce(NamedInput(node, "Value"), Keire::ShaderGraphValueType::Vector4).Data;
            if (outputPin.Name == "R" || outputPin.Name == "G" || outputPin.Name == "B" || outputPin.Name == "A")
            {
                const float component = outputPin.Name == "R"   ? value.X
                                        : outputPin.Name == "G" ? value.Y
                                        : outputPin.Name == "B" ? value.Z
                                                                : value.W;
                result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            }
            else
            {
                result = {.Data = value, .Type = outputPin.Type};
            }
            break;
        }
        case Keire::ShaderGraphNodeKind::ArcTangent:
            result = Unary(node, [](const float value) { return std::atan(value); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::HyperbolicSine:
            result = Unary(node, [](const float value) { return std::sinh(value); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::HyperbolicCosine:
            result = Unary(node, [](const float value) { return std::cosh(value); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::HyperbolicTangent:
            result = Unary(node, [](const float value) { return std::tanh(value); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::DegreesToRadians:
            result = Unary(node, [](const float value) { return value * 0.017453292519943295F; }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::RadiansToDegrees:
            result = Unary(node, [](const float value) { return value * 57.29577951308232F; }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::Negate:
            result = Unary(node, [](const float value) { return -value; }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::Exponential:
            result = Unary(node, [](const float value) { return std::exp(value); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::Logarithm:
            result = Unary(node, [](const float value) { return std::log(std::max(value, 1.0e-8F)); }, "Input");
            break;
        case Keire::ShaderGraphNodeKind::ScaleAndBias:
        {
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float bias = Coerce(NamedInput(node, "Bias"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = Unary(node, [scale, bias](const float value) { return value * scale + bias; }, "Input");
            break;
        }
        default:
            throw std::logic_error("Preview node dispatched to an incompatible math family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
