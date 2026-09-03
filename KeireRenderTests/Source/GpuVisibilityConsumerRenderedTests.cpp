#include "KeireRenderTests/RenderAssetFixture.h"

#include "Keire/Application.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderSurfaceStateInternal.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using KeireRenderTests::Detail::RenderAssetFixture;

namespace
{
    enum class LocalLightScenario : std::uint8_t
    {
        Visible,
        Occluded,
        CameraInside
    };

    struct WorksetOwnershipSnapshot final
    {
        std::uint64_t Frame = 0;
        std::uint64_t SurfaceEpoch = 0;
        std::uint32_t FrameSlot = 0;
        std::uint32_t DeviceGeneration = 0;
        std::uint32_t LocalLightCount = 0;
        bool VisibilityOwned = false;
        bool ForwardPlusOwned = false;
        bool SameFrame = false;
        bool VisibilityCompacted = false;
        std::uint32_t VfxMaskCount = 0;
        std::uint32_t VfxOutputCount = 0;
        std::uint32_t VfxOutputCapacity = 0;
        std::uint32_t VfxOutputRenderer = 0;
        bool VfxOutputOwned = false;
        bool VfxSameFrame = false;
        bool VfxOutputResolved = false;
        bool VfxOutputBuffersReady = false;
        bool VfxOutputBuffersDistinct = false;
        bool VfxPublishedTupleRejectedByOtherWorksets = false;
        std::uint32_t VfxOwnedWorksetCount = 0;
        std::uintptr_t VfxIndicesIdentity = 0;
        std::uintptr_t VfxIndirectArgumentsIdentity = 0;
        std::uintptr_t VfxInstancesIdentity = 0;
        bool VfxStaleFrameRejected = false;
        bool VfxStaleSlotRejected = false;
        bool VfxStaleEpochRejected = false;
        bool VfxStaleDeviceRejected = false;
        std::uint32_t SpatialMaskCount = 0;
        std::uint32_t SpatialRecordCount = 0;
        std::uint32_t SpatialConsumedDraws = 0;
        bool SpatialOwned = false;
        bool SpatialSameFrame = false;
        bool SpatialDispatchSucceeded = false;
        bool SpatialOutputReady = false;
        bool SpatialStaleFrameRejected = false;
        bool SpatialStaleSlotRejected = false;
        bool SpatialStaleEpochRejected = false;
        bool SpatialStaleDeviceRejected = false;
    };

    struct LocalLightMaskResults final
    {
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> Diagnostics;
        std::array<WorksetOwnershipSnapshot, 3> Ownership;
        Keire::RenderCapabilities Capabilities;
        Keire::RenderStatistics Statistics;
        bool Captured = false;
        bool TimedOut = false;
    };

    [[nodiscard]] WorksetOwnershipSnapshot CaptureOwnership(Keire::RenderSystem& renderer,
                                                            const Keire::RenderSurface& surface,
                                                            const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics)
    {
        WorksetOwnershipSnapshot result;
        const auto state = std::static_pointer_cast<Keire::RenderBackend::RenderSurfaceState>(
            Keire::RenderSystemInternalAccess::SurfaceLease(surface));
        if (!state)
            return result;

        Keire::RenderSystemInternalAccess::RunOnRenderThread(
            renderer,
            [state, diagnostics, &result]
            {
                const auto slot = diagnostics.SourceFrameSlot;
                if (slot >= state->Resources.Worksets.size())
                    return;
                const auto& workset = state->Resources.Worksets[slot];
                const auto& visibility = workset.GpuOcclusion;
                const auto& forwardPlus = workset.ForwardPlus;
                result.Frame = diagnostics.SourceFrame;
                result.FrameSlot = slot;
                result.SurfaceEpoch = diagnostics.SourceSurfaceEpoch;
                result.DeviceGeneration = diagnostics.SourceDeviceGeneration;
                result.LocalLightCount = visibility.LocalLightVisibilityCount;
                result.VisibilityOwned =
                    visibility.OwnedBy(result.Frame, slot, result.SurfaceEpoch, result.DeviceGeneration);
                result.ForwardPlusOwned =
                    forwardPlus.OwnedBy(result.Frame, slot, result.SurfaceEpoch, result.DeviceGeneration);
                result.SameFrame = result.Frame != 0U && forwardPlus.FrameId == result.Frame &&
                                   forwardPlus.FrameSlot == result.FrameSlot &&
                                   forwardPlus.SurfaceEpoch == result.SurfaceEpoch &&
                                   forwardPlus.DeviceGeneration == result.DeviceGeneration;
                result.VisibilityCompacted = forwardPlus.VisibilityCompacted;
                const auto& gpuVfx = workset.GpuVfx;
                result.VfxMaskCount = visibility.VfxVisibilityCount;
                result.VfxOutputCount = static_cast<std::uint32_t>(gpuVfx.Outputs.size());
                result.VfxOutputOwned =
                    gpuVfx.OwnedBy(result.Frame, slot, result.SurfaceEpoch, result.DeviceGeneration);
                result.VfxSameFrame =
                    result.Frame != 0U && gpuVfx.FrameId == result.Frame && gpuVfx.FrameSlot == result.FrameSlot &&
                    gpuVfx.SurfaceEpoch == result.SurfaceEpoch && gpuVfx.DeviceGeneration == result.DeviceGeneration;
                if (!gpuVfx.Outputs.empty())
                {
                    const auto& output = gpuVfx.Outputs.front();
                    result.VfxOutputCapacity = output.Capacity;
                    result.VfxOutputRenderer = output.Renderer;
                    result.VfxOutputBuffersReady = output.Indices && output.IndirectArguments && output.Instances;
                    result.VfxOutputBuffersDistinct = output.Indices != output.IndirectArguments &&
                                                      output.Indices != output.Instances &&
                                                      output.IndirectArguments != output.Instances;
                    result.VfxIndicesIdentity = reinterpret_cast<std::uintptr_t>(output.Indices);
                    result.VfxIndirectArgumentsIdentity = reinterpret_cast<std::uintptr_t>(output.IndirectArguments);
                    result.VfxInstancesIdentity = reinterpret_cast<std::uintptr_t>(output.Instances);
                    result.VfxOutputResolved =
                        ResolveGpuVfxFrameOutput(gpuVfx, result.Frame, slot, result.SurfaceEpoch,
                                                 result.DeviceGeneration, output.WorldId, output.HandleIndex,
                                                 output.HandleGeneration) != nullptr;
                    result.VfxStaleFrameRejected =
                        ResolveGpuVfxFrameOutput(gpuVfx, result.Frame + 1U, slot, result.SurfaceEpoch,
                                                 result.DeviceGeneration, output.WorldId, output.HandleIndex,
                                                 output.HandleGeneration) == nullptr;
                    result.VfxStaleSlotRejected =
                        ResolveGpuVfxFrameOutput(gpuVfx, result.Frame, slot + 1U, result.SurfaceEpoch,
                                                 result.DeviceGeneration, output.WorldId, output.HandleIndex,
                                                 output.HandleGeneration) == nullptr;
                    result.VfxStaleEpochRejected =
                        ResolveGpuVfxFrameOutput(gpuVfx, result.Frame, slot, result.SurfaceEpoch + 1U,
                                                 result.DeviceGeneration, output.WorldId, output.HandleIndex,
                                                 output.HandleGeneration) == nullptr;
                    result.VfxStaleDeviceRejected =
                        ResolveGpuVfxFrameOutput(gpuVfx, result.Frame, slot, result.SurfaceEpoch,
                                                 result.DeviceGeneration + 1U, output.WorldId, output.HandleIndex,
                                                 output.HandleGeneration) == nullptr;

                    result.VfxPublishedTupleRejectedByOtherWorksets = true;
                    for (std::size_t worksetSlot = 0; worksetSlot < state->Resources.Worksets.size(); ++worksetSlot)
                    {
                        const auto& candidate = state->Resources.Worksets[worksetSlot].GpuVfx;
                        result.VfxOwnedWorksetCount +=
                            static_cast<std::uint32_t>(candidate.OwnershipValid && !candidate.Outputs.empty());
                        if (worksetSlot == slot)
                            continue;
                        result.VfxPublishedTupleRejectedByOtherWorksets =
                            result.VfxPublishedTupleRejectedByOtherWorksets &&
                            ResolveGpuVfxFrameOutput(candidate, result.Frame, slot, result.SurfaceEpoch,
                                                     result.DeviceGeneration, output.WorldId, output.HandleIndex,
                                                     output.HandleGeneration) == nullptr;
                    }
                }
                const auto& spatial = workset.SpatialSelection;
                result.SpatialMaskCount = spatial.SpatialMaskCount;
                result.SpatialRecordCount = spatial.RecordCount;
                result.SpatialConsumedDraws = spatial.ConsumedDraws;
                result.SpatialOwned = spatial.OwnedBy(result.Frame, slot, result.SurfaceEpoch, result.DeviceGeneration);
                result.SpatialSameFrame =
                    result.Frame != 0U && spatial.FrameId == result.Frame && spatial.FrameSlot == result.FrameSlot &&
                    spatial.SurfaceEpoch == result.SurfaceEpoch && spatial.DeviceGeneration == result.DeviceGeneration;
                result.SpatialDispatchSucceeded = spatial.DispatchSucceeded;
                result.SpatialOutputReady = spatial.OutputRecords.Buffer != nullptr;
                result.SpatialStaleFrameRejected =
                    !spatial.OwnedBy(result.Frame + 1U, slot, result.SurfaceEpoch, result.DeviceGeneration);
                result.SpatialStaleSlotRejected =
                    !spatial.OwnedBy(result.Frame, slot + 1U, result.SurfaceEpoch, result.DeviceGeneration);
                result.SpatialStaleEpochRejected =
                    !spatial.OwnedBy(result.Frame, slot, result.SurfaceEpoch + 1U, result.DeviceGeneration);
                result.SpatialStaleDeviceRejected =
                    !spatial.OwnedBy(result.Frame, slot, result.SurfaceEpoch, result.DeviceGeneration + 1U);
            });
        return result;
    }

    [[nodiscard]] constexpr Keire::AssetId VisibilityVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x5646585649534942ULL, value);
    }

    [[nodiscard]] Keire::Ref<Keire::VfxEffectAsset> VisibilityVfxEffect(const Keire::VfxRendererType rendererType)
    {
        Keire::VfxRendererModule renderer;
        renderer.Type = rendererType;
        Keire::VfxShapeModule shape;
        shape.Shape = Keire::VfxShape::Box;
        shape.BoxHalfExtent = {0.12F, 0.12F, 0.02F};

        Keire::VfxEffectDefinition definition;
        definition.EmitterId = VisibilityVfxId(rendererType == Keire::VfxRendererType::Ribbon ? 100U : 200U);
        definition.Name = rendererType == Keire::VfxRendererType::Ribbon ? "Whole-ribbon visibility consumer"
                                                                         : "Sprite visibility consumer";
        definition.Duration = 4.0F;
        definition.Capacity = 4U;
        definition.Modules = {
            {VisibilityVfxId(1U), true, Keire::VfxBurstModule{0.0F, 4U, 1U, 0.1F}},
            {VisibilityVfxId(2U), true, shape},
            {VisibilityVfxId(3U), true, Keire::VfxInitializeModule{4.0F, 4.0F, {}, {}, {}, {}}},
            {VisibilityVfxId(4U), true,
             Keire::VfxSizeOverLifetimeModule{
                 Keire::Curve1D::Constant(rendererType == Keire::VfxRendererType::Ribbon ? 0.35F : 0.6F)}},
            {VisibilityVfxId(5U), true,
             Keire::VfxColorOverLifetimeModule{Keire::ColorGradient::Constant({1.0F, 0.0F, 0.0F, 1.0F})}},
            {VisibilityVfxId(6U), true, renderer},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        if (rendererType == Keire::VfxRendererType::Ribbon)
        {
            definition.Systems.front().DataType = Keire::VfxParticleDataType::ParticleStrip;
            definition.Systems.front().ParticlesPerStrip = 4U;
        }
        Keire::ValidateVfxEffect(definition);
        const auto compiled = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        if (!compiled.Valid)
            throw std::logic_error("GPU VFX visibility-consumer effect did not compile.");
        return Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition));
    }

    [[nodiscard]] std::size_t RedDominantPixels(const std::vector<std::uint8_t>& pixels) noexcept
    {
        std::size_t result = 0;
        for (std::size_t offset = 0; offset + 3U < pixels.size(); offset += 4U)
        {
            constexpr std::uint8_t dominance = 32U;
            const auto red = static_cast<std::uint16_t>(pixels[offset]);
            const auto green = static_cast<std::uint16_t>(pixels[offset + 1U]);
            const auto blue = static_cast<std::uint16_t>(pixels[offset + 2U]);
            result += static_cast<std::size_t>(red > green + dominance && red > blue + dominance);
        }
        return result;
    }

    [[nodiscard]] std::size_t GreenDominantPixels(const std::vector<std::uint8_t>& pixels) noexcept
    {
        std::size_t result = 0;
        for (std::size_t offset = 0; offset + 3U < pixels.size(); offset += 4U)
        {
            constexpr std::uint8_t dominance = 32U;
            const auto red = static_cast<std::uint16_t>(pixels[offset]);
            const auto green = static_cast<std::uint16_t>(pixels[offset + 1U]);
            const auto blue = static_cast<std::uint16_t>(pixels[offset + 2U]);
            result += static_cast<std::size_t>(green > red + dominance && green > blue + dominance);
        }
        return result;
    }

    class LocalLightMaskLayer final : public Keire::Layer
    {
      public:
        LocalLightMaskLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                            std::shared_ptr<LocalLightMaskResults> results)
            : Layer("GPU local-light visibility-mask consumers"), m_Mesh(mesh), m_Material(material),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            constexpr std::array scenarios{LocalLightScenario::Visible, LocalLightScenario::Occluded,
                                           LocalLightScenario::CameraInside};
            for (std::size_t index = 0; index < scenarios.size(); ++index)
            {
                m_Scenes[index] = CreateScene(scenarios[index], index);

                Keire::RenderSurfaceSpecification surface;
                surface.Name = "GPU local-light visibility mask " + std::to_string(index);
                surface.Width = SurfaceSize;
                surface.Height = SurfaceSize;
                surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
                surface.SampleCount = Keire::RenderSampleCount::One;
                surface.Depth = true;
                m_Views[index] = Owner().Renderer()->CreateView(surface);

                Keire::RenderCamera camera;
                camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
                camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
                camera.FarPlane = 100.0F;
                camera.ClearColor = surface.ClearColor;
                m_Views[index]->SetCamera(camera);
            }

            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (m_SubmittedFrames > 0U)
            {
                renderer->Flush();
                const bool resourcesReady =
                    Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(*renderer) > 0U;
                bool diagnosticsReady = resourcesReady && m_SubmittedFrames >= 6U;
                for (std::size_t index = 0; index < m_Views.size(); ++index)
                {
                    const auto diagnostics = m_Views[index]->Surface()->OcclusionDiagnostics();
                    const auto ownership = CaptureOwnership(*renderer, *m_Views[index]->Surface(), diagnostics);
                    m_LastDiagnostics[index] = diagnostics;
                    m_LastOwnership[index] = ownership;
                    diagnosticsReady = diagnosticsReady &&
                                       diagnostics.State == Keire::GpuOcclusionSurfaceState::Active &&
                                       diagnostics.ReadbackValid && diagnostics.Candidates >= 1U &&
                                       diagnostics.LocalLightCandidates == 1U && diagnostics.LocalLightMaskConsumed &&
                                       ownership.VisibilityOwned && ownership.ForwardPlusOwned && ownership.SameFrame;
                }

                if (diagnosticsReady || m_SubmittedFrames >= 120U)
                {
                    Capture(*renderer);
                    m_Results->TimedOut = !diagnosticsReady;
                    Owner().RequestExit();
                    return;
                }
            }

            for (std::size_t index = 0; index < m_Views.size(); ++index)
                renderer->Submit({m_Scenes[index], m_Views[index], false, m_Environment});
            ++m_SubmittedFrames;
        }

        void OnDetach() noexcept override
        {
            if (!m_Results->Captured && Owner().Renderer())
            {
                try
                {
                    Capture(*Owner().Renderer());
                }
                catch (...)
                {
                }
            }
            for (auto& scene : m_Scenes)
            {
                if (scene)
                    scene->Close();
                scene.Reset();
            }
            for (auto& view : m_Views)
                view.Reset();
        }

      private:
        [[nodiscard]] Keire::Ref<Keire::Scene> CreateScene(const LocalLightScenario scenario,
                                                           const std::size_t index) const
        {
            const auto scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("GPU local-light mask scene"),
                Keire::ComponentRegistry::CreateDefault());

            auto wall = scene->CreateEntity("Conservative depth occluder");
            wall.GetComponent<Keire::TransformComponent>()->SetLocalScale({2.2F, 2.2F, 0.2F});
            const auto wallRenderer = wall.AddComponent<Keire::MeshRendererComponent>();
            wallRenderer->SetMesh(m_Mesh);
            wallRenderer->SetMaterial(m_Material);

            auto lightEntity = scene->CreateEntity("Forward+ mask light " + std::to_string(index));
            auto position = Keire::Vector3{0.0F, 0.0F, 1.5F};
            float range = 0.25F;
            if (scenario == LocalLightScenario::Occluded)
                position = {0.0F, 0.0F, -1.5F};
            else if (scenario == LocalLightScenario::CameraInside)
            {
                position = {0.0F, 0.0F, 5.0F};
                range = 1.0F;
            }
            lightEntity.GetComponent<Keire::TransformComponent>()->SetLocalPosition(position);
            const auto light = lightEntity.AddComponent<Keire::PointLightComponent>();
            light->SetIntensity(8.0F);
            light->SetRange(range);
            light->SetShadows(Keire::ShadowQuality::Disabled);
            return scene;
        }

        void Capture(Keire::RenderSystem& renderer)
        {
            renderer.Flush();
            m_Results->Capabilities = renderer.Capabilities();
            m_Results->Statistics = renderer.Statistics();
            m_Results->Diagnostics = m_LastDiagnostics;
            m_Results->Ownership = m_LastOwnership;
            m_Results->Captured = true;
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<LocalLightMaskResults> m_Results;
        std::array<Keire::Ref<Keire::Scene>, 3> m_Scenes;
        std::array<Keire::Ref<Keire::RenderView>, 3> m_Views;
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> m_LastDiagnostics;
        std::array<WorksetOwnershipSnapshot, 3> m_LastOwnership;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint32_t m_SubmittedFrames = 0;
    };

    struct SpatialMaskResults final
    {
        Keire::GpuOcclusionSurfaceDiagnostics Diagnostics;
        WorksetOwnershipSnapshot Ownership;
        Keire::RenderCapabilities Capabilities;
        Keire::RenderStatistics Statistics;
        std::vector<std::uint8_t> Pixels;
        bool Captured = false;
        bool TimedOut = false;
    };

    class SpatialMaskLayer final : public Keire::Layer
    {
      public:
        SpatialMaskLayer(Keire::Ref<Keire::Scene> scene, std::shared_ptr<SpatialMaskResults> results)
            : Layer("GPU spatial-volume visibility-mask consumer"), m_Scene(std::move(scene)),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            Keire::RenderSurfaceSpecification surface;
            surface.Name = "GPU spatial-volume visibility mask";
            surface.Width = SurfaceSize;
            surface.Height = SurfaceSize;
            surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
            surface.SampleCount = Keire::RenderSampleCount::One;
            surface.Depth = true;
            m_View = Owner().Renderer()->CreateView(surface);

            Keire::RenderCamera camera;
            camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
            camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
            camera.FarPlane = 100.0F;
            camera.ClearColor = surface.ClearColor;
            m_View->SetCamera(camera);

            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (m_SubmittedFrames > 0U)
            {
                renderer->Flush();
                const auto diagnostics = m_View->Surface()->OcclusionDiagnostics();
                const auto ownership = CaptureOwnership(*renderer, *m_View->Surface(), diagnostics);
                const bool ready =
                    diagnostics.State == Keire::GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid &&
                    diagnostics.SpatialMaskEntries == 1U && diagnostics.SpatialSelectionRecords == 2U &&
                    diagnostics.SpatialSelectionDraws == 2U && diagnostics.SpatialMaskConsumed &&
                    ownership.SpatialOwned && ownership.SpatialSameFrame && ownership.SpatialDispatchSucceeded &&
                    ownership.SpatialOutputReady && ownership.SpatialConsumedDraws == 2U;
                if (ready || m_SubmittedFrames >= 120U)
                {
                    m_Results->Diagnostics = diagnostics;
                    m_Results->Ownership = ownership;
                    m_Results->Capabilities = renderer->Capabilities();
                    m_Results->Statistics = renderer->Statistics();
                    m_Results->Pixels = Keire::RenderSystemInternalAccess::ReadbackRGBA8(*renderer, *m_View->Surface());
                    m_Results->Captured = true;
                    m_Results->TimedOut = !ready;
                    Owner().RequestExit();
                    return;
                }
            }

            renderer->Submit({m_Scene, m_View, false, m_Environment});
            ++m_SubmittedFrames;
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
            m_Scene.Reset();
            m_View.Reset();
        }

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        std::shared_ptr<SpatialMaskResults> m_Results;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::RenderEnvironmentSettings m_Environment;
        std::uint32_t m_SubmittedFrames = 0U;
    };

    enum class VfxMaskScenario : std::uint8_t
    {
        VisibleSprite,
        OccludedSprite,
        WholeRibbon
    };

    struct VfxMaskRecoveryResults final
    {
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> BeforeDiagnostics;
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> AfterDiagnostics;
        std::array<Keire::GpuOcclusionSurfaceDiagnostics, 3> LastDiagnostics;
        std::array<WorksetOwnershipSnapshot, 3> BeforeOwnership;
        std::array<WorksetOwnershipSnapshot, 3> AfterOwnership;
        std::array<WorksetOwnershipSnapshot, 3> LastOwnership;
        std::array<std::vector<std::uint8_t>, 3> BeforePixels;
        std::array<std::vector<std::uint8_t>, 3> AfterPixels;
        Keire::RenderCapabilities Capabilities;
        Keire::RenderStatistics Statistics;
        std::optional<Keire::GpuDeviceLossDiagnostic> Recovery;
        bool CapturedBefore = false;
        bool CapturedAfter = false;
        bool TimedOut = false;
    };

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    class VfxMaskRecoveryLayer final : public Keire::Layer
    {
      public:
        VfxMaskRecoveryLayer(const Keire::AssetId mesh, const Keire::AssetId material,
                             std::shared_ptr<VfxMaskRecoveryResults> results)
            : Layer("GPU VFX visibility-mask recovery"), m_Mesh(mesh), m_Material(material),
              m_Results(std::move(results))
        {
        }

      protected:
        void OnAttach() override
        {
            Owner().Renderer()->RequestGpuVfxPipelineWarmup();
            constexpr std::array scenarios{VfxMaskScenario::VisibleSprite, VfxMaskScenario::OccludedSprite,
                                           VfxMaskScenario::WholeRibbon};
            for (std::size_t index = 0; index < scenarios.size(); ++index)
            {
                m_Scenes[index] = CreateScene(index);
                Keire::RenderSurfaceSpecification surface;
                surface.Name = "GPU VFX visibility mask " + std::to_string(index);
                surface.Width = SurfaceSize;
                surface.Height = SurfaceSize;
                surface.ClearColor = {0.0F, 0.0F, 0.0F, 1.0F};
                surface.SampleCount = Keire::RenderSampleCount::One;
                surface.Depth = true;
                m_Views[index] = Owner().Renderer()->CreateView(surface);

                Keire::RenderCamera camera;
                camera.View = Keire::Math::LookAt({0.0F, 0.0F, 5.0F}, {}, {0.0F, 1.0F, 0.0F});
                camera.Projection = Keire::Math::Perspective(55.0F, 1.0F, 0.1F, 100.0F);
                camera.FarPlane = 100.0F;
                camera.ClearColor = surface.ClearColor;
                m_Views[index]->SetCamera(camera);

                const auto rendererType = scenarios[index] == VfxMaskScenario::WholeRibbon
                                              ? Keire::VfxRendererType::Ribbon
                                              : Keire::VfxRendererType::Sprite;
                m_Effects[index] = VisibilityVfxEffect(rendererType);
                m_Worlds[index] = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                    .MaximumEffects = 1U, .MaximumParticles = 4U, .Backend = Keire::VfxBackend::Gpu});
                const auto handle = m_Worlds[index]->Activate({m_Effects[index]});
                if (!handle)
                    throw std::logic_error("Could not activate the GPU VFX visibility-consumer effect.");
                const auto position = scenarios[index] == VfxMaskScenario::OccludedSprite
                                          ? Keire::Vector3{0.0F, 0.0F, -1.5F}
                                          : Keire::Vector3{0.0F, 0.0F, 1.5F};
                m_Worlds[index]->SetTransform(handle, position, {});
                m_Worlds[index]->Update(0.01F);
            }

            m_Environment.AmbientColor = {1.0F, 1.0F, 1.0F, 1.0F};
            m_Environment.AmbientIntensity = 1.0F;
            m_Environment.SkyVisible = false;
            m_Environment.GpuOcclusion = Keire::GpuOcclusionMode::Forced;
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto renderer = Owner().Renderer();
            if (std::chrono::steady_clock::now() - m_Started > std::chrono::seconds(30))
            {
                m_Results->TimedOut = true;
                Owner().RequestExit();
                return;
            }
            if (m_Phase == Phase::AwaitRecovery)
            {
                const auto recovery = renderer->LastDeviceLoss();
                if (!recovery || !recovery->RecoverySucceeded)
                    return;
                m_Results->Recovery = recovery;
                renderer->RequestGpuVfxPipelineWarmup();
                m_Phase = Phase::AwaitRecoveredPipelines;
                return;
            }
            if (!renderer->Statistics().VfxPipelinesReady)
                return;
            if (m_Phase == Phase::AwaitRecoveredPipelines)
            {
                SubmitAll(*renderer);
                m_Phase = Phase::AwaitRestoredOutput;
                return;
            }

            if (m_SubmittedFrames > 0U)
            {
                renderer->Flush();
                std::array<WorksetOwnershipSnapshot, 3> ownership;
                if (Ready(*renderer, ownership))
                {
                    if (m_Phase == Phase::AwaitInitialOutput)
                    {
                        Capture(*renderer, ownership, false);
                        Keire::RenderSystemInternalAccess::InjectDeviceLoss(*renderer);
                        SubmitAll(*renderer);
                        m_Phase = Phase::AwaitRecovery;
                        return;
                    }
                    Capture(*renderer, ownership, true);
                    Owner().RequestExit();
                    m_Phase = Phase::Complete;
                    return;
                }
            }

            SubmitAll(*renderer);
            if (m_Phase == Phase::Warmup)
                m_Phase = Phase::AwaitInitialOutput;
        }

        void OnDetach() noexcept override
        {
            for (auto& world : m_Worlds)
                world.Reset();
            for (auto& effect : m_Effects)
                effect.Reset();
            for (auto& scene : m_Scenes)
            {
                if (scene)
                    scene->Close();
                scene.Reset();
            }
            for (auto& view : m_Views)
                view.Reset();
        }

      private:
        enum class Phase : std::uint8_t
        {
            Warmup,
            AwaitInitialOutput,
            AwaitRecovery,
            AwaitRecoveredPipelines,
            AwaitRestoredOutput,
            Complete
        };

        [[nodiscard]] Keire::Ref<Keire::Scene> CreateScene(const std::size_t index) const
        {
            const auto scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("GPU VFX visibility-mask scene"),
                Keire::ComponentRegistry::CreateDefault());
            auto wall = scene->CreateEntity("GPU VFX depth occluder " + std::to_string(index));
            wall.GetComponent<Keire::TransformComponent>()->SetLocalScale({2.2F, 2.2F, 0.2F});
            const auto wallRenderer = wall.AddComponent<Keire::MeshRendererComponent>();
            wallRenderer->SetMesh(m_Mesh);
            wallRenderer->SetMaterial(m_Material);
            return scene;
        }

        [[nodiscard]] bool Ready(Keire::RenderSystem& renderer,
                                 std::array<WorksetOwnershipSnapshot, 3>& ownership) const
        {
            constexpr std::array<std::uint32_t, 3> expectedMaskCounts{4U, 4U, 1U};
            bool ready = Keire::RenderSystemInternalAccess::MaterialBindingBuildCount(renderer) > 0U;
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                const auto diagnostics = m_Views[index]->Surface()->OcclusionDiagnostics();
                ownership[index] = CaptureOwnership(renderer, *m_Views[index]->Surface(), diagnostics);
                m_Results->LastDiagnostics[index] = diagnostics;
                m_Results->LastOwnership[index] = ownership[index];
                ready = ready && diagnostics.State == Keire::GpuOcclusionSurfaceState::Active &&
                        diagnostics.ReadbackValid && diagnostics.VfxMaskEntries == expectedMaskCounts[index] &&
                        diagnostics.VfxMaskedDraws == 1U && diagnostics.VfxMaskConsumed &&
                        ownership[index].VfxMaskCount == expectedMaskCounts[index] &&
                        ownership[index].VfxOutputCount == 1U && ownership[index].VisibilityOwned &&
                        ownership[index].VfxOutputOwned && ownership[index].VfxSameFrame &&
                        ownership[index].VfxOutputResolved && ownership[index].VfxOutputBuffersReady &&
                        ownership[index].VfxOutputBuffersDistinct &&
                        ownership[index].VfxPublishedTupleRejectedByOtherWorksets &&
                        ownership[index].VfxOwnedWorksetCount == 2U;
            }
            return ready;
        }

        void Capture(Keire::RenderSystem& renderer, const std::array<WorksetOwnershipSnapshot, 3>& ownership,
                     const bool afterRecovery)
        {
            auto& diagnostics = afterRecovery ? m_Results->AfterDiagnostics : m_Results->BeforeDiagnostics;
            auto& capturedOwnership = afterRecovery ? m_Results->AfterOwnership : m_Results->BeforeOwnership;
            auto& pixels = afterRecovery ? m_Results->AfterPixels : m_Results->BeforePixels;
            diagnostics = m_Results->LastDiagnostics;
            capturedOwnership = ownership;
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                pixels[index] = Keire::RenderSystemInternalAccess::ReadbackRGBA8(renderer, *m_Views[index]->Surface());
            }
            m_Results->Capabilities = renderer.Capabilities();
            m_Results->Statistics = renderer.Statistics();
            if (afterRecovery)
                m_Results->CapturedAfter = true;
            else
                m_Results->CapturedBefore = true;
        }

        void SubmitAll(Keire::RenderSystem& renderer)
        {
            for (std::size_t index = 0; index < m_Views.size(); ++index)
            {
                renderer.Submit({m_Scenes[index],
                                 m_Views[index],
                                 false,
                                 m_Environment,
                                 {},
                                 m_Worlds[index]->CaptureRenderSnapshot()});
            }
            ++m_SubmittedFrames;
        }

        Keire::AssetId m_Mesh;
        Keire::AssetId m_Material;
        std::shared_ptr<VfxMaskRecoveryResults> m_Results;
        std::array<Keire::Ref<Keire::Scene>, 3> m_Scenes;
        std::array<Keire::Ref<Keire::RenderView>, 3> m_Views;
        std::array<Keire::Ref<Keire::VfxEffectAsset>, 3> m_Effects;
        std::array<Keire::Ref<Keire::VfxWorld>, 3> m_Worlds;
        Keire::RenderEnvironmentSettings m_Environment;
        Phase m_Phase = Phase::Warmup;
        std::uint32_t m_SubmittedFrames = 0U;
        std::chrono::steady_clock::time_point m_Started = std::chrono::steady_clock::now();
    };
#endif
} // namespace

TEST_CASE("same-frame Forward+ local-light masks stay owned across multi-surface depths one through three")
{
    RenderAssetFixture assets(true);
    for (const auto depth : {1U, 2U, 3U})
    {
        CAPTURE(depth);
        auto results = std::make_shared<LocalLightMaskResults>();
        auto specification = RenderTestSpecification();
        specification.Render.MaximumFramesInFlight = depth;
        specification.Assets.Mode = Keire::AssetMode::Development;
        specification.Assets.DevelopmentCatalog = assets.Catalog;
        Keire::Application application(specification);
        (void)application.PushLayer(
            std::make_unique<LocalLightMaskLayer>(assets.CubeMesh, assets.ShaderGraphMaterial, results));
        REQUIRE(application.Run() == 0);

        REQUIRE(results->Captured);
        CHECK_FALSE(results->TimedOut);
        CHECK(results->Capabilities.GpuOcclusionCulling);
        CHECK(results->Capabilities.GpuOcclusionLocalLightMasks);
        CHECK(results->Statistics.AllowedFramesInFlight == depth);
        CHECK(results->Statistics.FramesInFlightHighWaterMark <= depth);
        CHECK(results->Statistics.GpuOcclusionLocalLightCandidates == 3U);

        for (std::size_t index = 0; index < results->Diagnostics.size(); ++index)
        {
            CAPTURE(index);
            const auto& diagnostics = results->Diagnostics[index];
            const auto& ownership = results->Ownership[index];
            CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
            CHECK(diagnostics.ReadbackValid);
            CHECK(diagnostics.Candidates >= 1U);
            CHECK(diagnostics.SourceFrame == ownership.Frame);
            CHECK(diagnostics.SourceFrameSlot == ownership.FrameSlot);
            CHECK(diagnostics.SourceSurfaceEpoch == ownership.SurfaceEpoch);
            CHECK(diagnostics.SourceDeviceGeneration == ownership.DeviceGeneration);
            CHECK(diagnostics.LocalLightCandidates == 1U);
            CHECK(diagnostics.LocalLightMaskConsumed);
            CHECK(ownership.Frame > 0U);
            CHECK(ownership.FrameSlot < depth);
            CHECK(ownership.LocalLightCount == 1U);
            CHECK(ownership.VisibilityOwned);
            CHECK(ownership.ForwardPlusOwned);
            CHECK(ownership.SameFrame);
            CHECK(ownership.VisibilityCompacted);
        }

        const auto& visible = results->Diagnostics[static_cast<std::size_t>(LocalLightScenario::Visible)];
        const auto& occluded = results->Diagnostics[static_cast<std::size_t>(LocalLightScenario::Occluded)];
        const auto& cameraInside = results->Diagnostics[static_cast<std::size_t>(LocalLightScenario::CameraInside)];
        CHECK(visible.LocalLightVisible == 1U);
        CHECK(visible.LocalLightCulled == 0U);
        CHECK(occluded.LocalLightVisible == 0U);
        CHECK(occluded.LocalLightCulled == 1U);
        CHECK(cameraInside.LocalLightVisible == 1U);
        CHECK(cameraInside.LocalLightCulled == 0U);
    }
}

TEST_CASE("ABI-v3 spatial selection consumes exact frame-owned output in a rendered draw")
{
    RenderAssetFixture assets(true);
    const auto scene = Keire::CreateRef<Keire::Scene>(
        Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("GPU spatial visibility consumer scene"),
        Keire::ComponentRegistry::CreateDefault());

    auto wall = scene->CreateEntity("Conservative depth occluder");
    wall.GetComponent<Keire::TransformComponent>()->SetLocalScale({2.2F, 2.2F, 0.2F});
    const auto wallRenderer = wall.AddComponent<Keire::MeshRendererComponent>();
    wallRenderer->SetMesh(assets.CubeMesh);
    wallRenderer->SetMaterial(assets.ShaderGraphMaterial);

    auto selected = scene->CreateEntity("ABI-v3 spatially selected draw");
    selected.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, 1.5F});
    selected.GetComponent<Keire::TransformComponent>()->SetLocalScale({0.3F, 0.3F, 0.3F});
    const auto selectedRenderer = selected.AddComponent<Keire::MeshRendererComponent>();
    selectedRenderer->SetMesh(assets.CubeMesh);
    selectedRenderer->SetMaterial(assets.Material);

    auto probe = scene->CreateEntity("Spatial visibility reflection probe");
    probe.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, 1.0F});
    const auto reflection = probe.AddComponent<Keire::ReflectionProbeComponent>();
    reflection->SetBoxExtents({2.0F, 2.0F, 2.0F});
    reflection->SetBlendDistance(0.25F);

    Keire::LightingSetDefinition lighting;
    lighting.Scene = scene->Asset();
    lighting.InputFingerprint = std::string(64U, 'a');
    lighting.ReflectionProbes.push_back({probe.Id().Value(), 0U});
    const auto lightingImporter = Keire::CreateLightingSetAssetImporter();
    const auto lightingSet = assets.Database->CreateAsset("SpatialVisibility.keirelighting", lightingImporter,
                                                          Keire::LightingSetAsset::Encode(lighting));
    scene->SetBakedLighting(lightingSet);
    assets.Catalog = assets.Database->ImportAll(Keire::AssetImportPolicy::KeepLastGood).CatalogPath;

    auto results = std::make_shared<SpatialMaskResults>();
    auto specification = RenderTestSpecification();
    specification.Render.MaximumFramesInFlight = 2U;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<SpatialMaskLayer>(scene, results));
    REQUIRE(application.Run() == 0);

    REQUIRE(results->Captured);
    CHECK_FALSE(results->TimedOut);
    CHECK(results->Capabilities.GpuOcclusionSpatialVolumeMasks);
    CHECK(results->Statistics.GpuOcclusionSpatialVolumeCandidates == 1U);
    CHECK(GreenDominantPixels(results->Pixels) > 4U);

    const auto& diagnostics = results->Diagnostics;
    const auto& ownership = results->Ownership;
    CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
    CHECK(diagnostics.ReadbackValid);
    CHECK(diagnostics.SourceFrame == ownership.Frame);
    CHECK(diagnostics.SourceFrameSlot == ownership.FrameSlot);
    CHECK(diagnostics.SourceSurfaceEpoch == ownership.SurfaceEpoch);
    CHECK(diagnostics.SourceDeviceGeneration == ownership.DeviceGeneration);
    CHECK(diagnostics.SpatialMaskEntries == 1U);
    CHECK(diagnostics.SpatialSelectionRecords == 2U);
    CHECK(diagnostics.SpatialSelectionDraws == 2U);
    CHECK(diagnostics.SpatialMaskConsumed);
    CHECK(ownership.VisibilityOwned);
    CHECK(ownership.SpatialMaskCount == 1U);
    CHECK(ownership.SpatialRecordCount == 2U);
    CHECK(ownership.SpatialConsumedDraws == 2U);
    CHECK(ownership.SpatialOwned);
    CHECK(ownership.SpatialSameFrame);
    CHECK(ownership.SpatialDispatchSucceeded);
    CHECK(ownership.SpatialOutputReady);
    CHECK(ownership.SpatialStaleFrameRejected);
    CHECK(ownership.SpatialStaleSlotRejected);
    CHECK(ownership.SpatialStaleEpochRejected);
    CHECK(ownership.SpatialStaleDeviceRejected);
}

#if defined(KEIRE_ENABLE_TEST_HOOKS)
TEST_CASE("GPU VFX mask outputs remain frame-owned and rebuild after device recovery")
{
    RenderAssetFixture assets(true);
    auto results = std::make_shared<VfxMaskRecoveryResults>();
    auto specification = RenderTestSpecification();
    specification.Render.MaximumFramesInFlight = 2U;
    specification.Render.DeviceLossRecoveryAttempts = 1U;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = assets.Catalog;
    Keire::Application application(specification);
    (void)application.PushLayer(
        std::make_unique<VfxMaskRecoveryLayer>(assets.CubeMesh, assets.ShaderGraphMaterial, results));
    REQUIRE(application.Run() == 0);

    std::ostringstream readiness;
    for (std::size_t index = 0; index < results->LastDiagnostics.size(); ++index)
    {
        const auto& diagnostics = results->LastDiagnostics[index];
        const auto& ownership = results->LastOwnership[index];
        readiness << "surface=" << index << " state=" << static_cast<std::uint32_t>(diagnostics.State)
                  << " readback=" << diagnostics.ReadbackValid << " candidates=" << diagnostics.Candidates
                  << " mask=" << ownership.VfxMaskCount << " outputs=" << ownership.VfxOutputCount
                  << " visibilityOwned=" << ownership.VisibilityOwned << " outputOwned=" << ownership.VfxOutputOwned
                  << " sameFrame=" << ownership.VfxSameFrame << " resolved=" << ownership.VfxOutputResolved
                  << " buffers=" << ownership.VfxOutputBuffersReady
                  << " distinct=" << ownership.VfxOutputBuffersDistinct
                  << " rejectsOther=" << ownership.VfxPublishedTupleRejectedByOtherWorksets
                  << " ownedWorksets=" << ownership.VfxOwnedWorksetCount << ';';
    }
    const auto readinessDiagnostic = readiness.str();
    CAPTURE(readinessDiagnostic);
    REQUIRE_FALSE(results->TimedOut);
    REQUIRE(results->CapturedBefore);
    REQUIRE(results->CapturedAfter);
    REQUIRE(results->Recovery);
    CHECK(results->Recovery->RecoverySucceeded);
    CHECK(results->Recovery->RecoveredDeviceGeneration > results->Recovery->DeviceGeneration);
    CHECK(results->Capabilities.GpuOcclusionVfxVisibilityMasks);
    CHECK(results->Statistics.AllowedFramesInFlight == 2U);
    CHECK(results->Statistics.FramesInFlightHighWaterMark <= 2U);
    CHECK(results->Statistics.VfxComputeDispatches > 0U);
    CHECK(results->Statistics.VfxIndirectDraws > 0U);

    constexpr std::array<std::uint32_t, 3> expectedMaskCounts{4U, 4U, 1U};
    constexpr std::array expectedRenderers{Keire::VfxRendererType::Sprite, Keire::VfxRendererType::Sprite,
                                           Keire::VfxRendererType::Ribbon};
    for (std::size_t index = 0; index < results->BeforeOwnership.size(); ++index)
    {
        CAPTURE(index);
        const auto assertOwnership =
            [index, &expectedMaskCounts, &expectedRenderers](const WorksetOwnershipSnapshot& ownership)
        {
            CHECK(ownership.Frame > 0U);
            CHECK(ownership.FrameSlot < 2U);
            CHECK(ownership.VfxMaskCount == expectedMaskCounts[index]);
            CHECK(ownership.VfxOutputCount == 1U);
            CHECK(ownership.VfxOutputCapacity == 4U);
            CHECK(ownership.VfxOutputRenderer == static_cast<std::uint32_t>(expectedRenderers[index]));
            CHECK(ownership.VisibilityOwned);
            CHECK(ownership.VfxOutputOwned);
            CHECK(ownership.VfxSameFrame);
            CHECK(ownership.VfxOutputResolved);
            CHECK(ownership.VfxOutputBuffersReady);
            CHECK(ownership.VfxOutputBuffersDistinct);
            CHECK(ownership.VfxPublishedTupleRejectedByOtherWorksets);
            CHECK(ownership.VfxOwnedWorksetCount == 2U);
            CHECK(ownership.VfxIndicesIdentity != 0U);
            CHECK(ownership.VfxIndirectArgumentsIdentity != 0U);
            CHECK(ownership.VfxInstancesIdentity != 0U);
            CHECK(ownership.VfxStaleFrameRejected);
            CHECK(ownership.VfxStaleSlotRejected);
            CHECK(ownership.VfxStaleEpochRejected);
            CHECK(ownership.VfxStaleDeviceRejected);
        };
        assertOwnership(results->BeforeOwnership[index]);
        assertOwnership(results->AfterOwnership[index]);
        CHECK(results->AfterOwnership[index].Frame > results->BeforeOwnership[index].Frame);
        CHECK(results->AfterOwnership[index].SurfaceEpoch == results->BeforeOwnership[index].SurfaceEpoch);
        CHECK(results->AfterOwnership[index].DeviceGeneration > results->BeforeOwnership[index].DeviceGeneration);
        CHECK(results->AfterOwnership[index].DeviceGeneration == results->Recovery->RecoveredDeviceGeneration);
    }

    for (std::size_t first = 0; first < results->BeforeOwnership.size(); ++first)
    {
        for (std::size_t second = first + 1U; second < results->BeforeOwnership.size(); ++second)
        {
            CAPTURE(first);
            CAPTURE(second);
            CHECK(results->BeforeOwnership[first].VfxIndicesIdentity !=
                  results->BeforeOwnership[second].VfxIndicesIdentity);
            CHECK(results->BeforeOwnership[first].VfxIndirectArgumentsIdentity !=
                  results->BeforeOwnership[second].VfxIndirectArgumentsIdentity);
            CHECK(results->BeforeOwnership[first].VfxInstancesIdentity !=
                  results->BeforeOwnership[second].VfxInstancesIdentity);
            CHECK(results->AfterOwnership[first].VfxIndicesIdentity !=
                  results->AfterOwnership[second].VfxIndicesIdentity);
            CHECK(results->AfterOwnership[first].VfxIndirectArgumentsIdentity !=
                  results->AfterOwnership[second].VfxIndirectArgumentsIdentity);
            CHECK(results->AfterOwnership[first].VfxInstancesIdentity !=
                  results->AfterOwnership[second].VfxInstancesIdentity);
        }
    }

    const auto visibleIndex = static_cast<std::size_t>(VfxMaskScenario::VisibleSprite);
    const auto occludedIndex = static_cast<std::size_t>(VfxMaskScenario::OccludedSprite);
    const auto ribbonIndex = static_cast<std::size_t>(VfxMaskScenario::WholeRibbon);
    for (const auto& diagnostics : results->BeforeDiagnostics)
    {
        CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
        CHECK(diagnostics.ReadbackValid);
    }
    for (const auto& diagnostics : results->AfterDiagnostics)
    {
        CHECK(diagnostics.State == Keire::GpuOcclusionSurfaceState::Active);
        CHECK(diagnostics.ReadbackValid);
    }
    for (std::size_t index = 0; index < results->BeforeDiagnostics.size(); ++index)
    {
        const auto assertDiagnostics =
            [index, &expectedMaskCounts](const Keire::GpuOcclusionSurfaceDiagnostics& diagnostics,
                                         const WorksetOwnershipSnapshot& ownership)
        {
            CHECK(diagnostics.SourceFrame == ownership.Frame);
            CHECK(diagnostics.SourceFrameSlot == ownership.FrameSlot);
            CHECK(diagnostics.SourceSurfaceEpoch == ownership.SurfaceEpoch);
            CHECK(diagnostics.SourceDeviceGeneration == ownership.DeviceGeneration);
            CHECK(diagnostics.VfxMaskEntries == expectedMaskCounts[index]);
            CHECK(diagnostics.VfxMaskedDraws == 1U);
            CHECK(diagnostics.VfxMaskConsumed);
        };
        assertDiagnostics(results->BeforeDiagnostics[index], results->BeforeOwnership[index]);
        assertDiagnostics(results->AfterDiagnostics[index], results->AfterOwnership[index]);
    }
    const auto assertPixels =
        [visibleIndex, occludedIndex, ribbonIndex](const std::array<std::vector<std::uint8_t>, 3>& pixels)
    {
        const auto visibleRed = RedDominantPixels(pixels[visibleIndex]);
        const auto occludedRed = RedDominantPixels(pixels[occludedIndex]);
        const auto ribbonRed = RedDominantPixels(pixels[ribbonIndex]);
        CHECK(visibleRed > 4U);
        CHECK(occludedRed == 0U);
        CHECK(ribbonRed > 4U);
    };
    assertPixels(results->BeforePixels);
    assertPixels(results->AfterPixels);
}
#endif
