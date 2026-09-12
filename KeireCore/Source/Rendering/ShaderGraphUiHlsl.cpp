#include "Keire/Rendering/ShaderGraph.h"

#include <stdexcept>
#include <string_view>

namespace Keire::Detail
{
    void ValidateUiShaderGraph(const ShaderGraphDefinition& definition)
    {
        if (!definition.Resources.empty())
            throw std::invalid_argument("UI Shader Graph does not support custom texture or storage resources.");
        for (const auto& node : definition.Nodes)
            if (node.Kind == ShaderGraphNodeKind::Custom || node.Kind == ShaderGraphNodeKind::CameraPosition ||
                node.ValueType == ShaderGraphValueType::MaterialAttributes ||
                node.ValueType == ShaderGraphValueType::Bsdf)
                throw std::invalid_argument("Node '" + node.Name +
                                            "' is not supported by retained UI shader lowering.");
    }

    [[nodiscard]] std::string_view ShaderGraphUiVertexInputHlsl() noexcept
    {
        return R"HLSL(
struct VertexInput
{
    float3 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 UV0 : TEXCOORD2;
};

cbuffer ViewportData : register(b0, space1)
{
    float4 Viewport;
};
)HLSL";
    }

    [[nodiscard]] std::string_view ShaderGraphUiVertexMainHlsl() noexcept
    {
        return R"HLSL(
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    const float2 viewportSize = max(Viewport.xy, float2(1.0F, 1.0F));
    output.Position = float4(input.Position.xy / viewportSize * float2(2.0F, -2.0F) +
                            float2(-1.0F, 1.0F), input.Position.z, 1.0F);
    output.Normal = float3(0.0F, 0.0F, 1.0F);
    output.Tangent = float3(1.0F, 0.0F, 0.0F);
    output.Bitangent = float3(0.0F, 1.0F, 0.0F);
    output.ViewDirection = float3(0.0F, 0.0F, 1.0F);
    output.UV0 = input.UV0;
    output.UV1 = input.UV0;
    output.Color = input.Color;
    output.WorldPosition = input.Position;
    output.ObjectPosition = input.Position;
    output.ViewDepth = input.Position.z;
    return output;
}
)HLSL";
    }
} // namespace Keire::Detail
