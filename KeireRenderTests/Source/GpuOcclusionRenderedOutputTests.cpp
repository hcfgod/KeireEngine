#include "KeireRenderTests/RenderAssetFixture.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Application.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using KeireRenderTests::Detail::RenderAssetFixture;

namespace
{
    [[nodiscard]] float MaximumPixelDifference(const std::vector<std::uint8_t>& left,
                                               const std::vector<std::uint8_t>& right) noexcept
    {
        if (left.size() != right.size())
            return 1.0F;
        float maximum = 0.0F;
        for (std::size_t index = 0; index < left.size(); ++index)
            maximum = std::max(maximum,
                               std::abs(static_cast<float>(left[index]) - static_cast<float>(right[index])) / 255.0F);
        return maximum;
    }

    [[nodiscard]] std::size_t RedDominantPixelCount(const std::vector<std::uint8_t>& pixels) noexcept
    {
        std::size_t result = 0;
        for (std::size_t offset = 0; offset + 3U < pixels.size(); offset += 4U)
        {
            constexpr std::uint8_t minimumDominance = 32U;
            const auto red = static_cast<std::uint16_t>(pixels[offset]);
            const auto green = static_cast<std::uint16_t>(pixels[offset + 1U]);
            const auto blue = static_cast<std::uint16_t>(pixels[offset + 2U]);
            result += static_cast<std::size_t>(red > green + minimumDominance && red > blue + minimumDominance);
        }
        return result;
    }

    [[nodiscard]] Keire::AssetId CreateCompleteBoundsSkin(RenderAssetFixture& assets)
    {
        Keire::AssetImporterRegistration importer;
        importer.Name = "KeireTests.SkinnedMesh";
        importer.Type = Keire::SkinnedMeshAsset::StaticType();
        importer.Extensions = {".keireskin"};
        importer.Import = [](const std::span<const std::byte> bytes)
        { return std::vector<std::byte>(bytes.begin(), bytes.end()); };

        std::array<Keire::SkinVertexInfluence8, 3> influences;
        for (auto& influence : influences)
        {
            influence.Bones[0] = 0;
            influence.Weights[0] = 1.0F;
            influence.Count = 1;
        }
        const std::array bounds{Keire::SkinInfluenceBounds{
            .Submesh = 0U, .Bone = 0U, .Minimum = {-0.9F, -0.8F, 0.0F}, .Maximum = {0.9F, 0.9F, 0.0F}}};
        const auto skin = assets.Database->CreateAsset(
            "TriangleCompleteBounds.keireskin", importer,
            Keire::SkinnedMeshAsset::Encode(assets.Mesh, assets.Skeleton, influences,
                                            Keire::SkinningMethod::LinearBlend, 1U, bounds));
        assets.Catalog = assets.Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;
        return skin;
    }

    struct MultiSurfaceResults final
    {
        std::array<std::vector<std::uint8_t>, 3> Frames;
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> OcclusionDiagnostics;
        std::vector<std::uint64_t> MaterialBindingBuilds;
        Keire::RenderStatistics Statistics;
        bool HasStatistics = false;
    };

    class MultiSurfaceCaptureLayer final : public Keire::Layer
    {
      public:
        MultiSurfaceCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                 std::shared_ptr<MultiSurfaceResults> results)
            : Layer("Multi-surface rendered output capture"), m_Mesh(mesh), m_Material(material),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Parse("711ace00-0000-4000-8000-000000000011"),
                                                     Keire::SceneAsset::EmptyDefinition("Multi-surface tests"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto object = m_Scene->CreateEntity("Rendered cube");
            const auto renderer = object.AddComponent<Keire::MeshRendererComponent>();
            renderer->SetMesh(m_Mesh);
            renderer->SetMaterials({&m_Material, 1});

            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                Keire::RenderSurfaceSpecification surface;
                surface.Name = "Multi-surface test " + std::to_string(index);
                surface.Width = SurfaceSize;
                surface.Height = SurfaceSize;
                surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
                surface.SampleCount = index == 1U ? Keire::RenderSampleCount::Four : Keire::RenderSampleCount::One;
                surface.Depth = true;
                m_Views[index] = Owner().Renderer()->CreateView(surface);
                Keire::RenderCamera camera;
                camera.View = Keire::Math::LookAt({0.0F, 0.0F, 2.5F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
                camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
                camera.ClearColor = surface.ClearColor;
                m_Views[index]->SetCamera(camera);
            }
        }

        void OnDetach() noexcept override
        {
            if (!m_CapturedFinalState)
                CaptureCurrentState();
            if (m_Scene)
                m_Scene->Close();
            for (auto& view : m_Views)
                view.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_SubmittedFrames != 0)
            {
                for (std::size_t index = 0; index < m_Views.size(); ++index)
                {
                    m_Results->Frames[index] = Keire::RenderSystemInternalAccess::ReadbackRGBA8(
                        *Owner().Renderer(), *m_Views[index]->Surface());
                }
                m_Results->MaterialBindingBuilds.push_back(
                    Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer()));
                if (HasStableMaterialBinding(m_Results->MaterialBindingBuilds) || m_SubmittedFrames >= 120)
                {
                    // RequestExit completes this update with an intentional no-submit frame, which correctly idles
                    // live-surface diagnostics. Snapshot the synchronized submitted frame before that lifecycle edge.
                    CaptureCurrentState();
                    Owner().RequestExit();
                    return;
                }
            }

            Keire::RenderEnvironmentSettings environment;
            environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            environment.AmbientIntensity = 1.0F;
            environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            constexpr std::array modes{Keire::GpuOcclusionMode::Disabled, Keire::GpuOcclusionMode::Automatic,
                                       Keire::GpuOcclusionMode::Forced};
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                environment.GpuOcclusion = modes[index];
                Owner().Renderer()->Submit({m_Scene, m_Views[index], false, environment});
            }
            ++m_SubmittedFrames;
        }

      private:
        void CaptureCurrentState() noexcept
        {
            if (!Owner().Renderer())
                return;
            m_Results->Statistics = Owner().Renderer()->Statistics();
            m_Results->HasStatistics = true;
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                if (m_Views[index])
                    m_Results->OcclusionDiagnostics[index] = m_Views[index]->Surface()->OcclusionDiagnostics();
            }
            m_CapturedFinalState = true;
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<MultiSurfaceResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        std::array<Keire::Ref<Keire::RenderView>, 3> m_Views;
        std::uint32_t m_SubmittedFrames = 0;
        bool m_CapturedFinalState = false;
    };

    enum class GpuOcclusionCaptureScenario : std::uint8_t
    {
        HiddenTarget,
        RevealTarget,
        ResetToDisabled,
        AlwaysVisibleTarget,
        DebugViews,
        LifecycleReset,
        SkinnedTarget,
        FreshPoseSkinnedTarget,
        TerminalFallback,
        NoSubmitIdle,
        PartialFallback,
        EmptyFrustumStandby,
        FrustumEdgeTransition
    };

    struct GpuOcclusionCaptureResults final
    {
        std::vector<std::vector<std::uint8_t>> Frames;
        std::vector<Keire::GpuOcclusionSurfaceDiagnostics> Diagnostics;
        std::vector<Keire::GpuOcclusionDebugView> DebugViews;
        std::vector<std::uint32_t> DebugMips;
        std::vector<Keire::GpuOcclusionSurfaceDiagnostics> ObservedAfterReset;
        std::vector<Keire::GpuOcclusionSurfaceDiagnostics> ObservedAfterReforce;
        Keire::RenderStatistics Statistics;
        Keire::ProfileFrame ProfilerFrame;
        Keire::FrameGraphSnapshot FrameGraph;
        std::uint64_t ReforcedFrameFloor = 0;
        bool RestoredResourcesAvailableBeforeSubmission = false;
        bool RestoredOutputPublishedBeforeSubmission = false;
        bool SawBelowAutomaticThreshold = false;
        bool TimedOut = false;
    };

    class GpuOcclusionCaptureLayer final : public Keire::Layer
    {
      public:
        GpuOcclusionCaptureLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                 const Keire::GpuOcclusionMode mode,
                                 std::shared_ptr<GpuOcclusionCaptureResults> results,
                                 const GpuOcclusionCaptureScenario scenario = GpuOcclusionCaptureScenario::HiddenTarget,
                                 const std::uint32_t width = SurfaceSize, const std::uint32_t height = SurfaceSize,
                                 const Keire::AssetId targetMaterial = {}, const std::uint32_t targetCount = 1U,
                                 const float occluderScale = 2.2F, const Keire::AssetId targetSkin = {},
                                 const Keire::AssetId targetSkeleton = {})
            : Layer("GPU occlusion rendered output"), m_Mesh(mesh), m_Material(material), m_Mode(mode),
              m_TargetMaterial(targetMaterial ? targetMaterial : material), m_Results(std::move(results)),
              m_Scenario(scenario), m_Width(width), m_Height(height), m_TargetCount(targetCount),
              m_OccluderScale(occluderScale), m_TargetSkin(targetSkin), m_TargetSkeleton(targetSkeleton)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("GPU occlusion lab"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto occluder = m_Scene->CreateEntity("Occlusion wall");
            const auto occluderRenderer = occluder.AddComponent<Keire::MeshRendererComponent>();
            occluderRenderer->SetMesh(m_Mesh);
            occluderRenderer->SetMaterial(m_Material);
            occluderRenderer->SetTint({1.0F, 1.0F, 1.0F, 1.0F});
            const auto occluderTransform = occluder.GetComponent<Keire::TransformComponent>();
            occluderTransform->SetLocalScale({m_OccluderScale, m_OccluderScale, 0.2F});

            for (std::uint32_t index = 0; index < m_TargetCount; ++index)
            {
                auto target = m_Scene->CreateEntity("Occluded target " + std::to_string(index));
                const auto targetRenderer = target.AddComponent<Keire::MeshRendererComponent>();
                targetRenderer->SetMesh(m_Mesh);
                targetRenderer->SetMaterial(m_TargetMaterial);
                targetRenderer->SetTint({0.75F, 1.0F, 0.75F, 1.0F});
                targetRenderer->SetAlwaysVisible(m_Scenario == GpuOcclusionCaptureScenario::AlwaysVisibleTarget);
                if (m_TargetSkin)
                {
                    const auto animator = target.AddComponent<Keire::AnimatorComponent>();
                    animator->SetSkeleton(m_TargetSkeleton);
                    animator->SetSkinnedMesh(m_TargetSkin);
                    const std::array palette{Keire::Math::ComposeTransform({}, {}, {1.0F, 1.0F, 1.0F})};
                    animator->SetRuntimePose("GPU occlusion test", 0.0F, true, palette);
                }
                const auto targetTransform = target.GetComponent<Keire::TransformComponent>();
                const auto column = m_TargetCount == 1U ? 0 : static_cast<std::int32_t>(index % 11U) - 5;
                const auto row = m_TargetCount == 1U ? 0 : static_cast<std::int32_t>(index / 11U) - 5;
                targetTransform->SetLocalPosition(
                    m_Scenario == GpuOcclusionCaptureScenario::FrustumEdgeTransition
                        ? Keire::Vector3{3.8F, 0.0F, -1.5F}
                        : Keire::Vector3{static_cast<float>(column) * 0.12F, static_cast<float>(row) * 0.12F, -1.5F});
                const float scale =
                    m_TargetCount == 1U
                        ? (m_Scenario == GpuOcclusionCaptureScenario::FreshPoseSkinnedTarget ? 1.0F : 0.5F)
                        : 0.12F;
                targetTransform->SetLocalScale({scale, scale, scale});
                if (index == 0U)
                    m_TargetTransform = targetTransform;
            }

            Keire::RenderSurfaceSpecification surface;
            surface.Name = "GPU occlusion lab";
            surface.Width = m_Width;
            surface.Height = m_Height;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            m_View = Owner().Renderer()->CreateView(surface);
            Keire::RenderCamera camera;
            camera.View = m_Scenario == GpuOcclusionCaptureScenario::EmptyFrustumStandby
                              ? Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, 10.0F}, {0.0F, 1.0F, 0.0F})
                              : Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, static_cast<float>(m_Width) / m_Height, 0.1F, 100.0F);
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);
            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            m_Environment.GpuOcclusion = m_Mode;
        }

        void OnDetach() noexcept override
        {
            if (Owner().Renderer() && m_Results->Frames.empty())
            {
                m_Results->Statistics = Owner().Renderer()->Statistics();
                m_Results->FrameGraph = Owner().Renderer()->CaptureFrameGraph();
            }
            if (m_View && m_Results->Diagnostics.empty())
                m_Results->Diagnostics.push_back(m_View->Surface()->OcclusionDiagnostics());
            if (m_Scene)
                m_Scene->Close();
            m_TargetTransform.Reset();
            m_View.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto surface = m_View->Surface();
            const auto recordDiagnostics = [&](const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics)
            {
                m_Results->Diagnostics.push_back(diagnostics);
                m_Results->DebugViews.push_back(surface->OcclusionDebugView());
                m_Results->DebugMips.push_back(surface->OcclusionDebugMip());
            };
            if (m_Scenario == GpuOcclusionCaptureScenario::LifecycleReset && m_LifecycleStage != 0U)
            {
                const auto diagnostics = surface->OcclusionDiagnostics();
                if (m_LifecycleStage == 1U && !surface->Available() && surface->Width() == 0U &&
                    surface->Height() == 0U)
                {
                    recordDiagnostics(diagnostics);
                    Keire::RenderSystemInternalAccess::RequestSurfaceSize(*surface, ResetWidth, ResetHeight);
                    m_LifecycleStage = 2U;
                }
                else if (m_LifecycleStage == 2U && surface->Available() && surface->Width() == ResetWidth &&
                         surface->Height() == ResetHeight)
                {
                    m_Results->RestoredResourcesAvailableBeforeSubmission = true;
                    const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
                        Keire::RenderSystemInternalAccess::SurfaceLease(*surface));
                    m_Results->RestoredOutputPublishedBeforeSubmission =
                        state && state->PublishedTexture.load(std::memory_order_acquire);
                    recordDiagnostics(diagnostics);
                    auto camera = m_View->Camera();
                    camera.Projection =
                        Keire::Math::Perspective(55.0F, static_cast<float>(ResetWidth) / ResetHeight, 0.1F, 100.0F);
                    m_View->SetCamera(camera);
                    m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
                    Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                    m_LifecycleStage = 3U;
                }
                else if (m_LifecycleStage == 3U)
                {
                    m_Results->ObservedAfterReset.push_back(diagnostics);
                    m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Disabled;
                    Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                    m_LifecycleStage = 4U;
                }
                else if (m_LifecycleStage == 4U)
                {
                    m_Results->ObservedAfterReset.push_back(diagnostics);
                    if (diagnostics.State == Keire::GpuOcclusionSurfaceState::Disabled)
                    {
                        recordDiagnostics(diagnostics);
                        m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
                        m_Results->ReforcedFrameFloor = Owner().Renderer()->Statistics().Frame;
                        Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                        m_LifecycleStage = 5U;
                    }
                    else
                        Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                }
                else if (m_LifecycleStage == 5U)
                {
                    m_Results->ObservedAfterReset.push_back(diagnostics);
                    m_Results->ObservedAfterReforce.push_back(diagnostics);
                    if (diagnostics.State == Keire::GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid &&
                        diagnostics.Culled > 0U && diagnostics.SourceFrame >= m_Results->ReforcedFrameFloor)
                    {
                        recordDiagnostics(diagnostics);
                        Owner().RequestExit();
                        return;
                    }
                    Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
                }

                if (++m_Frame > 180U)
                {
                    m_Results->TimedOut = true;
                    Owner().RequestExit();
                }
                return;
            }

            if (m_Submitted)
            {
                auto pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *surface);
                const auto diagnostics = surface->OcclusionDiagnostics();
                const auto materialBindingBuilds =
                    Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*Owner().Renderer());
                const auto expectedMaterialBindings = m_TargetMaterial == m_Material ? 1U : 2U;
                const auto skinningStaticBuilds =
                    Keire::RenderSystemInternalAccess::SkinningStaticBuildCount(*Owner().Renderer());
                const bool requiresSkinningResources =
                    m_TargetSkin && m_Scenario != GpuOcclusionCaptureScenario::SkinnedTarget;
                const bool skinReady = !requiresSkinningResources || skinningStaticBuilds > 0U;
                const bool resourcesReady = materialBindingBuilds >= expectedMaterialBindings && skinReady;
                m_ResourceReadyFrames = resourcesReady ? m_ResourceReadyFrames + 1U : 0U;
                if (diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::BelowAutomaticThreshold)
                    m_Results->SawBelowAutomaticThreshold = true;
                const auto capture = [&]()
                {
                    m_Results->Frames.push_back(std::move(pixels));
                    recordDiagnostics(diagnostics);
                    m_Results->Statistics = Owner().Renderer()->Statistics();
                    if (const auto profiler = Owner().GetProfiler(); profiler && profiler->IsOpen())
                        m_Results->ProfilerFrame = profiler->LatestFrame();
                    m_Results->FrameGraph = Owner().Renderer()->CaptureFrameGraph();
                };
                const bool fallback = diagnostics.State == Keire::GpuOcclusionSurfaceState::Fallback ||
                                      diagnostics.State == Keire::GpuOcclusionSurfaceState::Unsupported;
                if (m_Scenario == GpuOcclusionCaptureScenario::EmptyFrustumStandby &&
                    diagnostics.State == Keire::GpuOcclusionSurfaceState::Idle &&
                    diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::NoEligibleCandidates)
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
                if (m_Scenario == GpuOcclusionCaptureScenario::NoSubmitIdle && m_FollowUpPending)
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
                if (m_Mode == Keire::GpuOcclusionMode::Disabled && m_Frame >= 3U && m_ResourceReadyFrames >= 2U)
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
                if (m_Scenario == GpuOcclusionCaptureScenario::TerminalFallback && fallback &&
                    m_ResourceReadyFrames >= 2U)
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
                if (m_Scenario == GpuOcclusionCaptureScenario::ResetToDisabled && m_FollowUpPending &&
                    m_View->Surface()->Width() == ResetWidth && m_View->Surface()->Height() == ResetHeight &&
                    diagnostics.State == Keire::GpuOcclusionSurfaceState::Disabled)
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
                if (m_Scenario == GpuOcclusionCaptureScenario::DebugViews && m_FollowUpPending)
                {
                    capture();
                    if (m_DebugStage == 1U)
                    {
                        m_View->Surface()->SetOcclusionDebugView(Keire::GpuOcclusionDebugView::HierarchicalDepth,
                                                                 std::numeric_limits<std::uint32_t>::max());
                        m_DebugStage = 2U;
                    }
                    else
                    {
                        Owner().RequestExit();
                        return;
                    }
                }
                const bool classificationReady =
                    m_ResourceReadyFrames >= 2U && diagnostics.ReadbackValid &&
                    (diagnostics.Culled > 0U ||
                     (m_Scenario == GpuOcclusionCaptureScenario::AlwaysVisibleTarget && diagnostics.Candidates >= 2U &&
                      diagnostics.Visible == diagnostics.Candidates) ||
                     (m_Scenario == GpuOcclusionCaptureScenario::SkinnedTarget && diagnostics.Candidates >= 2U &&
                      diagnostics.Visible == diagnostics.Candidates && diagnostics.Culled == 0U) ||
                     (m_Scenario == GpuOcclusionCaptureScenario::FreshPoseSkinnedTarget &&
                      diagnostics.FreshPoseSkinnedCandidates != 0U && diagnostics.FreshPoseSkinnedDepthDraws != 0U) ||
                     (m_Scenario == GpuOcclusionCaptureScenario::FrustumEdgeTransition &&
                      diagnostics.Candidates == 1U && diagnostics.Visible == 1U && diagnostics.Culled == 0U) ||
                     (m_Scenario == GpuOcclusionCaptureScenario::PartialFallback &&
                      diagnostics.State == Keire::GpuOcclusionSurfaceState::Active &&
                      diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::LegacyShaderAbi));
                if (!m_FollowUpPending && classificationReady)
                {
                    if (m_Scenario == GpuOcclusionCaptureScenario::LifecycleReset)
                        surface->SetOcclusionDebugView(Keire::GpuOcclusionDebugView::HierarchicalDepth,
                                                       std::numeric_limits<std::uint32_t>::max());
                    capture();
                    if (m_Scenario == GpuOcclusionCaptureScenario::HiddenTarget ||
                        m_Scenario == GpuOcclusionCaptureScenario::AlwaysVisibleTarget ||
                        m_Scenario == GpuOcclusionCaptureScenario::SkinnedTarget ||
                        m_Scenario == GpuOcclusionCaptureScenario::FreshPoseSkinnedTarget ||
                        m_Scenario == GpuOcclusionCaptureScenario::PartialFallback)
                    {
                        Owner().RequestExit();
                        return;
                    }
                    if (m_Scenario == GpuOcclusionCaptureScenario::RevealTarget)
                        m_TargetTransform->SetLocalPosition({1.65F, 0.0F, -1.5F});
                    else if (m_Scenario == GpuOcclusionCaptureScenario::FrustumEdgeTransition)
                        m_TargetTransform->SetLocalPosition({3.4F, 0.0F, -1.5F});
                    else if (m_Scenario == GpuOcclusionCaptureScenario::ResetToDisabled)
                    {
                        m_View->Surface()->RequestSize(ResetWidth, ResetHeight);
                        m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Disabled;
                    }
                    else if (m_Scenario == GpuOcclusionCaptureScenario::DebugViews)
                    {
                        m_View->Surface()->SetOcclusionDebugView(Keire::GpuOcclusionDebugView::VisibilityBounds);
                        m_DebugStage = 1U;
                    }
                    else if (m_Scenario == GpuOcclusionCaptureScenario::LifecycleReset)
                    {
                        Keire::RenderSystemInternalAccess::RequestSurfaceSize(*surface, 0U, 0U);
                        m_LifecycleStage = 1U;
                        m_FollowUpPending = true;
                        return;
                    }
                    else if (m_Scenario == GpuOcclusionCaptureScenario::NoSubmitIdle)
                    {
                        m_FollowUpPending = true;
                        return;
                    }
                    m_FollowUpPending = true;
                }
                else if ((m_Scenario == GpuOcclusionCaptureScenario::RevealTarget ||
                          m_Scenario == GpuOcclusionCaptureScenario::FrustumEdgeTransition) &&
                         m_FollowUpPending &&
                         (m_Scenario != GpuOcclusionCaptureScenario::FrustumEdgeTransition ||
                          (diagnostics.ReadbackValid && diagnostics.Candidates >= 2U && diagnostics.Visible >= 2U)))
                {
                    capture();
                    Owner().RequestExit();
                    return;
                }
            }

            if (++m_Frame > 120U)
            {
                m_Results->TimedOut = true;
                Owner().RequestExit();
                return;
            }
            Owner().Renderer()->Submit({m_Scene, m_View, false, m_Environment});
            m_Submitted = true;
        }

      private:
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        Keire::AssetId m_TargetMaterial;
        Keire::GpuOcclusionMode m_Mode = Keire::GpuOcclusionMode::Automatic;
        std::shared_ptr<GpuOcclusionCaptureResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::TransformComponent> m_TargetTransform;
        Keire::RenderEnvironmentSettings m_Environment;
        static constexpr std::uint32_t ResetWidth = 101U;
        static constexpr std::uint32_t ResetHeight = 67U;
        std::uint32_t m_Frame = 0;
        GpuOcclusionCaptureScenario m_Scenario = GpuOcclusionCaptureScenario::HiddenTarget;
        std::uint32_t m_Width = SurfaceSize;
        std::uint32_t m_Height = SurfaceSize;
        std::uint32_t m_TargetCount = 1;
        float m_OccluderScale = 2.2F;
        Keire::AssetId m_TargetSkin;
        Keire::AssetId m_TargetSkeleton;
        std::uint32_t m_DebugStage = 0;
        std::uint32_t m_LifecycleStage = 0;
        std::uint32_t m_ResourceReadyFrames = 0;
        bool m_Submitted = false;
        bool m_FollowUpPending = false;
    };

    struct MixedGpuOcclusionSurfaceResults final
    {
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> Diagnostics;
        Keire::RenderStatistics Statistics;
        bool Captured = false;
        bool TimedOut = false;
    };

    class MixedGpuOcclusionSurfaceLayer final : public Keire::Layer
    {
      public:
        MixedGpuOcclusionSurfaceLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                      std::shared_ptr<MixedGpuOcclusionSurfaceResults> results,
                                      const bool fallbackFirst)
            : Layer("Mixed GPU occlusion surfaces"), m_Mesh(mesh), m_Material(material), m_Results(std::move(results)),
              m_FallbackFirst(fallbackFirst)
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Mixed GPU occlusion surfaces"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto occluder = m_Scene->CreateEntity("Occlusion wall");
            const auto occluderRenderer = occluder.AddComponent<Keire::MeshRendererComponent>();
            occluderRenderer->SetMesh(m_Mesh);
            occluderRenderer->SetMaterial(m_Material);
            occluder.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.5F, 4.5F, 0.2F});

            for (std::uint32_t index = 0; index < HiddenTargetCount; ++index)
            {
                auto target = m_Scene->CreateEntity("Occluded target " + std::to_string(index));
                const auto renderer = target.AddComponent<Keire::MeshRendererComponent>();
                renderer->SetMesh(m_Mesh);
                renderer->SetMaterial(m_Material);
                const auto transform = target.GetComponent<Keire::TransformComponent>();
                const auto column = static_cast<std::int32_t>(index % 11U) - 5;
                const auto row = static_cast<std::int32_t>(index / 11U) - 5;
                transform->SetLocalPosition(
                    {static_cast<float>(column) * 0.12F, static_cast<float>(row) * 0.12F, -1.5F});
                transform->SetLocalScale({0.12F, 0.12F, 0.12F});
            }

            constexpr std::array widths{SurfaceSize, SurfaceSize, SmallSurfaceWidth};
            constexpr std::array heights{SurfaceSize, SurfaceSize, SmallSurfaceHeight};
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                Keire::RenderSurfaceSpecification surface;
                surface.Name = "Mixed GPU occlusion surface " + std::to_string(index);
                surface.Width = widths[index];
                surface.Height = heights[index];
                surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
                surface.SampleCount = Keire::RenderSampleCount::One;
                m_Views[index] = Owner().Renderer()->CreateView(surface);
                Keire::RenderCamera camera;
                camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
                camera.Projection =
                    Keire::Math::Perspective(55.0F, static_cast<float>(widths[index]) / heights[index], 0.1F, 100.0F);
                camera.ClearColor = surface.ClearColor;
                m_Views[index]->SetCamera(camera);
            }

            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Automatic;
        }

        void OnDetach() noexcept override
        {
            if (!m_Results->Captured)
                CaptureCurrentState();
            if (m_Scene)
                m_Scene->Close();
            for (auto& view : m_Views)
                view.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                (void)Keire::RenderSystemInternalAccess::ReadbackRGBA8(*Owner().Renderer(), *m_Views[0]->Surface());
                CaptureCurrentState();
                const auto& first = m_Results->Diagnostics[0];
                const auto& second = m_Results->Diagnostics[1];
                const auto& standby = m_Results->Diagnostics[2];
                const auto active = [](const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics)
                {
                    return diagnostics.RequestedMode == Keire::GpuOcclusionMode::Automatic &&
                           diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Automatic &&
                           diagnostics.State == Keire::GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid &&
                           diagnostics.Culled >= HiddenTargetCount;
                };
                const bool typedStandby =
                    standby.RequestedMode == Keire::GpuOcclusionMode::Automatic &&
                    standby.EffectiveMode == Keire::GpuOcclusionMode::Disabled &&
                    standby.State == Keire::GpuOcclusionSurfaceState::Idle &&
                    standby.FallbackReason == Keire::GpuOcclusionFallbackReason::NoSafeOccluders &&
                    !standby.ReadbackValid;
                const auto& statistics = m_Results->Statistics;
                if (active(first) && active(second) && typedStandby && statistics.GpuOcclusionEnabled &&
                    !statistics.GpuOcclusionFallbackActive && statistics.GpuOcclusionDispatches > 0U &&
                    statistics.GpuOcclusionIndirectDraws > 0U)
                {
                    m_Results->Captured = true;
                    Owner().RequestExit();
                    return;
                }
            }

            if (++m_Frame > MaximumFrames)
            {
                m_Results->TimedOut = true;
                Owner().RequestExit();
                return;
            }

            constexpr std::array fallbackLastOrder{0U, 1U, 2U};
            constexpr std::array fallbackFirstOrder{2U, 0U, 1U};
            const auto& order = m_FallbackFirst ? fallbackFirstOrder : fallbackLastOrder;
            for (const auto index : order)
                Owner().Renderer()->Submit({m_Scene, m_Views[index], false, m_Environment});
            m_Submitted = true;
        }

      private:
        void CaptureCurrentState() noexcept
        {
            if (!Owner().Renderer())
                return;
            m_Results->Statistics = Owner().Renderer()->Statistics();
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                if (m_Views[index])
                    m_Results->Diagnostics[index] = m_Views[index]->Surface()->OcclusionDiagnostics();
            }
        }

        static constexpr std::uint32_t HiddenTargetCount = 127U;
        static constexpr std::uint32_t SmallSurfaceWidth = 32U;
        static constexpr std::uint32_t SmallSurfaceHeight = 18U;
        static constexpr std::uint32_t MaximumFrames = 180U;
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<MixedGpuOcclusionSurfaceResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        std::array<Keire::Ref<Keire::RenderView>, 3> m_Views;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint32_t m_Frame = 0U;
        bool m_FallbackFirst = false;
        bool m_Submitted = false;
    };

    struct CameraLocalGpuOcclusionResults final
    {
        std::array<std::vector<std::uint8_t>, 2> Frames;
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 2> Diagnostics;
        Keire::RenderStatistics Statistics;
        bool Captured = false;
        bool TimedOut = false;
    };

    class CameraLocalGpuOcclusionLayer final : public Keire::Layer
    {
      public:
        CameraLocalGpuOcclusionLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                                     std::shared_ptr<CameraLocalGpuOcclusionResults> results)
            : Layer("Camera-local GPU occlusion surfaces"), m_Mesh(mesh), m_Material(material),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Camera-local GPU occlusion"),
                                                     Keire::ComponentRegistry::CreateDefault());
            auto occluder = m_Scene->CreateEntity("Front-camera occluder");
            const auto occluderRenderer = occluder.AddComponent<Keire::MeshRendererComponent>();
            occluderRenderer->SetMesh(m_Mesh);
            occluderRenderer->SetMaterial(m_Material);
            occluder.GetComponent<Keire::TransformComponent>()->SetLocalScale({4.0F, 4.0F, 0.2F});

            auto target = m_Scene->CreateEntity("Camera-local target");
            const auto targetRenderer = target.AddComponent<Keire::MeshRendererComponent>();
            targetRenderer->SetMesh(m_Mesh);
            targetRenderer->SetMaterial(m_Material);
            const auto targetTransform = target.GetComponent<Keire::TransformComponent>();
            targetTransform->SetLocalPosition(TargetPosition);
            targetTransform->SetLocalScale({0.75F, 0.75F, 0.75F});

            Keire::RenderSurfaceSpecification surface;
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                surface.Name = index == 0U ? "Front game camera" : "Side observer camera";
                m_Views[index] = Owner().Renderer()->CreateView(surface);
                m_Views[index]->Surface()->SetOcclusionDebugView(Keire::GpuOcclusionDebugView::VisibilityBounds);
            }

            Keire::RenderCamera frontCamera;
            frontCamera.View = Keire::Math::LookAt({0.0F, 0.0F, 6.0F}, {}, {0.0F, 1.0F, 0.0F});
            frontCamera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            frontCamera.ClearColor = surface.ClearColor;
            m_Views[0]->SetCamera(frontCamera);

            auto observerCamera = frontCamera;
            observerCamera.View = Keire::Math::LookAt({8.0F, 0.0F, 5.0F}, TargetPosition, {0.0F, 1.0F, 0.0F});
            m_Views[1]->SetCamera(observerCamera);

            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.RequestedAntiAliasing = Keire::RenderAntiAliasingMode::None;
            m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            for (auto& view : m_Views)
                view.Reset();
            m_Scene.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Submitted)
            {
                for (std::size_t index = 0; index < m_Views.size(); ++index)
                {
                    m_Results->Frames[index] = Keire::RenderSystemInternalAccess::ReadbackRGBA8(
                        *Owner().Renderer(), *m_Views[index]->Surface());
                    m_Results->Diagnostics[index] = m_Views[index]->Surface()->OcclusionDiagnostics();
                }
                m_Results->Statistics = Owner().Renderer()->Statistics();

                const auto& front = m_Results->Diagnostics[0];
                const auto& observer = m_Results->Diagnostics[1];
                const auto active = [](const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics)
                {
                    return diagnostics.RequestedMode == Keire::GpuOcclusionMode::Forced &&
                           diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Forced &&
                           diagnostics.State == Keire::GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid;
                };
                if (active(front) && active(observer) && front.Culled > 0U && observer.Culled == 0U &&
                    observer.Visible == observer.Candidates)
                {
                    m_Results->Captured = true;
                    Owner().RequestExit();
                    return;
                }
            }

            if (++m_Frame > MaximumFrames)
            {
                m_Results->TimedOut = true;
                Owner().RequestExit();
                return;
            }

            for (const auto& view : m_Views)
                Owner().Renderer()->Submit({m_Scene, view, false, m_Environment});
            m_Submitted = true;
        }

      private:
        static constexpr Keire::Vector3 TargetPosition{0.0F, 0.0F, -3.0F};
        static constexpr std::uint32_t MaximumFrames = 180U;
        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<CameraLocalGpuOcclusionResults> m_Results;
        Keire::Ref<Keire::Scene> m_Scene;
        std::array<Keire::Ref<Keire::RenderView>, 2> m_Views;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint32_t m_Frame = 0U;
        bool m_Submitted = false;
    };
} // namespace

TEST_CASE("independent render surfaces submit in queue order and survive final-fence retirement")
{
    RenderAssetFixture assets;
    const auto results = std::make_shared<MultiSurfaceResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<MultiSurfaceCaptureLayer>(assets.Mesh, assets.LegacyMaterial, results));
        REQUIRE(application.Run() == 0);
    }
    for (const auto& frame : results->Frames)
    {
        REQUIRE(frame.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        CHECK(MeasureCenter(frame).Luminance() > MinimumBehaviorDelta);
    }
    REQUIRE(results->HasStatistics);
    REQUIRE(results->MaterialBindingBuilds.size() >= 2);
    CHECK(HasStableMaterialBinding(results->MaterialBindingBuilds));
    CHECK(results->OcclusionDiagnostics[0].RequestedMode == Keire::GpuOcclusionMode::Disabled);
    CHECK(results->OcclusionDiagnostics[0].State == Keire::GpuOcclusionSurfaceState::Disabled);
    CHECK(results->OcclusionDiagnostics[1].RequestedMode == Keire::GpuOcclusionMode::Automatic);
    CHECK(results->OcclusionDiagnostics[2].RequestedMode == Keire::GpuOcclusionMode::Forced);
    CHECK(results->OcclusionDiagnostics[2].State == Keire::GpuOcclusionSurfaceState::Fallback);
    CHECK(results->OcclusionDiagnostics[2].FallbackReason == Keire::GpuOcclusionFallbackReason::LegacyShaderAbi);
}

TEST_CASE("GPU occlusion decisions remain camera-local across game and observer surfaces")
{
    RenderAssetFixture assets(true);
    const auto results = std::make_shared<CameraLocalGpuOcclusionResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(
            std::make_unique<CameraLocalGpuOcclusionLayer>(assets.CubeMesh, assets.ShaderGraphMaterial, results));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Captured);
    const auto& front = results->Diagnostics[0];
    const auto& observer = results->Diagnostics[1];
    CHECK(front.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(observer.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(front.ReadbackValid);
    CHECK(observer.ReadbackValid);
    CHECK(front.SourceFrame != 0U);
    CHECK(front.SourceFrame == observer.SourceFrame);
    CHECK(front.Candidates == observer.Candidates);
    CHECK(front.Candidates == 2U);
    CHECK(front.Visible == 1U);
    CHECK(front.Culled == 1U);
    CHECK(observer.Visible == 2U);
    CHECK(observer.Culled == 0U);
    CHECK(front.SafeOccluders == 1U);
    CHECK(observer.SafeOccluders == 1U);

    const auto& statistics = results->Statistics;
    CHECK(statistics.GpuOcclusionEnabled);
    CHECK_FALSE(statistics.GpuOcclusionFallbackActive);
    CHECK(statistics.GpuOcclusionReadbackValid);
    CHECK(statistics.GpuOcclusionActiveSurfaces == 2U);
    CHECK(statistics.GpuOcclusionFallbackSurfaces == 0U);
    CHECK(statistics.GpuOcclusionPartialFallbackSurfaces == 0U);
    CHECK(statistics.GpuOcclusionCandidates == front.Candidates + observer.Candidates);
    CHECK(statistics.GpuOcclusionVisible == front.Visible + observer.Visible);
    CHECK(statistics.GpuOcclusionCulled == front.Culled + observer.Culled);
    CHECK(statistics.GpuOcclusionSafeOccluders == front.SafeOccluders + observer.SafeOccluders);
    CHECK(statistics.GpuOcclusionDispatches > 0U);
    CHECK(statistics.GpuOcclusionIndirectDraws == 4U);

    for (const auto& frame : results->Frames)
        REQUIRE(frame.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4U));
    CHECK(RedDominantPixelCount(results->Frames[0]) > 0U);
    CHECK(RedDominantPixelCount(results->Frames[1]) == 0U);
}

TEST_CASE("same-mode terminal GPU occlusion surfaces reject late active readbacks")
{
    Keire::RenderBackend::RenderSurfaceState surface;
    surface.Generation = 7U;
    surface.GpuOcclusionSubmissionEpoch = 11U;
    surface.GpuOcclusionSubmittedMode = Keire::GpuOcclusionMode::Forced;
    surface.GpuOcclusionDiagnostics.RequestedMode = Keire::GpuOcclusionMode::Forced;
    surface.GpuOcclusionDiagnostics.EffectiveMode = Keire::GpuOcclusionMode::Forced;
    surface.GpuOcclusionDiagnostics.State = Keire::GpuOcclusionSurfaceState::Active;

    Keire::RenderBackend::GpuOcclusionPendingReadback pending;
    pending.SurfaceGeneration = surface.Generation;
    pending.SubmissionEpoch = surface.GpuOcclusionSubmissionEpoch;
    pending.RequestedMode = surface.GpuOcclusionSubmittedMode;
    pending.SourceFrame = 17U;
    pending.SourceFrameSlot = 0U;
    pending.SourceSurfaceEpoch = surface.Epoch;
    pending.SourceDeviceGeneration = 3U;
    surface.Resources.Worksets.resize(1U);
    auto& visibility = surface.Workset(0U).GpuOcclusion;
    visibility.FrameId = pending.SourceFrame;
    visibility.FrameSlot = pending.SourceFrameSlot;
    visibility.SurfaceEpoch = pending.SourceSurfaceEpoch;
    visibility.DeviceGeneration = pending.SourceDeviceGeneration;
    visibility.OwnershipValid = true;
    CHECK(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, pending));

    CHECK(Keire::RenderBackend::PublishGpuOcclusionReadbackValidationFailure(surface, Keire::GpuOcclusionMode::Forced));
    CHECK(surface.GpuOcclusionSubmissionEpoch == pending.SubmissionEpoch + 1U);
    CHECK(surface.GpuOcclusionValidationFallbackEventPending);
    CHECK_FALSE(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, pending));

    auto fallbackEvents = 0U;
    const auto submitValidationCooldownFrame = [&]
    {
        const bool transitioned = Keire::RenderBackend::PublishGpuOcclusionFallback(
            surface, Keire::GpuOcclusionMode::Forced, Keire::GpuOcclusionFallbackReason::ReadbackValidationFailed);
        fallbackEvents +=
            Keire::RenderBackend::ConsumeGpuOcclusionValidationFallbackEvent(surface, transitioned) ? 1U : 0U;
    };
    submitValidationCooldownFrame();
    CHECK(fallbackEvents == 1U);
    CHECK_FALSE(surface.GpuOcclusionValidationFallbackEventPending);
    submitValidationCooldownFrame();
    CHECK(fallbackEvents == 1U);

    surface.GpuOcclusionDiagnostics.RequestedMode = Keire::GpuOcclusionMode::Forced;
    surface.GpuOcclusionDiagnostics.EffectiveMode = Keire::GpuOcclusionMode::Forced;
    surface.GpuOcclusionDiagnostics.State = Keire::GpuOcclusionSurfaceState::Active;
    surface.GpuOcclusionDiagnostics.FallbackReason = Keire::GpuOcclusionFallbackReason::None;
    CHECK_FALSE(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, pending));

    auto reactivatedPending = pending;
    reactivatedPending.SubmissionEpoch = surface.GpuOcclusionSubmissionEpoch;
    CHECK(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, reactivatedPending));
    CHECK(Keire::RenderBackend::PublishGpuOcclusionFallback(surface, Keire::GpuOcclusionMode::Forced,
                                                            Keire::GpuOcclusionFallbackReason::NoSafeOccluders));
    CHECK(surface.GpuOcclusionSubmissionEpoch == reactivatedPending.SubmissionEpoch + 1U);
    CHECK_FALSE(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, pending));
    CHECK_FALSE(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, reactivatedPending));
    surface.GpuOcclusionDiagnostics.State = Keire::GpuOcclusionSurfaceState::Idle;
    CHECK_FALSE(Keire::RenderBackend::CanPublishGpuOcclusionReadback(surface, pending));
}

TEST_CASE("forced GPU occlusion removes hidden instances without changing rendered pixels")
{
    RenderAssetFixture assets(true);
    const auto run = [&](const Keire::GpuOcclusionMode mode)
    {
        auto results = std::make_shared<GpuOcclusionCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        specification.Profiling.Mode = Keire::ProfilerMode::Enabled;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(
                std::make_unique<GpuOcclusionCaptureLayer>(assets.CubeMesh, assets.ShaderGraphMaterial, mode, results));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE_FALSE(results->Diagnostics.empty());
        CAPTURE(static_cast<int>(results->Diagnostics.back().State));
        CAPTURE(static_cast<int>(results->Diagnostics.back().FallbackReason));
        CAPTURE(results->Diagnostics.back().Candidates);
        CAPTURE(results->Diagnostics.back().Visible);
        CAPTURE(results->Diagnostics.back().Culled);
        CAPTURE(results->Diagnostics.back().SafeOccluders);
        CAPTURE(results->Diagnostics.back().ReadbackValid);
        REQUIRE_FALSE(results->TimedOut);
        REQUIRE(results->Frames.size() == 1);
        REQUIRE(results->Diagnostics.size() == 1);
        return results;
    };

    const auto direct = run(Keire::GpuOcclusionMode::Disabled);
    const auto occluded = run(Keire::GpuOcclusionMode::Forced);
    CHECK(direct->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Disabled);
    CHECK(occluded->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(occluded->Diagnostics.front().PyramidValid);
    CHECK(occluded->Diagnostics.front().ReadbackValid);
    CHECK(occluded->Diagnostics.front().Candidates >= 2U);
    CHECK(occluded->Diagnostics.front().Culled > 0U);
    CHECK(occluded->Statistics.GpuOcclusionEnabled);
    CHECK(occluded->Statistics.GpuOcclusionActiveSurfaces == 1U);
    CHECK(occluded->Statistics.GpuOcclusionFallbackSurfaces == 0U);
    CHECK(occluded->Statistics.GpuOcclusionPartialFallbackSurfaces == 0U);
    CHECK(occluded->Statistics.GpuOcclusionDispatches > 0U);
    CHECK(occluded->Statistics.GpuOcclusionIndirectDraws > 0U);
    CHECK(occluded->Statistics.GpuOcclusionCulledTriangles > 0U);
    const auto profilerCounter = [&occluded](const std::string_view name) -> std::optional<double>
    {
        const auto counter = std::ranges::find(occluded->ProfilerFrame.Counters, name, &Keire::ProfileCounter::Name);
        return counter == occluded->ProfilerFrame.Counters.end() ? std::nullopt : std::optional(counter->Value);
    };
    const auto profilerDispatches = profilerCounter("GPU occlusion dispatches");
    const auto profilerIndirectDraws = profilerCounter("GPU occlusion indirect draws");
    const auto profilerActiveSurfaces = profilerCounter("GPU occlusion active surfaces");
    const auto profilerFallbackSurfaces = profilerCounter("GPU occlusion fallback surfaces");
    REQUIRE(profilerDispatches);
    REQUIRE(profilerIndirectDraws);
    REQUIRE(profilerActiveSurfaces);
    REQUIRE(profilerFallbackSurfaces);
    CHECK(*profilerDispatches > 0.0);
    CHECK(*profilerIndirectDraws > 0.0);
    CHECK(*profilerActiveSurfaces == 1.0);
    CHECK(*profilerFallbackSurfaces == 0.0);
    CAPTURE(MeasureCenter(direct->Frames.front()).Luminance());
    CAPTURE(MeasureCenter(occluded->Frames.front()).Luminance());
    CHECK(MaximumPixelDifference(direct->Frames.front(), occluded->Frames.front()) <= ColorTolerance);

    const auto passOrder = [&occluded](const std::string_view name) -> std::optional<std::uint32_t>
    {
        const auto pass = std::ranges::find(occluded->FrameGraph.Passes, name, &Keire::FrameGraphSnapshotPass::Name);
        if (pass == occluded->FrameGraph.Passes.end())
            return std::nullopt;
        return pass->Order;
    };
    const auto depthOrder = passOrder("Occlusion depth");
    const auto pyramidOrder = passOrder("Occlusion depth pyramid");
    const auto cullingOrder = passOrder("GPU occlusion culling");
    auto opaqueOrder = passOrder("Deferred GBuffer standard");
    if (!opaqueOrder)
        opaqueOrder = passOrder("Opaque and mask");
    REQUIRE(depthOrder);
    REQUIRE(pyramidOrder);
    REQUIRE(cullingOrder);
    REQUIRE(opaqueOrder);
    CHECK(*depthOrder < *pyramidOrder);
    CHECK(*pyramidOrder < *cullingOrder);
    CHECK(*cullingOrder < *opaqueOrder);
}

TEST_CASE("a completed frame without a surface submission idles stale GPU occlusion diagnostics")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::NoSubmitIdle));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Diagnostics.size() == 2U);
    CHECK(results->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Active);
    const auto& idle = results->Diagnostics.back();
    CHECK(idle.RequestedMode == Keire::GpuOcclusionMode::Forced);
    CHECK(idle.EffectiveMode == Keire::GpuOcclusionMode::Disabled);
    CHECK(idle.State == Keire::GpuOcclusionSurfaceState::Idle);
    CHECK(idle.FallbackReason == Keire::GpuOcclusionFallbackReason::None);
    CHECK_FALSE(idle.PyramidValid);
    CHECK_FALSE(idle.ReadbackValid);
    CHECK(idle.Candidates == 0U);
    CHECK(idle.SourceFrame == 0U);
    CHECK(results->Statistics.GpuOcclusionActiveSurfaces == 0U);
    CHECK(results->Statistics.GpuOcclusionFallbackSurfaces == 0U);
    CHECK(results->Statistics.GpuOcclusionPartialFallbackSurfaces == 0U);
    CHECK(results->Statistics.GpuOcclusionDispatches == 0U);
    CHECK(results->Statistics.GpuOcclusionIndirectDraws == 0U);
    CHECK_FALSE(results->Statistics.GpuOcclusionEnabled);
    CHECK_FALSE(results->Statistics.GpuOcclusionFallbackActive);
    CHECK_FALSE(results->Statistics.GpuOcclusionReadbackValid);
    CHECK(results->Statistics.GpuOcclusionCandidates == 0U);
    CHECK(results->Statistics.GpuOcclusionVisible == 0U);
    CHECK(results->Statistics.GpuOcclusionCulled == 0U);
    CHECK(results->Statistics.GpuOcclusionCandidateTriangles == 0U);
    CHECK(results->Statistics.GpuOcclusionCulledTriangles == 0U);
}

TEST_CASE("forced GPU occlusion treats an empty camera frustum as standby instead of fallback")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::EmptyFrustumStandby));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Diagnostics.size() == 1U);
    const auto& diagnostics = results->Diagnostics.front();
    CHECK(diagnostics.RequestedMode == Keire::GpuOcclusionMode::Forced);
    CHECK(diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Disabled);
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Idle);
    CHECK(diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::NoEligibleCandidates);
    CHECK(diagnostics.EligibleCandidates == 0U);
    CHECK_FALSE(diagnostics.ReadbackValid);
    CHECK_FALSE(results->Statistics.GpuOcclusionEnabled);
    CHECK_FALSE(results->Statistics.GpuOcclusionFallbackActive);
    CHECK(results->Statistics.GpuOcclusionFallbackSurfaces == 0U);
}

TEST_CASE("active legacy partial fallback surfaces publish aggregate and profiler classification")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    specification.Profiling.Mode = Keire::ProfilerMode::Enabled;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::PartialFallback, SurfaceSize, SurfaceSize, assets.LegacyMaterial));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Diagnostics.size() == 1U);
    const auto& diagnostics = results->Diagnostics.front();
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::LegacyShaderAbi);
    CHECK(diagnostics.ReadbackValid);
    CHECK(results->Statistics.GpuOcclusionEnabled);
    CHECK(results->Statistics.GpuOcclusionFallbackActive);
    CHECK(results->Statistics.GpuOcclusionActiveSurfaces == 1U);
    CHECK(results->Statistics.GpuOcclusionFallbackSurfaces == 0U);
    CHECK(results->Statistics.GpuOcclusionPartialFallbackSurfaces == 1U);
    CHECK(results->Statistics.GpuOcclusionDispatches > 0U);
    CHECK(results->Statistics.GpuOcclusionIndirectDraws > 0U);

    const auto counter = [&results](const std::string_view name) -> std::optional<double>
    {
        const auto found = std::ranges::find(results->ProfilerFrame.Counters, name, &Keire::ProfileCounter::Name);
        return found == results->ProfilerFrame.Counters.end() ? std::nullopt : std::optional(found->Value);
    };
    const auto profilerActive = counter("GPU occlusion active surfaces");
    const auto profilerFallback = counter("GPU occlusion fallback surfaces");
    const auto profilerPartial = counter("GPU occlusion partial fallback surfaces");
    REQUIRE(profilerActive);
    REQUIRE(profilerFallback);
    REQUIRE(profilerPartial);
    CHECK(*profilerActive == 1.0);
    CHECK(*profilerFallback == 0.0);
    CHECK(*profilerPartial == 1.0);
}

TEST_CASE("same-frame GPU occlusion reveals a moved target on the next rendered frame")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::RevealTarget));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 2);
    REQUIRE(results->Diagnostics.size() == 2);
    CHECK(results->Diagnostics.front().ReadbackValid);
    CHECK(results->Diagnostics.front().Culled > 0U);
    CHECK(MaximumPixelDifference(results->Frames[0], results->Frames[1]) > MinimumBehaviorDelta);
}

TEST_CASE("GPU occlusion retains a target as its bounds enter the camera edge")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::FrustumEdgeTransition));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 2U);
    REQUIRE(results->Diagnostics.size() == 2U);
    CHECK(results->Diagnostics[0].Candidates == 1U);
    CHECK(results->Diagnostics[0].Visible == 1U);
    CHECK(results->Diagnostics[1].Candidates >= 2U);
    CHECK(results->Diagnostics[1].Visible >= 2U);
    CHECK(MaximumPixelDifference(results->Frames[0], results->Frames[1]) > MinimumBehaviorDelta);
}

TEST_CASE("depth-tested transparent Shader Graph singletons are occludees but never safe occluders")
{
    RenderAssetFixture assets(true, false, false, true);
    const auto run = [&](const Keire::GpuOcclusionMode mode)
    {
        auto results = std::make_shared<GpuOcclusionCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
                assets.CubeMesh, assets.ShaderGraphMaterial, mode, results, GpuOcclusionCaptureScenario::HiddenTarget,
                SurfaceSize, SurfaceSize, assets.TransparentShaderGraphMaterial));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE_FALSE(results->TimedOut);
        REQUIRE(results->Frames.size() == 1);
        REQUIRE(results->Diagnostics.size() == 1);
        return results;
    };

    const auto direct = run(Keire::GpuOcclusionMode::Disabled);
    const auto occluded = run(Keire::GpuOcclusionMode::Forced);
    CHECK(occluded->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(occluded->Diagnostics.front().ReadbackValid);
    CHECK(occluded->Diagnostics.front().Candidates >= 2U);
    CHECK(occluded->Diagnostics.front().SafeOccluders == 1U);
    CHECK(occluded->Diagnostics.front().Culled > 0U);
    CHECK(MaximumPixelDifference(direct->Frames.front(), occluded->Frames.front()) <= ColorTolerance);
}

TEST_CASE("Automatic GPU occlusion activates after two profitable frames without pixel regression")
{
    constexpr std::uint32_t hiddenTargets = 127U;
    RenderAssetFixture assets(true, false, false, false, true);
    const auto run = [&](const Keire::GpuOcclusionMode mode)
    {
        auto results = std::make_shared<GpuOcclusionCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
                assets.OcclusionStressMesh, assets.ShaderGraphMaterial, mode, results,
                GpuOcclusionCaptureScenario::HiddenTarget, SurfaceSize, SurfaceSize, Keire::AssetId{}, hiddenTargets,
                4.5F));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE_FALSE(results->TimedOut);
        REQUIRE(results->Frames.size() == 1);
        REQUIRE(results->Diagnostics.size() == 1);
        return results;
    };

    const auto direct = run(Keire::GpuOcclusionMode::Disabled);
    const auto automatic = run(Keire::GpuOcclusionMode::Automatic);
    const auto& diagnostics = automatic->Diagnostics.front();
    CHECK(automatic->SawBelowAutomaticThreshold);
    CHECK(diagnostics.RequestedMode == Keire::GpuOcclusionMode::Automatic);
    CHECK(diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Automatic);
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.ReadbackValid);
    CHECK(diagnostics.Candidates >= hiddenTargets + 1U);
    CHECK(diagnostics.SafeOccluders == 1U);
    CHECK(diagnostics.Culled >= hiddenTargets);
    CHECK(automatic->Statistics.GpuOcclusionCandidateTriangles >= 100'000U);
    CHECK(MaximumPixelDifference(direct->Frames.front(), automatic->Frames.front()) <= ColorTolerance);
}

TEST_CASE("mixed Automatic GPU occlusion surfaces retain active aggregate work when a small surface is on standby")
{
    RenderAssetFixture assets(true, false, false, false, true);
    for (const bool fallbackFirst : {false, true})
    {
        CAPTURE(fallbackFirst);
        auto results = std::make_shared<MixedGpuOcclusionSurfaceResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<MixedGpuOcclusionSurfaceLayer>(
                assets.OcclusionStressMesh, assets.ShaderGraphMaterial, results, fallbackFirst));
            REQUIRE(application.Run() == 0);
        }

        REQUIRE_FALSE(results->TimedOut);
        REQUIRE(results->Captured);
        for (std::size_t index = 0; index < 2U; ++index)
        {
            const auto& diagnostics = results->Diagnostics[index];
            CHECK(diagnostics.RequestedMode == Keire::GpuOcclusionMode::Automatic);
            CHECK(diagnostics.EffectiveMode == Keire::GpuOcclusionMode::Automatic);
            CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
            CHECK(diagnostics.FallbackReason == Keire::GpuOcclusionFallbackReason::None);
            CHECK(diagnostics.ReadbackValid);
            CHECK(diagnostics.Candidates >= 128U);
            CHECK(diagnostics.SafeOccluders == 1U);
            CHECK(diagnostics.Culled >= 127U);
        }

        const auto& standby = results->Diagnostics[2];
        CHECK(standby.RequestedMode == Keire::GpuOcclusionMode::Automatic);
        CHECK(standby.EffectiveMode == Keire::GpuOcclusionMode::Disabled);
        CHECK(standby.State == Keire::GpuOcclusionSurfaceState::Idle);
        CHECK(standby.FallbackReason == Keire::GpuOcclusionFallbackReason::NoSafeOccluders);
        CHECK_FALSE(standby.ReadbackValid);
        CHECK(standby.Candidates == 0U);

        const auto& statistics = results->Statistics;
        CHECK(statistics.GpuOcclusionEnabled);
        CHECK_FALSE(statistics.GpuOcclusionFallbackActive);
        CHECK(statistics.GpuOcclusionDispatches > 0U);
        CHECK(statistics.GpuOcclusionIndirectDraws > 0U);
        CHECK(statistics.GpuOcclusionActiveSurfaces == 2U);
        CHECK(statistics.GpuOcclusionFallbackSurfaces == 0U);
        CHECK(statistics.GpuOcclusionPartialFallbackSurfaces == 0U);
        CHECK(statistics.GpuOcclusionReadbackValid);
        CHECK(statistics.GpuOcclusionCandidates ==
              results->Diagnostics[0].Candidates + results->Diagnostics[1].Candidates);
        CHECK(statistics.GpuOcclusionCulled == results->Diagnostics[0].Culled + results->Diagnostics[1].Culled);
    }
}

TEST_CASE("forced GPU occlusion reports legacy shader fallback while preserving direct draws")
{
    RenderAssetFixture assets;
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.LegacyMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::TerminalFallback));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 1);
    REQUIRE(results->Diagnostics.size() == 1);
    CHECK(results->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Fallback);
    CHECK(results->Diagnostics.front().FallbackReason == Keire::GpuOcclusionFallbackReason::LegacyShaderAbi);
    CHECK(results->Statistics.GpuOcclusionActiveSurfaces == 0U);
    CHECK(results->Statistics.GpuOcclusionFallbackSurfaces == 1U);
    CHECK(MeasureCenter(results->Frames.front()).Luminance() > MinimumBehaviorDelta);
}

TEST_CASE("always-visible instances remain in forced GPU occlusion indirect draws")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::AlwaysVisibleTarget));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Diagnostics.size() == 1);
    const auto& diagnostics = results->Diagnostics.front();
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.ReadbackValid);
    CHECK(diagnostics.Candidates >= 2U);
    CHECK(diagnostics.Visible == diagnostics.Candidates);
    CHECK(diagnostics.Culled == 0U);
}

TEST_CASE("skinned instances enter forced GPU occlusion as force-visible non-occluders before deformation is ready")
{
    RenderAssetFixture assets(true);
    const auto run = [&](const Keire::GpuOcclusionMode mode)
    {
        auto results = std::make_shared<GpuOcclusionCaptureResults>();
        auto specification = RenderTestSpecification();
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        {
            Keire::Application application(std::move(specification));
            (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
                assets.Mesh, assets.ShaderGraphMaterial, mode, results, GpuOcclusionCaptureScenario::SkinnedTarget,
                SurfaceSize, SurfaceSize, Keire::AssetId{}, 1U, 2.2F, assets.Skin, assets.Skeleton));
            REQUIRE(application.Run() == 0);
        }
        REQUIRE_FALSE(results->TimedOut);
        REQUIRE(results->Frames.size() == 1);
        REQUIRE(results->Diagnostics.size() == 1);
        return results;
    };

    const auto direct = run(Keire::GpuOcclusionMode::Disabled);
    const auto occluded = run(Keire::GpuOcclusionMode::Forced);
    const auto& diagnostics = occluded->Diagnostics.front();
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.ReadbackValid);
    CHECK(diagnostics.Candidates >= 2U);
    CHECK(diagnostics.Visible == diagnostics.Candidates);
    CHECK(diagnostics.Culled == 0U);
    CHECK(occluded->Statistics.GpuOcclusionStaticMeshCandidates >= 1U);
    CHECK(occluded->Statistics.GpuOcclusionSkinnedMeshCandidates >= 1U);
    CHECK(occluded->Statistics.GpuOcclusionForcedVisibleCandidates >= 1U);
    CHECK(occluded->Statistics.GpuOcclusionSafeOccluders >= 1U);
    CHECK(diagnostics.FreshPoseSkinnedCandidates == 0U);
    CHECK(diagnostics.FreshPoseSkinnedDepthDraws == 0U);
    CHECK(MaximumPixelDifference(direct->Frames.front(), occluded->Frames.front()) <= ColorTolerance);
}

TEST_CASE("fresh current-pose skinned buffers participate in forced GPU occlusion depth")
{
    RenderAssetFixture assets(true);
    const auto completeBoundsSkin = CreateCompleteBoundsSkin(assets);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.Mesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::FreshPoseSkinnedTarget, SurfaceSize, SurfaceSize, Keire::AssetId{}, 1U, 2.2F,
            completeBoundsSkin, assets.Skeleton));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 1U);
    REQUIRE(results->Diagnostics.size() == 1U);
    const auto& diagnostics = results->Diagnostics.front();
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.ReadbackValid);
    CHECK(diagnostics.FreshPoseSkinnedCandidates == 1U);
    CHECK(diagnostics.FreshPoseSkinnedDepthDraws == 1U);
    CHECK(results->Statistics.GpuOcclusionSkinnedMeshCandidates >= 1U);
    CHECK(results->Statistics.GpuOcclusionForcedVisibleCandidates == 0U);
    CHECK(results->Statistics.GpuOcclusionSafeOccluders >= 2U);
}

TEST_CASE("procedural vertex displacement bypasses forced GPU occlusion without losing direct draws")
{
    RenderAssetFixture assets(true, true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::TerminalFallback));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 1);
    REQUIRE(results->Diagnostics.size() == 1);
    CHECK(results->Diagnostics.front().State == Keire::GpuOcclusionSurfaceState::Fallback);
    CHECK(results->Diagnostics.front().FallbackReason == Keire::GpuOcclusionFallbackReason::NoEligibleCandidates);
    CHECK(MeasureCenter(results->Frames.front()).Luminance() > MinimumBehaviorDelta);
}

TEST_CASE("odd-sized GPU occlusion pyramids conservatively reduce through a one-pixel mip")
{
    constexpr std::uint32_t width = 97U;
    constexpr std::uint32_t height = 65U;
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::HiddenTarget, width, height));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->Diagnostics.empty());
    CAPTURE(static_cast<int>(results->Diagnostics.back().State));
    CAPTURE(static_cast<int>(results->Diagnostics.back().FallbackReason));
    CAPTURE(results->Diagnostics.back().Candidates);
    CAPTURE(results->Diagnostics.back().Visible);
    CAPTURE(results->Diagnostics.back().Culled);
    CAPTURE(results->Diagnostics.back().SafeOccluders);
    CAPTURE(results->Diagnostics.back().ReadbackValid);
    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 1);
    REQUIRE(results->Frames.front().size() == static_cast<std::size_t>(width * height * 4U));
    REQUIRE(results->Diagnostics.size() == 1);
    CHECK(results->Diagnostics.front().PyramidValid);
    CHECK(results->Diagnostics.front().PyramidMipCount == 7U);
    CHECK(results->Diagnostics.front().Culled > 0U);
}

TEST_CASE("GPU occlusion debug views render real overlays and clamp hierarchical-depth mips")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::DebugViews));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 3);
    REQUIRE(results->Diagnostics.size() == 3);
    REQUIRE(results->DebugViews.size() == 3);
    REQUIRE(results->DebugMips.size() == 3);
    CHECK(results->DebugViews[0] == Keire::GpuOcclusionDebugView::None);
    CHECK(results->DebugViews[1] == Keire::GpuOcclusionDebugView::VisibilityBounds);
    CHECK(results->DebugViews[2] == Keire::GpuOcclusionDebugView::HierarchicalDepth);
    CHECK(results->DebugMips[0] == 0U);
    CHECK(results->DebugMips[1] == 0U);
    REQUIRE(results->Diagnostics[2].PyramidMipCount > 0U);
    CHECK(results->DebugMips[2] == results->Diagnostics[2].PyramidMipCount - 1U);
    CHECK(MaximumPixelDifference(results->Frames[0], results->Frames[1]) > MinimumBehaviorDelta);
    CHECK(MaximumPixelDifference(results->Frames[0], results->Frames[2]) > MinimumBehaviorDelta);
}

TEST_CASE("resize and mode changes clear stale GPU occlusion readback state")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::ResetToDisabled));
        REQUIRE(application.Run() == 0);
    }

    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Frames.size() == 2);
    REQUIRE(results->Diagnostics.size() == 2);
    CHECK(results->Diagnostics[0].State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(results->Diagnostics[0].ReadbackValid);
    CHECK(results->Diagnostics[0].Culled > 0U);
    CHECK(results->Diagnostics[1].RequestedMode == Keire::GpuOcclusionMode::Disabled);
    CHECK(results->Diagnostics[1].State == Keire::GpuOcclusionSurfaceState::Disabled);
    CHECK_FALSE(results->Diagnostics[1].PyramidValid);
    CHECK_FALSE(results->Diagnostics[1].ReadbackValid);
    CHECK(results->Diagnostics[1].Candidates == 0U);
    CHECK(results->Diagnostics[1].SourceFrame == 0U);
    CHECK(results->Diagnostics[1].ReadbackAge == std::numeric_limits<std::uint32_t>::max());
    CHECK(results->Frames[1].size() == static_cast<std::size_t>(101U * 67U * 4U));
}

TEST_CASE("active GPU occlusion survives minimize restore and Forced Disabled Forced epoch changes")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<GpuOcclusionCaptureResults>();
    auto specification = RenderTestSpecification();
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    {
        Keire::Application application(std::move(specification));
        (void)application.PushLayer(std::make_unique<GpuOcclusionCaptureLayer>(
            assets.CubeMesh, assets.ShaderGraphMaterial, Keire::GpuOcclusionMode::Forced, results,
            GpuOcclusionCaptureScenario::LifecycleReset));
        REQUIRE(application.Run() == 0);
    }

    CAPTURE(results->Diagnostics.size());
    CAPTURE(results->ObservedAfterReset.size());
    CAPTURE(results->ObservedAfterReforce.size());
    CAPTURE(results->ReforcedFrameFloor);
    if (!results->ObservedAfterReset.empty())
    {
        MESSAGE("last reset state=" << static_cast<int>(results->ObservedAfterReset.back().State) << " fallback="
                                    << static_cast<int>(results->ObservedAfterReset.back().FallbackReason)
                                    << " source=" << results->ObservedAfterReset.back().SourceFrame
                                    << " readback=" << results->ObservedAfterReset.back().ReadbackValid
                                    << " candidates=" << results->ObservedAfterReset.back().Candidates
                                    << " visible=" << results->ObservedAfterReset.back().Visible
                                    << " culled=" << results->ObservedAfterReset.back().Culled
                                    << " occluders=" << results->ObservedAfterReset.back().SafeOccluders
                                    << " pyramid=" << results->ObservedAfterReset.back().PyramidValid);
    }
    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->Diagnostics.size() == 5);
    REQUIRE(results->DebugViews.size() == 5);
    REQUIRE(results->DebugMips.size() == 5);
    CHECK(results->RestoredResourcesAvailableBeforeSubmission);
    CHECK_FALSE(results->RestoredOutputPublishedBeforeSubmission);
    const auto& initial = results->Diagnostics[0];
    const auto& minimized = results->Diagnostics[1];
    const auto& restored = results->Diagnostics[2];
    const auto& disabled = results->Diagnostics[3];
    const auto& reactivated = results->Diagnostics[4];
    CHECK(initial.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(initial.ReadbackValid);
    CHECK(initial.Culled > 0U);
    REQUIRE(initial.PyramidMipCount > 0U);
    CHECK(results->DebugViews[0] == Keire::GpuOcclusionDebugView::HierarchicalDepth);
    CHECK(results->DebugMips[0] == initial.PyramidMipCount - 1U);

    for (const auto* reset : {&minimized, &restored, &disabled})
    {
        CHECK_FALSE(reset->PyramidValid);
        CHECK_FALSE(reset->ReadbackValid);
        CHECK(reset->Candidates == 0U);
        CHECK(reset->SourceFrame == 0U);
        CHECK(reset->ReadbackAge == std::numeric_limits<std::uint32_t>::max());
    }
    CHECK(disabled.State == Keire::GpuOcclusionSurfaceState::Disabled);
    CHECK(results->DebugMips[1] == 0U);
    CHECK(results->DebugMips[2] == 0U);

    REQUIRE(results->ReforcedFrameFloor > initial.SourceFrame);
    CHECK(reactivated.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(reactivated.ReadbackValid);
    CHECK(reactivated.Culled > 0U);
    CHECK(reactivated.SourceFrame >= results->ReforcedFrameFloor);
    REQUIRE_FALSE(results->ObservedAfterReforce.empty());
    CHECK(std::ranges::all_of(
        results->ObservedAfterReforce, [&](const auto& diagnostics)
        { return !diagnostics.ReadbackValid || diagnostics.SourceFrame >= results->ReforcedFrameFloor; }));
}

TEST_CASE("GPU occlusion diagnostics snapshots remain coherent during publication")
{
    Keire::RenderBackend::RenderSurfaceState surface;
    Keire::GpuOcclusionSurfaceDiagnostics active;
    active.RequestedMode = Keire::GpuOcclusionMode::Forced;
    active.EffectiveMode = Keire::GpuOcclusionMode::Forced;
    active.State = Keire::GpuOcclusionSurfaceState::Active;
    active.SourceFrame = 23U;
    active.ReadbackAge = 1U;
    active.Candidates = 100U;
    active.Visible = 25U;
    active.Culled = 75U;
    active.SafeOccluders = 3U;
    active.PyramidMipCount = 7U;
    active.PyramidValid = true;
    active.ReadbackValid = true;

    Keire::GpuOcclusionSurfaceDiagnostics disabled;
    disabled.RequestedMode = Keire::GpuOcclusionMode::Disabled;
    disabled.State = Keire::GpuOcclusionSurfaceState::Disabled;
    disabled.FallbackReason = Keire::GpuOcclusionFallbackReason::DisabledBySetting;

    surface.GpuOcclusionDiagnostics = active;
    surface.PublishGpuOcclusionDiagnosticsSnapshot();
    std::atomic<bool> complete{false};
    std::thread publisher(
        [&]
        {
            for (std::uint32_t iteration = 0; iteration < 20'000U; ++iteration)
            {
                surface.GpuOcclusionDiagnostics = (iteration & 1U) == 0U ? disabled : active;
                surface.PublishGpuOcclusionDiagnosticsSnapshot();
            }
            complete.store(true, std::memory_order_release);
        });

    bool coherent = true;
    std::uint32_t observations = 0;
    do
    {
        const auto diagnostics = surface.GpuOcclusionDiagnosticsSnapshot();
        coherent = coherent && (diagnostics == active || diagnostics == disabled);
        ++observations;
    } while (!complete.load(std::memory_order_acquire));
    publisher.join();

    CHECK(coherent);
    CHECK(observations > 0U);
}
