#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <cmath>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateControlNode(const Keire::ShaderGraphNode& node,
                                                                             const Keire::ShaderGraphPin& outputPin)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::Keyword:
            result = {.Type = Keire::ShaderGraphValueType::Scalar};
            break;
        case Keire::ShaderGraphNodeKind::StaticSwitch:
        {
            const float condition = Coerce(NamedInput(node, "Condition"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = Coerce(NamedInput(node, condition != 0.0F ? "True" : "False"), node.ValueType);
            break;
        }
        case Keire::ShaderGraphNodeKind::Custom:
            result = GraphValue(node.Value, node.ValueType);
            break;
        case Keire::ShaderGraphNodeKind::Reroute:
            result = Coerce(NamedInput(node, "Input"), node.ValueType);
            break;
        case Keire::ShaderGraphNodeKind::If:
        {
            const float left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float threshold = Coerce(NamedInput(node, "Threshold"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = Coerce(NamedInput(node, std::abs(left - right) <= threshold ? "Equal"
                                             : left > right                      ? "Greater"
                                                                                 : "Less"),
                            node.ValueType);
            break;
        }
        case Keire::ShaderGraphNodeKind::Compare:
        {
            const float left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float threshold = Coerce(NamedInput(node, "Threshold"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float value = outputPin.Name == "Greater" ? (left > right ? 1.0F : 0.0F)
                                : outputPin.Name == "Less"  ? (left < right ? 1.0F : 0.0F)
                                                            : (std::abs(left - right) <= threshold ? 1.0F : 0.0F);
            result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::BooleanAnd:
        case Keire::ShaderGraphNodeKind::BooleanOr:
        {
            const bool left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X != 0.0F;
            const bool right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X != 0.0F;
            const float value =
                (node.Kind == Keire::ShaderGraphNodeKind::BooleanAnd ? left && right : left || right) ? 1.0F : 0.0F;
            result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::BooleanNot:
        {
            const float value =
                Coerce(NamedInput(node, "Input"), Keire::ShaderGraphValueType::Scalar).Data.X == 0.0F ? 1.0F : 0.0F;
            result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::FunctionCall:
            throw std::invalid_argument("Function Call preview requires an expanded reusable graph.");
        case Keire::ShaderGraphNodeKind::Master:
            throw std::invalid_argument("The Shader Output node cannot be used as an expression.");
        default:
            throw std::logic_error("Preview node dispatched to an incompatible control family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
