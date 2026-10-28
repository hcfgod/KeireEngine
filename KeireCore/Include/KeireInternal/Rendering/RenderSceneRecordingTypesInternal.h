#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/Atmosphere.h"
#include "Keire/Rendering/Cloud.h"
#include "Keire/Rendering/EnvironmentLighting.h"
#include "Keire/Rendering/Lighting.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/Rendering/GpuVisibilityCandidateInternal.h"
#include "KeireInternal/Rendering/SpatialLightingInternal.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Keire::RenderBackend
{
    enum class SceneDrawPhase : std::uint8_t
    {
        Opaque,
        Transparent,
        DeferredDepthVelocity,
        DeferredGBufferStandard,
        DeferredGBufferExtended,
        DeferredDecal,
        DeferredForwardOpaqueTail,
        DeferredSky
    };

    struct SceneLightProbeVolume final
    {
        EntityId Entity;
        Matrix4 LocalToWorld;
        Matrix4 WorldToLocal;
        Vector3 BoxExtents;
        Vector3 Spacing;
        std::int32_t Priority = 0;
        float NormalBias = 0.0F;
        float ViewBias = 0.0F;
    };

    struct SceneSpatialContribution final
    {
        AssetId Scene;
        AssetId BakedLighting;
        std::vector<Detail::SpatialReflectionProbe> ReflectionProbes;
        std::vector<SceneLightProbeVolume> LightProbeVolumes;
    };

    struct SceneLighting final
    {
        EntityId Entity;
        Vector4 Direction{0.0F, -1.0F, 0.0F, 0.0F};
        Color ColorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
        ShadowQuality Shadows = ShadowQuality::Disabled;
        ShadowResolutionHint ShadowResolution = ShadowResolutionHint::High;
        float ShadowStrength = 1.0F;
        float ShadowBias = 0.005F;
        AssetId Cookie;
        Vector2 CookieScale{1.0F, 1.0F};
        Vector2 CookieOffset;
        float CookieRotationDegrees = 0.0F;
        bool ContactShadows = false;
        bool Enabled = false;
    };

    struct SceneDrawItem final
    {
        AssetId Mesh;
        std::vector<AssetId> Materials;
        std::map<std::string, MaterialPropertyValue, std::less<>> MaterialProperties;
        std::map<std::size_t, std::map<std::string, MaterialPropertyValue, std::less<>>> MaterialInstanceProperties;
        Matrix4 World;
        Matrix4 PreviousWorld;
        Color Tint;
        EntityId Entity;
        AssetId Skin;
        AssetId SkinSkeleton;
        std::vector<Matrix4> SkinPalette;
        bool CastShadows = true;
        bool ReceiveShadows = true;
        bool AlwaysVisible = false;
        SkinningMethod Skinning = SkinningMethod::LinearBlend;
        SDL_GPUBuffer* SkinnedAssetVertices = nullptr;
        SDL_GPUBuffer* SkinnedBuiltinVertices = nullptr;
        AssetId Scene;
        std::uint32_t ContributionOrder = 0;
        GpuVisibilityClass VisibilityClass = GpuVisibilityClass::StaticMesh;
        std::uint64_t PoseGeneration = 0;
        std::vector<MeshBounds> CurrentPoseSubmeshBounds;
        std::uint64_t BoundsPoseGeneration = 0;
        std::uint64_t BoundsFrameIndex = 0;
        std::vector<Matrix4> PreviousSkinPalette;
        std::uint64_t PreviousPoseGeneration = 0;
        SDL_GPUBuffer* PreviousSkinnedAssetVertices = nullptr;
        SDL_GPUBuffer* PreviousSkinnedBuiltinVertices = nullptr;

        [[nodiscard]] bool HasFreshCurrentPoseBounds(const std::uint64_t frameIndex,
                                                     const std::size_t submeshCount) const noexcept
        {
            return HasFreshCurrentPoseBounds(frameIndex) && CurrentPoseSubmeshBounds.size() == submeshCount;
        }

        [[nodiscard]] bool HasFreshCurrentPoseBounds(const std::uint64_t frameIndex) const noexcept
        {
            return PoseGeneration != 0 && BoundsPoseGeneration == PoseGeneration && BoundsFrameIndex == frameIndex &&
                   !CurrentPoseSubmeshBounds.empty() && SkinnedAssetVertices;
        }
    };

    struct MotionHistoryKey final
    {
        AssetId Scene;
        EntityId Entity;
        std::uint64_t Surface = 0;
        std::uint64_t SurfaceEpoch = 0;

        [[nodiscard]] bool operator==(const MotionHistoryKey&) const noexcept = default;
    };

    struct MotionHistoryKeyHash final
    {
        [[nodiscard]] std::size_t operator()(const MotionHistoryKey& value) const noexcept
        {
            auto result = std::hash<AssetId>{}(value.Scene);
            result ^= std::hash<EntityId>{}(value.Entity) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            result ^= std::hash<std::uint64_t>{}(value.Surface) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            result ^= std::hash<std::uint64_t>{}(value.SurfaceEpoch) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            return result;
        }
    };

    struct MotionHistoryEntry final
    {
        Matrix4 Transform;
        std::uint64_t Frame = 0;
        AssetId EnvironmentSource;
        std::uint64_t EnvironmentRevision = 0;
        bool EnvironmentDirectional = false;
    };

    // Authoring edits invalidate camera history; ordinary evaluated solar motion does not.
    // Keep these bounded settings off per-entity motion records.
    struct CameraMotionHistoryEntry final
    {
        Matrix4 Transform;
        std::uint64_t Frame = 0;
        AssetId EnvironmentSource;
        std::uint64_t EnvironmentRevision = 0;
        bool EnvironmentDirectional = false;
        AtmosphereSettings Atmosphere;
        CloudSettings Clouds;
        BloomSettings Bloom;
        StarFieldSettings Stars;
        std::optional<double> EnvironmentSecondsPerDay;

        [[nodiscard]] bool HasContinuousEnvironment(const CameraMotionHistoryEntry& next) const noexcept
        {
            return EnvironmentSource == next.EnvironmentSource && EnvironmentRevision == next.EnvironmentRevision &&
                   EnvironmentDirectional == next.EnvironmentDirectional && Atmosphere == next.Atmosphere &&
                   Clouds == next.Clouds && Bloom == next.Bloom && Stars == next.Stars &&
                   EnvironmentSecondsPerDay == next.EnvironmentSecondsPerDay;
        }
    };

    [[nodiscard]] inline CameraMotionHistoryEntry
    MakeCameraMotionHistory(const Matrix4& transform, const std::uint64_t frame,
                            const RenderEnvironmentSettings& environment,
                            const std::optional<EnvironmentLightingSnapshot>& snapshot) noexcept
    {
        CameraMotionHistoryEntry result{transform,
                                        frame,
                                        snapshot ? snapshot->Source : AssetId{},
                                        snapshot ? snapshot->Revision : 0U,
                                        snapshot && snapshot->Directional,
                                        environment.Atmosphere,
                                        environment.Clouds,
                                        environment.Bloom,
                                        environment.Stars};
        if (snapshot && snapshot->Time)
        {
            result.EnvironmentSecondsPerDay = snapshot->Time->SecondsPerDay;
        }
        return result;
    }

    struct SkinMotionHistoryEntry final
    {
        std::vector<Matrix4> Palette;
        std::uint64_t PoseGeneration = 0;
        std::uint64_t Frame = 0;
    };
} // namespace Keire::RenderBackend
