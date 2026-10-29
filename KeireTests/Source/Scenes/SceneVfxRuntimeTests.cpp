#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Math/Vector3d.h"
#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Vfx/VfxSystem.h"
#include "KeireInternal/Scenes/VfxPhysicsCollision.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId SceneVfxId(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x5343454e45564658ULL, value);
    }

    [[nodiscard]] Keire::VfxEffectDefinition SceneRuntimeEffect(const bool rateExposed = true)
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = SceneVfxId(1);
        definition.Name = "Scene runtime override effect";
        definition.Loop = true;
        definition.Duration = 2.0F;
        definition.Capacity = 32;
        definition.Modules = {
            {SceneVfxId(2), true, Keire::VfxEmissionRateModule{10.0F}},
            {SceneVfxId(3), true, Keire::VfxShapeModule{}},
            {SceneVfxId(4), true, Keire::VfxInitializeModule{5.0F, 5.0F, {}, {}, {}, {}}},
            {SceneVfxId(5), true, Keire::VfxRendererModule{}},
        };
        definition = Keire::ConvertVfxEffectToGraph(definition);
        definition.Blackboard = {
            {SceneVfxId(20), "Rate", Keire::VfxValueType::Scalar, 4.0F, rateExposed},
            {SceneVfxId(21), "Hidden", Keire::VfxValueType::Scalar, 1.0F, false},
            {SceneVfxId(22), "Count", Keire::VfxValueType::Integer, std::int64_t{1}, true},
            {SceneVfxId(40), "Scalar Range", Keire::VfxValueType::ScalarRange, Keire::VfxScalarRange{0.0F, 1.0F}, true},
            {SceneVfxId(41), "Integer Range", Keire::VfxValueType::IntegerRange, Keire::VfxIntegerRange{-1, 1}, true},
            {SceneVfxId(42), "Unsigned Range", Keire::VfxValueType::UnsignedIntegerRange,
             Keire::VfxUnsignedIntegerRange{0, 1}, true},
            {SceneVfxId(43), "Vector2 Range", Keire::VfxValueType::Vector2Range,
             Keire::VfxVector2Range{{0.0F, 0.0F}, {1.0F, 1.0F}}, true},
            {SceneVfxId(44), "Vector3 Range", Keire::VfxValueType::Vector3Range,
             Keire::VfxVector3Range{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}, true},
            {SceneVfxId(45), "Vector4 Range", Keire::VfxValueType::Vector4Range,
             Keire::VfxVector4Range{{0.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}}, true},
            {SceneVfxId(46), "Color Range", Keire::VfxValueType::ColorRange,
             Keire::VfxColorRange{{0.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}}, true},
        };

        auto& system = definition.Systems.front();
        system.Nodes.push_back({SceneVfxId(30),
                                "Rate",
                                Keire::VfxContextType::Spawn,
                                {-300.0F, 0.0F},
                                {{SceneVfxId(31), "Rate", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                SceneVfxId(20),
                                {"keire.parameter"}});
        const auto spawn = std::ranges::find_if(system.Nodes, [](const Keire::VfxGraphNode& node)
                                                { return node.Context == Keire::VfxContextType::Spawn; });
        if (spawn == system.Nodes.end())
            throw std::logic_error("Converted scene VFX graph is missing its Spawn context.");
        const auto emission = std::ranges::find(spawn->Blocks, SceneVfxId(2), &Keire::VfxGraphBlock::Reference);
        if (emission == spawn->Blocks.end())
            throw std::logic_error("Converted scene VFX graph is missing its emission Block.");
        const auto rate =
            std::ranges::find(emission->Pins, std::string("particlesPerSecond"), &Keire::VfxGraphPin::Semantic);
        if (rate == emission->Pins.end())
            throw std::logic_error("Converted scene VFX graph is missing its emission-rate pin.");
        Keire::VfxGraphConnection binding;
        binding.Id = SceneVfxId(32);
        binding.OutputNode = SceneVfxId(30);
        binding.OutputPin = SceneVfxId(31);
        binding.InputNode = spawn->Id;
        binding.InputBlock = emission->Id;
        binding.InputPin = rate->Id;
        system.Connections.push_back(binding);
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] Keire::Ref<Keire::AssetSystem> CreateSceneVfxAssets()
    {
        Keire::AssetSystemSpecification specification;
        specification.Mode = Keire::AssetMode::Development;
        specification.Decoders.push_back(Keire::CreateVfxEffectAssetDecoder());
        return Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    }

    [[nodiscard]] Keire::VfxEffectDefinition GpuMeshEffect()
    {
        auto definition = SceneRuntimeEffect();
        definition.CompatibilityMode = Keire::VfxCompatibilityMode::NativeSchema4;
        auto initialize =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxInitializeModule>(module.Payload); });
        auto renderer =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxRendererModule>(module.Payload); });
        if (initialize == definition.Modules.end() || renderer == definition.Modules.end())
            throw std::logic_error("Scene VFX test effect is missing required modules.");
        auto& initializePayload = std::get<Keire::VfxInitializeModule>(initialize->Payload);
        initializePayload.RotationMinimum = {-20.0F, -180.0F, -180.0F};
        initializePayload.RotationMaximum = {20.0F, 180.0F, 180.0F};
        auto& rendererPayload = std::get<Keire::VfxRendererModule>(renderer->Payload);
        rendererPayload.Type = Keire::VfxRendererType::Mesh;
        rendererPayload.Mesh = SceneVfxId(90);
        for (auto& node : definition.Systems.front().Nodes)
        {
            for (auto& block : node.Blocks)
            {
                if (block.Reference == initialize->Id)
                {
                    for (auto& pin : block.Pins)
                    {
                        if (pin.Semantic == "rotationMinimum")
                            pin.DefaultValue = initializePayload.RotationMinimum;
                        else if (pin.Semantic == "rotationMaximum")
                            pin.DefaultValue = initializePayload.RotationMaximum;
                    }
                }
                else if (block.Reference == renderer->Id)
                {
                    const auto mesh = std::ranges::find(block.Pins, std::string("mesh"), &Keire::VfxGraphPin::Semantic);
                    if (mesh != block.Pins.end())
                        mesh->DefaultValue = rendererPayload.Mesh;
                }
            }
        }
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] Keire::VfxEffectDefinition SceneRuntimeEventEffect()
    {
        auto definition = SceneRuntimeEffect();
        definition.Name = "Scene runtime event effect";
        auto& system = definition.Systems.front();
        const auto source = std::ranges::find_if(system.Nodes, [](const Keire::VfxGraphNode& node)
                                                 { return node.Context == Keire::VfxContextType::Spawn; });
        if (source == system.Nodes.end())
            throw std::logic_error("Scene VFX test graph is missing its Spawn context.");
        const auto sourceId = source->Id;
        source->Context = Keire::VfxContextType::Event;
        source->Type = "Impact";
        source->TypeId.Value = "keire.context.event";
        source->Blocks.clear();
        std::erase_if(system.Connections, [sourceId](const Keire::VfxGraphConnection& connection)
                      { return connection.InputNode == sourceId && connection.InputBlock; });
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    [[nodiscard]] Keire::VfxEffectDefinition InvalidAllBackendsEffect()
    {
        auto definition = SceneRuntimeEffect();
        definition.CompatibilityMode = Keire::VfxCompatibilityMode::NativeSchema4;
        const auto initialize =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxInitializeModule>(module.Payload); });
        if (initialize == definition.Modules.end())
            throw std::logic_error("Scene VFX test effect is missing its Initialize module.");
        auto& initializePayload = std::get<Keire::VfxInitializeModule>(initialize->Payload);
        initializePayload.RotationMinimum = {45.0F, 0.0F, 0.0F};
        initializePayload.RotationMaximum = initializePayload.RotationMinimum;
        for (auto& node : definition.Systems.front().Nodes)
        {
            const auto block = std::ranges::find(node.Blocks, initialize->Id, &Keire::VfxGraphBlock::Reference);
            if (block == node.Blocks.end())
                continue;
            for (auto& pin : block->Pins)
            {
                if (pin.Semantic == "rotationMinimum")
                    pin.DefaultValue = initializePayload.RotationMinimum;
                else if (pin.Semantic == "rotationMaximum")
                    pin.DefaultValue = initializePayload.RotationMaximum;
            }
        }
        Keire::ValidateVfxEffect(definition);
        return definition;
    }
} // namespace

TEST_CASE("Scene simulation origin migration commits physics and VFX together")
{
    for (const bool rejectVfx : {false, true})
    {
        CAPTURE(rejectVfx);
        const auto scene =
            Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
        Keire::PhysicsSystemSpecification specification;
        specification.Mode = Keire::PhysicsMode::Enabled;
        const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
        const auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, Keire::Ref<Keire::AssetSystem>{},
                                                                          Keire::Ref<Keire::AudioSystem>{}, physics);
        session->Play();
        session->Pause();
        REQUIRE(session->Physics());
        REQUIRE(session->Vfx());
        Keire::PhysicsBodyDefinition bodyDefinition;
        bodyDefinition.Position.X = rejectVfx ? 0.0F : 0.015625F;
        const auto body = session->Physics()->CreateBody(bodyDefinition);
        Keire::VfxActivation activation{Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())};
        activation.Position.X = rejectVfx ? 0.015625F : 0.0F;
        const auto handle = session->Vfx()->Activate(activation);
        REQUIRE(handle);
        const auto beforeVfx = session->Vfx()->CaptureCheckpoint();
        CHECK_THROWS_AS(session->RebaseSimulationOrigin({16777216.0, 0.0, 0.0}), std::out_of_range);
        CHECK(session->Physics()->WorldOrigin() == Keire::Vector3d{});
        CHECK(session->Physics()->TryGetBody(body)->Position == bodyDefinition.Position);
        CHECK(session->Vfx()->CaptureCheckpoint() == beforeVfx);
        CHECK_THROWS_AS(session->RebaseSimulationOrigin({1152921504606846976.0, 0.0, 0.0}), std::out_of_range);
        CHECK(session->Physics()->WorldOrigin() == Keire::Vector3d{});
        CHECK(session->Physics()->TryGetBody(body)->Position == bodyDefinition.Position);
        CHECK(session->Vfx()->CaptureCheckpoint() == beforeVfx);
        CHECK(session->State() == Keire::ScenePlayState::Paused);
        const Keire::Vector3d destination{128.0, 64.0, -128.0};
        REQUIRE_NOTHROW(session->RebaseSimulationOrigin(destination));
        CHECK(session->Physics()->WorldOrigin() == destination);
        CHECK(session->Vfx()->WorldOrigin() == destination);
        CHECK(session->Physics()->TryGetBody(body)->Position.X == bodyDefinition.Position.X - 128.0F);
        CHECK(session->Vfx()->IsAlive(handle));
        session->RebasePhysicsOrigin({});
        REQUIRE_NOTHROW(session->RebaseSimulationOrigin(destination));
        CHECK(session->Physics()->WorldOrigin() == destination);
        CHECK(session->Vfx()->WorldOrigin() == destination);
        REQUIRE(session->Step(1.0F / 60.0F));
        CHECK(session->State() == Keire::ScenePlayState::Paused);
        session->Stop();
        CHECK_THROWS_AS(session->RebaseSimulationOrigin({}), std::logic_error);
    }
}

TEST_CASE("Play-mode scene keeps mixed Sprite and Mesh VFX on the GPU backend")
{
    const auto meshEffectId = SceneVfxId(80);
    const auto supportedEffectId = SceneVfxId(81);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(meshEffectId, Keire::CreateRef<Keire::VfxEffectAsset>(GpuMeshEffect())));
    REQUIRE(assets->PublishDevelopmentAsset(supportedEffectId,
                                            Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));

    auto scene = Keire::CreateRef<Keire::Scene>(SceneVfxId(82),
                                                Keire::SceneAsset::EmptyDefinition("Scene VFX failure isolation"));
    auto meshEntity = scene->CreateEntity("GPU Mesh VFX");
    auto meshEmitter = meshEntity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(meshEmitter);
    meshEmitter->SetEffect(meshEffectId);
    auto supportedEntity = scene->CreateEntity("Supported GPU VFX");
    auto supportedEmitter = supportedEntity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(supportedEmitter);
    supportedEmitter->SetEffect(supportedEffectId);

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.25F);

    CHECK(session->State() == Keire::ScenePlayState::Playing);
    CHECK(session->IsVfxAlive(meshEntity.Id()));
    CHECK(session->IsVfxAlive(supportedEntity.Id()));
    const auto world = session->Vfx();
    REQUIRE(world);
    const auto render = world->CaptureRenderSnapshot();
    REQUIRE(render.GpuEmitters().size() == 2);
    CHECK(render.Particles().empty());
    CHECK(std::ranges::any_of(render.GpuEmitters(), [](const Keire::VfxGpuEmitter& emitter)
                              { return emitter.Renderer == Keire::VfxRendererType::Mesh && emitter.Mesh; }));
    CHECK(std::ranges::any_of(render.GpuEmitters(), [](const Keire::VfxGpuEmitter& emitter)
                              { return emitter.Renderer == Keire::VfxRendererType::Sprite; }));

    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("Play-mode scene routes named VFX events into event systems")
{
    const auto effectId = SceneVfxId(86);
    const auto definition = SceneRuntimeEventEffect();
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(definition)));

    auto scene =
        Keire::CreateRef<Keire::Scene>(SceneVfxId(87), Keire::SceneAsset::EmptyDefinition("Scene VFX event routing"));
    auto entity = scene->CreateEntity("Event VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(effectId);

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.01F);
    CHECK_FALSE(session->SendVfxEvent(entity.Id(), "Missing", 2));
    REQUIRE(session->SendVfxEvent(entity.Id(), "Impact", 2));
    session->Update(0.01F);

    const auto world = session->Vfx();
    REQUIRE(world);
    const auto render = world->CaptureRenderSnapshot();
    REQUIRE(render.GpuEmitters().size() == 1);
    CHECK(render.GpuEmitters().front().System == definition.Systems.front().Id);
    CHECK(render.GpuEmitters().front().SpawnSequence == 2);

    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("Play-mode scene isolates VFX invalid on every backend without stopping gameplay")
{
    const auto rejectedEffectId = SceneVfxId(83);
    const auto supportedEffectId = SceneVfxId(84);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(rejectedEffectId,
                                            Keire::CreateRef<Keire::VfxEffectAsset>(InvalidAllBackendsEffect())));
    REQUIRE(assets->PublishDevelopmentAsset(supportedEffectId,
                                            Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));

    auto scene = Keire::CreateRef<Keire::Scene>(SceneVfxId(85),
                                                Keire::SceneAsset::EmptyDefinition("Scene VFX failure isolation"));
    auto rejectedEntity = scene->CreateEntity("Unsupported VFX");
    auto rejectedEmitter = rejectedEntity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(rejectedEmitter);
    rejectedEmitter->SetEffect(rejectedEffectId);
    auto supportedEntity = scene->CreateEntity("Supported GPU VFX");
    auto supportedEmitter = supportedEntity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(supportedEmitter);
    supportedEmitter->SetEffect(supportedEffectId);

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.25F);

    CHECK(session->State() == Keire::ScenePlayState::Playing);
    CHECK_FALSE(session->IsVfxAlive(rejectedEntity.Id()));
    CHECK(session->IsVfxAlive(supportedEntity.Id()));

    REQUIRE(assets->PublishDevelopmentAsset(rejectedEffectId,
                                            Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));
    session->Update(0.25F);
    CHECK(session->State() == Keire::ScenePlayState::Playing);
    CHECK(session->IsVfxAlive(rejectedEntity.Id()));
    CHECK(session->IsVfxAlive(supportedEntity.Id()));

    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("Play-mode scene VFX activates only compatible exposed Blackboard overrides")
{
    const auto effectId = SceneVfxId(100);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));

    auto scene =
        Keire::CreateRef<Keire::Scene>(SceneVfxId(101), Keire::SceneAsset::EmptyDefinition("Scene VFX overrides"));
    auto entity = scene->CreateEntity("VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(effectId);
    emitter->SetPlayOnAwake(true);
    emitter->SetParameterOverride({SceneVfxId(20), 8.0F});
    emitter->SetParameterOverride({SceneVfxId(21), 100.0F});
    emitter->SetParameterOverride({SceneVfxId(22), 100.0F});
    emitter->SetParameterOverride({SceneVfxId(23), 100.0F});

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.25F);

    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    REQUIRE(session->IsVfxAlive(entity.Id()));
    const auto world = session->Vfx();
    REQUIRE(world);
    const auto render = world->CaptureRenderSnapshot();
    REQUIRE(render.GpuEmitters().size() == 1);
    CHECK(render.GpuEmitters().front().SpawnSequence == 2);
    const auto debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK_FALSE(
        Keire::HasVfxDiagnostic(debug.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ParameterOverrideRejected));

    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("Play-mode scene VFX preserves rejected-override diagnostics after an asset reload")
{
    const auto effectId = SceneVfxId(110);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));

    auto scene =
        Keire::CreateRef<Keire::Scene>(SceneVfxId(111), Keire::SceneAsset::EmptyDefinition("Scene VFX reload"));
    auto entity = scene->CreateEntity("VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(effectId);
    emitter->SetPlayOnAwake(true);
    emitter->SetParameterOverride({SceneVfxId(20), 8.0F});

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.25F);
    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    REQUIRE(session->IsVfxAlive(entity.Id()));

    REQUIRE(
        assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect(false))));
    session->Update(0.1F);

    REQUIRE(session->State() == Keire::ScenePlayState::Playing);
    const auto world = session->Vfx();
    REQUIRE(world);
    auto debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK(
        Keire::HasVfxDiagnostic(debug.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ParameterOverrideRejected));

    session->Update(0.1F);
    debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK(
        Keire::HasVfxDiagnostic(debug.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ParameterOverrideRejected));

    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("Play-mode scene VFX range parameters update component and live handle transactionally")
{
    const auto effectId = SceneVfxId(120);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));

    auto scene =
        Keire::CreateRef<Keire::Scene>(SceneVfxId(121), Keire::SceneAsset::EmptyDefinition("Scene VFX ranges"));
    auto entity = scene->CreateEntity("VFX");
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(effectId);
    emitter->SetPlayOnAwake(true);

    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    const auto pendingRuntimeScene = session->RuntimeScene();
    REQUIRE(pendingRuntimeScene);
    const auto pendingRuntimeEntity = pendingRuntimeScene->FindEntity(entity.Id());
    const auto pendingRuntimeEmitter = pendingRuntimeEntity
                                           ? pendingRuntimeEntity.GetComponent<Keire::VfxEmitterComponent>()
                                           : Keire::Ref<Keire::VfxEmitterComponent>{};
    REQUIRE(pendingRuntimeEmitter);
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(40), Keire::VfxScalarRange{-2.0F, 5.0F}}));
    CHECK(pendingRuntimeEmitter->ParameterOverrides().empty());
    session->Update(0.1F);
    REQUIRE(session->IsVfxAlive(entity.Id()));

    const Keire::VfxScalarRange scalar{-2.0F, 5.0F};
    const Keire::VfxIntegerRange integer{std::numeric_limits<std::int64_t>::min(),
                                         std::numeric_limits<std::int64_t>::max()};
    const Keire::VfxUnsignedIntegerRange unsignedInteger{0, std::numeric_limits<std::uint64_t>::max()};
    const Keire::VfxVector2Range vector2{{-2.0F, -3.0F}, {4.0F, 8.0F}};
    const Keire::VfxVector3Range vector3{{-2.0F, -3.0F, 1.0F}, {4.0F, 8.0F, 9.0F}};
    const Keire::VfxVector4Range vector4{{-2.0F, -3.0F, 1.0F, 0.2F}, {4.0F, 8.0F, 9.0F, 0.8F}};
    const Keire::VfxColorRange color{{0.2F, 0.1F, 0.3F, 0.4F}, {0.9F, 0.7F, 0.8F, 1.0F}};
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(40), scalar}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(41), integer}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(42), unsignedInteger}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(43), vector2}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(44), vector3}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(45), vector4}));
    REQUIRE(session->SetVfxParameter(entity.Id(), {SceneVfxId(46), color}));

    const auto runtimeScene = session->RuntimeScene();
    REQUIRE(runtimeScene);
    const auto runtimeEntity = runtimeScene->FindEntity(entity.Id());
    const auto runtimeEmitter = runtimeEntity ? runtimeEntity.GetComponent<Keire::VfxEmitterComponent>()
                                              : Keire::Ref<Keire::VfxEmitterComponent>{};
    REQUIRE(runtimeEmitter);
    const auto findOverride = [&runtimeEmitter](const Keire::AssetId parameter)
    {
        const auto overrides = runtimeEmitter->ParameterOverrides();
        const auto result = std::ranges::find(overrides, parameter, &Keire::VfxParameterOverride::Parameter);
        return result == overrides.end() ? nullptr : &*result;
    };
    REQUIRE(findOverride(SceneVfxId(40)));
    CHECK(std::get<Keire::VfxScalarRange>(findOverride(SceneVfxId(40))->Value) == scalar);
    REQUIRE(findOverride(SceneVfxId(41)));
    CHECK(std::get<Keire::VfxIntegerRange>(findOverride(SceneVfxId(41))->Value) == integer);
    REQUIRE(findOverride(SceneVfxId(42)));
    CHECK(std::get<Keire::VfxUnsignedIntegerRange>(findOverride(SceneVfxId(42))->Value) == unsignedInteger);
    REQUIRE(findOverride(SceneVfxId(43)));
    CHECK(std::get<Keire::VfxVector2Range>(findOverride(SceneVfxId(43))->Value) == vector2);
    REQUIRE(findOverride(SceneVfxId(44)));
    CHECK(std::get<Keire::VfxVector3Range>(findOverride(SceneVfxId(44))->Value) == vector3);
    REQUIRE(findOverride(SceneVfxId(45)));
    CHECK(std::get<Keire::VfxVector4Range>(findOverride(SceneVfxId(45))->Value) == vector4);
    REQUIRE(findOverride(SceneVfxId(46)));
    CHECK(std::get<Keire::VfxColorRange>(findOverride(SceneVfxId(46))->Value) == color);

    const auto current = runtimeEmitter->ParameterOverrides();
    const std::vector<Keire::VfxParameterOverride> beforeRejected(current.begin(), current.end());
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(999), scalar}));
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(21), scalar}));
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(40), vector2}));
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(40), Keire::VfxScalarRange{5.0F, -2.0F}}));
    CHECK_FALSE(session->SetVfxParameter(
        entity.Id(), {SceneVfxId(44), Keire::VfxVector3Range{{0.0F, 0.0F, 0.0F},
                                                             {std::numeric_limits<float>::infinity(), 1.0F, 1.0F}}}));
    CHECK_FALSE(session->SetVfxParameter(Keire::EntityId(SceneVfxId(998)), {SceneVfxId(40), scalar}));
    CHECK(std::ranges::equal(runtimeEmitter->ParameterOverrides(), beforeRejected));

    bool wrongThreadRejected = false;
    std::thread wrongThread(
        [&]
        {
            try
            {
                (void)session->SetVfxParameter(entity.Id(), {SceneVfxId(40), Keire::VfxScalarRange{-9.0F, 9.0F}});
            }
            catch (...)
            {
                wrongThreadRejected = true;
            }
        });
    wrongThread.join();
    CHECK(wrongThreadRejected);
    CHECK(std::ranges::equal(runtimeEmitter->ParameterOverrides(), beforeRejected));

    session->Update(0.1F);
    REQUIRE(session->IsVfxAlive(entity.Id()));
    const auto world = session->Vfx();
    REQUIRE(world);
    const auto debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK_FALSE(
        Keire::HasVfxDiagnostic(debug.Effects[0].Diagnostics, Keire::VfxRuntimeDiagnostic::ParameterOverrideRejected));

    session->Stop();
    CHECK_FALSE(session->SetVfxParameter(entity.Id(), {SceneVfxId(40), Keire::VfxScalarRange{-9.0F, 9.0F}}));
    CHECK(std::ranges::equal(runtimeEmitter->ParameterOverrides(), beforeRejected));
    scene->Close();
    assets->Close();
}

TEST_CASE("Scene VFX preserves hierarchy orientation across distant translations and recovers from range failures")
{
    const auto effectId = SceneVfxId(200);
    auto assets = CreateSceneVfxAssets();
    REQUIRE(assets->PublishDevelopmentAsset(effectId, Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())));
    auto scene =
        Keire::CreateRef<Keire::Scene>(SceneVfxId(201), Keire::SceneAsset::EmptyDefinition("Precise VFX hierarchy"));
    const auto parent = scene->CreateEntity("Parent");
    const auto parentTransform = parent.GetComponent<Keire::TransformComponent>();
    REQUIRE(parentTransform);
    parentTransform->SetLocalEulerAngles({0.0F, 35.0F, 0.0F});
    parentTransform->SetLocalScale({-2.0F, 3.0F, 4.0F});
    auto entity = scene->CreateEntity("Emitter");
    const auto transform = entity.GetComponent<Keire::TransformComponent>();
    REQUIRE(transform);
    transform->SetParent(parent, false);
    transform->SetLocalPosition({0.25F, 2.0F, -1.0F});
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(effectId);

    Keire::Vector3 expectedPosition;
    Keire::Quaternion expectedRotation;
    Keire::Vector3 expectedScale;
    REQUIRE(
        Keire::Math::DecomposeTransform(transform->WorldMatrix(), expectedPosition, expectedRotation, expectedScale));
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{},
                                                                Keire::Ref<Keire::PhysicsSystem>{});
    session->Play();
    session->Update(0.25F);
    const auto runtimeParent =
        session->RuntimeScene()->FindEntity(parent.Id()).GetComponent<Keire::TransformComponent>();
    REQUIRE(runtimeParent);
    const auto world = session->Vfx();
    REQUIRE(world);
    REQUIRE(session->IsVfxAlive(entity.Id()));
    for (const double translation : {0.0, 1.0e15})
    {
        runtimeParent->SetLocalPositionPrecise({translation, 0.0, 0.0});
        session->Update(0.25F);
        const auto snapshot = world->CaptureRenderSnapshot();
        REQUIRE(snapshot.GpuEmitters().size() == 1);
        const auto& gpuEmitter = snapshot.GpuEmitters().front();
        CHECK(gpuEmitter.Rotation.X == doctest::Approx(expectedRotation.X));
        CHECK(gpuEmitter.Rotation.Y == doctest::Approx(expectedRotation.Y));
        CHECK(gpuEmitter.Rotation.Z == doctest::Approx(expectedRotation.Z));
        CHECK(gpuEmitter.Rotation.W == doctest::Approx(expectedRotation.W));
        CHECK(gpuEmitter.Position.Y == doctest::Approx(expectedPosition.Y));
        CHECK(gpuEmitter.Position.Z == doctest::Approx(expectedPosition.Z));
    }

    runtimeParent->SetLocalPositionPrecise({1.0e40, 0.0, 0.0});
    CHECK_NOTHROW(session->Update(0.25F));
    CHECK(session->State() == Keire::ScenePlayState::Playing);
    CHECK_FALSE(session->IsVfxAlive(entity.Id()));
    runtimeParent->SetLocalPositionPrecise({});
    session->Update(0.25F);
    CHECK(session->IsVfxAlive(entity.Id()));
    const auto recovered = world->CaptureRenderSnapshot();
    REQUIRE(recovered.GpuEmitters().size() == 1);
    CHECK(recovered.GpuEmitters().front().Position.X == doctest::Approx(expectedPosition.X));
    session->Stop();
    scene->Close();
    assets->Close();
}

TEST_CASE("VFX physics collisions preserve small offsets between distant simulation origins")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    const auto physics = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    for (const double distance : {0.0, 1.0e12, -1.0e12, 1.0e15})
    {
        CAPTURE(distance);
        const auto world = physics->CreateWorld({distance, distance, distance});
        Keire::PhysicsBodyDefinition floor;
        floor.Position = {0.0F, -0.5F, 0.0F};
        floor.HalfExtent = {1.0F, 0.5F, 1.0F};
        REQUIRE(world->CreateBody(floor));
        const Keire::Vector3d simulationOrigin{distance + 128.0, distance, distance};
        constexpr float localX = -128.0F + 0.015625F;
        const auto hit = Keire::Detail::QueryVfxPhysicsCollision(*world, simulationOrigin, {localX, 2.0F, 0.015625F},
                                                                 {localX, -2.0F, 0.015625F});
        REQUIRE(hit);
        CHECK(hit->Position.X == localX);
        CHECK(hit->Position.Y == doctest::Approx(0.0F));
        CHECK(hit->Position.Z == 0.015625F);
        CHECK(hit->Normal.Y == doctest::Approx(1.0F));
        CHECK_FALSE(Keire::Detail::QueryVfxPhysicsCollision(*world, simulationOrigin, {localX, 2.0F, 5.0F},
                                                            {localX, -2.0F, 5.0F}));
        CHECK_FALSE(Keire::Detail::QueryVfxPhysicsCollision(*world, simulationOrigin, {}, {}));
        CHECK_THROWS_AS((void)Keire::Detail::QueryVfxPhysicsCollision(*world, simulationOrigin, {1.0e30F, 0.0F, 0.0F},
                                                                      {-1.0e30F, 0.0F, 0.0F}),
                        std::invalid_argument);
    }
}

TEST_CASE("Deterministic scene VFX ignores render cadence and advances during paused steps")
{
    for (const bool withEffect : {false, true})
    {
        CAPTURE(withEffect);
        const auto scene = Keire::CreateRef<Keire::Scene>(SceneVfxId(301), Keire::SceneAsset::EmptyDefinition());
        const auto first = Keire::CreateRef<Keire::SceneRuntimeSession>(scene);
        const auto second = Keire::CreateRef<Keire::SceneRuntimeSession>(scene);
        for (const auto& session : {first, second})
        {
            session->SetDeterministicSimulation(true);
            session->Play();
            REQUIRE(session->Vfx());
            if (withEffect)
            {
                Keire::VfxActivation activation{Keire::CreateRef<Keire::VfxEffectAsset>(SceneRuntimeEffect())};
                REQUIRE(session->Vfx()->Activate(activation));
            }
        }
        const auto initial = first->CaptureVfxCheckpoint();
        for (int tick = 0; tick < 60; ++tick)
        {
            first->Update(0.017F, 0.2F);
            second->Update(0.003F, 0.1F);
            second->Update(0.009F, 0.9F);
            CHECK(first->CaptureVfxCheckpoint() == second->CaptureVfxCheckpoint());
            first->FixedUpdate(1.0F / 60.0F);
            second->FixedUpdate(1.0F / 60.0F);
            CHECK(first->CaptureVfxCheckpoint() == second->CaptureVfxCheckpoint());
        }
        CHECK(first->CaptureVfxCheckpoint() != initial);
        first->Pause();
        REQUIRE(first->Step(1.0F / 60.0F));
        second->FixedUpdate(1.0F / 60.0F);
        CHECK(first->CaptureVfxCheckpoint() == second->CaptureVfxCheckpoint());
        first->Stop();
        second->Stop();
        scene->Close();
    }
}
