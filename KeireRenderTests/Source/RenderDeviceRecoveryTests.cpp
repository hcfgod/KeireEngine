#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#if defined(KEIRE_ENABLE_TEST_HOOKS)
namespace
{
    enum class RecoveryScenario : std::uint8_t
    {
        ImmediateSuccess,
        HealthyCandidateFailure,
        ThirdAttemptSuccess,
        LostCandidateFailure,
        Exhaustion,
        Retirement,
        Shutdown,
        BeforeCallbackFailure,
        AfterCallbackFailure,
        ClosingFromAfterCallback
    };

    struct RecoveryResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> Diagnostic;
        Keire::RenderStatistics Statistics;
        std::vector<Keire::RenderFrameTimeline> Timelines;
        std::uint64_t AbandonedHandles = 0;
        std::uint64_t LostGenerationGpuCleanupCalls = 0;
        std::uint64_t HealthyCandidateCleanups = 0;
        std::uint64_t RetriedVfxSnapshots = 0;
        float LastBackoffMilliseconds = 0.0F;
        bool Closed = false;
    };

    [[nodiscard]] std::size_t RetryCount(const std::vector<Keire::RenderFrameTimeline>& timelines)
    {
        return static_cast<std::size_t>(
            std::ranges::count(timelines, true, &Keire::RenderFrameTimeline::RetriedAfterDeviceLoss));
    }

    class RecoveryScenarioLayer final : public Keire::Layer
    {
      public:
        RecoveryScenarioLayer(const RecoveryScenario scenario, RecoveryResults& results)
            : Layer("Rendered device recovery"), m_Scenario(scenario), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Device recovery"));
            m_View = Owner().Renderer()->CreateView({.Name = "Device recovery", .Width = 32, .Height = 32});
            if (m_Scenario == RecoveryScenario::BeforeCallbackFailure)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), [] { throw std::runtime_error("Injected pre-recovery callback failure."); },
                    {});
            }
            else if (m_Scenario == RecoveryScenario::AfterCallbackFailure)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), {},
                    [this](SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)
                    {
                        if (!m_AfterCallbackFailed)
                        {
                            m_AfterCallbackFailed = true;
                            throw std::runtime_error("Injected post-create recovery callback failure.");
                        }
                    });
            }
            else if (m_Scenario == RecoveryScenario::ClosingFromAfterCallback)
            {
                Keire::RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
                    *Owner().Renderer(), {},
                    [this](SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)
                    {
                        const auto renderer = Owner().Renderer();
                        Keire::RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(
                            *renderer, Keire::RenderDeviceState::Closing);
                        Owner().RequestExit();
                    });
            }
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (!m_Injected)
            {
                if (m_Scenario == RecoveryScenario::HealthyCandidateFailure ||
                    m_Scenario == RecoveryScenario::ThirdAttemptSuccess ||
                    m_Scenario == RecoveryScenario::Exhaustion)
                {
                    Keire::RenderSystemInternalAccess::InjectRecoveryCandidateFailure(
                        *renderer, Keire::RenderRecoveryCandidateFault::HealthyFailure,
                        m_Scenario == RecoveryScenario::ThirdAttemptSuccess || m_Scenario == RecoveryScenario::Exhaustion
                            ? 2U
                            : 1U);
                }
                else if (m_Scenario == RecoveryScenario::LostCandidateFailure)
                {
                    Keire::RenderSystemInternalAccess::InjectRecoveryCandidateFailure(
                        *renderer, Keire::RenderRecoveryCandidateFault::DeviceLoss);
                }

                if (m_Scenario == RecoveryScenario::Retirement)
                    Keire::RenderSystemInternalAccess::InjectDeviceLossAtRetirement(*renderer);
                else
                    Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Injected = true;
                if (m_Scenario == RecoveryScenario::Shutdown)
                    Owner().RequestExit();
                return;
            }

            const auto diagnostic = renderer->LastDeviceLoss();
            if (!diagnostic || !diagnostic->RecoverySucceeded)
                return;
            renderer->Flush();
            Capture(*renderer);
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                const auto renderer = Owner().Renderer();
                if (!m_Results.Diagnostic)
                    m_Results.Diagnostic = renderer->LastDeviceLoss();
                renderer->Close();
                Capture(*renderer);
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
            m_View.Reset();
            m_Scene.Reset();
        }

      private:
        void Capture(Keire::RenderSystem& renderer)
        {
            m_Results.Diagnostic = renderer.LastDeviceLoss();
            m_Results.Statistics = renderer.Statistics();
            m_Results.Timelines = renderer.RecentFrameTimelines();
            m_Results.AbandonedHandles =
                Keire::RenderSystemInternalAccess::LostGenerationAbandonedHandleCount(renderer);
            m_Results.LostGenerationGpuCleanupCalls =
                Keire::RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(renderer);
            m_Results.HealthyCandidateCleanups =
                Keire::RenderSystemInternalAccess::HealthyRecoveryCandidateCleanupCount(renderer);
            m_Results.RetriedVfxSnapshots = Keire::RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(renderer);
            m_Results.LastBackoffMilliseconds =
                Keire::RenderSystemInternalAccess::LastRecoveryBackoffMillisecondsForTest(renderer);
            m_Results.Closed = renderer.DeviceState() == Keire::RenderDeviceState::Closed;
        }

        RecoveryScenario m_Scenario;
        RecoveryResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        bool m_Injected = false;
        bool m_AfterCallbackFailed = false;
    };

    struct StabilityResetResults final
    {
        std::optional<Keire::GpuDeviceLossDiagnostic> First;
        std::optional<Keire::GpuDeviceLossDiagnostic> Second;
        std::uint32_t AttemptsAfterStability = 99U;
        std::uint32_t AttemptsAfterSecondRecovery = 99U;
        std::vector<Keire::RenderFrameTimeline> Timelines;
    };

    class StabilityResetLayer final : public Keire::Layer
    {
      public:
        explicit StabilityResetLayer(StabilityResetResults& results)
            : Layer("Device recovery stability reset"), m_Results(results)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Recovery stability"));
            m_View = Owner().Renderer()->CreateView({.Name = "Recovery stability", .Width = 32, .Height = 32});
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            switch (m_Phase)
            {
            case Phase::InjectFirst:
                Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Phase = Phase::WaitFirst;
                break;
            case Phase::WaitFirst:
                if (const auto diagnostic = renderer->LastDeviceLoss(); diagnostic && diagnostic->RecoverySucceeded)
                {
                    renderer->Flush();
                    m_Results.First = diagnostic;
                    Keire::RenderSystemInternalAccess::SatisfyRecoveryStabilityWindowForTest(*renderer);
                    renderer->Submit({m_Scene, m_View});
                    m_Phase = Phase::RetireStabilityFrame;
                }
                break;
            case Phase::RetireStabilityFrame:
                renderer->Flush();
                m_Results.AttemptsAfterStability =
                    Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                renderer->Submit({m_Scene, m_View});
                m_Phase = Phase::WaitSecond;
                break;
            case Phase::WaitSecond:
                if (const auto diagnostic = renderer->LastDeviceLoss();
                    diagnostic && diagnostic->RecoverySucceeded && m_Results.First &&
                    diagnostic->RecoveredDeviceGeneration > m_Results.First->RecoveredDeviceGeneration)
                {
                    renderer->Flush();
                    m_Results.Second = diagnostic;
                    m_Results.AttemptsAfterSecondRecovery =
                        Keire::RenderSystemInternalAccess::RecoveryAttemptCountForTest(*renderer);
                    m_Results.Timelines = renderer->RecentFrameTimelines();
                    Owner().RequestExit();
                    m_Phase = Phase::Complete;
                }
                break;
            case Phase::Complete:
                break;
            }
        }

        void OnDetach() noexcept override
        {
            try
            {
                Owner().Renderer()->Close();
            }
            catch (...)
            {
            }
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        enum class Phase : std::uint8_t
        {
            InjectFirst,
            WaitFirst,
            RetireStabilityFrame,
            WaitSecond,
            Complete
        };

        StabilityResetResults& m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Phase m_Phase = Phase::InjectFirst;
    };

    [[nodiscard]] Keire::ApplicationSpecification RecoverySpecification(const std::uint32_t attempts)
    {
        auto specification = RenderTestSpecification();
        specification.Render.DeviceLossRecoveryAttempts = attempts;
        return specification;
    }
} // namespace

TEST_CASE("rendered device recovery rebuilds UI and surfaces then retries one immutable VFX packet")
{
    RecoveryResults results;
    auto specification = RecoverySpecification(1U);
    specification.Ui.Mode = Keire::UiMode::Rendered;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ImmediateSuccess, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test frame injection");
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 1U);
    CHECK(results.Diagnostic->RecoveredDeviceGeneration > results.Diagnostic->DeviceGeneration);
    CHECK(results.Diagnostic->Backend.size() > 0U);
    CHECK(results.Diagnostic->Adapter.size() > 0U);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.RetriedVfxSnapshots == 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("healthy and lost recovery candidates use distinct cleanup contracts before attempt two succeeds")
{
    for (const auto scenario : {RecoveryScenario::HealthyCandidateFailure, RecoveryScenario::LostCandidateFailure})
    {
        CAPTURE(static_cast<std::uint32_t>(scenario));
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(scenario, results));
        CHECK(application.Run() == 0);
        REQUIRE(results.Diagnostic);
        CHECK(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 2U);
        CHECK(RetryCount(results.Timelines) == 1U);
        CHECK(results.LostGenerationGpuCleanupCalls == 0U);
        CHECK(results.HealthyCandidateCleanups == (scenario == RecoveryScenario::HealthyCandidateFailure ? 1U : 0U));
        CHECK(results.Closed);
    }
}

TEST_CASE("three recovery attempts are valid and every retry observes the bounded backoff")
{
    CHECK(Keire::RenderSpecification{}.DeviceLossRecoveryAttempts == 2U);
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(3U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ThirdAttemptSuccess, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 3U);
    CHECK(results.HealthyCandidateCleanups == 2U);
    CHECK(results.LastBackoffMilliseconds >= 240.0F);
    CHECK(RetryCount(results.Timelines) == 1U);
    CHECK(results.Closed);
}

TEST_CASE("device loss discovered during idle fence retirement retains one accepted packet for one retry")
{
    RecoveryResults results;
    auto specification = RecoverySpecification(1U);
    specification.Render.MaximumFramesInFlight = 1U;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Retirement, results));
    CHECK(application.Run() == 0);
    REQUIRE(results.Diagnostic);
    CHECK(results.Diagnostic->Operation == "test fence retirement injection");
    CHECK(results.Diagnostic->RecoverySucceeded);
    CHECK(results.AbandonedHandles >= 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.AcceptedFrames == results.Statistics.RetiredFrames);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(RetryCount(results.Timelines) == 1U);
}

TEST_CASE("recovery exhaustion preserves its actionable incident and closes without lost-generation cleanup")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Exhaustion, results));
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
    REQUIRE(results.Diagnostic);
    CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
    CHECK(results.Diagnostic->RecoveryAttempt == 2U);
    CHECK(results.Diagnostic->Operation == "test frame injection");
    CHECK(results.Diagnostic->Backend.size() > 0U);
    CHECK(results.Diagnostic->Adapter.size() > 0U);
    CHECK(results.HealthyCandidateCleanups == 2U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("recovery callback failures are contained before and after candidate creation")
{
    {
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(
            std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::BeforeCallbackFailure, results));
        CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
        REQUIRE(results.Diagnostic);
        CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 0U);
        CHECK(results.HealthyCandidateCleanups == 0U);
        CHECK(results.Closed);
    }
    {
        RecoveryResults results;
        Keire::Application application(RecoverySpecification(2U));
        (void)application.PushLayer(
            std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::AfterCallbackFailure, results));
        CHECK(application.Run() == 0);
        REQUIRE(results.Diagnostic);
        CHECK(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 2U);
        CHECK(results.HealthyCandidateCleanups == 1U);
        CHECK(results.Closed);
    }
}

TEST_CASE("device loss racing shutdown performs no recovery attempt and Close remains idempotent")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::Shutdown, results));
    CHECK(application.Run() == 0);
    if (results.Diagnostic)
    {
        CHECK_FALSE(results.Diagnostic->RecoverySucceeded);
        CHECK(results.Diagnostic->RecoveryAttempt == 0U);
    }
    CHECK(results.HealthyCandidateCleanups == 0U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Statistics.OutstandingFrames == 0U);
    CHECK(results.Closed);
}

TEST_CASE("Closing from a post-create recovery callback cleans the healthy candidate without state resurrection")
{
    RecoveryResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(
        std::make_unique<RecoveryScenarioLayer>(RecoveryScenario::ClosingFromAfterCallback, results));
    CHECK_THROWS_AS((void)application.Run(), Keire::GpuDeviceLostError);
    CHECK(results.HealthyCandidateCleanups == 1U);
    CHECK(results.LostGenerationGpuCleanupCalls == 0U);
    CHECK(results.Closed);
}

TEST_CASE("recovery attempts reset only after both stability thresholds then a repeated loss starts at one")
{
    StabilityResetResults results;
    Keire::Application application(RecoverySpecification(2U));
    (void)application.PushLayer(std::make_unique<StabilityResetLayer>(results));
    CHECK(application.Run() == 0);
    REQUIRE(results.First);
    REQUIRE(results.Second);
    CHECK(results.AttemptsAfterStability == 0U);
    CHECK(results.AttemptsAfterSecondRecovery == 1U);
    CHECK(results.First->RecoveryAttempt == 1U);
    CHECK(results.Second->RecoveryAttempt == 1U);
    CHECK(results.Second->RecoveredDeviceGeneration > results.First->RecoveredDeviceGeneration);
    CHECK(RetryCount(results.Timelines) == 2U);
}
#endif
