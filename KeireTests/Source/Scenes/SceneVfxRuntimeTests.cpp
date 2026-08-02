#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Vfx/VfxSystem.h"

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
