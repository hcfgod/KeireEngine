struct VertexInput
{
    float3 Position : TEXCOORD0;
    float3 PreviousPosition : TEXCOORD1;
};
struct VertexOutput
{
    float4 Position : SV_Position;
    float4 CurrentClip : TEXCOORD0;
    float4 PreviousClip : TEXCOORD1;
};
cbuffer MotionData : register(b0, space1)
{
    column_major float4x4 CurrentMvp;
    column_major float4x4 PreviousMvp;
    column_major float4x4 Model;
    column_major float4x4 View;
    column_major float4x4 Projection;
    float4 Parameters;
};
VertexOutput VSMain(const VertexInput input)
{
    VertexOutput output;
    // Match the color pass multiplication order so a prepass cannot reject coplanar color fragments.
    output.CurrentClip = Parameters.x > 0.5F
                             ? mul(Projection, mul(View, mul(Model, float4(input.Position, 1.0F))))
                             : mul(CurrentMvp, float4(input.Position, 1.0F));
    output.PreviousClip = mul(PreviousMvp, float4(input.PreviousPosition, 1.0F));
    output.Position = output.CurrentClip;
    return output;
}
float2 PSMain(const VertexOutput input) : SV_Target0
{
    const float2 currentNdc = input.CurrentClip.xy / max(abs(input.CurrentClip.w), 1.0e-6F);
    const float2 previousNdc = input.PreviousClip.xy / max(abs(input.PreviousClip.w), 1.0e-6F);
    return (currentNdc - previousNdc) * float2(0.5F, -0.5F);
}
