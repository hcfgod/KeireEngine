#include "Keire/Core.h"
#include "KeireInternal/Assets/TextureImportBackend.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderStatisticsInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    template <typename T> void AppendLittleEndian(std::vector<std::byte>& output, T value)
    {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t index = 0; index < sizeof(T); ++index)
        {
            output.push_back(std::byte(value & 0xffU));
            value >>= 8U;
        }
    }

    void AppendFloat(std::vector<std::byte>& output, const float value)
    {
        AppendLittleEndian(output, std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::vector<std::byte> LegacyMeshPayload(const std::span<const Keire::MeshVertex> vertices,
                                                           const std::span<const std::uint32_t> indices,
                                                           const std::uint32_t version)
    {
        std::vector<std::byte> output;
        for (const char value : std::string_view("KEIREMSH"))
            output.push_back(std::byte(value));
        AppendLittleEndian(output, version);
        AppendLittleEndian(output, static_cast<std::uint64_t>(vertices.size()));
        AppendLittleEndian(output, static_cast<std::uint64_t>(indices.size()));
        for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F})
            AppendFloat(output, value);
        for (const auto& vertex : vertices)
        {
            for (const float value :
                 {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, vertex.Normal.X, vertex.Normal.Y,
                  vertex.Normal.Z, vertex.UV0.X, vertex.UV0.Y, vertex.VertexColor.Red, vertex.VertexColor.Green,
                  vertex.VertexColor.Blue, vertex.VertexColor.Alpha})
                AppendFloat(output, value);
            if (version >= 2)
            {
                for (const float value : {vertex.Tangent.X, vertex.Tangent.Y, vertex.Tangent.Z, vertex.Tangent.W})
                    AppendFloat(output, value);
            }
        }
        for (const auto index : indices)
            AppendLittleEndian(output, index);
        return output;
    }

    struct TemporaryDirectory final
    {
        explicit TemporaryDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] std::vector<std::byte> ReadTestBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        std::vector<std::byte> result(characters.size());
        std::ranges::transform(characters, result.begin(), [](const char value) { return std::byte(value); });
        return result;
    }

    void WriteTestBytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output || (!bytes.empty() && !output.write(reinterpret_cast<const char*>(bytes.data()),
                                                        static_cast<std::streamsize>(bytes.size()))))
        {
            throw std::runtime_error("Could not write rendering test bytes.");
        }
    }

    [[nodiscard]] std::vector<std::byte> TestPng()
    {
        constexpr std::array<unsigned char, 70> source{
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
            0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
            0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0x50, 0x0f, 0x00, 0x05, 0xfe, 0x02, 0xfe,
            0xdd, 0xfd, 0x99, 0xe5, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        std::vector<std::byte> result(source.size());
        std::ranges::transform(source, result.begin(), [](const unsigned char value) { return std::byte(value); });
        return result;
    }

    [[nodiscard]] std::vector<std::byte> TestBitmap(const bool palette)
    {
        constexpr std::uint32_t Width = 16;
        constexpr std::uint32_t Height = 16;
        constexpr std::uint32_t HeaderSize = 54;
        constexpr std::uint32_t PixelBytes = Width * Height * 4U;
        std::vector<std::byte> result;
        result.reserve(HeaderSize + PixelBytes);
        result.push_back(std::byte{0x42});
        result.push_back(std::byte{0x4d});
        AppendLittleEndian(result, HeaderSize + PixelBytes);
        AppendLittleEndian(result, std::uint32_t{0});
        AppendLittleEndian(result, HeaderSize);
        AppendLittleEndian(result, std::uint32_t{40});
        AppendLittleEndian(result, Width);
        AppendLittleEndian(result, Height);
        AppendLittleEndian(result, std::uint16_t{1});
        AppendLittleEndian(result, std::uint16_t{32});
        AppendLittleEndian(result, std::uint32_t{0});
        AppendLittleEndian(result, PixelBytes);
        AppendLittleEndian(result, std::uint32_t{2835});
        AppendLittleEndian(result, std::uint32_t{2835});
        AppendLittleEndian(result, std::uint32_t{0});
        AppendLittleEndian(result, std::uint32_t{0});

        constexpr std::array paletteColors{
            std::array<std::uint8_t, 3>{16, 32, 48}, std::array<std::uint8_t, 3>{80, 96, 112},
            std::array<std::uint8_t, 3>{144, 160, 176}, std::array<std::uint8_t, 3>{208, 224, 240}};
        for (std::uint32_t y = 0; y < Height; ++y)
        {
            for (std::uint32_t x = 0; x < Width; ++x)
            {
                const auto color = palette ? paletteColors[(x / 4U + y / 4U) % paletteColors.size()]
                                           : std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(x * 16U),
                                                                         static_cast<std::uint8_t>(y * 16U),
                                                                         static_cast<std::uint8_t>((x + y) * 8U)};
                result.push_back(std::byte(color[2]));
                result.push_back(std::byte(color[1]));
                result.push_back(std::byte(color[0]));
                result.push_back(std::byte{255});
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<std::byte> TriangleGltfBuffer()
    {
        std::vector<std::byte> result;
        for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F})
            AppendFloat(result, value);
        for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F})
            AppendFloat(result, value);
        for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F})
            AppendFloat(result, value);
        AppendLittleEndian<std::uint16_t>(result, 0);
        AppendLittleEndian<std::uint16_t>(result, 1);
        AppendLittleEndian<std::uint16_t>(result, 2);
        return result;
    }

    [[nodiscard]] std::string TriangleGltf(const std::string_view bufferUri, const std::string_view imageUri = {})
    {
        std::string result = R"({"asset":{"version":"2.0"},"buffers":[{"uri":")";
        result += bufferUri;
        result +=
            R"(","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}])";
        if (!imageUri.empty())
        {
            result += R"(,"images":[{"uri":")";
            result += imageUri;
            result +=
                R"("}],"textures":[{"source":0}],"materials":[{"name":"SidecarPaint","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])";
        }
        result += R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3)";
        if (!imageUri.empty())
            result += R"(,"material":0)";
        result += R"(}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        return result;
    }

    void WriteImportedMaterialShader(const std::filesystem::path& sourceRoot, const Keire::AssetId shader)
    {
        const auto shaderRoot = sourceRoot / "Shaders";
        std::filesystem::create_directories(shaderRoot);
        {
            std::ofstream manifest(shaderRoot / "DefaultUnlit.keireshader");
            manifest << R"({"properties":[{"name":"Tint"},{"name":"MainTexture"}]})";
        }
        {
            std::ofstream metadata(shaderRoot / "DefaultUnlit.keireshader.keiremeta");
            metadata << "{\"id\":\"" << shader.ToString() << "\"}";
        }
    }
} // namespace

TEST_CASE("built-in shader resource counts match each stage")
{
    CHECK(Keire::Detail::BuiltinShaderUniformBufferCount(true) == 3);
    CHECK(Keire::Detail::BuiltinShaderUniformBufferCount(false) == 2);
}

TEST_CASE("render CPU preparation timing aggregates every surface submitted in a frame")
{
    Keire::RenderBackend::CpuPreparationTracker tracker;
    tracker.BeginFrame();

    tracker.Accumulate(1.25F);
    tracker.Accumulate(2.5F);
    tracker.Accumulate(0.75F);

    CHECK(tracker.CurrentMilliseconds() == doctest::Approx(4.5F));
}

TEST_CASE("render CPU preparation timing resets at the next frame boundary")
{
    Keire::RenderBackend::CpuPreparationTracker tracker;
    tracker.BeginFrame();
    tracker.Accumulate(4.0F);
    tracker.EndFrame();

    CHECK(tracker.CompletedMilliseconds() == doctest::Approx(4.0F));
    CHECK(tracker.P95Milliseconds() == doctest::Approx(4.0F));

    tracker.BeginFrame();

    CHECK(tracker.CurrentMilliseconds() == doctest::Approx(0.0F));
    CHECK(tracker.P95Milliseconds() == doctest::Approx(4.0F));

    tracker.Accumulate(1.5F);
    CHECK(tracker.CurrentMilliseconds() == doctest::Approx(1.5F));
    CHECK(tracker.CompletedMilliseconds() == doctest::Approx(4.0F));
    CHECK(tracker.P95Milliseconds() == doctest::Approx(4.0F));
}

TEST_CASE("render CPU preparation publishes only complete frame aggregates")
{
    Keire::RenderBackend::CpuPreparationTracker tracker;
    tracker.BeginFrame();

    tracker.Accumulate(1.25F);
    CHECK(tracker.CompletedMilliseconds() == doctest::Approx(0.0F));
    tracker.Accumulate(2.5F);
    CHECK(tracker.CompletedMilliseconds() == doctest::Approx(0.0F));

    tracker.EndFrame();
    CHECK(tracker.CompletedMilliseconds() == doctest::Approx(3.75F));
    CHECK(tracker.P95Milliseconds() == doctest::Approx(3.75F));
}

TEST_CASE("editor camera navigation matches scene-view gesture semantics")
{
    Keire::Detail::EditorCameraController camera;
    const auto initial = camera.State();

    Keire::Detail::EditorCameraInput orbit;
    orbit.Orbit = true;
    orbit.PointerDelta = {20.0F, -10.0F};
    CHECK(camera.Update(orbit));
    CHECK(camera.State().YawDegrees > initial.YawDegrees);
    CHECK(camera.State().PitchDegrees != doctest::Approx(initial.PitchDegrees));

    const auto beforePan = camera.State().Focus;
    Keire::Detail::EditorCameraInput pan;
    pan.Pan = true;
    pan.PointerDelta = {15.0F, 8.0F};
    CHECK(camera.Update(pan));
    CHECK(camera.State().Focus != beforePan);

    const float beforeDistance = camera.State().Distance;
    Keire::Detail::EditorCameraInput zoom;
    zoom.Wheel = 1.0F;
    CHECK(camera.Update(zoom));
    CHECK(camera.State().Distance < beforeDistance);

    const float beforeSpeed = camera.State().MoveSpeed;
    Keire::Detail::EditorCameraInput flySpeed;
    flySpeed.Fly = true;
    flySpeed.Wheel = 1.0F;
    CHECK(camera.Update(flySpeed));
    CHECK(camera.State().MoveSpeed > beforeSpeed);

    const auto beforeWalk = camera.State().Focus;
    Keire::Detail::EditorCameraInput walk;
    walk.DeltaSeconds = 0.1F;
    walk.MoveForward = 1.0F;
    walk.MoveRight = -1.0F;
    walk.Fast = true;
    CHECK(camera.Update(walk));
    CHECK(camera.State().Focus != beforeWalk);
}

TEST_CASE("editor camera navigation capture is independent of panel focus")
{
    using Keire::Detail::EditorCameraNavigationHeld;
    using Keire::Detail::EditorCameraNavigationMode;
    using Keire::Detail::ResolveEditorCameraNavigation;

    CHECK(ResolveEditorCameraNavigation(true, {.Left = true}) == EditorCameraNavigationMode::Orbit);
    CHECK(ResolveEditorCameraNavigation(false, {.Middle = true}) == EditorCameraNavigationMode::Pan);
    CHECK(ResolveEditorCameraNavigation(true, {.Right = true}) == EditorCameraNavigationMode::Zoom);
    CHECK(ResolveEditorCameraNavigation(false, {.Right = true}) == EditorCameraNavigationMode::Fly);
    CHECK(ResolveEditorCameraNavigation(false, {}) == EditorCameraNavigationMode::None);

    CHECK(EditorCameraNavigationHeld(EditorCameraNavigationMode::Orbit, {.Left = true}));
    CHECK(EditorCameraNavigationHeld(EditorCameraNavigationMode::Pan, {.Middle = true}));
    CHECK(EditorCameraNavigationHeld(EditorCameraNavigationMode::Zoom, {.Right = true}));
    CHECK(EditorCameraNavigationHeld(EditorCameraNavigationMode::Fly, {.Right = true}));
    CHECK_FALSE(EditorCameraNavigationHeld(EditorCameraNavigationMode::Fly, {}));
}

TEST_CASE("editor camera pointer wrapping moves only the cursor to the opposite viewport edge")
{
    const Keire::Vector2 minimum{100.0F, 50.0F};
    const Keire::Vector2 maximum{500.0F, 350.0F};

    const auto right = Keire::Detail::ResolveEditorCameraPointerWrap({500.0F, 200.0F}, minimum, maximum);
    REQUIRE(right.Wrapped);
    CHECK(right.Position.X == doctest::Approx(102.0F));
    CHECK(right.Position.Y == doctest::Approx(200.0F));

    const auto top = Keire::Detail::ResolveEditorCameraPointerWrap({250.0F, 49.0F}, minimum, maximum);
    REQUIRE(top.Wrapped);
    CHECK(top.Position.X == doctest::Approx(250.0F));
    CHECK(top.Position.Y == doctest::Approx(348.0F));

    const auto center = Keire::Detail::ResolveEditorCameraPointerWrap({250.0F, 200.0F}, minimum, maximum);
    CHECK_FALSE(center.Wrapped);
    CHECK(center.Position == (Keire::Vector2{250.0F, 200.0F}));
}

TEST_CASE("editor camera framing projection and orientation are deterministic")
{
    Keire::Detail::EditorCameraController camera;
    camera.Frame({4.0F, 2.0F, -3.0F}, 2.0F);
    CHECK(camera.State().Focus == (Keire::Vector3{4.0F, 2.0F, -3.0F}));
    CHECK(camera.State().Distance == doctest::Approx(5.0F));
    CHECK(camera.State().OrthographicSize == doctest::Approx(5.0F));

    const float landscapeDistance = camera.State().Distance;
    camera.Frame({4.0F, 2.0F, -3.0F}, 2.0F, 60.0F, 0.5F);
    CHECK(camera.State().Distance > landscapeDistance);
    CHECK_THROWS_AS(camera.Frame({}, 1.0F, 60.0F, 0.0F), std::invalid_argument);

    const auto perspective = camera.ProjectionMatrix(16.0F / 9.0F);
    camera.ToggleProjection();
    const auto orthographic = camera.ProjectionMatrix(16.0F / 9.0F);
    CHECK(perspective != orthographic);

    camera.Snap(Keire::Detail::EditorCameraAxis::PositiveX);
    CHECK(camera.State().YawDegrees == doctest::Approx(-90.0F));
    CHECK(camera.State().PitchDegrees == doctest::Approx(0.0F));
    CHECK(Keire::Math::IsFinite(camera.ViewMatrix()));
}

TEST_CASE("shader assets preserve deterministic variants and target cooking")
{
    Keire::ShaderAssetDefinition definition;
    definition.Source = "Assets/Shaders/Test.hlsl";
    definition.ReceivesShadows = true;
    definition.Topology = Keire::ShaderPrimitiveTopology::PointList;
    definition.Properties.push_back({"Tint", Keire::ShaderPropertyType::Color, {1.0F, 0.5F, 0.25F, 1.0F}});
    const auto defaultTexture = Keire::AssetId::Parse("11111111-2222-4333-8444-555555555555");
    definition.Properties.push_back({"MainTexture", Keire::ShaderPropertyType::Texture2D, {}, defaultTexture});
    definition.Properties.back().DisplayName = "Base Color Texture";
    definition.Properties.back().Category = "Surface";
    definition.Properties.back().TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    definition.Properties.push_back({"MetallicTexture", Keire::ShaderPropertyType::Texture2D});
    definition.Properties.back().TextureSemantic = Keire::ShaderTextureSemantic::Metallic;
    definition.Properties.push_back({"RoughnessTexture", Keire::ShaderPropertyType::Texture2D});
    definition.Properties.back().TextureSemantic = Keire::ShaderTextureSemantic::Roughness;
    const std::array formats{Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV,
                             Keire::ShaderBinaryFormat::Msl};
    for (std::size_t index = 0; index < formats.size(); ++index)
    {
        definition.Variants.push_back({formats[index],
                                       {std::byte{static_cast<unsigned char>(index + 1)}},
                                       {std::byte{static_cast<unsigned char>(index + 11)}}});
    }

    const auto encoded = Keire::ShaderAsset::Encode(definition);
    const auto decoded = Keire::ShaderAsset::Decode(encoded);
    REQUIRE(decoded->Definition().Variants.size() == 3);
    CHECK(decoded->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(decoded->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(decoded->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
    CHECK(decoded->Definition().Properties[1].DefaultTexture == defaultTexture);
    CHECK(decoded->Definition().Properties[1].DisplayName == "Base Color Texture");
    CHECK(decoded->Definition().Properties[1].TextureSemantic == Keire::ShaderTextureSemantic::BaseColor);
    CHECK(decoded->Definition().Properties[2].TextureSemantic == Keire::ShaderTextureSemantic::Metallic);
    CHECK(decoded->Definition().Properties[3].TextureSemantic == Keire::ShaderTextureSemantic::Roughness);
    CHECK(decoded->Definition().ReceivesShadows);
    CHECK(decoded->Definition().Topology == Keire::ShaderPrimitiveTopology::PointList);
    REQUIRE(decoded->Definition().MaximumWorldPositionDisplacementRadius);
    CHECK(*decoded->Definition().MaximumWorldPositionDisplacementRadius == doctest::Approx(0.0F));
    CHECK(Keire::ShaderAsset::Encode(decoded->Definition()) == encoded);

    std::vector<std::uint8_t> canonicalBytes(encoded.size());
    std::ranges::transform(encoded, canonicalBytes.begin(),
                           [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
    auto legacyCanonical = nlohmann::json::from_cbor(canonicalBytes);
    legacyCanonical["schemaVersion"] = 1;
    legacyCanonical.erase("maximumWorldPositionDisplacementRadius");
    legacyCanonical["occlusionSupport"] = 3U;
    const auto legacyUnsigned = nlohmann::json::to_cbor(legacyCanonical);
    std::vector<std::byte> legacyBytes(legacyUnsigned.size());
    std::ranges::transform(legacyUnsigned, legacyBytes.begin(),
                           [](const std::uint8_t value) { return std::byte(value); });
    const auto legacyDecoded = Keire::ShaderAsset::Decode(legacyBytes);
    CHECK(legacyDecoded->Definition().SchemaVersion == 2U);
    CHECK_FALSE(legacyDecoded->Definition().MaximumWorldPositionDisplacementRadius);
    CHECK(legacyDecoded->Definition().OcclusionSupport == Keire::ShaderOcclusionSupport::None);
    const auto migratedDecoded = Keire::ShaderAsset::Decode(Keire::ShaderAsset::Encode(legacyDecoded->Definition()));
    CHECK_FALSE(migratedDecoded->Definition().MaximumWorldPositionDisplacementRadius);

    auto unknownDisplacement = definition;
    unknownDisplacement.MaximumWorldPositionDisplacementRadius.reset();
    unknownDisplacement.OcclusionSupport = Keire::ShaderOcclusionSupport::ConservativeBounds;
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(unknownDisplacement), std::invalid_argument);
    auto invalidDisplacement = definition;
    invalidDisplacement.MaximumWorldPositionDisplacementRadius = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(invalidDisplacement), std::invalid_argument);
    auto displacedOccluder = definition;
    displacedOccluder.MaximumWorldPositionDisplacementRadius = 2.0F;
    displacedOccluder.OcclusionSupport =
        Keire::ShaderOcclusionSupport::ConservativeBounds | Keire::ShaderOcclusionSupport::DepthOnlyGeometryMatch;
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(displacedOccluder), std::invalid_argument);
    displacedOccluder.OcclusionSupport = Keire::ShaderOcclusionSupport::ConservativeBounds;
    CHECK_NOTHROW((void)Keire::ShaderAsset::Encode(displacedOccluder));

    constexpr std::string_view pointManifest =
        R"({"schemaVersion":1,"source":"Points.hlsl","stages":{"vertex":"VSMain","fragment":"PSMain"},"renderState":{"topology":"PointList"}})";
    std::vector<std::byte> pointManifestBytes(pointManifest.size());
    std::ranges::transform(pointManifest, pointManifestBytes.begin(),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    const auto decodedLegacyManifest = Keire::ShaderAsset::DecodeManifest(pointManifestBytes);
    CHECK(decodedLegacyManifest.Topology == Keire::ShaderPrimitiveTopology::PointList);
    CHECK_FALSE(decodedLegacyManifest.MaximumWorldPositionDisplacementRadius);
    CHECK(decodedLegacyManifest.OcclusionSupport == Keire::ShaderOcclusionSupport::None);

    const auto importer = Keire::CreateShaderAssetImporter();
    CHECK(importer.Version == 5U);
    REQUIRE(importer.Cook);
    Keire::ShaderImporterSpecification missingReflection;
    missingReflection.Formats = {Keire::ShaderBinaryFormat::Dxil};
    CHECK_THROWS_AS((void)Keire::CreateShaderAssetImporter(std::move(missingReflection)), std::invalid_argument);
    Keire::ShaderImporterSpecification duplicateFormat;
    duplicateFormat.Formats = {Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::SpirV};
    CHECK_THROWS_AS((void)Keire::CreateShaderAssetImporter(std::move(duplicateFormat)), std::invalid_argument);
    const auto windows = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::Windows));
    REQUIRE(windows->Definition().Variants.size() == 2);
    CHECK(windows->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(windows->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    const auto linux = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::Linux));
    REQUIRE(linux->Definition().Variants.size() == 1);
    CHECK(linux->Definition().Variants.front().Format == Keire::ShaderBinaryFormat::SpirV);
    const auto macOS = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::MacOS));
    REQUIRE(macOS->Definition().Variants.size() == 1);
    CHECK(macOS->Definition().Variants.front().Format == Keire::ShaderBinaryFormat::Msl);

    auto excessive = definition;
    for (std::size_t index = 0; index < 16; ++index)
        excessive.Properties.push_back(
            {"Texture" + std::to_string(index), Keire::ShaderPropertyType::Texture2D, {}, {}});
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(excessive), std::invalid_argument);
}

TEST_CASE("material and built-in mesh assets retain Kéire-owned identities")
{
    Keire::MaterialAssetDefinition definition;
    definition.Shader = Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000001");
    definition.Properties.emplace("Roughness", 0.5F);
    definition.Properties.emplace("Tint", Keire::Color{0.25F, 0.5F, 1.0F, 1.0F});
    const auto texture = Keire::AssetId::Parse("11111111-2222-4333-8444-555555555555");
    definition.SetTexture("MainTexture", texture);
    definition.Surface.AlphaMode = Keire::MaterialAlphaMode::Mask;
    definition.Surface.AlphaCutoff = 0.35F;
    definition.Surface.DoubleSided = true;
    definition.ContributeEmissionToGI = false;
    definition.EmissiveGIIntensity = 2.5F;
    CHECK(definition.Texture("MainTexture") == texture);
    CHECK_FALSE(definition.Texture("Missing"));

    const auto decoded = Keire::MaterialAsset::Decode(Keire::MaterialAsset::Encode(definition));
    CHECK(decoded->Definition().Shader == definition.Shader);
    CHECK(decoded->Definition().Properties.size() == 3);
    CHECK(std::get<Keire::AssetId>(decoded->Definition().Properties.at("MainTexture")) == texture);
    CHECK(decoded->Definition().Surface == definition.Surface);
    CHECK_FALSE(decoded->Definition().ContributeEmissionToGI);
    CHECK(decoded->Definition().EmissiveGIIntensity == doctest::Approx(2.5F));
    const auto sourceDecoded = Keire::MaterialAsset::DecodeSource(Keire::MaterialAsset::EncodeSource(definition));
    CHECK(sourceDecoded.Shader == definition.Shader);
    CHECK(sourceDecoded.Texture("MainTexture") == texture);
    CHECK_FALSE(sourceDecoded.ContributeEmissionToGI);
    CHECK(sourceDecoded.EmissiveGIIntensity == doctest::Approx(2.5F));
    auto mutableDefinition = sourceDecoded;
    CHECK(mutableDefinition.RemoveTexture("MainTexture"));
    CHECK_FALSE(mutableDefinition.RemoveTexture("MainTexture"));
    CHECK(Keire::MeshAsset::Cube()->Mesh() == Keire::BuiltinMesh::Cube);
    CHECK(Keire::MeshAsset::CubeId() != Keire::MeshAsset::ErrorId());
    CHECK(Keire::MaterialAsset::Error()->Definition().Properties.contains("ErrorColor"));
}

TEST_CASE("material alpha modes retain stable binary and authoring identities")
{
    constexpr std::array modes{Keire::MaterialAlphaMode::Opaque,      Keire::MaterialAlphaMode::Mask,
                               Keire::MaterialAlphaMode::Blend,       Keire::MaterialAlphaMode::Additive,
                               Keire::MaterialAlphaMode::Modulate,    Keire::MaterialAlphaMode::AlphaComposite,
                               Keire::MaterialAlphaMode::AlphaHoldout};
    for (const auto mode : modes)
    {
        Keire::MaterialAssetDefinition definition;
        definition.Surface.AlphaMode = mode;
        const auto runtime = Keire::MaterialAsset::Decode(Keire::MaterialAsset::Encode(definition));
        REQUIRE(runtime);
        CHECK(runtime->Definition().Surface.AlphaMode == mode);
        CHECK(Keire::MaterialAsset::DecodeSource(Keire::MaterialAsset::EncodeSource(definition)).Surface.AlphaMode ==
              mode);
    }
}

TEST_CASE("material overrides are validated against shader declarations")
{
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Assets/Shaders/Surface.hlsl";
    Keire::ShaderPropertyDefinition roughness{"Roughness", Keire::ShaderPropertyType::Scalar, {1.0F, 0.0F, 0.0F, 0.0F}};
    roughness.Minimum = 0.0F;
    roughness.Maximum = 1.0F;
    shader.Properties.push_back(roughness);
    Keire::ShaderPropertyDefinition normal{"NormalTexture", Keire::ShaderPropertyType::Texture2D};
    normal.TextureSemantic = Keire::ShaderTextureSemantic::Normal;
    shader.Properties.push_back(normal);

    Keire::MaterialAssetDefinition material;
    material.Properties.emplace("Roughness", 0.5F);
    material.SetTexture("NormalTexture", {});
    CHECK_NOTHROW(Keire::ValidateMaterialAgainstShader(material, shader));
    material.Properties["Roughness"] = 1.5F;
    CHECK_THROWS_AS(Keire::ValidateMaterialAgainstShader(material, shader), std::invalid_argument);
    material.Properties["Roughness"] = Keire::Color{};
    CHECK_THROWS_AS(Keire::ValidateMaterialAgainstShader(material, shader), std::invalid_argument);
    material.Properties.erase("Roughness");
    material.Properties.emplace("Unknown", 1.0F);
    CHECK_THROWS_AS(Keire::ValidateMaterialAgainstShader(material, shader), std::invalid_argument);
}

TEST_CASE("sandbox monster material binds dedicated color normal metallic and roughness sources")
{
    const auto source = ReadTestBytes("Samples/KeireSandbox/Assets/Materials/Monster1.keirematerial");
    const auto definition = Keire::MaterialAsset::DecodeSource(source);
    const auto baseColor = Keire::AssetId::Parse("38760a1d-9dfa-4bbc-8ba1-50921ae9d748");
    const auto normal = Keire::AssetId::Parse("0a8ba309-c28a-4842-8949-09ff8c60a1fa");
    const auto metallic = Keire::AssetId::Parse("cda3b77a-3982-42ef-8f7c-b6f8730e8eda");
    const auto roughness = Keire::AssetId::Parse("0b904e85-216d-4c6d-889b-2cee89089b02");
    CHECK(definition.Texture("MainTexture") == baseColor);
    CHECK(definition.Texture("NormalTexture") == normal);
    CHECK(definition.Texture("MetallicTexture") == metallic);
    CHECK(definition.Texture("RoughnessTexture") == roughness);
    const auto packed = definition.Texture("MetallicRoughnessTexture");
    CHECK((!packed || !*packed));
    const auto shader = Keire::ShaderAsset::DecodeManifest(
        ReadTestBytes("Samples/KeireSandbox/Assets/Shaders/DefaultUnlit.keireshader"));
    CHECK_NOTHROW(Keire::ValidateMaterialAgainstShader(definition, shader));

    const auto importer = Keire::CreateMaterialAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport({}, source);
    CHECK(std::ranges::find(imported.AssetDependencies, definition.Shader) != imported.AssetDependencies.end());
    for (const auto texture : std::array{baseColor, normal, metallic, roughness})
        CHECK(std::ranges::find(imported.AssetDependencies, texture) != imported.AssetDependencies.end());
}

TEST_CASE("scene and material importers extract transitive render dependencies")
{
    const auto shader = Keire::AssetId::Parse("11111111-1111-4111-8111-111111111111");
    const auto material = Keire::AssetId::Parse("22222222-2222-4222-8222-222222222222");
    const auto texture = Keire::AssetId::Parse("33333333-3333-4333-8333-333333333333");
    const std::string materialSource = "{\"schemaVersion\":1,\"shader\":\"" + shader.ToString() +
                                       "\",\"properties\":{\"MainTexture\":\"" + texture.ToString() + "\"}}";
    const auto materialBytes = std::as_bytes(std::span(materialSource.data(), materialSource.size()));
    const auto materialImporter = Keire::CreateMaterialAssetImporter();
    REQUIRE(materialImporter.ContextualImport);
    const auto materialOutput = materialImporter.ContextualImport({}, materialBytes);
    CHECK(materialOutput.AssetDependencies == std::vector<Keire::AssetId>{shader, texture});

    const auto sceneBytes = Keire::SceneAsset::Encode(Keire::SceneAsset::SampleDefinition(material));
    const auto sceneImporter = Keire::CreateSceneAssetImporter();
    REQUIRE(sceneImporter.ContextualImport);
    const auto sceneOutput = sceneImporter.ContextualImport({}, sceneBytes);
    CHECK(sceneOutput.AssetDependencies == std::vector<Keire::AssetId>{material});
}

TEST_CASE("mesh assets validate and preserve production vertex data")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}, {}},
                              Keire::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}, {}},
                              Keire::MeshVertex{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}, {}}};
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    const auto encoded = Keire::MeshAsset::Encode(vertices, indices);
    const auto decoded = Keire::MeshAsset::Decode(encoded);
    REQUIRE(decoded->Vertices().size() == 3);
    REQUIRE(decoded->Indices().size() == 3);
    CHECK(decoded->Vertices()[1].UV0 == (Keire::Vector2{1.0F, 0.0F}));
    CHECK(decoded->Bounds().Minimum == (Keire::Vector3{0.0F, 0.0F, 0.0F}));
    CHECK(decoded->Bounds().Maximum == (Keire::Vector3{1.0F, 1.0F, 0.0F}));
    REQUIRE(decoded->Submeshes().size() == 1);
    REQUIRE(decoded->MaterialSlots().size() == 1);
    REQUIRE(decoded->Lods().size() == 1);

    auto truncated = encoded;
    truncated.pop_back();
    CHECK_THROWS_AS((void)Keire::MeshAsset::Decode(truncated), std::invalid_argument);
    auto outOfRange = indices;
    outOfRange[2] = 3;
    CHECK_THROWS_AS((void)Keire::MeshAsset::Encode(vertices, outOfRange), std::invalid_argument);
    auto nonFinite = vertices;
    nonFinite[0].Position.X = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS((void)Keire::MeshAsset::Encode(nonFinite, indices), std::invalid_argument);

    const auto cube = Keire::MeshAsset::Cube();
    const auto error = Keire::MeshAsset::Error();
    CHECK(cube->Vertices().size() == 24);
    CHECK(cube->Indices().size() == 36);
    CHECK(cube->Vertices().front().Tangent == (Keire::Vector4{1.0F, 0.0F, 0.0F, 1.0F}));
    CHECK(error->Vertices().front().VertexColor == (Keire::Color{1.0F, 0.0F, 1.0F, 1.0F}));
}

TEST_CASE("built-in mesh catalog has stable identities and production geometry conventions")
{
    const auto catalog = Keire::BuiltinMeshCatalog();
    REQUIRE(catalog.size() == 8);
    for (const auto& descriptor : catalog)
    {
        CAPTURE(descriptor.Name);
        CHECK(descriptor.Id);
        CHECK_FALSE(descriptor.Name.empty());
        CHECK_FALSE(descriptor.CollisionExpectation.empty());
        CHECK(Keire::MeshAsset::BuiltinId(descriptor.Mesh) == descriptor.Id);
        CHECK(Keire::MeshAsset::BuiltinKind(descriptor.Id) == descriptor.Mesh);
        CHECK(Keire::MeshAsset::IsBuiltin(descriptor.Id));
        CHECK(std::ranges::count(catalog, descriptor.Id, &Keire::BuiltinMeshDescriptor::Id) == 1);
        CHECK(std::ranges::count(catalog, descriptor.Name, &Keire::BuiltinMeshDescriptor::Name) == 1);

        const auto mesh = Keire::MeshAsset::ResolveBuiltin(descriptor.Id);
        REQUIRE(mesh);
        CHECK(mesh->Mesh() == descriptor.Mesh);
        CHECK_FALSE(mesh->Vertices().empty());
        CHECK_FALSE(mesh->Indices().empty());
        CHECK(mesh->Indices().size() % 3 == 0);
        CHECK(mesh->Bounds().Minimum.X >= -0.501F);
        CHECK(mesh->Bounds().Minimum.Y >= -0.501F);
        CHECK(mesh->Bounds().Minimum.Z >= -0.501F);
        CHECK(mesh->Bounds().Maximum.X <= 0.501F);
        CHECK(mesh->Bounds().Maximum.Y <= 0.501F);
        CHECK(mesh->Bounds().Maximum.Z <= 0.501F);
        CHECK(mesh->Bounds().Minimum.X + mesh->Bounds().Maximum.X == doctest::Approx(0.0F).epsilon(0.001));
        CHECK(mesh->Bounds().Minimum.Y + mesh->Bounds().Maximum.Y == doctest::Approx(0.0F).epsilon(0.001));
        CHECK(mesh->Bounds().Minimum.Z + mesh->Bounds().Maximum.Z == doctest::Approx(0.0F).epsilon(0.001));
        for (const auto& vertex : mesh->Vertices())
        {
            const float normalLength = std::sqrt(vertex.Normal.X * vertex.Normal.X + vertex.Normal.Y * vertex.Normal.Y +
                                                 vertex.Normal.Z * vertex.Normal.Z);
            CHECK(normalLength == doctest::Approx(1.0F).epsilon(0.001));
            CHECK(vertex.UV0.X >= 0.0F);
            CHECK(vertex.UV0.X <= 1.0F);
            CHECK(vertex.UV0.Y >= 0.0F);
            CHECK(vertex.UV0.Y <= 1.0F);
        }
        for (const auto index : mesh->Indices())
            CHECK(index < mesh->Vertices().size());
    }

    CHECK(Keire::MeshAsset::Plane()->Bounds().Minimum.Y == 0.0F);
    CHECK(Keire::MeshAsset::Plane()->Bounds().Maximum.Y == 0.0F);
    CHECK(Keire::MeshAsset::Quad()->Bounds().Minimum.Z == 0.0F);
    CHECK(Keire::MeshAsset::Quad()->Bounds().Maximum.Z == 0.0F);
    CHECK_FALSE(Keire::MeshAsset::ResolveBuiltin(Keire::AssetId::Generate()));
}

TEST_CASE("mesh version one payloads generate the same deterministic tangent frame as version two")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}, {}},
                              Keire::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}, {}},
                              Keire::MeshVertex{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}, {}}};
    constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
    const auto versionOne = LegacyMeshPayload(vertices, indices, 1);
    const auto versionTwo = LegacyMeshPayload(vertices, indices, 2);

    const auto decodedOne = Keire::MeshAsset::Decode(versionOne);
    const auto decodedTwo = Keire::MeshAsset::Decode(versionTwo);
    REQUIRE(decodedOne->Vertices().size() == decodedTwo->Vertices().size());
    for (std::size_t index = 0; index < decodedOne->Vertices().size(); ++index)
        CHECK(decodedOne->Vertices()[index].Tangent == decodedTwo->Vertices()[index].Tangent);
    CHECK(decodedOne->Submeshes().size() == 1);
    CHECK(decodedTwo->MaterialSlots().front().Name == "Default");
}

TEST_CASE("mesh version three preserves ordered submeshes material slots and LODs")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                              Keire::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                              Keire::MeshVertex{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
                              Keire::MeshVertex{{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 2, 1, 3};
    const Keire::MeshBounds bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    const std::array submeshes{Keire::MeshSubmesh{0, 3, 0, bounds}, Keire::MeshSubmesh{3, 3, 1, bounds}};
    const auto material = Keire::AssetId::Parse("11111111-2222-4333-8444-555555555555");
    const std::array slots{Keire::MeshMaterialSlot{"Body", material}, Keire::MeshMaterialSlot{"Glass", {}}};
    const std::array lods{Keire::MeshLod{0.0F, 0, 2, bounds}};
    const auto decoded = Keire::MeshAsset::Decode(Keire::MeshAsset::Encode(vertices, indices, submeshes, slots, lods));
    REQUIRE(decoded->Submeshes().size() == 2);
    CHECK(decoded->Submeshes()[1].FirstIndex == 3);
    CHECK(decoded->Submeshes()[1].MaterialSlot == 1);
    CHECK(decoded->MaterialSlots()[0].DefaultMaterial == material);
    CHECK(decoded->MaterialSlots()[1].Name == "Glass");
    CHECK(decoded->Lods()[0].SubmeshCount == 2);
}

TEST_CASE("mesh version five preserves line-list submesh topology")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{1.0F, 1.0F, 0.0F}}};
    constexpr std::array<std::uint32_t, 2> indices{0, 1};
    const Keire::MeshBounds bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    const std::array submeshes{Keire::MeshSubmesh{0, 2, 0, bounds, Keire::ShaderPrimitiveTopology::LineList}};
    const std::array slots{Keire::MeshMaterialSlot{"Lines", {}}};
    const std::array lods{Keire::MeshLod{0.0F, 0, 1, bounds}};
    const auto decoded = Keire::MeshAsset::Decode(Keire::MeshAsset::Encode(vertices, indices, submeshes, slots, lods));
    REQUIRE(decoded->Submeshes().size() == 1);
    CHECK(std::ranges::equal(decoded->Indices(), indices));
    CHECK(decoded->Submeshes().front().Topology == Keire::ShaderPrimitiveTopology::LineList);
}

TEST_CASE("mesh version five preserves point-list submesh topology")
{
    const std::array vertices{Keire::MeshVertex{{0.0F, 0.0F, 0.0F}}, Keire::MeshVertex{{1.0F, 1.0F, 0.0F}}};
    constexpr std::array<std::uint32_t, 2> indices{0, 1};
    const Keire::MeshBounds bounds{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    const std::array submeshes{Keire::MeshSubmesh{0, 2, 0, bounds, Keire::ShaderPrimitiveTopology::PointList}};
    const std::array slots{Keire::MeshMaterialSlot{"Points", {}}};
    const std::array lods{Keire::MeshLod{0.0F, 0, 1, bounds}};
    const auto decoded = Keire::MeshAsset::Decode(Keire::MeshAsset::Encode(vertices, indices, submeshes, slots, lods));
    REQUIRE(decoded->Submeshes().size() == 1);
    CHECK(std::ranges::equal(decoded->Indices(), indices));
    CHECK(decoded->Submeshes().front().Topology == Keire::ShaderPrimitiveTopology::PointList);
}

TEST_CASE("Assimp imports a deterministic static OBJ into the Kéire mesh format")
{
    TemporaryDirectory directory("MeshImportTests");
    const auto sourcePath = directory.Path / "triangle.obj";
    {
        std::ofstream source(sourcePath);
        source << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                  "vt 0 0\nvt 1 0\nvt 0 1\n"
                  "f 1/1 2/2 3/3\n";
    }
    const auto importer = Keire::CreateMeshAssetImporter();
    REQUIRE(importer.ContextualImport);
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    const auto output = importer.ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    CHECK(mesh->Vertices().size() == 3);
    CHECK(mesh->Indices().size() == 3);
    CHECK(mesh->Bounds().Maximum == (Keire::Vector3{1.0F, 1.0F, 0.0F}));
    REQUIRE(output.Metadata.LocalBounds);
    CHECK(output.Metadata.LocalBounds->Maximum == std::array{1.0F, 1.0F, 0.0F});
    const auto origin = std::ranges::find(mesh->Vertices(), Keire::Vector3{}, &Keire::MeshVertex::Position);
    REQUIRE(origin != mesh->Vertices().end());
    CHECK(origin->UV0 == (Keire::Vector2{0.0F, 1.0F}));
    CHECK(origin->Normal.Z == doctest::Approx(-1.0F));
    REQUIRE(mesh->Indices().size() == 3);
    CHECK(mesh->Indices()[0] == 2);
    CHECK(mesh->Indices()[1] == 1);
    CHECK(mesh->Indices()[2] == 0);
    CHECK(mesh->Vertices().front().Tangent.X == doctest::Approx(1.0F));
}

TEST_CASE("Assimp imports OBJ lines with actionable topology diagnostics")
{
    TemporaryDirectory directory("MeshLineImportTests");
    const auto sourcePath = directory.Path / "lines.obj";
    {
        std::ofstream source(sourcePath);
        source << "v 0 0 0\nv 1 1 0\nl 1 2\n";
    }
    const auto importer = Keire::CreateMeshAssetImporter();
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    const auto output = importer.ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    REQUIRE(mesh->Submeshes().size() == 1);
    CHECK(mesh->Indices().size() == 2);
    CHECK(mesh->Submeshes().front().Topology == Keire::ShaderPrimitiveTopology::LineList);
    CHECK(std::ranges::any_of(output.Diagnostics, [](const auto& diagnostic)
                              { return diagnostic.Message.find("LineList") != std::string::npos; }));
}

TEST_CASE("Assimp imports OBJ points with actionable topology diagnostics")
{
    TemporaryDirectory directory("MeshPointImportTests");
    const auto sourcePath = directory.Path / "points.obj";
    {
        std::ofstream source(sourcePath);
        source << "v 0 0 0\nv 1 1 0\np 1 2\n";
    }
    const auto importer = Keire::CreateMeshAssetImporter();
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    const auto output = importer.ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    REQUIRE(mesh->Submeshes().size() == 1);
    CHECK(mesh->Indices().size() == 2);
    CHECK(mesh->Submeshes().front().Topology == Keire::ShaderPrimitiveTopology::PointList);
    CHECK(std::ranges::any_of(output.Diagnostics, [](const auto& diagnostic)
                              { return diagnostic.Message.find("PointList") != std::string::npos; }));
}

TEST_CASE("Assimp partitions mixed OBJ primitives without rejecting the model")
{
    TemporaryDirectory directory("MeshMixedTopologyImportTests");
    const auto sourcePath = directory.Path / "mixed.obj";
    {
        std::ofstream source(sourcePath);
        source << "o Mixed\n"
                  "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
                  "f 1 2 3\n"
                  "l 1 4\n"
                  "p 2\n";
    }
    const auto importer = Keire::CreateMeshAssetImporter();
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    const auto output = importer.ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    CHECK(std::ranges::any_of(mesh->Submeshes(), [](const auto& submesh)
                              { return submesh.Topology == Keire::ShaderPrimitiveTopology::TriangleList; }));
    CHECK(std::ranges::any_of(mesh->Submeshes(), [](const auto& submesh)
                              { return submesh.Topology == Keire::ShaderPrimitiveTopology::LineList; }));
    CHECK(std::ranges::any_of(mesh->Submeshes(), [](const auto& submesh)
                              { return submesh.Topology == Keire::ShaderPrimitiveTopology::PointList; }));
}

TEST_CASE("Sandbox pyramid triangle winding agrees with its authored outward normals")
{
    const auto projectRoot = std::filesystem::current_path() / "Samples/KeireSandbox";
    const auto sourceRoot = projectRoot / "Assets";
    const auto sourcePath = sourceRoot / "Meshes/TexturedPyramid.obj";
    REQUIRE(std::filesystem::is_regular_file(sourcePath));

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("c0ffee00-0000-4000-8000-000000000001");
    context.ProjectRoot = projectRoot;
    context.SourceRoot = sourceRoot;
    context.SourcePath = sourcePath;
    context.RelativePath = "Meshes/TexturedPyramid.obj";
    context.ReadProjectFile = [projectRoot](const std::filesystem::path& relative)
    { return ReadTestBytes(projectRoot / relative); };
    std::unordered_map<std::string, Keire::AssetId> subAssets;
    context.ResolveSubAssetId = [&subAssets](const std::string_view key)
    { return subAssets.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto importer = Keire::CreateMeshAssetImporter();
    const auto output = importer.ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    REQUIRE(mesh->Indices().size() % 3U == 0U);
    for (std::size_t triangle = 0; triangle < mesh->Indices().size(); triangle += 3U)
    {
        REQUIRE(mesh->Indices()[triangle] < mesh->Vertices().size());
        REQUIRE(mesh->Indices()[triangle + 1U] < mesh->Vertices().size());
        REQUIRE(mesh->Indices()[triangle + 2U] < mesh->Vertices().size());
        const auto& first = mesh->Vertices()[mesh->Indices()[triangle]];
        const auto& second = mesh->Vertices()[mesh->Indices()[triangle + 1U]];
        const auto& third = mesh->Vertices()[mesh->Indices()[triangle + 2U]];
        const Keire::Vector3 firstEdge{second.Position.X - first.Position.X, second.Position.Y - first.Position.Y,
                                       second.Position.Z - first.Position.Z};
        const Keire::Vector3 secondEdge{third.Position.X - first.Position.X, third.Position.Y - first.Position.Y,
                                        third.Position.Z - first.Position.Z};
        const Keire::Vector3 faceNormal{firstEdge.Y * secondEdge.Z - firstEdge.Z * secondEdge.Y,
                                        firstEdge.Z * secondEdge.X - firstEdge.X * secondEdge.Z,
                                        firstEdge.X * secondEdge.Y - firstEdge.Y * secondEdge.X};
        const Keire::Vector3 authoredNormal{first.Normal.X + second.Normal.X + third.Normal.X,
                                            first.Normal.Y + second.Normal.Y + third.Normal.Y,
                                            first.Normal.Z + second.Normal.Z + third.Normal.Z};
        const float agreement =
            faceNormal.X * authoredNormal.X + faceNormal.Y * authoredNormal.Y + faceNormal.Z * authoredNormal.Z;
        CHECK(agreement > 0.0F);
    }
}

TEST_CASE("model importer exposes explicit animation source routing")
{
    const auto importer = Keire::CreateMeshAssetImporter();
    CHECK(importer.Version == 20);
    const auto content =
        std::ranges::find(importer.ImportOptions, std::string("contentType"), &Keire::AssetImportOptionDescriptor::Key);
    REQUIRE(content != importer.ImportOptions.end());
    CHECK(content->Kind == Keire::AssetImportOptionKind::Choice);
    CHECK(std::get<std::string>(content->DefaultValue) == "model");
    CHECK(content->Choices == std::vector<std::string>{"model", "animation"});
    const auto materials = std::ranges::find(importer.ImportOptions, std::string("materialImport"),
                                             &Keire::AssetImportOptionDescriptor::Key);
    REQUIRE(materials != importer.ImportOptions.end());
    CHECK(materials->Group == "Materials");
    CHECK(std::get<std::string>(materials->DefaultValue) == "embedded");
    CHECK(materials->Choices == std::vector<std::string>{"embedded", "none"});
    const auto compression = std::ranges::find(importer.ImportOptions, std::string("animationCompression"),
                                               &Keire::AssetImportOptionDescriptor::Key);
    REQUIRE(compression != importer.ImportOptions.end());
    CHECK(compression->Group == "Animation");
    CHECK(std::get<std::string>(compression->DefaultValue) == "balanced");
    CHECK(compression->Choices == std::vector<std::string>{"none", "light", "balanced", "aggressive"});
    const auto motion = std::ranges::find(importer.ImportOptions, std::string("animationMotion"),
                                          &Keire::AssetImportOptionDescriptor::Key);
    REQUIRE(motion != importer.ImportOptions.end());
    CHECK(motion->Group == "Animation");
    CHECK(std::get<std::string>(motion->DefaultValue) == "rootMotion");
    CHECK(motion->Choices == std::vector<std::string>{"rootMotion", "authored", "inPlaceHorizontal", "inPlace"});
}

TEST_CASE("FBX model identities keep duplicate bone names deterministic across nodes weights and animation")
{
    auto bytes = ReadTestBytes("Vendor/assimp/test/models/FBX/animation_with_skeleton.fbx");
    REQUIRE_FALSE(bytes.empty());
    constexpr std::string_view SourceName = "Bone.002";
    constexpr std::string_view DuplicateName = "Bone.001";
    static_assert(SourceName.size() == DuplicateName.size());
    const auto sourcePattern = std::as_bytes(std::span(SourceName.data(), SourceName.size()));
    const auto duplicatePattern = std::as_bytes(std::span(DuplicateName.data(), DuplicateName.size()));
    std::size_t replacements = 0;
    for (auto found = std::search(bytes.begin(), bytes.end(), sourcePattern.begin(), sourcePattern.end());
         found != bytes.end(); found = std::search(found, bytes.end(), sourcePattern.begin(), sourcePattern.end()))
    {
        std::copy(duplicatePattern.begin(), duplicatePattern.end(), found);
        ++replacements;
        found += static_cast<std::ptrdiff_t>(sourcePattern.size());
    }
    REQUIRE(replacements >= 2);

    TemporaryDirectory directory("DuplicateFbxBoneIdentityTests");
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("12345678-1234-4567-89ab-100000000001");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = directory.Path / "duplicate-bones.fbx";
    context.RelativePath = context.SourcePath.filename();
    context.ImportSettings["materialImport"] = std::string("none");
    context.ImportSettings["rigSource"] = std::string("embedded");
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto importer = Keire::CreateMeshAssetImporter();
    const auto first = importer.ContextualImport(context, bytes);
    const auto second = importer.ContextualImport(context, bytes);
    CHECK(first.Bytes == second.Bytes);
    REQUIRE(first.SubAssets.size() == second.SubAssets.size());
    for (std::size_t index = 0; index < first.SubAssets.size(); ++index)
    {
        CHECK(first.SubAssets[index].Id == second.SubAssets[index].Id);
        CHECK(first.SubAssets[index].Type == second.SubAssets[index].Type);
        CHECK(first.SubAssets[index].Key == second.SubAssets[index].Key);
        CHECK(first.SubAssets[index].Bytes == second.SubAssets[index].Bytes);
    }

    const auto skeletonOutput =
        std::ranges::find(first.SubAssets, Keire::SkeletonAsset::StaticType(), &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(skeletonOutput != first.SubAssets.end());
    const auto skeleton = Keire::SkeletonAsset::Decode(skeletonOutput->Bytes);
    std::vector<std::string> duplicateBoneNames;
    std::vector<std::uint32_t> duplicateBoneIndices;
    for (std::size_t index = 0; index < skeleton->Bones().size(); ++index)
    {
        const auto& bone = skeleton->Bones()[index];
        if (!bone.Name.starts_with("Bone.001_FBX_"))
            continue;
        duplicateBoneNames.push_back(bone.Name);
        duplicateBoneIndices.push_back(static_cast<std::uint32_t>(index));
    }
    REQUIRE(duplicateBoneNames.size() == 2);
    CHECK(duplicateBoneNames[0] != duplicateBoneNames[1]);

    std::size_t animationClipCount = 0;
    std::unordered_set<std::uint32_t> animatedBones;
    for (const auto& subAsset : first.SubAssets)
    {
        if (subAsset.Type != Keire::AnimationClipAsset::StaticType())
            continue;
        ++animationClipCount;
        const auto clip = Keire::AnimationClipAsset::Decode(subAsset.Bytes);
        for (const auto& track : clip->Tracks())
            animatedBones.insert(track.Bone);
    }
    REQUIRE(animationClipCount > 0);
    REQUIRE(duplicateBoneIndices.size() == 2);
    CHECK(animatedBones.contains(duplicateBoneIndices[0]));
    CHECK(animatedBones.contains(duplicateBoneIndices[1]));

    std::vector<Keire::SkeletonBone> invalidBones(skeleton->Bones().begin(), skeleton->Bones().end());
    invalidBones.front().BindPose.Translation.X = std::numeric_limits<float>::infinity();
    CHECK_THROWS_WITH_AS((void)Keire::SkeletonAsset::Encode(invalidBones),
                         "Skeleton contains an invalid bone hierarchy or transform.", std::invalid_argument);
}

TEST_CASE("model importer version twenty publishes complete skinned influence bounds")
{
    TemporaryDirectory directory("SkinnedBoundsImportTests");
    const auto sourcePath = directory.Path / "character.obj";
    {
        std::ofstream source(sourcePath);
        source << "v -1 0 -1\nv 1 0 -1\nv -1 2 -1\nv 1 2 -1\n"
                  "v -1 0 1\nv 1 0 1\nv -1 2 1\nv 1 2 1\n"
                  "f 1 2 3\nf 2 4 3\nf 5 7 6\nf 6 7 8\n";
    }
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    context.ImportSettings["rigSource"] = std::string("generate");
    std::unordered_map<std::string, Keire::AssetId> subAssetIds;
    context.ResolveSubAssetId = [&subAssetIds](const std::string_view key)
    { return subAssetIds.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto output = Keire::CreateMeshAssetImporter().ContextualImport(context, ReadTestBytes(sourcePath));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    const auto skinnedSubAsset = std::ranges::find(output.SubAssets, Keire::SkinnedMeshAsset::StaticType(),
                                                   &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(skinnedSubAsset != output.SubAssets.end());
    const auto skinned = Keire::SkinnedMeshAsset::Decode(skinnedSubAsset->Bytes);
    CHECK(skinned->HasCompleteInfluenceBounds());
    CHECK(skinned->InfluenceBoundsSubmeshCount() == mesh->Submeshes().size());
    CHECK_FALSE(skinned->InfluenceBounds().empty());
    for (std::uint32_t submesh = 0; submesh < mesh->Submeshes().size(); ++submesh)
        CHECK(std::ranges::any_of(skinned->InfluenceBounds(), [submesh](const Keire::SkinInfluenceBounds& bounds)
                                  { return bounds.Submesh == submesh; }));
}

TEST_CASE("animation source import can bake semantic pelvis translation in place")
{
    TemporaryDirectory directory("AnimationMotionImportTests");
    const auto sourcePath = directory.Path / "motion.gltf";
    const std::string gltf = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAgD8AAIA/AAAAQAAAQEAAAIBAAADAQAAAAEE=","byteLength":32}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}],
        "nodes":[{"name":"Hips"}],
        "animations":[{"name":"Move","samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}],
        "scenes":[{"nodes":[0]}],
        "scene":0
    })";
    {
        std::ofstream source(sourcePath);
        source << gltf;
    }

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("12345678-1234-4567-89ab-123456789abc");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    context.ImportSettings["contentType"] = std::string("animation");
    context.ImportSettings["animationMotion"] = std::string("inPlace");
    std::size_t sidecarReads = 0;
    context.ReadProjectFile = [&sidecarReads](const std::filesystem::path&) -> std::vector<std::byte>
    {
        ++sidecarReads;
        throw std::runtime_error("Data URI import attempted a project-file read.");
    };
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto output = Keire::CreateMeshAssetImporter().ContextualImport(context, std::as_bytes(std::span(gltf)));
    const auto clipOutput = std::ranges::find(output.SubAssets, Keire::AnimationClipAsset::StaticType(),
                                              &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(clipOutput != output.SubAssets.end());
    const auto clip = Keire::AnimationClipAsset::Decode(clipOutput->Bytes);
    CHECK_FALSE(clip->RootMotion());
    REQUIRE(clip->Tracks().size() == 1);
    REQUIRE(clip->Tracks().front().Keys.size() == 2);
    CHECK(clip->Tracks().front().Keys.front().Value.Translation ==
          clip->Tracks().front().Keys.back().Value.Translation);
    CHECK(sidecarReads == 0);
    CHECK(std::ranges::any_of(output.Diagnostics, [](const auto& diagnostic)
                              { return diagnostic.Message.find("Baked animation") != std::string::npos; }));
}

TEST_CASE("glTF import publishes faithful material and embedded texture subassets")
{
    TemporaryDirectory directory("GltfMaterialImportTests");
    const auto shaderRoot = directory.Path / "Shaders";
    std::filesystem::create_directories(shaderRoot);
    const auto shaderId = Keire::AssetId::Parse("11aa22bb-33cc-44dd-8eee-ff0011223344");
    {
        std::ofstream shader(shaderRoot / "DefaultUnlit.keireshader");
        shader
            << R"({"properties":[{"name":"Tint"},{"name":"MainTexture"},{"name":"MetallicFactor"},{"name":"RoughnessFactor"},{"name":"NormalScale"},{"name":"NormalTexture"}]})";
        std::ofstream metadata(shaderRoot / "DefaultUnlit.keireshader.keiremeta");
        metadata << "{\"id\":\"" << shaderId.ToString() << "\"}";
    }
    const auto sourcePath = directory.Path / "material.gltf";
    const std::string gltf = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA=","byteLength":104}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
        "images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAF/gL+3f2Z5QAAAABJRU5ErkJggg=="}],
        "textures":[{"source":0}],
        "materials":[{"name":"Paint","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.6,0.8],"baseColorTexture":{"index":0},"metallicFactor":0.3,"roughnessFactor":0.7},"normalTexture":{"index":0,"scale":0.4},"alphaMode":"MASK","alphaCutoff":0.25,"doubleSided":true}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
    })";
    {
        std::ofstream source(sourcePath);
        source << gltf;
    }
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto importer = Keire::CreateMeshAssetImporter();
    CHECK_FALSE(importer.RestoreCachedOutput);
    const auto output = importer.ContextualImport(context, std::as_bytes(std::span(gltf)));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    const auto paintSlot =
        std::ranges::find(mesh->MaterialSlots(), std::string("Paint"), &Keire::MeshMaterialSlot::Name);
    REQUIRE(paintSlot != mesh->MaterialSlots().end());
    const auto materialId = paintSlot->DefaultMaterial;
    REQUIRE(materialId);
    const auto materialOutput = std::ranges::find(output.SubAssets, materialId, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(materialOutput != output.SubAssets.end());
    CHECK(materialOutput->Type == Keire::MaterialAsset::StaticType());
    const auto material = Keire::MaterialAsset::Decode(materialOutput->Bytes);
    CHECK(material->Definition().Shader == shaderId);
    CHECK(material->Definition().Surface.AlphaMode == Keire::MaterialAlphaMode::Mask);
    CHECK(material->Definition().Surface.AlphaCutoff == doctest::Approx(0.25F));
    CHECK(material->Definition().Surface.DoubleSided);
    REQUIRE(std::holds_alternative<Keire::Color>(material->Definition().Properties.at("Tint")));
    const auto tint = std::get<Keire::Color>(material->Definition().Properties.at("Tint"));
    CHECK(tint.Red == doctest::Approx(0.2F));
    CHECK(tint.Alpha == doctest::Approx(0.8F));
    CHECK(std::get<float>(material->Definition().Properties.at("MetallicFactor")) == doctest::Approx(0.3F));
    CHECK(std::get<float>(material->Definition().Properties.at("RoughnessFactor")) == doctest::Approx(0.7F));
    CHECK(std::get<float>(material->Definition().Properties.at("NormalScale")) == doctest::Approx(0.4F));

    const auto colorTexture = std::get<Keire::AssetId>(material->Definition().Properties.at("MainTexture"));
    const auto normalTexture = std::get<Keire::AssetId>(material->Definition().Properties.at("NormalTexture"));
    CHECK(colorTexture != normalTexture);
    const auto colorOutput = std::ranges::find(output.SubAssets, colorTexture, &Keire::AssetGeneratedSubAsset::Id);
    const auto normalOutput = std::ranges::find(output.SubAssets, normalTexture, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(colorOutput != output.SubAssets.end());
    REQUIRE(normalOutput != output.SubAssets.end());
    CHECK(Keire::Texture2DAsset::Decode(colorOutput->Bytes)->Settings().ColorSpace == Keire::TextureColorSpace::Srgb);
    CHECK(Keire::Texture2DAsset::Decode(normalOutput->Bytes)->Settings().Semantic == Keire::TextureSemantic::Normal);

    const auto repeated = importer.ContextualImport(context, std::as_bytes(std::span(gltf)));
    std::vector<Keire::AssetId> firstIds;
    std::vector<Keire::AssetId> repeatedIds;
    std::ranges::transform(output.SubAssets, std::back_inserter(firstIds), &Keire::AssetGeneratedSubAsset::Id);
    std::ranges::transform(repeated.SubAssets, std::back_inserter(repeatedIds), &Keire::AssetGeneratedSubAsset::Id);
    std::ranges::sort(firstIds);
    std::ranges::sort(repeatedIds);
    CHECK(firstIds == repeatedIds);
}

TEST_CASE("OBJ import resolves MTL and texture sidecars through project IO")
{
    TemporaryDirectory directory("ObjSidecarImportTests");
    const auto sourceRoot = directory.Path / "Assets";
    const auto modelPath = sourceRoot / "Models/painted.obj";
    const auto materialPath = sourceRoot / "Models/Materials/Painted.mtl";
    const auto texturePath = sourceRoot / "Models/Textures/albedo_palette.png";
    std::filesystem::create_directories(materialPath.parent_path());
    std::filesystem::create_directories(texturePath.parent_path());
    const auto shaderId = Keire::AssetId::Parse("02134567-89ab-4cde-8f01-23456789abcd");
    WriteImportedMaterialShader(sourceRoot, shaderId);

    const std::string obj = "mtllib Materials/Painted.mtl\n"
                            "o PaintedTriangle\n"
                            "v 0 0 0\n"
                            "v 1 0 0\n"
                            "v 0 1 0\n"
                            "vt 0 0\n"
                            "vt 1 0\n"
                            "vt 0 1\n"
                            "vn 0 0 1\n"
                            "usemtl Painted\n"
                            "f 1/1/1 2/2/1 3/3/1\n";
    {
        std::ofstream model(modelPath);
        model << obj;
        std::ofstream material(materialPath);
        material << "newmtl Painted\nKd 0.2 0.4 0.6\nmap_Kd ../Textures/albedo_palette.png\n";
    }
    const auto png = TestPng();
    WriteTestBytes(texturePath, png);

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("12345678-abcd-4abc-8abc-1234567890ab");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = sourceRoot;
    context.SourcePath = modelPath;
    context.RelativePath = "Models/painted.obj";
    std::vector<std::filesystem::path> reads;
    context.ReadProjectFile = [&directory, &reads](const std::filesystem::path& relative)
    {
        reads.push_back(relative.lexically_normal());
        return ReadTestBytes(directory.Path / relative);
    };
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto output =
        Keire::CreateMeshAssetImporter().ContextualImport(context, std::as_bytes(std::span(obj.data(), obj.size())));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    const auto painted =
        std::ranges::find(mesh->MaterialSlots(), std::string("Painted"), &Keire::MeshMaterialSlot::Name);
    REQUIRE(painted != mesh->MaterialSlots().end());
    REQUIRE(painted->DefaultMaterial);
    const auto materialOutput =
        std::ranges::find(output.SubAssets, painted->DefaultMaterial, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(materialOutput != output.SubAssets.end());
    const auto material = Keire::MaterialAsset::Decode(materialOutput->Bytes);
    REQUIRE(material->Definition().Shader == shaderId);
    const auto texture = std::get<Keire::AssetId>(material->Definition().Properties.at("MainTexture"));
    const auto textureOutput = std::ranges::find(output.SubAssets, texture, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(textureOutput != output.SubAssets.end());
    const auto textureAsset = Keire::Texture2DAsset::Decode(textureOutput->Bytes);
    CHECK(textureAsset->Settings().ColorSpace == Keire::TextureColorSpace::Srgb);
    CHECK(textureAsset->Settings().Mips == Keire::TextureMipPolicy::None);
    CHECK(textureAsset->Settings().Sampler.Minimum == Keire::TextureFilter::Nearest);
    CHECK(textureAsset->Settings().Sampler.Magnification == Keire::TextureFilter::Nearest);
    CHECK(textureAsset->Settings().Sampler.AddressU == Keire::TextureAddressMode::Clamp);
    CHECK(textureAsset->Settings().Sampler.AddressV == Keire::TextureAddressMode::Clamp);

    const std::array expectedDependencies{std::filesystem::path("Assets/Models/Materials/Painted.mtl"),
                                          std::filesystem::path("Assets/Models/Textures/albedo_palette.png")};
    CHECK(output.SourceDependencies.size() == expectedDependencies.size());
    for (const auto& expected : expectedDependencies)
    {
        const auto dependency = std::ranges::find(output.SourceDependencies, expected.lexically_normal(),
                                                  &Keire::AssetSourceDependency::RelativePath);
        REQUIRE(dependency != output.SourceDependencies.end());
        CHECK(dependency->Digest.size() == 64);
        CHECK(std::ranges::count(reads, expected.lexically_normal()) == 1);
    }
    CHECK(std::ranges::none_of(output.Diagnostics, [](const Keire::AssetImportDiagnostic& diagnostic)
                               { return diagnostic.Message.find("not resolved") != std::string::npos; }));
}

TEST_CASE("glTF import resolves external geometry and URI-encoded texture sidecars")
{
    TemporaryDirectory directory("GltfSidecarImportTests");
    const auto sourceRoot = directory.Path / "Assets";
    const auto modelPath = sourceRoot / "Models/external.gltf";
    const auto bufferPath = sourceRoot / "Models/Geometry/triangle.bin";
    const auto texturePath = sourceRoot / "Models/Textures/albedo color.png";
    const auto shaderId = Keire::AssetId::Parse("13134567-89ab-4cde-8f01-23456789abcd");
    WriteImportedMaterialShader(sourceRoot, shaderId);
    const auto geometry = TriangleGltfBuffer();
    const auto png = TestPng();
    const auto gltf = TriangleGltf("Geometry/triangle.bin", "Textures/albedo%20color.png");
    WriteTestBytes(bufferPath, geometry);
    WriteTestBytes(texturePath, png);
    WriteTestBytes(modelPath, std::as_bytes(std::span(gltf.data(), gltf.size())));

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("22345678-abcd-4abc-8abc-1234567890ab");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = sourceRoot;
    context.SourcePath = modelPath;
    context.RelativePath = "Models/external.gltf";
    context.ReadProjectFile = [&directory](const std::filesystem::path& relative)
    { return ReadTestBytes(directory.Path / relative); };
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    const auto output =
        Keire::CreateMeshAssetImporter().ContextualImport(context, std::as_bytes(std::span(gltf.data(), gltf.size())));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    CHECK(mesh->Vertices().size() == 3);
    CHECK(mesh->Indices().size() == 3);
    const auto painted =
        std::ranges::find(mesh->MaterialSlots(), std::string("SidecarPaint"), &Keire::MeshMaterialSlot::Name);
    REQUIRE(painted != mesh->MaterialSlots().end());
    REQUIRE(painted->DefaultMaterial);
    const auto materialOutput =
        std::ranges::find(output.SubAssets, painted->DefaultMaterial, &Keire::AssetGeneratedSubAsset::Id);
    REQUIRE(materialOutput != output.SubAssets.end());
    const auto material = Keire::MaterialAsset::Decode(materialOutput->Bytes);
    const auto texture = std::get<Keire::AssetId>(material->Definition().Properties.at("MainTexture"));
    CHECK(std::ranges::any_of(
        output.SubAssets, [texture](const Keire::AssetGeneratedSubAsset& subAsset)
        { return subAsset.Id == texture && subAsset.Type == Keire::Texture2DAsset::StaticType(); }));

    const std::array expectedDependencies{std::filesystem::path("Assets/Models/Geometry/triangle.bin"),
                                          std::filesystem::path("Assets/Models/Textures/albedo color.png")};
    for (const auto& expected : expectedDependencies)
    {
        const auto dependency = std::ranges::find(output.SourceDependencies, expected.lexically_normal(),
                                                  &Keire::AssetSourceDependency::RelativePath);
        REQUIRE(dependency != output.SourceDependencies.end());
        CHECK(dependency->Digest.size() == 64);
    }
}

TEST_CASE("model sidecar IO rejects a late external OBJ texture escape")
{
    TemporaryDirectory directory("ObjTextureEscapeTests");
    const auto sourceRoot = directory.Path / "Assets";
    const auto modelPath = sourceRoot / "Models/painted.obj";
    const auto materialPath = sourceRoot / "Models/Materials/Painted.mtl";
    std::filesystem::create_directories(materialPath.parent_path());
    const auto shaderId = Keire::AssetId::Parse("23134567-89ab-4cde-8f01-23456789abcd");
    WriteImportedMaterialShader(sourceRoot, shaderId);
    {
        std::ofstream material(materialPath);
        material << "newmtl Painted\nKd 0.2 0.4 0.6\nmap_Kd ../../../outside.png\n";
    }
    const std::string obj = "mtllib Materials/Painted.mtl\n"
                            "o PaintedTriangle\n"
                            "v 0 0 0\n"
                            "v 1 0 0\n"
                            "v 0 1 0\n"
                            "vt 0 0\n"
                            "vt 1 0\n"
                            "vt 0 1\n"
                            "vn 0 0 1\n"
                            "usemtl Painted\n"
                            "f 1/1/1 2/2/1 3/3/1\n";

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("32345678-abcd-4abc-8abc-1234567890ab");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = sourceRoot;
    context.SourcePath = modelPath;
    context.RelativePath = "Models/painted.obj";
    std::vector<std::filesystem::path> reads;
    context.ReadProjectFile = [&directory, &reads](const std::filesystem::path& relative)
    {
        reads.push_back(relative.lexically_normal());
        return ReadTestBytes(directory.Path / relative);
    };
    std::unordered_map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };

    CHECK_THROWS_WITH_AS((void)Keire::CreateMeshAssetImporter().ContextualImport(
                             context, std::as_bytes(std::span(obj.data(), obj.size()))),
                         doctest::Contains("escapes the project source root"), std::invalid_argument);
    CHECK(std::ranges::count(reads, std::filesystem::path("Assets/Models/Materials/Painted.mtl")) == 1);
    CHECK(std::ranges::none_of(reads, [](const std::filesystem::path& path)
                               { return path.generic_string().find("outside.png") != std::string::npos; }));
}

TEST_CASE("model sidecar IO rejects escapes and bounds failures without direct filesystem access")
{
    TemporaryDirectory directory("ModelSidecarSafetyTests");
    const auto sourceRoot = directory.Path / "Assets";
    std::filesystem::create_directories(sourceRoot / "Models");
    const auto importer = Keire::CreateMeshAssetImporter();
    const auto geometry = TriangleGltfBuffer();

    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = sourceRoot;
    context.SourcePath = sourceRoot / "Models/model.gltf";
    context.RelativePath = "Models/model.gltf";

    SUBCASE("source-root escape")
    {
        const auto gltf = TriangleGltf("../../outside.bin");
        std::vector<std::filesystem::path> reads;
        context.ReadProjectFile = [&reads](const std::filesystem::path& relative)
        {
            reads.push_back(relative.lexically_normal());
            return std::vector<std::byte>{};
        };
        CHECK_THROWS_WITH_AS(
            (void)importer.ContextualImport(context, std::as_bytes(std::span(gltf.data(), gltf.size()))),
            doctest::Contains("escapes the project source root"), std::invalid_argument);
        CHECK(reads.empty());
    }

    SUBCASE("per-file byte limit")
    {
        const auto gltf = TriangleGltf("Geometry/triangle.bin");
        context.MaximumDependencyBytes = geometry.size() - 1U;
        std::vector<std::filesystem::path> reads;
        context.ReadProjectFile = [&geometry, &reads](const std::filesystem::path& relative)
        {
            reads.push_back(relative.lexically_normal());
            return geometry;
        };
        CHECK_THROWS_WITH_AS(
            (void)importer.ContextualImport(context, std::as_bytes(std::span(gltf.data(), gltf.size()))),
            doctest::Contains("per-file byte limit"), std::invalid_argument);
        CHECK(!reads.empty());
        for (const auto& read : reads)
            CHECK(std::ranges::count(reads, read) == 1);
    }

    SUBCASE("required sidecar read failure")
    {
        const auto gltf = TriangleGltf("Geometry/missing.bin");
        context.ReadProjectFile = [](const std::filesystem::path&) -> std::vector<std::byte>
        { throw std::runtime_error("fixture sidecar is missing"); };
        CHECK_THROWS_WITH_AS(
            (void)importer.ContextualImport(context, std::as_bytes(std::span(gltf.data(), gltf.size()))),
            doctest::Contains("fixture sidecar is missing"), std::invalid_argument);
    }
}

TEST_CASE("model import can retain material slots without publishing source materials")
{
    TemporaryDirectory directory("GltfMaterialSlotOnlyImportTests");
    const auto sourcePath = directory.Path / "material-slots.gltf";
    const std::string gltf = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA=","byteLength":104}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
        "materials":[{"name":"AuthoredOverride","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.6,1.0]}}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
    })";
    {
        std::ofstream source(sourcePath);
        source << gltf;
    }

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff");
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    context.ImportSettings["materialImport"] = std::string("none");

    const auto output = Keire::CreateMeshAssetImporter().ContextualImport(context, std::as_bytes(std::span(gltf)));
    const auto mesh = Keire::MeshAsset::Decode(output.Bytes);
    const auto authoredSlot =
        std::ranges::find(mesh->MaterialSlots(), std::string("AuthoredOverride"), &Keire::MeshMaterialSlot::Name);
    REQUIRE(authoredSlot != mesh->MaterialSlots().end());
    CHECK_FALSE(authoredSlot->DefaultMaterial);
    CHECK(std::ranges::none_of(mesh->MaterialSlots(), [](const Keire::MeshMaterialSlot& slot)
                               { return static_cast<bool>(slot.DefaultMaterial); }));
    CHECK(std::ranges::none_of(output.SubAssets, [](const Keire::AssetGeneratedSubAsset& subAsset)
                               { return subAsset.Type == Keire::MaterialAsset::StaticType(); }));
}

TEST_CASE("static model import groups conventional mesh names into deterministic LOD ranges")
{
    TemporaryDirectory directory("MeshLodImportTests");
    const auto sourcePath = directory.Path / "lods.obj";
    {
        std::ofstream source(sourcePath);
        source << "o Creature_LOD1\n"
                  "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"
                  "o Creature_LOD0\n"
                  "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 4 5 6\n";
    }
    const auto importer = Keire::CreateMeshAssetImporter();
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path;
    context.SourcePath = sourcePath;
    context.RelativePath = sourcePath.filename();
    const auto mesh = Keire::MeshAsset::Decode(importer.ContextualImport(context, ReadTestBytes(sourcePath)).Bytes);
    REQUIRE(mesh->Lods().size() == 2);
    CHECK(mesh->Lods()[0].FirstSubmesh == 0);
    CHECK(mesh->Lods()[0].SubmeshCount == 1);
    CHECK(mesh->Lods()[0].MinimumScreenHeight == doctest::Approx(0.5F));
    CHECK(mesh->Lods()[0].Bounds.Maximum.X == doctest::Approx(2.0F));
    CHECK(mesh->Lods()[1].FirstSubmesh == 1);
    CHECK(mesh->Lods()[1].MinimumScreenHeight == doctest::Approx(0.0F));
}

TEST_CASE("texture importer emits validated RGBA8 mip chains")
{
    constexpr std::array<unsigned char, 70> bitmap{
        0x42, 0x4d, 70, 0, 0, 0,  0, 0, 0,   0, 54,  0,  0, 0, 40,  0,    0,    0,   2,   0,    0,    0, 2,
        0,    0,    0,  1, 0, 24, 0, 0, 0,   0, 0,   16, 0, 0, 0,   0x13, 0x0b, 0,   0,   0x13, 0x0b, 0, 0,
        0,    0,    0,  0, 0, 0,  0, 0, 255, 0, 255, 0,  0, 0, 255, 0,    0,    255, 255, 255,  0,    0};
    std::vector<std::byte> source(bitmap.size());
    std::ranges::transform(bitmap, source.begin(), [](const unsigned char value) { return std::byte(value); });
    const auto importer = Keire::CreateTexture2DAssetImporter();
    REQUIRE(importer.Import);
    const auto texture = Keire::Texture2DAsset::Decode(importer.Import(source));
    CHECK(texture->Width() == 2);
    CHECK(texture->Height() == 2);
    REQUIRE(texture->Mips().size() == 2);
    CHECK(texture->Mips().back().Width == 1);
    CHECK(texture->Mips().back().Height == 1);
    CHECK(Keire::Texture2DAsset::Encode(texture->Settings(), texture->Mips()) == importer.Import(source));

    const auto paletteAtlas = Keire::Texture2DAsset::Decode(importer.Import(TestBitmap(true)));
    REQUIRE(paletteAtlas->Mips().size() == 1);
    CHECK(paletteAtlas->Settings().Mips == Keire::TextureMipPolicy::None);
    CHECK(paletteAtlas->Settings().Sampler.Minimum == Keire::TextureFilter::Nearest);
    CHECK(paletteAtlas->Settings().Sampler.Magnification == Keire::TextureFilter::Nearest);
    CHECK(paletteAtlas->Settings().Sampler.Mip == Keire::TextureFilter::Nearest);
    CHECK(paletteAtlas->Settings().Sampler.AddressU == Keire::TextureAddressMode::Clamp);
    CHECK(paletteAtlas->Settings().Sampler.AddressV == Keire::TextureAddressMode::Clamp);

    Keire::AssetImportContext automaticContext;
    automaticContext.SourcePath = "neutral.bmp";
    const auto contextualPalette =
        Keire::Texture2DAsset::Decode(importer.ContextualImport(automaticContext, TestBitmap(true)).Bytes);
    CHECK(contextualPalette->Settings() == paletteAtlas->Settings());
    CHECK(contextualPalette->Mips().size() == 1);

    const auto detailedTexture = Keire::Texture2DAsset::Decode(importer.Import(TestBitmap(false)));
    CHECK(detailedTexture->Settings().Mips == Keire::TextureMipPolicy::Generate);
    CHECK(detailedTexture->Mips().size() > 1);

    auto explicitContext = automaticContext;
    explicitContext.ImportSettings["addressU"] = std::string("mirror");
    const auto explicitlyConfigured =
        Keire::Texture2DAsset::Decode(importer.ContextualImport(explicitContext, TestBitmap(true)).Bytes);
    CHECK(explicitlyConfigured->Settings().Sampler.AddressU == Keire::TextureAddressMode::Mirror);
    CHECK(explicitlyConfigured->Settings().Mips == Keire::TextureMipPolicy::Generate);
    CHECK(explicitlyConfigured->Mips().size() > 1);

    const std::string hdrHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::vector<std::byte> hdr;
    hdr.reserve(hdrHeader.size() + 4);
    std::ranges::transform(hdrHeader, std::back_inserter(hdr),
                           [](const char value) { return std::byte(static_cast<unsigned char>(value)); });
    hdr.insert(hdr.end(), {std::byte{128}, std::byte{64}, std::byte{32}, std::byte{129}});
    const auto environment = Keire::Texture2DAsset::Decode(importer.Import(hdr));
    CHECK(environment->Width() == 1);
    CHECK(environment->Height() == 1);
    CHECK(environment->Settings().Semantic == Keire::TextureSemantic::Environment);
    CHECK(environment->Settings().ColorSpace == Keire::TextureColorSpace::Linear);
    CHECK(environment->Settings().EnvironmentLayout == Keire::TextureEnvironmentLayout::Equirectangular);
    CHECK(environment->Settings().HighDynamicRange);

    auto versionTwo = Keire::Texture2DAsset::Encode(texture->Settings(), texture->Mips());
    versionTwo[8] = std::byte{2};
    versionTwo[9] = std::byte{0};
    versionTwo[10] = std::byte{0};
    versionTwo[11] = std::byte{0};
    versionTwo.erase(versionTwo.begin() + 23, versionTwo.begin() + 25);
    const auto legacyTexture = Keire::Texture2DAsset::Decode(versionTwo);
    CHECK(legacyTexture->Width() == texture->Width());
    CHECK(legacyTexture->Settings().EnvironmentLayout == Keire::TextureEnvironmentLayout::Auto);
    CHECK_FALSE(legacyTexture->Settings().HighDynamicRange);

    auto malformed = Keire::Texture2DAsset::Encode(texture->Settings(), texture->Mips());
    malformed.push_back(std::byte{0});
    CHECK_THROWS_AS((void)Keire::Texture2DAsset::Decode(malformed), std::invalid_argument);
    auto invalidSettings = texture->Settings();
    invalidSettings.Sampler.Anisotropy = 0;
    CHECK_THROWS_AS((void)Keire::Texture2DAsset::Encode(invalidSettings, texture->Mips()), std::invalid_argument);

    const auto checkerboard = Keire::Texture2DAsset::Checkerboard();
    CHECK(checkerboard->Width() == 2);
    CHECK(checkerboard->Height() == 2);
    CHECK(checkerboard->Mips().size() == 1);

    TemporaryDirectory directory("TextureMetadataTests");
    const auto metadata = directory.Path / "texture.bmp.keiremeta";
    {
        std::ofstream output(metadata);
        output
            << R"({"textureImportSettings":{"semantic":"data","colorSpace":"srgb","mips":"none","maximumSize":1,"flipGreen":true,"sampler":{"min":"nearest","addressU":"clamp","anisotropy":4}}})";
    }
    Keire::AssetImportContext context;
    context.MetadataPath = metadata;
    REQUIRE(importer.ContextualImport);
    const auto configured = Keire::Texture2DAsset::Decode(importer.ContextualImport(context, source).Bytes);
    CHECK(configured->Width() == 1);
    CHECK(configured->Mips().size() == 1);
    CHECK(configured->Settings().Semantic == Keire::TextureSemantic::Data);
    CHECK(configured->Settings().ColorSpace == Keire::TextureColorSpace::Linear);
    CHECK(configured->Settings().Sampler.Minimum == Keire::TextureFilter::Nearest);
    CHECK(configured->Settings().Sampler.AddressU == Keire::TextureAddressMode::Clamp);
    CHECK(configured->Settings().Sampler.Anisotropy == 4);
    CHECK(configured->Settings().FlipGreen);

    auto environmentSettings = texture->Settings();
    environmentSettings.Semantic = Keire::TextureSemantic::Environment;
    environmentSettings.ColorSpace = Keire::TextureColorSpace::Linear;
    environmentSettings.EnvironmentLayout = Keire::TextureEnvironmentLayout::HorizontalCross;
    environmentSettings.HighDynamicRange = true;
    const auto encodedEnvironment =
        Keire::Texture2DAsset::Decode(Keire::Texture2DAsset::Encode(environmentSettings, texture->Mips()));
    CHECK(encodedEnvironment->Settings().Semantic == Keire::TextureSemantic::Environment);
    CHECK(encodedEnvironment->Settings().EnvironmentLayout == Keire::TextureEnvironmentLayout::HorizontalCross);
    CHECK(encodedEnvironment->Settings().HighDynamicRange);
}

TEST_CASE("OpenEXR texture import supports color normal and HDR environment semantics")
{
    const std::array source{std::byte{0x76}, std::byte{0x2f}, std::byte{0x31}, std::byte{0x01}, std::byte{0}};
    const Keire::Detail::TextureDecodeBackend decode = [](std::span<const std::byte>)
    {
        return Keire::Detail::DecodedFloatTexture{
            .Width = 2, .Height = 1, .Pixels = {2.0F, 0.25F, 0.5F, 1.0F, 0.0F, 0.75F, 1.0F, 0.5F}};
    };

    Keire::TextureImportSettings colorSettings;
    colorSettings.ColorSpace = Keire::TextureColorSpace::Linear;
    colorSettings.Mips = Keire::TextureMipPolicy::None;
    const auto colorImporter = Keire::Detail::CreateTexture2DAssetImporter(colorSettings, decode);
    const auto color = Keire::Texture2DAsset::Decode(colorImporter.Import(source));
    REQUIRE(color->Mips().size() == 1);
    CHECK(color->Mips().front().Pixels[0] == std::byte{255});
    CHECK(color->Mips().front().Pixels[1] == std::byte{64});
    CHECK(color->Mips().front().Pixels[7] == std::byte{128});

    auto normalSettings = colorSettings;
    normalSettings.Semantic = Keire::TextureSemantic::Normal;
    normalSettings.FlipGreen = true;
    const auto normalImporter = Keire::Detail::CreateTexture2DAssetImporter(normalSettings, decode);
    const auto normal = Keire::Texture2DAsset::Decode(normalImporter.Import(source));
    CHECK(normal->Settings().Semantic == Keire::TextureSemantic::Normal);
    CHECK(normal->Mips().front().Pixels[1] == std::byte{191});

    auto environmentSettings = colorSettings;
    environmentSettings.Semantic = Keire::TextureSemantic::Environment;
    environmentSettings.HighDynamicRange = true;
    const auto environmentImporter = Keire::Detail::CreateTexture2DAssetImporter(environmentSettings, decode);
    const auto environment = Keire::Texture2DAsset::Decode(environmentImporter.Import(source));
    CHECK(environment->Settings().HighDynamicRange);
    CHECK(environment->Mips().front().Pixels[3] != std::byte{255});
}

TEST_CASE("pinned shader compiler resolves from the executable while the project is the working directory")
{
    const auto repository = std::filesystem::current_path();
#if defined(_WIN32)
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
    REQUIRE(std::filesystem::is_regular_file(compiler));
    const auto project = repository / "Samples/KeireSandbox";
    const auto sourceRoot = project / "Assets";
    const auto manifest = sourceRoot / "Shaders/DefaultUnlit.keireshader";
    Keire::AssetImportContext context;
    context.ProjectRoot = project;
    context.SourceRoot = sourceRoot;
    context.SourcePath = manifest;
    context.RelativePath = "Shaders/DefaultUnlit.keireshader";
    context.ReadProjectFile = [project](const std::filesystem::path& relative)
    { return ReadTestBytes(project / relative); };

    KeireTests::CurrentDirectoryGuard workingDirectory(project);
    const auto importer = Keire::CreateShaderAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto output = importer.ContextualImport(context, ReadTestBytes(manifest));
    const auto shader = Keire::ShaderAsset::Decode(output.Bytes);
    REQUIRE(shader->Definition().Variants.size() == 3);
    CHECK(shader->Definition().ReceivesShadows);
    CHECK(shader->Definition().SpatialLightingAbiVersion == 3U);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
    REQUIRE(output.SourceDependencies.size() == 1);
    CHECK(output.SourceDependencies.front().RelativePath == std::filesystem::path("Assets/Shaders/DefaultUnlit.hlsl"));
    CHECK(output.Diagnostics.empty());
}

TEST_CASE("camera and mesh renderer components validate renderer-neutral authoring data")
{
    auto camera = Keire::CreateRef<Keire::CameraComponent>();
    camera->SetVerticalFieldOfViewDegrees(75.0F);
    camera->SetClipPlanes(0.25F, 5000.0F);
    camera->SetClearColor({0.1F, 0.2F, 0.3F, 1.0F});
    CHECK(Keire::Math::IsFinite(camera->ProjectionMatrix(16.0F / 9.0F)));
    CHECK_THROWS_AS(camera->SetClipPlanes(10.0F, 1.0F), std::invalid_argument);
    CHECK_THROWS_AS(camera->SetVerticalFieldOfViewDegrees(180.0F), std::invalid_argument);

    auto renderer = Keire::CreateRef<Keire::MeshRendererComponent>();
    CHECK(renderer->Mesh() == Keire::MeshAsset::CubeId());
    renderer->SetMesh({});
    CHECK(renderer->Mesh() == Keire::MeshAsset::CubeId());
    renderer->SetMesh(Keire::MeshAsset::CubeId());
    renderer->SetMaterial(Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000002"));
    renderer->SetMaterial(2, Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000003"));
    renderer->SetTint({0.25F, 0.55F, 1.0F, 1.0F});
    CHECK(renderer->Visible());
    CHECK(renderer->Mesh() == Keire::MeshAsset::CubeId());
    CHECK(renderer->Materials().size() == 3);
    CHECK(renderer->Material(2) == Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000003"));
    renderer->SetMaterialInstanceProperty(2, "EmissiveStrength", 4.0F);
    CHECK(std::get<float>(renderer->MaterialInstanceProperties(2).at("EmissiveStrength")) == doctest::Approx(4.0F));
    renderer->SetMaterial(2, Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000004"));
    CHECK(renderer->MaterialInstanceProperties(2).empty());
    CHECK_THROWS_AS(renderer->SetMaterialInstanceProperty(256, "Invalid", 1.0F), std::out_of_range);
    CHECK_THROWS_AS(renderer->SetTint({2.0F, 0.0F, 0.0F, 1.0F}), std::invalid_argument);

    auto light = Keire::CreateRef<Keire::DirectionalLightComponent>();
    light->SetLightColor({0.8F, 0.7F, 0.6F, 1.0F});
    light->SetIntensity(2.5F);
    light->SetUseColorTemperature(true);
    light->SetColorTemperatureKelvin(4200.0F);
    CHECK(light->Intensity() == doctest::Approx(2.5F));
    CHECK(light->UseColorTemperature());
    CHECK(light->ColorTemperatureKelvin() == doctest::Approx(4200.0F));
    CHECK_THROWS_AS(light->SetColorTemperatureKelvin(500.0F), std::invalid_argument);

    auto point = Keire::CreateRef<Keire::PointLightComponent>();
    point->SetIntensity(12.0F);
    point->SetRange(18.0F);
    point->SetShadows(Keire::ShadowQuality::Hard);
    point->SetShadowStrength(0.75F);
    point->SetShadowBias(0.01F);
    CHECK(point->Range() == doctest::Approx(18.0F));
    CHECK(point->Shadows() == Keire::ShadowQuality::Hard);
    CHECK(point->ShadowStrength() == doctest::Approx(0.75F));
    CHECK_THROWS_AS(point->SetRange(0.0F), std::invalid_argument);

    auto spot = Keire::CreateRef<Keire::SpotLightComponent>();
    spot->SetRange(25.0F);
    spot->SetConeAngles(15.0F, 30.0F);
    spot->SetShadows(Keire::ShadowQuality::Soft);
    spot->SetShadowStrength(0.6F);
    spot->SetShadowBias(0.012F);
    CHECK(spot->InnerAngleDegrees() == doctest::Approx(15.0F));
    CHECK(spot->OuterAngleDegrees() == doctest::Approx(30.0F));
    CHECK(spot->Shadows() == Keire::ShadowQuality::Soft);
    CHECK(spot->ShadowStrength() == doctest::Approx(0.6F));
    CHECK_THROWS_AS(spot->SetConeAngles(45.0F, 30.0F), std::invalid_argument);

    const auto registry = Keire::ComponentRegistry::CreateDefault();
    CHECK(registry->Contains(Keire::PointLightComponent::StaticType()));
    CHECK(registry->Contains(Keire::SpotLightComponent::StaticType()));
}
