#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Application.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;
    constexpr float ColorTolerance = 0.04F;
    constexpr float MinimumBehaviorDelta = 0.08F;
    constexpr float MinimumShadowDelta = 0.025F;
    constexpr float MinimumShadowDepthDelta = 0.01F;

    enum class CaptureKind : std::uint8_t
    {
        AmbientZero,
        AmbientWhite,
        AmbientRed,
        AmbientGreen,
        AmbientBlue,
        DirectionalDisabled,
        DirectionalEnabled,
        TintRed,
        TintBlue,
        ExposureLow,
        ExposureHigh,
        NormalIdentity,
        NormalTransformed
    };

    constexpr std::array CaptureSequence{
        CaptureKind::AmbientZero,        CaptureKind::AmbientWhite, CaptureKind::AmbientRed,
        CaptureKind::AmbientGreen,       CaptureKind::AmbientBlue,  CaptureKind::DirectionalDisabled,
        CaptureKind::DirectionalEnabled, CaptureKind::TintRed,      CaptureKind::TintBlue,
        CaptureKind::ExposureLow,        CaptureKind::ExposureHigh, CaptureKind::NormalIdentity,
        CaptureKind::NormalTransformed};

    struct PixelStatistics final
    {
        float Red = 0.0F;
        float Green = 0.0F;
        float Blue = 0.0F;
        float Alpha = 0.0F;

        [[nodiscard]] float Luminance() const noexcept { return Red * 0.2126F + Green * 0.7152F + Blue * 0.0722F; }
    };

    [[nodiscard]] PixelStatistics MeasureCenter(const std::vector<std::uint8_t>& pixels)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        constexpr std::uint32_t minimum = SurfaceSize / 4;
        constexpr std::uint32_t maximum = SurfaceSize - minimum;
        PixelStatistics result;
        std::uint32_t count = 0;
        for (std::uint32_t y = minimum; y < maximum; ++y)
        {
            for (std::uint32_t x = minimum; x < maximum; ++x)
            {
                const auto offset = static_cast<std::size_t>((y * SurfaceSize + x) * 4);
                result.Red += static_cast<float>(pixels[offset]) / 255.0F;
                result.Green += static_cast<float>(pixels[offset + 1]) / 255.0F;
                result.Blue += static_cast<float>(pixels[offset + 2]) / 255.0F;
                result.Alpha += static_cast<float>(pixels[offset + 3]) / 255.0F;
                ++count;
            }
        }
        result.Red /= static_cast<float>(count);
        result.Green /= static_cast<float>(count);
        result.Blue /= static_cast<float>(count);
        result.Alpha /= static_cast<float>(count);
        return result;
    }

    [[nodiscard]] PixelStatistics MeasureSkyCorner(const std::vector<std::uint8_t>& pixels)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        PixelStatistics result;
        constexpr std::uint32_t extent = 12;
        for (std::uint32_t y = 0; y < extent; ++y)
        {
            for (std::uint32_t x = 0; x < extent; ++x)
            {
                const auto offset = static_cast<std::size_t>((y * SurfaceSize + x) * 4);
                result.Red += static_cast<float>(pixels[offset]) / 255.0F;
                result.Green += static_cast<float>(pixels[offset + 1]) / 255.0F;
                result.Blue += static_cast<float>(pixels[offset + 2]) / 255.0F;
                result.Alpha += static_cast<float>(pixels[offset + 3]) / 255.0F;
            }
        }
        constexpr float count = static_cast<float>(extent * extent);
        result.Red /= count;
        result.Green /= count;
        result.Blue /= count;
        result.Alpha /= count;
        return result;
    }

    [[nodiscard]] float GreenDominance(const std::vector<std::uint8_t>& pixels, const bool left)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        const auto minimumX = left ? 0U : SurfaceSize / 2U;
        const auto maximumX = left ? SurfaceSize / 2U : SurfaceSize;
        float result = 0.0F;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = minimumX; x < maximumX; ++x)
            {
                const auto offset = static_cast<std::size_t>((y * SurfaceSize + x) * 4);
                const auto red = static_cast<float>(pixels[offset]) / 255.0F;
                const auto green = static_cast<float>(pixels[offset + 1]) / 255.0F;
                const auto blue = static_cast<float>(pixels[offset + 2]) / 255.0F;
                result += std::max(green - std::max(red, blue), 0.0F);
            }
        }
        return result / static_cast<float>(SurfaceSize * SurfaceSize / 2U);
    }

    [[nodiscard]] float MaximumDarkening(const std::vector<std::uint8_t>& unshadowed,
                                         const std::vector<std::uint8_t>& shadowed)
    {
        if (unshadowed.size() != shadowed.size())
            return 0.0F;
        float maximum = 0.0F;
        for (std::size_t offset = 0; offset + 3 < unshadowed.size(); offset += 4)
        {
            const auto luminance = [offset](const std::vector<std::uint8_t>& frame)
            { return (0.2126F * frame[offset] + 0.7152F * frame[offset + 1] + 0.0722F * frame[offset + 2]) / 255.0F; };
            maximum = std::max(maximum, luminance(unshadowed) - luminance(shadowed));
        }
        return maximum;
    }

    [[nodiscard]] float MaximumDifference(const std::vector<float>& left, const std::vector<float>& right)
    {
        if (left.size() != right.size())
            return 0.0F;
        float maximum = 0.0F;
        for (std::size_t index = 0; index < left.size(); ++index)
            maximum = std::max(maximum, std::abs(left[index] - right[index]));
        return maximum;
    }

    struct CaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        std::vector<std::vector<float>> ShadowDepth;
        std::vector<std::uint64_t> MaterialBindingBuilds;
        std::vector<std::uint64_t> SkinningStaticBuilds;
        std::vector<std::uint64_t> SkinningOutputBuilds;
        std::vector<float> SkinningPreparationMilliseconds;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    struct VfxChannelSignal final
    {
        float Weight = 0.0F;
        float WeightedX = 0.0F;

        [[nodiscard]] float CentroidX() const noexcept { return Weight > 0.0F ? WeightedX / Weight : 0.0F; }
    };

    struct VfxGraphCaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    [[nodiscard]] VfxChannelSignal MeasureChannelSignal(const std::vector<std::uint8_t>& pixels,
                                                        const std::size_t channel)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        REQUIRE(channel < 3);
        VfxChannelSignal result;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                const auto offset = static_cast<std::size_t>((y * SurfaceSize + x) * 4);
                const auto firstOther = (channel + 1) % 3;
                const auto secondOther = (channel + 2) % 3;
                const auto value = static_cast<float>(pixels[offset + channel]);
                const auto other =
                    static_cast<float>(std::max(pixels[offset + firstOther], pixels[offset + secondOther]));
                const auto weight = std::max(value - other, 0.0F) / 255.0F;
                result.Weight += weight;
                result.WeightedX += weight * static_cast<float>(x);
            }
        }
        return result;
    }

    [[nodiscard]] constexpr Keire::AssetId RenderVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x4750555658464752ULL, value);
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> RenderedGraphEffect(const bool customBeforeForce)
    {
        const auto base = customBeforeForce ? 100ULL : 200ULL;
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = RenderVfxId(base);
        definition.Name = customBeforeForce ? "GPU graph custom before force" : "GPU graph custom after force";
        definition.Duration = 2.0F;
        definition.Capacity = 4;
        definition.Modules = {
            {RenderVfxId(base + 1), true, Keire::VfxBurstModule{0.0F, 1, 1, 0.1F}},
            {RenderVfxId(base + 2), true, Keire::VfxShapeModule{}},
            {RenderVfxId(base + 3), true, Keire::VfxInitializeModule{5.0F, 5.0F, {}, {}, {}, {}}},
            {RenderVfxId(base + 4), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Constant(0.8F)}},
            {RenderVfxId(base + 5), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {RenderVfxId(base + 6), true, Keire::VfxForceModule{{4.0F, 0.0F, 0.0F}, 0.0F}},
            {RenderVfxId(base + 7), true, Keire::VfxRendererModule{}},
        };
        definition.Blackboard = {
            {RenderVfxId(base + 20), "Tint Override", Keire::VfxValueType::Color, Keire::Color{0.0F, 0.0F, 1.0F, 1.0F},
             true},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);

        auto& system = definition.Systems.front();
        const auto update = std::ranges::find_if(
            system.Nodes, [](const Keire::VfxGraphNode& node)
            { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
        if (update == system.Nodes.end())
            throw std::logic_error("Converted GPU VFX graph is missing its Update Context.");
        const auto force = std::ranges::find(update->Blocks, RenderVfxId(base + 6), &Keire::VfxGraphBlock::Reference);
        if (force == update->Blocks.end())
            throw std::logic_error("Converted GPU VFX graph is missing its Force Block.");

        const auto parameter = std::ranges::find(system.Nodes, RenderVfxId(base + 20), &Keire::VfxGraphNode::Reference);
        if (parameter == system.Nodes.end() || parameter->Pins.empty())
            throw std::logic_error("Converted GPU VFX graph is missing its Blackboard parameter.");
        const auto parameterNode = parameter->Id;
        const auto parameterOutputPin = parameter->Pins.front().Id;

        const auto customBlock = RenderVfxId(base + 30);
        const auto customTintInput = RenderVfxId(base + 32);
        auto custom = Keire::CreateVfxGraphPortableHlslBlock("Velocity = float3(0.0, 0.0, 0.0);\nTint = TintOverride;");
        custom.Id = customBlock;
        custom.Pins.push_back(
            {customTintInput, "Tint Override", Keire::VfxValueType::Color, true, "TintOverride", std::nullopt});
        update->Blocks.insert(customBeforeForce ? force : std::next(force), std::move(custom));

        Keire::VfxGraphConnection tintConnection;
        tintConnection.Id = RenderVfxId(base + 35);
        tintConnection.OutputNode = parameterNode;
        tintConnection.OutputPin = parameterOutputPin;
        tintConnection.InputNode = update->Id;
        tintConnection.InputPin = customTintInput;
        tintConnection.InputBlock = customBlock;
        system.Connections.push_back(tintConnection);

        Keire::ValidateVfxEffect(definition);
        const auto compiled = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        if (!compiled.Valid)
            throw std::logic_error("Rendered schema-4 GPU VFX graph did not compile.");
        const auto persisted = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(definition));
        if (!persisted)
            throw std::logic_error("Rendered schema-4 GPU VFX graph did not survive persistence.");
        return persisted;
    }

    class RenderAssetFixture final
    {
      public:
        [[nodiscard]] static std::vector<std::byte>
        SolidTexture(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue,
                     const Keire::TextureSemantic semantic = Keire::TextureSemantic::Color,
                     const Keire::TextureColorSpace colorSpace = Keire::TextureColorSpace::Srgb,
                     const std::uint8_t alpha = 255)
        {
            Keire::TextureImportSettings textureSettings;
            textureSettings.Semantic = semantic;
            textureSettings.ColorSpace = colorSpace;
            textureSettings.Mips = Keire::TextureMipPolicy::None;
            Keire::TextureMipLevel mip;
            mip.Width = 2;
            mip.Height = 2;
            for (std::size_t pixel = 0; pixel < 4; ++pixel)
            {
                mip.Pixels.push_back(static_cast<std::byte>(red));
                mip.Pixels.push_back(static_cast<std::byte>(green));
                mip.Pixels.push_back(static_cast<std::byte>(blue));
                mip.Pixels.push_back(static_cast<std::byte>(alpha));
            }
            return Keire::Texture2DAsset::Encode(textureSettings, {&mip, 1});
        }

        RenderAssetFixture()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-RenderAssetTests-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto meshImporter = Keire::CreateMeshAssetImporter();
            const auto shaderImporter = Keire::CreateShaderAssetImporter();
            const auto materialImporter = Keire::CreateMaterialAssetImporter();
            Keire::AssetImporterRegistration skinImporter;
            skinImporter.Name = "KeireTests.SkinnedMesh";
            skinImporter.Type = Keire::SkinnedMeshAsset::StaticType();
            skinImporter.Extensions = {".keireskin"};
            skinImporter.Import = [](const std::span<const std::byte> bytes)
            { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
            Keire::AssetImporterRegistration textureImporter;
            textureImporter.Name = "KeireTests.Texture";
            textureImporter.Type = Keire::Texture2DAsset::StaticType();
            textureImporter.Extensions = {".texture"};
            textureImporter.Import = [](const std::span<const std::byte> bytes)
            { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
            Database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
                .ProjectRoot = Root,
                .Importers = std::vector<Keire::AssetImporterRegistration>{
                    meshImporter, shaderImporter, materialImporter, skinImporter, textureImporter}});
            const std::array vertices{Keire::MeshVertex{{-0.9F, -0.8F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}},
                                      Keire::MeshVertex{{0.9F, -0.8F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}},
                                      Keire::MeshVertex{{0.0F, 0.9F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}}};
            const std::array<std::uint32_t, 3> indices{0, 1, 2};
            Mesh =
                Database->CreateAsset("Triangle.keiremesh", meshImporter, Keire::MeshAsset::Encode(vertices, indices));
            Skeleton = Keire::AssetId::Generate();
            std::array<Keire::SkinVertexInfluence8, 3> skinInfluences;
            for (auto& influence : skinInfluences)
            {
                influence.Bones[0] = 0;
                influence.Weights[0] = 1.0F;
                influence.Count = 1;
            }
            Skin = Database->CreateAsset(
                "Triangle.keireskin", skinImporter,
                Keire::SkinnedMeshAsset::Encode(Mesh, Skeleton, skinInfluences, Keire::SkinningMethod::LinearBlend));
            const auto builtInCube = Keire::MeshAsset::Cube();
            CubeMesh = Database->CreateAsset("Cube.keiremesh", meshImporter,
                                             Keire::MeshAsset::Encode(builtInCube->Vertices(), builtInCube->Indices()));

            TexturePath = Root / "Assets/Green.texture";
            Texture = Database->CreateAsset("Green.texture", textureImporter, SolidTexture(0, 255, 0));
            TransparentTexture = Database->CreateAsset(
                "TransparentGreen.texture", textureImporter,
                SolidTexture(0, 255, 0, Keire::TextureSemantic::Color, Keire::TextureColorSpace::Srgb, 0));
            NeutralNormal = Database->CreateAsset(
                "NeutralNormal.texture", textureImporter,
                SolidTexture(128, 128, 255, Keire::TextureSemantic::Normal, Keire::TextureColorSpace::Linear));
            PerturbedNormal = Database->CreateAsset(
                "PerturbedNormal.texture", textureImporter,
                SolidTexture(128, 255, 128, Keire::TextureSemantic::Normal, Keire::TextureColorSpace::Linear));
            NeutralOrm = Database->CreateAsset(
                "NeutralOrm.texture", textureImporter,
                SolidTexture(255, 255, 0, Keire::TextureSemantic::Data, Keire::TextureColorSpace::Linear));
            OccludedOrm = Database->CreateAsset(
                "OccludedOrm.texture", textureImporter,
                SolidTexture(0, 255, 0, Keire::TextureSemantic::Data, Keire::TextureColorSpace::Linear));
            MetallicSmoothOrm = Database->CreateAsset(
                "MetallicSmoothOrm.texture", textureImporter,
                SolidTexture(255, 0, 255, Keire::TextureSemantic::Data, Keire::TextureColorSpace::Linear));
            MetallicMap = Database->CreateAsset(
                "Metallic.texture", textureImporter,
                SolidTexture(255, 255, 255, Keire::TextureSemantic::Data, Keire::TextureColorSpace::Linear));
            RoughnessMap = Database->CreateAsset(
                "Roughness.texture", textureImporter,
                SolidTexture(0, 0, 0, Keire::TextureSemantic::Data, Keire::TextureColorSpace::Linear));
            BlackEmissive = Database->CreateAsset("BlackEmissive.texture", textureImporter, SolidTexture(0, 0, 0));
            RedEmissive = Database->CreateAsset("RedEmissive.texture", textureImporter, SolidTexture(255, 0, 0));

            const auto shaderDirectory = Root / "Assets/Shaders";
            std::filesystem::create_directories(shaderDirectory);
            ShaderSourcePath = shaderDirectory / "DefaultUnlit.hlsl";
            std::filesystem::copy_file("Samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl", ShaderSourcePath);
            const std::string shaderManifest = R"({
  "schemaVersion": 1,
  "source": "Assets/Shaders/DefaultUnlit.hlsl",
  "vertexLayoutVersion": 2,
  "receivesShadows": true,
  "usesForwardPlus": true,
  "usesInstancing": true,
  "stages": {"vertex": "VSMain", "fragment": "PSMain"},
  "includeRoots": ["Assets/Shaders"],
  "renderState": {"topology": "TriangleList", "culling": "None", "depthTest": true, "depthWrite": true, "blend": false},
  "properties": [
    {"name": "Tint", "type": "Color", "default": [1, 1, 1, 1]},
    {"name": "MainTexture", "type": "Texture2D", "semantic": "BaseColor", "default": null},
    {"name": "MetallicFactor", "type": "Float", "default": [0, 0, 0, 0]},
    {"name": "RoughnessFactor", "type": "Float", "default": [1, 0, 0, 0]},
    {"name": "NormalScale", "type": "Float", "default": [1, 0, 0, 0]},
    {"name": "OcclusionStrength", "type": "Float", "default": [1, 0, 0, 0]},
    {"name": "EmissiveFactor", "type": "Color", "default": [0, 0, 0, 1]},
    {"name": "NormalTexture", "type": "Texture2D", "semantic": "Normal", "default": null},
    {"name": "MetallicRoughnessTexture", "type": "Texture2D", "semantic": "MetallicRoughness", "default": null},
    {"name": "OcclusionTexture", "type": "Texture2D", "semantic": "Occlusion", "default": null},
    {"name": "EmissiveTexture", "type": "Texture2D", "semantic": "Emissive", "default": null},
    {"name": "MetallicTexture", "type": "Texture2D", "semantic": "Metallic", "default": null},
    {"name": "RoughnessTexture", "type": "Texture2D", "semantic": "Roughness", "default": null}
  ]
})";
            Shader = Database->CreateAsset("Shader.keireshader", shaderImporter,
                                           std::as_bytes(std::span(shaderManifest.data(), shaderManifest.size())));
            const std::string materialManifest = "{\"schemaVersion\":1,\"shader\":\"" + Shader.ToString() +
                                                 "\",\"properties\":{\"Tint\":[1,1,1,1],\"MainTexture\":\"" +
                                                 Texture.ToString() + "\"}}";
            MaterialPath = Root / "Assets/Material.keirematerial";
            Material =
                Database->CreateAsset("Material.keirematerial", materialImporter,
                                      std::as_bytes(std::span(materialManifest.data(), materialManifest.size())));
            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        }

        ~RenderAssetFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        [[nodiscard]] bool ReplaceTexture(Keire::Application& application, const std::span<const std::byte> payload)
        {
            std::ofstream stream(TexturePath, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            stream.close();
            if (!stream)
                return false;

            return ReloadAsset(application, Texture);
        }

        [[nodiscard]] bool ReplaceMaterialTint(Keire::Application& application, const Keire::Color tint)
        {
            const std::string properties = "\"Tint\":[" + std::to_string(tint.Red) + "," + std::to_string(tint.Green) +
                                           "," + std::to_string(tint.Blue) + "," + std::to_string(tint.Alpha) +
                                           "],\"MainTexture\":\"" + Texture.ToString() + "\"";
            return ReplaceMaterialProperties(application, properties);
        }

        [[nodiscard]] bool ReplaceMaterialProperties(Keire::Application& application, const std::string_view properties)
        {
            const std::string manifest = "{\"schemaVersion\":1,\"shader\":\"" + Shader.ToString() +
                                         "\",\"properties\":{" + std::string(properties) + "}}";
            std::ofstream stream(MaterialPath, std::ios::binary | std::ios::trunc);
            stream << manifest;
            stream.close();
            return stream && ReloadAsset(application, Material);
        }

        [[nodiscard]] bool ReplaceShaderOutputSwizzle(Keire::Application& application)
        {
            std::ifstream input(ShaderSourcePath, std::ios::binary);
            std::string source(std::istreambuf_iterator<char>(input), {});
            constexpr std::string_view original = "float4(color, 1.0F)";
            const auto position = source.find(original);
            if (!input || position == std::string::npos)
                return false;
            source.replace(position, original.size(), "float4(color.brg, 1.0F)");
            std::ofstream output(ShaderSourcePath, std::ios::binary | std::ios::trunc);
            output << source;
            output.close();
            return output && ReloadAsset(application, Shader);
        }

      private:
        [[nodiscard]] bool ReloadAsset(Keire::Application& application, const Keire::AssetId id)
        {

            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
            auto assets = application.Assets();
            if (!assets || !assets->Unmount(Catalog))
                return false;
            assets->Mount({Catalog, 0, true});
            return assets->Reload(id);
        }

      public:
        std::filesystem::path Root;
        std::filesystem::path Catalog;
        std::filesystem::path TexturePath;
        std::filesystem::path MaterialPath;
        std::filesystem::path ShaderSourcePath;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetId Mesh;
        Keire::AssetId Skeleton;
        Keire::AssetId Skin;
        Keire::AssetId CubeMesh;
        Keire::AssetId Material;
        Keire::AssetId Shader;
        Keire::AssetId Texture;
        Keire::AssetId TransparentTexture;
        Keire::AssetId NeutralNormal;
        Keire::AssetId PerturbedNormal;
        Keire::AssetId NeutralOrm;
        Keire::AssetId OccludedOrm;
        Keire::AssetId MetallicSmoothOrm;
        Keire::AssetId MetallicMap;
        Keire::AssetId RoughnessMap;
        Keire::AssetId BlackEmissive;
        Keire::AssetId RedEmissive;
    };

    class RenderCaptureLayer final : public Keire::Layer
    {
      public:
        explicit RenderCaptureLayer(std::shared_ptr<CaptureResults> results)
            : Layer("Rendered output capture"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000001"),
                                                     Keire::SceneAsset::EmptyDefinition("Rendered output tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Rendered cube");
            m_Transform = object.GetComponent<Keire::TransformComponent>();
            m_Renderer = object.AddComponent<Keire::MeshRendererComponent>();

            auto lightEntity = m_Scene->CreateEntity("Directional light");
            m_LightTransform = lightEntity.GetComponent<Keire::TransformComponent>();
            m_Light = lightEntity.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetLightColor({1.0F, 1.0F, 1.0F, 1.0F});
            m_Light->SetIntensity(1.0F);
            m_LightTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Rendered output tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_LightTransform.Reset();
            m_Renderer.Reset();
            m_Transform.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));

            if (m_NextCapture == CaptureSequence.size())
            {
                Owner().RequestExit();
                return;
            }

            Configure(CaptureSequence[m_NextCapture]);
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
            ++m_NextCapture;
            m_Submitted = true;
        }

      private:
        void Configure(const CaptureKind kind)
        {
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.Exposure = 1.0F;
            m_Renderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
            m_Light->SetEnabled(false);
            m_Transform->SetLocalRotation({});
            m_Transform->SetLocalScale({1.0F, 1.0F, 1.0F});

            switch (kind)
            {
            case CaptureKind::AmbientZero:
            case CaptureKind::DirectionalDisabled:
                m_Environment.AmbientIntensity = 0.0F;
                break;
            case CaptureKind::AmbientWhite:
                break;
            case CaptureKind::AmbientRed:
                m_Environment.AmbientColor = {1.0F, 0.0F, 0.0F, 1.0F};
                break;
            case CaptureKind::AmbientGreen:
                m_Environment.AmbientColor = {0.0F, 1.0F, 0.0F, 1.0F};
                break;
            case CaptureKind::AmbientBlue:
                m_Environment.AmbientColor = {0.0F, 0.0F, 1.0F, 1.0F};
                break;
            case CaptureKind::DirectionalEnabled:
            case CaptureKind::NormalIdentity:
                m_Environment.AmbientIntensity = 0.0F;
                m_Light->SetEnabled(true);
                break;
            case CaptureKind::TintRed:
                m_Renderer->SetTint({1.0F, 0.0F, 0.0F, 1.0F});
                break;
            case CaptureKind::TintBlue:
                m_Renderer->SetTint({0.0F, 0.0F, 1.0F, 1.0F});
                break;
            case CaptureKind::ExposureLow:
                m_Environment.Exposure = 0.25F;
                break;
            case CaptureKind::ExposureHigh:
                m_Environment.Exposure = 1.0F;
                break;
            case CaptureKind::NormalTransformed:
                m_Environment.AmbientIntensity = 0.0F;
                m_Light->SetEnabled(true);
                m_Transform->SetLocalEulerAngles({25.0F, 55.0F, 0.0F});
                m_Transform->SetLocalScale({1.0F, 1.5F, 0.65F});
                break;
            }
        }

        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::TransformComponent> m_Transform;
        Keire::Ref<Keire::MeshRendererComponent> m_Renderer;
        Keire::Ref<Keire::TransformComponent> m_LightTransform;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::RenderEnvironmentSettings m_Environment;
        std::size_t m_NextCapture = 0;
        bool m_Submitted = false;
    };

    class VfxGraphGpuCaptureLayer final : public Keire::Layer
    {
      public:
        explicit VfxGraphGpuCaptureLayer(std::shared_ptr<VfxGraphCaptureResults> results)
            : Layer("Schema-v3 GPU VFX graph capture"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000014"),
                                                     Keire::SceneAsset::EmptyDefinition("GPU VFX graph tests"),
                                                     Keire::ComponentRegistry::CreateDefault());

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "GPU VFX graph tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.SkyVisible = false;
            m_Environment.AmbientIntensity = 0.0F;

            StartVariant(false);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
                CaptureStatistics();
            m_World.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!m_Submitted)
            {
                Submit();
                m_Submitted = true;
                return;
            }

            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
            CaptureStatistics();
            switch (m_Phase)
            {
            case Phase::AfterForceSpawn:
                m_World->Update(0.5F);
                m_Phase = Phase::AfterForceSimulated;
                Submit();
                break;
            case Phase::AfterForceSimulated:
                StartVariant(true);
                m_Phase = Phase::BeforeForceSpawn;
                Submit();
                break;
            case Phase::BeforeForceSpawn:
                m_World->Update(0.5F);
                m_Phase = Phase::BeforeForceSimulated;
                Submit();
                break;
            case Phase::BeforeForceSimulated:
                Owner().RequestExit();
                break;
            }
        }

      private:
        enum class Phase : std::uint8_t
        {
            AfterForceSpawn,
            AfterForceSimulated,
            BeforeForceSpawn,
            BeforeForceSimulated
        };

        void StartVariant(const bool customBeforeForce)
        {
            m_World = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1, .MaximumParticles = 4, .Backend = Keire::VfxBackend::Gpu});
            const auto effect = RenderedGraphEffect(customBeforeForce);
            const auto handle = m_World->Activate(
                {effect,
                 1,
                 {},
                 {},
                 0,
                 {{RenderVfxId((customBeforeForce ? 100ULL : 200ULL) + 20), Keire::Color{0.0F, 1.0F, 0.0F, 1.0F}}}});
            if (!handle)
                throw std::logic_error("Could not activate the rendered schema-v3 GPU VFX graph.");
            m_World->Update(0.01F);
        }

        void Submit()
        {
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment, m_World->CaptureRenderSnapshot()});
        }

        void CaptureStatistics() noexcept
        {
            const auto statistics = Owner().Renderer()->Statistics();
            m_Results->Statistics.VfxComputeDispatches =
                std::max(m_Results->Statistics.VfxComputeDispatches, statistics.VfxComputeDispatches);
            m_Results->Statistics.VfxIndirectDraws =
                std::max(m_Results->Statistics.VfxIndirectDraws, statistics.VfxIndirectDraws);
            m_Results->Statistics.VfxGpuWorlds = std::max(m_Results->Statistics.VfxGpuWorlds, statistics.VfxGpuWorlds);
            m_Results->HasStatistics = true;
        }

        std::shared_ptr<VfxGraphCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::VfxWorld> m_World;
        Keire::RenderEnvironmentSettings m_Environment;
        Phase m_Phase = Phase::AfterForceSpawn;
        bool m_Submitted = false;
    };

    class CloseAfterSubmitLayer final : public Keire::Layer
    {
      public:
        CloseAfterSubmitLayer() : Layer("Close scene after submit") {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000010"),
                                                     Keire::SceneAsset::EmptyDefinition("Frame-local scene packet"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Closing cube");
            (void)object.AddComponent<Keire::MeshRendererComponent>();
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Frame-local scene packet";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            Owner().Renderer()->Submit({m_Scene, m_View});
            m_Scene->Close();
            Owner().RequestExit();
        }

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
    };

    class AssetMeshCaptureLayer final : public Keire::Layer
    {
      public:
        AssetMeshCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                              std::shared_ptr<CaptureResults> results)
            : Layer("Asset mesh capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000002"),
                                                     Keire::SceneAsset::EmptyDefinition("Asset mesh tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Asset mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterial(m_Material);
            renderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
            auto sharedObject = m_Scene->CreateEntity("Shared asset mesh");
            const auto sharedRenderer = sharedObject.AddComponent<Keire::MeshRendererComponent>();
            sharedRenderer->SetMesh(m_Mesh);
            sharedRenderer->SetMaterial(m_Material);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Asset mesh tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            bool exitAfterSubmit = false;
            if (m_Submitted)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->MaterialBindingBuilds.push_back(
                    Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer()));
                const auto statistics = MeasureCenter(m_Results->Frames.back());
                const auto bindingCountIsStable =
                    m_Results->MaterialBindingBuilds.size() >= 2 &&
                    m_Results->MaterialBindingBuilds[m_Results->MaterialBindingBuilds.size() - 2] ==
                        m_Results->MaterialBindingBuilds.back();
                if (m_Results->Frames.size() >= 3 && bindingCountIsStable &&
                    statistics.Green > statistics.Red + MinimumBehaviorDelta &&
                    statistics.Green > statistics.Blue + MinimumBehaviorDelta)
                    exitAfterSubmit = true;
                if (m_Results->Frames.size() >= 120)
                    exitAfterSubmit = true;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
            if (exitAfterSubmit)
                Owner().RequestExit();
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Submitted = false;
    };

    class SkinnedMeshCaptureLayer final : public Keire::Layer
    {
      public:
        SkinnedMeshCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material, const Keire::AssetId skeleton,
                                const Keire::AssetId skin, std::shared_ptr<CaptureResults> results)
            : Layer("Skinned mesh capture"), m_Mesh(mesh), m_Material(material), m_Skeleton(skeleton), m_Skin(skin),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000009"),
                                                     Keire::SceneAsset::EmptyDefinition("Skinned mesh tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Skinned asset mesh");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterial(m_Material);
            m_Animator = object.AddComponent<Keire::AnimatorComponent>();
            m_Animator->SetSkeleton(m_Skeleton);
            m_Animator->SetSkinnedMesh(m_Skin);
            SetPaletteTranslation(-0.65F);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Skinned mesh tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->HasStatistics = true;
            }
            if (m_Scene)
                m_Scene->Close();
            m_Animator.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            bool complete = false;
            if (m_Submitted)
            {
                m_Results->SkinningStaticBuilds.push_back(
                    Keire::RenderSystemInternalAccess::SkinningStaticBuildCount(*Owner().Renderer()));
                m_Results->SkinningOutputBuilds.push_back(
                    Keire::RenderSystemInternalAccess::SkinningOutputBuildCount(*Owner().Renderer()));
                m_Results->SkinningPreparationMilliseconds.push_back(
                    Owner().Renderer()->Statistics().SkinningPreparationMilliseconds);
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto left = GreenDominance(pixels, true);
                const auto right = GreenDominance(pixels, false);
                if (m_Results->Frames.empty() && right > left + MinimumBehaviorDelta)
                {
                    m_Results->Frames.push_back(std::move(pixels));
                    SetPaletteTranslation(0.65F);
                }
                else if (m_Results->Frames.size() == 1 && left > right + MinimumBehaviorDelta)
                {
                    m_Results->Frames.push_back(std::move(pixels));
                    m_DeformationCaptured = true;
                }
            }

            if (m_DeformationCaptured && m_Frames >= 8)
                complete = true;
            if (++m_Frames >= 120)
                complete = true;
            if (complete)
            {
                Owner().RequestExit();
                return;
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.SkyVisible = false;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        void SetPaletteTranslation(const float translation)
        {
            const std::array palette{Keire::Math::ComposeTransform({translation, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F})};
            m_Animator->SetRuntimePose("Test", 0.0F, true, palette);
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        Keire::AssetId m_Skeleton;
        Keire::AssetId m_Skin;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::AnimatorComponent> m_Animator;
        std::uint32_t m_Frames = 0;
        bool m_Submitted = false;
        bool m_DeformationCaptured = false;
    };

    class ShadowCaptureLayer final : public Keire::Layer
    {
      public:
        ShadowCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                           std::shared_ptr<CaptureResults> results)
            : Layer("Shadow capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000004"),
                                                     Keire::SceneAsset::EmptyDefinition("Shadow tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto floor = m_Scene->CreateEntity("Shadow receiver");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.75F, 0.0F});
            floor.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 0.15F, 4.0F});
            const auto floorRenderer = floor.AddComponent<Keire::MeshRendererComponent>();
            floorRenderer->SetMesh(m_Mesh);
            floorRenderer->SetMaterial(m_Material);
            floorRenderer->SetCastShadows(false);

            auto caster = m_Scene->CreateEntity("Shadow caster");
            caster.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
            caster.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.65F, 0.65F, 0.65F});
            const auto casterRenderer = caster.AddComponent<Keire::MeshRendererComponent>();
            casterRenderer->SetMesh(m_Mesh);
            casterRenderer->SetMaterial(m_Material);
            casterRenderer->SetReceiveShadows(false);
            casterRenderer->SetCastShadows(false);
            m_Caster = casterRenderer;

            auto sun = m_Scene->CreateEntity("Sun");
            sun.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Light = sun.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetIntensity(4.0F);
            m_Light->SetShadows(Keire::ShadowQuality::Disabled);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Shadow tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.02F, 0.02F, 0.02F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({3.0F, 3.0F, 5.0F}, {0.0F, -0.25F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_Caster.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame == 48)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Light->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 60)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->ShadowDepth.push_back(Keire::RenderSystemInternalAccess::ReadbackDirectionalShadow(
                    *Owner().Renderer(), *m_View->Surface(), 0));
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 72)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                m_Results->ShadowDepth.push_back(Keire::RenderSystemInternalAccess::ReadbackDirectionalShadow(
                    *Owner().Renderer(), *m_View->Surface(), 0));
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.08F, 0.08F, 0.08F, 1.0F};
            environment.AmbientIntensity = 0.3F;
            environment.DirectionalShadowCascadeCount = 2;
            environment.DirectionalShadowResolution = 1024;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        std::uint32_t m_Frame = 0;
    };

    class LocalShadowCaptureLayer final : public Keire::Layer
    {
      public:
        LocalShadowCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                std::shared_ptr<CaptureResults> results)
            : Layer("Local shadow capture"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000005"),
                                                     Keire::SceneAsset::EmptyDefinition("Local shadow tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto floor = m_Scene->CreateEntity("Shadow receiver");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.75F, 0.0F});
            floor.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 0.15F, 4.0F});
            const auto floorRenderer = floor.AddComponent<Keire::MeshRendererComponent>();
            floorRenderer->SetMesh(m_Mesh);
            floorRenderer->SetMaterial(m_Material);
            floorRenderer->SetCastShadows(false);

            auto caster = m_Scene->CreateEntity("Shadow caster");
            caster.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
            caster.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.65F, 0.65F, 0.65F});
            const auto casterRenderer = caster.AddComponent<Keire::MeshRendererComponent>();
            casterRenderer->SetMesh(m_Mesh);
            casterRenderer->SetMaterial(m_Material);
            casterRenderer->SetReceiveShadows(false);
            casterRenderer->SetCastShadows(false);
            m_Caster = casterRenderer;

            auto pointEntity = m_Scene->CreateEntity("Point shadow light");
            pointEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            m_Point = pointEntity.AddComponent<Keire::PointLightComponent>();
            m_Point->SetIntensity(16.0F);
            m_Point->SetRange(10.0F);
            m_Point->SetShadows(Keire::ShadowQuality::Disabled);

            auto spotEntity = m_Scene->CreateEntity("Spot shadow light");
            spotEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            spotEntity.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Spot = spotEntity.AddComponent<Keire::SpotLightComponent>();
            m_Spot->SetIntensity(20.0F);
            m_Spot->SetRange(10.0F);
            m_Spot->SetConeAngles(35.0F, 55.0F);
            m_Spot->SetShadows(Keire::ShadowQuality::Disabled);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Local shadow tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.02F, 0.02F, 0.02F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({3.0F, 3.0F, 5.0F}, {0.0F, -0.25F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Point.Reset();
            m_Spot.Reset();
            m_Caster.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame == 48)
            {
                Capture();
                m_Point->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 60)
            {
                Capture();
                CaptureShadow(11);
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 72)
            {
                Capture();
                CaptureShadow(11);
                m_Point->SetShadows(Keire::ShadowQuality::Disabled);
                m_Caster->SetCastShadows(false);
                m_Spot->SetShadows(Keire::ShadowQuality::Soft);
            }
            else if (m_Frame == 84)
            {
                Capture();
                CaptureShadow(0);
                m_Caster->SetCastShadows(true);
            }
            else if (m_Frame == 96)
            {
                Capture();
                CaptureShadow(0);
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {0.05F, 0.05F, 0.05F, 1.0F};
            environment.AmbientIntensity = 0.2F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        void Capture()
        {
            m_Results->Frames.push_back(
                Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
        }

        void CaptureShadow(const std::uint32_t layer)
        {
            m_Results->ShadowDepth.push_back(
                Keire::RenderSystemInternalAccess::ReadbackLocalShadow(*Owner().Renderer(), *m_View->Surface(), layer));
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::PointLightComponent> m_Point;
        Keire::Ref<Keire::SpotLightComponent> m_Spot;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        std::uint32_t m_Frame = 0;
    };

    struct RendererLifecycleResults final
    {
        std::uint32_t QueueHighWaterMark = 0;
        std::uint64_t InitialGeneration = 0;
        std::uint64_t ResizedGeneration = 0;
        std::uint64_t MinimizedGeneration = 0;
        std::uint64_t RestoredGeneration = 0;
        bool Resized = false;
        bool Minimized = false;
        bool Restored = false;
    };

    class RendererLifecycleLayer final : public Keire::Layer
    {
      public:
        explicit RendererLifecycleLayer(std::shared_ptr<RendererLifecycleResults> results)
            : Layer("Renderer lifecycle"), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Renderer lifecycle";
            surface.Width = 64;
            surface.Height = 64;
            m_Surface = Owner().Renderer()->CreateSurface(surface);
        }

        void OnDetach() noexcept override { m_Surface.Reset(); }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame == 0)
            {
                m_Results->InitialGeneration = m_Surface->Generation();
                m_Results->QueueHighWaterMark =
                    Keire::RenderSystemInternalAccess::SaturateRendererQueue(*Owner().Renderer());
                m_Surface->RequestSize(128, 80);
            }
            else if (m_Frame == 1)
            {
                m_Results->ResizedGeneration = m_Surface->Generation();
                m_Results->Resized = m_Surface->Available() && m_Surface->Width() == 128 && m_Surface->Height() == 80;
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 0, 0);
            }
            else if (m_Frame == 2)
            {
                m_Results->MinimizedGeneration = m_Surface->Generation();
                m_Results->Minimized = !m_Surface->Available() && m_Surface->Width() == 0 && m_Surface->Height() == 0;
                Keire::RenderSystemInternalAccess::RequestSurfaceSize(*m_Surface, 96, 48);
            }
            else
            {
                m_Results->RestoredGeneration = m_Surface->Generation();
                m_Results->Restored = m_Surface->Available() && m_Surface->Width() == 96 && m_Surface->Height() == 48;
                Owner().RequestExit();
                return;
            }
            ++m_Frame;
        }

      private:
        std::shared_ptr<RendererLifecycleResults> m_Results;
        Keire::Ref<Keire::RenderSurface> m_Surface;
        std::uint32_t m_Frame = 0;
    };

    class DeviceLossLayer final : public Keire::Layer
    {
      public:
        DeviceLossLayer() : Layer("Device loss") {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            Keire::RenderSystemInternalAccess::InjectDeviceLoss(*Owner().Renderer());
        }
    };

    struct ReloadCaptureResults final
    {
        std::vector<std::uint8_t> Green;
        std::vector<std::uint8_t> Red;
        std::vector<std::uint8_t> DimRed;
        std::vector<std::uint8_t> ShaderGreen;
        std::vector<std::uint8_t> AfterFailure;
        bool TextureReloadQueued = false;
        bool MaterialReloadQueued = false;
        bool ShaderReloadQueued = false;
        bool InvalidReloadQueued = false;
        std::uint64_t PenultimateFailureBuilds = 0;
        std::uint64_t SettledFailureBuilds = 0;
    };

    class AssetRevisionCaptureLayer final : public Keire::Layer
    {
      public:
        AssetRevisionCaptureLayer(RenderAssetFixture& fixture, std::shared_ptr<ReloadCaptureResults> results)
            : Layer("Asset revision capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000003"),
                                                     Keire::SceneAsset::EmptyDefinition("Texture reload tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Reloaded texture");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Fixture.Mesh);
            renderer->SetMaterial(m_Fixture.Material);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Texture reload tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto statistics = MeasureCenter(pixels);
                if (m_Stage == 0 && statistics.Green > statistics.Red + MinimumBehaviorDelta)
                {
                    m_Results->Green = pixels;
                    const auto red = RenderAssetFixture::SolidTexture(255, 0, 0);
                    m_Results->TextureReloadQueued = m_Fixture.ReplaceTexture(Owner(), red);
                    m_Stage = 1;
                }
                else if (m_Stage == 1 && statistics.Red > statistics.Green + MinimumBehaviorDelta)
                {
                    m_Results->Red = pixels;
                    m_Results->MaterialReloadQueued = m_Fixture.ReplaceMaterialTint(Owner(), {0.25F, 1.0F, 1.0F, 1.0F});
                    m_Stage = 2;
                }
                else if (m_Stage == 2 && statistics.Red > statistics.Green + MinimumBehaviorDelta &&
                         statistics.Red < MeasureCenter(m_Results->Red).Red - MinimumBehaviorDelta)
                {
                    m_Results->DimRed = pixels;
                    m_Results->ShaderReloadQueued = m_Fixture.ReplaceShaderOutputSwizzle(Owner());
                    m_Stage = 3;
                }
                else if (m_Stage == 3 && statistics.Green > statistics.Red + MinimumBehaviorDelta)
                {
                    m_Results->ShaderGreen = pixels;
                    constexpr std::array invalid{std::byte{0x4b}, std::byte{0x45}, std::byte{0x49}};
                    m_Results->InvalidReloadQueued = m_Fixture.ReplaceTexture(Owner(), invalid);
                    m_Stage = 4;
                }
                else if (m_Stage == 4 && ++m_FramesAfterFailure == 8)
                {
                    m_Results->AfterFailure = std::move(pixels);
                    m_Results->PenultimateFailureBuilds = m_PreviousFailureBuilds;
                    m_Results->SettledFailureBuilds =
                        Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer());
                    Owner().RequestExit();
                    return;
                }
                if (m_Stage == 4)
                {
                    m_PreviousFailureBuilds =
                        Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer());
                }
            }

            if (++m_FrameCount > 120)
            {
                Owner().RequestExit();
                return;
            }
            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            m_Submitted = true;
        }

      private:
        RenderAssetFixture& m_Fixture;
        std::shared_ptr<ReloadCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        std::size_t m_FrameCount = 0;
        std::size_t m_FramesAfterFailure = 0;
        std::uint64_t m_PreviousFailureBuilds = 0;
        int m_Stage = 0;
        bool m_Submitted = false;
    };

    struct MaterialSemanticResults final
    {
        std::array<std::vector<std::uint8_t>, 15> Frames;
        bool ReloadsSucceeded = true;
    };

    class MaterialSemanticCaptureLayer final : public Keire::Layer
    {
      public:
        MaterialSemanticCaptureLayer(RenderAssetFixture& fixture, std::shared_ptr<MaterialSemanticResults> results)
            : Layer("Material semantic capture"), m_Fixture(fixture), m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000004"),
                                                     Keire::SceneAsset::EmptyDefinition("Material semantic tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("PBR triangle");
            m_ObjectTransform = object.GetComponent<Keire::TransformComponent>();
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Fixture.Mesh);
            renderer->SetMaterial(m_Fixture.Material);

            auto lightEntity = m_Scene->CreateEntity("Directional light");
            m_LightTransform = lightEntity.GetComponent<Keire::TransformComponent>();
            m_LightTransform->SetLocalEulerAngles({30.0F, 180.0F, 0.0F});
            m_Light = lightEntity.AddComponent<Keire::DirectionalLightComponent>();
            m_Light->SetIntensity(4.0F);

            auto pointEntity = m_Scene->CreateEntity("Point light");
            m_PointTransform = pointEntity.GetComponent<Keire::TransformComponent>();
            m_PointTransform->SetLocalPosition({0.0F, 0.0F, 1.5F});
            m_PointLight = pointEntity.AddComponent<Keire::PointLightComponent>();
            m_PointLight->SetIntensity(8.0F);
            m_PointLight->SetRange(4.0F);
            m_PointLight->SetEnabled(false);

            auto spotEntity = m_Scene->CreateEntity("Spot light");
            m_SpotTransform = spotEntity.GetComponent<Keire::TransformComponent>();
            m_SpotTransform->SetLocalPosition({0.0F, 0.0F, 1.5F});
            m_SpotTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});
            m_SpotLight = spotEntity.AddComponent<Keire::SpotLightComponent>();
            m_SpotLight->SetIntensity(8.0F);
            m_SpotLight->SetRange(4.0F);
            m_SpotLight->SetConeAngles(20.0F, 35.0F);
            m_SpotLight->SetEnabled(false);

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Material semantic tests";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::Four;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 0.05F;
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Light.Reset();
            m_LightTransform.Reset();
            m_PointLight.Reset();
            m_PointTransform.Reset();
            m_SpotLight.Reset();
            m_SpotTransform.Reset();
            m_ObjectTransform.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted && ++m_SettledFrames >= 8)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface());
                const auto statistics = MeasureCenter(pixels);
                if (m_Stage == 0 && statistics.Green <= statistics.Red + MinimumBehaviorDelta)
                {
                    m_SettledFrames = 0;
                    if (++m_StartupWaits >= 15)
                    {
                        Owner().RequestExit();
                        return;
                    }
                    Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                    return;
                }
                m_Results->Frames[m_Stage] = std::move(pixels);
                if (m_Stage + 1 == m_Results->Frames.size())
                {
                    Owner().RequestExit();
                    return;
                }
                ++m_Stage;
                m_Results->ReloadsSucceeded = ConfigureStage(m_Stage) && m_Results->ReloadsSucceeded;
                m_SettledFrames = 0;
            }

            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
            m_Submitted = true;
        }

      private:
        [[nodiscard]] std::string CommonProperties(const Keire::AssetId texture = {}) const
        {
            const auto baseColorTexture = texture ? texture : m_Fixture.Texture;
            return "\"Tint\":[1,1,1,1],\"MainTexture\":\"" + baseColorTexture.ToString() +
                   "\",\"MetallicFactor\":0,\"RoughnessFactor\":1,\"NormalScale\":1,"
                   "\"OcclusionStrength\":1,\"EmissiveFactor\":[1,1,1,1]";
        }

        [[nodiscard]] bool ConfigureStage(const std::size_t stage)
        {
            auto properties = CommonProperties();
            if (stage == 1)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.NeutralNormal.ToString() +
                              "\",\"MetallicRoughnessTexture\":\"" + m_Fixture.NeutralOrm.ToString() +
                              "\",\"OcclusionTexture\":\"" + m_Fixture.NeutralOrm.ToString() +
                              "\",\"EmissiveTexture\":\"" + m_Fixture.BlackEmissive.ToString() + "\"";
            }
            else if (stage == 2)
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
            else if (stage == 3)
            {
                properties += ",\"MetallicFactor\":1,\"MetallicRoughnessTexture\":\"" +
                              m_Fixture.MetallicSmoothOrm.ToString() + "\"";
            }
            else if (stage == 4)
            {
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.35F;
            }
            else if (stage == 5)
            {
                m_Light->SetEnabled(false);
                properties += ",\"OcclusionTexture\":\"" + m_Fixture.OccludedOrm.ToString() + "\"";
            }
            else if (stage == 6)
            {
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.0F;
                properties += ",\"EmissiveTexture\":\"" + m_Fixture.RedEmissive.ToString() + "\"";
            }
            else if (stage == 7)
            {
                m_Light->SetEnabled(true);
                m_Environment.AmbientIntensity = 0.05F;
                properties += ",\"MetallicFactor\":1,\"MetallicTexture\":\"" + m_Fixture.MetallicMap.ToString() +
                              "\",\"RoughnessTexture\":\"" + m_Fixture.RoughnessMap.ToString() + "\"";
            }
            else if (stage == 8)
            {
                m_Light->SetEnabled(true);
                m_Environment.AmbientIntensity = 0.05F;
                properties = CommonProperties(m_Fixture.TransparentTexture);
            }
            else if (stage == 9)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalScale({1.0F, 1.0F, 0.5F});
            }
            else if (stage == 10)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalEulerAngles({70.0F, 0.0F, 0.0F});
            }
            else if (stage == 11)
            {
                properties += ",\"NormalTexture\":\"" + m_Fixture.PerturbedNormal.ToString() + "\"";
                m_ObjectTransform->SetLocalEulerAngles({});
                m_ObjectTransform->SetLocalScale({-1.0F, 1.0F, 1.0F});
            }
            else if (stage == 12)
            {
                m_ObjectTransform->SetLocalScale({1.0F, 1.0F, 1.0F});
                m_Light->SetEnabled(false);
                m_Environment.AmbientIntensity = 0.0F;
                m_PointLight->SetEnabled(true);
            }
            else if (stage == 13)
            {
                m_PointLight->SetEnabled(false);
                m_SpotLight->SetEnabled(true);
                m_SpotTransform->SetLocalEulerAngles({0.0F, 180.0F, 0.0F});
            }
            else if (stage == 14)
                m_SpotTransform->SetLocalEulerAngles({});
            return m_Fixture.ReplaceMaterialProperties(Owner(), properties);
        }

        RenderAssetFixture& m_Fixture;
        std::shared_ptr<MaterialSemanticResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::TransformComponent> m_LightTransform;
        Keire::Ref<Keire::TransformComponent> m_PointTransform;
        Keire::Ref<Keire::TransformComponent> m_SpotTransform;
        Keire::Ref<Keire::TransformComponent> m_ObjectTransform;
        Keire::Ref<Keire::DirectionalLightComponent> m_Light;
        Keire::Ref<Keire::PointLightComponent> m_PointLight;
        Keire::Ref<Keire::SpotLightComponent> m_SpotLight;
        Keire::RenderEnvironmentSettings m_Environment;
        std::size_t m_Stage = 0;
        std::size_t m_SettledFrames = 0;
        std::size_t m_StartupWaits = 0;
        bool m_Submitted = false;
    };

    [[nodiscard]] Keire::ApplicationSpecification RenderTestSpecification()
    {
        const char* backend = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_TEST_BACKEND");
        if (backend && *backend && !SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            throw std::runtime_error("Could not restore the requested GPU backend after SDL shutdown.");

        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire rendered output tests";
        specification.MainWindow.Width = SurfaceSize;
        specification.MainWindow.Height = SurfaceSize;
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
        specification.Render.EnableGpuValidation =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_VALIDATION") != nullptr;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        specification.Input.Mode = Keire::InputMode::Disabled;
        specification.Scenes.Mode = Keire::SceneMode::Disabled;
        specification.ManageLogging = false;
        specification.SuspendWhenMainWindowMinimized = false;
        return specification;
    }
} // namespace

namespace KeireRenderTests
{
    bool ProbeRenderedOutput(std::string& diagnostic) noexcept
    {
        try
        {
            const auto results = std::make_shared<CaptureResults>();
            Keire::Application application(RenderTestSpecification());
            (void)application.PushLayer(std::make_unique<RenderCaptureLayer>(results));
            if (application.Run() != 0 || results->Frames.size() != CaptureSequence.size())
            {
                diagnostic = "capture sequence did not complete";
                return false;
            }
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
        catch (...)
        {
            diagnostic = "unknown render failure";
            return false;
        }
    }
} // namespace KeireRenderTests

TEST_CASE("rendered lighting output preserves observable color contracts")
{
    const auto results = std::make_shared<CaptureResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<RenderCaptureLayer>(results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == CaptureSequence.size());
    const auto defaultSky = MeasureSkyCorner(results->Frames.front());
    CHECK(defaultSky.Luminance() > MinimumBehaviorDelta);
    CHECK(defaultSky.Blue > defaultSky.Red + ColorTolerance);
    std::vector<PixelStatistics> captures;
    captures.reserve(results->Frames.size());
    for (const auto& pixels : results->Frames)
        captures.push_back(MeasureCenter(pixels));

    const auto at = [&captures](const CaptureKind kind) -> const PixelStatistics&
    {
        const auto found = std::ranges::find(CaptureSequence, kind);
        REQUIRE(found != CaptureSequence.end());
        return captures[static_cast<std::size_t>(std::distance(CaptureSequence.begin(), found))];
    };

    CHECK(at(CaptureKind::AmbientWhite).Luminance() > at(CaptureKind::AmbientZero).Luminance() + MinimumBehaviorDelta);
    CHECK(std::abs(at(CaptureKind::AmbientZero).Luminance() - at(CaptureKind::DirectionalDisabled).Luminance()) <=
          ColorTolerance);

    CHECK(at(CaptureKind::AmbientRed).Red > at(CaptureKind::AmbientRed).Green + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientRed).Red > at(CaptureKind::AmbientRed).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientGreen).Green > at(CaptureKind::AmbientGreen).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientGreen).Green > at(CaptureKind::AmbientGreen).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientBlue).Blue > at(CaptureKind::AmbientBlue).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::AmbientBlue).Blue > at(CaptureKind::AmbientBlue).Green + MinimumBehaviorDelta);

    CHECK(at(CaptureKind::DirectionalEnabled).Luminance() >
          at(CaptureKind::DirectionalDisabled).Luminance() + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::TintRed).Red > at(CaptureKind::TintRed).Blue + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::TintBlue).Blue > at(CaptureKind::TintBlue).Red + MinimumBehaviorDelta);
    CHECK(at(CaptureKind::ExposureHigh).Luminance() > at(CaptureKind::ExposureLow).Luminance() + MinimumBehaviorDelta);
    CHECK(std::abs(at(CaptureKind::NormalIdentity).Luminance() - at(CaptureKind::NormalTransformed).Luminance()) >
          ColorTolerance);
}

TEST_CASE("schema-4 Block order Blackboard overrides and Portable HLSL drive rendered GPU VFX particles")
{
    const auto results = std::make_shared<VfxGraphCaptureResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<VfxGraphGpuCaptureLayer>(results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 4);
    const auto afterSpawnRed = MeasureChannelSignal(results->Frames[0], 0);
    const auto afterSpawnGreen = MeasureChannelSignal(results->Frames[0], 1);
    const auto afterSimulatedRed = MeasureChannelSignal(results->Frames[1], 0);
    const auto afterSimulatedGreen = MeasureChannelSignal(results->Frames[1], 1);
    const auto beforeSpawnRed = MeasureChannelSignal(results->Frames[2], 0);
    const auto beforeSpawnGreen = MeasureChannelSignal(results->Frames[2], 1);
    const auto beforeSimulatedRed = MeasureChannelSignal(results->Frames[3], 0);
    const auto beforeSimulatedGreen = MeasureChannelSignal(results->Frames[3], 1);

    REQUIRE(afterSpawnRed.Weight > 10.0F);
    REQUIRE(beforeSpawnRed.Weight > 10.0F);
    CHECK(afterSpawnRed.Weight > afterSpawnGreen.Weight + 10.0F);
    CHECK(beforeSpawnRed.Weight > beforeSpawnGreen.Weight + 10.0F);

    REQUIRE(afterSimulatedGreen.Weight > 10.0F);
    REQUIRE(beforeSimulatedGreen.Weight > 10.0F);
    CHECK(afterSimulatedGreen.Weight > afterSimulatedRed.Weight + 10.0F);
    CHECK(beforeSimulatedGreen.Weight > beforeSimulatedRed.Weight + 10.0F);

    CHECK(std::abs(afterSimulatedGreen.CentroidX() - afterSpawnRed.CentroidX()) < 4.0F);
    CHECK(std::abs(beforeSimulatedGreen.CentroidX() - beforeSpawnRed.CentroidX()) > 20.0F);
    CHECK(std::abs(beforeSimulatedGreen.CentroidX() - afterSimulatedGreen.CentroidX()) > 20.0F);

    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.VfxGpuWorlds > 0);
}

TEST_CASE("submitted scene data remains valid when the scene closes before end frame")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<CloseAfterSubmitLayer>());
    CHECK(application.Run() == 0);
}

TEST_CASE("renderer replaces the deterministic error mesh with an asset-backed indexed mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<AssetMeshCaptureLayer>(assets.Mesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() >= 2);
    REQUIRE(results->Frames.size() <= 120);
    const auto first = MeasureCenter(results->Frames.front());
    const auto last = MeasureCenter(results->Frames.back());
    CHECK(first.Red > first.Green + MinimumBehaviorDelta);
    CHECK(first.Blue > first.Green + MinimumBehaviorDelta);
    CHECK(last.Green > last.Red + MinimumBehaviorDelta);
    CHECK(last.Green > last.Blue + MinimumBehaviorDelta);
    REQUIRE(results->MaterialBindingBuilds.size() == results->Frames.size());
    CHECK(results->MaterialBindingBuilds.back() == 1);
    CHECK(results->MaterialBindingBuilds[results->MaterialBindingBuilds.size() - 2] ==
          results->MaterialBindingBuilds.back());
    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.ExecutedFrameGraphPasses == results->Statistics.PlannedFrameGraphPasses);
    CHECK(results->Statistics.FrameGraphTransitions > 0);
    CHECK(results->Statistics.TransientResourceAllocations > 0);
    CHECK(results->Statistics.RendererQueueHighWaterMark > 0);
    CHECK(results->Statistics.InstanceBatches == 1);
    CHECK(results->Statistics.FrameUploadSubmissions == 0);
    CHECK(results->Statistics.AllowedFramesInFlight == 1);
    CHECK(results->Statistics.DrawPreparationMilliseconds > 0.0F);
    CHECK(results->Statistics.DepthPassMilliseconds > 0.0F);
    CHECK(results->Statistics.CommandRecordingUnattributedMilliseconds >= 0.0F);
    CHECK(results->Statistics.DrawCalls < 25);
    CHECK(results->Statistics.CpuPreparationP95Milliseconds >= 0.0F);
    CHECK(results->Statistics.RendererLatencyMilliseconds >= 0.0F);
}

TEST_CASE("skinned asset vertices follow bounded palette deformation")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    specification.Render.MaximumFramesInFlight = 3;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<SkinnedMeshCaptureLayer>(assets.Mesh, assets.Material,
                                                                              assets.Skeleton, assets.Skin, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 2);
    CHECK(GreenDominance(results->Frames[0], false) > GreenDominance(results->Frames[0], true) + MinimumBehaviorDelta);
    CHECK(GreenDominance(results->Frames[1], true) > GreenDominance(results->Frames[1], false) + MinimumBehaviorDelta);
    REQUIRE(results->SkinningStaticBuilds.size() >= 8);
    const auto firstStaticBuild =
        std::ranges::find_if(results->SkinningStaticBuilds, [](const std::uint64_t count) { return count != 0; });
    REQUIRE(firstStaticBuild != results->SkinningStaticBuilds.end());
    CHECK(std::ranges::all_of(firstStaticBuild, results->SkinningStaticBuilds.end(),
                              [](const std::uint64_t count) { return count == 1; }));
    REQUIRE(results->SkinningOutputBuilds.size() == results->SkinningStaticBuilds.size());
    CHECK(results->SkinningOutputBuilds.back() == 3);
    CHECK(results->SkinningOutputBuilds[results->SkinningOutputBuilds.size() - 2] ==
          results->SkinningOutputBuilds.back());
    REQUIRE(results->HasStatistics);
    CHECK(results->Statistics.AllowedFramesInFlight == 3);
    CHECK(std::ranges::any_of(results->SkinningPreparationMilliseconds,
                              [](const float milliseconds) { return milliseconds > 0.0F; }));
    CHECK(results->Statistics.CommandRecordingUnattributedMilliseconds >= 0.0F);
}

TEST_CASE("directional shadow maps occlude a separate receiving mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<ShadowCaptureLayer>(assets.CubeMesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 3);
    const auto unshadowed = MeasureCenter(results->Frames[0]);
    const auto withoutCaster = MeasureCenter(results->Frames[1]);
    CHECK(std::abs(unshadowed.Red - withoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - withoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - withoutCaster.Blue) <= ColorTolerance);
    REQUIRE(results->ShadowDepth.size() == 2);
    CHECK(MaximumDifference(results->ShadowDepth[0], results->ShadowDepth[1]) >= MinimumShadowDepthDelta);
    CHECK(MaximumDarkening(results->Frames[1], results->Frames[2]) >= MinimumShadowDelta);
}

TEST_CASE("renderer thread handles resize minimize restore and bounded queue saturation")
{
    const auto results = std::make_shared<RendererLifecycleResults>();
    {
        Keire::Application application(RenderTestSpecification());
        (void)application.PushLayer(std::make_unique<RendererLifecycleLayer>(results));
        REQUIRE(application.Run() == 0);
    }

    CHECK(results->QueueHighWaterMark == 2);
    CHECK(results->Resized);
    CHECK(results->Minimized);
    CHECK(results->Restored);
    CHECK(results->ResizedGeneration > results->InitialGeneration);
    CHECK(results->MinimizedGeneration > results->ResizedGeneration);
    CHECK(results->RestoredGeneration > results->MinimizedGeneration);
}

TEST_CASE("injected GPU device loss propagates and renderer shutdown remains safe")
{
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<DeviceLossLayer>());
    CHECK_THROWS_WITH((void)application.Run(), "Injected GPU device loss.");
}

TEST_CASE("point and spot shadow maps occlude a separate receiving mesh")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<CaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<LocalShadowCaptureLayer>(assets.CubeMesh, assets.Material, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->Frames.size() == 5);
    REQUIRE(results->ShadowDepth.size() == 4);
    CHECK(MaximumDifference(results->ShadowDepth[0], results->ShadowDepth[1]) >= MinimumShadowDepthDelta);
    CHECK(MaximumDifference(results->ShadowDepth[2], results->ShadowDepth[3]) >= MinimumShadowDepthDelta);
    const auto unshadowed = MeasureCenter(results->Frames[0]);
    const auto pointWithoutCaster = MeasureCenter(results->Frames[1]);
    const auto spotWithoutCaster = MeasureCenter(results->Frames[3]);
    CHECK(std::abs(unshadowed.Red - pointWithoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - pointWithoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - pointWithoutCaster.Blue) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Red - spotWithoutCaster.Red) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Green - spotWithoutCaster.Green) <= ColorTolerance);
    CHECK(std::abs(unshadowed.Blue - spotWithoutCaster.Blue) <= ColorTolerance);
    CHECK(MaximumDarkening(results->Frames[1], results->Frames[2]) >= MinimumShadowDelta);
    CHECK(MaximumDarkening(results->Frames[3], results->Frames[4]) >= MinimumShadowDelta);
}

TEST_CASE("PBR material semantics produce stable behavioral pixel deltas")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<MaterialSemanticResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<MaterialSemanticCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->ReloadsSucceeded);
    std::array<PixelStatistics, 15> captures;
    for (std::size_t index = 0; index < captures.size(); ++index)
    {
        REQUIRE_FALSE(results->Frames[index].empty());
        captures[index] = MeasureCenter(results->Frames[index]);
    }
    CHECK(std::abs(captures[0].Red - captures[1].Red) <= ColorTolerance);
    CHECK(std::abs(captures[0].Green - captures[1].Green) <= ColorTolerance);
    CHECK(std::abs(captures[0].Blue - captures[1].Blue) <= ColorTolerance);
    CHECK(std::abs(captures[1].Luminance() - captures[2].Luminance()) > MinimumBehaviorDelta);
    CHECK(std::abs(captures[1].Luminance() - captures[3].Luminance()) > MinimumBehaviorDelta);
    CHECK(captures[4].Luminance() > captures[5].Luminance() + MinimumBehaviorDelta);
    CHECK(captures[6].Red > captures[6].Green + MinimumBehaviorDelta);
    CHECK(captures[6].Red > captures[6].Blue + MinimumBehaviorDelta);
    CHECK(std::abs(captures[3].Red - captures[7].Red) <= ColorTolerance);
    CHECK(std::abs(captures[3].Green - captures[7].Green) <= ColorTolerance);
    CHECK(std::abs(captures[3].Blue - captures[7].Blue) <= ColorTolerance);
    CHECK(captures[8].Alpha >= 1.0F - ColorTolerance);
    CHECK(std::abs(captures[2].Red - captures[9].Red) <= ColorTolerance);
    CHECK(std::abs(captures[2].Green - captures[9].Green) <= ColorTolerance);
    CHECK(std::abs(captures[2].Blue - captures[9].Blue) <= ColorTolerance);
    CHECK(std::abs(captures[9].Luminance() - captures[10].Luminance()) > MinimumBehaviorDelta);
    CHECK(std::abs(captures[2].Red - captures[11].Red) <= ColorTolerance);
    CHECK(std::abs(captures[2].Green - captures[11].Green) <= ColorTolerance);
    CHECK(std::abs(captures[2].Blue - captures[11].Blue) <= ColorTolerance);
    CHECK(captures[12].Luminance() > MinimumBehaviorDelta);
    CHECK(captures[13].Luminance() > captures[14].Luminance() + MinimumBehaviorDelta);
}

TEST_CASE("render asset revisions swap atomically and failed reloads preserve last-good output")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<ReloadCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<AssetRevisionCaptureLayer>(assets, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE(results->TextureReloadQueued);
    REQUIRE(results->MaterialReloadQueued);
    REQUIRE(results->ShaderReloadQueued);
    REQUIRE(results->InvalidReloadQueued);
    REQUIRE_FALSE(results->Green.empty());
    REQUIRE_FALSE(results->Red.empty());
    REQUIRE_FALSE(results->DimRed.empty());
    REQUIRE_FALSE(results->ShaderGreen.empty());
    REQUIRE_FALSE(results->AfterFailure.empty());
    const auto green = MeasureCenter(results->Green);
    const auto red = MeasureCenter(results->Red);
    const auto dimRed = MeasureCenter(results->DimRed);
    const auto shaderGreen = MeasureCenter(results->ShaderGreen);
    const auto afterFailure = MeasureCenter(results->AfterFailure);
    CHECK(green.Green > green.Red + MinimumBehaviorDelta);
    CHECK(red.Red > red.Green + MinimumBehaviorDelta);
    CHECK(dimRed.Red < red.Red - MinimumBehaviorDelta);
    CHECK(shaderGreen.Green > shaderGreen.Red + MinimumBehaviorDelta);
    CHECK(std::abs(afterFailure.Red - shaderGreen.Red) <= ColorTolerance);
    CHECK(std::abs(afterFailure.Green - shaderGreen.Green) <= ColorTolerance);
    CHECK(std::abs(afterFailure.Blue - shaderGreen.Blue) <= ColorTolerance);
    CHECK(results->PenultimateFailureBuilds == results->SettledFailureBuilds);
}
