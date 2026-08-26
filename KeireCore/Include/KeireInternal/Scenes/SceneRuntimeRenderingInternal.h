#pragma once

#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace Keire::Internal
{
    struct RuntimeSceneCamera final
    {
        Entity Entity;
        Ref<CameraComponent> Camera;
        Ref<TransformComponent> Transform;
    };

    struct RuntimeRenderSessionSelection final
    {
        Ref<SceneRuntimeSession> Session;
        std::optional<RuntimeSceneCamera> Camera;
        std::size_t ContributionIndex = std::numeric_limits<std::size_t>::max();

        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(Session); }
    };

    [[nodiscard]] inline std::optional<RuntimeSceneCamera> SelectRuntimeSceneCamera(const Ref<Scene>& scene)
    {
        if (!scene || !scene->IsOpen())
            return std::nullopt;
        std::optional<RuntimeSceneCamera> selected;
        bool selectedPrimary = false;
        for (const auto& entity : scene->Query<CameraComponent>())
        {
            const auto camera = entity.GetComponent<CameraComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (!selected || (camera->Primary() && !selectedPrimary) ||
                (camera->Primary() == selectedPrimary && (camera->Priority() > selected->Camera->Priority() ||
                                                          (camera->Priority() == selected->Camera->Priority() &&
                                                           entity.Id().Value() < selected->Entity.Id().Value()))))
            {
                selected = RuntimeSceneCamera{entity, camera, transform};
                selectedPrimary = camera->Primary();
            }
        }
        return selected;
    }

    [[nodiscard]] inline RuntimeRenderSessionSelection SelectRuntimeRenderSession(const Ref<SceneRuntimeWorld>& world)
    {
        if (!world)
            return {};
        const auto sessions = world->Sessions();
        const auto active = world->Session(world->Active());
        std::optional<std::size_t> activeIndex;
        for (std::size_t index = 0; index < sessions.size(); ++index)
        {
            if (sessions[index] == active)
            {
                activeIndex = index;
                break;
            }
        }
        if (activeIndex && active && active->RuntimeScene())
        {
            if (auto camera = SelectRuntimeSceneCamera(active->RuntimeScene()))
                return {active, std::move(camera), *activeIndex};
        }
        for (std::size_t index = 0; index < sessions.size(); ++index)
        {
            const auto& session = sessions[index];
            if (!session || !session->RuntimeScene())
                continue;
            if (auto camera = SelectRuntimeSceneCamera(session->RuntimeScene()))
                return {session, std::move(camera), index};
        }
        if (activeIndex && active && active->RuntimeScene())
            return {active, std::nullopt, *activeIndex};
        for (std::size_t index = 0; index < sessions.size(); ++index)
            if (sessions[index] && sessions[index]->RuntimeScene())
                return {sessions[index], std::nullopt, index};
        return {};
    }

    [[nodiscard]] inline Ref<ScenePresentationRuntime>
    ActiveRuntimePresentation(const Ref<SceneRuntimeWorld>& world) noexcept
    {
        const auto active = world ? world->Session(world->Active()) : Ref<SceneRuntimeSession>{};
        return active ? active->Presentation() : Ref<ScenePresentationRuntime>{};
    }

    [[nodiscard]] inline std::optional<SceneRenderRequest>
    CaptureRuntimeSceneRenderRequest(const Ref<SceneRuntimeWorld>& world, const Ref<SceneRuntimeSession>& primary,
                                     const Ref<RenderView>& view, const RenderEnvironmentSettings& environment,
                                     std::map<std::string, MaterialPropertyValue, std::less<>> globalMaterialProperties,
                                     const bool drawSceneContributions)
    {
        if (!world || !view)
            return std::nullopt;
        std::optional<SceneRenderRequest> request;
        std::size_t contributionIndex = 0;
        for (const auto& session : world->Sessions())
        {
            if (!session || !session->RuntimeScene())
                continue;
            VfxRenderSnapshot vfx;
            if (drawSceneContributions)
                if (const auto worldVfx = session->Vfx())
                    vfx = worldVfx->CaptureRenderSnapshot();
            if (!request)
            {
                request.emplace(SceneRenderRequest{session->RuntimeScene(), view, false, environment,
                                                   std::move(globalMaterialProperties), std::move(vfx)});
            }
            else
            {
                request->AdditionalScenes.push_back({session->RuntimeScene(), std::move(vfx)});
            }
            if (session == primary)
                request->PrimaryContributionIndex = contributionIndex;
            ++contributionIndex;
        }
        if (request)
            request->DrawSceneContributions = drawSceneContributions;
        return request;
    }
} // namespace Keire::Internal
