#pragma once

#include "Keire/Application.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Vfx/VfxVolumeAsset.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace KeireRenderTests::Detail
{
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

        explicit RenderAssetFixture(const bool includeShaderGraph = false,
                                    const bool includeProceduralVertexOffset = false,
                                    const bool parameterDrivenVertexOffset = false,
                                    const bool includeTransparentShaderGraph = false,
                                    const bool includeOcclusionStressMesh = false)
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-RenderAssetTests-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto meshImporter = Keire::CreateMeshAssetImporter();
            const auto shaderImporter = Keire::CreateShaderAssetImporter();
            const auto materialImporter = Keire::CreateMaterialAssetImporter();
            const auto materialGraphImporter = Keire::CreateShaderGraphAssetImporter();
            const auto volumeImporter = Keire::CreateVfxVolumeAssetImporter();
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
                    meshImporter, shaderImporter, materialImporter, materialGraphImporter, volumeImporter,
                    Keire::CreateLightingSetAssetImporter(), skinImporter, textureImporter}});
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
            if (includeOcclusionStressMesh)
            {
                std::vector<std::uint32_t> stressIndices;
                stressIndices.reserve(builtInCube->Indices().size() * 70U);
                for (std::size_t repeat = 0; repeat < 70U; ++repeat)
                {
                    stressIndices.insert(stressIndices.end(), builtInCube->Indices().begin(),
                                         builtInCube->Indices().end());
                }
                OcclusionStressMesh =
                    Database->CreateAsset("OcclusionStress.keiremesh", meshImporter,
                                          Keire::MeshAsset::Encode(builtInCube->Vertices(), stressIndices));
            }
            Keire::VfxVolumeDefinition volumeDefinition;
            volumeDefinition.Cells = {{{-0.9F, -0.25F, -0.05F}, {-0.55F, 0.25F, 0.05F}, 1.0F},
                                      {{0.45F, -0.15F, -0.05F}, {0.65F, 0.15F, 0.05F}, 0.2F}};
            Volume = Database->CreateAsset("Spawn.keirevfxvolume", volumeImporter,
                                           Keire::VfxVolumeAsset::Encode(volumeDefinition));

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
            const auto legacyShaderSourcePath = shaderDirectory / "LegacyUnlit.hlsl";
            std::filesystem::copy_file(ShaderSourcePath, legacyShaderSourcePath);
            {
                std::ifstream input(legacyShaderSourcePath, std::ios::binary);
                std::string source(std::istreambuf_iterator<char>(input), {});
                const auto addressingBegin = source.find("cbuffer InstanceAddressingData");
                const auto addressingEnd =
                    addressingBegin == std::string::npos ? std::string::npos : source.find("};", addressingBegin);
                constexpr std::string_view currentInstanceLookup = "Instances[InstanceParameters.x + instanceId]";
                const auto instanceLookup = source.find(currentInstanceLookup);
                if (!input || addressingBegin == std::string::npos || addressingEnd == std::string::npos ||
                    instanceLookup == std::string::npos)
                {
                    throw std::runtime_error("The render-test shader fixture could not derive its legacy ABI variant.");
                }
                source.erase(addressingBegin, addressingEnd + 2U - addressingBegin);
                const auto adjustedInstanceLookup = source.find(currentInstanceLookup);
                source.replace(adjustedInstanceLookup, currentInstanceLookup.size(), "Instances[instanceId]");
                std::ofstream output(legacyShaderSourcePath, std::ios::binary | std::ios::trunc);
                output << source;
                output.close();
                if (!output)
                    throw std::runtime_error("The render-test legacy shader fixture could not be written.");
            }
            const std::string shaderManifest = R"({
  "schemaVersion": 2,
  "source": "Assets/Shaders/DefaultUnlit.hlsl",
  "vertexLayoutVersion": 3,
  "receivesShadows": true,
  "usesForwardPlus": true,
  "usesInstancing": true,
  "instanceAddressingAbiVersion": 2,
  "usesImageBasedLighting": true,
  "spatialLightingAbiVersion": 3,
  "occlusionSupport": 3,
  "maximumWorldPositionDisplacementRadius": 0.0,
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
            auto legacyShaderManifest = shaderManifest;
            const auto schemaVersion = legacyShaderManifest.find("\"schemaVersion\": 2");
            const auto sourcePath = legacyShaderManifest.find("Assets/Shaders/DefaultUnlit.hlsl");
            const auto instanceAbi = legacyShaderManifest.find("  \"instanceAddressingAbiVersion\": 2,\n");
            if (schemaVersion == std::string::npos || sourcePath == std::string::npos ||
                instanceAbi == std::string::npos)
            {
                throw std::runtime_error("The render-test legacy shader manifest could not be derived.");
            }
            legacyShaderManifest.replace(schemaVersion, std::string_view("\"schemaVersion\": 2").size(),
                                         "\"schemaVersion\": 1");
            legacyShaderManifest.replace(sourcePath, std::string_view("Assets/Shaders/DefaultUnlit.hlsl").size(),
                                         "Assets/Shaders/LegacyUnlit.hlsl");
            legacyShaderManifest.erase(instanceAbi,
                                       std::string_view("  \"instanceAddressingAbiVersion\": 2,\n").size());
            LegacyShader = Database->CreateAsset(
                "LegacyShader.keireshader", shaderImporter,
                std::as_bytes(std::span(legacyShaderManifest.data(), legacyShaderManifest.size())));
            const std::string materialManifest = "{\"schemaVersion\":1,\"shader\":\"" + Shader.ToString() +
                                                 "\",\"properties\":{\"Tint\":[1,1,1,1],\"MainTexture\":\"" +
                                                 Texture.ToString() + "\"}}";
            MaterialPath = Root / "Assets/Material.keiremateriallegacy";
            Material =
                Database->CreateAsset("Material.keiremateriallegacy", materialImporter,
                                      std::as_bytes(std::span(materialManifest.data(), materialManifest.size())));
            const std::string legacyMaterialManifest =
                "{\"schemaVersion\":1,\"shader\":\"" + LegacyShader.ToString() + "\",\"properties\":{}}";
            LegacyMaterial = Database->CreateAsset(
                "LegacyMaterial.keiremateriallegacy", materialImporter,
                std::as_bytes(std::span(legacyMaterialManifest.data(), legacyMaterialManifest.size())));

            if (includeShaderGraph)
            {
                auto graph = Keire::CreateDefaultShaderGraph();
                auto baseColor = std::ranges::find(graph.Nodes.front().Pins, "BaseColor", &Keire::ShaderGraphPin::Name);
                if (baseColor == graph.Nodes.front().Pins.end())
                    throw std::logic_error("The default Shader Graph does not expose a BaseColor input.");
                baseColor->DefaultValue = Keire::Color{0.0F, 1.0F, 0.0F, 1.0F};
                if (includeProceduralVertexOffset)
                {
                    auto offset =
                        Keire::CreateShaderGraphNode(parameterDrivenVertexOffset ? Keire::ShaderGraphNodeKind::Parameter
                                                                                 : Keire::ShaderGraphNodeKind::Constant,
                                                     Keire::ShaderGraphValueType::Vector3);
                    offset.Value = Keire::Vector3{0.0F, 0.05F, 0.0F};
                    if (parameterDrivenVertexOffset)
                        offset.Symbol = "VertexOffset";
                    graph.Nodes.push_back(std::move(offset));
                    const auto offsetOutput =
                        std::ranges::find(graph.Nodes.back().Pins, "Value", &Keire::ShaderGraphPin::Name);
                    const auto masterInput = std::ranges::find(graph.Nodes.front().Pins, "WorldPositionOffset",
                                                               &Keire::ShaderGraphPin::Name);
                    if (offsetOutput == graph.Nodes.back().Pins.end() || masterInput == graph.Nodes.front().Pins.end())
                        throw std::logic_error("The procedural Shader Graph vertex pins are unavailable.");
                    graph.Connections.push_back({Keire::AssetId::Generate(),
                                                 {graph.Nodes.back().Id, offsetOutput->Id},
                                                 {graph.Nodes.front().Id, masterInput->Id}});
                }
                ShaderGraph = Database->CreateAsset("Basic.keireshadergraph", materialGraphImporter,
                                                    Keire::ShaderGraphAsset::EncodeSource(graph));
                const auto record = Database->Find(ShaderGraph);
                if (!record || record->SubAssets.empty())
                    throw std::runtime_error("The Shader Graph import did not publish its runtime material.");
                ShaderGraphMaterial = record->SubAssets.back();

                if (includeTransparentShaderGraph)
                {
                    auto transparentGraph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Transparent);
                    auto transparentBaseColor = std::ranges::find(transparentGraph.Nodes.front().Pins, "BaseColor",
                                                                  &Keire::ShaderGraphPin::Name);
                    auto transparentOpacity =
                        std::ranges::find(transparentGraph.Nodes.front().Pins, "Opacity", &Keire::ShaderGraphPin::Name);
                    if (transparentBaseColor == transparentGraph.Nodes.front().Pins.end() ||
                        transparentOpacity == transparentGraph.Nodes.front().Pins.end())
                        throw std::logic_error("The default Shader Graph does not expose transparent surface inputs.");
                    transparentBaseColor->DefaultValue = Keire::Color{0.0F, 1.0F, 0.0F, 1.0F};
                    transparentOpacity->DefaultValue = 0.5F;
                    TransparentShaderGraph =
                        Database->CreateAsset("Transparent.keireshadergraph", materialGraphImporter,
                                              Keire::ShaderGraphAsset::EncodeSource(transparentGraph));
                    const auto transparentRecord = Database->Find(TransparentShaderGraph);
                    if (!transparentRecord || transparentRecord->SubAssets.empty())
                        throw std::runtime_error("The transparent Shader Graph did not publish its runtime material.");
                    TransparentShaderGraphMaterial = transparentRecord->SubAssets.back();
                }
            }
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
        Keire::AssetId Volume;
        Keire::AssetId Skeleton;
        Keire::AssetId Skin;
        Keire::AssetId CubeMesh;
        Keire::AssetId OcclusionStressMesh;
        Keire::AssetId Material;
        Keire::AssetId LegacyMaterial;
        Keire::AssetId ShaderGraph;
        Keire::AssetId ShaderGraphMaterial;
        Keire::AssetId TransparentShaderGraph;
        Keire::AssetId TransparentShaderGraphMaterial;
        Keire::AssetId Shader;
        Keire::AssetId LegacyShader;
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
} // namespace KeireRenderTests::Detail
