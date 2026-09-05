#pragma once

#include "Keire/ECS/Components/LightProbeVolumeComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/Scenes/Scene.h"

#include <cstddef>

namespace KeireEditor
{
    struct LightingBakeTargetSummary final
    {
        std::size_t LightmapReceivers = 0;
        std::size_t ReflectionProbes = 0;
        std::size_t LightProbeVolumes = 0;
        std::size_t StaticRenderersUsingProbes = 0;
        std::size_t NonStaticRenderersUsingLightmaps = 0;

        [[nodiscard]] std::size_t TotalTargets() const noexcept
        {
            return LightmapReceivers + ReflectionProbes + LightProbeVolumes;
        }
    };

    [[nodiscard]] inline LightingBakeTargetSummary SummarizeLightingBakeTargets(const Keire::Scene& scene)
    {
        LightingBakeTargetSummary result;
        for (const auto& entity : scene.Query<Keire::MeshRendererComponent>())
        {
            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            if (!renderer || !renderer->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (renderer->StaticLighting() && renderer->GIReceive() == Keire::GIReceiveMode::Lightmaps)
                ++result.LightmapReceivers;
            else if (renderer->StaticLighting() && renderer->GIReceive() == Keire::GIReceiveMode::LightProbes)
                ++result.StaticRenderersUsingProbes;
            else if (!renderer->StaticLighting() && renderer->GIReceive() == Keire::GIReceiveMode::Lightmaps)
                ++result.NonStaticRenderersUsingLightmaps;
        }
        for (const auto& entity : scene.Query<Keire::ReflectionProbeComponent>())
        {
            const auto probe = entity.GetComponent<Keire::ReflectionProbeComponent>();
            if (probe && probe->Enabled() && entity.ActiveInHierarchy())
                ++result.ReflectionProbes;
        }
        for (const auto& entity : scene.Query<Keire::LightProbeVolumeComponent>())
        {
            const auto volume = entity.GetComponent<Keire::LightProbeVolumeComponent>();
            if (volume && volume->Enabled() && entity.ActiveInHierarchy())
                ++result.LightProbeVolumes;
        }
        return result;
    }
} // namespace KeireEditor
