#include "Keire/Core.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "KeireInternal/Environment/EnvironmentCloudPhase.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace
{
    class EnvironmentCaptureLayer final : public Keire::Layer
    {
      public:
        explicit EnvironmentCaptureLayer(bool& checked) : Layer("environment-capture"), m_Checked(checked) {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                        Keire::SceneAsset::EmptyDefinition("environment-capture"));
            auto entity = scene->CreateEntity("Authored directional");
            REQUIRE(entity.AddComponent<Keire::DirectionalLightComponent>());
            auto view = Owner().Renderer()->CreateView({.Name = "environment-capture", .Width = 32, .Height = 32});
            auto* surface = static_cast<Keire::RenderBackend::RenderSurfaceState*>(
                Keire::RenderSystemInternalAccess::SurfaceState(*view->Surface()));
            REQUIRE(surface);
            const auto state = surface->Owner.lock();
            REQUIRE(state);
            const Keire::RenderBackend::RenderSurfaceToken token{surface->Id, surface->Epoch, surface->Lifetime};
            Keire::SceneRenderRequest request{scene, view};
            request.EvaluatedEnvironment = Keire::EnvironmentLightingSnapshot{
                Keire::AssetId(1, 2), 1,
                Keire::EnvironmentDirectionalLight{{0.0F, -1.0F, 0.0F}, {0.2F, 0.4F, 0.6F}, 3.0F}};
            const auto capture = [&](const std::uint64_t frame)
            {
                state->CaptureRequests.clear();
                state->CapturePendingSceneRequest({request, token}, frame);
                REQUIRE(state->CaptureRequests.size() == 1);
                return state->CaptureRequests.front().Packet;
            };
            auto packet = capture(1);
            CHECK(packet.Environment.AmbientIntensity == request.Environment.AmbientIntensity);
            request.EvaluatedEnvironment->Ambient = Keire::EnvironmentAmbientLight{{0.1F, 0.2F, 0.3F}, 0.05F};
            packet = capture(1);
            CHECK(packet.Environment.AmbientIntensity == 0.05F);
            CHECK(packet.Environment.AmbientColor.Red == 0.1F);
            CHECK(request.Environment.AmbientIntensity == Keire::RenderEnvironmentSettings{}.AmbientIntensity);
            request.EvaluatedEnvironment->Ambient.reset();
            CHECK(packet.Lighting.Shadows == Keire::ShadowQuality::Disabled);
            request.EvaluatedEnvironment->Directional->Shadows = {Keire::ShadowQuality::Soft,
                                                                  Keire::ShadowResolutionHint::Medium, 0.7F, 0.02F};
            packet = capture(1);
            CHECK(packet.Lighting.Shadows == Keire::ShadowQuality::Soft);
            CHECK(packet.Lighting.ShadowResolution == Keire::ShadowResolutionHint::Medium);
            CHECK(packet.Lighting.ShadowStrength == 0.7F);
            CHECK(packet.Lighting.ShadowBias == 0.02F);
            CHECK_FALSE(packet.Lighting.Cookie);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            CHECK_FALSE(packet.Lighting.Entity);
            CHECK(packet.Lighting.Enabled);
            CHECK(packet.Lighting.ColorAndIntensity.Red == 0.2F);
            CHECK(packet.Lighting.ColorAndIntensity.Alpha == 3.0F);
            CHECK(capture(2).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Revision = 2;
            CHECK_FALSE(capture(3).TemporalHistoryContinuous);
            CHECK(capture(4).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Source = Keire::AssetId(3, 4);
            CHECK_FALSE(capture(5).TemporalHistoryContinuous);
            CHECK(capture(6).TemporalHistoryContinuous);
            const Keire::RenderBackend::MotionHistoryKey key{{}, {}, token.Id, token.Epoch};
            request.EvaluatedEnvironment->Directional->Intensity = -1.0F;
            CHECK_THROWS_AS(capture(7), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 6);
            CHECK(state->CaptureRequests.empty());
            request.EvaluatedEnvironment->Directional->Intensity = 0.0F;
            request.EvaluatedEnvironment->Directional->Shadows.Bias = -1.0F;
            CHECK_THROWS_AS(capture(7), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 6);
            CHECK(state->CaptureRequests.empty());
            request.EvaluatedEnvironment->Directional->Shadows.Bias = 0.02F;
            packet = capture(7);
            CHECK(packet.TemporalHistoryContinuous);
            CHECK(packet.Lighting.Enabled);
            CHECK_FALSE(packet.Lighting.Entity);
            CHECK(packet.Lighting.ColorAndIntensity.Alpha == 0.0F);
            CHECK(packet.Lighting.Shadows == Keire::ShadowQuality::Soft);
            CHECK(packet.Lighting.ShadowStrength == 0.7F);
            request.EvaluatedEnvironment->Directional.reset();
            packet = capture(8);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            CHECK(packet.Lighting.Entity == entity.Id());
            CHECK(packet.Lighting.Enabled);
            CHECK(packet.Lighting.ColorAndIntensity.Alpha == 1.0F);
            CHECK(packet.Lighting.Shadows == Keire::ShadowQuality::Soft);
            CHECK(packet.Lighting.ShadowResolution == Keire::ShadowResolutionHint::High);
            CHECK(packet.Lighting.ShadowStrength == 1.0F);
            CHECK(packet.Lighting.ShadowBias == 0.005F);
            request.EvaluatedEnvironment.reset();
            CHECK_FALSE(capture(9).TemporalHistoryContinuous);
            CHECK(capture(10).TemporalHistoryContinuous);
            request.Environment.Atmosphere.Enabled = true;
            CHECK_FALSE(capture(11).TemporalHistoryContinuous);
            CHECK(capture(12).TemporalHistoryContinuous);
            request.Environment.Atmosphere.Height += 1000.0;
            CHECK_FALSE(capture(13).TemporalHistoryContinuous);
            CHECK(capture(14).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Enabled = true;
            CHECK_FALSE(capture(15).TemporalHistoryContinuous);
            CHECK(capture(16).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Coverage = 0.7;
            CHECK_FALSE(capture(17).TemporalHistoryContinuous);
            CHECK(capture(18).TemporalHistoryContinuous);
            const auto acceptedHistory = state->CameraMotionHistory.at(key);
            const auto validRadius = request.Environment.Atmosphere.PlanetRadius;
            request.Environment.Atmosphere.PlanetRadius = -1.0;
            CHECK_THROWS_AS(capture(19), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == acceptedHistory.Frame);
            CHECK(state->CameraMotionHistory.at(key).Atmosphere == acceptedHistory.Atmosphere);
            CHECK(state->CaptureRequests.empty());
            request.Environment.Atmosphere.PlanetRadius = validRadius;
            request.Environment.Clouds.Layers[0].Coverage = 2.0;
            CHECK_THROWS_AS(capture(19), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == acceptedHistory.Frame);
            CHECK(state->CameraMotionHistory.at(key).Clouds == acceptedHistory.Clouds);
            CHECK(state->CaptureRequests.empty());
            request.Environment.Clouds.Layers[0].Coverage = 0.7;
            request.EvaluatedEnvironment = Keire::EnvironmentLightingSnapshot{
                Keire::AssetId(3, 4), 2,
                Keire::EnvironmentDirectionalLight{{0.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 3.0F}};
            CHECK_FALSE(capture(19).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Directional->Direction = {0.0F, 0.0F, -1.0F};
            request.EvaluatedEnvironment->Directional->Intensity = 0.5F;
            request.EvaluatedEnvironment->Ambient = Keire::EnvironmentAmbientLight{{0.1F, 0.2F, 0.3F}, 0.05F};
            CHECK(capture(20).TemporalHistoryContinuous);
            request.MaterialTimeSeconds = 10.0F;
            CHECK(capture(21).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Time = Keire::EnvironmentTimeSample{1, 0.25, 1200.0};
            packet = capture(22);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            const auto firstPhase = packet.CloudAdvectionPhase;
            CHECK(firstPhase ==
                  Keire::Detail::EvaluateEnvironmentCloudPhase(request.Environment.Clouds, {1, 0.25, 1200.0}));
            request.EvaluatedEnvironment->Time->DayFraction = 0.5;
            packet = capture(23);
            CHECK(packet.TemporalHistoryContinuous);
            CHECK(packet.CloudAdvectionPhase != firstPhase);
            CHECK(packet.CloudAdvectionPhase ==
                  Keire::Detail::EvaluateEnvironmentCloudPhase(request.Environment.Clouds, {1, 0.5, 1200.0}));
            request.EvaluatedEnvironment->Time->Day = 2;
            CHECK(capture(24).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Time->SecondsPerDay = 2400.0;
            CHECK_FALSE(capture(25).TemporalHistoryContinuous);
            CHECK(capture(26).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Time->DayFraction = 1.0;
            CHECK_THROWS_AS(capture(27), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 26);
            CHECK(state->CameraMotionHistory.at(key).EnvironmentSecondsPerDay == 2400.0);
            CHECK(state->CaptureRequests.empty());
            request.EvaluatedEnvironment->Time->DayFraction = 0.5;
            CHECK(capture(27).TemporalHistoryContinuous);
            request.EvaluatedEnvironment->Time.reset();
            packet = capture(28);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            const std::array<std::array<double, 2>, Keire::MaximumCloudLayers> stationaryPhase{};
            CHECK(packet.CloudAdvectionPhase == stationaryPhase);
            CHECK(capture(29).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Appearance = Keire::CloudAppearance::LegacyUnlit;
            CHECK_FALSE(capture(30).TemporalHistoryContinuous);
            CHECK(capture(31).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Appearance = Keire::CloudAppearance::NaturalLit;
            CHECK_FALSE(capture(32).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Seed = 42;
            CHECK_FALSE(capture(33).TemporalHistoryContinuous);
            CHECK(capture(34).TemporalHistoryContinuous);
            request.Environment.Clouds.Layers[0].Appearance = static_cast<Keire::CloudAppearance>(255);
            CHECK_THROWS_AS(capture(35), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 34);
            CHECK(state->CameraMotionHistory.at(key).Clouds.Layers[0].Seed == 42);
            CHECK(state->CaptureRequests.empty());
            request.Environment.Clouds.Layers[0].Appearance = Keire::CloudAppearance::NaturalLit;
            request.Environment.Bloom.Enabled = true;
            CHECK_FALSE(capture(35).TemporalHistoryContinuous);
            CHECK(capture(36).TemporalHistoryContinuous);
            request.Environment.Bloom.Intensity = 0.25;
            CHECK_FALSE(capture(37).TemporalHistoryContinuous);
            CHECK(capture(38).TemporalHistoryContinuous);
            request.Environment.Bloom.Radius = -1.0;
            CHECK_THROWS_AS(capture(39), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 38);
            CHECK(state->CameraMotionHistory.at(key).Bloom.Intensity == 0.25);
            CHECK(state->CaptureRequests.empty());
            request.Environment.Bloom.Radius = 2.0;
            request.Environment.Stars.Enabled = true;
            request.Environment.Stars.Seed = 12345;
            packet = capture(39);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            CHECK(packet.Environment.Stars == request.Environment.Stars);
            CHECK(packet.StarTwinklePhase == 0.0);
            CHECK(capture(40).TemporalHistoryContinuous);
            request.Environment.Stars.TwinkleFrequency = 0.125;
            request.EvaluatedEnvironment->Time = Keire::EnvironmentTimeSample{1, 0.5, 10.0};
            packet = capture(41);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            CHECK(packet.StarTwinklePhase == doctest::Approx(0.875));
            CHECK(capture(42).TemporalHistoryContinuous);
            request.Environment.Stars.Seed = 12346;
            CHECK_FALSE(capture(43).TemporalHistoryContinuous);
            CHECK(capture(44).TemporalHistoryContinuous);
            request.Environment.Stars.Density = -1.0;
            CHECK_THROWS_AS(capture(45), std::invalid_argument);
            CHECK(state->CameraMotionHistory.at(key).Frame == 44);
            CHECK(state->CameraMotionHistory.at(key).Stars.Seed == 12346);
            CHECK(state->CaptureRequests.empty());
            request.Environment.Stars.Density = 0.0;
            auto traveller = scene->CreateEntity("Distant mesh");
            REQUIRE(traveller.AddComponent<Keire::MeshRendererComponent>());
            traveller.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({1e15 + 0.5, 0.0, 0.0});
            auto camera = view->Camera();
            camera.WorldOrigin = {1e15, 0.0, 0.0};
            camera.EnvironmentAnchor = camera.WorldOrigin;
            view->SetCamera(camera);
            packet = capture(100);
            REQUIRE(packet.DrawItems.size() == 1U);
            CHECK(packet.DrawItems[0].World.Elements[12] == 0.5F);
            camera.WorldOrigin.X += 0.25;
            view->SetCamera(camera);
            packet = capture(101);
            REQUIRE(packet.DrawItems.size() == 1U);
            CHECK(packet.TemporalHistoryContinuous);
            CHECK(packet.DrawItems[0].World.Elements[12] == 0.25F);
            CHECK(packet.DrawItems[0].PreviousWorld.Elements[12] == 0.5F);
            CHECK(state->CameraMotionHistory.at(key).WorldOrigin == camera.WorldOrigin);
            auto appearing = scene->CreateEntity("Newly visible distant mesh");
            REQUIRE(appearing.AddComponent<Keire::MeshRendererComponent>());
            appearing.GetComponent<Keire::TransformComponent>()->SetLocalPositionPrecise({1e15 + 0.75, 0.0, 0.0});
            camera.WorldOrigin.X += 0.25;
            view->SetCamera(camera);
            packet = capture(102);
            REQUIRE(packet.DrawItems.size() == 2U);
            const auto appearingDraw =
                std::ranges::find_if(packet.DrawItems, [&](const auto& item) { return item.Entity == appearing.Id(); });
            REQUIRE(appearingDraw != packet.DrawItems.end());
            CHECK(appearingDraw->World.Elements[12] == 0.25F);
            CHECK(appearingDraw->PreviousWorld.Elements[12] == 0.5F);
            const auto validCamera = camera;
            camera.WorldOrigin.X = std::numeric_limits<double>::infinity();
            CHECK_THROWS_AS(view->SetCamera(camera), std::invalid_argument);
            CHECK(view->Camera().WorldOrigin == validCamera.WorldOrigin);
            camera = validCamera;
            camera.EnvironmentAnchor.X += 1.0;
            view->SetCamera(camera);
            CHECK_FALSE(capture(103).TemporalHistoryContinuous);
            packet = capture(105);
            CHECK_FALSE(packet.TemporalHistoryContinuous);
            for (const auto& item : packet.DrawItems)
            {
                CHECK(item.PreviousWorld == item.World);
            }
            state->CaptureRequests.clear();
            request.Environment.Atmosphere.Enabled = true;
            request.EvaluatedEnvironment.reset();
            Keire::PlanetEnvironmentView nearPlanet;
            nearPlanet.Planet = Keire::AssetId(1, 1);
            nearPlanet.World = 1;
            nearPlanet.PlanetFrame = 2;
            nearPlanet.ObserverFrame = 1;
            nearPlanet.TopologyRevision = 1;
            nearPlanet.RadiusMetres = 1000.0;
            nearPlanet.ObserverFromCenterPlanetMetres = {0.0, 1001.0, 0.0};
            auto farPlanet = nearPlanet;
            farPlanet.Planet = Keire::AssetId(1, 2);
            farPlanet.PlanetFrame = 3;
            farPlanet.ObserverFromCenterPlanetMetres = {10000.0, 0.0, 0.0};
            request.PlanetAtmospheres = {farPlanet, nearPlanet};
            packet = capture(200);
            REQUIRE(packet.PlanetAtmospheres.size() == 2);
            CHECK(packet.PlanetAtmospheres[0].Planet == nearPlanet.Planet);
            CHECK(packet.PlanetAtmospheres[1].Planet == farPlanet.Planet);
            CHECK(capture(201).TemporalHistoryContinuous);
            std::ranges::reverse(request.PlanetAtmospheres);
            CHECK(capture(202).TemporalHistoryContinuous);
            request.PlanetAtmospheres[1].DiscontinuityRevision += 1;
            CHECK_FALSE(capture(203).TemporalHistoryContinuous);
            request.PlanetAtmospheres[1].ObserverFromCenterPlanetMetres[0] += 1.0;
            CHECK_FALSE(capture(204).TemporalHistoryContinuous);
            request.PlanetAtmospheres[1].SnapshotTime = 1.0;
            CHECK_THROWS_AS(capture(205), std::invalid_argument);
            request.PlanetAtmospheres[1].SnapshotTime = 0.0;
            request.PlanetAtmospheres.push_back(nearPlanet);
            CHECK_THROWS_AS(capture(205), std::invalid_argument);
            request.PlanetAtmospheres.assign(9, nearPlanet);
            CHECK_THROWS_AS(capture(205), std::length_error);
            request.Environment.Atmosphere.Enabled = false;
            CHECK(capture(205).PlanetAtmospheres.empty());
            state->CaptureRequests.clear();
            scene->Close();
            m_Checked = true;
            Owner().RequestExit();
        }

      private:
        bool& m_Checked;
    };
} // namespace

TEST_CASE("Environment capture validates light values and source-aware history without test hooks")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.SuspendWhenMainWindowMinimized = false;
    specification.ManageLogging = false;
    specification.Render.Mode = Keire::RenderMode::Headless;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    bool checked = false;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<EnvironmentCaptureLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
