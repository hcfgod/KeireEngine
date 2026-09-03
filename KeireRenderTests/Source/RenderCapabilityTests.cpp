#include "Keire/Core.h"
#include "KeireRenderTests/RenderedOutputTestSupport.h"

#include <doctest/doctest.h>

#include <memory>

namespace
{
    struct RenderCapabilityProbe final
    {
        Keire::RenderCapabilities Capabilities;
        bool Observed = false;
    };

    class RenderCapabilityLayer final : public Keire::Layer
    {
      public:
        explicit RenderCapabilityLayer(RenderCapabilityProbe& probe) : Layer("Render capabilities"), m_Probe(probe) {}

      protected:
        void OnUpdate(const Keire::Time&) override
        {
            m_Probe.Capabilities = Owner().Renderer()->Capabilities();
            m_Probe.Observed = true;
            Owner().RequestExit();
        }

      private:
        RenderCapabilityProbe& m_Probe;
    };

} // namespace

TEST_CASE("Rendered mode advertises GPU depth collision when sampled depth is available")
{
    RenderCapabilityProbe probe;
    Keire::Application application(RenderTestSpecification());
    (void)application.PushLayer(std::make_unique<RenderCapabilityLayer>(probe));
    REQUIRE(application.Run() == 0);
    REQUIRE(probe.Observed);
    CHECK(probe.Capabilities.DeferredHybrid);
    CHECK(probe.Capabilities.BakedGlobalIllumination);
    CHECK(probe.Capabilities.RealtimeGlobalIllumination);
    CHECK(probe.Capabilities.IrradynGlobalIllumination);
    CHECK(probe.Capabilities.Fxaa);
    CHECK(probe.Capabilities.TemporalAntiAliasing);
    CHECK(probe.Capabilities.DynamicResolution);
    CHECK(probe.Capabilities.GpuVfxSimulation);
    CHECK(probe.Capabilities.SampledResolvedDepth);
    CHECK(probe.Capabilities.GpuDepthCollision);
    CHECK(probe.Capabilities.GpuOcclusionVfxVisibilityMasks);
    CHECK(probe.Capabilities.GpuOcclusionLocalLightMasks);
    CHECK(probe.Capabilities.GpuOcclusionSpatialVolumeMasks);
}
