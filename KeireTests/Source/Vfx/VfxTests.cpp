#include "Keire/Application.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxSystem.h"
#include "Keire/Vfx/VfxVolumeAsset.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
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

    [[nodiscard]] Keire::VfxGraphSystem CloneSystemWithStableIds(const Keire::VfxGraphSystem& source, std::string name)
    {
        auto result = source;
        std::map<Keire::AssetId, Keire::AssetId> replacements;
        const auto registerId = [&replacements](const Keire::AssetId value)
        {
            if (value)
                replacements.emplace(value, Keire::AssetId::Generate());
        };
        registerId(result.Id);
        for (const auto& node : result.Nodes)
        {
            registerId(node.Id);
            for (const auto& pin : node.Pins)
                registerId(pin.Id);
            for (const auto& block : node.Blocks)
            {
                registerId(block.Id);
                for (const auto& pin : block.Pins)
                    registerId(pin.Id);
            }
        }
        for (const auto& connection : result.Connections)
            registerId(connection.Id);
        const auto replace = [&replacements](Keire::AssetId& value)
        {
            if (const auto replacement = replacements.find(value); replacement != replacements.end())
                value = replacement->second;
        };
        replace(result.Id);
        result.Name = std::move(name);
        for (auto& node : result.Nodes)
        {
            replace(node.Id);
            for (auto& pin : node.Pins)
                replace(pin.Id);
            for (auto& block : node.Blocks)
            {
                replace(block.Id);
                for (auto& pin : block.Pins)
                    replace(pin.Id);
            }
        }
        for (auto& connection : result.Connections)
        {
            replace(connection.Id);
            replace(connection.OutputNode);
            replace(connection.OutputPin);
            replace(connection.InputNode);
            replace(connection.InputPin);
            replace(connection.InputBlock);
        }
        return result;
    }

    [[nodiscard]] Keire::VfxEffectDefinition MultiSystemEventEffect()
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        definition.Name = "Multi-system event effect";
        definition.Capacity = 32;
        auto eventSystem = CloneSystemWithStableIds(definition.Systems.front(), "Impact particles");
        auto source = std::ranges::find_if(eventSystem.Nodes, [](const Keire::VfxGraphNode& node)
                                           { return node.Context == Keire::VfxContextType::Spawn; });
        if (source == eventSystem.Nodes.end())
            throw std::logic_error("Test graph is missing its Spawn context.");
        source->Context = Keire::VfxContextType::Event;
        source->Type = "Impact";
        source->TypeId.Value = "keire.context.event";
        source->Blocks.clear();
        eventSystem.DataType = Keire::VfxParticleDataType::ParticleStrip;
        eventSystem.ParticlesPerStrip = 4;
        definition.Systems.push_back(std::move(eventSystem));
        Keire::ValidateVfxEffect(definition);
        return definition;
    }

    void BindGraphDefault(Keire::VfxEffectDefinition& definition, const std::uint64_t idBase,
                          const Keire::AssetId moduleId, const std::string& semantic, const Keire::VfxValueType type,
                          Keire::VfxParameterValue value)
    {
        auto& system = definition.Systems.front();
        const auto context = std::ranges::find_if(
            system.Nodes,
            [moduleId](const Keire::VfxGraphNode& node)
            {
                return std::ranges::find(node.Blocks, moduleId, &Keire::VfxGraphBlock::Reference) != node.Blocks.end();
            });
        if (context == system.Nodes.end())
            throw std::logic_error("Test graph Block was not found.");
        const auto block = std::ranges::find(context->Blocks, moduleId, &Keire::VfxGraphBlock::Reference);
        const auto input = std::ranges::find(block->Pins, semantic, &Keire::VfxGraphPin::Semantic);
        if (input == block->Pins.end())
            throw std::logic_error("Test graph Block input was not found.");
        const auto moduleNodeId = context->Id;
        const auto moduleBlockId = block->Id;
        const auto moduleInputId = input->Id;

        definition.Blackboard.push_back(
            {Id(idBase), "Bound default " + std::to_string(idBase), type, std::move(value), true});
        system.Nodes.push_back({Id(idBase + 1),
                                "Bound default",
                                context->Context,
                                {},
                                {{Id(idBase + 2), "Value", type, false, "value", std::nullopt}},
                                {},
                                Keire::VfxGraphNodeKind::Parameter,
                                Id(idBase),
                                {"keire.parameter"}});
        Keire::VfxGraphConnection connection;
        connection.Id = Id(idBase + 3);
        connection.OutputNode = Id(idBase + 1);
        connection.OutputPin = Id(idBase + 2);
        connection.InputNode = moduleNodeId;
        connection.InputPin = moduleInputId;
        connection.InputBlock = moduleBlockId;
        system.Connections.push_back(connection);
    }

    void ConvertBlocksToSchemaThreeFlowNodes(Keire::VfxEffectDefinition& definition)
    {
        struct Stage
        {
            Keire::AssetId Context;
            Keire::AssetId InputPin;
            Keire::AssetId OutputPin;
            std::vector<Keire::VfxGraphNode> Modules;
        };

        auto& system = definition.Systems.front();
        const std::array contexts{Keire::VfxContextType::Spawn, Keire::VfxContextType::Initialize,
                                  Keire::VfxContextType::Update, Keire::VfxContextType::Output};
        std::array<Stage, 4> stages;
        std::uint64_t nextId = 5'000;
        for (std::size_t stageIndex = 0; stageIndex < contexts.size(); ++stageIndex)
        {
            auto context =
                std::ranges::find_if(system.Nodes, [type = contexts[stageIndex]](const auto& node)
                                     { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == type; });
            REQUIRE(context != system.Nodes.end());
            auto& stage = stages[stageIndex];
            stage.Context = context->Id;
            if (const auto input =
                    std::ranges::find_if(context->Pins, [](const auto& pin)
                                         { return pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
                input != context->Pins.end())
            {
                stage.InputPin = input->Id;
            }
            if (const auto output =
                    std::ranges::find_if(context->Pins, [](const auto& pin)
                                         { return !pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
                output != context->Pins.end())
            {
                stage.OutputPin = output->Id;
            }
            for (const auto& block : context->Blocks)
            {
                const auto module =
                    std::ranges::find(definition.Modules, block.Reference, &Keire::VfxModuleDefinition::Id);
                REQUIRE(module != definition.Modules.end());
                auto node = Keire::CreateVfxGraphModuleNode(*module, context->EditorPosition);
                node.Id = block.Id;
                node.Pins.clear();
                node.Pins.push_back(
                    {Id(nextId++), "Particles", Keire::VfxValueType::ParticleStream, true, "particles"});
                node.Pins.insert(node.Pins.end(), block.Pins.begin(), block.Pins.end());
                node.Pins.push_back(
                    {Id(nextId++), "Particles", Keire::VfxValueType::ParticleStream, false, "particles"});
                stage.Modules.push_back(std::move(node));
            }
            context->Blocks.clear();
        }

        std::erase_if(system.Connections,
                      [&system](const Keire::VfxGraphConnection& connection)
                      {
                          const auto node =
                              std::ranges::find(system.Nodes, connection.OutputNode, &Keire::VfxGraphNode::Id);
                          if (node == system.Nodes.end())
                              return false;
                          const auto pin = std::ranges::find(node->Pins, connection.OutputPin, &Keire::VfxGraphPin::Id);
                          return pin != node->Pins.end() && pin->Type == Keire::VfxValueType::ParticleStream;
                      });
        for (auto& connection : system.Connections)
        {
            if (!connection.InputBlock)
                continue;
            connection.InputNode = connection.InputBlock;
            connection.InputBlock = {};
        }

        auto previousNode = stages.front().Context;
        auto previousPin = stages.front().OutputPin;
        for (std::size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex)
        {
            if (stageIndex > 0 && stageIndex < stages.size() - 1)
            {
                system.Connections.push_back(
                    {Id(nextId++), previousNode, previousPin, stages[stageIndex].Context, stages[stageIndex].InputPin});
                previousNode = stages[stageIndex].Context;
                previousPin = stages[stageIndex].OutputPin;
            }
            for (const auto& module : stages[stageIndex].Modules)
            {
                const auto input =
                    std::ranges::find_if(module.Pins, [](const auto& pin)
                                         { return pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
                const auto output =
                    std::ranges::find_if(module.Pins, [](const auto& pin)
                                         { return !pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
                REQUIRE(input != module.Pins.end());
                REQUIRE(output != module.Pins.end());
                system.Connections.push_back({Id(nextId++), previousNode, previousPin, module.Id, input->Id});
                previousNode = module.Id;
                previousPin = output->Id;
            }
            if (stageIndex == stages.size() - 1)
            {
                system.Connections.push_back(
                    {Id(nextId++), previousNode, previousPin, stages[stageIndex].Context, stages[stageIndex].InputPin});
            }
        }
        for (auto& stage : stages)
            for (auto& module : stage.Modules)
                system.Nodes.push_back(std::move(module));
        definition.SchemaVersion = 3;
    }

    struct HeadlessRenderProbe final
    {
        Keire::RenderStatistics Statistics;
        Keire::RenderCapabilities Capabilities;
        bool Submitted = false;
        bool NonOwnerWarmupRejected = false;
    };

    class HeadlessVfxRenderLayer final : public Keire::Layer
    {
      public:
        explicit HeadlessVfxRenderLayer(HeadlessRenderProbe& probe) : Layer("Headless VFX Render"), m_Probe(probe) {}

      protected:
        void OnAttach() override
        {
            m_Scene = Keire::CreateRef<Keire::Scene>(Id(900), Keire::SceneAsset::EmptyDefinition("VFX Render"));
            const auto renderer = Owner().Renderer();
            renderer->RequestGpuVfxPipelineWarmup();
            std::jthread nonOwner(
                [this, renderer]
                {
                    try
                    {
                        renderer->RequestGpuVfxPipelineWarmup();
                    }
                    catch (const std::logic_error&)
                    {
                        m_Probe.NonOwnerWarmupRejected = true;
                    }
                });
            nonOwner.join();
            m_View = renderer->CreateView({.Name = "VFX Headless", .Width = 64, .Height = 64});
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
    CHECK(decoded->Definition().SchemaVersion == Keire::CurrentVfxSchemaVersion);
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

TEST_CASE("VFX Kill Shape round trips and applies solid and inverted CPU volume semantics")
{
    const auto createDefinition = [](const Keire::VfxKillShapeMode mode)
    {
        Keire::VfxEffectDefinition definition;
        definition.EmitterId = Id(mode == Keire::VfxKillShapeMode::Solid ? 2'500 : 2'510);
        definition.Name = "Kill Shape";
        definition.Loop = false;
        definition.Duration = 10.0F;
        definition.Space = Keire::VfxSimulationSpace::Local;
        definition.Capacity = 4;
        definition.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
        definition.Modules = {
            {Id(2'501), true, Keire::VfxBurstModule{0.0F, 1}},
            {Id(2'502), true, Keire::VfxShapeModule{}},
            {Id(2'503), true,
             Keire::VfxInitializeModule{5.0F, 5.0F, Keire::Vector3{}, Keire::Vector3{}, Keire::Vector3{},
                                        Keire::Vector3{}}},
            {Id(2'504), true, Keire::VfxKillShapeModule{Keire::VfxShape::Sphere, {}, {0.5F, 0.5F, 0.5F}, 1.0F, mode}},
            {Id(2'505), true, Keire::VfxRendererModule{}},
        };
        return definition;
    };

    const auto solidDefinition = createDefinition(Keire::VfxKillShapeMode::Solid);
    const auto encoded = Keire::VfxEffectAsset::Encode(solidDefinition);
    const auto decoded = Keire::VfxEffectAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition() == solidDefinition);
    CHECK(Keire::VfxEffectAsset::Encode(decoded->Definition()) == encoded);

    const auto simulate = [](Keire::VfxEffectDefinition definition)
    {
        auto world =
            Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 4});
        REQUIRE(world->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(std::move(definition))}));
        world->Update(0.01F);
        REQUIRE(world->Statistics().ActiveParticles == 1);
        world->Update(0.01F);
        return world->Statistics().ActiveParticles;
    };

    CHECK(simulate(solidDefinition) == 0);
    CHECK(simulate(createDefinition(Keire::VfxKillShapeMode::Inverted)) == 1);

    auto invalid = solidDefinition;
    std::get<Keire::VfxKillShapeModule>(invalid.Modules[3].Payload).Shape = Keire::VfxShape::Cone;
    CHECK_THROWS_WITH_AS(Keire::ValidateVfxEffect(invalid), "VFX kill-shape module is invalid.", std::invalid_argument);
}

TEST_CASE("VFX schema four round trips every persisted value property block and endpoint field")
{
    auto definition = EffectDefinition();
    std::uint64_t parameterId = 2000;
    const auto addParameter =
        [&](const std::string& name, const Keire::VfxValueType type, Keire::VfxParameterValue value)
    { definition.Blackboard.push_back({Id(parameterId++), name, type, std::move(value), true}); };

    Keire::Matrix4 matrix;
    for (std::size_t index = 0; index < matrix.Elements.size(); ++index)
        matrix.Elements[index] = static_cast<float>(index) + 0.25F;
    const Keire::Curve1D curve({{0.0F, -1.0F, 0.0F, 2.0F, Keire::CurveInterpolation::Cubic},
                                {1.0F, 3.0F, -2.0F, 0.0F, Keire::CurveInterpolation::Linear}});
    const Keire::ColorGradient gradient({{0.0F, {0.1F, 0.2F, 0.3F, 0.4F}}, {1.0F, {0.9F, 0.8F, 0.7F, 0.6F}}},
                                        Keire::GradientInterpolation::Constant);

    addParameter("Boolean", Keire::VfxValueType::Boolean, true);
    addParameter("Integer", Keire::VfxValueType::Integer, std::int64_t{-42});
    addParameter("Scalar", Keire::VfxValueType::Scalar, 3.25F);
    addParameter("Vector2", Keire::VfxValueType::Vector2, Keire::Vector2{1.0F, 2.0F});
    addParameter("Vector3", Keire::VfxValueType::Vector3, Keire::Vector3{1.0F, 2.0F, 3.0F});
    addParameter("Color", Keire::VfxValueType::Color, Keire::Color{0.1F, 0.2F, 0.3F, 0.4F});
    addParameter("Texture", Keire::VfxValueType::Texture, Id(2100));
    addParameter("Mesh", Keire::VfxValueType::Mesh, Id(2101));
    addParameter("Asset", Keire::VfxValueType::Asset, Id(2102));
    addParameter("Unsigned Integer", Keire::VfxValueType::UnsignedInteger, std::uint64_t{0xfedcba9876543210ULL});
    addParameter("Vector4", Keire::VfxValueType::Vector4, Keire::Vector4{1.0F, 2.0F, 3.0F, 4.0F});
    addParameter("Quaternion", Keire::VfxValueType::Quaternion, Keire::Quaternion{0.1F, 0.2F, 0.3F, 0.9F});
    addParameter("Matrix", Keire::VfxValueType::Matrix, matrix);
    addParameter("Curve", Keire::VfxValueType::Curve, curve);
    addParameter("Gradient", Keire::VfxValueType::Gradient, gradient);
    addParameter("Scalar Range", Keire::VfxValueType::ScalarRange, Keire::VfxScalarRange{-1.0F, 2.0F});
    addParameter("Integer Range", Keire::VfxValueType::IntegerRange, Keire::VfxIntegerRange{-10, 20});
    addParameter("Unsigned Integer Range", Keire::VfxValueType::UnsignedIntegerRange,
                 Keire::VfxUnsignedIntegerRange{10, 20});
    addParameter("Vector2 Range", Keire::VfxValueType::Vector2Range,
                 Keire::VfxVector2Range{{-1.0F, -2.0F}, {3.0F, 4.0F}});
    addParameter("Vector3 Range", Keire::VfxValueType::Vector3Range,
                 Keire::VfxVector3Range{{-1.0F, -2.0F, -3.0F}, {4.0F, 5.0F, 6.0F}});
    addParameter("Vector4 Range", Keire::VfxValueType::Vector4Range,
                 Keire::VfxVector4Range{{-1.0F, -2.0F, -3.0F, -4.0F}, {5.0F, 6.0F, 7.0F, 8.0F}});
    addParameter("Color Range", Keire::VfxValueType::ColorRange,
                 Keire::VfxColorRange{{0.1F, 0.2F, 0.3F, 0.4F}, {0.5F, 0.6F, 0.7F, 0.8F}});
    addParameter("Texture 2D Array", Keire::VfxValueType::Texture2DArray, Id(2103));
    addParameter("Texture 3D", Keire::VfxValueType::Texture3D, Id(2104));
    addParameter("Texture Cube", Keire::VfxValueType::TextureCube, Id(2105));
    addParameter("Buffer", Keire::VfxValueType::Buffer, Id(2106));
    addParameter("Point Cache", Keire::VfxValueType::PointCache, Id(2107));
    addParameter("Signed Distance Field", Keire::VfxValueType::SignedDistanceField, Id(2108));

    Keire::VfxGraphNode source;
    source.Id = Id(2200);
    source.TypeId.Value = "keire.context.spawn";
    source.Type = "Schema Source";
    source.Context = Keire::VfxContextType::Spawn;
    source.Pins = {{Id(2201), "Dynamic Input", Keire::VfxValueType::Scalar, true, "dynamic", 1.0F},
                   {Id(2202), "Value", Keire::VfxValueType::Scalar, false, "value", std::nullopt}};
    source.Kind = Keire::VfxGraphNodeKind::Context;
    source.DefinitionVersion = 7;
    source.ResolvedSignature = {Keire::VfxValueType::Scalar, Keire::VfxValueType::Vector4};
    source.DynamicPinOrder = {Id(2201)};
    source.Properties = {{"Boolean", true},
                         {"Integer", std::int64_t{-4}},
                         {"Unsigned Integer", std::uint64_t{0xf000000000000000ULL}},
                         {"Scalar", 2.5F},
                         {"String", std::string("typed")},
                         {"Vector2", Keire::Vector2{1.0F, 2.0F}},
                         {"Vector3", Keire::Vector3{1.0F, 2.0F, 3.0F}},
                         {"Vector4", Keire::Vector4{1.0F, 2.0F, 3.0F, 4.0F}},
                         {"Quaternion", Keire::Quaternion{0.0F, 0.0F, 0.0F, 1.0F}},
                         {"Color", Keire::Color{0.1F, 0.2F, 0.3F, 0.4F}},
                         {"Matrix", matrix},
                         {"Asset", Id(2203)}};

    Keire::VfxGraphBlock block;
    block.Id = Id(2210);
    block.TypeId.Value = "keire.block.force";
    block.Type = "Schema Block";
    block.Enabled = false;
    block.Pins = {{Id(2211), "Input", Keire::VfxValueType::Scalar, true, "input", 0.5F},
                  {Id(2212), "Output", Keire::VfxValueType::Scalar, false, "output", std::nullopt}};
    block.Properties = {{"Mode", std::string("schema")}};
    block.DefinitionVersion = 3;
    block.Reference = Id(6);

    Keire::VfxGraphNode context;
    context.Id = Id(2220);
    context.TypeId.Value = "keire.context.update";
    context.Type = "Schema Context";
    context.Context = Keire::VfxContextType::Update;
    context.Kind = Keire::VfxGraphNodeKind::Context;
    context.Blocks.push_back(block);

    Keire::VfxGraphNode sink;
    sink.Id = Id(2230);
    sink.TypeId.Value = "keire.context.output";
    sink.Type = "Schema Sink";
    sink.Context = Keire::VfxContextType::Output;
    sink.Pins = {{Id(2231), "Value", Keire::VfxValueType::Scalar, true, "value", 0.0F}};
    sink.Kind = Keire::VfxGraphNodeKind::Context;

    Keire::VfxGraphSystem system;
    system.Id = Id(2240);
    system.Name = "Schema Four";
    system.Nodes = {source, context, sink};
    system.Connections = {{Id(2241), source.Id, Id(2202), context.Id, Id(2211), {}, block.Id},
                          {Id(2242), context.Id, Id(2212), sink.Id, Id(2231), block.Id, {}}};
    system.Connections.front().RoutingPoints = {{180.0F, 72.0F}, {260.0F, 104.0F}};
    definition.Systems.push_back(std::move(system));

    const auto encoded = Keire::VfxEffectAsset::Encode(definition);
    const auto document = nlohmann::json::parse(reinterpret_cast<const char*>(encoded.data()),
                                                reinterpret_cast<const char*>(encoded.data() + encoded.size()));
    CHECK(document.at("schemaVersion") == Keire::CurrentVfxSchemaVersion);
    CHECK(document.at("blackboard").at(9).at("default").get<std::uint64_t>() == 0xfedcba9876543210ULL);
    CHECK(document.at("systems").at(0).at("nodes").at(0).at("properties").at(1).at("type") == "integer");
    CHECK(document.at("systems").at(0).at("nodes").at(1).at("blocks").at(0).at("typeId") == "keire.block.force");
    CHECK(document.at("systems").at(0).at("connections").at(0).at("inputBlock") == Id(2210).ToString());
    CHECK(document.at("systems").at(0).at("connections").at(0).at("routing").size() == 2);

    const auto decoded = Keire::VfxEffectAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition() == definition);
    CHECK(Keire::VfxEffectAsset::Encode(decoded->Definition()) == encoded);
}

TEST_CASE("VFX schema one definitions publish as schema four legacy programs without mutating the source")
{
    auto legacy = EffectDefinition();
    legacy.SchemaVersion = 1;
    legacy.Systems.clear();
    legacy.Blackboard.clear();

    const auto published = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(legacy));
    CHECK(legacy.SchemaVersion == 1);
    CHECK(legacy.Systems.empty());
    CHECK(published->Definition().SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(published->Definition().ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
    CHECK(published->Definition().Systems.empty());
}

TEST_CASE("Historical VFX schema fixtures migrate to schema four without replacing stable identities")
{
    const std::string header =
        R"("emitterId":"56465854-4553-5449-0000-000000000001","name":"Golden","loop":false,"duration":1.0,"space":"world","seed":7,"capacity":32,"modules":[{"id":"56465854-4553-5449-0000-000000000002","enabled":true,"type":"emissionRate","particlesPerSecond":1.0},{"id":"56465854-4553-5449-0000-000000000003","enabled":true,"type":"renderer","renderer":"sprite","sprite":"","mesh":""}])";
    const auto schemaOne = Bytes("{\"schemaVersion\":1," + header + "}");
    const auto schemaTwo = Bytes(
        "{\"schemaVersion\":2," + header +
        R"(,"systems":[{"id":"56465854-4553-5449-0000-000000000010","name":"Legacy Two","nodes":[{"id":"56465854-4553-5449-0000-000000000011","type":"Spawn Context","context":"spawn","position":[12.0,34.0],"pins":[],"customHlsl":""}],"connections":[]}],"blackboard":[]})");
    const auto schemaThree = Bytes(
        "{\"schemaVersion\":3," + header +
        R"(,"executionSource":"legacyModules","systems":[{"id":"56465854-4553-5449-0000-000000000020","name":"Legacy Three","nodes":[{"id":"56465854-4553-5449-0000-000000000021","type":"Emission Rate","context":"spawn","position":[56.0,78.0],"pins":[],"customHlsl":"","kind":"module","reference":"56465854-4553-5449-0000-000000000002"}],"connections":[]}],"blackboard":[]})");

    const auto one = Keire::VfxEffectAsset::Decode(schemaOne)->Definition();
    const auto two = Keire::VfxEffectAsset::Decode(schemaTwo)->Definition();
    const auto three = Keire::VfxEffectAsset::Decode(schemaThree)->Definition();
    CHECK(one.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(two.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(three.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(one.ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
    CHECK(two.ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
    CHECK(three.ExecutionSource == Keire::VfxExecutionSource::LegacyModules);
    REQUIRE(two.Systems.size() == 1);
    REQUIRE(two.Systems.front().Nodes.size() == 1);
    CHECK(two.Systems.front().Id == Id(16));
    CHECK(two.Systems.front().Nodes.front().Id == Id(17));
    CHECK(two.Systems.front().Nodes.front().TypeId.Value == "keire.context.spawn");
    CHECK(two.Systems.front().Nodes.front().Blocks.empty());
    REQUIRE(three.Systems.size() == 1);
    REQUIRE(three.Systems.front().Nodes.size() == 1);
    CHECK(three.Systems.front().Id == Id(32));
    CHECK(three.Systems.front().Nodes.front().Id == Id(33));
    CHECK(three.Systems.front().Nodes.front().Reference == Id(2));
    CHECK(three.Systems.front().Nodes.front().TypeId.Value == "keire.block.emission-rate");

    auto historical = Keire::ConvertVfxEffectToGraph(EffectDefinition());
    historical.SchemaVersion = 3;
    historical.Systems.front().Nodes.push_back(
        {.Id = Id(2400), .Type = "Legacy Parameter", .Kind = Keire::VfxGraphNodeKind::Parameter});
    historical.Systems.front().Nodes.push_back(
        {.Id = Id(2401), .Type = "Legacy HLSL", .Kind = Keire::VfxGraphNodeKind::CustomHlsl});
    historical.Systems.front().Nodes.push_back(
        {.Id = Id(2402), .Type = "Add", .Kind = Keire::VfxGraphNodeKind::Operator});
    const auto migrated = Keire::MigrateVfxEffectToSchema4(historical);
    CHECK(historical.SchemaVersion == 3);
    CHECK(migrated.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(migrated.EmitterId == historical.EmitterId);
    CHECK(migrated.Systems.front().Id == historical.Systems.front().Id);
    CHECK(migrated.Systems.front().Connections == historical.Systems.front().Connections);
    const auto parameterNode = std::ranges::find(migrated.Systems.front().Nodes, Id(2400), &Keire::VfxGraphNode::Id);
    const auto hlslNode = std::ranges::find(migrated.Systems.front().Nodes, Id(2401), &Keire::VfxGraphNode::Id);
    const auto operatorNode = std::ranges::find(migrated.Systems.front().Nodes, Id(2402), &Keire::VfxGraphNode::Id);
    REQUIRE(parameterNode != migrated.Systems.front().Nodes.end());
    REQUIRE(hlslNode != migrated.Systems.front().Nodes.end());
    REQUIRE(operatorNode != migrated.Systems.front().Nodes.end());
    CHECK(parameterNode->TypeId.Value == "keire.parameter");
    CHECK(hlslNode->TypeId.Value == "keire.operator.portable-hlsl");
    CHECK(operatorNode->TypeId.Value == "keire.operator.add");
    CHECK(Keire::MigrateVfxEffectToSchema4(migrated) == migrated);

    auto unsupported = migrated;
    unsupported.SchemaVersion = Keire::CurrentVfxSchemaVersion + 1;
    CHECK_THROWS_WITH_AS((void)Keire::MigrateVfxEffectToSchema4(unsupported),
                         "VFX effect migration source schema is unsupported.", std::invalid_argument);
}

TEST_CASE("schema-four module layout revisions add resource pins without invalidating existing effects")
{
    auto canonical = Keire::VfxEffectAsset::DefaultDefinition();
    const auto update = std::ranges::find_if(canonical.Systems.front().Nodes, [](const Keire::VfxGraphNode& node)
                                             { return node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != canonical.Systems.front().Nodes.end());
    update->Blocks.push_back(Keire::CreateVfxGraphPortableHlslBlock("Velocity = float3(0.0, 1.0, 0.0);"));
    const auto portableBlockId = update->Blocks.back().Id;
    const auto encoded = Keire::VfxEffectAsset::Encode(canonical);
    auto document = nlohmann::json::parse(reinterpret_cast<const char*>(encoded.data()),
                                          reinterpret_cast<const char*>(encoded.data() + encoded.size()));

    for (auto& system : document.at("systems"))
    {
        for (auto& node : system.at("nodes"))
        {
            for (auto& block : node.at("blocks"))
            {
                const auto typeId = block.at("typeId").get<std::string>();
                const auto removedSemantic = typeId == "keire.block.shape"       ? "volume"
                                             : typeId == "keire.output.renderer" ? "material"
                                                                                 : "";
                if (removedSemantic[0] == '\0')
                    continue;
                block["definitionVersion"] = 1;
                auto& pins = block.at("pins");
                pins.erase(std::remove_if(pins.begin(), pins.end(), [removedSemantic](const nlohmann::json& pin)
                                          { return pin.at("semantic").get<std::string>() == removedSemantic; }),
                           pins.end());
            }
        }
    }

    const auto historical = Bytes(document.dump(2));
    const auto first = Keire::VfxEffectAsset::Decode(historical);
    const auto second = Keire::VfxEffectAsset::Decode(historical);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->Definition() == second->Definition());

    bool foundVolume = false;
    bool foundMaterial = false;
    bool foundPortable = false;
    for (const auto& node : first->Definition().Systems.front().Nodes)
    {
        for (const auto& block : node.Blocks)
        {
            if (block.Id == portableBlockId)
            {
                CHECK(block.TypeId.View() == "keire.block.portable-hlsl");
                foundPortable = true;
            }
            if (block.TypeId.View() != "keire.block.shape" && block.TypeId.View() != "keire.output.renderer")
                continue;
            CHECK(block.DefinitionVersion == 2);
            const auto expectedSemantic = block.TypeId.View() == "keire.block.shape" ? "volume" : "material";
            const auto pin = std::ranges::find(block.Pins, expectedSemantic, &Keire::VfxGraphPin::Semantic);
            REQUIRE(pin != block.Pins.end());
            CHECK(pin->Id);
            CHECK(pin->DefaultValue == Keire::VfxParameterValue{Keire::AssetId{}});
            foundVolume |= expectedSemantic == std::string_view("volume");
            foundMaterial |= expectedSemantic == std::string_view("material");
        }
    }
    CHECK(foundVolume);
    CHECK(foundMaterial);
    CHECK(foundPortable);
    Keire::ValidateVfxEffect(first->Definition());
    CHECK(Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(first->Definition()))->Definition() ==
          first->Definition());
}

TEST_CASE("schema-three flow Modules migrate to ordered Context Blocks without changing executable identity")
{
    auto historical = Keire::ConvertVfxEffectToGraph(EffectDefinition());
    BindGraphDefault(historical, 2'500, Id(6), "gravityMultiplier", Keire::VfxValueType::Scalar, 2.0F);
    const auto originalModuleCount = historical.Modules.size();
    ConvertBlocksToSchemaThreeFlowNodes(historical);

    const auto forceNode = std::ranges::find(historical.Systems.front().Nodes, Id(6), &Keire::VfxGraphNode::Reference);
    REQUIRE(forceNode != historical.Systems.front().Nodes.end());
    const auto forceBlockId = forceNode->Id;
    const auto gravityPin =
        std::ranges::find(forceNode->Pins, std::string("gravityMultiplier"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(gravityPin != forceNode->Pins.end());
    const auto gravityPinId = gravityPin->Id;
    const auto historicalProgram = Keire::CompileVfxEffect(historical, Keire::VfxBackend::Cpu);
    REQUIRE(historicalProgram.Valid);

    const auto migrated = Keire::MigrateVfxEffectToSchema4(historical);
    CHECK(historical.SchemaVersion == 3);
    CHECK(migrated.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    REQUIRE(migrated.Systems.size() == 1);
    const auto& system = migrated.Systems.front();
    CHECK(std::ranges::none_of(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Module || node.Kind == Keire::VfxGraphNodeKind::CustomHlsl; }));
    const auto blockCount =
        std::accumulate(system.Nodes.begin(), system.Nodes.end(), std::size_t{0},
                        [](const std::size_t count, const auto& node) { return count + node.Blocks.size(); });
    CHECK(blockCount == originalModuleCount);

    const auto update = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != system.Nodes.end());
    const auto forceBlock = std::ranges::find(update->Blocks, forceBlockId, &Keire::VfxGraphBlock::Id);
    REQUIRE(forceBlock != update->Blocks.end());
    CHECK(forceBlock->Reference == Id(6));
    CHECK(std::ranges::find(forceBlock->Pins, gravityPinId, &Keire::VfxGraphPin::Id) != forceBlock->Pins.end());

    const auto binding = std::ranges::find(system.Connections, Id(2'503), &Keire::VfxGraphConnection::Id);
    REQUIRE(binding != system.Connections.end());
    CHECK(binding->InputNode == update->Id);
    CHECK(binding->InputBlock == forceBlockId);
    CHECK(binding->InputPin == gravityPinId);

    Keire::ValidateVfxEffect(migrated);
    const auto migratedProgram = Keire::CompileVfxEffect(migrated, Keire::VfxBackend::Cpu);
    REQUIRE(migratedProgram.Valid);
    CHECK(migratedProgram.Hash == historicalProgram.Hash);
    CHECK(migratedProgram.CanonicalIr == historicalProgram.CanonicalIr);
    CHECK(Keire::MigrateVfxEffectToSchema4(migrated) == migrated);
}

TEST_CASE("VFX schema four rejects unknown type IDs and malformed typed fields")
{
    auto definition = EffectDefinition();
    Keire::Matrix4 matrix;
    definition.Systems = {{.Id = Id(2300),
                           .Name = "Malformed Schema Probe",
                           .Nodes = {{.Id = Id(2301),
                                      .Type = "Source",
                                      .Context = Keire::VfxContextType::Spawn,
                                      .Pins = {{Id(2302), "Value", Keire::VfxValueType::Matrix, false}},
                                      .Kind = Keire::VfxGraphNodeKind::Context,
                                      .TypeId = {"keire.context.spawn"},
                                      .Properties = {{"Strength", 1.0F}}},
                                     {.Id = Id(2303),
                                      .Type = "Sink",
                                      .Context = Keire::VfxContextType::Output,
                                      .Pins = {{Id(2304), "Value", Keire::VfxValueType::Matrix, true, "value", matrix}},
                                      .Kind = Keire::VfxGraphNodeKind::Context,
                                      .TypeId = {"keire.context.output"}}},
                           .Connections = {{Id(2305), Id(2301), Id(2302), Id(2303), Id(2304)}}}};
    const auto encoded = Keire::VfxEffectAsset::Encode(definition);
    const auto valid = nlohmann::json::parse(reinterpret_cast<const char*>(encoded.data()),
                                             reinterpret_cast<const char*>(encoded.data() + encoded.size()));

    auto unknownProperty = valid;
    unknownProperty["systems"][0]["nodes"][0]["properties"][0]["type"] = "unknown";
    CHECK_THROWS_AS((void)Keire::VfxEffectAsset::Decode(Bytes(unknownProperty.dump())), std::runtime_error);

    auto malformedMatrix = valid;
    malformedMatrix["systems"][0]["nodes"][1]["pins"][0]["default"].erase(15);
    CHECK_THROWS_WITH_AS((void)Keire::VfxEffectAsset::Decode(Bytes(malformedMatrix.dump())),
                         "VFX matrix values must contain exactly sixteen scalars.", std::runtime_error);

    auto missingEndpoint = valid;
    missingEndpoint["systems"][0]["connections"][0].erase("inputBlock");
    CHECK_THROWS_WITH_AS((void)Keire::VfxEffectAsset::Decode(Bytes(missingEndpoint.dump())),
                         "VFX schema-four connection block endpoints are required.", std::runtime_error);

    auto unknownTypeId = valid;
    unknownTypeId["executionSource"] = "graph";
    unknownTypeId["systems"][0]["nodes"][0]["kind"] = "operator";
    unknownTypeId["systems"][0]["nodes"][0]["typeId"] = "keire.operator.unknown-schema-probe";
    CHECK_THROWS_AS((void)Keire::VfxEffectAsset::Decode(Bytes(unknownTypeId.dump())), std::invalid_argument);
}

TEST_CASE("Default VFX effects expose a connected authoring context graph")
{
    const auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    CHECK(definition.SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(definition.ExecutionSource == Keire::VfxExecutionSource::Graph);
    REQUIRE(definition.Systems.size() == 1);
    const auto& system = definition.Systems.front();
    CHECK(system.Nodes.size() == 4);
    CHECK(system.Connections.size() == 3);
    CHECK(std::ranges::count(system.Nodes, Keire::VfxGraphNodeKind::Context, &Keire::VfxGraphNode::Kind) == 4);
    CHECK(std::ranges::count(system.Nodes, Keire::VfxGraphNodeKind::Module, &Keire::VfxGraphNode::Kind) == 0);
    std::size_t blockCount = 0;
    for (const auto& node : system.Nodes)
        blockCount += node.Blocks.size();
    CHECK(blockCount == definition.Modules.size());

    const auto cpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    const auto gpu = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
    CHECK(cpu.Valid);
    CHECK(gpu.Valid);
    CHECK(cpu.Modules.size() == definition.Modules.size());
    CHECK(cpu.CanonicalIr == gpu.CanonicalIr);
}

TEST_CASE("VFX legacy conversion produces deterministic executable schema-four topology")
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
                            Id(200),
                            {"keire.parameter"}});

    const auto forceContext = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return std::ranges::find(node.Blocks, Id(6), &Keire::VfxGraphBlock::Reference) != node.Blocks.end(); });
    REQUIRE(forceContext != system.Nodes.end());
    const auto forceBlock = std::ranges::find(forceContext->Blocks, Id(6), &Keire::VfxGraphBlock::Reference);
    REQUIRE(forceBlock != forceContext->Blocks.end());
    const auto gravityPin =
        std::ranges::find(forceBlock->Pins, std::string("gravityMultiplier"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(gravityPin != forceBlock->Pins.end());
    const auto forceNodeId = forceContext->Id;
    const auto forceBlockId = forceBlock->Id;
    const auto gravityPinId = gravityPin->Id;
    Keire::VfxGraphConnection gravityConnection;
    gravityConnection.Id = Id(203);
    gravityConnection.OutputNode = Id(201);
    gravityConnection.OutputPin = Id(202);
    gravityConnection.InputNode = forceNodeId;
    gravityConnection.InputPin = gravityPinId;
    gravityConnection.InputBlock = forceBlockId;
    system.Connections.push_back(gravityConnection);

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
         {},
         {"keire.operator.portable-hlsl"}});
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
    Keire::VfxGraphConnection duplicateConnection;
    duplicateConnection.Id = Id(208);
    duplicateConnection.OutputNode = Id(201);
    duplicateConnection.OutputPin = Id(202);
    duplicateConnection.InputNode = forceNodeId;
    duplicateConnection.InputBlock = forceBlockId;
    duplicateConnection.InputPin = gravityPinId;
    duplicateDriver.Systems.front().Connections.push_back(duplicateConnection);
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
    renderer.Material = Id(107);

    Keire::VfxGraphNode context;
    context.Id = Id(300);
    context.TypeId.Value = "keire.context.update";
    context.Type = "Dependency Context";
    context.Kind = Keire::VfxGraphNodeKind::Context;
    context.Properties = {{"Node Asset", Id(101)}};
    context.Pins = {{Id(301), "Texture", Keire::VfxValueType::Texture, true, "texture", Id(102)}};
    context.Blocks = {{.Id = Id(302),
                       .TypeId = {"keire.block.force"},
                       .Type = "Dependency Block",
                       .Pins = {{Id(303), "Buffer", Keire::VfxValueType::Buffer, true, "buffer", Id(103)}},
                       .Properties = {{"Block Asset", Id(104)}}}};
    Keire::VfxGraphNode subgraph;
    subgraph.Id = Id(304);
    subgraph.TypeId.Value = "keire.subgraph.dependency";
    subgraph.Type = "Dependency Subgraph";
    subgraph.Kind = Keire::VfxGraphNodeKind::Subgraph;
    subgraph.Reference = Id(105);
    definition.Systems = {{.Id = Id(305), .Name = "Dependencies", .Nodes = {context, subgraph}}};
    definition.Blackboard.push_back({Id(306), "Volume Texture", Keire::VfxValueType::Texture3D, Id(106), true});

    const auto dependencies = Keire::VfxEffectDependencies(definition);
    CHECK(dependencies ==
          std::vector<Keire::AssetId>{Id(99), Id(100), Id(101), Id(102), Id(103), Id(104), Id(105), Id(106), Id(107)});

    const auto source = Keire::VfxEffectAsset::Encode(definition);
    const auto importer = Keire::CreateVfxEffectAssetImporter();
    CHECK(importer.Name == "Keire.VfxEffect");
    CHECK(importer.Version == 5);
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

    const auto checkpoint = first->CaptureCheckpoint();
    const auto checkpointRender = first->CaptureRenderSnapshot();
    first->Update(0.25F);
    CHECK(first->CaptureDebugSnapshot().Revision != firstSnapshot.Revision);
    first->RestoreCheckpoint(checkpoint);
    const auto restoredRender = first->CaptureRenderSnapshot();
    CHECK(restoredRender.Revision() == checkpointRender.Revision());
    CHECK(std::ranges::equal(restoredRender.Particles(), checkpointRender.Particles()));
    auto corruptCheckpoint = checkpoint;
    corruptCheckpoint.pop_back();
    CHECK_THROWS_AS(first->RestoreCheckpoint(corruptCheckpoint), std::runtime_error);
    CHECK(std::ranges::equal(first->CaptureRenderSnapshot().Particles(), checkpointRender.Particles()));

    first->Update(0.25F);
    second->Update(0.25F);
    CHECK(std::ranges::equal(first->CaptureRenderSnapshot().Particles(), second->CaptureRenderSnapshot().Particles()));
}

TEST_CASE("schema-4 compiles, serializes, and owns multiple particle systems transactionally")
{
    const auto definition = MultiSystemEventEffect();
    const auto cpu = Keire::CompileVfxEffectSystems(definition, Keire::VfxBackend::Cpu);
    const auto gpu = Keire::CompileVfxEffectSystems(definition, Keire::VfxBackend::Gpu);
    REQUIRE(cpu.size() == 2);
    REQUIRE(gpu.size() == 2);
    CHECK(std::ranges::all_of(cpu, &Keire::VfxCompiledProgram::Valid));
    CHECK(std::ranges::all_of(gpu, &Keire::VfxCompiledProgram::Valid));
    CHECK(cpu[0].System == definition.Systems[0].Id);
    CHECK(cpu[1].System == definition.Systems[1].Id);
    CHECK(cpu[1].EventName == "Impact");
    CHECK(cpu[1].DataType == Keire::VfxParticleDataType::ParticleStrip);
    CHECK(cpu[1].ParticlesPerStrip == 4);

    const auto legacyEntryPoint = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    CHECK_FALSE(legacyEntryPoint.Valid);
    REQUIRE_FALSE(legacyEntryPoint.Diagnostics.empty());
    CHECK(legacyEntryPoint.Diagnostics.front().Message.find("CompileVfxEffectSystems") != std::string::npos);

    const auto decoded = Keire::VfxEffectAsset::Decode(Keire::VfxEffectAsset::Encode(definition));
    REQUIRE(decoded);
    REQUIRE(decoded->Definition().Systems.size() == 2);
    CHECK(decoded->Definition().Systems[1].DataType == Keire::VfxParticleDataType::ParticleStrip);
    CHECK(decoded->Definition().Systems[1].ParticlesPerStrip == 4);
}

TEST_CASE("one root VFX handle controls all systems and routes named events on CPU and GPU")
{
    const auto definition = MultiSystemEventEffect();
    for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
    {
        auto world = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
            .MaximumEffects = 1, .MaximumSystemsPerEffect = 2, .MaximumParticles = 64, .Backend = backend});
        const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
        const auto handle = world->Activate({effect});
        REQUIRE(handle);
        CHECK(world->Statistics().ActiveEffects == 1);
        CHECK_FALSE(world->Activate({effect}));
        CHECK(world->Statistics().DroppedEffects == 1);

        world->Update(0.0F);
        if (backend == Keire::VfxBackend::Cpu)
        {
            const auto before = world->CaptureDebugSnapshot();
            const auto eventBefore =
                std::ranges::find(before.Effects, definition.Systems[1].Id, &Keire::VfxDebugEffect::System);
            REQUIRE(eventBefore != before.Effects.end());
            CHECK(eventBefore->ActiveParticles == 0);
        }
        else
        {
            const auto before = world->CaptureRenderSnapshot();
            const auto eventBefore =
                std::ranges::find(before.GpuEmitters(), definition.Systems[1].Id, &Keire::VfxGpuEmitter::System);
            REQUIRE(eventBefore != before.GpuEmitters().end());
            CHECK(eventBefore->SpawnSequence == 0);
        }

        CHECK_FALSE(world->SendEvent(handle, "Missing", 3));
        REQUIRE(world->SendEvent(handle, "Impact", 3));
        world->Update(0.01F);
        if (backend == Keire::VfxBackend::Cpu)
        {
            const auto after = world->CaptureDebugSnapshot();
            const auto eventAfter =
                std::ranges::find(after.Effects, definition.Systems[1].Id, &Keire::VfxDebugEffect::System);
            REQUIRE(eventAfter != after.Effects.end());
            CHECK(eventAfter->ActiveParticles == 3);
            const auto renderSnapshot = world->CaptureRenderSnapshot();
            const auto particles = renderSnapshot.Particles();
            CHECK(std::ranges::count(particles, definition.Systems[1].Id, &Keire::VfxRenderParticle::System) == 3);
            CHECK(std::ranges::all_of(particles,
                                      [&definition](const Keire::VfxRenderParticle& particle)
                                      {
                                          return particle.System != definition.Systems[1].Id ||
                                                 (particle.ParticleIndexInStrip < 4 &&
                                                  particle.StripId == particle.ParticleIndexInStrip / 4);
                                      }));
        }
        else
        {
            const auto after = world->CaptureRenderSnapshot();
            const auto eventAfter =
                std::ranges::find(after.GpuEmitters(), definition.Systems[1].Id, &Keire::VfxGpuEmitter::System);
            REQUIRE(eventAfter != after.GpuEmitters().end());
            CHECK(eventAfter->SpawnSequence == 3);
            CHECK(eventAfter->DataType == Keire::VfxParticleDataType::ParticleStrip);
            CHECK(eventAfter->ParticlesPerStrip == 4);
        }

        world->Stop(handle);
        CHECK_FALSE(world->IsAlive(handle));
        CHECK(world->Statistics().ActiveEffects == 0);
    }
}

TEST_CASE("duplicate Blocks retain independent execution IDs and bindings on CPU and GPU")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    Keire::VfxModuleDefinition force{Keire::AssetId::Generate(), true, Keire::VfxForceModule{}};
    auto first = Keire::CreateVfxGraphBlock(force);
    auto second = first;
    second.Id = Keire::AssetId::Generate();
    for (auto& pin : second.Pins)
        pin.Id = Keire::AssetId::Generate();
    const auto setForce = [](Keire::VfxGraphBlock& block, const Keire::Vector3 value)
    {
        const auto pin = std::ranges::find(block.Pins, std::string("force"), &Keire::VfxGraphPin::Semantic);
        REQUIRE(pin != block.Pins.end());
        pin->DefaultValue = value;
    };
    setForce(first, {1.0F, 0.0F, 0.0F});
    setForce(second, {0.0F, 2.0F, 0.0F});
    const auto firstId = first.Id;
    const auto secondId = second.Id;
    definition.Modules.push_back(force);
    const auto update = std::ranges::find_if(definition.Systems.front().Nodes, [](const Keire::VfxGraphNode& node)
                                             { return node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != definition.Systems.front().Nodes.end());
    update->Blocks.push_back(std::move(first));
    update->Blocks.push_back(std::move(second));

    for (const auto backend : {Keire::VfxBackend::Cpu, Keire::VfxBackend::Gpu})
    {
        const auto program = Keire::CompileVfxEffect(definition, backend);
        REQUIRE(program.Valid);
        const auto firstCompiled = std::ranges::find(program.Modules, firstId, &Keire::VfxCompiledModule::Node);
        const auto secondCompiled = std::ranges::find(program.Modules, secondId, &Keire::VfxCompiledModule::Node);
        REQUIRE(firstCompiled != program.Modules.end());
        REQUIRE(secondCompiled != program.Modules.end());
        CHECK(firstCompiled->Module == force.Id);
        CHECK(secondCompiled->Module == force.Id);
        CHECK(std::ranges::count(program.Bindings, firstId, &Keire::VfxCompiledBinding::Node) == 1);
        CHECK(std::ranges::count(program.Bindings, secondId, &Keire::VfxCompiledBinding::Node) == 1);
    }

    auto gpuWorld = Keire::CreateRef<Keire::VfxWorld>(
        Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
    REQUIRE(gpuWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
    gpuWorld->Update(0.1F);
    const auto snapshot = gpuWorld->CaptureRenderSnapshot();
    REQUIRE(snapshot.GpuEmitters().size() == 1);
    REQUIRE(snapshot.GpuEmitters().front().Execution);
    CHECK(std::ranges::count(snapshot.GpuEmitters().front().Execution->ParticleOperations,
                             Keire::VfxGpuParticleOperationKind::Force, &Keire::VfxGpuParticleOperation::Kind) == 2);
}

TEST_CASE("Ribbon and Volumetric outputs compile and publish executable CPU and GPU render data")
{
    for (const auto rendererType : {Keire::VfxRendererType::Ribbon, Keire::VfxRendererType::Volumetric})
    {
        auto definition = Keire::VfxEffectAsset::DefaultDefinition();
        const auto renderer =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxRendererModule>(module.Payload); });
        REQUIRE(renderer != definition.Modules.end());
        std::get<Keire::VfxRendererModule>(renderer->Payload).Type = rendererType;
        if (rendererType == Keire::VfxRendererType::Ribbon)
        {
            definition.Systems.front().DataType = Keire::VfxParticleDataType::ParticleStrip;
            definition.Systems.front().ParticlesPerStrip = 8;
        }

        const auto cpuProgram = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
        const auto gpuProgram = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Gpu);
        REQUIRE(cpuProgram.Valid);
        REQUIRE(gpuProgram.Valid);

        auto cpuWorld = Keire::CreateRef<Keire::VfxWorld>(
            Keire::VfxWorldSpecification{.MaximumEffects = 1, .MaximumParticles = 32});
        REQUIRE(cpuWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
        cpuWorld->Update(0.2F);
        cpuWorld->Update(0.2F);
        const auto cpuSnapshot = cpuWorld->CaptureRenderSnapshot();
        REQUIRE_FALSE(cpuSnapshot.Particles().empty());
        CHECK(std::ranges::all_of(cpuSnapshot.Particles(), [rendererType](const Keire::VfxRenderParticle& particle)
                                  { return particle.Renderer == rendererType; }));
        if (rendererType == Keire::VfxRendererType::Ribbon)
        {
            CHECK(std::ranges::all_of(cpuSnapshot.Particles(), [](const Keire::VfxRenderParticle& particle)
                                      { return particle.ParticleIndexInStrip < 8; }));
            CHECK(std::ranges::any_of(cpuSnapshot.Particles(), [](const Keire::VfxRenderParticle& particle)
                                      { return particle.PreviousPosition != particle.Position; }));
        }

        auto gpuWorld = Keire::CreateRef<Keire::VfxWorld>(Keire::VfxWorldSpecification{
            .MaximumEffects = 1, .MaximumParticles = 32, .Backend = Keire::VfxBackend::Gpu});
        REQUIRE(gpuWorld->Activate({Keire::CreateRef<Keire::VfxEffectAsset>(definition)}));
        gpuWorld->Update(0.2F);
        const auto gpuSnapshot = gpuWorld->CaptureRenderSnapshot();
        REQUIRE(gpuSnapshot.GpuEmitters().size() == 1);
        CHECK(gpuSnapshot.GpuEmitters().front().Renderer == rendererType);
        CHECK(gpuSnapshot.GpuEmitters().front().DataType == definition.Systems.front().DataType);
        CHECK(gpuSnapshot.GpuEmitters().front().ParticlesPerStrip ==
              (rendererType == Keire::VfxRendererType::Ribbon ? 8 : 1));
    }
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
    CHECK_FALSE(probe.Statistics.VfxPipelineWarmupPending);
    CHECK_FALSE(probe.Statistics.VfxPipelinesReady);
    CHECK(probe.Statistics.VfxPipelineWarmupMilliseconds == 0.0F);
    CHECK(probe.NonOwnerWarmupRejected);
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

TEST_CASE("GPU live Force parameters preserve a draining non-looping effect")
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
    const auto updated = world->CaptureRenderSnapshot();
    REQUIRE(updated.GpuEmitters().size() == 1);
    CHECK(updated.GpuEmitters().front().SimulationRevision == simulationRevision);
    CHECK(updated.GpuEmitters().front().SpawnSequence == before.GpuEmitters().front().SpawnSequence);
    CHECK(updated.GpuEmitters().front().Acceleration.Y ==
          doctest::Approx(before.GpuEmitters().front().Acceleration.Y + 1.0F));
    debug = world->CaptureDebugSnapshot();
    REQUIRE(debug.EffectCount == 1);
    CHECK(debug.Effects[0].ElapsedSeconds == doctest::Approx(0.25F));
    CHECK(debug.Effects[0].ActiveParticles > 0);
    CHECK_FALSE(debug.Effects[0].Emitting);

    world->Update(0.25F);
    const auto draining = world->CaptureRenderSnapshot();
    REQUIRE(draining.GpuEmitters().size() == 1);
    CHECK(draining.GpuEmitters().front().SpawnSequence == before.GpuEmitters().front().SpawnSequence);
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

    auto& mutableEmitter = dynamic_cast<Keire::VfxEmitterComponent&>(*component);
    CHECK_THROWS_WITH_AS(mutableEmitter.SetParameterOverride({Id(101), Keire::VfxScalarRange{2.0F, 1.0F}}),
                         "VFX Emitter parameter override is invalid.", std::invalid_argument);

    auto malformedValues = registration.Serialize(*component);
    const nlohmann::json malformedOverrides =
        nlohmann::json::array({{{"parameter", Id(102).ToString()},
                                {"kind", "vector3Range"},
                                {"value", nlohmann::json::array({nlohmann::json::array({0.0F, 1.0F, 2.0F, 3.0F}),
                                                                 nlohmann::json::array({4.0F, 5.0F, 6.0F})})}}});
    malformedValues["parameterOverrides"] = malformedOverrides.dump();
    CHECK_THROWS_AS(registration.Deserialize(*component, malformedValues, 2), std::invalid_argument);
    CHECK(mutableEmitter.ParameterOverrides().empty());
}

TEST_CASE("VFX volume assets round trip weighted cells and sample deterministically")
{
    Keire::VfxVolumeDefinition definition;
    definition.Cells = {{{-2.0F, -1.0F, -0.5F}, {-1.0F, 1.0F, 0.5F}, 2.0F},
                        {{1.0F, 2.0F, 3.0F}, {3.0F, 4.0F, 5.0F}, 0.5F}};
    const auto encoded = Keire::VfxVolumeAsset::Encode(definition);
    const auto decoded = Keire::VfxVolumeAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->Definition() == definition);
    CHECK(decoded->CumulativeWeights().size() == definition.Cells.size());
    CHECK(decoded->TotalWeight() == doctest::Approx(8.0F));

    for (std::uint32_t random = 1; random < 1000; ++random)
    {
        const auto sample = decoded->Sample(random);
        const auto inFirst = sample.X >= -2.0F && sample.X <= -1.0F && sample.Y >= -1.0F && sample.Y <= 1.0F &&
                             sample.Z >= -0.5F && sample.Z <= 0.5F;
        const auto inSecond = sample.X >= 1.0F && sample.X <= 3.0F && sample.Y >= 2.0F && sample.Y <= 4.0F &&
                              sample.Z >= 3.0F && sample.Z <= 5.0F;
        CHECK((inFirst || inSecond));
        CHECK(sample == decoded->Sample(random));
    }

    auto invalid = definition;
    invalid.Cells.front().Maximum.X = invalid.Cells.front().Minimum.X;
    CHECK_THROWS_WITH_AS((void)Keire::VfxVolumeAsset::Encode(invalid), "VFX volume contains an invalid density cell.",
                         std::invalid_argument);
    const auto importer = Keire::CreateVfxVolumeAssetImporter();
    CHECK(importer.Type == Keire::VfxVolumeAsset::StaticType());
    CHECK(importer.Extensions == std::vector<std::string>{".keirevfxvolume"});
}
