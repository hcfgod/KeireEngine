#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("Malformed model import diagnostics identify the source asset instead of Assimp memory paths")
{
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.SourcePath = "Broken.glb";
    context.RelativePath = "Characters/Broken.glb";
    const std::vector<std::byte> bytes(32, std::byte{0x41});
    try
    {
        (void)Keire::CreateMeshAssetImporter().ContextualImport(context, bytes);
        FAIL("Malformed model was accepted");
    }
    catch (const std::invalid_argument& error)
    {
        const std::string_view message = error.what();
        CHECK(message.find("Characters/Broken.glb") != std::string_view::npos);
        CHECK(message.find("$$$___magic___$$$") == std::string_view::npos);
    }
}

TEST_CASE("downloaded rigging models preserve skeletons skinning clips and identity retargets" *
          doctest::skip(!std::filesystem::is_directory("Build/Validation/RiggingModels")))
{
    const auto root = std::filesystem::absolute("Build/Validation/RiggingModels");
    std::size_t modelCount = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(root))
    {
        if (file.path().extension() != ".glb")
            continue;
        CAPTURE(file.path().string());
        ++modelCount;
        std::ifstream stream(file.path(), std::ios::binary | std::ios::ate);
        REQUIRE(stream.is_open());
        const auto size = stream.tellg();
        REQUIRE(size > 0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        Keire::AssetImportContext context;
        context.Asset = Keire::AssetId::Generate();
        context.ProjectRoot = root;
        context.SourceRoot = root;
        context.SourcePath = file.path();
        context.RelativePath = std::filesystem::relative(file.path(), root);
        context.ImportSettings["materialImport"] = std::string("none");
        context.ImportSettings["rigSource"] = std::string("embedded");
        context.ImportSettings["rigProfile"] = std::string(file.path().stem() == "Fox"          ? "quadruped"
                                                           : file.path().stem() == "WolfSpider" ? "custom"
                                                                                                : "humanoid");
        if (file.path().stem() == "Fox")
        {
            auto invalidSettings = context;
            invalidSettings.ImportSettings["rigSource"] = std::string("generate");
            invalidSettings.ImportSettings["rigProfile"] = std::string("custom");
            CHECK_THROWS_WITH(Keire::CreateMeshAssetImporter().ContextualImport(invalidSettings, bytes),
                              "Custom mapping preserves an imported skeleton. Choose Keep imported skeleton, "
                              "or select a humanoid, biped, or quadruped profile to generate a skeleton.");
        }
        for (const auto skinning : {"linearBlend", "dualQuaternion"})
            for (const auto influences : {4, 8})
            {
                CAPTURE(skinning);
                CAPTURE(influences);
                context.ImportSettings["skinningMethod"] = std::string(skinning);
                context.ImportSettings["maximumInfluences"] = std::string(std::to_string(influences));
                std::map<std::string, Keire::AssetId> identities;
                context.ResolveSubAssetId = [&identities](const std::string_view key)
                { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
                const auto importer = Keire::CreateMeshAssetImporter();
                const auto imported = importer.ContextualImport(context, bytes);
                const auto repeated = importer.ContextualImport(context, bytes);
                CHECK(imported.Bytes == repeated.Bytes);
                REQUIRE(imported.SubAssets.size() == repeated.SubAssets.size());
                for (std::size_t index = 0; index < imported.SubAssets.size(); ++index)
                {
                    CHECK(imported.SubAssets[index].Id == repeated.SubAssets[index].Id);
                    CHECK(imported.SubAssets[index].Bytes == repeated.SubAssets[index].Bytes);
                }
                const auto skeletonData = std::ranges::find(imported.SubAssets, Keire::SkeletonAsset::StaticType(),
                                                            &Keire::AssetGeneratedSubAsset::Type);
                const auto rigData = std::ranges::find(imported.SubAssets, Keire::RigDefinitionAsset::StaticType(),
                                                       &Keire::AssetGeneratedSubAsset::Type);
                REQUIRE(skeletonData != imported.SubAssets.end());
                REQUIRE(rigData != imported.SubAssets.end());
                const auto skeleton = Keire::SkeletonAsset::Decode(skeletonData->Bytes);
                const auto rig = Keire::RigDefinitionAsset::Decode(rigData->Bytes);
                CHECK_NOTHROW(Keire::ValidateRigDefinition(rig->Definition()));
                CHECK(rig->Definition().Profile == (file.path().stem() == "Fox" ? Keire::RigProfileType::Quadruped
                                                    : file.path().stem() == "WolfSpider"
                                                        ? Keire::RigProfileType::Custom
                                                        : Keire::RigProfileType::Humanoid));
                REQUIRE_FALSE(skeleton->Bones().empty());
                if (file.path().stem() == "Fox")
                    CHECK(rig->Definition().Chains.size() >= 4);
                if (std::string_view(skinning) == "linearBlend" && influences == 4)
                {
                    std::vector<Keire::BoneTransform> bindPose;
                    for (const auto& bone : skeleton->Bones())
                        bindPose.push_back(bone.BindPose);
                    const auto matrices = [&](const auto& pose)
                    {
                        std::vector<Keire::Matrix4> result(pose.size());
                        for (std::size_t index = 0; index < pose.size(); ++index)
                        {
                            result[index] = Keire::Math::ComposeTransform(pose[index].Translation, pose[index].Rotation,
                                                                          pose[index].Scale);
                            const auto parent = skeleton->Bones()[index].Parent;
                            if (parent >= 0)
                                result[index] = Keire::Math::Multiply(result[parent], result[index]);
                        }
                        return result;
                    };
                    const auto distance = [](const Keire::Vector3 a, const Keire::Vector3 b)
                    {
                        return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) +
                                         (a.Z - b.Z) * (a.Z - b.Z));
                    };
                    const auto bind = matrices(bindPose);
                    std::size_t testedChains = 0;
                    for (std::uint32_t end = 0; end < bind.size(); ++end)
                    {
                        const auto middle = skeleton->Bones()[end].Parent;
                        if (middle < 0 || skeleton->Bones()[middle].Parent < 0)
                            continue;
                        const auto rootBone = static_cast<std::uint32_t>(skeleton->Bones()[middle].Parent);
                        const auto a = Keire::Math::TransformPoint(bind[rootBone], {});
                        const auto b = Keire::Math::TransformPoint(bind[middle], {});
                        const auto c = Keire::Math::TransformPoint(bind[end], {});
                        const auto upper = distance(a, b);
                        const auto lower = distance(b, c);
                        if (std::min(upper, lower) < 0.0001F)
                            continue;
                        CAPTURE(skeleton->Bones()[end].Name);
                        CAPTURE(skeleton->Bones()[rootBone].Name);
                        CAPTURE(bindPose[rootBone].Scale.X);
                        CAPTURE(bindPose[middle].Scale.X);
                        Keire::Vector3 probePosition, probeScale;
                        Keire::Quaternion probeRotation;
                        const bool rootDecomposes =
                            Keire::Math::DecomposeTransform(bind[rootBone], probePosition, probeRotation, probeScale);
                        const bool middleDecomposes =
                            Keire::Math::DecomposeTransform(bind[middle], probePosition, probeRotation, probeScale);
                        CAPTURE(rootDecomposes);
                        CAPTURE(middleDecomposes);
                        const float reach = upper + lower;
                        const Keire::Vector3 target{c.X + reach * 0.08F, c.Y - reach * 0.06F, c.Z + reach * 0.04F};
                        for (const bool fabrik : {false, true})
                        {
                            CAPTURE(fabrik);
                            auto pose = bindPose;
                            if (fabrik)
                                REQUIRE(Keire::SolveFabrikIk(*skeleton, pose,
                                                             {{rootBone, static_cast<std::uint32_t>(middle), end},
                                                              target,
                                                              128,
                                                              reach * 0.0001F,
                                                              1.0F}));
                            else
                                REQUIRE(Keire::SolveTwoBoneIk(*skeleton, pose,
                                                              {rootBone,
                                                               static_cast<std::uint32_t>(middle),
                                                               end,
                                                               target,
                                                               {b.X, b.Y, b.Z + reach},
                                                               1.0F}));
                            const auto solved = matrices(pose);
                            for (const auto& matrix : solved)
                                for (const auto element : matrix.Elements)
                                    REQUIRE(std::isfinite(element));
                            const auto endpoint = Keire::Math::TransformPoint(solved[end], {});
                            CHECK(distance(endpoint, target) <= distance(c, target) + reach * 0.01F);
                            CHECK(distance(Keire::Math::TransformPoint(solved[rootBone], {}), a) < reach * 0.001F);
                            for (int frame = 0; frame < 24; ++frame)
                            {
                                CAPTURE(frame);
                                const float phase = static_cast<float>(frame) * 0.3F;
                                const Keire::Vector3 movingTarget{c.X + reach * 0.15F * std::sin(phase),
                                                                  c.Y + reach * 0.12F * std::cos(phase),
                                                                  c.Z + reach * 0.1F * std::sin(phase * 0.7F)};
                                const auto before = matrices(pose);
                                const auto previousEnd = Keire::Math::TransformPoint(before[end], {});
                                if (fabrik)
                                    REQUIRE(Keire::SolveFabrikIk(*skeleton, pose,
                                                                 {{rootBone, static_cast<std::uint32_t>(middle), end},
                                                                  movingTarget,
                                                                  128,
                                                                  reach * 0.0001F,
                                                                  1.0F}));
                                else
                                    REQUIRE(Keire::SolveTwoBoneIk(*skeleton, pose,
                                                                  {rootBone,
                                                                   static_cast<std::uint32_t>(middle),
                                                                   end,
                                                                   movingTarget,
                                                                   {b.X, b.Y, b.Z + reach},
                                                                   1.0F}));
                                const auto current = matrices(pose);
                                for (const auto& matrix : current)
                                    for (const auto element : matrix.Elements)
                                        REQUIRE(std::isfinite(element));
                                const auto rootPosition = Keire::Math::TransformPoint(current[rootBone], {});
                                const auto middlePosition = Keire::Math::TransformPoint(current[middle], {});
                                const auto endPosition = Keire::Math::TransformPoint(current[end], {});
                                CHECK(distance(endPosition, movingTarget) <=
                                      distance(previousEnd, movingTarget) + reach * 0.01F);
                                CHECK(distance(rootPosition, a) < reach * 0.001F);
                                CHECK(std::abs(distance(rootPosition, middlePosition) - upper) < reach * 0.01F);
                                CHECK(std::abs(distance(middlePosition, endPosition) - lower) < reach * 0.01F);
                            }
                        }
                        ++testedChains;
                    }
                    CHECK(testedChains > 0);
                }
                std::size_t clips = 0;
                std::size_t skins = 0;
                for (const auto& asset : imported.SubAssets)
                {
                    CAPTURE(asset.Name);
                    if (asset.Type == Keire::SkinnedMeshAsset::StaticType())
                    {
                        const auto skin = Keire::SkinnedMeshAsset::Decode(asset.Bytes);
                        CHECK(skin->Skeleton() == skeletonData->Id);
                        CHECK(skin->Method() == (std::string_view(skinning) == "dualQuaternion"
                                                     ? Keire::SkinningMethod::DualQuaternion
                                                     : Keire::SkinningMethod::LinearBlend));
                        REQUIRE_FALSE(skin->Influences8().empty());
                        for (const auto& influence : skin->Influences8())
                        {
                            CHECK(influence.Count <= influences);
                            for (std::size_t index = 0; index < influence.Count; ++index)
                                REQUIRE(influence.Bones[index] < skeleton->Bones().size());
                            float sum = 0.0F;
                            for (const auto weight : influence.Weights)
                            {
                                REQUIRE(std::isfinite(weight));
                                REQUIRE(weight >= 0.0F);
                                sum += weight;
                            }
                            CHECK(sum == doctest::Approx(1.0F).epsilon(0.001));
                        }
                        ++skins;
                    }
                    if (asset.Type != Keire::AnimationClipAsset::StaticType())
                        continue;
                    ++clips;
                    const auto clip = Keire::AnimationClipAsset::Decode(asset.Bytes);
                    CHECK(clip->Skeleton() == skeletonData->Id);
                    REQUIRE(clip->Duration() > 0.0F);
                    if (file.path().stem() == "WolfSpider" && std::string_view(skinning) == "linearBlend" &&
                        influences == 4 && asset.Name == "Wolf Spider Armature|Spider walking")
                    {
                        // Independently evaluated glTF node, inverse-bind and first-key transforms, converted to LH.
                        const auto skinData =
                            std::ranges::find(imported.SubAssets, Keire::SkinnedMeshAsset::StaticType(),
                                              &Keire::AssetGeneratedSubAsset::Type);
                        REQUIRE(skinData != imported.SubAssets.end());
                        const auto skin = Keire::SkinnedMeshAsset::Decode(skinData->Bytes);
                        const auto mesh = Keire::MeshAsset::Decode(imported.Bytes);
                        std::vector<Keire::BoneTransform> pose;
                        for (const auto& bone : skeleton->Bones())
                            pose.push_back(bone.BindPose);
                        for (const auto& track : clip->Tracks())
                        {
                            REQUIRE_FALSE(track.Keys.empty());
                            REQUIRE(track.Keys.front().Time == 0.0F);
                            pose[track.Bone] = track.Keys.front().Value;
                        }
                        std::vector<Keire::Matrix4> world(pose.size()), palette(pose.size());
                        for (std::size_t index = 0; index < pose.size(); ++index)
                        {
                            world[index] = Keire::Math::ComposeTransform(pose[index].Translation, pose[index].Rotation,
                                                                         pose[index].Scale);
                            const auto& bone = skeleton->Bones()[index];
                            if (bone.Parent >= 0)
                                world[index] = Keire::Math::Multiply(world[bone.Parent], world[index]);
                            palette[index] = Keire::Math::Multiply(world[index], bone.InverseBindPose);
                        }
                        std::vector<Keire::MeshVertex> deformed(mesh->Vertices().size());
                        Keire::SkinMeshCpu(mesh->Vertices(), skin->Influences8(), palette,
                                           Keire::SkinningMethod::LinearBlend, deformed);
                        const float limit = std::numeric_limits<float>::max();
                        Keire::Vector3 minimum{limit, limit, limit}, maximum{-limit, -limit, -limit};
                        for (const auto& vertex : deformed)
                        {
                            minimum.X = std::min(minimum.X, vertex.Position.X);
                            minimum.Y = std::min(minimum.Y, vertex.Position.Y);
                            minimum.Z = std::min(minimum.Z, vertex.Position.Z);
                            maximum.X = std::max(maximum.X, vertex.Position.X);
                            maximum.Y = std::max(maximum.Y, vertex.Position.Y);
                            maximum.Z = std::max(maximum.Z, vertex.Position.Z);
                        }
                        CHECK(minimum.X == doctest::Approx(-0.01294915F).epsilon(0.001));
                        CHECK(minimum.Y == doctest::Approx(-0.00011764F).epsilon(0.001));
                        CHECK(minimum.Z == doctest::Approx(-0.02404215F).epsilon(0.001));
                        CHECK(maximum.X == doctest::Approx(0.01503824F).epsilon(0.001));
                        CHECK(maximum.Y == doctest::Approx(0.01042986F).epsilon(0.001));
                        CHECK(maximum.Z == doctest::Approx(0.00808613F).epsilon(0.001));
                    }
                    const auto result = Keire::RetargetAnimationClipWithDiagnostics(
                        *skeleton, rig->Definition(), *clip, skeletonData->Id, *skeleton, rig->Definition());
                    REQUIRE(result.Clip);
                    CHECK(result.Clip->Tracks().size() == clip->Tracks().size());
                    CHECK(result.Clip->Duration() == doctest::Approx(clip->Duration()));
                }
                CHECK(skins > 0);
                CHECK(clips > 0);
                if (file.path().stem() == "WolfSpider")
                    CHECK(clips == 7);
            }
    }
    CHECK(modelCount >= 3);
}
