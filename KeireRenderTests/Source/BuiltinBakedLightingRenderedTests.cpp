#include "Keire/Application.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireRenderTests/RenderAssetFixture.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    struct LightingParityCapture
    {
        std::array<std::vector<std::uint8_t>, 2> Frames;
    };

    class LightingParityLayer final : public Keire::Layer
    {
      public:
        LightingParityLayer(std::shared_ptr<std::vector<LightingParityCapture>> results,
                            const Keire::RenderAntiAliasingMode antiAliasing, const Keire::AssetId material = {})
            : Layer("Default material render-path parity"), m_Results(std::move(results)), m_AntiAliasing(antiAliasing),
              m_Material(material)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Lighting parity"));
            for (int index = 0; index < 2; ++index)
            {
                auto cube = m_Scene->CreateEntity("Default cube");
                cube.GetComponent<Keire::TransformComponent>()->SetLocalPosition(
                    {index == 0 ? -0.65F : 0.65F, 0.0F, 0.0F});
                cube.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.9F, 1.4F, 0.9F});
                m_Receivers[index] = cube.AddComponent<Keire::MeshRendererComponent>();
                m_Receivers[index]->SetMaterial(m_Material);
                m_Receivers[index]->SetTint({64.0F / 255.0F, 140.0F / 255.0F, 1.0F, 1.0F});
            }
            m_Sun = m_Scene->CreateEntity("Sun").AddComponent<Keire::DirectionalLightComponent>();
            m_Sun->SetShadows(Keire::ShadowQuality::Disabled);
            auto point = m_Scene->CreateEntity("Point");
            point.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.0F, -2.0F});
            m_Point = point.AddComponent<Keire::PointLightComponent>();
            m_Point->SetShadows(Keire::ShadowQuality::Disabled);
            auto spot = m_Scene->CreateEntity("Spot");
            spot.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, -2.0F});
            m_Spot = spot.AddComponent<Keire::SpotLightComponent>();
            m_Spot->SetConeAngles(70.0F, 100.0F);
            m_Spot->SetShadows(Keire::ShadowQuality::Disabled);
            for (auto& view : m_Views)
            {
                Keire::RenderSurfaceSpecification surface;
                surface.Width = SurfaceSize;
                surface.Height = SurfaceSize;
                surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
                surface.SampleCount = Keire::RenderSampleCount::One;
                view = Owner().Renderer()->CreateView(surface);
                Keire::RenderCamera camera;
                camera.View = Keire::Math::LookAt({0.0F, 0.3F, -3.0F}, {}, {0.0F, 1.0F, 0.0F});
                camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
                camera.ClearColor = surface.ClearColor;
                view->SetCamera(camera);
            }
        }

        void OnDetach() noexcept override
        {
            m_Views = {};
            m_Receivers = {};
            m_Sun.Reset();
            m_Point.Reset();
            m_Spot.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame >= 120U && (m_Frame - 120U) % 12U == 0U)
            {
                LightingParityCapture capture;
                for (std::size_t index = 0; index < m_Views.size(); ++index)
                    capture.Frames[index] = Keire::RenderSystemInternalAccess::ReadbackRGBA8(
                        *Owner().Renderer(), *m_Views[index]->Surface());
                m_Results->push_back(std::move(capture));
                if (++m_Case == 30U)
                {
                    Owner().RequestExit();
                    return;
                }
            }
            const auto channel = m_Case % 10U;
            const bool combined = channel == 7U || channel == 9U;
            m_Sun->SetEnabled(channel == 2U || combined);
            m_Sun->SetIntensity(2.0F);
            m_Point->SetEnabled(channel == 3U || channel == 8U || combined);
            m_Point->SetIntensity(channel == 8U ? 200.0F : 12.0F);
            m_Spot->SetEnabled(channel == 4U || combined);
            m_Spot->SetIntensity(8.0F);
            for (auto& receiver : m_Receivers)
                receiver->SetReceiveShadows(channel != 9U);
            Keire::RenderEnvironmentSettings environment;
            environment.SkyVisible = false;
            environment.Exposure = std::array{0.35F, 1.0F, 2.2F}[m_Case / 10U];
            environment.AmbientIntensity = channel == 1U || combined ? 0.4F : 0.0F;
            environment.EnvironmentDiffuseIntensity = channel == 5U || combined ? 0.8F : 0.0F;
            environment.EnvironmentSpecularIntensity = channel == 6U || combined ? 0.7F : 0.0F;
            environment.RequestedGlobalIllumination = Keire::GlobalIlluminationMode::Realtime;
            environment.RequestedAntiAliasing = m_AntiAliasing;
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                environment.RequestedRenderPath =
                    index == 0U ? Keire::RenderPath::ForwardPlus : Keire::RenderPath::DeferredHybrid;
                const auto selection =
                    Keire::ResolveRenderFeatureSelection(environment, Owner().Renderer()->FeatureCapabilities());
                if (m_Frame == 0U)
                {
                    REQUIRE(selection.EffectivePath == environment.RequestedRenderPath);
                    REQUIRE(selection.EffectiveAntiAliasing == m_AntiAliasing);
                }
                m_Views[index]->Surface()->RequestSampleCount(Keire::ResolveRenderSurfaceSampleCount(selection));
                Owner().Renderer()->Submit({m_Scene, m_Views[index], false, environment});
            }
            ++m_Frame;
        }

      private:
        std::shared_ptr<std::vector<LightingParityCapture>> m_Results;
        Keire::RenderAntiAliasingMode m_AntiAliasing;
        Keire::AssetId m_Material;
        Keire::Ref<Keire::Scene> m_Scene;
        std::array<Keire::Ref<Keire::RenderView>, 2> m_Views;
        std::array<Keire::Ref<Keire::MeshRendererComponent>, 2> m_Receivers;
        Keire::Ref<Keire::DirectionalLightComponent> m_Sun;
        Keire::Ref<Keire::PointLightComponent> m_Point;
        Keire::Ref<Keire::SpotLightComponent> m_Spot;
        std::uint32_t m_Frame = 0;
        std::uint32_t m_Case = 0;
    };

    struct BakedCaptureResults
    {
        std::vector<std::vector<std::uint8_t>> Frames;
    };

    class BakedCaptureLayer final : public Keire::Layer
    {
      public:
        BakedCaptureLayer(Keire::Ref<Keire::Scene> scene, const Keire::RenderPath path,
                          std::shared_ptr<BakedCaptureResults> results, const float exposure = 1.0F)
            : Layer("Built-in baked-lighting capture"), m_Scene(std::move(scene)), m_Path(path),
              m_Results(std::move(results)), m_Exposure(exposure)
        {
        }

      protected:
        void OnAttach() override
        {
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "Built-in baked-lighting layers";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, -3.0F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            // Allow asynchronous lighting assets to settle before evaluating actual output, then exercise
            // mode changes and removal of the binding without changing any geometry or direct lights.
            if (m_Frame == 120U || m_Frame == 132U || m_Frame == 144U)
            {
                m_Results->Frames.push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                if (m_Frame == 132U)
                    m_Scene->SetBakedLighting({});
                if (m_Frame == 144U)
                {
                    Owner().RequestExit();
                    return;
                }
            }
            Keire::RenderEnvironmentSettings environment;
            environment.RequestedRenderPath = m_Path;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            environment.Exposure = m_Exposure;
            environment.RequestedGlobalIllumination = m_Frame >= 120U && m_Frame < 132U
                                                          ? Keire::GlobalIlluminationMode::Disabled
                                                          : Keire::GlobalIlluminationMode::Baked;
            environment.AmbientIntensity = 0.0F;
            environment.EnvironmentDiffuseIntensity = 0.0F;
            environment.EnvironmentSpecularIntensity = 0.0F;
            environment.SkyVisible = false;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::RenderPath m_Path;
        std::shared_ptr<BakedCaptureResults> m_Results;
        float m_Exposure;
        std::uint32_t m_Frame = 0;
    };

    struct BakedFixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-BakedRenderTests-" + Keire::AssetId::Generate().ToString());
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::Ref<Keire::Scene> Scene;
        std::filesystem::path Catalog;

        BakedFixture()
        {
            std::filesystem::create_directories(Root / "Assets");
            const auto textures = Keire::CreateLightingTextureArrayAssetImporter();
            const auto sets = Keire::CreateLightingSetAssetImporter();
            Database = Keire::CreateRef<Keire::AssetDatabase>(
                Keire::AssetDatabaseSpecification{.ProjectRoot = Root, .Importers = {textures, sets}});
            Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                   Keire::SceneAsset::EmptyDefinition("Baked-only default meshes"));
            Keire::LightingTextureArrayDefinition texture;
            texture.Encoding = Keire::LightingTextureEncoding::Rgbe8;
            // Equal-energy red and green layers. The objects share geometry and an absent material, so an
            // incorrect instance merge or a missing built-in lightmap binding cannot pass this comparison.
            texture.Mips.push_back({1U,
                                    1U,
                                    2U,
                                    {std::byte{128}, std::byte{0}, std::byte{0}, std::byte{129}, std::byte{0},
                                     std::byte{128}, std::byte{0}, std::byte{129}}});
            Keire::LightingSetDefinition lighting;
            lighting.Scene = Scene->Asset();
            lighting.InputFingerprint = std::string(64U, 'a');
            lighting.Lightmaps = Database->CreateAsset("Layers.keirelightingtexture", textures,
                                                       Keire::LightingTextureArrayAsset::Encode(texture));
            for (std::uint32_t layer = 0; layer < 2U; ++layer)
            {
                auto entity = Scene->CreateEntity(layer == 0U ? "Red lightmap receiver" : "Green lightmap receiver");
                const auto transform = entity.GetComponent<Keire::TransformComponent>();
                transform->SetLocalPosition({layer == 0U ? -0.65F : 0.65F, 0.0F, 0.0F});
                transform->SetLocalScale({0.8F, 0.8F, 0.8F});
                const auto renderer = entity.AddComponent<Keire::MeshRendererComponent>();
                renderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
                renderer->SetStaticLighting(true);
                renderer->SetGIReceive(Keire::GIReceiveMode::Lightmaps);
                lighting.Renderers.push_back({entity.Id().Value(), layer});
            }
            Scene->SetBakedLighting(
                Database->CreateAsset("Lighting.keirelighting", sets, Keire::LightingSetAsset::Encode(lighting)));
            Catalog = Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        }

        ~BakedFixture()
        {
            if (Scene)
                Scene->Close();
            Scene.Reset();
            Database.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };
} // namespace

TEST_CASE("default material brightness matches Forward+ and Deferred across lighting and exposure")
{
    for (const auto antiAliasing :
         {Keire::RenderAntiAliasingMode::None, Keire::RenderAntiAliasingMode::Fxaa, Keire::RenderAntiAliasingMode::Taa,
          Keire::RenderAntiAliasingMode::Msaa2, Keire::RenderAntiAliasingMode::Msaa4})
    {
        CAPTURE(static_cast<int>(antiAliasing));
        const auto results = std::make_shared<std::vector<LightingParityCapture>>();
        {
            Keire::Application application(RenderTestSpecification());
            (void)application.PushLayer(std::make_unique<LightingParityLayer>(results, antiAliasing));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE(results->size() == 30U);
        constexpr std::array channels{
            "dark",     "ambient",   "directional",     "point", "spot", "environment diffuse", "environment specular",
            "combined", "HDR point", "shadows disabled"};
        for (std::size_t index = 0; index < results->size(); ++index)
        {
            INFO("Lighting channel: ", channels[index % channels.size()]);
            const float exposure = std::array{0.35F, 1.0F, 2.2F}[index / channels.size()];
            CAPTURE(exposure);
            const auto& forward = (*results)[index].Frames[0];
            const auto& deferred = (*results)[index].Frames[1];
            REQUIRE(forward.size() == SurfaceSize * SurfaceSize * 4U);
            REQUIRE(deferred.size() == forward.size());
            double absoluteError = 0.0;
            for (std::size_t offset = 0; offset < forward.size(); offset += 4U)
                for (std::size_t channel = 0; channel < 3U; ++channel)
                    absoluteError += std::abs(static_cast<int>(forward[offset + channel]) -
                                              static_cast<int>(deferred[offset + channel]));
            const auto meanError = absoluteError / (SurfaceSize * SurfaceSize * 3.0 * 255.0);
            INFO("Forward luminance: ", MeasureCenter(forward).Luminance(),
                 "; Deferred luminance: ", MeasureCenter(deferred).Luminance());
            CHECK(meanError < 0.006);
            if (index % channels.size() == 0U)
            {
                CHECK(MeasureCenter(forward).Luminance() < 0.001F);
                CHECK(MeasureCenter(deferred).Luminance() < 0.001F);
            }
            else
            {
                CHECK(MeasureCenter(forward).Luminance() > 0.005F);
                CHECK(MeasureCenter(deferred).Luminance() > 0.005F);
            }
        }
    }
}

TEST_CASE("graph material brightness matches Forward+ and Deferred across lighting and exposure")
{
    KeireRenderTests::Detail::RenderAssetFixture assets(true);
    const auto results = std::make_shared<std::vector<LightingParityCapture>>();
    {
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<LightingParityLayer>(results, Keire::RenderAntiAliasingMode::None,
                                                                          assets.ShaderGraphMaterial));
        REQUIRE(application.Run() == 0);
    }
    REQUIRE(results->size() == 30U);
    for (std::size_t index = 0; index < results->size(); ++index)
    {
        CAPTURE(index);
        const auto& forward = (*results)[index].Frames[0];
        const auto& deferred = (*results)[index].Frames[1];
        REQUIRE(forward.size() == SurfaceSize * SurfaceSize * 4U);
        REQUIRE(deferred.size() == forward.size());
        double error = 0.0;
        for (std::size_t offset = 0; offset < forward.size(); offset += 4U)
            for (std::size_t channel = 0; channel < 3U; ++channel)
                error += std::abs(static_cast<int>(forward[offset + channel]) -
                                  static_cast<int>(deferred[offset + channel]));
        CHECK(error / (SurfaceSize * SurfaceSize * 3.0 * 255.0) < 0.006);
        if (index % 10U != 0U)
        {
            CHECK(MeasureCenter(forward).Green > 0.005F);
            CHECK(MeasureCenter(deferred).Green > 0.005F);
        }
    }
}

TEST_CASE("built-in meshes consume distinct RGBE lightmap layers and clear stale GI bindings")
{
    for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
    {
        CAPTURE(static_cast<int>(path));
        BakedFixture assets;
        const auto results = std::make_shared<BakedCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(specification);
            (void)application.PushLayer(std::make_unique<BakedCaptureLayer>(assets.Scene, path, results));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE(results->Frames.size() == 3U);
        const auto& baked = results->Frames[0];
        REQUIRE(baked.size() == SurfaceSize * SurfaceSize * 4U);
        std::array<std::size_t, 2> dominantPixels{};
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = 0; x < SurfaceSize; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * SurfaceSize + x) * 4U;
                const int red = baked[offset];
                const int green = baked[offset + 1U];
                const int blue = baked[offset + 2U];
                if (x < SurfaceSize / 2U && red > green + 20 && red > blue + 20)
                    ++dominantPixels[0];
                if (x >= SurfaceSize / 2U && green > red + 20 && green > blue + 20)
                    ++dominantPixels[1];
            }
        }
        CHECK(dominantPixels[0] > 100U);
        CHECK(dominantPixels[1] > 100U);
        CHECK(MeasureCenter(results->Frames[1]).Luminance() < 0.01F);
        CHECK(MeasureCenter(results->Frames[2]).Luminance() < 0.01F);
    }
}

TEST_CASE("default material baked-light exposure matches both render paths")
{
    for (const float exposure : {0.35F, 2.2F})
    {
        CAPTURE(exposure);
        std::array<PixelStatistics, 2> brightness;
        std::size_t index = 0;
        for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
        {
            BakedFixture assets;
            const auto results = std::make_shared<BakedCaptureResults>();
            auto specification = RenderTestSpecification();
            specification.Assets.Mode = Keire::AssetMode::Development;
            specification.Assets.DevelopmentCatalog = assets.Catalog;
            {
                Keire::Application application(specification);
                (void)application.PushLayer(std::make_unique<BakedCaptureLayer>(assets.Scene, path, results, exposure));
                REQUIRE(application.Run() == 0);
            }
            REQUIRE(results->Frames.size() == 3U);
            brightness[index++] = MeasureCenter(results->Frames[0]);
        }
        CHECK(brightness[0].Luminance() > 0.02F);
        CHECK(brightness[0].Red == doctest::Approx(brightness[1].Red).epsilon(0.02));
        CHECK(brightness[0].Green == doctest::Approx(brightness[1].Green).epsilon(0.02));
        CHECK(brightness[0].Blue == doctest::Approx(brightness[1].Blue).epsilon(0.02));
    }
}
