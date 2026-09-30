#include "Keire/Animation/Skinning.h"
#include "KeireInternal/Scenes/AnimationIkPasses.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    std::string AwaitLog(const std::filesystem::path& path, const std::string_view marker, const std::size_t offset = 0)
    {
        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string contents;
        do
        {
            Keire::Log::Flush();
            contents = KeireTests::ReadFile(path);
            if (contents.find(marker, offset) != std::string::npos)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < timeout);
        return contents;
    }

    struct SupportFixture
    {
        explicit SupportFixture(const bool restrictLayers = false, const bool airborneRight = false,
                                const float supportElevation = 0.0F,
                                Keire::Ref<Keire::SkeletonAsset> sourceSkeleton = {},
                                Keire::Ref<Keire::AnimationClipAsset> sourceClip = {},
                                Keire::Ref<Keire::SkinnedMeshAsset> sourceSkin = {},
                                Keire::Ref<Keire::MeshAsset> sourceMesh = {})
            : SourceSkeleton(std::move(sourceSkeleton)), SourceClip(std::move(sourceClip)),
              SourceSkin(std::move(sourceSkin)), SourceMesh(std::move(sourceMesh))
        {
            Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
            Character = Scene->CreateEntity("Character");
            Floor = Scene->CreateEntity("Support");
            Floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition(
                {0, (airborneRight ? -.7F : -.45F) + supportElevation, 0});
            Floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5, .5F, 5});
            AddAnimation(airborneRight);
            Keire::PhysicsSystemSpecification physics;
            physics.Mode = Keire::PhysicsMode::Enabled;
            if (restrictLayers)
            {
                physics.CollisionMatrix[0] &= ~2U;
                physics.CollisionMatrix[1] &= ~1U;
            }
            Physics = Keire::CreateRef<Keire::PhysicsSystem>(physics);
            Session =
                Keire::CreateRef<Keire::SceneRuntimeSession>(Scene, Assets, Keire::Ref<Keire::AudioSystem>{}, Physics);
            Session->Play();
            Animator = Session->RuntimeScene()->FindEntity(Character.Id()).GetComponent<Keire::AnimatorComponent>();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while ((!Animator->RuntimeDebugSnapshot() || Animator->RuntimeDebugSnapshot()->Pose.empty()) &&
                   std::chrono::steady_clock::now() < deadline)
            {
                (void)Assets->PumpCompletions();
                Tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(Animator->RuntimeDebugSnapshot());
            REQUIRE(Animator->RuntimeDebugSnapshot()->Pose.size() ==
                    (SourceSkeleton ? SourceSkeleton->Bones().size() : 7));
        }
        ~SupportFixture()
        {
            if (Session)
                Session->Stop();
            if (Physics)
                Physics->Close();
            if (Scene)
                Scene->Close();
            if (Assets)
                Assets->Close();
            Database = {};
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
        void Tick(const float delta = 1.0F / 60.0F)
        {
            Session->FixedUpdate(delta);
            Session->Update(delta, 1.0F);
            INFO(Session->Diagnostic().Message);
            REQUIRE(Session->State() == Keire::ScenePlayState::Playing);
        }
        float FootY() const { return Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.Y; }
        void Write(const std::string& name, const std::vector<std::byte>& bytes)
        {
            std::ofstream stream(Root / "Assets" / name, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }
        void AddAnimation(const bool airborneRight)
        {
            std::filesystem::create_directories(Root / "Assets");
            Keire::AssetDatabaseSpecification specification;
            specification.ProjectRoot = Root;
            const auto importer = [&](const std::string& extension, const Keire::AssetTypeId type)
            {
                Keire::AssetImporterRegistration value;
                value.Name = "SupportTest" + extension;
                value.Type = type;
                value.Extensions = {extension};
                value.Import = [](std::span<const std::byte> bytes)
                { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
                specification.Importers.push_back(std::move(value));
            };
            importer(".supportskeleton", Keire::SkeletonAsset::StaticType());
            importer(".supportclip", Keire::AnimationClipAsset::StaticType());
            importer(".supportgraph", Keire::AnimationGraphAsset::StaticType());
            importer(".supportmesh", Keire::MeshAsset::StaticType());
            importer(".supportskin", Keire::SkinnedMeshAsset::StaticType());
            Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
            if (SourceSkeleton)
                Write("Rig.supportskeleton", Keire::SkeletonAsset::Encode(SourceSkeleton->Bones()));
            else
                Write("Rig.supportskeleton",
                      Keire::SkeletonAsset::Encode(std::vector<Keire::SkeletonBone>{
                          {"Hips", -1, {{0, 2, 0}, {}, {1, 1, 1}}, {}},
                          {"LeftUpLeg", 0, {{-.25F, 0, 0}, {}, {1, 1, 1}}, {}},
                          {"LeftLeg", 1, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                          {"LeftFoot", 2, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                          {"RightUpLeg",
                           0,
                           {{.25F, airborneRight ? .4F : 0.0F, airborneRight ? .2F : 0.0F}, {}, {1, 1, 1}},
                           {}},
                          {"RightLeg", 4, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                          {"RightFoot", 5, {{0, -1, 0}, {}, {1, 1, 1}}, {}}}));
            (void)Database->ImportAll();
            const auto skeleton = Database->Find("Rig.supportskeleton");
            REQUIRE(skeleton);
            if (SourceSkin && SourceMesh)
            {
                Write("Rig.supportmesh", Keire::MeshAsset::Encode(SourceMesh->Vertices(), SourceMesh->Indices()));
                (void)Database->ImportAll();
                const auto mesh = Database->Find("Rig.supportmesh");
                REQUIRE(mesh);
                Write("Rig.supportskin", Keire::SkinnedMeshAsset::Encode(
                                             mesh->Id, skeleton->Id, SourceSkin->Influences8(), SourceSkin->Method()));
            }
            Keire::AnimationTrack track;
            track.Bone = 0;
            track.Keys = {{0.0F, {{0, 2, 0}, {}, {1, 1, 1}}}, {1.0F, {{0, 2, 0}, {}, {1, 1, 1}}}};
            if (SourceClip)
                Write("Move.supportclip",
                      Keire::AnimationClipAsset::Encode(skeleton->Id, SourceClip->Duration(), SourceClip->Tracks(),
                                                        SourceClip->Events(), false));
            else
                Write("Move.supportclip",
                      Keire::AnimationClipAsset::Encode(skeleton->Id, 1.0F, std::span(&track, 1), {}, false));
            (void)Database->ImportAll();
            const auto clip = Database->Find("Move.supportclip");
            REQUIRE(clip);
            Keire::AnimationGraphDefinition graph;
            graph.EntryState = "Move";
            graph.States = {{"Move", clip->Id}};
            Write("Controller.supportgraph", Keire::AnimationGraphAsset::Encode(graph));
            const auto imported = Database->ImportAll();
            const auto controller = Database->Find("Controller.supportgraph");
            REQUIRE(controller);
            Keire::AssetSystemSpecification assets;
            assets.Mode = Keire::AssetMode::Development;
            assets.DevelopmentCatalog = imported.CatalogPath;
            assets.WorkerCount = 1;
            assets.Decoders = {Keire::CreateSkeletonAssetDecoder(), Keire::CreateAnimationClipAssetDecoder(),
                               Keire::CreateAnimationGraphAssetDecoder(), Keire::CreateSkinnedMeshAssetDecoder(),
                               Keire::CreateMeshAssetDecoder()};
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assets));
            const auto animator = Character.AddComponent<Keire::AnimatorComponent>();
            animator->SetSkeleton(skeleton->Id);
            if (SourceSkin && SourceMesh)
            {
                const auto skin = Database->Find("Rig.supportskin");
                REQUIRE(skin);
                animator->SetSkinnedMesh(skin->Id);
            }
            animator->SetGraph(controller->Id);
            animator->SetApplyRootMotion(false);
            Keire::AnimatorFootGroundingSettings grounding;
            grounding.Enabled = true;
            grounding.AutomaticBoneMapping = false;
            grounding.FootOffset = 0;
            grounding.ResponseTime = 0;
            if (SourceSkeleton)
            {
                grounding.Pelvis = "Skeleton_torso_joint_1";
                grounding.LeftUpperLeg = "leg_joint_L_1";
                grounding.LeftLowerLeg = "leg_joint_L_2";
                grounding.LeftFoot = "leg_joint_L_3";
                grounding.RightUpperLeg = "leg_joint_R_1";
                grounding.RightLowerLeg = "leg_joint_R_2";
                grounding.RightFoot = "leg_joint_R_3";
                grounding.PlantDistance = 0.015F;
                grounding.ReleaseDistance = 0.035F;
                grounding.ResponseTime = 0.12F;
            }
            if (airborneRight)
            {
                grounding.PlantDistance = 0.25F;
                grounding.ReleaseDistance = 0.3F;
            }
            animator->SetFootGrounding(grounding);
        }

        std::filesystem::path Root = KeireTests::MakeTestDirectory("support-lifecycle");
        Keire::Ref<Keire::Scene> Scene;
        Keire::Entity Character;
        Keire::Entity Floor;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::Ref<Keire::AssetSystem> Assets;
        Keire::Ref<Keire::PhysicsSystem> Physics;
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::Ref<Keire::AnimatorComponent> Animator;
        Keire::Ref<Keire::SkeletonAsset> SourceSkeleton;
        Keire::Ref<Keire::AnimationClipAsset> SourceClip;
        Keire::Ref<Keire::SkinnedMeshAsset> SourceSkin;
        Keire::Ref<Keire::MeshAsset> SourceMesh;
    };
} // namespace

TEST_CASE("Standing clip grounding balances over one supported foot without shifting walking poses")
{
    SupportFixture fixture;
    auto scene = fixture.Session->RuntimeScene();
    const auto floor = scene->FindEntity(fixture.Floor.Id());
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.6F, -0.45F, 0.0F});
    floor.GetComponent<Keire::ColliderComponent>()->SetHalfExtent({0.6F, 0.5F, 5.0F});
    SUBCASE("The unsupported foot has no ray hit") {}
    SUBCASE("The supporting ankle is just outside the ledge")
    {
        floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.9F, -0.45F, 0});
    }
    SUBCASE("The unsupported foot sees ground beyond leg reach")
    {
        auto lowerFloor = scene->CreateEntity("Lower floor");
        lowerFloor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -1.5F, 0});
        lowerFloor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5, 0.5F, 5});
    }
    fixture.Animator->SetRuntimeFootGroundingWeight(0.0F);
    fixture.Tick();
    fixture.Animator->SetRuntimeFootGroundingWeight(1.0F);
    auto character = scene->FindEntity(fixture.Character.Id());
    const auto controller = character.AddComponent<Keire::CharacterControllerComponent>();
    // Exercise the animation stage against a deterministic post-physics standing state.
    controller->ApplyRuntimeState(1, true, {0, 1, 0}, {});
    for (int tick = 0; tick < 90; ++tick)
        fixture.Session->Update(1.0F / 60.0F, 1.0F);
    INFO(fixture.Animator->RuntimeDiagnostic());
    REQUIRE(fixture.Animator->RuntimeDiagnostic().empty());
    CHECK(fixture.Animator->RuntimeDebugSnapshot()->Pose[0].WorldPosition.X < -0.05F);
    CHECK(fixture.FootY() >= 0.049F);

    controller->ApplyRuntimeState(1, true, {0, 1, 0}, {0, 0, 2});
    for (int tick = 0; tick < 90; ++tick)
        fixture.Session->Update(1.0F / 60.0F, 1.0F);
    CHECK(fixture.Animator->RuntimeDebugSnapshot()->Pose[0].WorldPosition.X == doctest::Approx(0.0F).epsilon(0.005));
    CHECK(fixture.FootY() >= 0.049F);

    controller->ApplyRuntimeState(1, false, {0, 1, 0}, {});
    for (int tick = 0; tick < 90; ++tick)
        fixture.Session->Update(1.0F / 60.0F, 1.0F);
    CHECK(fixture.Animator->RuntimeDebugSnapshot()->Pose[0].WorldPosition.X == doctest::Approx(0.0F).epsilon(0.005));
}

TEST_CASE("Standing clip grounding reaches lower support without forcing a walking swing to plant")
{
    for (const bool standing : {false, true})
    {
        SupportFixture fixture;
        auto scene = fixture.Session->RuntimeScene();
        const auto floor = scene->FindEntity(fixture.Floor.Id());
        floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.6F, -0.45F, 0});
        floor.GetComponent<Keire::ColliderComponent>()->SetHalfExtent({0.6F, 0.5F, 5});
        auto lowerFloor = scene->CreateEntity("Reachable lower floor");
        lowerFloor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -0.65F, 0});
        lowerFloor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5, 0.5F, 5});
        fixture.Animator->SetRuntimeFootGroundingWeight(0);
        fixture.Tick();
        fixture.Animator->SetRuntimeFootGroundingWeight(1);
        auto character = scene->FindEntity(fixture.Character.Id());
        const auto controller = character.AddComponent<Keire::CharacterControllerComponent>();
        controller->ApplyRuntimeState(1, true, {0, 1, 0}, {0, 0, standing ? 0.0F : 2.0F});
        for (int tick = 0; tick < 90; ++tick)
            fixture.Session->Update(1.0F / 60.0F, 1.0F);
        CAPTURE(standing);
        INFO(fixture.Animator->RuntimeDiagnostic());
        REQUIRE(fixture.Animator->RuntimeDiagnostic().empty());
        const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
        CHECK(pose[6].WorldPosition.Y == doctest::Approx(standing ? -0.15F : 0.0F).epsilon(0.005));
        CHECK(fixture.FootY() >= 0.049F);
        if (standing)
            CHECK(pose[0].WorldPosition.Y < 1.9F);
    }
}

TEST_CASE("Foot contact trace logs transitions without logging steady planted frames")
{
    KeireTests::LogFixture logs("foot-contact-trace");
    logs.Config.Level = Keire::LogLevel::Trace;
    Keire::Log::Initialize(logs.Config);
    SupportFixture fixture;
    const auto path = logs.Directory / logs.Config.CoreLogFile;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        fixture.Tick();
        Keire::Log::Flush();
    } while (KeireTests::ReadFile(path).find("foot=1 planted") == std::string::npos &&
             std::chrono::steady_clock::now() < deadline);
    const auto planted = KeireTests::ReadFile(path);
    CHECK(planted.find("[IK] entity=") != std::string::npos);
    CHECK(planted.find("foot=0 planted") != std::string::npos);
    CHECK(planted.find("foot=1 planted") != std::string::npos);
    for (int frame = 0; frame < 60; ++frame)
        fixture.Tick();
    Keire::Log::Flush();
    CHECK(KeireTests::ReadFile(path) == planted);
    const auto floor = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
    floor.GetComponent<Keire::ColliderComponent>()->SetEnabled(false);
    fixture.Tick();
    Keire::Log::Flush();
    // Flush queues an asynchronous logger operation; wait for publication without advancing simulation.
    const auto lost = AwaitLog(path, "foot=1 support-lost");
    CHECK(lost.find("foot=0 support-lost") != std::string::npos);
    CHECK(lost.find("foot=1 support-lost") != std::string::npos);
    for (int frame = 0; frame < 60; ++frame)
        fixture.Tick();
    Keire::Log::Flush();
    CHECK(KeireTests::ReadFile(path) == lost);
    floor.GetComponent<Keire::ColliderComponent>()->SetEnabled(true);
    fixture.Tick();
    Keire::Log::Flush();
    CHECK(AwaitLog(path, "foot=1 planted", lost.size()).substr(lost.size()).find("planted") != std::string::npos);
}

TEST_CASE("Foot reach trace reports limit changes and recovery without per-frame repetition")
{
    KeireTests::LogFixture logs("foot-reach-trace");
    logs.Config.Level = Keire::LogLevel::Trace;
    Keire::Log::Initialize(logs.Config);
    SupportFixture fixture;
    auto settings = fixture.Animator->FootGrounding();
    settings.MaximumPelvisAdjustment = 0;
    settings.LockPlantedFeet = false;
    fixture.Animator->SetFootGrounding(settings);
    const auto floor = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -0.7F, 0});
    fixture.Tick();
    Keire::Log::Flush();
    const auto path = logs.Directory / logs.Config.CoreLogFile;
    const auto limited = AwaitLog(path, "reach-limit feet=2");
    CHECK(limited.find("reach-limit feet=2") != std::string::npos);
    for (int frame = 0; frame < 60; ++frame)
        fixture.Tick();
    Keire::Log::Flush();
    CHECK(KeireTests::ReadFile(path) == limited);
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -0.45F, 0});
    fixture.Tick();
    Keire::Log::Flush();
    CHECK(AwaitLog(path, "reach-limit feet=0", limited.size()).substr(limited.size()).find("reach-limit feet=0") !=
          std::string::npos);
    floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -0.7F, 0});
    fixture.Tick();
    floor.GetComponent<Keire::ColliderComponent>()->SetEnabled(false);
    fixture.Tick();
    Keire::Log::Flush();
    CHECK(AwaitLog(path, "reach-limit cleared reason=no-contacts").find("reach-limit cleared reason=no-contacts") !=
          std::string::npos);
}

TEST_CASE("Foot grounding contact fade does not sink a reachable foot into raised support")
{
    SupportFixture fixture;
    auto settings = fixture.Animator->FootGrounding();
    settings.Enabled = false;
    fixture.Animator->SetFootGrounding(settings);
    fixture.Tick();
    settings.Enabled = true;
    settings.ResponseTime = 0.12F;
    fixture.Animator->SetFootGrounding(settings);
    for (int frame = 0; frame < 30; ++frame)
    {
        fixture.Tick();
        CAPTURE(frame);
        CHECK(fixture.FootY() >= 0.049F);
    }
}

TEST_CASE("Foot planting releases unavailable support and reacquires restored colliders")
{
    for (int mode = 0; mode < 4; ++mode)
    {
        CAPTURE(mode);
        SupportFixture fixture;
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(.05F).epsilon(.005));
        auto floor = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
        auto collider = floor.GetComponent<Keire::ColliderComponent>();
        if (mode == 0)
            collider->SetEnabled(false);
        else if (mode == 1)
            REQUIRE(floor.RemoveComponent<Keire::ColliderComponent>());
        else if (mode == 2)
            floor.SetActive(false);
        else
            collider->SetTrigger(true);
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(0.0F).epsilon(.005));
        if (mode == 0)
            collider->SetEnabled(true);
        else if (mode == 1)
            floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5, .5F, 5});
        else if (mode == 2)
            floor.SetActive(true);
        else
            collider->SetTrigger(false);
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(.05F).epsilon(.005));
    }
}

TEST_CASE("Foot planting respects changed collision filters and reacquires eligible support")
{
    for (int mode = 0; mode < 4; ++mode)
    {
        CAPTURE(mode);
        SupportFixture fixture(mode == 3);
        auto settings = fixture.Animator->FootGrounding();
        settings.CollisionMask = mode == 3 ? ~0U : 1U;
        fixture.Animator->SetFootGrounding(settings);
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(.05F).epsilon(.005));
        auto collider =
            fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id()).GetComponent<Keire::ColliderComponent>();
        if (mode == 0 || mode == 3)
            collider->SetLayer(2U);
        else if (mode == 1)
            collider->SetMask(2U);
        else
        {
            settings.CollisionMask = 2U;
            fixture.Animator->SetFootGrounding(settings);
        }
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(0.0F).epsilon(.005));
        collider->SetLayer(1U);
        collider->SetMask(~0U);
        settings.CollisionMask = mode == 3 ? ~0U : 1U;
        fixture.Animator->SetFootGrounding(settings);
        for (int i = 0; i < 5; ++i)
            fixture.Tick();
        CHECK(fixture.FootY() == doctest::Approx(.05F).epsilon(.005));
    }
}

TEST_CASE("Planted feet follow static support transform rebuilds without losing their anchor")
{
    SupportFixture fixture;
    SUBCASE("Immediate contact response") {}
    SUBCASE("Smoothed contact response")
    {
        auto settings = fixture.Animator->FootGrounding();
        settings.ResponseTime = 0.12F;
        fixture.Animator->SetFootGrounding(settings);
    }
    for (int i = 0; i < 5; ++i)
        fixture.Tick();
    auto support = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
    const auto transform = support.GetComponent<Keire::TransformComponent>();
    const auto initialFoot = fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition;
    REQUIRE(initialFoot.Y == doctest::Approx(0.05F).epsilon(0.005));
    const auto localAnchor = Keire::Math::TransformPoint(Keire::Math::Inverse(transform->WorldMatrix()), initialFoot);
    for (int stage = 0; stage < 4; ++stage)
    {
        CAPTURE(stage);
        if (stage == 0)
            transform->SetLocalPosition({0.05F, -0.35F, 0.0F});
        else if (stage == 1)
            transform->SetLocalScale({1.1F, 1.2F, 1.0F});
        else if (stage == 2)
            transform->SetLocalRotation(Keire::Math::EulerDegreesToQuaternion({0.0F, 5.0F, 5.0F}));
        else
        {
            transform->SetLocalRotation({});
            transform->SetLocalPosition({0.0F, -0.45F, 0.0F});
            transform->SetLocalScale({1.0F, 1.0F, 1.0F});
        }
        fixture.Tick();
        const auto expected = Keire::Math::TransformPoint(transform->WorldMatrix(), localAnchor);
        const auto actual = fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition;
        CHECK(actual.X == doctest::Approx(expected.X).epsilon(0.005));
        CHECK(actual.Y == doctest::Approx(expected.Y).epsilon(0.005));
        CHECK(actual.Z == doctest::Approx(expected.Z).epsilon(0.005));
    }
}

TEST_CASE("Smoothed planted legs remain stable through repeated moving support reversals")
{
    for (const int hz : {30, 60, 144})
    {
        CAPTURE(hz);
        const auto step = 1.0F / static_cast<float>(hz);
        float maximumAnchorError = 0.0F;
        float maximumKneeSpeed = 0.0F;
        SupportFixture fixture(false, false, 0.1F);
        auto settings = fixture.Animator->FootGrounding();
        settings.ResponseTime = 0.12F;
        fixture.Animator->SetFootGrounding(settings);
        const auto support = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
        const auto transform = support.GetComponent<Keire::TransformComponent>();
        for (int frame = 0; frame < hz; ++frame)
            fixture.Tick(step);
        const auto initialPose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
        const auto inverse = Keire::Math::Inverse(transform->WorldMatrix());
        const std::array anchors{Keire::Math::TransformPoint(inverse, initialPose[3].WorldPosition),
                                 Keire::Math::TransformPoint(inverse, initialPose[6].WorldPosition)};
        std::array previousKnees{initialPose[2].WorldPosition, initialPose[5].WorldPosition};
        const auto distance = [](Keire::Vector3 a, Keire::Vector3 b)
        { return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z)); };
        for (int frame = 1; frame <= hz * 10; ++frame)
        {
            CAPTURE(frame);
            const auto phase = static_cast<float>(frame) * 6.283185307F / (5.0F * static_cast<float>(hz));
            transform->SetLocalPosition({0.06F * std::sin(phase), -0.35F + 0.04F * std::sin(phase), 0.0F});
            transform->SetLocalRotation(Keire::Math::EulerDegreesToQuaternion(
                {2.0F * std::sin(phase), 5.0F * std::sin(phase), 3.0F * std::sin(phase)}));
            fixture.Tick(step);
            const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
            for (std::size_t leg = 0; leg < 2; ++leg)
            {
                CAPTURE(leg);
                const auto hip = pose[1 + leg * 3].WorldPosition;
                const auto knee = pose[2 + leg * 3].WorldPosition;
                const auto foot = pose[3 + leg * 3].WorldPosition;
                const auto expected = Keire::Math::TransformPoint(transform->WorldMatrix(), anchors[leg]);
                const auto anchorError = distance(foot, expected);
                const auto kneeSpeed = distance(knee, previousKnees[leg]) / step;
                maximumAnchorError = std::max(maximumAnchorError, anchorError);
                maximumKneeSpeed = std::max(maximumKneeSpeed, kneeSpeed);
                CHECK(anchorError < 0.002F);
                CHECK(distance(hip, knee) == doctest::Approx(1.0F).epsilon(0.0005));
                CHECK(distance(knee, foot) == doctest::Approx(1.0F).epsilon(0.0005));
                CHECK(distance(hip, foot) < 1.996F);
                CHECK(kneeSpeed < 1.8F);
                previousKnees[leg] = knee;
            }
        }
        MESSAGE("Moving support hz=", hz, " max anchor error=", maximumAnchorError,
                " max knee speed=", maximumKneeSpeed);
    }
}

TEST_CASE("Foot grounding smoothly releases lost support and safely reacquires a rising platform")
{
    SupportFixture fixture;
    auto settings = fixture.Animator->FootGrounding();
    settings.ResponseTime = 0.12F;
    fixture.Animator->SetFootGrounding(settings);
    auto support = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
    const auto collider = support.GetComponent<Keire::ColliderComponent>();
    const auto transform = support.GetComponent<Keire::TransformComponent>();
    float previous = fixture.FootY();
    collider->SetEnabled(false);
    for (int frame = 0; frame < 90; ++frame)
    {
        fixture.Tick();
        const float current = fixture.FootY();
        CAPTURE(frame);
        CHECK(current <= previous + 0.0001F);
        CHECK(current >= -0.0001F);
        CHECK(previous - current < 0.02F);
        CHECK(fixture.Animator->RuntimeDiagnostic().empty());
        previous = current;
    }
    CHECK(previous == doctest::Approx(0.0F).epsilon(0.001));
    transform->SetLocalPosition({0.0F, -0.35F, 0.0F});
    collider->SetEnabled(true);
    for (int frame = 0; frame < 30; ++frame)
    {
        fixture.Tick();
        CAPTURE(frame);
        CHECK(fixture.FootY() >= 0.149F);
        CHECK(fixture.FootY() <= 0.151F);
        CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    }
    settings.Weight = 0.0F;
    fixture.Animator->SetFootGrounding(settings);
    fixture.Tick();
    CHECK(fixture.FootY() == doctest::Approx(0.0F).epsilon(0.001));
    settings.Weight = 1.0F;
    fixture.Animator->SetFootGrounding(settings);
    fixture.Tick();
    CHECK(fixture.FootY() >= 0.149F);
}

TEST_CASE("Animator grounding diagnostics clear after repair or disabling the failed pass")
{
    for (const bool disable : {false, true})
    {
        CAPTURE(disable);
        SupportFixture fixture;
        auto settings = fixture.Animator->FootGrounding();
        settings.LeftFoot = "UnavailableFoot";
        fixture.Animator->SetFootGrounding(settings);
        fixture.Tick();
        CHECK(fixture.Animator->RuntimeDiagnostic().find("unavailable skeleton bones") != std::string::npos);
        fixture.Tick();
        CHECK_FALSE(fixture.Animator->RuntimeDiagnostic().empty());
        if (disable)
            settings.Enabled = false;
        else
            settings.LeftFoot = "LeftFoot";
        fixture.Animator->SetFootGrounding(settings);
        fixture.Tick();
        CHECK(fixture.Animator->RuntimeDiagnostic().empty());
        CHECK(fixture.FootY() == doctest::Approx(disable ? 0.0F : .05F).epsilon(.005));
    }
}

TEST_CASE("Foot grounding preserves a swinging endpoint while the pelvis follows lower support")
{
    SupportFixture fixture(false, true);
    for (int frame = 0; frame < 30; ++frame)
    {
        fixture.Tick();
        const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
        CAPTURE(frame);
        CHECK(pose[0].WorldPosition.Y < 1.95F);
        CHECK(pose[3].WorldPosition.Y >= -0.201F);
        CHECK(pose[3].WorldPosition.Y <= -0.19F);
        CHECK(pose[6].WorldPosition.X == doctest::Approx(0.25F).epsilon(0.001F));
        CHECK(pose[6].WorldPosition.Y == doctest::Approx(0.4F).epsilon(0.001F));
        CHECK(pose[6].WorldPosition.Z == doctest::Approx(0.2F).epsilon(0.001F));
    }
}

TEST_CASE("Runtime pelvis release applies contact support weight without an additional global fade")
{
    SupportFixture fixture(false, true);
    fixture.Tick();
    const float initialCorrection = fixture.Animator->RuntimeDebugSnapshot()->Pose[0].WorldPosition.Y - 2.0F;
    REQUIRE(initialCorrection < -0.1F);
    auto settings = fixture.Animator->FootGrounding();
    settings.ResponseTime = 0.12F;
    fixture.Animator->SetFootGrounding(settings);
    fixture.Session->RuntimeScene()
        ->FindEntity(fixture.Floor.Id())
        .GetComponent<Keire::ColliderComponent>()
        ->SetEnabled(false);
    for (int frame = 1; frame <= 12; ++frame)
    {
        fixture.Tick();
        CAPTURE(frame);
        const auto blend = std::exp(-static_cast<float>(frame) / (60.0F * settings.ResponseTime));
        // Pelvis reach uses the retained support anchor, independent of the fading endpoint.
        const auto expected = initialCorrection * blend;
        const auto actual = fixture.Animator->RuntimeDebugSnapshot()->Pose[0].WorldPosition.Y - 2.0F;
        CHECK(actual == doctest::Approx(expected).epsilon(0.0001F));
    }
}

TEST_CASE("Clip foot locks preserve horizontal body motion through repeated support transfers")
{
    SupportFixture fixture;
    auto settings = fixture.Animator->FootGrounding();
    settings.ResponseTime = 0.12F;
    fixture.Animator->SetFootGrounding(settings);
    const auto transform =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::TransformComponent>();
    // Move against the planted anchors, then reverse: contact release/reacquisition must not
    // counteract the character's authored motion by translating the pelvis within its model.
    for (int frame = 0; frame < 240; ++frame)
    {
        const float z = 0.3F * std::sin(static_cast<float>(frame) * 0.05F);
        transform->SetLocalPosition({0.0F, 0.0F, z});
        fixture.Tick();
        CAPTURE(frame);
        const auto& pelvis = fixture.Animator->RuntimeDebugSnapshot()->Pose[0];
        CHECK(pelvis.WorldPosition.X == doctest::Approx(0.0F).epsilon(0.00001F));
        CHECK(pelvis.WorldPosition.Z == doctest::Approx(0.0F).epsilon(0.00001F));
        CHECK(std::isfinite(fixture.FootY()));
    }
}

TEST_CASE("Reachable planted feet resist horizontal stance drift without repeated reanchoring")
{
    SupportFixture fixture;
    const auto transform =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::TransformComponent>();
    for (int frame = 0; frame < 240; ++frame)
    {
        const float z = 0.6F * std::sin(static_cast<float>(frame) * 0.05F);
        transform->SetLocalPosition({0.0F, 0.0F, z});
        fixture.Tick();
        CAPTURE(frame);
        const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
        CHECK(std::abs(pose[3].WorldPosition.Z + z) < 0.01F);
        CHECK(std::abs(pose[6].WorldPosition.Z + z) < 0.01F);
    }
}

TEST_CASE("Raised support does not suppress a deliberate foot lift or reacquire a swinging foot")
{
    SupportFixture fixture(false, false, 0.1F);
    const auto transform =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::TransformComponent>();
    for (const float z : {0.0F, 0.05F, 0.1F})
    {
        transform->SetLocalPosition({0.0F, 0.22F, z});
        fixture.Tick();
        CHECK(fixture.FootY() + 0.22F == doctest::Approx(0.22F).epsilon(0.001F));
    }
    transform->SetLocalPosition({0.0F, 0.07F, 0.1F});
    fixture.Tick();
    CHECK(fixture.FootY() + 0.07F == doctest::Approx(0.15F).epsilon(0.001F));
    transform->SetLocalPosition({0.0F, 0.0F, 0.1F});
    fixture.Tick();
    CHECK(fixture.FootY() == doctest::Approx(0.15F).epsilon(0.001F));
    transform->SetLocalPosition({0.0F, 0.22F, 0.1F});
    fixture.Tick();
    CHECK(fixture.FootY() + 0.22F == doctest::Approx(0.22F).epsilon(0.001F));
}

TEST_CASE("Unsupported knee stabilization respects disabled controls and partial grounding weight")
{
    const auto skeleton = Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(
        std::vector<Keire::SkeletonBone>{{"Skeleton_torso_joint_1", -1, {{0, 2, 0}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_L_1", 0, {{-.2F, 0, 0}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_L_2", 1, {{0, -.9F, .02F}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_L_3", 2, {{0, -.9F, -.02F}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_R_1", 0, {{.2F, 0, 0}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_R_2", 4, {{0, -.9F, -.2F}, {}, {1, 1, 1}}, {}},
                                         {"leg_joint_R_3", 5, {{0, -.9F, .2F}, {}, {1, 1, 1}}, {}}}));
    Keire::AnimationTrack track;
    track.Bone = 0;
    track.Keys = {{0.0F, {{0, 2, 0}, {}, {1, 1, 1}}}, {1.0F, {{0, 2, 0}, {}, {1, 1, 1}}}};
    const auto clip = Keire::AnimationClipAsset::Decode(
        Keire::AnimationClipAsset::Encode(Keire::AssetId::Generate(), 1.0F, std::span(&track, 1), {}, false));
    for (const bool enabled : {false, true})
        for (const float stability : {0.0F, 0.9F})
            for (const float weight : {0.0F, 0.25F, 1.0F})
            {
                CAPTURE(enabled);
                CAPTURE(stability);
                CAPTURE(weight);
                SupportFixture fixture(false, false, -10.0F, skeleton, clip);
                auto settings = fixture.Animator->FootGrounding();
                settings.Enabled = enabled;
                settings.KneeStability = stability;
                settings.Weight = weight;
                settings.ResponseTime = 0.0F;
                fixture.Animator->SetFootGrounding(settings);
                for (int frame = 0; frame < 4; ++frame)
                    fixture.Tick();
                const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
                CHECK(pose[0].WorldPosition.Y == doctest::Approx(2.0F));
                CHECK(pose[5].WorldPosition.Z == doctest::Approx(-0.2F));
                CHECK(pose[3].WorldPosition.Y == doctest::Approx(0.2F).epsilon(0.0001F));
                CHECK(pose[3].WorldPosition.Z == doctest::Approx(0.0F).epsilon(0.0001F));
                auto footRotation = Keire::Math::ComposeTransform({}, pose[0].LocalTransform.Rotation, {1, 1, 1});
                for (std::size_t bone = 1; bone <= 3; ++bone)
                    footRotation = Keire::Math::Multiply(
                        footRotation, Keire::Math::ComposeTransform({}, pose[bone].LocalTransform.Rotation, {1, 1, 1}));
                const auto footUp = Keire::Math::TransformPoint(footRotation, {0, 1, 0});
                CHECK(footUp.Y == doctest::Approx(1.0F).epsilon(0.00001F));
                CHECK(footUp.Z == doctest::Approx(0.0F).epsilon(0.00001F));
                if (!enabled || stability == 0.0F || weight == 0.0F)
                {
                    CHECK(pose[2].WorldPosition.Z == doctest::Approx(0.02F));
                    CHECK(pose[3].WorldPosition.Y == doctest::Approx(0.2F));
                }
                else if (weight == 1.0F)
                    CHECK(pose[2].WorldPosition.Z < 0.0F);
                else
                {
                    CHECK(pose[2].WorldPosition.Z < 0.02F);
                    CHECK(pose[2].WorldPosition.Z > -0.02F);
                }
            }
}

TEST_CASE("Unsupported bend correction preserves transformed rig endpoints and segment lengths")
{
    using namespace Keire::Detail;
    for (const Keire::Vector3 scale : {Keire::Vector3{1, 1, 1}, {2, 2, 2}, {1.5F, .75F, .9F}, {-1, 1, 1}})
        for (const float weight : {0.25F, 1.0F})
        {
            CAPTURE(scale.X);
            CAPTURE(scale.Y);
            CAPTURE(scale.Z);
            CAPTURE(weight);
            const Keire::BoneTransform parent{{0, 2, 0}, Keire::Math::EulerDegreesToQuaternion({15, 35, 10}), scale};
            const auto skeleton = Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(
                std::vector<Keire::SkeletonBone>{{"Skeleton_torso_joint_1", -1, parent, {}},
                                                 {"leg_joint_L_1", 0, {{-.2F, 0, 0}, {}, {1, 1, 1}}, {}},
                                                 {"leg_joint_L_2", 1, {{0, -.9F, .02F}, {}, {1, 1, 1}}, {}},
                                                 {"leg_joint_L_3", 2, {{0, -.9F, -.02F}, {}, {1, 1, 1}}, {}},
                                                 {"leg_joint_R_1", 0, {{.2F, 0, 0}, {}, {1, 1, 1}}, {}},
                                                 {"leg_joint_R_2", 4, {{0, -.9F, -.2F}, {}, {1, 1, 1}}, {}},
                                                 {"leg_joint_R_3", 5, {{0, -.9F, .2F}, {}, {1, 1, 1}}, {}}}));
            Keire::AnimationTrack track;
            track.Bone = 0;
            track.Keys = {{0.0F, parent}, {1.0F, parent}};
            const auto clip = Keire::AnimationClipAsset::Decode(
                Keire::AnimationClipAsset::Encode(Keire::AssetId::Generate(), 1.0F, std::span(&track, 1), {}, false));
            SupportFixture fixture(false, false, -10.0F, skeleton, clip);
            auto settings = fixture.Animator->FootGrounding();
            settings.Enabled = false;
            fixture.Animator->SetFootGrounding(settings);
            fixture.Tick();
            const auto before = fixture.Animator->RuntimeDebugSnapshot()->Pose;
            settings.Enabled = true;
            settings.Weight = weight;
            settings.ResponseTime = 0.0F;
            fixture.Animator->SetFootGrounding(settings);
            fixture.Tick();
            const auto& after = fixture.Animator->RuntimeDebugSnapshot()->Pose;
            CHECK(fixture.Animator->RuntimeDiagnostic().empty());
            CHECK(IkVectorLength(IkSubtract(after[2].WorldPosition, before[2].WorldPosition)) > 0.001F);
            const auto footMatrix = [](const auto& pose)
            {
                auto matrix = Keire::Math::ComposeTransform({}, {}, {1, 1, 1});
                for (std::size_t bone = 0; bone <= 3; ++bone)
                {
                    const auto& local = pose[bone].LocalTransform;
                    matrix = Keire::Math::Multiply(
                        matrix, Keire::Math::ComposeTransform(local.Translation, local.Rotation, local.Scale));
                }
                return matrix;
            };
            const auto beforeFoot = footMatrix(before), afterFoot = footMatrix(after);
            for (std::size_t element = 0; element < 16; ++element)
                CHECK(afterFoot.Elements[element] == doctest::Approx(beforeFoot.Elements[element]).epsilon(0.0001F));
            for (const std::size_t bone : {1U, 3U, 4U, 6U})
                CHECK(IkVectorLength(IkSubtract(after[bone].WorldPosition, before[bone].WorldPosition)) < 0.0001F);
            for (const auto chain : {std::array<std::size_t, 3>{1, 2, 3}, {4, 5, 6}})
                for (std::size_t segment = 1; segment < 3; ++segment)
                {
                    const auto start = chain[segment - 1], end = chain[segment];
                    CHECK(IkVectorLength(IkSubtract(after[end].WorldPosition, after[start].WorldPosition)) ==
                          doctest::Approx(
                              IkVectorLength(IkSubtract(before[end].WorldPosition, before[start].WorldPosition)))
                              .epsilon(0.0001F));
                }
        }
}

TEST_CASE("Imported walking character exercises physics grounding across surfaces and update rates" *
          doctest::skip(!std::filesystem::is_regular_file("Build/Validation/RiggingModels/CesiumMan/CesiumMan.glb")))
{
    KeireTests::LogFixture logs("imported-walking-grounding");
    logs.Config.Level = Keire::LogLevel::Trace;
    Keire::Log::Initialize(logs.Config);
    const auto path = std::filesystem::absolute("Build/Validation/RiggingModels/CesiumMan/CesiumMan.glb");
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.is_open());
    REQUIRE(stream.tellg() > 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.SourcePath = path;
    context.RelativePath = path.filename();
    context.ImportSettings["materialImport"] = std::string("none");
    context.ImportSettings["rigSource"] = std::string("embedded");
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto imported = Keire::CreateMeshAssetImporter().ContextualImport(context, bytes);
    const auto skeletonData =
        std::ranges::find(imported.SubAssets, Keire::SkeletonAsset::StaticType(), &Keire::AssetGeneratedSubAsset::Type);
    const auto clipData = std::ranges::find(imported.SubAssets, Keire::AnimationClipAsset::StaticType(),
                                            &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(skeletonData != imported.SubAssets.end());
    REQUIRE(clipData != imported.SubAssets.end());
    const auto skeleton = Keire::SkeletonAsset::Decode(skeletonData->Bytes);
    const auto clip = Keire::AnimationClipAsset::Decode(clipData->Bytes);
    std::array<std::array<std::size_t, 3>, 2> chains{};
    for (std::size_t leg = 0; leg < 2; ++leg)
        for (std::size_t joint = 0; joint < 3; ++joint)
        {
            const auto name = std::string("leg_joint_") + (leg == 0 ? "L_" : "R_") + std::to_string(joint + 1);
            const auto bone = std::ranges::find(skeleton->Bones(), name, &Keire::SkeletonBone::Name);
            REQUIRE(bone != skeleton->Bones().end());
            chains[leg][joint] = static_cast<std::size_t>(bone - skeleton->Bones().begin());
        }
    std::ofstream trace;
    std::string outputDirectory;
#if defined(_WIN32)
    char* environmentValue = nullptr;
    std::size_t environmentSize = 0;
    (void)_dupenv_s(&environmentValue, &environmentSize, "KEIRE_IK_TEST_TRACE_DIRECTORY");
    const std::unique_ptr<char, decltype(&std::free)> ownedEnvironment(environmentValue, &std::free);
    if (ownedEnvironment)
        outputDirectory = ownedEnvironment.get();
#else
    if (const auto value = std::getenv("KEIRE_IK_TEST_TRACE_DIRECTORY"))
        outputDirectory = value;
#endif
    if (!outputDirectory.empty())
    {
        std::filesystem::create_directories(outputDirectory);
        trace.open(std::filesystem::path(outputDirectory) / "walking-poses.csv");
        REQUIRE(trace.is_open());
        trace << "surface,hz,frame,leg,hipX,hipY,hipZ,kneeX,kneeY,kneeZ,footX,footY,footZ,bendSide\n";
    }
    using namespace Keire::Detail;
    for (const int hz : {30, 60, 144})
        for (int surface = 0; surface < 6; ++surface)
        {
            CAPTURE(hz);
            CAPTURE(surface);
            SupportFixture fixture(false, false, surface == 2 ? 0.05F : -0.05F, skeleton, clip);
            if (surface == 5)
            {
                auto settings = fixture.Animator->FootGrounding();
                settings.Enabled = false;
                fixture.Animator->SetFootGrounding(settings);
            }
            const auto support = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
            const auto transform = support.GetComponent<Keire::TransformComponent>();
            const auto origin = transform->LocalPosition();
            if (surface == 1)
                transform->SetLocalRotation(Keire::Math::EulerDegreesToQuaternion({0, 0, 8}));
            const auto initial = fixture.Animator->RuntimeDebugSnapshot()->Pose;
            std::array<float, 2> upperLengths{}, lowerLengths{};
            for (std::size_t leg = 0; leg < 2; ++leg)
            {
                upperLengths[leg] = IkVectorLength(
                    IkSubtract(initial[chains[leg][1]].WorldPosition, initial[chains[leg][0]].WorldPosition));
                lowerLengths[leg] = IkVectorLength(
                    IkSubtract(initial[chains[leg][2]].WorldPosition, initial[chains[leg][1]].WorldPosition));
            }
            std::array<Keire::Vector3, 2> previousKnees{};
            for (int frame = 0; frame < hz * 6; ++frame)
            {
                CAPTURE(frame);
                const auto time = static_cast<float>(frame) / static_cast<float>(hz);
                if (surface == 3)
                    transform->SetLocalPosition({origin.X + 0.06F * std::sin(time * 1.256637F),
                                                 origin.Y + 0.04F * std::sin(time * 1.256637F), origin.Z});
                if (surface == 4 && (frame == hz * 2 || frame == hz * 3))
                    support.GetComponent<Keire::ColliderComponent>()->SetEnabled(frame == hz * 3);
                if (trace.is_open())
                    Keire::Log::GetCoreLogger().Write(Keire::LogLevel::Trace,
                                                      Keire::LogMessage("[IK-test] entity={} surface={} hz={} frame={}",
                                                                        fixture.Character.Id(), surface, hz, frame));
                fixture.Tick(1.0F / static_cast<float>(hz));
                const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
                const auto reference = AutomaticBipedKneeReference(pose[chains[0][0]].WorldPosition,
                                                                   pose[chains[1][0]].WorldPosition, {0, 1, 0});
                for (std::size_t leg = 0; leg < 2; ++leg)
                {
                    CAPTURE(leg);
                    const auto hip = pose[chains[leg][0]].WorldPosition;
                    const auto knee = pose[chains[leg][1]].WorldPosition;
                    const auto foot = pose[chains[leg][2]].WorldPosition;
                    REQUIRE(Keire::Math::IsFinite(hip));
                    REQUIRE(Keire::Math::IsFinite(knee));
                    REQUIRE(Keire::Math::IsFinite(foot));
                    // This clip's unmodified knee speed stays below 1.6 units/s. Raised-ground
                    // clearance must not introduce the former 6-9 units/s forced-plant jolt.
                    if ((surface == 2 || surface == 3) && frame > 0)
                        CHECK(IkVectorLength(IkSubtract(knee, previousKnees[leg])) <= 2.0F / static_cast<float>(hz));
                    previousKnees[leg] = knee;
                    CHECK(IkVectorLength(IkSubtract(knee, hip)) == doctest::Approx(upperLengths[leg]).epsilon(0.001));
                    CHECK(IkVectorLength(IkSubtract(foot, knee)) == doctest::Approx(lowerLengths[leg]).epsilon(0.001));
                    const auto bend = IkProjectOntoPlane(IkSubtract(knee, hip), IkNormalize(IkSubtract(foot, hip)));
                    const auto side = IkDot(bend, reference);
                    if (surface != 5)
                        CHECK(side >= -0.001F);
                    if (trace.is_open())
                        trace << surface << ',' << hz << ',' << frame << ',' << leg << ',' << hip.X << ',' << hip.Y
                              << ',' << hip.Z << ',' << knee.X << ',' << knee.Y << ',' << knee.Z << ',' << foot.X << ','
                              << foot.Y << ',' << foot.Z << ',' << side << '\n';
                }
            }
        }
    if (!outputDirectory.empty())
    {
        constexpr std::string_view completed = "[IK-test] walking sweep complete";
        Keire::Log::GetCoreLogger().Write(Keire::LogLevel::Trace, completed);
        REQUIRE(AwaitLog(logs.Directory / logs.Config.CoreLogFile, completed).find(completed) != std::string::npos);
        std::filesystem::copy_file(logs.Directory / logs.Config.CoreLogFile,
                                   std::filesystem::path(outputDirectory) / "walking-contacts.log",
                                   std::filesystem::copy_options::overwrite_existing);
    }
}
TEST_CASE("Authored IK recovers from deleted targets and poles without interrupting independent goals")
{
    SupportFixture fixture;
    auto grounding = fixture.Animator->FootGrounding();
    grounding.Enabled = false;
    fixture.Animator->SetFootGrounding(grounding);
    const auto scene = fixture.Session->RuntimeScene();
    const Keire::Vector3 desired{-0.25F, 0.6F, 0.3F};
    auto target = scene->CreateEntity("IK target");
    target.GetComponent<Keire::TransformComponent>()->SetLocalPosition(desired);
    auto pole = scene->CreateEntity("IK pole");
    pole.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.25F, 1.0F, 1.0F});
    Keire::AnimatorLimbIkSettings limb;
    limb.Enabled = true;
    limb.AutomaticBoneMapping = false;
    limb.Root = "LeftUpLeg";
    limb.Middle = "LeftLeg";
    limb.End = "LeftFoot";
    limb.Target = target.Id();
    limb.Pole = pole.Id();
    limb.RotationWeight = 0;
    fixture.Animator->SetLeftArmIk(limb);
    fixture.Animator->SetTwoBoneIk("independent", "RightUpLeg", "RightLeg", "RightFoot", {0.25F, 0.5F, 0.2F},
                                   {0.25F, 1.0F, 1.0F}, 1.0F, Keire::AnimatorIkSpace::Model);
    const auto checkPose = [&](const bool solved)
    {
        fixture.Tick();
        const auto snapshot = fixture.Animator->RuntimeDebugSnapshot();
        REQUIRE(snapshot);
        REQUIRE(snapshot->Pose.size() == 7);
        for (const auto& bone : snapshot->Pose)
            CHECK(Keire::Math::IsFinite(bone.WorldPosition));
        const auto left = snapshot->Pose[3].WorldPosition;
        CHECK(left.X == doctest::Approx(desired.X).epsilon(0.001F));
        CHECK(left.Y == doctest::Approx(solved ? desired.Y : 0.0F).epsilon(0.001F));
        CHECK(left.Z == doctest::Approx(solved ? desired.Z : 0.0F).epsilon(0.001F));
        const auto right = snapshot->Pose[6].WorldPosition;
        CHECK(right.X == doctest::Approx(0.25F).epsilon(0.001F));
        CHECK(right.Y == doctest::Approx(0.5F).epsilon(0.001F));
        CHECK(right.Z == doctest::Approx(0.2F).epsilon(0.001F));
    };
    checkPose(true);
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    REQUIRE(scene->DestroyEntity(target.Id()));
    checkPose(false);
    CHECK(fixture.Animator->RuntimeDiagnostic().find("target is unavailable") != std::string::npos);
    target = scene->CreateEntity("Replacement IK target");
    target.GetComponent<Keire::TransformComponent>()->SetLocalPosition(desired);
    limb.Target = target.Id();
    fixture.Animator->SetLeftArmIk(limb);
    checkPose(true);
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    REQUIRE(scene->DestroyEntity(pole.Id()));
    checkPose(false);
    CHECK(fixture.Animator->RuntimeDiagnostic().find("pole override is unavailable") != std::string::npos);
    limb.Pole = {};
    fixture.Animator->SetLeftArmIk(limb);
    checkPose(true);
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    pole = scene->CreateEntity("Replacement IK pole");
    pole.GetComponent<Keire::TransformComponent>()->SetLocalPosition({-0.25F, 1.0F, 1.0F});
    limb.Pole = pole.Id();
    fixture.Animator->SetLeftArmIk(limb);
    checkPose(true);
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
}
TEST_CASE("Runtime named IK goals and diagnostics do not leak across Play restarts")
{
    SupportFixture fixture;
    const auto authored = fixture.Character.GetComponent<Keire::AnimatorComponent>();
    REQUIRE(authored->IkGoals().empty());
    fixture.Animator->SetTwoBoneIk("bad", "Missing", "LeftLeg", "LeftFoot", {}, {});
    fixture.Animator->SetTwoBoneIk("right", "RightUpLeg", "RightLeg", "RightFoot", {0.25F, 0.5F, 0.2F},
                                   {0.25F, 1.0F, 1.0F}, 1.0F, Keire::AnimatorIkSpace::Model);
    fixture.Tick();
    CHECK(fixture.Animator->RuntimeDiagnostic().find("'bad'") != std::string::npos);
    CHECK(authored->IkGoals().empty());
    const auto previousAnimator = fixture.Animator;
    fixture.Session->Stop();
    REQUIRE(fixture.Session->State() == Keire::ScenePlayState::Stopped);
    CHECK(authored->IkGoals().empty());
    fixture.Session->Play();
    fixture.Animator =
        fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id()).GetComponent<Keire::AnimatorComponent>();
    REQUIRE(fixture.Animator);
    CHECK(fixture.Animator != previousAnimator);
    CHECK(fixture.Animator->IkGoals().empty());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!fixture.Animator->RuntimeDebugSnapshot() || fixture.Animator->RuntimeDebugSnapshot()->Pose.empty()) &&
           std::chrono::steady_clock::now() < deadline)
    {
        (void)fixture.Assets->PumpCompletions();
        fixture.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(fixture.Animator->RuntimeDebugSnapshot());
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    CHECK(fixture.Animator->IkGoals().empty());
    CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
}
TEST_CASE("Reactivating a moved actor acquires contacts at its current position")
{
    for (const bool disableEntity : {false, true})
    {
        CAPTURE(disableEntity);
        SupportFixture fixture;
        auto actor = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
        const auto transform = actor.GetComponent<Keire::TransformComponent>();
        fixture.Animator->SetTwoBoneIk("right", "RightUpLeg", "RightLeg", "RightFoot", {0.25F, 0.5F, 0.2F},
                                       {0.25F, 1.0F, 1.0F}, 1.0F, Keire::AnimatorIkSpace::Model);
        fixture.Tick();
        const float originalX = fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.X;
        if (disableEntity)
            actor.SetActive(false);
        else
            fixture.Animator->SetEnabled(false);
        fixture.Tick();
        transform->SetLocalPosition({0.3F, 0.0F, 0.0F});
        fixture.Tick();
        if (disableEntity)
            actor.SetActive(true);
        else
            fixture.Animator->SetEnabled(true);
        fixture.Tick();
        REQUIRE(fixture.Animator->RuntimeDebugSnapshot());
        REQUIRE(fixture.Animator->IkGoals().size() == 1);
        CHECK(fixture.Animator->IkGoals().front().Name == "right");
        const auto rightFoot = fixture.Animator->RuntimeDebugSnapshot()->Pose[6].WorldPosition;
        CHECK(rightFoot.X == doctest::Approx(0.25F).epsilon(0.005F));
        CHECK(rightFoot.Y == doctest::Approx(0.5F).epsilon(0.005F));
        CHECK(rightFoot.Z == doctest::Approx(0.2F).epsilon(0.005F));
        CHECK(fixture.Animator->RuntimeDiagnostic().empty());
        CHECK(Keire::Math::TransformPoint(transform->WorldMatrix(),
                                          fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition)
                  .X == doctest::Approx(originalX + 0.3F).epsilon(0.005F));
        CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
    }
}
TEST_CASE("Repairing grounding after an invalid mapping acquires contacts at the current actor position")
{
    SupportFixture fixture;
    auto actor = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    const auto transform = actor.GetComponent<Keire::TransformComponent>();
    const auto originalX = fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.X;
    auto settings = fixture.Animator->FootGrounding();
    settings.LeftFoot = "UnavailableFoot";
    fixture.Animator->SetFootGrounding(settings);
    fixture.Tick();
    REQUIRE_FALSE(fixture.Animator->RuntimeDiagnostic().empty());
    transform->SetLocalPosition({0.3F, 0.0F, 0.0F});
    fixture.Tick();
    settings.LeftFoot = "LeftFoot";
    fixture.Animator->SetFootGrounding(settings);
    fixture.Tick();
    REQUIRE(fixture.Animator->RuntimeDebugSnapshot());
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    CHECK(Keire::Math::TransformPoint(transform->WorldMatrix(),
                                      fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition)
              .X == doctest::Approx(originalX + 0.3F).epsilon(0.005F));
    CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
}

TEST_CASE("Descending swing respects raised clearance without a late forced-plant jump")
{
    for (const int hz : {30, 60, 144})
        for (const float response : {0.0F, 0.12F})
        {
            CAPTURE(hz);
            CAPTURE(response);
            SupportFixture fixture(false, false, 0.1F);
            auto settings = fixture.Animator->FootGrounding();
            settings.ResponseTime = response;
            settings.PlantDistance = 0.015F;
            settings.ReleaseDistance = 0.035F;
            fixture.Animator->SetFootGrounding(settings);
            const auto actor = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
            const auto transform = actor.GetComponent<Keire::TransformComponent>();
            transform->SetLocalPosition({0.0F, 0.22F, 0.0F});
            for (int frame = 0; frame < hz; ++frame)
                fixture.Tick(1.0F / static_cast<float>(hz));
            float previousY = fixture.FootY() + 0.22F;
            for (int frame = 1; frame <= hz; ++frame)
            {
                CAPTURE(frame);
                const float fraction = static_cast<float>(frame) / static_cast<float>(hz);
                const float y = 0.22F - 0.14F * fraction;
                transform->SetLocalPosition({0.0F, y, 0.1F * fraction});
                fixture.Tick(1.0F / static_cast<float>(hz));
                const auto foot = Keire::Math::TransformPoint(
                    transform->WorldMatrix(), fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition);
                CHECK(foot.Y >= 0.149F);
                // This synthetic leg starts exactly straight; allow its two-unit chain's
                // 0.25% solver singularity margin, but not accumulated terrain penetration.
                CHECK(std::abs(foot.Y - previousY) <= 0.2F / static_cast<float>(hz) + 0.0051F);
                previousY = foot.Y;
            }
            transform->SetLocalPosition({0.0F, 0.22F, 0.2F});
            for (int frame = 0; frame < hz; ++frame)
                fixture.Tick(1.0F / static_cast<float>(hz));
            CHECK(fixture.FootY() + 0.22F == doctest::Approx(0.22F).epsilon(0.005F));
        }
}
TEST_CASE("Recovering an unavailable skinned mesh acquires foot contacts at the current actor position")
{
    SupportFixture fixture;
    auto actor = fixture.Session->RuntimeScene()->FindEntity(fixture.Character.Id());
    const auto transform = actor.GetComponent<Keire::TransformComponent>();
    const auto originalX = fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.X;
    fixture.Animator->SetSkinnedMesh(Keire::AssetId::Generate());
    fixture.Tick();
    REQUIRE_FALSE(fixture.Animator->RuntimeDiagnostic().empty());
    transform->SetLocalPosition({0.3F, 0.0F, 0.0F});
    fixture.Tick();
    fixture.Animator->SetSkinnedMesh({});
    fixture.Tick();
    REQUIRE(fixture.Animator->RuntimeDebugSnapshot());
    CHECK(fixture.Animator->RuntimeDiagnostic().empty());
    CHECK(Keire::Math::TransformPoint(transform->WorldMatrix(),
                                      fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition)
              .X == doctest::Approx(originalX + 0.3F).epsilon(0.005F));
    CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
}

TEST_CASE("Skeleton reload reacquires contacts for changed limb geometry")
{
    SupportFixture fixture;
    const auto skeletonId = fixture.Animator->Skeleton();
    const auto loaded = fixture.Assets->Load<Keire::SkeletonAsset>(skeletonId).TryGetLoaded();
    REQUIRE(loaded);
    std::vector<Keire::SkeletonBone> bones(loaded->Bones().begin(), loaded->Bones().end());
    bones[1].BindPose.Translation.X += 0.3F;
    const auto replacement = Keire::SkeletonAsset::Decode(Keire::SkeletonAsset::Encode(bones));
    REQUIRE(fixture.Assets->PublishDevelopmentAsset(skeletonId, replacement));
    fixture.Tick();
    REQUIRE(fixture.Animator->RuntimeDebugSnapshot());
    CHECK(fixture.Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.X == doctest::Approx(0.05F).epsilon(0.005F));
    CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
}

TEST_CASE("Shared-scene IK actors isolate goals and recover independently after component replacement")
{
    SupportFixture fixture;
    constexpr std::size_t count = 32;
    std::array<Keire::Entity, count> actors;
    std::array<Keire::Ref<Keire::AnimatorComponent>, count> animators;
    const auto configure = [&](const std::size_t index)
    {
        auto animator = actors[index].AddComponent<Keire::AnimatorComponent>();
        animator->SetSkeleton(fixture.Animator->Skeleton());
        animator->SetGraph(fixture.Animator->Graph());
        animator->SetApplyRootMotion(false);
        animator->SetTwoBoneIk("foot", "RightUpLeg", "RightLeg", "RightFoot",
                               {0.25F, 0.5F, 0.1F + 0.01F * static_cast<float>(index)}, {0.25F, 1.0F, 1.0F}, 1.0F,
                               Keire::AnimatorIkSpace::Model);
        animators[index] = animator;
    };
    for (std::size_t index = 0; index < count; ++index)
    {
        actors[index] = fixture.Session->RuntimeScene()->CreateEntity("IK actor " + std::to_string(index));
        actors[index].GetComponent<Keire::TransformComponent>()->SetLocalPosition(
            {static_cast<float>(index) * 3.0F, 0.0F, 0.0F});
        configure(index);
    }
    for (int frame = 0; frame < 120; ++frame)
    {
        CAPTURE(frame);
        if (frame == 30)
            animators[0]->SetSkinnedMesh(Keire::AssetId::Generate());
        if (frame == 60)
            animators[0]->SetSkinnedMesh({});
        if (frame == 90)
        {
            REQUIRE(actors[1].RemoveComponent<Keire::AnimatorComponent>());
            fixture.Tick();
            configure(1);
            CHECK(animators[1]->IkGoals().size() == 1);
        }
        fixture.Tick();
        for (std::size_t index = 0; index < count; ++index)
        {
            CAPTURE(index);
            if (index == 0 && frame >= 30 && frame < 60)
            {
                CHECK_FALSE(animators[index]->RuntimeDiagnostic().empty());
                continue;
            }
            CHECK(animators[index]->RuntimeDiagnostic().empty());
            REQUIRE(animators[index]->RuntimeDebugSnapshot());
            const auto& pose = animators[index]->RuntimeDebugSnapshot()->Pose;
            REQUIRE(pose.size() == 7);
            const auto foot = pose[6].WorldPosition;
            CHECK(foot.X == doctest::Approx(0.25F).epsilon(0.005F));
            CHECK(foot.Y == doctest::Approx(0.5F).epsilon(0.005F));
            CHECK(foot.Z == doctest::Approx(0.1F + 0.01F * static_cast<float>(index)).epsilon(0.005F));
            for (const auto& bone : pose)
            {
                CHECK(std::isfinite(bone.WorldPosition.X));
                CHECK(std::isfinite(bone.WorldPosition.Y));
                CHECK(std::isfinite(bone.WorldPosition.Z));
            }
        }
        CHECK(fixture.Animator->IkGoals().empty());
        CHECK(fixture.Animator->RuntimeDiagnostic().empty());
        CHECK(fixture.FootY() == doctest::Approx(0.05F).epsilon(0.005F));
    }
}

TEST_CASE("Animated foot mesh stays above moving support throughout the walking cycle" *
          doctest::skip(!std::filesystem::is_regular_file("Build/Validation/RiggingModels/CesiumMan/CesiumMan.glb")))
{
    const auto path = std::filesystem::absolute("Build/Validation/RiggingModels/CesiumMan/CesiumMan.glb");
    if (!std::filesystem::exists(path))
    {
        MESSAGE("Optional CesiumMan fixture is unavailable.");
        return;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE(input.is_open());
    std::vector<std::byte> bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.good());
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.SourcePath = path;
    context.RelativePath = path.filename();
    context.ImportSettings["materialImport"] = std::string("none");
    context.ImportSettings["rigSource"] = std::string("embedded");
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto imported = Keire::CreateMeshAssetImporter().ContextualImport(context, bytes);
    const auto find = [&](const Keire::AssetTypeId type)
    {
        const auto found = std::ranges::find(imported.SubAssets, type, &Keire::AssetGeneratedSubAsset::Type);
        REQUIRE(found != imported.SubAssets.end());
        return found->Bytes;
    };
    const auto skeleton = Keire::SkeletonAsset::Decode(find(Keire::SkeletonAsset::StaticType()));
    const auto clip = Keire::AnimationClipAsset::Decode(find(Keire::AnimationClipAsset::StaticType()));
    const auto skin = Keire::SkinnedMeshAsset::Decode(find(Keire::SkinnedMeshAsset::StaticType()));
    const auto mesh = Keire::MeshAsset::Decode(imported.Bytes);
    int hz = 60;
    float elevation = 0.08F;
    float slope = 0;
    bool grounding = true;
    SUBCASE("Raised support at 60 Hz") {}
    SUBCASE("Raised support at 30 Hz") { hz = 30; }
    SUBCASE("Raised support at 144 Hz") { hz = 144; }
    SUBCASE("Flat support") { elevation = 0; }
    SUBCASE("Sloped support") { slope = 10; }
    SUBCASE("Authored motion reference") { grounding = false; }
    CAPTURE(hz);
    CAPTURE(elevation);
    CAPTURE(slope);
    SupportFixture fixture(false, false, 0.03F, skeleton, clip, skin, mesh);
    auto settings = fixture.Animator->FootGrounding();
    settings.FootOffset = 0.02F;
    settings.Enabled = grounding;
    fixture.Animator->SetFootGrounding(settings);
    const auto support = fixture.Session->RuntimeScene()->FindEntity(fixture.Floor.Id());
    const auto transform = support.GetComponent<Keire::TransformComponent>();
    const auto supportRotation = Keire::Math::EulerDegreesToQuaternion({0, 0, slope});
    transform->SetLocalRotation(supportRotation);
    const auto normal =
        Keire::Math::TransformDirection(Keire::Math::ComposeTransform({}, supportRotation, {1, 1, 1}), {0, 1, 0});
    std::vector<Keire::Matrix4> matrices(skeleton->Bones().size()), palette(matrices.size());
    std::vector<Keire::MeshVertex> deformed(mesh->Vertices().size());
    float worstClearance = 1.0F;
    float maximumPelvisSpeed = 0;
    Keire::Vector3 previousPelvis;
    int worstFrame = 0;
    float maximumThighStep = 0;
    float maximumCalfStep = 0;
    int worstThighFrame = 0;
    std::array<Keire::Quaternion, 4> previousThighs{};
    std::array<float, 2> previousFlexion{};
    float maximumFlexionStep = 0;
    const std::array<std::string, 4> legBones{"leg_joint_L_1", "leg_joint_R_1", "leg_joint_L_2", "leg_joint_R_2"};
    for (int frame = 0; frame < 6 * hz; ++frame)
    {
        const auto phase = static_cast<float>(frame) / static_cast<float>(hz) * 1.256637F;
        const float surface = elevation + 0.04F * std::sin(phase);
        const Keire::Vector3 surfacePoint{0.06F * std::sin(phase), surface, 0};
        transform->SetLocalPosition(
            Keire::Detail::IkSubtract(surfacePoint, {normal.X * 0.5F, normal.Y * 0.5F, normal.Z * 0.5F}));
        (void)fixture.Assets->PumpCompletions();
        fixture.Tick(1.0F / static_cast<float>(hz));
        const auto& pose = fixture.Animator->RuntimeDebugSnapshot()->Pose;
        for (std::size_t bone = 0; bone < pose.size(); ++bone)
        {
            const auto& local = pose[bone].LocalTransform;
            matrices[bone] = Keire::Math::ComposeTransform(local.Translation, local.Rotation, local.Scale);
            const auto& definition = skeleton->Bones()[bone];
            if (definition.Parent >= 0)
                matrices[bone] = Keire::Math::Multiply(matrices[definition.Parent], matrices[bone]);
            palette[bone] = Keire::Math::Multiply(matrices[bone], definition.InverseBindPose);
        }
        Keire::SkinMeshCpu(mesh->Vertices(), skin->Influences8(), palette, skin->Method(), deformed);
        float lowest = 1000;
        for (const auto& vertex : deformed)
        {
            REQUIRE(Keire::Math::IsFinite(vertex.Position));
            lowest = std::min(lowest,
                              Keire::Detail::IkDot(Keire::Detail::IkSubtract(vertex.Position, surfacePoint), normal));
        }
        if (frame > hz / 2 && lowest < worstClearance)
        {
            worstClearance = lowest;
            worstFrame = frame;
        }
        const auto pelvisBone =
            std::ranges::find(pose, std::string("Skeleton_torso_joint_1"), [](const auto& bone) { return bone.Name; });
        const auto pelvis = pelvisBone->WorldPosition;
        if (frame > hz / 2)
            maximumPelvisSpeed = std::max(
                maximumPelvisSpeed, Keire::Detail::IkVectorLength(Keire::Detail::IkSubtract(pelvis, previousPelvis)) *
                                        static_cast<float>(hz));
        previousPelvis = pelvis;
        for (std::size_t leg = 0; leg < 2; ++leg)
        {
            const auto prefix = std::string(leg == 0 ? "leg_joint_L_" : "leg_joint_R_");
            const auto joint = [&](const char* suffix)
            {
                return std::ranges::find(pose, prefix + suffix, [](const auto& bone) { return bone.Name; })
                    ->WorldPosition;
            };
            const auto upper = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(joint("2"), joint("1")));
            const auto lower = Keire::Detail::IkNormalize(Keire::Detail::IkSubtract(joint("3"), joint("2")));
            const auto flexion = std::acos(std::clamp(Keire::Detail::IkDot(upper, lower), -1.0F, 1.0F)) * 57.2957795F;
            if (frame > hz / 2)
                maximumFlexionStep = std::max(maximumFlexionStep, std::abs(flexion - previousFlexion[leg]));
            previousFlexion[leg] = flexion;
        }
        for (std::size_t leg = 0; leg < legBones.size(); ++leg)
        {
            const auto thigh = std::ranges::find(pose, legBones[leg], [](const auto& bone) { return bone.Name; });
            const auto rotation = thigh->LocalTransform.Rotation;
            const auto& previous = previousThighs[leg];
            const float dot = std::abs(rotation.X * previous.X + rotation.Y * previous.Y + rotation.Z * previous.Z +
                                       rotation.W * previous.W);
            const float step = 2.0F * std::acos(std::min(dot, 1.0F)) * 57.2957795F;
            if (frame > hz / 2 && leg < 2 && step > maximumThighStep)
            {
                maximumThighStep = step;
                worstThighFrame = frame;
            }
            if (frame > hz / 2 && leg >= 2)
                maximumCalfStep = std::max(maximumCalfStep, step);
            previousThighs[leg] = rotation;
        }
    }
    MESSAGE("Maximum thigh rotation step=" << maximumThighStep << " frame=" << worstThighFrame);
    MESSAGE("Maximum knee flexion step=" << maximumFlexionStep);
    CHECK(maximumThighStep * static_cast<float>(hz) <= 720.0F);
    // Knee flexion combines the motion of both segments; retain a separate bound from hip swing.
    CHECK(maximumCalfStep * static_cast<float>(hz) <= 1440.0F);
    CHECK(maximumFlexionStep * static_cast<float>(hz) <= 1440.0F);
    MESSAGE("Actual skinned mesh minimum clearance=" << worstClearance << " worst frame=" << worstFrame
                                                     << " maximum pelvis speed=" << maximumPelvisSpeed);
    if (grounding)
        CHECK(worstClearance >= 0.0F);
    // Includes the authored vertical gait plus support acquisition/release, not just platform velocity.
    CHECK(maximumPelvisSpeed < 0.75F);
}
