#include "KeireTests/TestSupport.h"

#include "KeireInternal/Assets/BuiltinMeshes.h"
#include "KeireInternal/Rendering/GpuOcclusionPolicyInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Math/Math.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Scenes/SceneAsset.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view StressScenePath = "Samples/KeireSandbox/Assets/Scenes/GpuOcclusionStress.keirescene";
    constexpr std::string_view StressSceneMetadataPath =
        "Samples/KeireSandbox/Assets/Scenes/GpuOcclusionStress.keirescene.keiremeta";
    constexpr std::string_view StressSceneTemplatePath =
        "KeireHubContent/Templates/Payloads/Sandbox/Assets/Scenes/GpuOcclusionStress.keirescene";
    constexpr std::string_view StressSceneTemplateMetadataPath =
        "KeireHubContent/Templates/Payloads/Sandbox/Assets/Scenes/GpuOcclusionStress.keirescene.keiremeta";
    constexpr std::string_view SandboxProjectPath = "Samples/KeireSandbox/ProjectSettings/Project.keireproject";
    constexpr std::string_view SandboxRenderingSettingsPath =
        "Samples/KeireSandbox/ProjectSettings/Rendering.keiresettings";
    constexpr std::string_view SandboxBuildScenesPath =
        "Samples/KeireSandbox/ProjectSettings/BuildScenes.keiresettings";
    constexpr std::string_view SandboxTemplateBuildScenesPath =
        "KeireHubContent/Templates/Payloads/Sandbox/ProjectSettings/BuildScenes.keiresettings";
    constexpr std::string_view SandboxStartupSceneMetadataPath =
        "Samples/KeireSandbox/Assets/Scenes/SandboxShowcase.keirescene.keiremeta";
    constexpr std::string_view StudioPaintMaterialGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/"
        "MG_01_StudioPaint.keirematerialgraph";
    constexpr std::string_view StudioPaintMaterialMetadataPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/"
        "MG_01_StudioPaint.keirematerialgraph.keiremeta";
    constexpr std::string_view StudioPaintShaderGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/"
        "SG_01_StudioPaint.keireshadergraph";
    constexpr std::string_view AutomotiveMaterialGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/02_Production/"
        "MG_05_AutomotiveClearCoat.keirematerialgraph";
    constexpr std::string_view AutomotiveMaterialMetadataPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/02_Production/"
        "MG_05_AutomotiveClearCoat.keirematerialgraph.keiremeta";
    constexpr std::string_view AutomotiveShaderGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/02_Production/"
        "SG_05_AutomotiveClearCoat.keireshadergraph";
    constexpr std::string_view NeonMaterialGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/"
        "MG_03_NeonPulse.keirematerialgraph";
    constexpr std::string_view NeonMaterialMetadataPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs/01_Foundations/"
        "MG_03_NeonPulse.keirematerialgraph.keiremeta";
    constexpr std::string_view NeonShaderGraphPath =
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/"
        "SG_03_NeonPulse.keireshadergraph";
    constexpr std::string_view StudioPaintMaterial = "77c1e51e-6397-5983-b80b-e82587b2edaa";
    constexpr std::string_view AutomotiveMaterial = "78b21fbf-d81b-511f-9d6e-78df263d3652";
    constexpr std::string_view NeonMaterial = "8b3aebee-37f7-5e6d-a096-617a4893e5b9";

    struct PresentationMaterial final
    {
        std::string_view MaterialGraphPath;
        std::string_view MaterialMetadataPath;
        std::string_view ShaderGraphPath;
        std::string_view Material;
        std::string_view ColorProperty;
        std::array<float, 4> ExpectedColor;
    };

    constexpr std::array PresentationMaterials{
        PresentationMaterial{StudioPaintMaterialGraphPath,
                             StudioPaintMaterialMetadataPath,
                             StudioPaintShaderGraphPath,
                             StudioPaintMaterial,
                             "BaseTint",
                             {0.72F, 0.06F, 0.04F, 1.0F}},
        PresentationMaterial{AutomotiveMaterialGraphPath,
                             AutomotiveMaterialMetadataPath,
                             AutomotiveShaderGraphPath,
                             AutomotiveMaterial,
                             "BodyColor",
                             {0.015F, 0.12F, 0.5F, 1.0F}},
        PresentationMaterial{NeonMaterialGraphPath,
                             NeonMaterialMetadataPath,
                             NeonShaderGraphPath,
                             NeonMaterial,
                             "EmissionColor",
                             {0.0F, 1.0F, 0.65F, 1.0F}},
    };
    constexpr std::string_view HiddenRootName = "Hidden Targets - 144 Behind Occluder";
    constexpr std::string_view VisibleRootName = "Visible Controls - 16 Outside Occluder";
    constexpr std::string_view CameraName = "Play Mode Camera - Automatic GPU Occlusion";
    constexpr std::string_view OccluderName = "Opaque Occluder - Automatic Coverage Reference";
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    struct ViewportSize final
    {
        std::uint32_t Width;
        std::uint32_t Height;
    };

    struct ProjectedRectangle final
    {
        float MinimumX;
        float MinimumY;
        float MaximumX;
        float MaximumY;

        [[nodiscard]] float Area() const noexcept
        {
            return std::max(0.0F, MaximumX - MinimumX) * std::max(0.0F, MaximumY - MinimumY);
        }
    };

    [[nodiscard]] Keire::Ref<Keire::SceneAsset> LoadStressScene()
    {
        const auto source = KeireTests::ReadFile(std::filesystem::current_path() / StressScenePath);
        return Keire::SceneAsset::Decode(std::as_bytes(std::span(source.data(), source.size())));
    }

    [[nodiscard]] const Keire::SceneObjectDefinition* FindObject(const Keire::SceneDefinition& scene,
                                                                 const std::string_view name)
    {
        const auto found = std::ranges::find(scene.Objects, name, &Keire::SceneObjectDefinition::Name);
        return found == scene.Objects.end() ? nullptr : std::addressof(*found);
    }

    [[nodiscard]] const Keire::SceneComponentDefinition* FindComponent(const Keire::SceneObjectDefinition& object,
                                                                       const Keire::ComponentTypeId type)
    {
        const auto found = std::ranges::find(object.Components, type, &Keire::SceneComponentDefinition::Type);
        return found == object.Components.end() ? nullptr : std::addressof(*found);
    }

    [[nodiscard]] nlohmann::json ComponentData(const Keire::SceneObjectDefinition& object,
                                               const Keire::ComponentTypeId type)
    {
        const auto* component = FindComponent(object, type);
        return component ? nlohmann::json::parse(component->Data) : nlohmann::json{};
    }

    [[nodiscard]] Keire::MeshBounds CalculateBounds(const std::span<const Keire::MeshVertex> vertices)
    {
        const auto maximum = std::numeric_limits<float>::max();
        const auto minimum = std::numeric_limits<float>::lowest();
        Keire::MeshBounds bounds{{maximum, maximum, maximum}, {minimum, minimum, minimum}};
        for (const auto& vertex : vertices)
        {
            bounds.Minimum.X = std::min(bounds.Minimum.X, vertex.Position.X);
            bounds.Minimum.Y = std::min(bounds.Minimum.Y, vertex.Position.Y);
            bounds.Minimum.Z = std::min(bounds.Minimum.Z, vertex.Position.Z);
            bounds.Maximum.X = std::max(bounds.Maximum.X, vertex.Position.X);
            bounds.Maximum.Y = std::max(bounds.Maximum.Y, vertex.Position.Y);
            bounds.Maximum.Z = std::max(bounds.Maximum.Z, vertex.Position.Z);
        }
        return bounds;
    }

    [[nodiscard]] Keire::Matrix4 ClipFromObject(const Keire::Matrix4& clipFromWorld,
                                                const Keire::SceneObjectDefinition& object)
    {
        const auto world =
            Keire::Math::ComposeTransform(object.Transform.Position, object.Transform.Rotation, object.Transform.Scale);
        return Keire::Math::Multiply(clipFromWorld, world);
    }

    [[nodiscard]] ProjectedRectangle ProjectBounds(const Keire::Matrix4& clipFromLocal, const Keire::MeshBounds bounds,
                                                   const ViewportSize viewport) noexcept
    {
        float minimumX = static_cast<float>(viewport.Width);
        float minimumY = static_cast<float>(viewport.Height);
        float maximumX = 0.0F;
        float maximumY = 0.0F;
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const Keire::Vector3 point{(corner & 1U) != 0U ? bounds.Maximum.X : bounds.Minimum.X,
                                       (corner & 2U) != 0U ? bounds.Maximum.Y : bounds.Minimum.Y,
                                       (corner & 4U) != 0U ? bounds.Maximum.Z : bounds.Minimum.Z};
            const auto clip = Keire::RenderBackend::GeometryDetail::TransformClip(clipFromLocal, point);
            if (!std::isfinite(clip.X) || !std::isfinite(clip.Y) || !std::isfinite(clip.W))
                return {};
            if (clip.W <= 0.00001F)
            {
                return {0.0F, 0.0F, static_cast<float>(viewport.Width), static_cast<float>(viewport.Height)};
            }
            const float x = (clip.X / clip.W * 0.5F + 0.5F) * static_cast<float>(viewport.Width);
            const float y = (-clip.Y / clip.W * 0.5F + 0.5F) * static_cast<float>(viewport.Height);
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
        minimumX = std::clamp(minimumX, 0.0F, static_cast<float>(viewport.Width));
        minimumY = std::clamp(minimumY, 0.0F, static_cast<float>(viewport.Height));
        maximumX = std::clamp(maximumX, 0.0F, static_cast<float>(viewport.Width));
        maximumY = std::clamp(maximumY, 0.0F, static_cast<float>(viewport.Height));
        return {minimumX, minimumY, maximumX, maximumY};
    }

    [[nodiscard]] std::uint32_t CountCoveredCells(const ProjectedRectangle rectangle,
                                                  const ViewportSize viewport) noexcept
    {
        std::uint32_t covered = 0;
        for (std::uint32_t row = 0; row < Policy::AutomaticCoverageRows; ++row)
        {
            const float minimumY = static_cast<float>(row) * static_cast<float>(viewport.Height) /
                                   static_cast<float>(Policy::AutomaticCoverageRows);
            const float maximumY = static_cast<float>(row + 1U) * static_cast<float>(viewport.Height) /
                                   static_cast<float>(Policy::AutomaticCoverageRows);
            for (std::uint32_t column = 0; column < Policy::AutomaticCoverageColumns; ++column)
            {
                const float minimumX = static_cast<float>(column) * static_cast<float>(viewport.Width) /
                                       static_cast<float>(Policy::AutomaticCoverageColumns);
                const float maximumX = static_cast<float>(column + 1U) * static_cast<float>(viewport.Width) /
                                       static_cast<float>(Policy::AutomaticCoverageColumns);
                covered +=
                    static_cast<std::uint32_t>(rectangle.MinimumX <= minimumX && rectangle.MaximumX >= maximumX &&
                                               rectangle.MinimumY <= minimumY && rectangle.MaximumY >= maximumY);
            }
        }
        return covered;
    }
} // namespace

TEST_CASE("Sandbox GPU occlusion stress scene stays above Automatic activation thresholds")
{
    const auto asset = LoadStressScene();
    REQUIRE(asset);
    const auto& scene = asset->Definition();
    CHECK(scene.SchemaVersion == Keire::CurrentSceneSchemaVersion);
    CHECK(scene.Name == "GPU Occlusion Stress - Automatic Threshold and Hidden Targets");
    CHECK(scene.PrefabInstances.empty());
    CHECK(scene.PrefabOverrides.empty());

    const auto* camera = FindObject(scene, CameraName);
    const auto* occluder = FindObject(scene, OccluderName);
    const auto* hiddenRoot = FindObject(scene, HiddenRootName);
    const auto* visibleRoot = FindObject(scene, VisibleRootName);
    REQUIRE(camera);
    REQUIRE(occluder);
    REQUIRE(hiddenRoot);
    REQUIRE(visibleRoot);

    CHECK(camera->Active);
    CHECK(hiddenRoot->Active);
    CHECK(visibleRoot->Active);
    CHECK_FALSE(camera->Parent);
    CHECK_FALSE(occluder->Parent);
    CHECK_FALSE(hiddenRoot->Parent);
    CHECK_FALSE(visibleRoot->Parent);
    CHECK(hiddenRoot->Transform.Position == Keire::Vector3{});
    CHECK(hiddenRoot->Transform.Rotation == Keire::Quaternion{0.0F, 0.0F, 0.0F, 1.0F});
    CHECK(hiddenRoot->Transform.Scale == Keire::Vector3{1.0F, 1.0F, 1.0F});
    CHECK(visibleRoot->Transform.Position == Keire::Vector3{});
    CHECK(visibleRoot->Transform.Rotation == Keire::Quaternion{0.0F, 0.0F, 0.0F, 1.0F});
    CHECK(visibleRoot->Transform.Scale == Keire::Vector3{1.0F, 1.0F, 1.0F});
    CHECK(camera->Transform.Position == Keire::Vector3{0.0F, 0.0F, -30.0F});
    CHECK(camera->Transform.Rotation == Keire::Quaternion{0.0F, 0.0F, 0.0F, 1.0F});
    const auto* cameraComponent = FindComponent(*camera, Keire::CameraComponent::StaticType());
    REQUIRE(cameraComponent);
    CHECK(cameraComponent->Enabled);
    const auto cameraData = ComponentData(*camera, Keire::CameraComponent::StaticType());
    REQUIRE_FALSE(cameraData.empty());
    CHECK(cameraData.at("primary").get<bool>());
    CHECK(cameraData.at("projection").get<std::uint32_t>() == 0U);
    CHECK(cameraData.at("fieldOfView").get<float>() == doctest::Approx(55.0F));

    const auto* occluderRenderer = FindComponent(*occluder, Keire::MeshRendererComponent::StaticType());
    REQUIRE(occluderRenderer);
    CHECK(occluder->Active);
    CHECK(occluderRenderer->Enabled);
    CHECK(occluderRenderer->SchemaVersion == 4U);
    const auto occluderData = nlohmann::json::parse(occluderRenderer->Data);
    CHECK(occluderData.at("mesh").get<std::string>() == Keire::MeshAsset::CubeId().ToString());
    CHECK(occluderData.at("material").get<std::string>() == AutomotiveMaterial);
    CHECK(occluderData.at("visible").get<bool>());
    CHECK_FALSE(occluderData.at("alwaysVisible").get<bool>());
    CHECK(occluder->Transform.Position == Keire::Vector3{});
    CHECK(occluder->Transform.Rotation == Keire::Quaternion{0.0F, 0.0F, 0.0F, 1.0F});
    CHECK(occluder->Transform.Scale == Keire::Vector3{12.0F, 18.0F, 1.0F});

    std::size_t hiddenTargetCount = 0;
    std::size_t visibleControlCount = 0;
    std::size_t sphereRendererCount = 0;
    std::size_t totalRendererCount = 0;
    std::vector<const Keire::SceneObjectDefinition*> sphereObjects;
    for (const auto& object : scene.Objects)
    {
        if (FindComponent(object, Keire::MeshRendererComponent::StaticType()))
            ++totalRendererCount;
        const bool hidden = object.Parent == hiddenRoot->Id;
        const bool visibleControl = object.Parent == visibleRoot->Id;
        if (!hidden && !visibleControl)
            continue;

        hiddenTargetCount += static_cast<std::size_t>(hidden);
        visibleControlCount += static_cast<std::size_t>(visibleControl);
        const auto* renderer = FindComponent(object, Keire::MeshRendererComponent::StaticType());
        REQUIRE(renderer);
        CHECK(object.Active);
        CHECK(renderer->Enabled);
        CHECK(renderer->SchemaVersion == 4U);
        const auto data = nlohmann::json::parse(renderer->Data);
        CHECK(data.at("mesh").get<std::string>() == Keire::MeshAsset::SphereId().ToString());
        CHECK(data.at("material").get<std::string>() == (hidden ? StudioPaintMaterial : NeonMaterial));
        CHECK(data.at("visible").get<bool>());
        CHECK_FALSE(data.at("alwaysVisible").get<bool>());
        ++sphereRendererCount;
        sphereObjects.push_back(std::addressof(object));

        INFO(object.Name);
        const float radius = object.Transform.Scale.X * 0.5F;
        CHECK(object.Transform.Scale.X == doctest::Approx(object.Transform.Scale.Y));
        CHECK(object.Transform.Scale.X == doctest::Approx(object.Transform.Scale.Z));
        CHECK(object.Transform.Position.Z - radius >
              occluder->Transform.Position.Z + occluder->Transform.Scale.Z * 0.5F);

        const float cameraZ = camera->Transform.Position.Z;
        const float wallFrontDistance = occluder->Transform.Position.Z - occluder->Transform.Scale.Z * 0.5F - cameraZ;
        if (hidden)
        {
            const float targetNearDistance = object.Transform.Position.Z - radius - cameraZ;
            const float projectedXAtWall =
                (std::abs(object.Transform.Position.X) + radius) * wallFrontDistance / targetNearDistance;
            const float projectedYAtWall =
                (std::abs(object.Transform.Position.Y) + radius) * wallFrontDistance / targetNearDistance;
            CHECK(projectedXAtWall < occluder->Transform.Scale.X * 0.5F);
            CHECK(projectedYAtWall < occluder->Transform.Scale.Y * 0.5F);
        }
        else
        {
            const float targetFarDistance = object.Transform.Position.Z + radius - cameraZ;
            const float innerProjectedXAtWall =
                (std::abs(object.Transform.Position.X) - radius) * wallFrontDistance / targetFarDistance;
            CHECK(innerProjectedXAtWall > occluder->Transform.Scale.X * 0.5F);
        }
    }

    CHECK(hiddenTargetCount == 144U);
    CHECK(visibleControlCount == 16U);
    CHECK(sphereRendererCount == 160U);
    CHECK(totalRendererCount == 161U);

    const auto sphereGeometry = Keire::Detail::CreateBuiltinMeshGeometry(Keire::BuiltinMesh::Sphere);
    const auto cubeGeometry = Keire::Detail::CreateBuiltinMeshGeometry(Keire::BuiltinMesh::Cube);
    const auto sphereTriangles = sphereGeometry.second.size() / 3U;
    const auto cubeTriangles = cubeGeometry.second.size() / 3U;
    const auto candidateCount = sphereRendererCount + 1U;
    const auto candidateTriangles = static_cast<std::uint64_t>(sphereRendererCount) * sphereTriangles + cubeTriangles;
    const auto hiddenTargetTriangles = static_cast<std::uint64_t>(hiddenTargetCount) * sphereTriangles;
    CHECK(hiddenTargetCount >= Policy::AutomaticMinimumCandidates);
    CHECK(hiddenTargetTriangles >= Policy::AutomaticMinimumCandidateTriangles);
    CHECK(candidateCount >= Policy::AutomaticMinimumCandidates);
    CHECK(candidateTriangles >= Policy::AutomaticMinimumCandidateTriangles);
    CHECK(static_cast<double>(cubeTriangles) <=
          static_cast<double>(candidateTriangles) * Policy::AutomaticMaximumDepthCostRatio);

    const auto sphereBounds = CalculateBounds(sphereGeometry.first);
    const auto cubeBounds = CalculateBounds(cubeGeometry.first);
    const auto cameraWorld =
        Keire::Math::ComposeTransform(camera->Transform.Position, camera->Transform.Rotation, camera->Transform.Scale);
    const auto view = Keire::Math::Inverse(cameraWorld);
    constexpr std::array viewports{ViewportSize{1280U, 720U}, ViewportSize{1920U, 1080U}, ViewportSize{2560U, 1440U},
                                   ViewportSize{3840U, 2160U}};
    for (const auto viewport : viewports)
    {
        CAPTURE(viewport.Width);
        CAPTURE(viewport.Height);
        const auto projection = Keire::Math::Perspective(
            cameraData.at("fieldOfView").get<float>(), static_cast<float>(viewport.Width) / viewport.Height,
            cameraData.at("nearPlane").get<float>(), cameraData.at("farPlane").get<float>());
        const auto clipFromWorld = Keire::Math::Multiply(projection, view);
        const auto wallClip = ClipFromObject(clipFromWorld, *occluder);
        const auto wallRectangle = ProjectBounds(wallClip, cubeBounds, viewport);
        REQUIRE(wallRectangle.Area() >= Policy::AutomaticMinimumOccluderPixels);

        std::uint32_t safeOccluders = 1U;
        std::uint64_t depthTriangles = cubeTriangles;
        for (const auto* object : sphereObjects)
        {
            const auto clipFromSphere = ClipFromObject(clipFromWorld, *object);
            const auto rectangle = ProjectBounds(clipFromSphere, sphereBounds, viewport);
            if (rectangle.Area() >= Policy::AutomaticMinimumOccluderPixels)
            {
                ++safeOccluders;
                depthTriangles += sphereTriangles;
            }
        }
        for (const auto* object : sphereObjects)
        {
            CHECK(Keire::RenderBackend::GeometryDetail::IntersectsFrustum(ClipFromObject(clipFromWorld, *object),
                                                                          sphereBounds));
        }

        CHECK(safeOccluders == 1U);
        CHECK(depthTriangles == cubeTriangles);
        CHECK(static_cast<double>(depthTriangles) <=
              static_cast<double>(candidateTriangles) * Policy::AutomaticMaximumDepthCostRatio);
        const auto coveredCells = CountCoveredCells(wallRectangle, viewport);
        CHECK(static_cast<float>(coveredCells) /
                  static_cast<float>(Policy::AutomaticCoverageColumns * Policy::AutomaticCoverageRows) >=
              Policy::AutomaticMinimumOccluderCoverage);
    }
}

TEST_CASE("Sandbox GPU occlusion stress scene uses distinct opaque occlusion-compatible presentation materials")
{
    const auto sceneSource = KeireTests::ReadFile(std::filesystem::current_path() / StressScenePath);
    const auto sceneMetadataSource = KeireTests::ReadFile(std::filesystem::current_path() / StressSceneMetadataPath);
    CHECK(sceneSource == KeireTests::ReadFile(std::filesystem::current_path() / StressSceneTemplatePath));
    CHECK(sceneMetadataSource ==
          KeireTests::ReadFile(std::filesystem::current_path() / StressSceneTemplateMetadataPath));

    const auto sceneMetadata = nlohmann::json::parse(sceneMetadataSource);
    CHECK(sceneMetadata.at("importer") == "Keire.Scene");
    REQUIRE(sceneMetadata.at("dependencies").size() == PresentationMaterials.size());
    for (const auto& presentation : PresentationMaterials)
    {
        CHECK(std::ranges::any_of(sceneMetadata.at("dependencies"), [&](const nlohmann::json& value)
                                  { return value.get<std::string>() == presentation.Material; }));
    }

    const auto project =
        nlohmann::json::parse(KeireTests::ReadFile(std::filesystem::current_path() / SandboxProjectPath));
    const auto startupSceneMetadata =
        nlohmann::json::parse(KeireTests::ReadFile(std::filesystem::current_path() / SandboxStartupSceneMetadataPath));
    CHECK(project.at("startupScene") == startupSceneMetadata.at("id"));
    CHECK(project.at("startupScene") != sceneMetadata.at("id"));

    const auto renderingSettings =
        nlohmann::json::parse(KeireTests::ReadFile(std::filesystem::current_path() / SandboxRenderingSettingsPath));
    CHECK(renderingSettings.at("gpuOcclusion") == "automatic");

    const auto buildScenesSource = KeireTests::ReadFile(std::filesystem::current_path() / SandboxBuildScenesPath);
    CHECK(buildScenesSource == KeireTests::ReadFile(std::filesystem::current_path() / SandboxTemplateBuildScenesPath));
    const auto buildScenes = nlohmann::json::parse(buildScenesSource);
    REQUIRE(buildScenes.at("scenes").size() == 4U);
    CHECK(buildScenes.at("scenes").at(3).at("scene") == sceneMetadata.at("id"));
    CHECK(buildScenes.at("scenes").at(3).at("enabled").get<bool>());

    for (const auto& presentation : PresentationMaterials)
    {
        CAPTURE(presentation.MaterialGraphPath);
        const auto materialMetadata = nlohmann::json::parse(
            KeireTests::ReadFile(std::filesystem::current_path() / presentation.MaterialMetadataPath));
        CHECK(std::ranges::any_of(materialMetadata.at("subAssets"), [&](const nlohmann::json& value)
                                  { return value.get<std::string>() == presentation.Material; }));

        const auto materialGraph = nlohmann::json::parse(
            KeireTests::ReadFile(std::filesystem::current_path() / presentation.MaterialGraphPath));
        CHECK(materialGraph.at("surface").at("alphaMode").get<std::uint32_t>() == 0U);
        CHECK_FALSE(materialGraph.at("surface").at("doubleSided").get<bool>());
        CHECK(materialGraph.at("shader").at("kind") == "graph");
        const auto colorProperty =
            std::ranges::find_if(materialGraph.at("properties"), [&](const nlohmann::json& property)
                                 { return property.at("name").get<std::string>() == presentation.ColorProperty; });
        REQUIRE(colorProperty != materialGraph.at("properties").end());
        const auto color = colorProperty->at("value").get<std::array<float, 4>>();
        for (std::size_t channel = 0; channel < color.size(); ++channel)
            CHECK(color[channel] == doctest::Approx(presentation.ExpectedColor[channel]));

        const auto shaderSource = KeireTests::ReadFile(std::filesystem::current_path() / presentation.ShaderGraphPath);
        const auto shaderGraph =
            Keire::ShaderGraphAsset::DecodeSource(std::as_bytes(std::span(shaderSource.data(), shaderSource.size())));
        const auto compilation = Keire::CompileShaderGraph(shaderGraph);
        REQUIRE(compilation.Succeeded());
        REQUIRE(compilation.Variants.size() == 1U);
        const auto manifest = nlohmann::json::parse(compilation.Variants.front().Manifest);
        CHECK(manifest.at("instanceAddressingAbiVersion").get<std::uint32_t>() == 2U);
        const auto renderState = manifest.at("renderState");
        CHECK(renderState.at("topology") == "TriangleList");
        CHECK(renderState.at("depthTest").get<bool>());
        CHECK(renderState.at("depthWrite").get<bool>());
        CHECK_FALSE(renderState.at("blend").get<bool>());
        const auto support = manifest.at("occlusionSupport").get<std::uint32_t>();
        CHECK(Keire::HasShaderOcclusionSupport(static_cast<Keire::ShaderOcclusionSupport>(support),
                                               Keire::ShaderOcclusionSupport::ConservativeBounds));
        CHECK(Keire::HasShaderOcclusionSupport(static_cast<Keire::ShaderOcclusionSupport>(support),
                                               Keire::ShaderOcclusionSupport::DepthOnlyGeometryMatch));
    }
}
