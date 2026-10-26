#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateNode(const Keire::ShaderGraphNode& node,
                                                                      const Keire::ShaderGraphPin& outputPin)
    {
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::Parameter:
        case Keire::ShaderGraphNodeKind::Constant:
        case Keire::ShaderGraphNodeKind::TextureSample:
        case Keire::ShaderGraphNodeKind::TriplanarSample:
        case Keire::ShaderGraphNodeKind::TextureSampleLevel:
            return EvaluateSamplingNode(node, outputPin);
        case Keire::ShaderGraphNodeKind::UV:
        case Keire::ShaderGraphNodeKind::UVTransform:
        case Keire::ShaderGraphNodeKind::NormalMap:
        case Keire::ShaderGraphNodeKind::DetailNormal:
        case Keire::ShaderGraphNodeKind::Parallax:
        case Keire::ShaderGraphNodeKind::Fresnel:
        case Keire::ShaderGraphNodeKind::VertexColor:
        case Keire::ShaderGraphNodeKind::WorldPosition:
        case Keire::ShaderGraphNodeKind::WorldNormal:
        case Keire::ShaderGraphNodeKind::ViewDirection:
        case Keire::ShaderGraphNodeKind::UV1:
        case Keire::ShaderGraphNodeKind::ScreenPosition:
        case Keire::ShaderGraphNodeKind::WorldTangent:
        case Keire::ShaderGraphNodeKind::CameraPosition:
        case Keire::ShaderGraphNodeKind::ObjectPosition:
        case Keire::ShaderGraphNodeKind::Time:
        case Keire::ShaderGraphNodeKind::DeltaTime:
        case Keire::ShaderGraphNodeKind::DepthFade:
        case Keire::ShaderGraphNodeKind::ReflectionVector:
        case Keire::ShaderGraphNodeKind::FacingRatio:
        case Keire::ShaderGraphNodeKind::HeightToNormal:
        case Keire::ShaderGraphNodeKind::FlattenNormal:
        case Keire::ShaderGraphNodeKind::RotateUV:
        case Keire::ShaderGraphNodeKind::Panner:
        case Keire::ShaderGraphNodeKind::PolarCoordinates:
            return EvaluateGeometryNode(node);
        case Keire::ShaderGraphNodeKind::SimpleNoise:
        case Keire::ShaderGraphNodeKind::Desaturate:
        case Keire::ShaderGraphNodeKind::Posterize:
        case Keire::ShaderGraphNodeKind::Luminance:
        case Keire::ShaderGraphNodeKind::HueShift:
        case Keire::ShaderGraphNodeKind::Checkerboard:
        case Keire::ShaderGraphNodeKind::VoronoiNoise:
        case Keire::ShaderGraphNodeKind::SphereMask:
        case Keire::ShaderGraphNodeKind::RadialGradient:
        case Keire::ShaderGraphNodeKind::LinearGradient:
        case Keire::ShaderGraphNodeKind::Contrast:
        case Keire::ShaderGraphNodeKind::Saturation:
        case Keire::ShaderGraphNodeKind::BlendOverlay:
        case Keire::ShaderGraphNodeKind::Blackbody:
        case Keire::ShaderGraphNodeKind::Dither:
        case Keire::ShaderGraphNodeKind::GradientNoise:
        case Keire::ShaderGraphNodeKind::Wave:
            return EvaluateProceduralNode(node, outputPin);
        case Keire::ShaderGraphNodeKind::MakeMaterialAttributes:
        case Keire::ShaderGraphNodeKind::BreakMaterialAttributes:
        case Keire::ShaderGraphNodeKind::BlendMaterialAttributes:
        case Keire::ShaderGraphNodeKind::StandardSurfaceBsdf:
        case Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf:
        case Keire::ShaderGraphNodeKind::MixSlabs:
        case Keire::ShaderGraphNodeKind::AddSlabs:
        case Keire::ShaderGraphNodeKind::CoatSlab:
        case Keire::ShaderGraphNodeKind::ClearCoatBsdf:
        case Keire::ShaderGraphNodeKind::FuzzSlab:
        case Keire::ShaderGraphNodeKind::SheenBsdf:
        case Keire::ShaderGraphNodeKind::SubsurfaceBsdf:
        case Keire::ShaderGraphNodeKind::TransmissionBsdf:
        case Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes:
            return EvaluateSurfaceNode(node, outputPin);
        case Keire::ShaderGraphNodeKind::Keyword:
        case Keire::ShaderGraphNodeKind::StaticSwitch:
        case Keire::ShaderGraphNodeKind::Custom:
        case Keire::ShaderGraphNodeKind::Reroute:
        case Keire::ShaderGraphNodeKind::If:
        case Keire::ShaderGraphNodeKind::Compare:
        case Keire::ShaderGraphNodeKind::BooleanAnd:
        case Keire::ShaderGraphNodeKind::BooleanOr:
        case Keire::ShaderGraphNodeKind::BooleanNot:
        case Keire::ShaderGraphNodeKind::FunctionCall:
        case Keire::ShaderGraphNodeKind::Master:
            return EvaluateControlNode(node, outputPin);
        case Keire::ShaderGraphNodeKind::Add:
        case Keire::ShaderGraphNodeKind::Subtract:
        case Keire::ShaderGraphNodeKind::Multiply:
        case Keire::ShaderGraphNodeKind::Divide:
        case Keire::ShaderGraphNodeKind::Power:
        case Keire::ShaderGraphNodeKind::Minimum:
        case Keire::ShaderGraphNodeKind::Maximum:
        case Keire::ShaderGraphNodeKind::Lerp:
        case Keire::ShaderGraphNodeKind::OneMinus:
        case Keire::ShaderGraphNodeKind::Clamp:
        case Keire::ShaderGraphNodeKind::Absolute:
        case Keire::ShaderGraphNodeKind::Floor:
        case Keire::ShaderGraphNodeKind::Ceiling:
        case Keire::ShaderGraphNodeKind::Fraction:
        case Keire::ShaderGraphNodeKind::Sine:
        case Keire::ShaderGraphNodeKind::Cosine:
        case Keire::ShaderGraphNodeKind::Normalize:
        case Keire::ShaderGraphNodeKind::Length:
        case Keire::ShaderGraphNodeKind::Dot:
        case Keire::ShaderGraphNodeKind::Remap:
        case Keire::ShaderGraphNodeKind::SmoothStep:
        case Keire::ShaderGraphNodeKind::Step:
        case Keire::ShaderGraphNodeKind::Round:
        case Keire::ShaderGraphNodeKind::Truncate:
        case Keire::ShaderGraphNodeKind::Sign:
        case Keire::ShaderGraphNodeKind::Modulo:
        case Keire::ShaderGraphNodeKind::SquareRoot:
        case Keire::ShaderGraphNodeKind::ReciprocalSquareRoot:
        case Keire::ShaderGraphNodeKind::Exponential2:
        case Keire::ShaderGraphNodeKind::Logarithm2:
        case Keire::ShaderGraphNodeKind::Tangent:
        case Keire::ShaderGraphNodeKind::ArcSine:
        case Keire::ShaderGraphNodeKind::ArcCosine:
        case Keire::ShaderGraphNodeKind::ArcTangent2:
        case Keire::ShaderGraphNodeKind::DerivativeX:
        case Keire::ShaderGraphNodeKind::DerivativeY:
        case Keire::ShaderGraphNodeKind::FilterWidth:
        case Keire::ShaderGraphNodeKind::Cross:
        case Keire::ShaderGraphNodeKind::Distance:
        case Keire::ShaderGraphNodeKind::Reflect:
        case Keire::ShaderGraphNodeKind::Refract:
        case Keire::ShaderGraphNodeKind::AppendVector:
        case Keire::ShaderGraphNodeKind::ComponentMask:
        case Keire::ShaderGraphNodeKind::ArcTangent:
        case Keire::ShaderGraphNodeKind::HyperbolicSine:
        case Keire::ShaderGraphNodeKind::HyperbolicCosine:
        case Keire::ShaderGraphNodeKind::HyperbolicTangent:
        case Keire::ShaderGraphNodeKind::DegreesToRadians:
        case Keire::ShaderGraphNodeKind::RadiansToDegrees:
        case Keire::ShaderGraphNodeKind::Negate:
        case Keire::ShaderGraphNodeKind::Exponential:
        case Keire::ShaderGraphNodeKind::Logarithm:
        case Keire::ShaderGraphNodeKind::ScaleAndBias:
            return EvaluateMathNode(node, outputPin);
        }
        return {};
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
