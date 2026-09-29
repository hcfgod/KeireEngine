#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct SupportFixture
    {
        explicit SupportFixture(const bool restrictLayers = false)
        {
            Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition());
            Character = Scene->CreateEntity("Character");
            Floor = Scene->CreateEntity("Support");
            Floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0, -.45F, 0});
            Floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({5, .5F, 5});
            AddAnimation();
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
            REQUIRE(Animator->RuntimeDebugSnapshot()->Pose.size() == 7);
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
        void Tick()
        {
            Session->FixedUpdate(1.0F / 60.0F);
            Session->Update(1.0F / 60.0F, 1.0F);
            REQUIRE(Session->State() == Keire::ScenePlayState::Playing);
        }
        float FootY() const { return Animator->RuntimeDebugSnapshot()->Pose[3].WorldPosition.Y; }
        void Write(const std::string& name, const std::vector<std::byte>& bytes)
        {
            std::ofstream stream(Root / "Assets" / name, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }
        void AddAnimation()
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
            Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
            Write("Rig.supportskeleton", Keire::SkeletonAsset::Encode(std::vector<Keire::SkeletonBone>{
                                             {"Hips", -1, {{0, 2, 0}, {}, {1, 1, 1}}, {}},
                                             {"LeftUpLeg", 0, {{-.25F, 0, 0}, {}, {1, 1, 1}}, {}},
                                             {"LeftLeg", 1, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                                             {"LeftFoot", 2, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                                             {"RightUpLeg", 0, {{.25F, 0, 0}, {}, {1, 1, 1}}, {}},
                                             {"RightLeg", 4, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                                             {"RightFoot", 5, {{0, -1, 0}, {}, {1, 1, 1}}, {}}}));
            (void)Database->ImportAll();
            const auto skeleton = Database->Find("Rig.supportskeleton");
            REQUIRE(skeleton);
            Keire::AnimationTrack track;
            track.Bone = 0;
            track.Keys = {{0.0F, {{0, 2, 0}, {}, {1, 1, 1}}}, {1.0F, {{0, 2, 0}, {}, {1, 1, 1}}}};
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
                               Keire::CreateAnimationGraphAssetDecoder()};
            Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assets));
            const auto animator = Character.AddComponent<Keire::AnimatorComponent>();
            animator->SetSkeleton(skeleton->Id);
            animator->SetGraph(controller->Id);
            animator->SetApplyRootMotion(false);
            Keire::AnimatorFootGroundingSettings grounding;
            grounding.Enabled = true;
            grounding.AutomaticBoneMapping = false;
            grounding.FootOffset = 0;
            grounding.ResponseTime = 0;
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
    };
} // namespace
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
