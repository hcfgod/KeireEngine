#include "KeireRuntimeInternal/RuntimeSceneRendering.h"

#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"

#include <utility>

namespace KeireRuntime
{
    void SubmitRuntimeWorldRendering(
        const Keire::Ref<Keire::RenderSystem>& renderer, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
        const Keire::Ref<Keire::RenderView>& view, const Keire::RenderEnvironmentSettings& environment,
        const std::map<std::string, Keire::MaterialPropertyValue, std::less<>>& globalMaterialProperties,
        const Keire::Ref<Keire::SceneRuntimeSession>& primary, const bool drawSceneContributions)
    {
        if (!renderer || !world || !view)
            return;

        for (const auto& session : world->Sessions())
        {
            if (const auto presentation =
                    session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{})
            {
                for (auto submission : presentation->UiRenderSubmissions(view))
                    renderer->SubmitRuntimeUiTarget(std::move(submission));
            }
        }
        const auto fallback = Keire::Internal::SelectRuntimeRenderSession(world);
        auto renderRequest = Keire::Internal::CaptureRuntimeSceneRenderRequest(
            world, primary ? primary : fallback.Session, view, environment, globalMaterialProperties,
            drawSceneContributions);
        if (renderRequest)
            renderer->Submit(std::move(*renderRequest));
    }
} // namespace KeireRuntime
