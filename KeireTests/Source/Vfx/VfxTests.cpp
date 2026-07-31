#include "Keire/Application.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId Id(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x5646585445535449ULL, value);
    }

    [[nodiscard]] Keire::VfxEffectDefinition EffectDefinition()
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = Id(1);
        definition.Name = "Sparks";
        definition.Loop = true;
        definition.Duration = 2.0F;
        definition.Space = Keire::VfxSimulationSpace::World;
        definition.Seed = 42;
        definition.Capacity = 16;
        definition.Modules = {
            {Id(2), true, Keire::VfxEmissionRateModule{10.0F}},
            {Id(3), true, Keire::VfxBurstModule{0.0F, 2, 2, 0.25F}},
            {Id(4), true, Keire::VfxShapeModule{}},
            {Id(5), true,
             Keire::VfxInitializeModule{
                 1.0F, 2.0F, {-1.0F, 1.0F, -1.0F}, {1.0F, 2.0F, 1.0F}, {-45.0F, 0.0F, -45.0F}, {45.0F, 360.0F, 45.0F}}},
            {Id(6), true, Keire::VfxForceModule{{0.0F, 1.0F, 0.0F}, 0.5F}},
            {Id(7), true, Keire::VfxSizeOverLifetimeModule{Keire::Curve1D::Linear(1.0F, 0.0F)}},
            {Id(8), true,
             Keire::VfxColorOverLifetimeModule{
                 Keire::ColorGradient({{0.0F, {1.0F, 0.5F, 0.0F, 1.0F}}, {1.0F, {0.2F, 0.0F, 0.0F, 0.0F}}})}},
            {Id(9), true, Keire::VfxCollisionModule{}},
            {Id(10), true, Keire::VfxRendererModule{Keire::VfxRendererType::Sprite, Id(100), {}}},
        };
        return definition;
    }

    [[nodiscard]] std::vector<std::byte> Bytes(const std::string& value)
    {
        std::vector<std::byte> result(value.size());
        std::memcpy(result.data(), value.data(), value.size());
        return result;
    }

    struct HeadlessRenderProbe final
    {
        Keire::RenderStatistics Statistics;
        Keire::RenderCapabilities Capabilities;
        bool Submitted = false;
    };

    class HeadlessVfxRenderLayer final : public Keire::Layer
    {
      public:
        explicit HeadlessVfxRenderLayer(HeadlessRenderProbe& probe) : Layer("Headless VFX Render"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Id(900), Keire::SceneAsset::EmptyDefinition("VFX Render"));
            m_View = Owner().Renderer()->CreateView({.Name = "VFX Headless", .Width = 64, .Height = 64});
            auto definition = EffectDefinition();
            definition.Modules.erase(definition.Modules.begin() + 1);
            auto world = Keire::CreateRef<Keire::VfxWorld>(
                Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 4});
            REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
            world->Update(0.25F);
            m_Snapshot = world->CaptureRenderSnapshot(1);
        }

        void OnUpdate(const Keire::Time&) override
        {
            Keire::SceneRenderRequest request{m_Scene, m_View};
            request.Vfx = m_Snapshot;
            Owner().Renderer()->Submit(std::move(request));
            m_Probe.Statistics = Owner().Renderer()->Statistics();
            m_Probe.Capabilities = Owner().Renderer()->Capabilities();
            m_Probe.Submitted = true;
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            if (m_Scene)
                m_Scene->Close();
        }

      private:
        HeadlessRenderProbe& m_Probe;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::VfxRenderSnapshot m_Snapshot;
    };

    class HeadlessVfxRenderApplication final : public Keire::Application
    {
      public:
        explicit HeadlessVfxRenderApplication(HeadlessRenderProbe& probe) : Application(Specification()), m_Probe(probe)
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<HeadlessVfxRenderLayer>(m_Probe)); }

      private:
        [[nodiscard]] static Keire::ApplicationSpecification Specification()
        {
            Keire::ApplicationSpecification result;
            result.MainWindow.Title = "Headless VFX render preparation";
            result.MainWindow.Visible = false;
            result.Render.Mode = Keire::RenderMode::Headless;
            result.Ui.Mode = Keire::UiMode::Disabled;
            result.ManageLogging = false;
            result.SuspendWhenMainWindowMinimized = false;
            return result;
        }

        HeadlessRenderProbe& m_Probe;
    };
} // namespace

TEST_CASE("VFX schema round trips deterministically and preserves stable module IDs")
{
    auto definition = EffectDefinition();
    const auto encoded = Keire::VfxEffectAsset::Encode(definition);
    const auto decoded = Keire::VfxEffectAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition().SchemaVersion == 2);
    CHECK(decoded->Definition().EmitterId == Id(1));
    CHECK(decoded->Definition().Name == "Sparks");
    REQUIRE(decoded->Definition().Modules.size() == 9);
    CHECK(decoded->Definition().Modules[0].Id == Id(2));
    CHECK(std::get<Keire::VfxEmissionRateModule>(decoded->Definition().Modules[0].Payload).ParticlesPerSecond ==
          doctest::Approx(10.0F));
    CHECK(std::get<Keire::VfxBurstModule>(decoded->Definition().Modules[1].Payload).Cycles == 2);
    CHECK(std::get<Keire::VfxSizeOverLifetimeModule>(decoded->Definition().Modules[5].Payload).Size.Evaluate(0.5F) ==
          doctest::Approx(0.5F));
    CHECK(Keire::VfxEffectAsset::Encode(decoded->Definition()) == encoded);

    definition.Name = "Renamed Sparks";
    const auto renamed = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(definition));
    CHECK(renamed->Definition().EmitterId == Id(1));
    CHECK(renamed->Definition().Modules[0].Id == Id(2));
}

TEST_CASE("VFX schema one definitions publish as schema two without mutating the source definition")
{
    auto legacy = EffectDefinition();
    legacy.SchemaVersion = 1;
    legacy.Systems.clear();
    legacy.Blackboard.clear();

    const auto published = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(legacy));
    CHECK(legacy.SchemaVersion == 1);
    CHECK(legacy.Systems.empty());
    CHECK(published->Definition().SchemaVersion == 2);
    REQUIRE(published->Definition().Systems.size() == 1);
    CHECK(published->Definition().Systems.front().Nodes.size() == 4);
}

TEST_CASE("Default VFX effects expose a connected authoring context graph")
{
    const auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    REQUIRE(definition.Systems.size() == 1);
    const auto& system = definition.Systems.front();
    REQUIRE(system.Nodes.size() == 4);
    REQUIRE(system.Connections.size() == 3);
    CHECK(system.Nodes[0].Pins.size() == 1);
    CHECK(system.Nodes[1].Pins.size() == 2);
    CHECK(system.Nodes[2].Pins.size() == 2);
    CHECK(system.Nodes[3].Pins.size() == 1);

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(cpu.Valid);
    CHECK(gpu.Valid);
    CHECK(cpu.CanonicalIr == gpu.CanonicalIr);
}

TEST_CASE("VFX validation rejects malformed assets and invalid stable topology")
{
    auto definition = EffectDefinition();

    SUBCASE("Duplicate stable ID")
    {
        definition.Modules[1].Id = definition.Modules[0].Id;
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(definition),
                             "VFX effect contains an empty or duplicate stable ID.", std::invalid_argument);
    }

    SUBCASE("Missing enabled renderer")
    {
        definition.Modules.back().Enabled = false;
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(definition),
                             "VFX effect requires enabled emission and renderer modules.", std::invalid_argument);
    }

    SUBCASE("Burst exceeds duration")
    {
        auto& burst = std::get<Keire::VfxBurstModule>(definition.Modules[1].Payload);
        burst.Cycles = 10;
        burst.Interval = 1.0F;
        CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(definition), "VFX burst is invalid.", std::invalid_argument);
    }

    CHECK_THROWS_WITH_AS((void)Keire::VfxEffectAsset::Decode(Bytes("{\"schemaVersion\":1,\"modules\":[")),
                         doctest::Contains("VFX effect asset JSON is malformed:"), std::runtime_error);
    CHECK_THROWS_WITH_AS((void)Keire::VfxEffectAsset::Decode(Bytes("{\"schemaVersion\":99}")),
                         "VFX effect asset has an unsupported schema.", std::runtime_error);
}

TEST_CASE("VFX importer canonicalizes source and extracts sorted dependencies")
{
    auto definition = EffectDefinition();
    auto& shape = std::get<Keire::VfxShapeModule>(definition.Modules[2].Payload);
    shape.Shape = Keire::VfxShape::Volume;
    shape.Volume = Id(99);
    auto& renderer = std::get<Keire::VfxRendererModule>(definition.Modules.back().Payload);
    renderer.Sprite = Id(100);
    const auto dependencies = Keire::VfxEffectDependencies(definition);
    CHECK(dependencies == std::vector<Keire::AssetId>{Id(99), Id(100)});

    const auto source = Keire::VfxEffectAsset::Encode(definition);
    const auto importer = Keire::CreateVfxEffectAssetImporter();
    CHECK(importer.Name == "Keire.VfxEffect");
    CHECK(importer.Type == Keire::VfxEffectAsset::StaticType());
    CHECK(importer.Extensions == std::vector<std::string>{".keirevfx"});
    REQUIRE(importer.Import);
    REQUIRE(importer.ContextualImport);
    CHECK(importer.Import(source) == source);
    const auto output = importer.ContextualImport({}, source);
    CHECK(output.Bytes == source);
    CHECK(output.AssetDependencies == dependencies);

    const auto decoder = Keire::CreateVfxEffectAssetDecoder();
    CHECK(decoder.Type == Keire::VfxEffectAsset::StaticType());
    CHECK(decoder.Fallback->Type() == Keire::VfxEffectAsset::StaticType());
    CHECK(Keire::DynamicRefCast<Keire::VfxEffectAsset>(decoder.Decode(source))->Definition().Name == "Sparks");
}

TEST_CASE("CPU VFX simulation is deterministic and snapshots are bounded value objects")
{
    auto definition = EffectDefinition();
    definition.Modules.erase(definition.Modules.begin() + 1);
    auto firstAsset = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    auto secondAsset = Keire::CreateRef<Keire::VfxEffectAsset>(definition);

    Keire::VfxWorldSpecification specification;
    specification.MaximumEffects = 2;
    specification.MaximumParticles = 32;
    auto first = Keire::CreateRef<Keire::VfxWorld>(specification);
    auto second = Keire::CreateRef<Keire::VfxWorld>(specification);
    const auto firstHandle = first->Activate({firstAsset, 1, {2.0F, 3.0F, 4.0F}, {}, 7});
    const auto secondHandle = second->Activate({secondAsset, 1, {2.0F, 3.0F, 4.0F}, {}, 7});
    REQUIRE(firstHandle);
    REQUIRE(secondHandle);

    first->Update(0.25F);
    second->Update(0.25F);
    const auto firstSnapshot = first->CaptureDebugSnapshot();
    const auto secondSnapshot = second->CaptureDebugSnapshot();
    CHECK(firstSnapshot.EffectCount == 1);
    REQUIRE(firstSnapshot.ParticleCount == 2);
    REQUIRE(secondSnapshot.ParticleCount == firstSnapshot.ParticleCount);
    for (std::size_t index = 0; index < firstSnapshot.ParticleCount; ++index)
    {
        CHECK(firstSnapshot.Particles[index].Position == secondSnapshot.Particles[index].Position);
        CHECK(firstSnapshot.Particles[index].Velocity == secondSnapshot.Particles[index].Velocity);
        CHECK(firstSnapshot.Particles[index].Rotation == secondSnapshot.Particles[index].Rotation);
        CHECK(firstSnapshot.Particles[index].Tint == secondSnapshot.Particles[index].Tint);
        CHECK(firstSnapshot.Particles[index].Size == secondSnapshot.Particles[index].Size);
    }
    std::array<Keire::VfxRenderParticle, 1> packets;
    const auto copied = first->CopyRenderPackets(packets);
    CHECK(copied.Written == 1);
    CHECK(copied.Dropped == 1);
    CHECK(packets[0].Sprite == Id(100));
    const auto renderSnapshot = first->CaptureRenderSnapshot(1);
    CHECK(renderSnapshot.Revision() == firstSnapshot.Revision);
    REQUIRE(renderSnapshot.Particles().size() == 1);
    CHECK(renderSnapshot.Particles().front() == packets.front());
    CHECK(renderSnapshot.DroppedParticles() == 1);
    CHECK_THROWS_AS((void)first->CaptureRenderSnapshot(Keire::VfxRenderSnapshot::MaximumParticles + 1),
                    std::invalid_argument);
}

TEST_CASE("VFX transform updates preserve World particles and move Local particles")
{
    const auto captureAfterMove = [](const Keire::VfxSimulationSpace space)
    {
        auto definition = EffectDefinition();
        definition.Space = space;
        definition.Modules.erase(definition.Modules.begin());
        auto& burst = std::get<Keire::VfxBurstModule>(definition.Modules.front().Payload);
        burst.Count = 1;
        burst.Cycles = 1;
        auto& initialize = std::get<Keire::VfxInitializeModule>(definition.Modules[2].Payload);
        initialize.LifetimeMinimum = 5.0F;
        initialize.LifetimeMaximum = 5.0F;
        initialize.VelocityMinimum = {};
        initialize.VelocityMaximum = {};
        auto& force = std::get<Keire::VfxForceModule>(definition.Modules[3].Payload);
        force.Force = {};
        force.GravityMultiplier = 0.0F;

        auto world =
            Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 4});
        const auto handle =
            world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition), 1, {1.0F, 2.0F, 3.0F}});
        REQUIRE(handle);
        world->Update(0.01F);
        REQUIRE(world->CaptureRenderSnapshot().Particles().size() == 1);
        world->SetTransform(handle, {11.0F, 2.0F, 3.0F}, {});
        return world->CaptureRenderSnapshot().Particles().front().Position;
    };

    CHECK(captureAfterMove(Keire::VfxSimulationSpace::World) == (Keire::Vector3{1.0F, 2.0F, 3.0F}));
    CHECK(captureAfterMove(Keire::VfxSimulationSpace::Local) == (Keire::Vector3{11.0F, 2.0F, 3.0F}));
}

TEST_CASE("GPU VFX simulation publishes compact emitter work without allocating CPU particles")
{
    auto definition = EffectDefinition();
    definition.Modules.erase(definition.Modules.begin() + 1);

    Keire::VfxWorldSpecification specification;
    specification.Backend = Keire::VfxBackend::Gpu;
    specification.MaximumEffects = 2;
    specification.MaximumParticles = 64;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition), 1, {1.0F, 2.0F, 3.0F}}));

    world->Update(0.25F);
    const auto debug = world->CaptureDebugSnapshot();
    CHECK(debug.EffectCount == 1);
    CHECK(debug.ParticleCount == 0);

    const auto render = world->CaptureRenderSnapshot();
    CHECK(render.Particles().empty());
    REQUIRE(render.GpuEmitters().size() == 1);
    CHECK(render.GpuEmitters().front().Position == Keire::Vector3{1.0F, 2.0F, 3.0F});
    CHECK(render.GpuEmitters().front().Renderer == Keire::VfxRendererType::Sprite);
}

TEST_CASE("Stopping one GPU VFX handle preserves unrelated world state")
{
    auto definition = EffectDefinition();
    Keire::VfxWorldSpecification specification;
    specification.Backend = Keire::VfxBackend::Gpu;
    specification.MaximumEffects = 2;
    specification.MaximumParticles = 64;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    const auto first = world->Activate({effect, 1, {1.0F, 0.0F, 0.0F}});
    const auto second = world->Activate({effect, 1, {2.0F, 0.0F, 0.0F}});
    REQUIRE(first);
    REQUIRE(second);
    world->Update(0.1F);

    const auto resetRevision = world->CaptureRenderSnapshot().ResetRevision();
    world->Stop(first);
    const auto afterStop = world->CaptureRenderSnapshot();
    CHECK(afterStop.ResetRevision() == resetRevision);
    REQUIRE(afterStop.GpuEmitters().size() == 1);
    CHECK(afterStop.GpuEmitters().front().Handle == second);

    const auto replacement = world->Activate({effect, 1, {3.0F, 0.0F, 0.0F}});
    REQUIRE(replacement);
    CHECK(replacement.Index() == first.Index());
    CHECK(replacement.Generation() != first.Generation());
    const auto afterReuse = world->CaptureRenderSnapshot();
    REQUIRE(afterReuse.GpuEmitters().size() == 2);
    CHECK(afterReuse.GpuEmitters()[0].Handle == replacement);
    CHECK(afterReuse.GpuEmitters()[1].Handle == second);
    CHECK(afterReuse.ResetRevision() == resetRevision);

    world->Clear();
    CHECK(world->CaptureRenderSnapshot().ResetRevision() > resetRevision);
}

TEST_CASE("Incompatible GPU VFX reloads reset only the reloaded emitter")
{
    auto definition = EffectDefinition();
    Keire::VfxWorldSpecification specification;
    specification.Backend = Keire::VfxBackend::Gpu;
    specification.MaximumEffects = 2;
    specification.MaximumParticles = 64;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    const auto first = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition), 1});
    const auto second = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition), 1});
    REQUIRE(first);
    REQUIRE(second);
    world->Update(0.1F);

    const auto before = world->CaptureRenderSnapshot();
    REQUIRE(before.GpuEmitters().size() == 2);
    const auto resetRevision = before.ResetRevision();
    const auto simulationRevision = before.GpuEmitters()[0].SimulationRevision;
    const auto survivorSimulationRevision = before.GpuEmitters()[1].SimulationRevision;

    auto compatible = definition;
    compatible.Name = "Compatible reload";
    REQUIRE(world->Reload(first, Keire::CreateRef<Keire::VfxEffectAsset>(compatible), 2));
    auto afterReload = world->CaptureRenderSnapshot();
    REQUIRE(afterReload.GpuEmitters().size() == 2);
    CHECK(afterReload.GpuEmitters()[0].SimulationRevision == simulationRevision);
    CHECK(afterReload.ResetRevision() == resetRevision);

    auto incompatible = compatible;
    incompatible.EmitterId = Id(900);
    REQUIRE(world->Reload(first, Keire::CreateRef<Keire::VfxEffectAsset>(incompatible), 3));
    afterReload = world->CaptureRenderSnapshot();
    REQUIRE(afterReload.GpuEmitters().size() == 2);
    CHECK(afterReload.GpuEmitters()[0].SimulationRevision != simulationRevision);
    CHECK(afterReload.GpuEmitters()[0].SpawnSequence == 0);
    CHECK(afterReload.GpuEmitters()[1].SimulationRevision == survivorSimulationRevision);
    CHECK(afterReload.ResetRevision() == resetRevision);
}

TEST_CASE("Non-looping GPU VFX effects remain alive while particles drain and then release")
{
    auto definition = EffectDefinition();
    definition.Loop = false;
    definition.Duration = 0.1F;
    definition.Modules.erase(definition.Modules.begin() + 1);
    auto& initialize = std::get<Keire::VfxInitializeModule>(definition.Modules[2].Payload);
    initialize.LifetimeMinimum = 0.25F;
    initialize.LifetimeMaximum = 0.25F;

    Keire::VfxWorldSpecification specification;
    specification.Backend = Keire::VfxBackend::Gpu;
    specification.MaximumEffects = 1;
    specification.MaximumParticles = 64;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);

    world->Update(0.1F);
    CHECK(world->IsAlive(handle));
    world->Update(0.2F);
    CHECK(world->IsAlive(handle));
    world->Update(0.1F);
    CHECK_FALSE(world->IsAlive(handle));
}

TEST_CASE("Headless rendering consumes immutable VFX packets without advertising GPU support")
{
#if defined(_WIN32)
    REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
    REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    HeadlessRenderProbe probe;
    HeadlessVfxRenderApplication application(probe);
    CHECK(application.Run() == 0);
    CHECK(probe.Submitted);
    CHECK(probe.Statistics.VfxSpriteParticles == 1);
    CHECK(probe.Statistics.VfxMeshParticles == 0);
    CHECK(probe.Statistics.DroppedVfxParticles == 1);
    CHECK(probe.Capabilities.CpuVfxSimulation);
    CHECK_FALSE(probe.Capabilities.GpuVfxSimulation);
    CHECK_FALSE(probe.Capabilities.TransparentPass);
    CHECK_FALSE(probe.Capabilities.SampledResolvedDepth);
    CHECK_FALSE(probe.Capabilities.GpuDepthCollision);
}

TEST_CASE("VFX quotas pool particles and reject stale generation handles")
{
    auto definition = EffectDefinition();
    definition.Capacity = 2;
    definition.Modules.erase(definition.Modules.begin() + 1);
    std::get<Keire::VfxEmissionRateModule>(definition.Modules.front().Payload).ParticlesPerSecond = 100.0F;
    auto asset = Keire::CreateRef<Keire::VfxEffectAsset>(definition);

    Keire::VfxWorldSpecification specification;
    specification.MaximumEffects = 1;
    specification.MaximumParticles = 2;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    CHECK_THROWS_WITH_AS((void)world->Activate({}), "VFX activation is invalid.", std::invalid_argument);
    CHECK(world->Statistics().ActiveEffects == 0);
    CHECK(world->Statistics().DroppedEffects == 0);
    const auto first = world->Activate({asset});
    REQUIRE(first);
    CHECK_FALSE(world->Activate({asset}));
    CHECK(world->Statistics().DroppedEffects == 1);

    world->Update(0.1F);
    CHECK(world->Statistics().ActiveParticles == 2);
    CHECK(world->Statistics().DroppedParticles == 8);
    const auto debug = world->CaptureDebugSnapshot();
    CHECK(debug.Effects[0].DroppedParticles == 8);

    world->Stop(first);
    CHECK_FALSE(world->IsAlive(first));
    const auto replacement = world->Activate({asset});
    REQUIRE(replacement);
    CHECK(replacement.Index() == first.Index());
    CHECK(replacement.Generation() != first.Generation());
    CHECK_THROWS_WITH_AS(world->SetTransform(first, {}, {}), "Cannot transform a stale VFX handle.",
                         std::invalid_argument);
}

TEST_CASE("VFX debug snapshots truncate deterministically at their fixed bound")
{
    auto definition = EffectDefinition();
    auto asset = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 257, .MaximumParticles = 1});
    for (std::size_t index = 0; index < 257; ++index)
        REQUIRE(world->Activate({asset, 1, {}, {}, static_cast<std::uint32_t>(index)}));

    const auto snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.EffectCount == Keire::VfxDebugSnapshot::MaximumEffects);
    CHECK(snapshot.DroppedEffectSamples == 1);
    CHECK(snapshot.Statistics.ActiveEffects == 257);
}

TEST_CASE("VFX revision reload preserves compatible state and safely restarts incompatible topology")
{
    auto definition = EffectDefinition();
    definition.Modules.erase(definition.Modules.begin() + 1);
    definition.Capacity = 4;
    auto world =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 8});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    world->Update(0.25F);
    REQUIRE(world->Statistics().ActiveParticles == 2);

    auto compatible = definition;
    compatible.Name = "Reloaded";
    compatible.Capacity = 1;
    CHECK(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(compatible), 2));
    CHECK(world->Statistics().ActiveParticles == 1);
    CHECK_FALSE(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(compatible), 2));
    auto snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.Effects[0].Revision == 2);
    CHECK(snapshot.Effects[0].ElapsedSeconds == doctest::Approx(0.25F));

    auto incompatible = compatible;
    incompatible.EmitterId = Id(500);
    CHECK(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(incompatible), 3));
    CHECK(world->Statistics().ActiveParticles == 0);
    snapshot = world->CaptureDebugSnapshot();
    CHECK(snapshot.Effects[0].EmitterId == Id(500));
    CHECK(snapshot.Effects[0].ElapsedSeconds == doctest::Approx(0.0F));
    CHECK(snapshot.Effects[0].Emitting);
}

TEST_CASE("CPU VFX reports collision fallback and consumes a provided collision query")
{
    auto definition = EffectDefinition();
    definition.Loop = false;
    definition.Modules.erase(definition.Modules.begin());
    auto& burst = std::get<Keire::VfxBurstModule>(definition.Modules.front().Payload);
    burst.Count = 1;
    burst.Cycles = 1;
    auto& initialize = std::get<Keire::VfxInitializeModule>(definition.Modules[2].Payload);
    initialize.VelocityMinimum = {0.0F, -1.0F, 0.0F};
    initialize.VelocityMaximum = initialize.VelocityMinimum;
    auto& collision = std::get<Keire::VfxCollisionModule>(definition.Modules[6].Payload);
    collision.Mode = Keire::VfxCollisionMode::GpuDepth;
    collision.KillOnCollision = true;
    auto asset = Keire::CreateRef<Keire::VfxEffectAsset>(definition);

    auto fallbackWorld =
        Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 4});
    const auto fallback = fallbackWorld->Activate({asset});
    REQUIRE(fallback);
    auto snapshot = fallbackWorld->CaptureDebugSnapshot();
    CHECK(Keire::HasVfxDiagnostic(snapshot.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::GpuDepthFellBackToCpu));
    CHECK(Keire::HasVfxDiagnostic(snapshot.Effects[0].Diagnostics,
                                  Keire::VfxRuntimeDiagnostic::CollisionQueryUnavailable));

    auto scenePhysicsDefinition = definition;
    std::get<Keire::VfxCollisionModule>(scenePhysicsDefinition.Modules[6].Payload).Mode =
        Keire::VfxCollisionMode::ScenePhysics;
    CHECK(fallbackWorld->Reload(fallback, Keire::CreateRef<Keire::VfxEffectAsset>(scenePhysicsDefinition), 2));
    snapshot = fallbackWorld->CaptureDebugSnapshot();
    CHECK(
        Keire::HasVfxDiagnostic(snapshot.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ScenePhysicsSelectedCpu));

    Keire::VfxWorldSpecification queriedSpecification;
    queriedSpecification.MaximumEffects = 1;
    queriedSpecification.MaximumParticles = 4;
    queriedSpecification.CollisionQuery = [](const Keire::Vector3,
                                             const Keire::Vector3 end) -> std::optional<Keire::VfxCollisionHit>
    {
        if (end.Y < 0.0F)
            return Keire::VfxCollisionHit{{end.X, 0.0F, end.Z}, {0.0F, 1.0F, 0.0F}};
        return std::nullopt;
    };
    auto queriedWorld = Keire::CreateRef<Keire::VfxWorld>(std::move(queriedSpecification));
    const auto queried = queriedWorld->Activate({asset});
    REQUIRE(queried);
    queriedWorld->Update(0.1F);
    CHECK(queriedWorld->Statistics().ActiveParticles == 1);
    queriedWorld->Update(0.1F);
    CHECK(queriedWorld->Statistics().ActiveParticles == 0);
}

TEST_CASE("VFX Emitter component schema exposes typed effect authoring")
{
    const auto registration = Keire::CreateVfxEmitterComponentRegistration();
    CHECK(registration.Type == Keire::VfxEmitterComponent::StaticType());
    CHECK(registration.SchemaVersion == 1);
    CHECK(registration.RequiredComponents ==
          std::vector<Keire::ComponentTypeId>{Keire::TransformComponent::StaticType()});
    REQUIRE(registration.Properties.size() == 10);
    CHECK(registration.Properties.front().ExpectedAssetType == Keire::VfxEffectAsset::StaticType());

    const auto component = registration.Factory();
    registration.Deserialize(*component,
                             {{"effect", Id(100)},
                              {"playOnAwake", false},
                              {"autoDestroy", true},
                              {"simulationSpeed", 2.0},
                              {"seedOffset", std::int64_t{99}}},
                             1);
    const auto& emitter = dynamic_cast<const Keire::VfxEmitterComponent&>(*component);
    CHECK(emitter.Effect() == Id(100));
    CHECK_FALSE(emitter.PlayOnAwake());
    CHECK(emitter.AutoDestroy());
    CHECK(emitter.SimulationSpeed() == doctest::Approx(2.0F));
    CHECK(emitter.SeedOffset() == 99);
    CHECK(std::get<Keire::AssetId>(registration.Serialize(*component).at("effect")) == Id(100));
    CHECK_THROWS_WITH_AS(registration.Deserialize(*component, {}, 2),
                         "Unsupported VFX Emitter component schema version.", std::invalid_argument);
}
