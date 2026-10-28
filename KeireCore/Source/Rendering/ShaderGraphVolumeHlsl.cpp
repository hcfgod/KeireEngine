#include "KeireInternal/Rendering/ShaderGraphVolumeHlsl.h"

#include "Keire/CloudVolumeSource.h"

namespace Keire::Detail
{
    std::string ShaderGraphFragmentTailHlsl(const bool screenSpace, const bool premultiplied,
                                            const std::string_view depthOffset)
    {
        std::string source = "#else\n    ShaderGraphFragmentOutput output;\n    output.Color = ";
        source += !screenSpace    ? "graphSurfaceColor"
                  : premultiplied ? "float4(graphColor * alpha, alpha)"
                                  : "float4(graphColor, alpha)";
        source += ";\n#endif\n";
        if (!depthOffset.empty())
        {
            source += "    output.Depth = ";
            source += !screenSpace ? "graphSurfaceDepth"
                                   : "saturate(input.Position.z + (" + std::string(depthOffset) +
                                         ") * max(fwidth(input.Position.z), 1.0e-7F))";
            source += ";\n";
        }
        source += "    return output;\n}\n";
        return source;
    }

    std::string ShaderGraphVolumeEvaluationHlsl(const std::string_view depthOffset, const bool unlit)
    {
        std::string source;
        if (unlit)
        {
            source += "    if (SurfaceParameters.y < 3.5F || SurfaceParameters.y > 4.5F)\n"
                      "        graphColor *= DirectionalDirectionExposure.w;\n";
        }
        source += "    const float graphSurfaceDepth = ";
        source += depthOffset.empty() ? "input.Position.z"
                                      : "saturate(input.Position.z + (" + std::string(depthOffset) +
                                            ") * max(fwidth(input.Position.z), 1.0e-7F))";
        source += ";\n    const float4 graphSurfaceColor = ComposeGraphVolumeSurface(graphColor, alpha, "
                  "SurfaceParameters.y, input.Position.xy, graphSurfaceDepth, SurfaceCloudVolume);\n"
                  "    graphColor = graphSurfaceColor.rgb;\n";
        return source;
    }

    std::string ShaderGraphVolumeHlsl(const bool unlit, const std::uint32_t noiseSlot)
    {
        std::string source = "#define KEIRE_CLOUD_NOISE_TEXTURE t" + std::to_string(noiseSlot) +
                             "\n#define KEIRE_CLOUD_NOISE_SAMPLER s" + std::to_string(noiseSlot) + "\n";
        source += CloudVolumeShaderSource;
        if (unlit)
        {
            source += R"HLSL(
cbuffer VolumeData : register(b2, space3)
{
    CloudVolumeView SurfaceCloudVolume;
};
)HLSL";
        }
        source += R"HLSL(
float4 ComposeGraphVolumeSurface(float3 radiance, float coverage, float alphaMode,
                                float2 pixelPosition, float depth, CloudVolumeView volumeView)
{
    // Scene opaque draws receive disabled media; opaque particles drawn later receive active media.
    if (alphaMode < 1.5F && volumeView.Quality.x == 0U && volumeView.Atmosphere.Controls.x <= 0.5F)
        return float4(radiance, coverage);
    // Multiplicative filtering needs a separate compensation pass for front scattering.
    // Preserve the authored filter until that pass is available.
    if (alphaMode > 3.5F && alphaMode < 4.5F)
        return float4(radiance, coverage);
    const CloudVolumeViewTransport volume = EvaluateCloudVolumeView(
        pixelPosition * volumeView.CameraControls.zw, depth, volumeView, true);
    return float4(
        VolumeMaterialSurfaceSource(radiance.r, coverage, volume.Radiance.r, volume.Transmission.r, alphaMode),
        VolumeMaterialSurfaceSource(radiance.g, coverage, volume.Radiance.g, volume.Transmission.g, alphaMode),
        VolumeMaterialSurfaceSource(radiance.b, coverage, volume.Radiance.b, volume.Transmission.b, alphaMode), coverage);
}
)HLSL";
        return source;
    }
} // namespace Keire::Detail
