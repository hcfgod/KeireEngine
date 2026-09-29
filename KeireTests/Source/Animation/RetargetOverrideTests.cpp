#include "Keire/Animation/RiggingSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

TEST_CASE("Manual retarget mappings repair unmatched tracks and reject ambiguous bindings")
{
    const Keire::SkeletonAsset source(
        {{"Root", -1, {}}, {"AuthoredArm", 0, {{1.0F, 0.0F, 0.0F}}}, {"AutomaticArm", 0, {{1.0F, 0.0F, 0.0F}}}});
    const Keire::SkeletonAsset target({{"Root", -1, {}}, {"AutomaticArm", 0, {{2.0F, 0.0F, 0.0F}}}});
    const auto sourceRig = Keire::InferRigDefinition(source, Keire::RigProfileType::Custom);
    const auto targetRig = Keire::InferRigDefinition(target, Keire::RigProfileType::Custom);
    const Keire::AnimationClipAsset clip(Keire::AssetId::Generate(), 1.0F,
                                         {{1, {{0.0F, {{1.0F, 1.0F, 0.0F}}}}}, {2, {{0.0F, {{1.0F, 0.0F, 0.0F}}}}}});
    const std::array bindings{Keire::AnimationRetargetOverride{"AuthoredArm", "AutomaticArm"}};
    const auto automatic = Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig);
    REQUIRE(automatic.Mappings.size() == 2);
    CHECK_FALSE(automatic.Mappings[0].TargetBone);
    CHECK(automatic.Mappings[1].TargetBone == 1);
    const auto result = Keire::RetargetAnimationClipWithDiagnostics(source, sourceRig, clip, Keire::AssetId::Generate(),
                                                                    target, targetRig, bindings);
    CHECK(result.Diagnostics.ManualMatchCount == 1);
    CHECK(result.Diagnostics.MappedTrackCount == 1);
    CHECK(result.Diagnostics.Mappings[0].Match == Keire::AnimationRetargetMatch::Manual);
    CHECK(result.Diagnostics.Mappings[1].Match == Keire::AnimationRetargetMatch::TargetConflict);
    REQUIRE(result.Clip->Tracks().size() == 1);
    CHECK(result.Clip->Tracks()[0].Bone == 1);
    CHECK(result.Clip->Tracks()[0].Keys[0].Value.Translation.Y == doctest::Approx(2.0F));

    for (const auto& invalid : {Keire::AnimationRetargetOverride{"Missing", "AutomaticArm"},
                                Keire::AnimationRetargetOverride{"AuthoredArm", "Missing"}})
    {
        const std::array invalidBindings{invalid};
        CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target,
                                                                              targetRig, invalidBindings)),
                        std::invalid_argument);
    }
    const std::array duplicateSource{bindings[0], bindings[0]};
    CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig,
                                                                          duplicateSource)),
                    std::invalid_argument);
    const std::array duplicateTarget{bindings[0], Keire::AnimationRetargetOverride{"AutomaticArm", "AutomaticArm"}};
    CHECK_THROWS_AS(static_cast<void>(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig,
                                                                          duplicateTarget)),
                    std::invalid_argument);
    CHECK(Keire::DiagnoseAnimationRetargeting(source, sourceRig, clip, target, targetRig).ManualMatchCount == 0);
}
