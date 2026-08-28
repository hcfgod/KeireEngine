#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <memory>

namespace Keire::RenderBackend
{
    struct RenderSharedState;
    struct RenderSurfaceState;
} // namespace Keire::RenderBackend

namespace Keire
{
    class RenderSurface::Impl final
    {
      public:
        explicit Impl(std::shared_ptr<RenderBackend::RenderSurfaceState> state);
        ~Impl();

        std::shared_ptr<RenderBackend::RenderSurfaceState> State;
    };

    class RenderView::Impl final
    {
      public:
        explicit Impl(Ref<RenderSurface> surface);
        ~Impl();

        Ref<RenderSurface> Surface;
        RenderCamera Camera;
    };

    class RenderSystem::Impl final
    {
      public:
        Impl(RenderSpecification specification, const Ref<WindowSystem>& windows, const Ref<Window>& window,
             const Ref<AssetSystem>& assets, const Ref<JobSystem>& jobs, const Ref<StreamingSystem>& streaming);
        ~Impl();

        std::shared_ptr<RenderBackend::RenderSharedState> State;
    };
} // namespace Keire
