#include "Keire/Core.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
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
    CHECK(Keire::Detail::BuiltinShaderUniformBufferCount(false) == 0);
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
    CHECK(camera.State().Distance > 2.0F);

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
    CHECK(definition.Texture("MainTexture") == texture);
    CHECK_FALSE(definition.Texture("Missing"));

    const auto decoded = Keire::MaterialAsset::Decode(Keire::MaterialAsset::Encode(definition));
    CHECK(decoded->Definition().Shader == definition.Shader);
    CHECK(decoded->Definition().Properties.size() == 3);
    CHECK(std::get<Keire::AssetId>(decoded->Definition().Properties.at("MainTexture")) == texture);
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
    const auto versionTwo = Keire::MeshAsset::Encode(vertices, indices);
    constexpr std::size_t headerSize = 8U + 4U + 8U + 8U + 6U * sizeof(float);
    constexpr std::size_t versionTwoVertexSize = 16U * sizeof(float);
    constexpr std::size_t versionOneVertexSize = 12U * sizeof(float);
    std::vector<std::byte> versionOne(versionTwo.begin(), versionTwo.begin() + headerSize);
    versionOne[8] = std::byte{1};
    versionOne[9] = std::byte{0};
    versionOne[10] = std::byte{0};
    versionOne[11] = std::byte{0};
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        const auto begin = versionTwo.begin() + static_cast<std::ptrdiff_t>(headerSize + index * versionTwoVertexSize);
        versionOne.insert(versionOne.end(), begin, begin + versionOneVertexSize);
    }
    versionOne.insert(versionOne.end(),
                      versionTwo.begin() +
                          static_cast<std::ptrdiff_t>(headerSize + vertices.size() * versionTwoVertexSize),
                      versionTwo.end());

    const auto decodedOne = Keire::MeshAsset::Decode(versionOne);
    const auto decodedTwo = Keire::MeshAsset::Decode(versionTwo);
    REQUIRE(decodedOne->Vertices().size() == decodedTwo->Vertices().size());
    for (std::size_t index = 0; index < decodedOne->Vertices().size(); ++index)
        CHECK(decodedOne->Vertices()[index].Tangent == decodedTwo->Vertices()[index].Tangent);
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
    renderer->SetTint({0.25F, 0.55F, 1.0F, 1.0F});
    CHECK(renderer->Visible());
    CHECK(renderer->Mesh() == Keire::MeshAsset::CubeId());
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
}
