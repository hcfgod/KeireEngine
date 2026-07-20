#include "Keire/Application.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;
    constexpr float ColorTolerance = 0.04F;
    constexpr float MinimumBehaviorDelta = 0.08F;

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
                ++count;
            }
        }
        result.Red /= static_cast<float>(count);
        result.Green /= static_cast<float>(count);
        result.Blue /= static_cast<float>(count);
        return result;
    }

    struct CaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
    };

    class RenderAssetFixture final
    {
      public:
        [[nodiscard]] static std::vector<std::byte> SolidTexture(const std::uint8_t red, const std::uint8_t green,
                                                                 const std::uint8_t blue)
        {
            Keire::TextureImportSettings textureSettings;
            textureSettings.ColorSpace = Keire::TextureColorSpace::Linear;
            textureSettings.Mips = Keire::TextureMipPolicy::None;
            Keire::TextureMipLevel mip;
            mip.Width = 2;
            mip.Height = 2;
            for (std::size_t pixel = 0; pixel < 4; ++pixel)
            {
                mip.Pixels.push_back(static_cast<std::byte>(red));
                mip.Pixels.push_back(static_cast<std::byte>(green));
                mip.Pixels.push_back(static_cast<std::byte>(blue));
                mip.Pixels.push_back(std::byte{255});
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
            Keire::AssetImporterRegistration textureImporter;
            textureImporter.Name = "KeireTests.Texture";
            textureImporter.Type = Keire::Texture2DAsset::StaticType();
            textureImporter.Extensions = {".texture"};
            textureImporter.Import = [](const std::span<const std::byte> bytes)
            { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
            Database = Keire::CreateRef<Keire::AssetDatabase>(Keire::AssetDatabaseSpecification{
                .ProjectRoot = Root,
                .Importers = std::vector<Keire::AssetImporterRegistration>{meshImporter, shaderImporter,
                                                                           materialImporter, textureImporter}});
            const std::array vertices{Keire::MeshVertex{{-0.9F, -0.8F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}},
                                      Keire::MeshVertex{{0.9F, -0.8F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}},
                                      Keire::MeshVertex{{0.0F, 0.9F, 0.0F}, {0.0F, 0.0F, 1.0F}, {}, {}}};
            const std::array<std::uint32_t, 3> indices{0, 1, 2};
            Mesh =
                Database->CreateAsset("Triangle.keiremesh", meshImporter, Keire::MeshAsset::Encode(vertices, indices));

            TexturePath = Root / "Assets/Green.texture";
            Texture = Database->CreateAsset("Green.texture", textureImporter, SolidTexture(0, 255, 0));

            const auto shaderDirectory = Root / "Assets/Shaders";
            std::filesystem::create_directories(shaderDirectory);
            ShaderSourcePath = shaderDirectory / "DefaultUnlit.hlsl";
            std::filesystem::copy_file("Samples/KeireSandbox/Assets/Shaders/DefaultUnlit.hlsl", ShaderSourcePath);
            const std::string shaderManifest = R"({
  "schemaVersion": 1,
  "source": "Assets/Shaders/DefaultUnlit.hlsl",
  "stages": {"vertex": "VSMain", "fragment": "PSMain"},
  "includeRoots": ["Assets/Shaders"],
  "renderState": {"topology": "TriangleList", "culling": "None", "depthTest": true, "depthWrite": true, "blend": false},
  "properties": [
    {"name": "Tint", "type": "Color", "default": [1, 1, 1, 1]},
    {"name": "MainTexture", "type": "Texture2D", "default": null}
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
            Catalog = Database->ImportAll().CatalogPath;
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
            const std::string manifest =
                "{\"schemaVersion\":1,\"shader\":\"" + Shader.ToString() + "\",\"properties\":{\"Tint\":[" +
                std::to_string(tint.Red) + "," + std::to_string(tint.Green) + "," + std::to_string(tint.Blue) + "," +
                std::to_string(tint.Alpha) + "],\"MainTexture\":\"" + Texture.ToString() + "\"}}";
            std::ofstream stream(MaterialPath, std::ios::binary | std::ios::trunc);
            stream << manifest;
            stream.close();
            return stream && ReloadAsset(application, Material);
        }

        [[nodiscard]] bool ReplaceShaderOutputSwizzle(Keire::Application& application)
        {
            std::ifstream input(ShaderSourcePath, std::ios::binary);
            std::string source(std::istreambuf_iterator<char>(input), {});
            constexpr std::string_view original = "surface.rgb * lighting";
            const auto position = source.find(original);
            if (!input || position == std::string::npos)
                return false;
            source.replace(position, original.size(), "surface.brg * lighting");
            std::ofstream output(ShaderSourcePath, std::ios::binary | std::ios::trunc);
            output << source;
            output.close();
            return output && ReloadAsset(application, Shader);
        }

      private:
        [[nodiscard]] bool ReloadAsset(Keire::Application& application, const Keire::AssetId id)
        {

            Catalog = Database->ImportAll().CatalogPath;
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
        Keire::AssetId Material;
        Keire::AssetId Shader;
        Keire::AssetId Texture;
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
            surface.SampleCount = Keire::RenderSampleCount::One;
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
            if (m_Scene)
                m_Scene->Close();
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
            if (m_Scene)
                m_Scene->Close();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
            if (m_Results->Frames.size() == 6)
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
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Submitted = false;
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
            if (m_Scene)
                m_Scene->Close();
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
                    Owner().RequestExit();
                    return;
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
        int m_Stage = 0;
        bool m_Submitted = false;
    };

    [[nodiscard]] Keire::ApplicationSpecification RenderTestSpecification()
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire rendered output tests";
        specification.MainWindow.Width = SurfaceSize;
        specification.MainWindow.Height = SurfaceSize;
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
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

    REQUIRE(results->Frames.size() == 6);
    const auto first = MeasureCenter(results->Frames.front());
    const auto last = MeasureCenter(results->Frames.back());
    CHECK(first.Red > first.Green + MinimumBehaviorDelta);
    CHECK(first.Blue > first.Green + MinimumBehaviorDelta);
    CHECK(last.Green > last.Red + MinimumBehaviorDelta);
    CHECK(last.Green > last.Blue + MinimumBehaviorDelta);
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
}
