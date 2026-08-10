#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace
{
    class ReloadableComponent final : public Keire::Component
    {
      public:
        ReloadableComponent() : Component(StaticType()) {}

        [[nodiscard]] static constexpr Keire::ComponentTypeId StaticType() noexcept
        {
            return Keire::ComponentTypeId(Keire::AssetId(0x72656c6f61646162ULL, 0x6c652d7479706501ULL));
        }
    };

    [[nodiscard]] Keire::ComponentRegistration Registration(std::string name)
    {
        Keire::ComponentRegistration result;
        result.Type = ReloadableComponent::StaticType();
        result.Name = std::move(name);
        result.Factory = [] { return Keire::Ref<Keire::Component>(Keire::CreateRef<ReloadableComponent>()); };
        result.Serialize = [](const Keire::Component&) { return Keire::ComponentPropertyBag{}; };
        result.Deserialize = [](Keire::Component&, const Keire::ComponentPropertyBag&, std::uint32_t) {};
        return result;
    }
} // namespace

TEST_CASE("Profiler records bounded cross-thread spans and counters")
{
    Keire::ProfilerSpecification specification;
    specification.Mode = Keire::ProfilerMode::Enabled;
    specification.MaximumSpansPerFrame = 2;
    specification.MaximumCountersPerFrame = 1;
    auto profiler = Keire::CreateRef<Keire::Profiler>(specification);

    profiler->BeginFrame();
    {
        Keire::ProfileScope mainScope(profiler, Keire::ProfileCategory::Application, "Main");
    }
    std::thread worker([profiler]
                       { Keire::ProfileScope workerScope(profiler, Keire::ProfileCategory::User, "Worker"); });
    worker.join();
    profiler->RecordSpan(Keire::ProfileCategory::User, "Overflow", 1.0, 1.0);
    profiler->SetCounter(Keire::ProfileCategory::Application, "Frame", 1.0);
    profiler->SetCounter(Keire::ProfileCategory::Application, "Frame", 2.0);
    profiler->SetCounter(Keire::ProfileCategory::Audio, "Overflow", 1.0);
    profiler->EndFrame();

    const auto frame = profiler->LatestFrame();
    CHECK(frame.Sequence == 1);
    CHECK(frame.Spans.size() == 2);
    REQUIRE(frame.Counters.size() == 1);
    CHECK(frame.Counters.front().Value == 2.0);
    CHECK(frame.Truncated);
    CHECK(frame.DroppedSpans == 1);
    CHECK(frame.DroppedCounters == 1);
    const auto summary = profiler->LatestSummary();
    CHECK(summary.Sequence == frame.Sequence);
    CHECK(summary.SpanCount == frame.Spans.size());
    CHECK(summary.CounterCount == frame.Counters.size());
    CHECK(summary.DroppedSpans == 1);
    CHECK(summary.DroppedCounters == 1);
    const auto trace = profiler->LatestChromeTrace();
    CHECK(trace.find("\"ph\":\"X\"") != std::string::npos);
    CHECK(trace.find("\"name\":\"Worker\"") != std::string::npos);
    CHECK(trace.find("\"ph\":\"C\"") != std::string::npos);

    profiler->Close();
    CHECK_FALSE(profiler->IsOpen());
    CHECK_NOTHROW(profiler->Close());
}

TEST_CASE("Profiler retains bounded frame summaries in chronological order")
{
    Keire::ProfilerSpecification specification;
    specification.Mode = Keire::ProfilerMode::Enabled;
    specification.MaximumRetainedFrameSummaries = 2;
    auto profiler = Keire::CreateRef<Keire::Profiler>(specification);

    for (std::uint64_t frame = 1; frame <= 3; ++frame)
    {
        profiler->BeginFrame();
        profiler->RecordSpan(Keire::ProfileCategory::Assets, "Asset work", 0.0, static_cast<double>(frame));
        profiler->RecordSpan(Keire::ProfileCategory::User, "Editor UI", 0.0, static_cast<double>(frame * 2));
        profiler->SetCounter(Keire::ProfileCategory::Application, "Frame", static_cast<double>(frame));
        profiler->EndFrame();
    }

    const auto summaries = profiler->RecentSummaries(10);
    REQUIRE(summaries.size() == 2);
    CHECK(summaries[0].Sequence == 2);
    CHECK(summaries[1].Sequence == 3);
    CHECK(summaries[1].DurationMicroseconds >= 0.0);
    CHECK(summaries[1].AssetsMicroseconds == doctest::Approx(3.0));
    CHECK(summaries[1].UserMicroseconds == doctest::Approx(6.0));
    CHECK(profiler->RecentSummaries(1).front().Sequence == 3);
}

TEST_CASE("Profiler owner-thread rejections leave recording state unchanged")
{
    Keire::ProfilerSpecification specification;
    specification.Mode = Keire::ProfilerMode::Enabled;
    auto profiler = Keire::CreateRef<Keire::Profiler>(specification);
    std::atomic<bool> rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                profiler->BeginFrame();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected.load());
    CHECK_NOTHROW(profiler->BeginFrame());
    CHECK_NOTHROW(profiler->EndFrame());
}

TEST_CASE("Runtime service handles close idempotently and reject worker teardown")
{
    Keire::PhysicsSystemSpecification physicsSpecification;
    physicsSpecification.Mode = Keire::PhysicsMode::Enabled;
    auto physics = Keire::CreateRef<Keire::PhysicsSystem>(physicsSpecification);
    std::atomic<bool> rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                physics->Close();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected.load());
    CHECK(physics->IsOpen());
    CHECK_NOTHROW(physics->Close());
    CHECK_NOTHROW(physics->Close());
    CHECK_FALSE(physics->IsOpen());

    CHECK_THROWS_AS(Keire::CreateRef<Keire::ScriptSystem>(Keire::ScriptSystemSpecification{}), std::invalid_argument);
    CHECK_THROWS_AS(Keire::CreateRef<Keire::AudioSystem>(Keire::AudioSystemSpecification{}), std::invalid_argument);
    CHECK_THROWS_AS(Keire::CreateRef<Keire::NavigationSystem>(Keire::NavigationSystemSpecification{}),
                    std::invalid_argument);
}

TEST_CASE("Physics worlds validate bodies and produce deterministic queries and contacts")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto world = system->CreateWorld();

    Keire::PhysicsBodyDefinition floor;
    floor.Position = {0.0F, -1.0F, 0.0F};
    floor.HalfExtent = {5.0F, 0.5F, 5.0F};
    const auto floorBody = world->CreateBody(floor);

    Keire::PhysicsBodyDefinition trigger;
    trigger.Shape = Keire::ColliderShape::Sphere;
    trigger.Position = {0.0F, 0.0F, 0.0F};
    trigger.Trigger = true;
    const auto triggerBody = world->CreateBody(trigger);

    const auto hits = world->RayCast({.Origin = {0.0F, 2.0F, 0.0F}, .Direction = {0.0F, -1.0F, 0.0F}});
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].Body == triggerBody);
    CHECK(hits[1].Body == floorBody);

    world->Step(1.0F / 60.0F);
    auto contacts = world->DrainContactEvents();
    REQUIRE(contacts.size() == 1);
    CHECK(contacts[0].Phase == Keire::ContactPhase::Enter);
    CHECK(contacts[0].Trigger);

    world->Step(1.0F / 60.0F);
    contacts = world->DrainContactEvents();
    REQUIRE(contacts.size() == 1);
    CHECK(contacts[0].Phase == Keire::ContactPhase::Stay);

    system->Close();
    CHECK_FALSE(world->IsOpen());
    CHECK_THROWS_AS(world->Step(1.0F / 60.0F), std::logic_error);
}

TEST_CASE("Physics body rejection is transactional")
{
    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto world = system->CreateWorld();
    Keire::PhysicsBodyDefinition invalid;
    invalid.HalfExtent.X = 0.0F;
    CHECK_THROWS_AS((void)world->CreateBody(invalid), std::invalid_argument);
    CHECK(world->OverlapSphere({}, 10.0F).empty());
}

TEST_CASE("Jolt collision cooking is deterministic and rejects dynamic triangle meshes transactionally")
{
    Keire::CollisionCookInput convex;
    convex.Kind = Keire::CollisionMeshKind::Convex;
    convex.Vertices = {
        {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const auto first = Keire::CookCollisionMesh(convex);
    const auto second = Keire::CookCollisionMesh(convex);
    REQUIRE(first);
    CHECK(first->ContentHash == second->ContentHash);
    CHECK(first->Vertices.size() == 4);

    Keire::CollisionCookInput triangles;
    triangles.Kind = Keire::CollisionMeshKind::Triangle;
    triangles.Vertices = {{-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F}};
    triangles.Indices = {0, 2, 1};
    const auto triangleMesh = Keire::CookCollisionMesh(triangles);

    Keire::PhysicsSystemSpecification specification;
    specification.Mode = Keire::PhysicsMode::Enabled;
    auto system = Keire::CreateRef<Keire::PhysicsSystem>(specification);
    auto world = system->CreateWorld();
    Keire::PhysicsBodyDefinition invalid;
    invalid.Motion = Keire::PhysicsMotionType::Dynamic;
    invalid.Shape = Keire::ColliderShape::TriangleMesh;
    invalid.Collision = triangleMesh;
    CHECK_THROWS_WITH_AS((void)world->CreateBody(invalid),
                         "Dynamic triangle-mesh collision is unsupported; use a convex collider.",
                         std::invalid_argument);
    CHECK(world->OverlapSphere({}, 10.0F).empty());
}

TEST_CASE("Audio graph snapshots reject zero-delay cycles and publish atomically")
{
    Keire::AudioGraphSnapshot graph;
    graph.Revision = 7;
    graph.Output = Keire::AudioGraphNodeId(3);
    graph.Nodes = {{Keire::AudioGraphNodeId(1), "Input", Keire::AudioGraphNodeType::Input, {}, {}},
                   {Keire::AudioGraphNodeId(2),
                    "Gain",
                    Keire::AudioGraphNodeType::Gain,
                    {{Keire::AudioGraphNodeId(1), false}},
                    {0.5F}},
                   {Keire::AudioGraphNodeId(3),
                    "Output",
                    Keire::AudioGraphNodeType::Output,
                    {{Keire::AudioGraphNodeId(2), false}},
                    {}}};
    CHECK_NOTHROW(Keire::ValidateAudioGraph(graph));

    Keire::AudioSystemSpecification specification;
    specification.Mode = Keire::AudioMode::Headless;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(specification);
    audio->SubmitGraph(std::make_shared<const Keire::AudioGraphSnapshot>(graph));
    CHECK(audio->GraphRevision() == 7);
    REQUIRE(audio->CurrentGraph());
    CHECK(audio->CurrentGraph()->Nodes.size() == 3);

    graph.Revision = 8;
    graph.Nodes[1].Inputs.push_back({Keire::AudioGraphNodeId(3), false});
    CHECK_THROWS_WITH_AS(Keire::ValidateAudioGraph(graph), "Audio graph contains a zero-delay cycle.",
                         std::invalid_argument);
    CHECK(audio->GraphRevision() == 7);

    graph.Nodes[1].Type = Keire::AudioGraphNodeType::Delay;
    graph.Nodes[1].Inputs.back().DelayedFeedback = true;
    CHECK_NOTHROW(Keire::ValidateAudioGraph(graph));
    audio->SubmitGraph(std::make_shared<const Keire::AudioGraphSnapshot>(graph));
    CHECK(audio->GraphRevision() == 8);

    graph.Revision = 9;
    graph.Nodes[1].Type = Keire::AudioGraphNodeType::Gain;
    graph.Nodes[1].Inputs.pop_back();
    audio->SubmitGraph(std::make_shared<const Keire::AudioGraphSnapshot>(graph));
    const std::array input{1.0F, -1.0F, 0.5F, -0.5F, 0.25F, -0.25F, 0.0F, 0.0F};
    const auto rendered = audio->RenderOffline(input, 4);
    REQUIRE(rendered.size() == input.size());
    for (std::size_t index = 0; index < input.size(); ++index)
        CHECK(rendered[index] == doctest::Approx(input[index] * 0.5F));

    audio->Close();
    CHECK_FALSE(audio->IsOpen());
    CHECK_THROWS_AS((void)audio->RenderOffline(input, 4), std::logic_error);
}

TEST_CASE("Algorithmic reverb preserves dry input and produces a bounded deterministic tail")
{
    Keire::AudioGraphSnapshot graph;
    graph.Revision = 1;
    graph.SampleRate = 8000;
    graph.Channels = 1;
    graph.Output = Keire::AudioGraphNodeId(3);
    graph.Nodes = {{Keire::AudioGraphNodeId(1), "Input", Keire::AudioGraphNodeType::Input, {}, {}},
                   {Keire::AudioGraphNodeId(2),
                    "Room",
                    Keire::AudioGraphNodeType::AlgorithmicReverb,
                    {{Keire::AudioGraphNodeId(1), false}},
                    {10.0F, 0.6F, 0.5F}},
                   {Keire::AudioGraphNodeId(3),
                    "Output",
                    Keire::AudioGraphNodeType::Output,
                    {{Keire::AudioGraphNodeId(2), false}},
                    {}}};
    Keire::AudioSystemSpecification specification;
    specification.Mode = Keire::AudioMode::Headless;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(specification);
    audio->SubmitGraph(std::make_shared<const Keire::AudioGraphSnapshot>(graph));
    std::vector<float> impulse(256, 0.0F);
    impulse.front() = 1.0F;
    const auto first = audio->RenderOffline(impulse, impulse.size());
    const auto second = audio->RenderOffline(impulse, impulse.size());
    REQUIRE(first.size() == impulse.size());
    CHECK(first == second);
    CHECK(first.front() == doctest::Approx(0.5F));
    bool hasTail = false;
    for (std::size_t index = 20; index < first.size(); ++index)
        hasTail = hasTail || std::abs(first[index]) > 0.001F;
    CHECK(hasTail);
    for (const auto sample : first)
        CHECK(std::isfinite(sample));
    audio->Close();
}

TEST_CASE("Audio voices spatialize, virtualize, loop, and interpolate mixer snapshots")
{
    Keire::AudioSystemSpecification specification;
    specification.Mode = Keire::AudioMode::Headless;
    specification.MaximumVoices = 1;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(specification);
    auto clip = std::make_shared<Keire::AudioClipData>();
    clip->SampleRate = 48000;
    clip->Channels = 1;
    clip->Samples = {1.0F, 1.0F, 1.0F, 1.0F};

    Keire::AudioVoiceSpecification quiet;
    quiet.Clip = clip;
    quiet.Loop = true;
    quiet.Spatial = true;
    quiet.Position = {-1.0F, 0.0F, 0.0F};
    quiet.Priority = 1;
    const auto quietVoice = audio->Play(quiet);
    auto important = quiet;
    important.Position = {1.0F, 0.0F, 0.0F};
    important.Priority = 2;
    const auto importantVoice = audio->Play(important);

    const auto voices = audio->Voices();
    REQUIRE(voices.size() == 2);
    CHECK(voices[0].Id == quietVoice);
    CHECK(voices[0].Virtualized);
    CHECK_FALSE(voices[1].Virtualized);
    const auto first = audio->RenderVoicesOffline(2);
    REQUIRE(first.size() == 4);
    CHECK(first[1] > first[0]);
    const auto beforeSnapshot = std::abs(first[0]) + std::abs(first[1]);

    audio->SubmitSnapshot(
        {.Revision = 1, .Transition = std::chrono::milliseconds(100), .BusGains = {{"Master", 0.0F}}});
    audio->Update(std::chrono::milliseconds(50));
    const auto faded = audio->RenderVoicesOffline(2);
    CHECK(std::abs(faded[0]) + std::abs(faded[1]) < beforeSnapshot);
    const auto statistics = audio->Statistics();
    CHECK(statistics.Voices == 2);
    CHECK(statistics.AudibleVoices == 1);
    CHECK(statistics.VirtualVoices == 1);
    CHECK(statistics.RenderedFrames == 4);
    CHECK(audio->Stop(importantVoice));
    CHECK_FALSE(audio->Stop(importantVoice));
    CHECK(audio->Statistics().AudibleVoices == 1);
    audio->Close();
}

TEST_CASE("Animation assets round trip and animator sampling is deterministic")
{
    const std::vector<Keire::SkeletonBone> bones{{"Root", -1, {}, {}},
                                                 {"Hand", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}};
    const auto skeleton = Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(bones));
    REQUIRE(skeleton->Bones().size() == 2);
    CHECK(skeleton->Bones()[1].Parent == 0);

    const auto skeletonId = Keire::AssetId::Generate();
    Keire::AnimationTrack rootTrack;
    rootTrack.Bone = 0;
    rootTrack.Keys = {{0.0F, {}}, {1.0F, {{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}}};
    const std::vector<Keire::AnimationEvent> events{{0.25F, "Footstep", "left"}};
    const auto clip = Keire::AnimationClipAsset::Decode(
        Keire::AnimationClipAsset::Encode(skeletonId, 1.0F, std::span(&rootTrack, 1), events, true));
    CHECK(clip->Duration() == doctest::Approx(1.0F));
    CHECK(clip->RootMotion());

    const auto clipId = Keire::AssetId::Generate();
    Keire::AnimationGraphDefinition graphDefinition;
    graphDefinition.EntryState = "Idle";
    graphDefinition.Parameters = {"Speed"};
    graphDefinition.States = {
        {"Idle",
         clipId,
         1.0F,
         true,
         {{"Run", 0.1F, false, 1.0F, {{"Speed", Keire::AnimationConditionComparison::Greater, 0.1F}}}}},
        {"Run", clipId, 1.0F, true, {}}};
    const auto graph = Keire::AnimationGraphAsset::Decode(Keire::AnimationGraphAsset::Encode(graphDefinition));
    Keire::AnimatorInstance animator(skeleton, graph, [clipId, clip](const Keire::AssetId id)
                                     { return id == clipId ? clip : Keire::Ref<Keire::AnimationClipAsset>{}; });
    animator.SetFloat("Speed", 1.0F);
    const auto sample = animator.Update(0.5F);
    CHECK(sample.State == "Run");
    CHECK(sample.NormalizedTime == doctest::Approx(0.5F));
    REQUIRE(sample.LocalPose.size() == 2);
    CHECK(sample.LocalPose[0].Translation == Keire::Vector3{});
    CHECK(sample.RootMotion.X == doctest::Approx(0.5F));
    REQUIRE(sample.Events.size() == 1);
    CHECK(sample.Events.front().Name == "Footstep");
}

TEST_CASE("Navigation worlds publish revisioned meshes and return deterministic paths")
{
    Keire::NavigationMeshSnapshot mesh;
    mesh.Revision = 4;
    mesh.Nodes = {{Keire::NavigationNodeId(1), {0.0F, 0.0F, 0.0F}, 1},
                  {Keire::NavigationNodeId(2), {1.0F, 0.0F, 0.0F}, 1},
                  {Keire::NavigationNodeId(3), {2.0F, 0.0F, 0.0F}, 1}};
    mesh.Edges = {{Keire::NavigationNodeId(1), Keire::NavigationNodeId(2), 1.0F, true},
                  {Keire::NavigationNodeId(2), Keire::NavigationNodeId(3), 1.0F, true}};
    CHECK_NOTHROW(Keire::ValidateNavigationMesh(mesh));

    Keire::NavigationSystemSpecification specification;
    specification.Mode = Keire::NavigationMode::Enabled;
    auto navigation = Keire::CreateRef<Keire::NavigationSystem>(specification);
    auto world = navigation->CreateWorld();
    world->PublishMesh(std::make_shared<const Keire::NavigationMeshSnapshot>(mesh));
    const auto path = world->FindPath({{0.1F, 0.0F, 0.0F}, {1.9F, 0.0F, 0.0F}});
    CHECK(path.State == Keire::NavigationPathState::Succeeded);
    CHECK(path.MeshRevision == 4);
    REQUIRE(path.Points.size() == 5);
    CHECK(path.Points.front() == Keire::Vector3{0.1F, 0.0F, 0.0F});
    CHECK(path.Points.back() == Keire::Vector3{1.9F, 0.0F, 0.0F});

    auto operation = world->FindPathAsync({{0.1F, 0.0F, 0.0F}, {1.9F, 0.0F, 0.0F}});
    CHECK(operation->WaitFor(std::chrono::seconds(1)));
    CHECK(operation->Complete());
    CHECK(operation->Result().State == Keire::NavigationPathState::Succeeded);

    const auto agent = world->CreateAgent({.Position = {0.0F, 0.0F, 0.0F},
                                           .Radius = 0.1F,
                                           .Height = 1.8F,
                                           .MaximumSpeed = 2.0F,
                                           .MaximumAcceleration = 10.0F});
    CHECK(world->SetAgentTarget(agent, {2.0F, 0.0F, 0.0F}));
    std::vector<Keire::NavigationAgentState> crowd;
    for (std::size_t step = 0; step < 30; ++step)
        crowd = world->StepCrowd(0.1F);
    REQUIRE(crowd.size() == 1);
    CHECK(crowd.front().Position.X == doctest::Approx(2.0F));
    CHECK(crowd.front().Status == Keire::NavigationAgentStatus::Arrived);
    CHECK(world->NearestPoint({1.1F, 0.0F, 0.0F}) == Keire::Vector3{1.0F, 0.0F, 0.0F});
    CHECK(world->Raycast({0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}).ReachedEnd);
    const auto obstacle = world->AddObstacle({{1.0F, 0.0F, 0.0F}, 0.25F, true});
    CHECK(world->FindPath({{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}}).State == Keire::NavigationPathState::Unreachable);
    CHECK(world->RemoveObstacle(obstacle));
    CHECK(world->DestroyAgent(agent));

    navigation->Close();
    CHECK_FALSE(world->IsOpen());
    CHECK_THROWS_AS((void)world->FindPath({}), std::logic_error);
}

TEST_CASE("Recast navigation bakes are deterministic and publishable")
{
    Keire::NavigationBakeInput input;
    input.Revision = 12;
    input.Vertices = {{-5.0F, 0.0F, -5.0F}, {5.0F, 0.0F, -5.0F}, {5.0F, 0.0F, 5.0F}, {-5.0F, 0.0F, 5.0F}};
    input.Indices = {0, 2, 1, 0, 3, 2};
    input.Settings.AgentRadius = 0.0F;
    input.Settings.RegionMinimumArea = 0.0F;
    input.Settings.RegionMergeArea = 0.0F;

    const auto first = Keire::BakeNavigationMesh(input);
    const auto second = Keire::BakeNavigationMesh(input);
    CHECK(first.DependencyHash != 0);
    CHECK(first.DependencyHash == second.DependencyHash);
    CHECK(first.Mesh.Revision == input.Revision);
    REQUIRE_FALSE(first.Mesh.Nodes.empty());
    REQUIRE(first.Mesh.Tiles.size() == 1);
    CHECK_FALSE(first.Mesh.Tiles.front().Bytes.empty());
    CHECK(first.Mesh.Tiles == second.Mesh.Tiles);
    CHECK(first.Mesh.Nodes == second.Mesh.Nodes);
    CHECK(first.Mesh.Edges == second.Mesh.Edges);

    Keire::NavigationSystemSpecification specification;
    specification.Mode = Keire::NavigationMode::Enabled;
    auto navigation = Keire::CreateRef<Keire::NavigationSystem>(specification);
    auto world = navigation->CreateWorld();
    world->PublishMesh(std::make_shared<const Keire::NavigationMeshSnapshot>(first.Mesh));
    const auto path = world->FindPath({{-4.0F, 0.0F, -4.0F}, {4.0F, 0.0F, 4.0F}});
    CHECK(path.State == Keire::NavigationPathState::Succeeded);
}

TEST_CASE("Navigation mesh publication rejects invalid replacement transactionally")
{
    Keire::NavigationSystemSpecification specification;
    specification.Mode = Keire::NavigationMode::Enabled;
    auto navigation = Keire::CreateRef<Keire::NavigationSystem>(specification);
    auto world = navigation->CreateWorld();
    Keire::NavigationMeshSnapshot valid;
    valid.Revision = 1;
    valid.Nodes = {{Keire::NavigationNodeId(1), {}, 1}};
    world->PublishMesh(std::make_shared<const Keire::NavigationMeshSnapshot>(valid));

    auto invalid = valid;
    invalid.Revision = 2;
    invalid.Nodes.push_back(invalid.Nodes.front());
    CHECK_THROWS_AS(world->PublishMesh(std::make_shared<const Keire::NavigationMeshSnapshot>(invalid)),
                    std::invalid_argument);
    CHECK(world->MeshRevision() == 1);
}

TEST_CASE("Component registry replacement is atomic and revisioned")
{
    auto registry = Keire::ComponentRegistry::CreateDefault();
    const auto initialRevision = registry->Revision();
    registry->Register(Registration("Version One"));
    CHECK(registry->Revision() == initialRevision + 1);
    REQUIRE(registry->Find(ReloadableComponent::StaticType()));
    CHECK(registry->Find(ReloadableComponent::StaticType())->Name == "Version One");

    const std::vector<Keire::ComponentTypeId> replace{ReloadableComponent::StaticType()};
    registry->ReplaceBatch(replace, {Registration("Version Two")});
    CHECK(registry->Revision() == initialRevision + 2);
    REQUIRE(registry->Find(ReloadableComponent::StaticType()));
    CHECK(registry->Find(ReloadableComponent::StaticType())->Name == "Version Two");

    auto invalid = Registration("Invalid");
    invalid.RequiredComponents.push_back(
        Keire::ComponentTypeId(Keire::AssetId(0x6d697373696e672dULL, 0x7479706500000001ULL)));
    const auto revisionBeforeFailure = registry->Revision();
    CHECK_THROWS_AS(registry->ReplaceBatch(replace, {std::move(invalid)}), std::invalid_argument);
    CHECK(registry->Revision() == revisionBeforeFailure);
    REQUIRE(registry->Find(ReloadableComponent::StaticType()));
    CHECK(registry->Find(ReloadableComponent::StaticType())->Name == "Version Two");

    const Keire::ComponentTypeId firstCycle(Keire::AssetId(0x6379636c652d6f6eULL, 0x652d747970650001ULL));
    const Keire::ComponentTypeId secondCycle(Keire::AssetId(0x6379636c652d7477ULL, 0x6f2d747970650001ULL));
    auto firstCyclic = Registration("Cycle One");
    firstCyclic.Type = firstCycle;
    firstCyclic.RequiredComponents = {secondCycle};
    auto secondCyclic = Registration("Cycle Two");
    secondCyclic.Type = secondCycle;
    secondCyclic.RequiredComponents = {firstCycle};
    CHECK_THROWS_AS(registry->ReplaceBatch({}, {std::move(firstCyclic), std::move(secondCyclic)}),
                    std::invalid_argument);
    CHECK(registry->Revision() == revisionBeforeFailure);
    CHECK_FALSE(registry->Contains(firstCycle));
    CHECK_FALSE(registry->Contains(secondCycle));
}
