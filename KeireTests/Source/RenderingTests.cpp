#include "Keire/Core.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
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
} // namespace

TEST_CASE("built-in shader resource counts match each stage")
{
    CHECK(Keire::Detail::BuiltinShaderUniformBufferCount(true) == 1);
    CHECK(Keire::Detail::BuiltinShaderUniformBufferCount(false) == 2);
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
    CHECK(Keire::ShaderAsset::Encode(decoded->Definition()) == encoded);

    const auto importer = Keire::CreateShaderAssetImporter();
    REQUIRE(importer.Cook);
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
    CHECK(definition.Texture("MainTexture") == texture);
    CHECK_FALSE(definition.Texture("Missing"));

    const auto decoded = Keire::MaterialAsset::Decode(Keire::MaterialAsset::Encode(definition));
    CHECK(decoded->Definition().Shader == definition.Shader);
    CHECK(decoded->Definition().Properties.size() == 3);
    CHECK(std::get<Keire::AssetId>(decoded->Definition().Properties.at("MainTexture")) == texture);
    CHECK(decoded->Definition().Surface == definition.Surface);
    const auto sourceDecoded = Keire::MaterialAsset::DecodeSource(Keire::MaterialAsset::EncodeSource(definition));
    CHECK(sourceDecoded.Shader == definition.Shader);
    CHECK(sourceDecoded.Texture("MainTexture") == texture);
    auto mutableDefinition = sourceDecoded;
    CHECK(mutableDefinition.RemoveTexture("MainTexture"));
    CHECK_FALSE(mutableDefinition.RemoveTexture("MainTexture"));
    CHECK(Keire::MeshAsset::Cube()->Mesh() == Keire::BuiltinMesh::Cube);
    CHECK(Keire::MeshAsset::CubeId() != Keire::MeshAsset::ErrorId());
    CHECK(Keire::MaterialAsset::Error()->Definition().Properties.contains("ErrorColor"));
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
    renderer->SetMesh(Keire::MeshAsset::CubeId());
    renderer->SetMaterial(Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000002"));
    renderer->SetMaterial(2, Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000003"));
    renderer->SetTint({0.25F, 0.55F, 1.0F, 1.0F});
    CHECK(renderer->Visible());
    CHECK(renderer->Mesh() == Keire::MeshAsset::CubeId());
    CHECK(renderer->Materials().size() == 3);
    CHECK(renderer->Material(2) == Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000003"));
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
