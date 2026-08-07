struct VertexOutput
{
    float4 Position : SV_Position;
};

float4 PSMain(VertexOutput input) : SV_Target0
{
    return float4(0.10, 0.48, 0.95, 1.0);
}
