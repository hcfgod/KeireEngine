#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireRenderTests/RenderAssetFixture.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{
    using Frames = std::vector<std::vector<std::uint8_t>>;

    class ShadowReceptionLayer final : public Keire::Layer
    {
      public:
        ShadowReceptionLayer(const Keire::AssetId material, const Keire::AssetId cookie, const Keire::RenderPath path,
                             std::shared_ptr<Frames> frames)
            : Layer("Shadow reception regression"), m_Material(material), m_Cookie(cookie), m_Path(path),
              m_Frames(std::move(frames))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Shadow reception"));
            auto floor = m_Scene->CreateEntity("Receiver");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.75F, 0.0F});
            floor.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 0.15F, 4.0F});
            m_Receiver = floor.AddComponent<Keire::MeshRendererComponent>();
            m_Receiver->SetMaterial(m_Material);
            m_Receiver->SetCastShadows(false);
            auto caster = m_Scene->CreateEntity("Caster");
            caster.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.5F, 0.0F});
            caster.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.65F, 0.65F, 0.65F});
            m_Caster = caster.AddComponent<Keire::MeshRendererComponent>();
            m_Caster->SetMaterial(m_Material);
            m_Caster->SetReceiveShadows(false);
            auto sun = m_Scene->CreateEntity("Sun");
            sun.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Sun = sun.AddComponent<Keire::DirectionalLightComponent>();
            m_Sun->SetIntensity(4.0F);
            m_Sun->SetShadows(Keire::ShadowQuality::Soft);
            auto point = m_Scene->CreateEntity("Point");
            point.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            m_Point = point.AddComponent<Keire::PointLightComponent>();
            m_Point->SetIntensity(16.0F);
            m_Point->SetRange(10.0F);
            m_Point->SetShadows(Keire::ShadowQuality::Soft);
            auto spot = m_Scene->CreateEntity("Spot");
            spot.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-1.5F, 2.5F, 1.5F});
            spot.GetComponent<Keire::TransformComponent>()->SetLocalRotation(
                Keire::Math::EulerDegreesToQuaternion({124.0F, 0.0F, 0.0F}));
            m_Spot = spot.AddComponent<Keire::SpotLightComponent>();
            m_Spot->SetIntensity(20.0F);
            m_Spot->SetRange(10.0F);
            m_Spot->SetConeAngles(35.0F, 55.0F);
            m_Spot->SetShadows(Keire::ShadowQuality::Soft);
            Keire::RenderSurfaceSpecification surface;
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({3.0F, 3.0F, 5.0F}, {0.0F, -0.25F, 0.0F}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(50.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            m_View->SetCamera(camera);
        }

        void OnDetach() noexcept override
        {
            m_View.Reset();
            m_Receiver.Reset();
            m_Caster.Reset();
            m_Sun.Reset();
            m_Point.Reset();
            m_Spot.Reset();
            if (m_Scene)
                m_Scene->Close();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame >= 120U && (m_Frame - 120U) % 24U == 0U)
            {
                m_Frames->push_back(
                    Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_View->Surface()));
                if (++m_Case == 14U)
                {
                    Owner().RequestExit();
                    return;
                }
            }
            // Baseline, shadowed, reception disabled, reception restored; repeat for each light type.
            m_Sun->SetEnabled(m_Case / 4U == 0U);
            m_Point->SetEnabled(m_Case / 4U == 1U);
            m_Spot->SetEnabled(m_Case >= 8U);
            m_Spot->SetCookie(m_Case == 13U ? m_Cookie : Keire::AssetId{});
            m_Caster->SetCastShadows(m_Case < 12U && m_Case % 4U != 0U);
            m_Receiver->SetReceiveShadows(m_Case % 4U != 2U);
            Keire::RenderEnvironmentSettings environment;
            environment.RequestedRenderPath = m_Path;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            environment.RequestedGlobalIllumination = Keire::GlobalIlluminationMode::Disabled;
            environment.AmbientIntensity = 0.1F;
            environment.SkyVisible = false;
            environment.DirectionalShadowCascadeCount = 2;
            environment.DirectionalShadowResolution = 1024;
            Owner().Renderer()->Submit({m_Scene, m_View, false, environment});
            ++m_Frame;
        }

      private:
        Keire::AssetId m_Material;
        Keire::AssetId m_Cookie;
        Keire::RenderPath m_Path;
        std::shared_ptr<Frames> m_Frames;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::MeshRendererComponent> m_Receiver;
        Keire::Ref<Keire::MeshRendererComponent> m_Caster;
        Keire::Ref<Keire::DirectionalLightComponent> m_Sun;
        Keire::Ref<Keire::PointLightComponent> m_Point;
        Keire::Ref<Keire::SpotLightComponent> m_Spot;
        std::uint32_t m_Frame = 0;
        std::uint32_t m_Case = 0;
    };
} // namespace

TEST_CASE("shadow reception toggles preserve lighting for default and graph materials in both paths")
{
    KeireRenderTests::Detail::RenderAssetFixture assets(true);
    Keire::TextureImportSettings cookieSettings;
    cookieSettings.Mips = Keire::TextureMipPolicy::None;
    Keire::TextureMipLevel cookieMip;
    cookieMip.Width = 64U;
    cookieMip.Height = 64U;
    for (std::uint32_t y = 0; y < 64U; ++y)
    {
        for (std::uint32_t x = 0; x < 64U; ++x)
        {
            const auto value = x >= 24U && x < 40U && y >= 24U && y < 40U ? std::byte{255} : std::byte{0};
            cookieMip.Pixels.insert(cookieMip.Pixels.end(), {value, value, value, std::byte{255}});
        }
    }
    Keire::AssetImporterRegistration cookieImporter;
    cookieImporter.Name = "KeireTests.Texture";
    cookieImporter.Type = Keire::Texture2DAsset::StaticType();
    cookieImporter.Extensions = {".texture"};
    cookieImporter.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    const auto cookie = assets.Database->CreateAsset("Cookie.texture", cookieImporter,
                                                     Keire::Texture2DAsset::Encode(cookieSettings, {&cookieMip, 1U}));
    assets.Catalog = assets.Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
    for (const auto material : std::array{Keire::AssetId{}, assets.ShaderGraphMaterial})
    {
        std::vector<std::shared_ptr<Frames>> pathFrames;
        for (const auto path : {Keire::RenderPath::ForwardPlus, Keire::RenderPath::DeferredHybrid})
        {
            CAPTURE(static_cast<int>(path));
            CAPTURE(static_cast<bool>(material));
            auto frames = std::make_shared<Frames>();
            {
                auto specification = RenderTestSpecification();
                specification.Assets.Mode = Keire::AssetMode::Development;
                specification.Assets.DevelopmentCatalog = assets.Catalog;
                Keire::Application application(specification);
                (void)application.PushLayer(std::make_unique<ShadowReceptionLayer>(material, cookie, path, frames));
                CHECK(application.Run() == 0);
            }
            REQUIRE(frames->size() == 14U);
            for (std::size_t light = 0; light < 3U; ++light)
            {
                CAPTURE(light);
                const auto offset = light * 4U;
                CHECK(MaximumDarkening((*frames)[offset], (*frames)[offset + 1U]) > MinimumShadowDelta);
                CHECK(MaximumDarkening((*frames)[offset], (*frames)[offset + 2U]) < 0.01F);
                CHECK(MaximumDarkening((*frames)[offset + 2U], (*frames)[offset]) < 0.01F);
                CHECK(MaximumDarkening((*frames)[offset + 2U], (*frames)[offset + 3U]) > MinimumShadowDelta);
            }
            if (material)
                CHECK(MaximumDarkening((*frames)[12U], (*frames)[13U]) > MinimumShadowDelta);
            pathFrames.push_back(frames);
        }
        if (material)
        {
            const auto& forward = (*pathFrames[0])[13U];
            const auto& deferred = (*pathFrames[1])[13U];
            REQUIRE(forward.size() == deferred.size());
            float totalError = 0.0F;
            for (std::size_t pixel = 0; pixel < forward.size(); ++pixel)
                totalError += std::abs(static_cast<float>(forward[pixel]) - static_cast<float>(deferred[pixel]));
            CHECK(totalError / (static_cast<float>(forward.size()) * 255.0F) < 0.008F);
        }
    }
}
