#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    enum class LightingTextureTarget : std::uint8_t
    {
        Texture2DArray,
        CubeArray
    };

    enum class LightingTextureEncoding : std::uint8_t
    {
        Rgba8,
        Rgbe8,
        Rgba16Float
    };

    struct LightingTextureMip
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t Layers = 0;
        std::vector<std::byte> Pixels;
    };

    struct LightingTextureArrayDefinition
    {
        std::uint32_t SchemaVersion = 1;
        LightingTextureTarget Target = LightingTextureTarget::Texture2DArray;
        LightingTextureEncoding Encoding = LightingTextureEncoding::Rgba16Float;
        std::vector<LightingTextureMip> Mips;
    };

    class KEIRE_API LightingTextureArrayAsset final : public Asset
    {
      public:
        explicit LightingTextureArrayAsset(LightingTextureArrayDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454c5441ULL, 0x5252415900000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const LightingTextureArrayDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<LightingTextureArrayAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const LightingTextureArrayDefinition& definition);

      private:
        LightingTextureArrayDefinition m_Definition;
    };

    struct BakedLightProbe
    {
        std::array<Vector3, 9> Irradiance;
        std::array<float, 6> Visibility{1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
        float Validity = 1.0F;
    };

    struct LightProbeVolumeDefinition
    {
        std::uint32_t SchemaVersion = 1;
        Vector3 Origin;
        Vector3 Spacing{1.0F, 1.0F, 1.0F};
        std::uint32_t CountX = 0;
        std::uint32_t CountY = 0;
        std::uint32_t CountZ = 0;
        std::vector<BakedLightProbe> Probes;
    };

    class KEIRE_API LightProbeVolumeAsset final : public Asset
    {
      public:
        explicit LightProbeVolumeAsset(LightProbeVolumeDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454c5056ULL, 0x4153534554000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const LightProbeVolumeDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<LightProbeVolumeAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const LightProbeVolumeDefinition& definition);

      private:
        LightProbeVolumeDefinition m_Definition;
    };

    struct LightmapRendererBinding
    {
        AssetId Renderer;
        std::uint32_t LightmapLayer = 0;
        Vector4 ScaleOffset{1.0F, 1.0F, 0.0F, 0.0F};
        std::uint32_t ShadowMaskLayer = 0;
    };

    struct MixedLightBinding
    {
        AssetId Light;
        std::uint8_t ShadowMaskChannel = 0;
    };

    struct ReflectionProbeBinding
    {
        AssetId Probe;
        std::uint32_t CubeIndex = 0;
    };

    struct LightProbeVolumeBinding
    {
        AssetId Volume;
        AssetId Data;
    };

    struct LightingSetDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId Scene;
        std::string InputFingerprint;
        AssetId Lightmaps;
        AssetId Directionality;
        AssetId ShadowMasks;
        AssetId ShadowMasksSecondary;
        AssetId ReflectionCubemaps;
        std::vector<LightmapRendererBinding> Renderers;
        std::vector<MixedLightBinding> MixedLights;
        std::vector<ReflectionProbeBinding> ReflectionProbes;
        std::vector<LightProbeVolumeBinding> LightProbeVolumes;
    };

    class KEIRE_API LightingSetAsset final : public Asset
    {
      public:
        explicit LightingSetAsset(LightingSetDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454c5345ULL, 0x5441535345540001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const LightingSetDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<LightingSetAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const LightingSetDefinition& definition);

      private:
        LightingSetDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateLightingTextureArrayAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateLightProbeVolumeAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateLightingSetAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateLightingTextureArrayAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateLightProbeVolumeAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateLightingSetAssetDecoder();
} // namespace Keire
