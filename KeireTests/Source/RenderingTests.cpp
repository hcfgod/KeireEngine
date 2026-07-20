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
#include <utility>
#include <vector>

namespace
{
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
    CHECK(Keire::ShaderAsset::Encode(decoded->Definition()) == encoded);

    const auto importer = Keire::CreateShaderAssetImporter();
    REQUIRE(importer.Cook);
    const auto windows = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::Windows));
    REQUIRE(windows->Definition().Variants.size() == 1);
    CHECK(windows->Definition().Variants.front().Format == Keire::ShaderBinaryFormat::Dxil);
    const auto linux = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::Linux));
    REQUIRE(linux->Definition().Variants.size() == 1);
    CHECK(linux->Definition().Variants.front().Format == Keire::ShaderBinaryFormat::SpirV);
    const auto macOS = Keire::ShaderAsset::Decode(importer.Cook(encoded, Keire::AssetTargetPlatform::MacOS));
    REQUIRE(macOS->Definition().Variants.size() == 1);
    CHECK(macOS->Definition().Variants.front().Format == Keire::ShaderBinaryFormat::Msl);
}

TEST_CASE("material and built-in mesh assets retain Kéire-owned identities")
{
    Keire::MaterialAssetDefinition definition;
    definition.Shader = Keire::AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000001");
    definition.Properties.emplace("Roughness", 0.5F);
    definition.Properties.emplace("Tint", Keire::Color{0.25F, 0.5F, 1.0F, 1.0F});

    const auto decoded = Keire::MaterialAsset::Decode(Keire::MaterialAsset::Encode(definition));
    CHECK(decoded->Definition().Shader == definition.Shader);
    CHECK(decoded->Definition().Properties.size() == 2);
    CHECK(Keire::MeshAsset::Cube()->Mesh() == Keire::BuiltinMesh::Cube);
    CHECK(Keire::MeshAsset::CubeId() != Keire::MeshAsset::ErrorId());
    CHECK(Keire::MaterialAsset::Error()->Definition().Properties.contains("ErrorColor"));
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
