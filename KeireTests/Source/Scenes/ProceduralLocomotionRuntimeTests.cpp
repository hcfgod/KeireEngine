#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr float FixedDeltaSeconds = 1.0F / 60.0F;

    [[nodiscard]] Keire::AssetImporterRegistration PassThroughImporter(std::string name, const Keire::AssetTypeId type,
                                                                       std::string extension)
    {
        Keire::AssetImporterRegistration result;
        result.Name = std::move(name);
        result.Type = type;
        result.Extensions.push_back(std::move(extension));
        result.Import = [](const std::span<const std::byte> bytes)
        { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
        return result;
    }

    [[nodiscard]] Keire::MeshAsset BoxMesh()
    {
        std::vector<Keire::MeshVertex> vertices{{{-1.0F, 0.0F, -0.5F}}, {{1.0F, 0.0F, -0.5F}}, {{-1.0F, 2.0F, -0.5F}},
                                                {{1.0F, 2.0F, -0.5F}},  {{-1.0F, 0.0F, 0.5F}}, {{1.0F, 0.0F, 0.5F}},
                                                {{-1.0F, 2.0F, 0.5F}},  {{1.0F, 2.0F, 0.5F}}};
        std::vector<std::uint32_t> indices{0, 1, 2, 1, 3, 2, 4, 6, 5, 5, 6, 7};
        return Keire::MeshAsset(std::move(vertices), std::move(indices), {{-1.0F, 0.0F, -0.5F}, {1.0F, 2.0F, 0.5F}});
    }

    class ProceduralRuntimeFixture final
    {
      public:
        explicit ProceduralRuntimeFixture(const bool rotatedImportRoot = false)
            : m_Root(std::filesystem::temp_directory_path() /
                     ("keire-procedural-runtime-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(m_Root / "Assets");

            auto generated = Keire::GenerateRig(BoxMesh(), {});
            if (rotatedImportRoot)
            {
                const auto rotation = Keire::Math::EulerDegreesToQuaternion({90.0F, 0.0F, 0.0F});
                const auto basis = Keire::Math::ComposeTransform({}, rotation, {1.0F, 1.0F, 1.0F});
                const auto inverse = Keire::Math::Inverse(basis);
                for (std::size_t index = 0; index < generated.Skeleton.size(); ++index)
                {
                    auto& bone = generated.Skeleton[index];
                    auto local = Keire::Math::Multiply(Keire::Math::ComposeTransform(bone.BindPose.Translation,
                                                                                     bone.BindPose.Rotation,
                                                                                     bone.BindPose.Scale),
                                                       basis);
                    if (bone.Parent >= 0)
                        local = Keire::Math::Multiply(inverse, local);
                    REQUIRE(Keire::Math::DecomposeTransform(local, bone.BindPose.Translation, bone.BindPose.Rotation,
                                                            bone.BindPose.Scale));
                    bone.InverseBindPose = Keire::Math::Multiply(inverse, bone.InverseBindPose);
                    generated.Rig.Bones[index].BindPose = bone.BindPose;
                }
            }
            m_Rig = generated.Rig;
            Write("Humanoid.testskeleton", Keire::SkeletonAsset::Encode(generated.Skeleton));
            Write("Humanoid.testrig", Keire::RigDefinitionAsset::Encode(generated.Rig));

            auto profile = Keire::ProceduralMotionProfile::GroundedArmored();
            profile.VelocityResponseTime = 0.12F;
            profile.FacingResponseTime = 0.10F;
            profile.PoseResponseTime = 0.08F;
            profile.GroundingResponseTime = 0.04F;
            profile.PreLandingProbeTime = 0.35F;
            Write("Humanoid.testmotion", Keire::ProceduralMotionProfileAsset::Encode(profile));

            Keire::AssetDatabaseSpecification databaseSpecification;
            databaseSpecification.ProjectRoot = m_Root;
            databaseSpecification.Importers.push_back(
                PassThroughImporter("Test.Skeleton", Keire::SkeletonAsset::StaticType(), ".testskeleton"));
            databaseSpecification.Importers.push_back(
                PassThroughImporter("Test.Rig", Keire::RigDefinitionAsset::StaticType(), ".testrig"));
            databaseSpecification.Importers.push_back(PassThroughImporter(
                "Test.ProceduralMotion", Keire::ProceduralMotionProfileAsset::StaticType(), ".testmotion"));
            m_Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
            const auto imported = m_Database->ImportAll();
            const auto skeletonRecord = m_Database->Find("Humanoid.testskeleton");
            const auto rigRecord = m_Database->Find("Humanoid.testrig");
            const auto profileRecord = m_Database->Find("Humanoid.testmotion");
            REQUIRE(skeletonRecord);
            REQUIRE(rigRecord);
            REQUIRE(profileRecord);

            Keire::AssetSystemSpecification assetSpecification;
            assetSpecification.Mode = Keire::AssetMode::Development;
            assetSpecification.DevelopmentCatalog = imported.CatalogPath;
            assetSpecification.WorkerCount = 1;
            assetSpecification.Decoders.push_back(Keire::CreateSkeletonAssetDecoder());
            assetSpecification.Decoders.push_back(Keire::CreateRigDefinitionAssetDecoder());
            assetSpecification.Decoders.push_back(Keire::CreateProceduralMotionProfileAssetDecoder());
            m_Assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));

            Keire::PhysicsSystemSpecification physicsSpecification;
            physicsSpecification.Mode = Keire::PhysicsMode::Enabled;
            m_Physics = Keire::CreateRef<Keire::PhysicsSystem>(physicsSpecification);
            m_Scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                     Keire::SceneAsset::EmptyDefinition("Procedural locomotion"));

            auto floor = m_Scene->CreateEntity("Floor");
            floor.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, -0.5F, 0.0F});
            floor.AddComponent<Keire::ColliderComponent>()->SetHalfExtent({20.0F, 0.5F, 20.0F});

            auto character = m_Scene->CreateEntity("Character");
            m_Character = character.Id();
            character.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 1.0F, 0.0F});
            character.AddComponent<Keire::CharacterControllerComponent>()->ConfigureCapsule(0.35F, 1.8F, 0.35F, 0.04F);
            const auto animator = character.AddComponent<Keire::AnimatorComponent>();
            animator->SetPoseSource(Keire::AnimatorPoseSource::ProceduralHumanoid);
            animator->SetSkeleton(skeletonRecord->Id);
            animator->SetProceduralProfile(profileRecord->Id);
            animator->SetRigDefinition(rigRecord->Id);
            animator->SetProceduralQuality(Keire::ProceduralMotionQuality::High);

            m_Session = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Scene, m_Assets,
                                                                     Keire::Ref<Keire::AudioSystem>{}, m_Physics);
            m_Session->Play();
            REQUIRE(m_Session->State() == Keire::ScenePlayState::Playing);
            m_RuntimeCharacter = m_Session->RuntimeScene()->FindEntity(m_Character);
            REQUIRE(m_RuntimeCharacter);
            m_RuntimeAnimator = m_RuntimeCharacter.GetComponent<Keire::AnimatorComponent>();
            m_RuntimeController = m_RuntimeCharacter.GetComponent<Keire::CharacterControllerComponent>();
            REQUIRE(m_RuntimeAnimator);
            REQUIRE(m_RuntimeController);

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            float settlingVelocity = 0.0F;
            while ((m_RuntimeAnimator->SkinPalette().empty() || !m_RuntimeController->Grounded() ||
                    m_RuntimeAnimator->ProceduralState().State == Keire::ProceduralMotionState::Landing) &&
                   std::chrono::steady_clock::now() < deadline)
            {
                // Character controllers consume requested movement; they do not integrate gravity themselves.
                // Reach the floor instead of depending on a ground probe to classify an airborne capsule as grounded.
                settlingVelocity =
                    m_RuntimeController->Grounded() ? 0.0F : settlingVelocity - 9.81F * FixedDeltaSeconds;
                Tick({0.0F, settlingVelocity * FixedDeltaSeconds, 0.0F}, {0.0F, 0.0F, 1.0F});
                (void)m_Assets->PumpCompletions();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE_FALSE(m_RuntimeAnimator->SkinPalette().empty());
            REQUIRE(m_RuntimeController->Grounded());
            CHECK(m_RuntimeCharacter.GetComponent<Keire::TransformComponent>()->LocalPosition().Y ==
                  doctest::Approx(0.9F).epsilon(0.005));
            REQUIRE(m_RuntimeAnimator->ProceduralState().State == Keire::ProceduralMotionState::Idle);
        }

        ~ProceduralRuntimeFixture()
        {
            if (m_Session)
                m_Session->Stop();
            if (m_Physics)
                m_Physics->Close();
            if (m_Scene)
                m_Scene->Close();
            if (m_Assets)
                m_Assets->Close();
            m_RuntimeController = {};
            m_RuntimeAnimator = {};
            m_RuntimeCharacter = {};
            m_Session = {};
            m_Scene = {};
            m_Physics = {};
            m_Assets = {};
            m_Database = {};
            std::error_code ignored;
            std::filesystem::remove_all(m_Root, ignored);
        }

        ProceduralRuntimeFixture(const ProceduralRuntimeFixture&) = delete;
        ProceduralRuntimeFixture& operator=(const ProceduralRuntimeFixture&) = delete;

        void Tick(const Keire::Vector3 displacement, const Keire::Vector3 facing, const bool jump = false)
        {
            REQUIRE(m_RuntimeController->QueueDesiredMovement(displacement));
            const Keire::Vector3 desiredVelocity{displacement.X / FixedDeltaSeconds, displacement.Y / FixedDeltaSeconds,
                                                 displacement.Z / FixedDeltaSeconds};
            m_RuntimeAnimator->SetProceduralLocomotion({desiredVelocity, facing, {}, 0.0F, 0.0F, jump});
            m_Session->FixedUpdate(FixedDeltaSeconds);
            m_Session->Update(FixedDeltaSeconds, 1.0F);
            REQUIRE(m_Session->State() == Keire::ScenePlayState::Playing);
        }

        [[nodiscard]] const Keire::AnimatorPoseBoneDebugState& Bone(const Keire::RigBoneSemantic semantic) const
        {
            const auto rigBone = std::ranges::find(m_Rig.Bones, semantic, &Keire::RigBoneDefinition::Semantic);
            REQUIRE(rigBone != m_Rig.Bones.end());
            const auto snapshot = m_RuntimeAnimator->RuntimeDebugSnapshot();
            REQUIRE(snapshot);
            const auto debugBone =
                std::ranges::find(snapshot->Pose, rigBone->Name, &Keire::AnimatorPoseBoneDebugState::Name);
            REQUIRE(debugBone != snapshot->Pose.end());
            return *debugBone;
        }

        [[nodiscard]] static float RotationDot(const Keire::Quaternion left, const Keire::Quaternion right) noexcept
        {
            return std::abs(left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W);
        }

        [[nodiscard]] Keire::Ref<Keire::AnimatorComponent> Animator() const noexcept { return m_RuntimeAnimator; }
        [[nodiscard]] Keire::Ref<Keire::CharacterControllerComponent> Controller() const noexcept
        {
            return m_RuntimeController;
        }

      private:
        void Write(const std::filesystem::path& relative, const std::span<const std::byte> bytes) const
        {
            const auto path = m_Root / "Assets" / relative;
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }

        std::filesystem::path m_Root;
        Keire::RigDefinition m_Rig;
        Keire::Ref<Keire::AssetDatabase> m_Database;
        Keire::Ref<Keire::AssetSystem> m_Assets;
        Keire::Ref<Keire::PhysicsSystem> m_Physics;
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::SceneRuntimeSession> m_Session;
        Keire::EntityId m_Character;
        Keire::Entity m_RuntimeCharacter;
        Keire::Ref<Keire::AnimatorComponent> m_RuntimeAnimator;
        Keire::Ref<Keire::CharacterControllerComponent> m_RuntimeController;
    };
} // namespace

TEST_CASE("Scene runtime procedural locomotion consumes facing and accepts the complete airborne lifecycle")
{
    ProceduralRuntimeFixture fixture;
    const auto animator = fixture.Animator();

    const auto idlePelvisRotation = fixture.Bone(Keire::RigBoneSemantic::Pelvis).LocalTransform.Rotation;
    for (int tick = 0; tick < 20; ++tick)
        fixture.Tick({}, {1.0F, 0.0F, 0.0F});
    CHECK(animator->ProceduralState().State == Keire::ProceduralMotionState::TurnInPlace);
    CHECK(ProceduralRuntimeFixture::RotationDot(
              idlePelvisRotation, fixture.Bone(Keire::RigBoneSemantic::Pelvis).LocalTransform.Rotation) < 0.999F);

    const auto initialPhase = animator->ProceduralState().GaitPhase;
    for (int tick = 0; tick < 12; ++tick)
        fixture.Tick({0.0F, 0.0F, 2.8F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    CHECK(animator->ProceduralState().State == Keire::ProceduralMotionState::Locomotion);
    CHECK(animator->ProceduralState().Speed == doctest::Approx(2.8F).epsilon(0.05));
    CHECK(animator->ProceduralState().GaitPhase != doctest::Approx(initialPhase));
    CHECK(animator->ProceduralState().ActualWorldVelocity.Z == doctest::Approx(2.8F).epsilon(0.05));

    for (int tick = 0; tick < 30 && !fixture.Controller()->Grounded(); ++tick)
        fixture.Tick({}, {0.0F, 0.0F, 1.0F});
    auto verticalSpeed = 6.0F;
    fixture.Tick({0.0F, verticalSpeed * FixedDeltaSeconds, 0.0F}, {0.0F, 0.0F, 1.0F}, true);
    verticalSpeed -= 9.81F * FixedDeltaSeconds;

    bool observedTakeoff = animator->ProceduralState().State == Keire::ProceduralMotionState::Takeoff;
    bool observedRising = false;
    bool observedFalling = false;
    bool observedLanding = false;
    float earlyFallingFeet = 0.0F;
    float lateFallingFeet = 0.0F;
    bool capturedEarlyFalling = false;
    float landingIntensity = 0.0F;
    for (int tick = 0; tick < 360 && !observedLanding; ++tick)
    {
        fixture.Tick({0.0F, verticalSpeed * FixedDeltaSeconds, 0.0F}, {0.0F, 0.0F, 1.0F});
        verticalSpeed -= 9.81F * FixedDeltaSeconds;
        const auto state = animator->ProceduralState();
        observedTakeoff = observedTakeoff || state.State == Keire::ProceduralMotionState::Takeoff;
        observedRising = observedRising || state.State == Keire::ProceduralMotionState::Rising;
        observedFalling = observedFalling || state.State == Keire::ProceduralMotionState::Falling;
        if (state.State == Keire::ProceduralMotionState::Falling)
        {
            const auto feet = fixture.Bone(Keire::RigBoneSemantic::LeftFoot).WorldPosition.Y +
                              fixture.Bone(Keire::RigBoneSemantic::RightFoot).WorldPosition.Y;
            if (!capturedEarlyFalling)
            {
                earlyFallingFeet = feet;
                capturedEarlyFalling = true;
            }
            lateFallingFeet = feet;
        }
        if (state.State == Keire::ProceduralMotionState::Landing)
        {
            observedLanding = true;
            landingIntensity = state.LandingIntensity;
        }
    }

    CHECK(observedTakeoff);
    CHECK(observedRising);
    CHECK(observedFalling);
    CHECK(observedLanding);
    CHECK(capturedEarlyFalling);
    CHECK(lateFallingFeet < earlyFallingFeet);
    CHECK(landingIntensity > 0.0F);
}

TEST_CASE("Scene runtime procedural artifacts reuse warmed storage without reallocating")
{
    ProceduralRuntimeFixture fixture;
    const auto animator = fixture.Animator();

    // A reallocation changes these addresses, making warmed artifact storage reuse directly observable.
    fixture.Tick({0.0F, 0.0F, 2.0F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    const auto paletteData = animator->SkinPalette().data();
    auto first = animator->RuntimeDebugSnapshot();
    REQUIRE(first);
    const auto* firstSnapshot = first.get();
    const auto* firstPoseData = first->Pose.data();
    first.reset();

    fixture.Tick({0.0F, 0.0F, 2.0F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    CHECK(animator->SkinPalette().data() == paletteData);
    auto second = animator->RuntimeDebugSnapshot();
    REQUIRE(second);
    CHECK(second.get() != firstSnapshot);
    second.reset();

    fixture.Tick({0.0F, 0.0F, 2.0F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    CHECK(animator->SkinPalette().data() == paletteData);
    const auto third = animator->RuntimeDebugSnapshot();
    REQUIRE(third);
    CHECK(third.get() == firstSnapshot);
    CHECK(third->Pose.data() == firstPoseData);

    const auto retainedRevision = third->Revision;
    const auto retainedPelvis = third->Pose.front().LocalTransform;
    fixture.Tick({0.0F, 0.0F, 2.0F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    fixture.Tick({0.0F, 0.0F, 2.0F * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
    CHECK(third->Revision == retainedRevision);
    CHECK(third->Pose.front().LocalTransform == retainedPelvis);
}

TEST_CASE("Scene runtime procedural motion is invariant under imported bone coordinate bases")
{
    ProceduralRuntimeFixture canonical;
    ProceduralRuntimeFixture imported(true);
    for (int tick = 0; tick < 150; ++tick)
    {
        const Keire::Vector3 displacement =
            tick < 90 ? Keire::Vector3{0.0F, 0.0F, 1.4F * FixedDeltaSeconds} : Keire::Vector3{};
        const Keire::Vector3 facing = tick > 110 ? Keire::Vector3{1.0F, 0.0F, 0.0F} : Keire::Vector3{0.0F, 0.0F, 1.0F};
        canonical.Tick(displacement, facing);
        imported.Tick(displacement, facing);
        CHECK(canonical.Animator()->ProceduralState().State == imported.Animator()->ProceduralState().State);
        for (const auto semantic :
             {Keire::RigBoneSemantic::Pelvis, Keire::RigBoneSemantic::Head, Keire::RigBoneSemantic::LeftHand,
              Keire::RigBoneSemantic::RightHand, Keire::RigBoneSemantic::LeftFoot, Keire::RigBoneSemantic::RightFoot})
        {
            CAPTURE(tick);
            CAPTURE(static_cast<int>(semantic));
            const auto expected = canonical.Bone(semantic).WorldPosition;
            const auto actual = imported.Bone(semantic).WorldPosition;
            CHECK(actual.X == doctest::Approx(expected.X).epsilon(0.002));
            CHECK(actual.Y == doctest::Approx(expected.Y).epsilon(0.002));
            CHECK(actual.Z == doctest::Approx(expected.Z).epsilon(0.002));
        }
    }
}

TEST_CASE("Scene runtime procedural pose smoothing preserves final IK constraints")
{
    ProceduralRuntimeFixture fixture(true);
    const auto upper = fixture.Bone(Keire::RigBoneSemantic::LeftUpperArm);
    const auto lower = fixture.Bone(Keire::RigBoneSemantic::LeftLowerArm);
    const auto hand = fixture.Bone(Keire::RigBoneSemantic::LeftHand);
    const Keire::Vector3 target{hand.WorldPosition.X, hand.WorldPosition.Y + 0.12F, hand.WorldPosition.Z + 0.08F};
    fixture.Animator()->SetTwoBoneIk("contact-order", upper.Name, lower.Name, hand.Name, target, lower.WorldPosition,
                                     1.0F, Keire::AnimatorIkSpace::Model);
    fixture.Tick({}, {0.0F, 0.0F, 1.0F});
    const auto actual = fixture.Bone(Keire::RigBoneSemantic::LeftHand).WorldPosition;
    CHECK(actual.X == doctest::Approx(target.X).epsilon(0.001));
    CHECK(actual.Y == doctest::Approx(target.Y).epsilon(0.001));
    CHECK(actual.Z == doctest::Approx(target.Z).epsilon(0.001));
}

TEST_CASE("Scene runtime procedural knees retain anatomical bend during reverse and lateral travel")
{
    ProceduralRuntimeFixture fixture;
    for (const auto direction : {Keire::Vector3{0.0F, 0.0F, 1.0F}, Keire::Vector3{0.0F, 0.0F, -1.0F},
                                 Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{-1.0F, 0.0F, 0.0F}})
    {
        for (int tick = 0; tick < 90; ++tick)
        {
            fixture.Tick({direction.X * FixedDeltaSeconds, 0.0F, direction.Z * FixedDeltaSeconds}, {0.0F, 0.0F, 1.0F});
            for (const bool left : {true, false})
            {
                const auto hip =
                    fixture.Bone(left ? Keire::RigBoneSemantic::LeftUpperLeg : Keire::RigBoneSemantic::RightUpperLeg)
                        .WorldPosition;
                const auto knee =
                    fixture.Bone(left ? Keire::RigBoneSemantic::LeftLowerLeg : Keire::RigBoneSemantic::RightLowerLeg)
                        .WorldPosition;
                const auto foot =
                    fixture.Bone(left ? Keire::RigBoneSemantic::LeftFoot : Keire::RigBoneSemantic::RightFoot)
                        .WorldPosition;
                CAPTURE(tick);
                CAPTURE(left);
                CHECK(knee.Z >= (hip.Z + foot.Z) * 0.5F - 0.005F);
            }
        }
    }
}
