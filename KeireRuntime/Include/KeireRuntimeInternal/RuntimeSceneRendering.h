#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Ref.h"

#include <functional>
#include <map>
#include <string>

namespace Keire
{
    class RenderSystem;
    class RenderView;
    class SceneRuntimeWorld;
    class SceneRuntimeSession;
    struct RenderEnvironmentSettings;
} // namespace Keire

namespace KeireRuntime
{
    void SubmitRuntimeWorldRendering(
        const Keire::Ref<Keire::RenderSystem>& renderer, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
        const Keire::Ref<Keire::RenderView>& view, const Keire::RenderEnvironmentSettings& environment,
        const std::map<std::string, Keire::MaterialPropertyValue, std::less<>>& globalMaterialProperties,
        const Keire::Ref<Keire::SceneRuntimeSession>& primary = {}, bool drawSceneContributions = true);
} // namespace KeireRuntime
