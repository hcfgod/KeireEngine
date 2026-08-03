#include "Keire/Assets/LightingAssets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumAssetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaximumBindings = 1'000'000;
        constexpr std::size_t MaximumProbeCount = 262'144;

        [[nodiscard]] Json EncodeAssetId(const AssetId value) { return value ? Json(value.ToString()) : Json(nullptr); }

        [[nodiscard]] AssetId DecodeAssetId(const Json& value)
        {
            return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
        }

        [[nodiscard]] Json EncodeVector3(const Vector3 value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Vector3 DecodeVector3(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("Lighting asset Vector3 must contain exactly three numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }

        [[nodiscard]] Json EncodeVector4(const Vector4 value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Vector4 DecodeVector4(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("Lighting asset Vector4 must contain exactly four numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
        }

        [[nodiscard]] std::vector<std::byte> ToCbor(const Json& document)
        {
            const auto encoded = Json::to_cbor(document);
            std::vector<std::byte> result(encoded.size());
            std::memcpy(result.data(), encoded.data(), encoded.size());
            return result;
        }

        [[nodiscard]] Json FromCbor(const std::span<const std::byte> bytes, const char* kind)
        {
            if (bytes.empty() || bytes.size() > MaximumAssetBytes)
                throw std::runtime_error(std::string(kind) + " asset is empty or exceeds the 2 GiB safety limit.");
            try
            {
                return Json::from_cbor(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                       reinterpret_cast<const std::uint8_t*>(bytes.data() + bytes.size()), true, true);
            }
            catch (const Json::exception& error)
            {
                throw std::runtime_error(std::string(kind) + " asset CBOR is malformed: " + error.what());
            }
        }

        [[nodiscard]] std::size_t BytesPerPixel(const LightingTextureEncoding encoding)
        {
            switch (encoding)
            {
            case LightingTextureEncoding::Rgba8:
            case LightingTextureEncoding::Rgbe8:
                return 4;
            case LightingTextureEncoding::Rgba16Float:
                return 8;
            }
            throw std::invalid_argument("Lighting texture encoding is invalid.");
        }

        void ValidateTexture(const LightingTextureArrayDefinition& definition, const bool allowEmpty)
        {
            if (definition.SchemaVersion != 1)
                throw std::invalid_argument("Lighting texture array has an unsupported schema.");
            const auto bytesPerPixel = BytesPerPixel(definition.Encoding);
            if (definition.Mips.empty())
            {
                if (allowEmpty)
                    return;
                throw std::invalid_argument("Lighting texture array must contain at least one mip.");
            }
            if (definition.Mips.size() > 16)
                throw std::invalid_argument("Lighting texture array exceeds the 16 mip limit.");
            const auto layerCount = definition.Mips.front().Layers;
            if (layerCount == 0 || layerCount > 4096 ||
                (definition.Target == LightingTextureTarget::CubeArray && layerCount % 6U != 0U))
                throw std::invalid_argument("Lighting texture array layer count is invalid.");
            std::uint32_t expectedWidth = definition.Mips.front().Width;
            std::uint32_t expectedHeight = definition.Mips.front().Height;
            if (expectedWidth == 0 || expectedHeight == 0 || expectedWidth > 16'384 || expectedHeight > 16'384)
                throw std::invalid_argument("Lighting texture array dimensions are invalid.");
            for (const auto& mip : definition.Mips)
            {
                if (mip.Width != expectedWidth || mip.Height != expectedHeight || mip.Layers != layerCount)
                    throw std::invalid_argument("Lighting texture array mip dimensions are inconsistent.");
                const auto pixelCount = static_cast<std::uint64_t>(mip.Width) * mip.Height * mip.Layers;
                if (pixelCount > MaximumAssetBytes / bytesPerPixel || mip.Pixels.size() != pixelCount * bytesPerPixel)
                    throw std::invalid_argument("Lighting texture array mip byte count is invalid.");
                expectedWidth = std::max(1U, expectedWidth / 2U);
                expectedHeight = std::max(1U, expectedHeight / 2U);
            }
        }

        void ValidateProbeVolume(const LightProbeVolumeDefinition& definition, const bool allowEmpty)
        {
            if (definition.SchemaVersion != 1)
                throw std::invalid_argument("Light probe volume has an unsupported schema.");
            const auto expected = static_cast<std::uint64_t>(definition.CountX) * definition.CountY * definition.CountZ;
            if (expected == 0)
            {
                if (allowEmpty && definition.Probes.empty())
                    return;
                throw std::invalid_argument("Light probe volume dimensions must be non-zero.");
            }
            if (expected > MaximumProbeCount || definition.Probes.size() != expected ||
                !Math::IsFinite(definition.Origin) || !Math::IsFinite(definition.Spacing) ||
                definition.Spacing.X <= 0.0F || definition.Spacing.Y <= 0.0F || definition.Spacing.Z <= 0.0F)
                throw std::invalid_argument("Light probe volume grid is invalid.");
            for (const auto& probe : definition.Probes)
            {
                if (!std::isfinite(probe.Validity) || probe.Validity < 0.0F || probe.Validity > 1.0F ||
                    std::ranges::any_of(probe.Irradiance, [](const Vector3 value) { return !Math::IsFinite(value); }) ||
                    std::ranges::any_of(probe.Visibility,
                                        [](const float value) { return !std::isfinite(value) || value < 0.0F; }))
                    throw std::invalid_argument("Light probe volume contains non-finite probe data.");
            }
        }

        void ValidateLightingSet(const LightingSetDefinition& definition)
        {
            if (definition.SchemaVersion != 1)
                throw std::invalid_argument("Lighting set has an unsupported schema.");
            if (definition.InputFingerprint.size() > 128 || definition.Renderers.size() > MaximumBindings ||
                definition.MixedLights.size() > MaximumBindings ||
                definition.ReflectionProbes.size() > MaximumBindings ||
                definition.LightProbeVolumes.size() > MaximumBindings)
                throw std::invalid_argument("Lighting set exceeds a safety limit.");
            if (!definition.InputFingerprint.empty() &&
                !std::ranges::all_of(definition.InputFingerprint, [](const char value)
                                     { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
                throw std::invalid_argument("Lighting set fingerprint must be lowercase hexadecimal.");
            for (const auto& binding : definition.Renderers)
            {
                if (!binding.Renderer || !Math::IsFinite(binding.ScaleOffset))
                    throw std::invalid_argument("Lighting set contains an invalid renderer binding.");
            }
            for (const auto& binding : definition.MixedLights)
            {
                if (!binding.Light || binding.ShadowMaskChannel >= 8U)
                    throw std::invalid_argument("Lighting set contains an invalid mixed-light binding.");
            }
            for (const auto& binding : definition.ReflectionProbes)
            {
                if (!binding.Probe)
                    throw std::invalid_argument("Lighting set contains an invalid reflection-probe binding.");
            }
            for (const auto& binding : definition.LightProbeVolumes)
            {
                if (!binding.Volume || !binding.Data)
                    throw std::invalid_argument("Lighting set contains an invalid light-probe-volume binding.");
            }
        }

        template <typename AssetT, typename DefinitionT>
        [[nodiscard]] AssetImporterRegistration CreateImporter(std::string name, std::string extension)
        {
            AssetImporterRegistration result;
            result.Name = std::move(name);
            result.Version = 1;
            result.Type = AssetT::StaticType();
            result.Extensions = {std::move(extension)};
            result.Import = [](const std::span<const std::byte> bytes)
            {
                const auto decoded = AssetT::Decode(bytes);
                return AssetT::Encode(decoded->Definition());
            };
            return result;
        }
    } // namespace

    LightingTextureArrayAsset::LightingTextureArrayAsset(LightingTextureArrayDefinition definition)
        : m_Definition(std::move(definition))
    {
        ValidateTexture(m_Definition, true);
    }

    std::size_t LightingTextureArrayAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Mips.capacity() * sizeof(LightingTextureMip);
        for (const auto& mip : m_Definition.Mips)
            result += mip.Pixels.capacity();
        return result;
    }

    Ref<LightingTextureArrayAsset> LightingTextureArrayAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = FromCbor(bytes, "Lighting texture array");
        LightingTextureArrayDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        definition.Target = static_cast<LightingTextureTarget>(document.at("target").get<std::uint8_t>());
        definition.Encoding = static_cast<LightingTextureEncoding>(document.at("encoding").get<std::uint8_t>());
        for (const auto& encodedMip : document.at("mips"))
        {
            LightingTextureMip mip;
            mip.Width = encodedMip.at("width").get<std::uint32_t>();
            mip.Height = encodedMip.at("height").get<std::uint32_t>();
            mip.Layers = encodedMip.at("layers").get<std::uint32_t>();
            const auto& binary = encodedMip.at("pixels").get_binary();
            mip.Pixels.resize(binary.size());
            std::memcpy(mip.Pixels.data(), binary.data(), binary.size());
            definition.Mips.push_back(std::move(mip));
        }
        ValidateTexture(definition, false);
        return CreateRef<LightingTextureArrayAsset>(std::move(definition));
    }

    std::vector<std::byte> LightingTextureArrayAsset::Encode(const LightingTextureArrayDefinition& definition)
    {
        ValidateTexture(definition, false);
        Json mips = Json::array();
        for (const auto& mip : definition.Mips)
        {
            std::vector<std::uint8_t> pixels(mip.Pixels.size());
            std::memcpy(pixels.data(), mip.Pixels.data(), mip.Pixels.size());
            mips.push_back({{"width", mip.Width},
                            {"height", mip.Height},
                            {"layers", mip.Layers},
                            {"pixels", Json::binary(std::move(pixels))}});
        }
        return ToCbor({{"schemaVersion", definition.SchemaVersion},
                       {"target", static_cast<std::uint8_t>(definition.Target)},
                       {"encoding", static_cast<std::uint8_t>(definition.Encoding)},
                       {"mips", std::move(mips)}});
    }

    LightProbeVolumeAsset::LightProbeVolumeAsset(LightProbeVolumeDefinition definition)
        : m_Definition(std::move(definition))
    {
        ValidateProbeVolume(m_Definition, true);
    }

    std::size_t LightProbeVolumeAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Definition.Probes.capacity() * sizeof(BakedLightProbe);
    }

    Ref<LightProbeVolumeAsset> LightProbeVolumeAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = FromCbor(bytes, "Light probe volume");
        LightProbeVolumeDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        definition.Origin = DecodeVector3(document.at("origin"));
        definition.Spacing = DecodeVector3(document.at("spacing"));
        definition.CountX = document.at("countX").get<std::uint32_t>();
        definition.CountY = document.at("countY").get<std::uint32_t>();
        definition.CountZ = document.at("countZ").get<std::uint32_t>();
        for (const auto& encodedProbe : document.at("probes"))
        {
            BakedLightProbe probe;
            const auto& coefficients = encodedProbe.at("irradiance");
            if (!coefficients.is_array() || coefficients.size() != probe.Irradiance.size())
                throw std::runtime_error("Light probe volume irradiance table is invalid.");
            for (std::size_t index = 0; index < probe.Irradiance.size(); ++index)
                probe.Irradiance[index] = DecodeVector3(coefficients[index]);
            probe.Visibility = encodedProbe.at("visibility").get<std::array<float, 6>>();
            probe.Validity = encodedProbe.at("validity").get<float>();
            definition.Probes.push_back(probe);
        }
        ValidateProbeVolume(definition, false);
        return CreateRef<LightProbeVolumeAsset>(std::move(definition));
    }

    std::vector<std::byte> LightProbeVolumeAsset::Encode(const LightProbeVolumeDefinition& definition)
    {
        ValidateProbeVolume(definition, false);
        Json probes = Json::array();
        for (const auto& probe : definition.Probes)
        {
            Json coefficients = Json::array();
            for (const auto coefficient : probe.Irradiance)
                coefficients.push_back(EncodeVector3(coefficient));
            probes.push_back({{"irradiance", std::move(coefficients)},
                              {"visibility", probe.Visibility},
                              {"validity", probe.Validity}});
        }
        return ToCbor({{"schemaVersion", definition.SchemaVersion},
                       {"origin", EncodeVector3(definition.Origin)},
                       {"spacing", EncodeVector3(definition.Spacing)},
                       {"countX", definition.CountX},
                       {"countY", definition.CountY},
                       {"countZ", definition.CountZ},
                       {"probes", std::move(probes)}});
    }

    LightingSetAsset::LightingSetAsset(LightingSetDefinition definition) : m_Definition(std::move(definition))
    {
        ValidateLightingSet(m_Definition);
    }

    std::size_t LightingSetAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Definition.InputFingerprint.capacity() +
               m_Definition.Renderers.capacity() * sizeof(LightmapRendererBinding) +
               m_Definition.MixedLights.capacity() * sizeof(MixedLightBinding) +
               m_Definition.ReflectionProbes.capacity() * sizeof(ReflectionProbeBinding) +
               m_Definition.LightProbeVolumes.capacity() * sizeof(LightProbeVolumeBinding);
    }

    Ref<LightingSetAsset> LightingSetAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto document = FromCbor(bytes, "Lighting set");
        LightingSetDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        definition.Scene = DecodeAssetId(document.at("scene"));
        definition.InputFingerprint = document.value("inputFingerprint", std::string{});
        definition.Lightmaps = DecodeAssetId(document.at("lightmaps"));
        definition.Directionality = DecodeAssetId(document.at("directionality"));
        definition.ShadowMasks = DecodeAssetId(document.at("shadowMasks"));
        definition.ShadowMasksSecondary = DecodeAssetId(document.value("shadowMasksSecondary", Json(nullptr)));
        definition.ReflectionCubemaps = DecodeAssetId(document.at("reflectionCubemaps"));
        for (const auto& encoded : document.at("renderers"))
            definition.Renderers.push_back(
                {DecodeAssetId(encoded.at("renderer")), encoded.at("lightmapLayer").get<std::uint32_t>(),
                 DecodeVector4(encoded.at("scaleOffset")), encoded.at("shadowMaskLayer").get<std::uint32_t>()});
        for (const auto& encoded : document.at("mixedLights"))
            definition.MixedLights.push_back(
                {DecodeAssetId(encoded.at("light")), encoded.at("shadowMaskChannel").get<std::uint8_t>()});
        for (const auto& encoded : document.at("reflectionProbes"))
            definition.ReflectionProbes.push_back(
                {DecodeAssetId(encoded.at("probe")), encoded.at("cubeIndex").get<std::uint32_t>()});
        for (const auto& encoded : document.at("lightProbeVolumes"))
            definition.LightProbeVolumes.push_back(
                {DecodeAssetId(encoded.at("volume")), DecodeAssetId(encoded.at("data"))});
        ValidateLightingSet(definition);
        return CreateRef<LightingSetAsset>(std::move(definition));
    }

    std::vector<std::byte> LightingSetAsset::Encode(const LightingSetDefinition& definition)
    {
        ValidateLightingSet(definition);
        Json renderers = Json::array();
        for (const auto& binding : definition.Renderers)
            renderers.push_back({{"renderer", EncodeAssetId(binding.Renderer)},
                                 {"lightmapLayer", binding.LightmapLayer},
                                 {"scaleOffset", EncodeVector4(binding.ScaleOffset)},
                                 {"shadowMaskLayer", binding.ShadowMaskLayer}});
        Json mixedLights = Json::array();
        for (const auto& binding : definition.MixedLights)
            mixedLights.push_back(
                {{"light", EncodeAssetId(binding.Light)}, {"shadowMaskChannel", binding.ShadowMaskChannel}});
        Json reflectionProbes = Json::array();
        for (const auto& binding : definition.ReflectionProbes)
            reflectionProbes.push_back({{"probe", EncodeAssetId(binding.Probe)}, {"cubeIndex", binding.CubeIndex}});
        Json lightProbeVolumes = Json::array();
        for (const auto& binding : definition.LightProbeVolumes)
            lightProbeVolumes.push_back(
                {{"volume", EncodeAssetId(binding.Volume)}, {"data", EncodeAssetId(binding.Data)}});
        return ToCbor({{"schemaVersion", definition.SchemaVersion},
                       {"scene", EncodeAssetId(definition.Scene)},
                       {"inputFingerprint", definition.InputFingerprint},
                       {"lightmaps", EncodeAssetId(definition.Lightmaps)},
                       {"directionality", EncodeAssetId(definition.Directionality)},
                       {"shadowMasks", EncodeAssetId(definition.ShadowMasks)},
                       {"shadowMasksSecondary", EncodeAssetId(definition.ShadowMasksSecondary)},
                       {"reflectionCubemaps", EncodeAssetId(definition.ReflectionCubemaps)},
                       {"renderers", std::move(renderers)},
                       {"mixedLights", std::move(mixedLights)},
                       {"reflectionProbes", std::move(reflectionProbes)},
                       {"lightProbeVolumes", std::move(lightProbeVolumes)}});
    }

    AssetImporterRegistration CreateLightingTextureArrayAssetImporter()
    {
        return CreateImporter<LightingTextureArrayAsset, LightingTextureArrayDefinition>("Keire.LightingTextureArray",
                                                                                         ".keirelightingtexture");
    }

    AssetImporterRegistration CreateLightProbeVolumeAssetImporter()
    {
        return CreateImporter<LightProbeVolumeAsset, LightProbeVolumeDefinition>("Keire.LightProbeVolume",
                                                                                 ".keireprobevolume");
    }

    AssetImporterRegistration CreateLightingSetAssetImporter()
    {
        auto result = CreateImporter<LightingSetAsset, LightingSetDefinition>("Keire.LightingSet", ".keirelighting");
        result.Import = {};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto decoded = LightingSetAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = LightingSetAsset::Encode(decoded->Definition());
            const auto& definition = decoded->Definition();
            const std::array direct{definition.Lightmaps, definition.Directionality, definition.ShadowMasks,
                                    definition.ShadowMasksSecondary, definition.ReflectionCubemaps};
            for (const auto dependency : direct)
            {
                if (dependency)
                    output.AssetDependencies.push_back(dependency);
            }
            for (const auto& volume : definition.LightProbeVolumes)
                output.AssetDependencies.push_back(volume.Data);
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(std::ranges::unique(output.AssetDependencies).begin(),
                                           output.AssetDependencies.end());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateLightingTextureArrayAssetDecoder()
    {
        return {LightingTextureArrayAsset::StaticType(), CreateRef<LightingTextureArrayAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return LightingTextureArrayAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateLightProbeVolumeAssetDecoder()
    {
        return {LightProbeVolumeAsset::StaticType(), CreateRef<LightProbeVolumeAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return LightProbeVolumeAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateLightingSetAssetDecoder()
    {
        return {LightingSetAsset::StaticType(), CreateRef<LightingSetAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return LightingSetAsset::Decode(bytes); }};
    }
} // namespace Keire
