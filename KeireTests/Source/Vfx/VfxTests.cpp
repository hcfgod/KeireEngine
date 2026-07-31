#include "Keire/Application.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
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
        definition.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
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

    void BindGraphDefault(Keire::VfxEffectDefinition& definition, const std::uint64_t idBase,
                          const Keire::AssetId moduleId, const std::string& semantic, const Keire::VfxValueType type,
                          Keire::VfxParameterValue value)
    {
        auto& system = definition.Systems.front();
        const auto module =
            std::ranges::find(system.Nodes, moduleId, [](const Keire::VfxGraphNode& node) { return node.Reference; });
        if (module == system.Nodes.end())
            throw std::logic_error("Test graph module node was not found.");
        const auto input = std::ranges::find(module->Pins, semantic, &Keire::VfxGraphPin::Semantic);
        if (input == module->Pins.end())
            throw std::logic_error("Test graph module input was not found.");
        const auto moduleNodeId = module->Id;
        const auto moduleInputId = input->Id;

        definition.Blackboard.push_back(
            {Id(idBase), "Bound default " + std::to_string(idBase), type, std::move(value), true});
        system.Nodes.push_back({Id(idBase + 1),
                                "Bound default",
                                module->Context,
                                {},
                                {{Id(idBase + 2), "Value", type, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                Id(idBase)});
        system.Connections.push_back({Id(idBase + 3), Id(idBase + 1), Id(idBase + 2), moduleNodeId, moduleInputId});
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
    CHECK(decoded->Definition().SchemaVersion == 3);
    CHECK(decoded->Definition().ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
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

TEST_CASE("VFX schema one definitions publish as schema three legacy programs without mutating the source")
{
    auto legacy = EffectDefinition();
    legacy.SchemaVersion = 1;
    legacy.Systems.clear();
    legacy.Blackboard.clear();

    const auto published = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(legacy));
    CHECK(legacy.SchemaVersion == 1);
    CHECK(legacy.Systems.empty());
    CHECK(published->Definition().SchemaVersion == 3);
    CHECK(published->Definition().ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
    CHECK(published->Definition().Systems.empty());
}

TEST_CASE("Default VFX effects expose a connected authoring context graph")
{
    const auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    CHECK(definition.SchemaVersion == 3);
    CHECK(definition.ExecutionSource == Keire::VfxExecutionSource::Graph);
    REQUIRE(definition.Systems.size() == 1);
    const auto& system = definition.Systems.front();
    CHECK(system.Nodes.size() == definition.Modules.size() + 4);
    CHECK(system.Connections.size() == definition.Modules.size() + 3);
    CHECK(std::ranges::count(system.Nodes, Keire::VfxGraphNodeKind::Context, &Keire::VfxGraphNode::Kind) == 4);
    CHECK(std::ranges::count(system.Nodes, Keire::VfxGraphNodeKind::Module, &Keire::VfxGraphNode::Kind) ==
          definition.Modules.size());

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(cpu.Valid);
    CHECK(gpu.Valid);
    CHECK(cpu.Modules.size() == definition.Modules.size());
    CHECK(cpu.CanonicalIr == gpu.CanonicalIr);
}

TEST_CASE("VFX legacy conversion produces deterministic executable schema-three topology")
{
    const auto first = Keire::ConvertVfxEffectToGraph(EffectDefinition());
    const auto second = Keire::ConvertVfxEffectToGraph(EffectDefinition());
    CHECK(first.ExecutionSource == Keire::VfxExecutionSource::Graph);
    CHECK(first.Systems == second.Systems);

    const auto roundTrip = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(first));
    REQUIRE(roundTrip);
    CHECK(roundTrip->Definition() == first);

    const auto compiled = Keire::CompileVfxEffect(first, Keire::VfxBackend::Cpu);
    REQUIRE(compiled.Valid);
    CHECK(compiled.Modules.size() == first.Modules.size());

    auto presentationOnly = first;
    presentationOnly.Systems.front().Name = "Renamed presentation";
    std::ranges::reverse(presentationOnly.Systems.front().Nodes);
    std::ranges::reverse(presentationOnly.Systems.front().Connections);
    for (auto& node : presentationOnly.Systems.front().Nodes)
    {
        node.Type += " renamed";
        node.EditorPosition.X += 1000.0F;
        node.EditorPosition.Y -= 250.0F;
    }
    const auto presentationCompile = Keire::CompileVfxEffect(presentationOnly, Keire::VfxBackend::Cpu);
    REQUIRE(presentationCompile.Valid);
    CHECK(presentationCompile.Hash == compiled.Hash);
    CHECK(presentationCompile.StateLayoutHash == compiled.StateLayoutHash);
}

TEST_CASE("VFX graph compiler lowers Blackboard bindings and bounded Portable Custom HLSL")
{
    auto definition = Keire::ConvertVfxEffectToGraph(EffectDefinition());
    definition.Blackboard.push_back({Id(200), "Gravity", Keire::VfxValueType::Scalar, 2.0F, true});
    auto& system = definition.Systems.front();
    system.Nodes.push_back({Id(201),
                            "Gravity",
                            Keire::VfxContextType::Update,
                            {-300.0F, 0.0F},
                            {{Id(202), "Gravity", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
                            {},
                            Keire::VfxGraphNodeKind::Parameter,
                            Id(200)});

    const auto forceNode =
        std::ranges::find(system.Nodes, Id(6), [](const Keire::VfxGraphNode& node) { return node.Reference; });
    REQUIRE(forceNode != system.Nodes.end());
    const auto gravityPin =
        std::ranges::find(forceNode->Pins, std::string("gravityMultiplier"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(gravityPin != forceNode->Pins.end());
    const auto forceNodeId = forceNode->Id;
    const auto gravityPinId = gravityPin->Id;
    system.Connections.push_back({Id(203), Id(201), Id(202), forceNodeId, gravityPinId});

    const auto updateContext = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(updateContext != system.Nodes.end());
    const auto updateCable =
        std::ranges::find(system.Connections, updateContext->Id, &Keire::VfxGraphConnection::OutputNode);
    REQUIRE(updateCable != system.Connections.end());
    const auto previousInputNode = updateCable->InputNode;
    const auto previousInputPin = updateCable->InputPin;
    system.Nodes.push_back(
        {Id(204),
         "Portable Custom HLSL",
         Keire::VfxContextType::Update,
         {800.0F, 200.0F},
         {{Id(205), "Particles", Keire::VfxValueType::ParticleStream, true, "particles", std::nullopt},
          {Id(206), "Particles", Keire::VfxValueType::ParticleStream, false, "particles", std::nullopt}},
         "Velocity += float3(0.0, 1.0, 0.0) * DeltaTime;\nSize *= 0.5;",
         Keire::VfxGraphNodeKind::CustomHlsl,
         {}});
    updateCable->InputNode = Id(204);
    updateCable->InputPin = Id(205);
    system.Connections.push_back({Id(207), Id(204), Id(206), previousInputNode, previousInputPin});

    const auto compiled = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(compiled.Valid);
    REQUIRE(compiled.Parameters.size() == 1);
    CHECK(compiled.Parameters.front().Parameter == Id(200));
    REQUIRE(compiled.Bindings.size() == 1);
    CHECK(compiled.Bindings.front().Property == Keire::VfxModuleProperty::ForceGravityMultiplier);
    REQUIRE(compiled.CustomInstructions.size() == 2);
    CHECK(compiled.CustomInstructions[0].Target == Keire::VfxCustomTarget::Velocity);
    CHECK(compiled.CustomInstructions[0].ScaleByDeltaTime);
    CHECK(compiled.CustomInstructions[1].Target == Keire::VfxCustomTarget::Size);

    auto duplicateDriver = definition;
    duplicateDriver.Systems.front().Connections.push_back({Id(208), Id(201), Id(202), forceNodeId, gravityPinId});
    CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(duplicateDriver), "VFX graph input pins may have at most one cable.",
                         std::invalid_argument);

    auto invalidHlsl = definition;
    const auto custom = std::ranges::find(invalidHlsl.Systems.front().Nodes, Id(204), &Keire::VfxGraphNode::Id);
    REQUIRE(custom != invalidHlsl.Systems.front().Nodes.end());
    custom->CustomHlsl = "result = 1.0;";
    CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(invalidHlsl),
                         "Portable Custom HLSL targets Position, Velocity, Rotation, Tint, or Size.",
                         std::invalid_argument);
}

TEST_CASE("VFX graph compilation rejects defaults that invalidate resolved executable modules")
{
    const auto checkCompileError = [](const Keire::VfxEffectDefinition& definition, const std::string& message)
    {
        const auto compiled = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        CHECK_FALSE(compiled.Valid);
        const auto error = std::ranges::find(compiled.Diagnostics, Keire::VfxCompileDiagnosticSeverity::Error,
                                             &Keire::VfxCompileDiagnostic::Severity);
        REQUIRE(error != compiled.Diagnostics.end());
        CHECK(error->Message == message);
    };

    SUBCASE("Scalar range")
    {
        auto definition = Keire::ConvertVfxEffectToGraph(EffectDefinition());
        BindGraphDefault(definition, 300, Id(2), "particlesPerSecond", Keire::VfxValueType::Scalar, -1.0F);
        checkCompileError(definition, "VFX emission rate is invalid.");
    }

    SUBCASE("Burst count")
    {
        auto definition = Keire::ConvertVfxEffectToGraph(EffectDefinition());
        BindGraphDefault(definition, 310, Id(3), "count", Keire::VfxValueType::Integer, std::int64_t{0});
        checkCompileError(definition, "VFX burst is invalid.");
    }

    SUBCASE("Burst cycles")
    {
        auto definition = Keire::ConvertVfxEffectToGraph(EffectDefinition());
        BindGraphDefault(definition, 320, Id(3), "cycles", Keire::VfxValueType::Integer, std::int64_t{0});
        checkCompileError(definition, "VFX burst is invalid.");
    }

    SUBCASE("Required asset")
    {
        auto definition = EffectDefinition();
        auto& shape = std::get<Keire::VfxShapeModule>(definition.Modules[2].Payload);
        shape.Shape = Keire::VfxShape::Mesh;
        shape.Mesh = Id(350);
        definition = Keire::ConvertVfxEffectToGraph(definition);
        BindGraphDefault(definition, 330, Id(4), "mesh", Keire::VfxValueType::Mesh, Keire::AssetId{});
        checkCompileError(definition, "VFX shape module is invalid.");
    }

    SUBCASE("Cross-property range")
    {
        auto definition = Keire::ConvertVfxEffectToGraph(EffectDefinition());
        BindGraphDefault(definition, 340, Id(5), "lifetimeMinimum", Keire::VfxValueType::Scalar, 3.0F);
        checkCompileError(definition, "VFX initialize module is invalid.");
    }
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
    CHECK(importer.Version == 3);
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

TEST_CASE("GPU VFX snapshots publish the scaled simulation delta for each handle")
{
    Keire::VfxWorldSpecification specification;
    specification.Backend = Keire::VfxBackend::Gpu;
    specification.MaximumEffects = 2;
    specification.MaximumParticles = 64;
    auto world = Keire::CreateRef<Keire::VfxWorld>(specification);
    const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(EffectDefinition());
    const auto paused = world->Activate({effect});
    const auto accelerated = world->Activate({effect});
    REQUIRE(paused);
    REQUIRE(accelerated);

    world->SetSimulationSpeed(paused, 0.0F);
    world->SetSimulationSpeed(accelerated, 2.0F);
    world->Update(0.25F);

    auto snapshot = world->CaptureRenderSnapshot();
    const auto firstSimulationStepRevision = snapshot.SimulationStepRevision();
    CHECK(firstSimulationStepRevision > 0);
    CHECK(snapshot.DeltaSeconds() == doctest::Approx(0.25F));
    REQUIRE(snapshot.GpuEmitters().size() == 2);
    CHECK(snapshot.GpuEmitters()[0].Handle == paused);
    CHECK(snapshot.GpuEmitters()[0].SimulationDeltaSeconds == 0.0F);
    CHECK(snapshot.GpuEmitters()[1].Handle == accelerated);
    CHECK(snapshot.GpuEmitters()[1].SimulationDeltaSeconds == doctest::Approx(0.5F));

    const auto snapshotRevision = snapshot.Revision();
    world->SetTransform(paused, {1.0F, 2.0F, 3.0F}, {});
    snapshot = world->CaptureRenderSnapshot();
    CHECK(snapshot.Revision() > snapshotRevision);
    CHECK(snapshot.SimulationStepRevision() == firstSimulationStepRevision);

    world->Update(0.0F);
    CHECK(world->CaptureRenderSnapshot().SimulationStepRevision() == firstSimulationStepRevision);

    world->SetSimulationSpeed(paused, 1.0F);
    world->SetSimulationSpeed(accelerated, 0.5F);
    world->Update(0.2F);
    snapshot = world->CaptureRenderSnapshot();
    CHECK(snapshot.SimulationStepRevision() > firstSimulationStepRevision);
    REQUIRE(snapshot.GpuEmitters().size() == 2);
    CHECK(snapshot.GpuEmitters()[0].SimulationDeltaSeconds == doctest::Approx(0.2F));
    CHECK(snapshot.GpuEmitters()[1].SimulationDeltaSeconds == doctest::Approx(0.1F));
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

TEST_CASE("VFX reload restarts CPU and GPU state when space seed or renderer representation changes")
{
    auto definition = EffectDefinition();
    definition.Modules.erase(definition.Modules.begin() + 1);

    std::array<Keire::VfxEffectDefinition, 3> incompatible{definition, definition, definition};
    incompatible[0].Space = Keire::VfxSimulationSpace::Local;
    ++incompatible[1].Seed;
    auto& renderer = std::get<Keire::VfxRendererModule>(incompatible[2].Modules.back().Payload);
    renderer.Type = Keire::VfxRendererType::Mesh;
    renderer.Mesh = Id(600);

    const auto baselineProgram = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(baselineProgram.Valid);
    for (const auto& candidate : incompatible)
    {
        const auto candidateProgram = Keire::CompileVfxEffect(candidate, Keire::VfxBackend::Cpu);
        REQUIRE(candidateProgram.Valid);
        CHECK(candidateProgram.StateLayoutHash != baselineProgram.StateLayoutHash);
    }

    SUBCASE("CPU particles and deterministic timeline restart")
    {
        for (const auto& candidate : incompatible)
        {
            auto world = Keire::CreateRef<Keire::VfxWorld>(
                Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 64});
            const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
            REQUIRE(handle);
            world->Update(0.25F);
            REQUIRE(world->Statistics().ActiveParticles > 0);

            REQUIRE(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(candidate), 2));
            CHECK(world->Statistics().ActiveParticles == 0);
            const auto snapshot = world->CaptureDebugSnapshot();
            REQUIRE(snapshot.EffectCount == 1);
            CHECK(snapshot.Effects[0].ElapsedSeconds == doctest::Approx(0.0F));
            CHECK(snapshot.Effects[0].Emitting);
        }
    }

    SUBCASE("GPU emitter state restarts without disturbing world reset ownership")
    {
        for (const auto& candidate : incompatible)
        {
            auto world = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
                .MaximumEffects = 1, .MaximumParticles = 64, .Backend = Keire::VfxBackend::Gpu});
            const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
            REQUIRE(handle);
            world->Update(0.25F);
            const auto before = world->CaptureRenderSnapshot();
            REQUIRE(before.GpuEmitters().size() == 1);
            REQUIRE(before.GpuEmitters().front().SpawnSequence > 0);
            const auto simulationRevision = before.GpuEmitters().front().SimulationRevision;
            const auto resetRevision = before.ResetRevision();

            REQUIRE(world->Reload(handle, Keire::CreateRef<Keire::VfxEffectAsset>(candidate), 2));
            const auto after = world->CaptureRenderSnapshot();
            REQUIRE(after.GpuEmitters().size() == 1);
            CHECK(after.GpuEmitters().front().SimulationRevision != simulationRevision);
            CHECK(after.GpuEmitters().front().SpawnSequence == 0);
            CHECK(after.ResetRevision() == resetRevision);
        }
    }
}

TEST_CASE("GPU stored-state parameter changes restart a draining non-looping effect coherently")
{
    auto definition = EffectDefinition();
    definition.Loop = false;
    definition.Duration = 0.25F;
    definition.Modules.erase(definition.Modules.begin() + 1);
    auto& initialize = std::get<Keire::VfxInitializeModule>(definition.Modules[2].Payload);
    initialize.LifetimeMinimum = 2.0F;
    initialize.LifetimeMaximum = 2.0F;
    definition = Keire::ConvertVfxEffectToGraph(definition);
    BindGraphDefault(definition, 700, Id(6), "force", Keire::VfxValueType::Vector3, Keire::Vector3{0.0F, 1.0F, 0.0F});

    auto world = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 64, .Backend = Keire::VfxBackend::Gpu});
    const auto handle = world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)});
    REQUIRE(handle);
    world->Update(0.25F);

    const auto before = world->CaptureRenderSnapshot();
    REQUIRE(before.GpuEmitters().size() == 1);
    REQUIRE(before.GpuEmitters().front().SpawnSequence > 0);
    const auto simulationRevision = before.GpuEmitters().front().SimulationRevision;
    auto debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK_FALSE(debug.Effects[0].Emitting);
    CHECK(debug.Effects[0].ElapsedSeconds == doctest::Approx(0.25F));

    world->SetParameter(handle, Id(700), Keire::Vector3{0.0F, 2.0F, 0.0F});
    const auto restarted = world->CaptureRenderSnapshot();
    REQUIRE(restarted.GpuEmitters().size() == 1);
    CHECK(restarted.GpuEmitters().front().SimulationRevision != simulationRevision);
    CHECK(restarted.GpuEmitters().front().SpawnSequence == 0);
    debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK(debug.Effects[0].ElapsedSeconds == doctest::Approx(0.0F));
    CHECK(debug.Effects[0].ActiveParticles == 0);
    CHECK(debug.Effects[0].Emitting);

    world->Update(0.25F);
    const auto replayed = world->CaptureRenderSnapshot();
    REQUIRE(replayed.GpuEmitters().size() == 1);
    CHECK(replayed.GpuEmitters().front().SpawnSequence == before.GpuEmitters().front().SpawnSequence);
    CHECK(world->IsAlive(handle));
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
    CHECK(registration.SchemaVersion == 2);
    CHECK(registration.RequiredComponents ==
          std::vector<Keire::ComponentTypeId>{Keire::TransformComponent::StaticType()});
    REQUIRE(registration.Properties.size() == 11);
    CHECK(registration.Properties.front().ExpectedAssetType == Keire::VfxEffectAsset::StaticType());
    CHECK(registration.Properties.back().Key == "parameterOverrides");
    REQUIRE(registration.Migrate);

    const auto component = registration.Factory();
    const auto migrated = registration.Migrate({{"effect", Id(100)},
                                                {"playOnAwake", false},
                                                {"autoDestroy", true},
                                                {"simulationSpeed", 2.0},
                                                {"seedOffset", std::int64_t{99}}},
                                               1);
    CHECK(std::get<std::string>(migrated.at("parameterOverrides")) == "[]");
    registration.Deserialize(*component, migrated, 2);
    const auto& emitter = dynamic_cast<const Keire::VfxEmitterComponent&>(*component);
    CHECK(emitter.Effect() == Id(100));
    CHECK_FALSE(emitter.PlayOnAwake());
    CHECK(emitter.AutoDestroy());
    CHECK(emitter.SimulationSpeed() == doctest::Approx(2.0F));
    CHECK(emitter.SeedOffset() == 99);
    CHECK(std::get<Keire::AssetId>(registration.Serialize(*component).at("effect")) == Id(100));
    CHECK_THROWS_WITH_AS(registration.Deserialize(*component, {}, 3),
                         "Unsupported VFX Emitter component schema version.", std::invalid_argument);
}
