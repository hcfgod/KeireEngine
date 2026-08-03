#pragma once

#include "Keire/Assets/LightingAssets.h"
#include "Keire/Scenes/SceneAsset.h"

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace Keire
{
    enum class LightingBakePhase : std::uint8_t
    {
        Fingerprinting,
        CacheLookup,
        PreparingGeometry,
        BakingLightmaps,
        BakingReflectionProbes,
        BakingLightProbes,
        Publishing,
        Complete
    };

    struct LightingBakeProgress
    {
        LightingBakePhase Phase = LightingBakePhase::Fingerprinting;
        std::size_t Completed = 0;
        std::size_t Total = 0;
        std::string Message;
    };

    struct LightingBakeInputDigest
    {
        AssetId Asset;
        std::string Digest;
    };

    struct LightingBakeRequest
    {
        AssetId Scene;
        SceneDefinition Definition;
        std::filesystem::path ProjectRoot = ".";
        std::filesystem::path OutputDirectory = "Assets/Lighting";
        std::filesystem::path CacheDirectory = "Library/LightingCache";
        std::vector<LightingBakeInputDigest> Inputs;
        std::function<Ref<const Asset>(AssetId)> ResolveAsset;
        std::function<void(const LightingBakeProgress&)> Progress;
        std::stop_token Cancellation;
        bool Force = false;
    };

    struct LightingBakeAssetOutput
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path RelativePath;
    };

    struct LightingBakeResult
    {
        AssetId LightingSet;
        std::string InputFingerprint;
        LightingBakeBackend Backend = LightingBakeBackend::CPU;
        bool CacheHit = false;
        std::vector<LightingBakeAssetOutput> Assets;
    };

    class KEIRE_API LightingBaker final
    {
      public:
        [[nodiscard]] static LightingBakeResult Bake(const LightingBakeRequest& request);
        [[nodiscard]] static std::string Fingerprint(const LightingBakeRequest& request);
    };
} // namespace Keire
