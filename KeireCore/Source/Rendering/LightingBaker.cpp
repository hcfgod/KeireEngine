#include "Keire/Rendering/LightingBaker.h"

#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/LightProbeVolumeComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        struct Artifact
        {
            LightingBakeAssetOutput Output;
            std::string Importer;
            std::vector<std::byte> Bytes;
        };

        class ProjectAssetResolver final
        {
          public:
            explicit ProjectAssetResolver(const std::filesystem::path& projectRoot)
            {
                AssetSystemSpecification specification;
                specification.Mode = AssetMode::Development;
                specification.DevelopmentCatalog = projectRoot / "Library/AssetCache/Runtime/catalog.json";
                specification.Decoders = CreateBuiltinAssetDecoders();
                specification.WorkerCount = 1;
                m_Assets = CreateRef<AssetSystem>(std::move(specification));
            }

            ~ProjectAssetResolver()
            {
                if (m_Assets)
                    m_Assets->Close();
            }

            [[nodiscard]] Ref<const Asset> Resolve(const AssetId id)
            {
                if (!id)
                    return {};
                if (const auto found = m_Cache.find(id); found != m_Cache.end())
                    return found->second;
                Ref<const Asset> result;
                if (auto builtin = MeshAsset::ResolveBuiltin(id))
                    result = std::move(builtin);
                else if (const auto type = m_Assets->TryGetType(id))
                {
                    if (*type == MeshAsset::StaticType())
                        result = Await(m_Assets->Load<MeshAsset>(id, AssetPriority::High));
                    else if (*type == MaterialAsset::StaticType())
                        result = Await(m_Assets->Load<MaterialAsset>(id, AssetPriority::High));
                    else if (*type == Texture2DAsset::StaticType())
                        result = Await(m_Assets->Load<Texture2DAsset>(id, AssetPriority::High));
                }
                m_Cache.emplace(id, result);
                return result;
            }

          private:
            template <typename T> [[nodiscard]] Ref<const T> Await(const AssetHandle<T>& handle)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (handle.State() == AssetState::Queued || handle.State() == AssetState::Loading)
                {
                    (void)m_Assets->PumpCompletions();
                    if (std::chrono::steady_clock::now() >= deadline)
                        throw std::runtime_error("Timed out while resolving a lighting-bake dependency.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                (void)m_Assets->PumpCompletions();
                return handle.Require();
            }

            Ref<AssetSystem> m_Assets;
            std::unordered_map<AssetId, Ref<const Asset>> m_Cache;
        };

        void Report(const LightingBakeRequest& request, const LightingBakePhase phase, const std::size_t completed,
                    const std::size_t total, std::string message)
        {
            if (request.Cancellation.stop_requested())
                throw AssetOperationCancelled();
            if (request.Progress)
                request.Progress({phase, completed, total, std::move(message)});
        }

        [[nodiscard]] std::filesystem::path ValidateRelativeDirectory(const std::filesystem::path& value,
                                                                      const char* kind)
        {
            const auto normalized = value.lexically_normal();
            if (normalized.empty() || normalized.is_absolute() || normalized == ".." ||
                normalized.generic_string().starts_with("../"))
                throw std::invalid_argument(std::string(kind) + " must be a project-relative directory.");
            return normalized;
        }

        [[nodiscard]] std::string SanitizeName(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char input : value)
            {
                const auto character = static_cast<unsigned char>(input);
                result.push_back(static_cast<char>(std::isalnum(character) || character == '-' || character == '_'
                                                       ? character
                                                       : static_cast<unsigned char>('_')));
            }
            while (!result.empty() && result.back() == '_')
                result.pop_back();
            return result.empty() ? "Scene" : result.substr(0, 96);
        }

        [[nodiscard]] AssetId StableId(const AssetId scene, const std::string_view kind, const AssetId subject = {})
        {
            std::string seed = scene.ToString();
            seed.push_back(':');
            seed.append(kind);
            if (subject)
            {
                seed.push_back(':');
                seed.append(subject.ToString());
            }
            const auto digest = Detail::Sha256(std::as_bytes(std::span(seed)));
            std::uint64_t high = 0;
            std::uint64_t low = 0;
            std::memcpy(&high, digest.data(), sizeof(high));
            std::memcpy(&low, digest.data() + sizeof(high), sizeof(low));
            return AssetId(high, low == 0U && high == 0U ? 1U : low);
        }

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                throw std::runtime_error("Failed to open cached lighting artifact: " + Detail::PathToUtf8(path));
            const auto length = stream.tellg();
            if (length < 0 || static_cast<std::uint64_t>(length) > 2ULL * 1024ULL * 1024ULL * 1024ULL)
                throw std::runtime_error("Cached lighting artifact exceeds the 2 GiB safety limit.");
            std::vector<std::byte> result(static_cast<std::size_t>(length));
            stream.seekg(0);
            if (!result.empty())
                stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
            if (!stream)
                throw std::runtime_error("Failed to read cached lighting artifact: " + Detail::PathToUtf8(path));
            return result;
        }

        void WriteMetadata(const std::filesystem::path& source, const LightingBakeAssetOutput& output,
                           const std::string_view importer)
        {
            const Json metadata{
                {"schemaVersion", 1},        {"id", output.Id.ToString()}, {"type", output.Type.ToString()},
                {"importer", importer},      {"importerVersion", 1},       {"dependencies", Json::array()},
                {"subAssets", Json::array()}};
            Detail::WriteTextFileAtomically(Detail::PathWithSuffix(source, ".keiremeta"), metadata.dump(2) + '\n');
        }

        [[nodiscard]] std::string ArtifactDigest(const std::span<const std::byte> bytes)
        {
            return Detail::DigestToString(Detail::Sha256(bytes));
        }

        [[nodiscard]] bool LoadCachedArtifacts(const std::filesystem::path& cache, const std::string_view fingerprint,
                                               std::vector<Artifact>& artifacts)
        {
            try
            {
                const auto manifestBytes = ReadBytes(cache / "manifest.json");
                const std::string manifestText(reinterpret_cast<const char*>(manifestBytes.data()),
                                               manifestBytes.size());
                const auto manifest = Json::parse(manifestText);
                if (manifest.value("schemaVersion", 0) != 2 ||
                    manifest.value("fingerprint", std::string{}) != fingerprint || !manifest.contains("artifacts") ||
                    !manifest.at("artifacts").is_array() || manifest.at("artifacts").size() != artifacts.size())
                    return false;
                for (std::size_t index = 0; index < artifacts.size(); ++index)
                {
                    const auto& entry = manifest.at("artifacts").at(index);
                    auto& artifact = artifacts[index];
                    if (entry.value("filename", std::string{}) !=
                            artifact.Output.RelativePath.filename().generic_string() ||
                        entry.value("id", std::string{}) != artifact.Output.Id.ToString() ||
                        entry.value("type", std::string{}) != artifact.Output.Type.ToString() ||
                        entry.value("importer", std::string{}) != artifact.Importer)
                        return false;
                    artifact.Bytes = ReadBytes(cache / artifact.Output.RelativePath.filename());
                    if (entry.value("digest", std::string{}) != ArtifactDigest(artifact.Bytes))
                        return false;
                }
                return true;
            }
            catch (const std::exception&)
            {
                for (auto& artifact : artifacts)
                    artifact.Bytes.clear();
                return false;
            }
        }

        void StoreCachedArtifacts(const std::filesystem::path& cache, const std::string_view fingerprint,
                                  const std::span<const Artifact> artifacts)
        {
            Json entries = Json::array();
            for (const auto& artifact : artifacts)
            {
                Detail::WriteFileAtomically(cache / artifact.Output.RelativePath.filename(), artifact.Bytes);
                entries.push_back({{"filename", artifact.Output.RelativePath.filename().generic_string()},
                                   {"id", artifact.Output.Id.ToString()},
                                   {"type", artifact.Output.Type.ToString()},
                                   {"importer", artifact.Importer},
                                   {"digest", ArtifactDigest(artifact.Bytes)}});
            }
            Detail::WriteTextFileAtomically(
                cache / "manifest.json",
                Json{{"schemaVersion", 2}, {"fingerprint", fingerprint}, {"artifacts", std::move(entries)}}.dump(2) +
                    '\n');
        }

        void PublishArtifacts(const LightingBakeRequest& request, const std::span<const Artifact> artifacts,
                              LightingBakeResult& result)
        {
            Report(request, LightingBakePhase::Publishing, 0, artifacts.size(), "Publishing baked lighting assets");
            for (std::size_t index = 0; index < artifacts.size(); ++index)
            {
                const auto& artifact = artifacts[index];
                const auto destination = request.ProjectRoot / artifact.Output.RelativePath;
                Detail::WriteFileAtomically(destination, artifact.Bytes);
                WriteMetadata(destination, artifact.Output, artifact.Importer);
                result.Assets.push_back(artifact.Output);
                Report(request, LightingBakePhase::Publishing, index + 1U, artifacts.size(),
                       "Published " + artifact.Output.RelativePath.filename().string());
            }
        }

        [[nodiscard]] std::array<std::byte, 4> EncodeRgbe(const Vector3 radiance) noexcept
        {
            const auto maximum = std::max({radiance.X, radiance.Y, radiance.Z});
            if (maximum < 1.0e-32F)
                return {};
            int exponent = 0;
            const auto mantissa = std::frexp(maximum, &exponent);
            const auto scale = mantissa * 256.0F / maximum;
            return {std::byte(static_cast<std::uint8_t>(std::clamp(radiance.X * scale, 0.0F, 255.0F))),
                    std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Y * scale, 0.0F, 255.0F))),
                    std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Z * scale, 0.0F, 255.0F))),
                    std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)))};
        }

        [[nodiscard]] LightingTextureMip ConstantMip(const std::uint32_t width, const std::uint32_t height,
                                                     const std::uint32_t layers, const std::array<std::byte, 4> pixel)
        {
            const auto pixelCount = static_cast<std::uint64_t>(width) * height * layers;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / pixel.size())
                throw std::invalid_argument("Baked lighting texture exceeds the addressable size.");
            LightingTextureMip result{width, height, layers,
                                      std::vector<std::byte>(static_cast<std::size_t>(pixelCount) * pixel.size())};
            for (std::size_t offset = 0; offset < result.Pixels.size(); offset += pixel.size())
                std::memcpy(result.Pixels.data() + offset, pixel.data(), pixel.size());
            return result;
        }

        [[nodiscard]] Vector3 EmissiveFactor(const MaterialAssetDefinition& material) noexcept
        {
            if (!material.ContributeEmissionToGI)
                return {};
            for (const auto name : {std::string_view("EmissiveFactor"), std::string_view("Emission")})
            {
                const auto found = material.Properties.find(name);
                if (found == material.Properties.end())
                    continue;
                if (const auto* color = std::get_if<Color>(&found->second))
                    return {color->Red * material.EmissiveGIIntensity, color->Green * material.EmissiveGIIntensity,
                            color->Blue * material.EmissiveGIIntensity};
                if (const auto* vector = std::get_if<Vector4>(&found->second))
                    return {vector->X * material.EmissiveGIIntensity, vector->Y * material.EmissiveGIIntensity,
                            vector->Z * material.EmissiveGIIntensity};
            }
            return {};
        }

        enum class BakedLightType : std::uint8_t
        {
            Directional,
            Point,
            Spot
        };

        struct BakedLight
        {
            BakedLightType Type = BakedLightType::Directional;
            Vector3 Position;
            Vector3 Direction{0.0F, 0.0F, 1.0F};
            Vector3 Radiance;
            float Range = 1.0F;
            float InnerCosine = 1.0F;
            float OuterCosine = 1.0F;
        };

        struct EmissiveSource
        {
            Vector3 Position;
            Vector3 Radiance;
        };

        struct BakeSceneLighting
        {
            std::vector<BakedLight> Lights;
            std::vector<EmissiveSource> EmissiveSources;
            Vector3 AmbientIrradiance{0.09424778F, 0.09424778F, 0.09424778F};
        };

        struct IrradianceSample
        {
            Vector3 Irradiance;
            Vector3 DominantDirection{0.0F, 1.0F, 0.0F};
            float DirectionalWeight = 0.0F;
        };

        [[nodiscard]] Vector3 AddVector(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 SubtractVector(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] Vector3 ScaleVector(const Vector3 value, const float scale) noexcept
        {
            return {value.X * scale, value.Y * scale, value.Z * scale};
        }

        [[nodiscard]] float DotVector(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 NormalizeVector(const Vector3 value) noexcept
        {
            const auto lengthSquared = DotVector(value, value);
            if (lengthSquared <= 1.0e-12F)
                return {};
            return ScaleVector(value, 1.0F / std::sqrt(lengthSquared));
        }

        [[nodiscard]] float Luminance(const Vector3 value) noexcept
        {
            return value.X * 0.2126F + value.Y * 0.7152F + value.Z * 0.0722F;
        }

        [[nodiscard]] BakeSceneLighting GatherBakeSceneLighting(const LightingBakeRequest& request,
                                                                const Ref<Scene>& scene,
                                                                const std::vector<Entity>& renderers)
        {
            BakeSceneLighting result;
            const auto bounceScale =
                std::min(static_cast<float>(request.Definition.Lighting.IndirectBounceCount) * 0.1F, 1.0F);
            const auto radiance =
                [bounceScale](const Color color, const float intensity, const float indirect, const LightBakeMode mode)
            {
                const auto indirectScale = std::max(indirect, 0.0F) * bounceScale;
                const auto scale =
                    mode == LightBakeMode::Mixed ? intensity * indirectScale : intensity * (1.0F + indirectScale);
                return Vector3{color.Red * scale, color.Green * scale, color.Blue * scale};
            };
            for (const auto& entity : scene->Query<DirectionalLightComponent>())
            {
                const auto light = entity.GetComponent<DirectionalLightComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (light && transform && light->Enabled() && entity.ActiveInHierarchy() &&
                    light->BakeMode() != LightBakeMode::Realtime)
                {
                    result.Lights.push_back(
                        {BakedLightType::Directional,
                         {},
                         NormalizeVector(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F})),
                         radiance(light->LightColor(), light->Intensity(), light->IndirectMultiplier(),
                                  light->BakeMode())});
                }
            }
            for (const auto& entity : scene->Query<PointLightComponent>())
            {
                const auto light = entity.GetComponent<PointLightComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (light && transform && light->Enabled() && entity.ActiveInHierarchy() &&
                    light->BakeMode() != LightBakeMode::Realtime)
                {
                    result.Lights.push_back({BakedLightType::Point,
                                             transform->WorldPosition(),
                                             {},
                                             radiance(light->LightColor(), light->Intensity(),
                                                      light->IndirectMultiplier(), light->BakeMode()),
                                             light->Range()});
                }
            }
            constexpr float degreesToRadians = 0.01745329251994329577F;
            for (const auto& entity : scene->Query<SpotLightComponent>())
            {
                const auto light = entity.GetComponent<SpotLightComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (light && transform && light->Enabled() && entity.ActiveInHierarchy() &&
                    light->BakeMode() != LightBakeMode::Realtime)
                {
                    result.Lights.push_back(
                        {BakedLightType::Spot, transform->WorldPosition(),
                         NormalizeVector(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F})),
                         radiance(light->LightColor(), light->Intensity(), light->IndirectMultiplier(),
                                  light->BakeMode()),
                         light->Range(), std::cos(light->InnerAngleDegrees() * degreesToRadians),
                         std::cos(light->OuterAngleDegrees() * degreesToRadians)});
                }
            }
            if (request.ResolveAsset)
            {
                for (const auto& entity : renderers)
                {
                    const auto renderer = entity.GetComponent<MeshRendererComponent>();
                    const auto transform = entity.GetComponent<TransformComponent>();
                    if (!renderer || !transform)
                        continue;
                    Vector3 entityEmission;
                    for (const auto materialId : renderer->Materials())
                    {
                        const auto material = DynamicRefCast<const MaterialAsset>(request.ResolveAsset(materialId));
                        if (!material)
                            continue;
                        const auto emissive = EmissiveFactor(material->Definition());
                        entityEmission = AddVector(entityEmission, emissive);
                    }
                    entityEmission = ScaleVector(entityEmission, bounceScale);
                    if (Luminance(entityEmission) > 1.0e-6F)
                        result.EmissiveSources.push_back({transform->WorldPosition(), entityEmission});
                }
            }
            return result;
        }

        [[nodiscard]] IrradianceSample EvaluateIrradiance(const BakeSceneLighting& lighting, const Vector3 position,
                                                          const Vector3 normal)
        {
            IrradianceSample result{lighting.AmbientIrradiance};
            const auto surfaceNormal = NormalizeVector(normal);
            float totalDirectionalLuminance = 0.0F;
            float dominantLuminance = 0.0F;
            const auto addDirectional = [&](const Vector3 value, const Vector3 direction)
            {
                result.Irradiance = AddVector(result.Irradiance, value);
                const auto luminance = Luminance(value);
                totalDirectionalLuminance += luminance;
                if (luminance > dominantLuminance)
                {
                    dominantLuminance = luminance;
                    result.DominantDirection = direction;
                }
            };
            for (const auto& light : lighting.Lights)
            {
                Vector3 incoming;
                float attenuation = 1.0F;
                if (light.Type == BakedLightType::Directional)
                    incoming = ScaleVector(light.Direction, -1.0F);
                else
                {
                    const auto offset = SubtractVector(light.Position, position);
                    const auto distanceSquared = DotVector(offset, offset);
                    const auto distance = std::sqrt(std::max(distanceSquared, 1.0e-8F));
                    if (distance >= light.Range)
                        continue;
                    incoming = ScaleVector(offset, 1.0F / distance);
                    const auto normalizedDistance = distance / light.Range;
                    const auto rangeFade = std::clamp(1.0F - std::pow(normalizedDistance, 4.0F), 0.0F, 1.0F);
                    attenuation = rangeFade * rangeFade / std::max(distanceSquared, 0.01F);
                    if (light.Type == BakedLightType::Spot)
                    {
                        const auto coneCosine = DotVector(light.Direction, ScaleVector(incoming, -1.0F));
                        const auto coneRange = std::max(light.InnerCosine - light.OuterCosine, 1.0e-5F);
                        const auto cone = std::clamp((coneCosine - light.OuterCosine) / coneRange, 0.0F, 1.0F);
                        attenuation *= cone * cone * (3.0F - 2.0F * cone);
                    }
                }
                const auto cosine = std::max(DotVector(surfaceNormal, incoming), 0.0F);
                if (cosine > 0.0F && attenuation > 0.0F)
                    addDirectional(ScaleVector(light.Radiance, cosine * attenuation), incoming);
            }
            for (const auto& source : lighting.EmissiveSources)
            {
                const auto offset = SubtractVector(source.Position, position);
                const auto distanceSquared = DotVector(offset, offset);
                if (distanceSquared <= 1.0e-8F)
                    continue;
                const auto incoming = ScaleVector(offset, 1.0F / std::sqrt(distanceSquared));
                const auto cosine = std::max(DotVector(surfaceNormal, incoming), 0.0F);
                if (cosine > 0.0F)
                    addDirectional(ScaleVector(source.Radiance, cosine / (1.0F + distanceSquared)), incoming);
            }
            result.Irradiance = {std::clamp(result.Irradiance.X, 0.0F, 65'000.0F),
                                 std::clamp(result.Irradiance.Y, 0.0F, 65'000.0F),
                                 std::clamp(result.Irradiance.Z, 0.0F, 65'000.0F)};
            const auto total = totalDirectionalLuminance + Luminance(lighting.AmbientIrradiance);
            result.DirectionalWeight = total > 1.0e-6F ? std::clamp(dominantLuminance / total, 0.0F, 1.0F) : 0.0F;
            return result;
        }

        [[nodiscard]] Vector3 EstimateSceneRadiance(const BakeSceneLighting& lighting, const Vector3 position = {})
        {
            constexpr std::array directions{Vector3{1.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F},
                                            Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, -1.0F, 0.0F},
                                            Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 0.0F, -1.0F}};
            Vector3 result;
            for (const auto direction : directions)
                result = AddVector(result, EvaluateIrradiance(lighting, position, direction).Irradiance);
            return ScaleVector(result, 1.0F / (static_cast<float>(directions.size()) * 3.14159265358979323846F));
        }

        struct BakedLightmapLayer
        {
            std::vector<std::byte> Irradiance;
            std::vector<std::byte> Directionality;
        };

        void StoreLightmapSample(BakedLightmapLayer& layer, const std::size_t pixel, const IrradianceSample& sample)
        {
            const auto rgbe = EncodeRgbe(sample.Irradiance);
            const auto offset = pixel * 4U;
            std::memcpy(layer.Irradiance.data() + offset, rgbe.data(), rgbe.size());
            const auto encodeDirection = [](const float value)
            {
                return std::byte(
                    static_cast<std::uint8_t>(std::clamp(std::lround((value * 0.5F + 0.5F) * 255.0F), 0L, 255L)));
            };
            layer.Directionality[offset] = encodeDirection(sample.DominantDirection.X);
            layer.Directionality[offset + 1U] = encodeDirection(sample.DominantDirection.Y);
            layer.Directionality[offset + 2U] = encodeDirection(sample.DominantDirection.Z);
            layer.Directionality[offset + 3U] = std::byte(
                static_cast<std::uint8_t>(std::clamp(std::lround(sample.DirectionalWeight * 255.0F), 0L, 255L)));
        }

        [[nodiscard]] float TriangleArea(const Vector2 first, const Vector2 second, const Vector2 third) noexcept
        {
            return (second.X - first.X) * (third.Y - first.Y) - (second.Y - first.Y) * (third.X - first.X);
        }

        [[nodiscard]] BakedLightmapLayer BakeLightmapLayer(const BakeSceneLighting& lighting, const Entity& entity,
                                                           const std::uint32_t resolution,
                                                           const std::function<Ref<const Asset>(AssetId)>& resolveAsset)
        {
            const auto pixelCount = static_cast<std::size_t>(resolution) * resolution;
            BakedLightmapLayer result{std::vector<std::byte>(pixelCount * 4U), std::vector<std::byte>(pixelCount * 4U)};
            const auto renderer = entity.GetComponent<MeshRendererComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!renderer || !transform)
                return result;
            const auto fallback = EvaluateIrradiance(lighting, transform->WorldPosition(), {0.0F, 1.0F, 0.0F});
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                StoreLightmapSample(result, pixel, fallback);

            Ref<const MeshAsset> mesh;
            if (!renderer->Mesh())
                mesh = MeshAsset::Cube();
            else if (auto builtin = MeshAsset::ResolveBuiltin(renderer->Mesh()))
                mesh = std::move(builtin);
            else if (resolveAsset)
                mesh = DynamicRefCast<const MeshAsset>(resolveAsset(renderer->Mesh()));
            if (!mesh || mesh->Indices().size() < 3U)
                return result;

            const auto vertices = mesh->Vertices();
            const auto indices = mesh->Indices();
            const auto world = transform->WorldMatrix();
            for (std::size_t triangle = 0; triangle + 2U < indices.size(); triangle += 3U)
            {
                const auto index0 = indices[triangle];
                const auto index1 = indices[triangle + 1U];
                const auto index2 = indices[triangle + 2U];
                if (index0 >= vertices.size() || index1 >= vertices.size() || index2 >= vertices.size())
                    continue;
                const auto& vertex0 = vertices[index0];
                const auto& vertex1 = vertices[index1];
                const auto& vertex2 = vertices[index2];
                auto uv0 = vertex0.UV1;
                auto uv1 = vertex1.UV1;
                auto uv2 = vertex2.UV1;
                auto area = TriangleArea(uv0, uv1, uv2);
                if (std::abs(area) <= 1.0e-8F)
                {
                    uv0 = vertex0.UV0;
                    uv1 = vertex1.UV0;
                    uv2 = vertex2.UV0;
                    area = TriangleArea(uv0, uv1, uv2);
                }
                if (std::abs(area) <= 1.0e-8F)
                    continue;
                const auto minimumX = static_cast<std::uint32_t>(
                    std::clamp(std::floor(std::min({uv0.X, uv1.X, uv2.X}) * static_cast<float>(resolution)), 0.0F,
                               static_cast<float>(resolution - 1U)));
                const auto maximumX = static_cast<std::uint32_t>(
                    std::clamp(std::ceil(std::max({uv0.X, uv1.X, uv2.X}) * static_cast<float>(resolution)), 0.0F,
                               static_cast<float>(resolution - 1U)));
                const auto minimumY = static_cast<std::uint32_t>(
                    std::clamp(std::floor(std::min({uv0.Y, uv1.Y, uv2.Y}) * static_cast<float>(resolution)), 0.0F,
                               static_cast<float>(resolution - 1U)));
                const auto maximumY = static_cast<std::uint32_t>(
                    std::clamp(std::ceil(std::max({uv0.Y, uv1.Y, uv2.Y}) * static_cast<float>(resolution)), 0.0F,
                               static_cast<float>(resolution - 1U)));
                for (std::uint32_t y = minimumY; y <= maximumY; ++y)
                {
                    for (std::uint32_t x = minimumX; x <= maximumX; ++x)
                    {
                        const Vector2 uv{(static_cast<float>(x) + 0.5F) / static_cast<float>(resolution),
                                         (static_cast<float>(y) + 0.5F) / static_cast<float>(resolution)};
                        const auto weight0 = TriangleArea(uv1, uv2, uv) / area;
                        const auto weight1 = TriangleArea(uv2, uv0, uv) / area;
                        const auto weight2 = 1.0F - weight0 - weight1;
                        if (weight0 < -1.0e-5F || weight1 < -1.0e-5F || weight2 < -1.0e-5F)
                            continue;
                        const Vector3 localPosition{
                            vertex0.Position.X * weight0 + vertex1.Position.X * weight1 + vertex2.Position.X * weight2,
                            vertex0.Position.Y * weight0 + vertex1.Position.Y * weight1 + vertex2.Position.Y * weight2,
                            vertex0.Position.Z * weight0 + vertex1.Position.Z * weight1 + vertex2.Position.Z * weight2};
                        const Vector3 localNormal{
                            vertex0.Normal.X * weight0 + vertex1.Normal.X * weight1 + vertex2.Normal.X * weight2,
                            vertex0.Normal.Y * weight0 + vertex1.Normal.Y * weight1 + vertex2.Normal.Y * weight2,
                            vertex0.Normal.Z * weight0 + vertex1.Normal.Z * weight1 + vertex2.Normal.Z * weight2};
                        const auto worldPosition = Math::TransformPoint(world, localPosition);
                        const auto worldNormal = NormalizeVector(Math::TransformDirection(world, localNormal));
                        StoreLightmapSample(result, static_cast<std::size_t>(y) * resolution + x,
                                            EvaluateIrradiance(lighting, worldPosition, worldNormal));
                    }
                }
            }
            return result;
        }

        [[nodiscard]] std::array<Vector3, 9> BakeProbeIrradiance(const BakeSceneLighting& lighting,
                                                                 const Vector3 worldPosition)
        {
            constexpr std::size_t sampleCount = 64;
            constexpr float pi = 3.14159265358979323846F;
            constexpr float goldenAngle = 2.39996322972865332F;
            constexpr float c0 = 0.28209479177387814F;
            constexpr float c1 = 0.4886025119029199F;
            constexpr float c2 = 1.0925484305920792F;
            constexpr float c3 = 0.31539156525252005F;
            constexpr float c4 = 0.5462742152960396F;
            std::array<Vector3, 9> result{};
            for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                const auto z = 1.0F - 2.0F * (static_cast<float>(sampleIndex) + 0.5F) / static_cast<float>(sampleCount);
                const auto radius = std::sqrt(std::max(0.0F, 1.0F - z * z));
                const auto angle = goldenAngle * static_cast<float>(sampleIndex);
                const Vector3 normal{radius * std::cos(angle), radius * std::sin(angle), z};
                const auto irradiance = EvaluateIrradiance(lighting, worldPosition, normal).Irradiance;
                const std::array basis{c0,
                                       c1 * normal.Y,
                                       c1 * normal.Z,
                                       c1 * normal.X,
                                       c2 * normal.X * normal.Y,
                                       c2 * normal.Y * normal.Z,
                                       c3 * (3.0F * normal.Z * normal.Z - 1.0F),
                                       c2 * normal.X * normal.Z,
                                       c4 * (normal.X * normal.X - normal.Y * normal.Y)};
                for (std::size_t coefficient = 0; coefficient < result.size(); ++coefficient)
                    result[coefficient] = AddVector(result[coefficient], ScaleVector(irradiance, basis[coefficient]));
            }
            const auto integrationWeight = 4.0F * pi / static_cast<float>(sampleCount);
            for (auto& coefficient : result)
                coefficient = ScaleVector(coefficient, integrationWeight);
            return result;
        }

        [[nodiscard]] LightingBakeBackend SelectBackend(const LightingBakeSettings& settings) noexcept
        {
            (void)settings;
            return LightingBakeBackend::CPU;
        }

        [[nodiscard]] Artifact MakeArtifact(const LightingBakeAssetOutput& output, std::string importer,
                                            std::vector<std::byte> bytes)
        {
            return {output, std::move(importer), std::move(bytes)};
        }
    } // namespace

    std::string LightingBaker::Fingerprint(const LightingBakeRequest& request)
    {
        if (!request.Scene)
            throw std::invalid_argument("Lighting bake requires a stable scene asset ID.");
        SceneAsset::Validate(request.Definition);
        auto fingerprintDefinition = request.Definition;
        fingerprintDefinition.BakedLighting = {};
        std::vector<std::byte> seed = SceneAsset::Encode(fingerprintDefinition);
        auto inputs = request.Inputs;
        std::ranges::sort(inputs, {}, &LightingBakeInputDigest::Asset);
        for (const auto& input : inputs)
        {
            if (!input.Asset || input.Digest.empty())
                throw std::invalid_argument("Lighting bake input digests require asset IDs and non-empty digests.");
            const auto identity = input.Asset.ToString() + ":" + input.Digest + "\n";
            const auto bytes = std::as_bytes(std::span(identity));
            seed.insert(seed.end(), bytes.begin(), bytes.end());
        }
        constexpr std::string_view bakerVersion = "Keire.LightingBaker/3\n";
        const auto versionBytes = std::as_bytes(std::span(bakerVersion));
        seed.insert(seed.end(), versionBytes.begin(), versionBytes.end());
        return Detail::DigestToString(Detail::Sha256(seed));
    }

    LightingBakeResult LightingBaker::Bake(const LightingBakeRequest& request)
    {
        Report(request, LightingBakePhase::Fingerprinting, 0, 1, "Fingerprinting scene and lighting inputs");
        const auto fingerprint = Fingerprint(request);
        const auto outputRoot = ValidateRelativeDirectory(request.OutputDirectory, "Lighting output directory");
        const auto cacheRoot = ValidateRelativeDirectory(request.CacheDirectory, "Lighting cache directory");
        const auto scene = CreateRef<Scene>(request.Scene, request.Definition);
        std::vector<Entity> renderers;
        for (const auto& entity : scene->Query<MeshRendererComponent>())
        {
            const auto renderer = entity.GetComponent<MeshRendererComponent>();
            if (renderer && renderer->Enabled() && entity.ActiveInHierarchy() && renderer->StaticLighting() &&
                renderer->GIReceive() == GIReceiveMode::Lightmaps)
                renderers.push_back(entity);
        }
        std::ranges::sort(renderers, {}, &Entity::Id);
        auto reflectionEntities = scene->Query<ReflectionProbeComponent>();
        std::erase_if(reflectionEntities,
                      [](const Entity& entity)
                      {
                          const auto probe = entity.GetComponent<ReflectionProbeComponent>();
                          return !probe || !probe->Enabled() || !entity.ActiveInHierarchy();
                      });
        std::ranges::sort(reflectionEntities, {}, &Entity::Id);
        auto volumeEntities = scene->Query<LightProbeVolumeComponent>();
        std::erase_if(volumeEntities,
                      [](const Entity& entity)
                      {
                          const auto volume = entity.GetComponent<LightProbeVolumeComponent>();
                          return !volume || !volume->Enabled() || !entity.ActiveInHierarchy();
                      });
        std::ranges::sort(volumeEntities, {}, &Entity::Id);
        std::vector<Entity> mixedLights;
        const auto appendMixed = [&]<typename T>()
        {
            for (const auto& entity : scene->Query<T>())
            {
                const auto light = entity.template GetComponent<T>();
                if (light && light->Enabled() && entity.ActiveInHierarchy() &&
                    light->BakeMode() == LightBakeMode::Mixed)
                    mixedLights.push_back(entity);
            }
        };
        appendMixed.template operator()<DirectionalLightComponent>();
        appendMixed.template operator()<PointLightComponent>();
        appendMixed.template operator()<SpotLightComponent>();
        std::ranges::sort(mixedLights, {}, &Entity::Id);
        if (mixedLights.size() > 8U)
            throw std::invalid_argument("A bake currently supports at most eight overlapping mixed lights.");

        const auto sceneDirectory = outputRoot / SanitizeName(request.Definition.Name);
        const auto absoluteOutput = request.ProjectRoot / sceneDirectory;
        const auto absoluteCache = request.ProjectRoot / cacheRoot / fingerprint;
        std::error_code directoryError;
        std::filesystem::create_directories(absoluteOutput, directoryError);
        if (directoryError)
            throw std::runtime_error("Failed to create the baked-lighting output directory: " +
                                     directoryError.message());
        std::filesystem::create_directories(absoluteCache, directoryError);
        if (directoryError)
            throw std::runtime_error("Failed to create the baked-lighting cache directory: " +
                                     directoryError.message());

        LightingBakeResult result;
        result.LightingSet = StableId(request.Scene, "lighting-set");
        result.InputFingerprint = fingerprint;
        result.Backend = SelectBackend(request.Definition.Lighting);
        LightingSetDefinition lightingSet;
        lightingSet.Scene = request.Scene;
        lightingSet.InputFingerprint = fingerprint;
        std::vector<Artifact> artifacts;
        const auto addOutput = [&](const AssetId id, const AssetTypeId type, const std::filesystem::path& filename)
        { return LightingBakeAssetOutput{id, type, sceneDirectory / filename}; };
        std::vector<Artifact> cachedArtifacts;
        if (!renderers.empty())
        {
            cachedArtifacts.push_back(
                MakeArtifact(addOutput(StableId(request.Scene, "lightmaps"), LightingTextureArrayAsset::StaticType(),
                                       "Lightmaps.keirelightingtexture"),
                             "Keire.LightingTextureArray", {}));
            cachedArtifacts.push_back(MakeArtifact(addOutput(StableId(request.Scene, "lightmap-directionality"),
                                                             LightingTextureArrayAsset::StaticType(),
                                                             "LightmapDirectionality.keirelightingtexture"),
                                                   "Keire.LightingTextureArray", {}));
        }
        if (!mixedLights.empty() && !renderers.empty())
        {
            cachedArtifacts.push_back(
                MakeArtifact(addOutput(StableId(request.Scene, "shadow-masks-0-3"),
                                       LightingTextureArrayAsset::StaticType(), "ShadowMasks0.keirelightingtexture"),
                             "Keire.LightingTextureArray", {}));
        }
        if (!reflectionEntities.empty())
        {
            cachedArtifacts.push_back(MakeArtifact(addOutput(StableId(request.Scene, "reflection-cubemaps"),
                                                             LightingTextureArrayAsset::StaticType(),
                                                             "ReflectionProbes.keirelightingtexture"),
                                                   "Keire.LightingTextureArray", {}));
        }
        for (std::size_t volumeIndex = 0; volumeIndex < volumeEntities.size(); ++volumeIndex)
        {
            const auto id = StableId(request.Scene, "light-probe-volume", volumeEntities[volumeIndex].Id().Value());
            cachedArtifacts.push_back(
                MakeArtifact(addOutput(id, LightProbeVolumeAsset::StaticType(),
                                       "LightProbeVolume_" + std::to_string(volumeIndex) + ".keireprobevolume"),
                             "Keire.LightProbeVolume", {}));
        }
        cachedArtifacts.push_back(
            MakeArtifact(addOutput(result.LightingSet, LightingSetAsset::StaticType(), "BakedLighting.keirelighting"),
                         "Keire.LightingSet", {}));
        Report(request, LightingBakePhase::CacheLookup, 0, cachedArtifacts.size(),
               "Checking deterministic lighting cache");
        if (!request.Force && LoadCachedArtifacts(absoluteCache, fingerprint, cachedArtifacts))
        {
            result.CacheHit = true;
            PublishArtifacts(request, cachedArtifacts, result);
            Report(request, LightingBakePhase::Complete, 1, 1, "Lighting bake restored from cache");
            scene->Close();
            return result;
        }

        LightingBakeRequest resolvedRequest = request;
        std::shared_ptr<ProjectAssetResolver> projectAssets;
        const auto developmentCatalog = request.ProjectRoot / "Library/AssetCache/Runtime/catalog.json";
        if (!resolvedRequest.ResolveAsset && std::filesystem::is_regular_file(developmentCatalog))
        {
            projectAssets = std::make_shared<ProjectAssetResolver>(request.ProjectRoot);
            resolvedRequest.ResolveAsset = [projectAssets](const AssetId id) { return projectAssets->Resolve(id); };
        }
        if (request.Definition.Lighting.Backend == LightingBakeBackend::GPU)
        {
            Report(request, LightingBakePhase::PreparingGeometry, 0, renderers.size(),
                   "GPU bake backend unavailable; using deterministic CPU fallback");
        }
        else
        {
            Report(request, LightingBakePhase::PreparingGeometry, 0, renderers.size(),
                   "Preparing static geometry and emissive inputs");
        }
        const auto bakeLighting = GatherBakeSceneLighting(resolvedRequest, scene, renderers);

        Report(request, LightingBakePhase::BakingLightmaps, 0, renderers.size(), "Baking static renderer lightmaps");
        if (!renderers.empty())
        {
            const auto layers = static_cast<std::uint32_t>(renderers.size());
            const auto resolution = request.Definition.Lighting.LightmapResolution;
            lightingSet.Lightmaps = StableId(request.Scene, "lightmaps");
            lightingSet.Directionality = StableId(request.Scene, "lightmap-directionality");
            LightingTextureArrayDefinition lightmaps;
            lightmaps.Encoding = LightingTextureEncoding::Rgbe8;
            lightmaps.Mips.push_back(ConstantMip(resolution, resolution, layers, {}));
            LightingTextureArrayDefinition directionality;
            directionality.Encoding = LightingTextureEncoding::Rgba8;
            directionality.Mips.push_back(ConstantMip(resolution, resolution, layers, {}));
            const auto layerBytes = static_cast<std::size_t>(resolution) * resolution * 4U;
            for (std::uint32_t layer = 0; layer < layers; ++layer)
            {
                const auto baked =
                    BakeLightmapLayer(bakeLighting, renderers[layer], resolution, resolvedRequest.ResolveAsset);
                std::memcpy(lightmaps.Mips.front().Pixels.data() + static_cast<std::size_t>(layer) * layerBytes,
                            baked.Irradiance.data(), layerBytes);
                std::memcpy(directionality.Mips.front().Pixels.data() + static_cast<std::size_t>(layer) * layerBytes,
                            baked.Directionality.data(), layerBytes);
                Report(request, LightingBakePhase::BakingLightmaps, layer + 1U, layers,
                       "Baked renderer " + renderers[layer].Id().Value().ToString());
            }
            artifacts.push_back(MakeArtifact(addOutput(lightingSet.Lightmaps, LightingTextureArrayAsset::StaticType(),
                                                       "Lightmaps.keirelightingtexture"),
                                             "Keire.LightingTextureArray",
                                             LightingTextureArrayAsset::Encode(lightmaps)));
            artifacts.push_back(
                MakeArtifact(addOutput(lightingSet.Directionality, LightingTextureArrayAsset::StaticType(),
                                       "LightmapDirectionality.keirelightingtexture"),
                             "Keire.LightingTextureArray", LightingTextureArrayAsset::Encode(directionality)));
            for (std::uint32_t layer = 0; layer < layers; ++layer)
                lightingSet.Renderers.push_back(
                    {renderers[layer].Id().Value(), layer, {1.0F, 1.0F, 0.0F, 0.0F}, layer});
        }

        if (!mixedLights.empty() && !renderers.empty())
        {
            const auto resolution = request.Definition.Lighting.LightmapResolution;
            const auto layers = static_cast<std::uint32_t>(renderers.size());
            lightingSet.ShadowMasks = StableId(request.Scene, "shadow-masks-0-3");
            lightingSet.ShadowMasksSecondary = lightingSet.ShadowMasks;
            LightingTextureArrayDefinition masks;
            masks.Encoding = LightingTextureEncoding::Rgba8;
            masks.Mips.push_back(ConstantMip(resolution, resolution, layers * 2U,
                                             {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}));
            artifacts.push_back(MakeArtifact(addOutput(lightingSet.ShadowMasks, LightingTextureArrayAsset::StaticType(),
                                                       "ShadowMasks0.keirelightingtexture"),
                                             "Keire.LightingTextureArray", LightingTextureArrayAsset::Encode(masks)));
            for (std::size_t channel = 0; channel < mixedLights.size(); ++channel)
                lightingSet.MixedLights.push_back(
                    {mixedLights[channel].Id().Value(), static_cast<std::uint8_t>(channel)});
        }

        Report(request, LightingBakePhase::BakingReflectionProbes, 0, reflectionEntities.size(),
               "Baking and GGX-prefiltering reflection probes");
        if (!reflectionEntities.empty())
        {
            std::uint32_t resolution = 64;
            for (const auto& entity : reflectionEntities)
                resolution =
                    std::max(resolution,
                             static_cast<std::uint32_t>(entity.GetComponent<ReflectionProbeComponent>()->Resolution()));
            LightingTextureArrayDefinition cubemaps;
            cubemaps.Target = LightingTextureTarget::CubeArray;
            cubemaps.Encoding = LightingTextureEncoding::Rgbe8;
            std::uint32_t mipResolution = resolution;
            while (true)
            {
                auto mip = ConstantMip(mipResolution, mipResolution,
                                       static_cast<std::uint32_t>(reflectionEntities.size()) * 6U, {});
                const auto faceBytes = static_cast<std::size_t>(mipResolution) * mipResolution * 4U;
                for (std::size_t cube = 0; cube < reflectionEntities.size(); ++cube)
                {
                    const auto transform = reflectionEntities[cube].GetComponent<TransformComponent>();
                    const auto probeRadiance =
                        EstimateSceneRadiance(bakeLighting, transform ? transform->WorldPosition() : Vector3{});
                    const auto encoded = EncodeRgbe(probeRadiance);
                    for (std::size_t face = 0; face < 6U; ++face)
                    {
                        const auto faceOffset = (cube * 6U + face) * faceBytes;
                        for (std::size_t offset = 0; offset < faceBytes; offset += encoded.size())
                            std::memcpy(mip.Pixels.data() + faceOffset + offset, encoded.data(), encoded.size());
                    }
                }
                cubemaps.Mips.push_back(std::move(mip));
                if (mipResolution == 1U)
                    break;
                mipResolution = std::max(1U, mipResolution / 2U);
            }
            lightingSet.ReflectionCubemaps = StableId(request.Scene, "reflection-cubemaps");
            artifacts.push_back(
                MakeArtifact(addOutput(lightingSet.ReflectionCubemaps, LightingTextureArrayAsset::StaticType(),
                                       "ReflectionProbes.keirelightingtexture"),
                             "Keire.LightingTextureArray", LightingTextureArrayAsset::Encode(cubemaps)));
            for (std::uint32_t cube = 0; cube < reflectionEntities.size(); ++cube)
                lightingSet.ReflectionProbes.push_back({reflectionEntities[cube].Id().Value(), cube});
            Report(request, LightingBakePhase::BakingReflectionProbes, reflectionEntities.size(),
                   reflectionEntities.size(), "Baked spatial reflection probe array");
        }

        Report(request, LightingBakePhase::BakingLightProbes, 0, volumeEntities.size(),
               "Baking spherical-harmonic light probe volumes");
        for (std::size_t volumeIndex = 0; volumeIndex < volumeEntities.size(); ++volumeIndex)
        {
            const auto& entity = volumeEntities[volumeIndex];
            const auto volume = entity.GetComponent<LightProbeVolumeComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            LightProbeVolumeDefinition definition;
            definition.Origin = {-volume->BoxExtents().X, -volume->BoxExtents().Y, -volume->BoxExtents().Z};
            definition.Spacing = volume->Spacing();
            definition.CountX =
                static_cast<std::uint32_t>(std::ceil((volume->BoxExtents().X * 2.0F) / definition.Spacing.X)) + 1U;
            definition.CountY =
                static_cast<std::uint32_t>(std::ceil((volume->BoxExtents().Y * 2.0F) / definition.Spacing.Y)) + 1U;
            definition.CountZ =
                static_cast<std::uint32_t>(std::ceil((volume->BoxExtents().Z * 2.0F) / definition.Spacing.Z)) + 1U;
            definition.Probes.resize(static_cast<std::size_t>(definition.CountX) * definition.CountY *
                                     definition.CountZ);
            for (std::uint32_t z = 0; z < definition.CountZ; ++z)
            {
                for (std::uint32_t y = 0; y < definition.CountY; ++y)
                {
                    for (std::uint32_t x = 0; x < definition.CountX; ++x)
                    {
                        const Vector3 localPosition{definition.Origin.X + static_cast<float>(x) * definition.Spacing.X,
                                                    definition.Origin.Y + static_cast<float>(y) * definition.Spacing.Y,
                                                    definition.Origin.Z + static_cast<float>(z) * definition.Spacing.Z};
                        const auto probeIndex =
                            (static_cast<std::size_t>(z) * definition.CountY + y) * definition.CountX + x;
                        definition.Probes[probeIndex].Irradiance = BakeProbeIrradiance(
                            bakeLighting, Math::TransformPoint(transform->WorldMatrix(), localPosition));
                    }
                }
            }
            const auto id = StableId(request.Scene, "light-probe-volume", entity.Id().Value());
            const auto filename = "LightProbeVolume_" + std::to_string(volumeIndex) + ".keireprobevolume";
            artifacts.push_back(MakeArtifact(addOutput(id, LightProbeVolumeAsset::StaticType(), filename),
                                             "Keire.LightProbeVolume", LightProbeVolumeAsset::Encode(definition)));
            lightingSet.LightProbeVolumes.push_back({entity.Id().Value(), id});
            Report(request, LightingBakePhase::BakingLightProbes, volumeIndex + 1U, volumeEntities.size(),
                   "Baked light probe volume " + entity.Id().Value().ToString());
        }

        artifacts.push_back(
            MakeArtifact(addOutput(result.LightingSet, LightingSetAsset::StaticType(), "BakedLighting.keirelighting"),
                         "Keire.LightingSet", LightingSetAsset::Encode(lightingSet)));
        StoreCachedArtifacts(absoluteCache, fingerprint, artifacts);
        PublishArtifacts(request, artifacts, result);
        Report(request, LightingBakePhase::Complete, 1, 1, "Lighting bake complete");
        scene->Close();
        return result;
    }
} // namespace Keire
