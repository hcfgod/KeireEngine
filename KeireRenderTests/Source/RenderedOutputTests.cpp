#include "Keire/Application.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
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
