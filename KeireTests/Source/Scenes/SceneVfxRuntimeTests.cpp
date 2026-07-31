#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Vfx/VfxSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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
        };

        auto& system = definition.Systems.front();
        system.Nodes.push_back({SceneVfxId(30),
                                "Rate",
                                Keire::VfxContextType::Spawn,
                                {-300.0F, 0.0F},
                                {{SceneVfxId(31), "Rate", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                SceneVfxId(20)});
        const auto emission = std::ranges::find(system.Nodes, SceneVfxId(2), &Keire::VfxGraphNode::Reference);
        if (emission == system.Nodes.end())
            throw std::logic_error("Converted scene VFX graph is missing its emission node.");
        const auto rate =
            std::ranges::find(emission->Pins, std::string("particlesPerSecond"), &Keire::VfxGraphPin::Semantic);
        if (rate == emission->Pins.end())
            throw std::logic_error("Converted scene VFX graph is missing its emission-rate pin.");
        system.Connections.push_back({SceneVfxId(32), SceneVfxId(30), SceneVfxId(31), emission->Id, rate->Id});
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
} // namespace

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
