#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    std::vector<Keire::Matrix4> QuadrupedMatrices(const Keire::SkeletonAsset& skeleton,
                                                  const std::span<const Keire::BoneTransform> pose)
    {
        std::vector<Keire::Matrix4> result(pose.size());
        for (std::size_t index = 0; index < pose.size(); ++index)
        {
            result[index] =
                Keire::Math::ComposeTransform(pose[index].Translation, pose[index].Rotation, pose[index].Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            if (parent >= 0)
                result[index] = Keire::Math::Multiply(result[parent], result[index]);
        }
        return result;
    }

    float QuadrupedDistance(const Keire::Vector3 a, const Keire::Vector3 b)
    {
        return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
    }
} // namespace

TEST_CASE("downloaded quadruped animated limbs preserve lengths and recover across solver changes" *
          doctest::skip(!std::filesystem::is_regular_file("Build/Validation/RiggingModels/Fox/Fox.glb")))
{
    const auto path = std::filesystem::absolute("Build/Validation/RiggingModels/Fox/Fox.glb");
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
    context.ImportSettings["rigProfile"] = std::string("quadruped");
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto imported = Keire::CreateMeshAssetImporter().ContextualImport(context, bytes);
    const auto data =
        std::ranges::find(imported.SubAssets, Keire::SkeletonAsset::StaticType(), &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(data != imported.SubAssets.end());
    const auto skeleton = Keire::SkeletonAsset::Decode(data->Bytes);

    std::vector<std::vector<std::uint32_t>> chains;
    for (const std::string_view endName : {"b_RightHand_08", "b_LeftHand_011", "b_LeftFoot01_017", "b_RightFoot01_021"})
    {
        const auto end =
            std::ranges::find(skeleton->Bones(), endName, [](const auto& bone) { return std::string_view(bone.Name); });
        REQUIRE(end != skeleton->Bones().end());
        const auto endIndex = static_cast<std::uint32_t>(end - skeleton->Bones().begin());
        REQUIRE(end->Parent >= 0);
        const auto middle = static_cast<std::uint32_t>(end->Parent);
        REQUIRE(skeleton->Bones()[middle].Parent >= 0);
        chains.push_back({static_cast<std::uint32_t>(skeleton->Bones()[middle].Parent), middle, endIndex});
    }
    std::size_t clips = 0;
    for (const auto& asset : imported.SubAssets)
    {
        if (asset.Type != Keire::AnimationClipAsset::StaticType())
            continue;
        CAPTURE(asset.Name);
        const auto clip = Keire::AnimationClipAsset::Decode(asset.Bytes);
        Keire::AnimationGraphDefinition graph;
        graph.EntryState = "Creature";
        graph.States = {{"Creature", asset.Id}};
        const auto otherAsset = std::ranges::find_if(
            imported.SubAssets, [&](const auto& candidate)
            { return candidate.Type == Keire::AnimationClipAsset::StaticType() && candidate.Id != asset.Id; });
        REQUIRE(otherAsset != imported.SubAssets.end());
        const auto otherClip = Keire::AnimationClipAsset::Decode(otherAsset->Bytes);
        graph.States.push_back({"Other", otherAsset->Id});
        Keire::AnimatorInstance animator(skeleton, Keire::CreateRef<Keire::AnimationGraphAsset>(graph),
                                         [clip, otherClip, id = asset.Id](Keire::AssetId requested)
                                         { return requested == id ? clip : otherClip; });
        bool sawTransition = false;
        for (int frame = 0; frame < 192; ++frame)
        {
            CAPTURE(frame);
            if (frame == 96)
                animator.CrossFade("Other", clip->Duration() * 0.25F);
            if (frame == 144)
                animator.CrossFade("Creature", clip->Duration() * 0.25F);
            const auto sampledPose = animator.Update(clip->Duration() / 40.0F).LocalPose;
            if (const auto snapshot = animator.DebugSnapshot())
                for (const auto& layer : snapshot->Layers)
                    sawTransition = sawTransition || layer.InTransition;
            const auto sampled = QuadrupedMatrices(*skeleton, sampledPose);
            for (const bool fabrik : {false, true})
            {
                CAPTURE(fabrik);
                const float ramp = static_cast<float>(frame % 48) / 47.0F;
                for (const float weight : {0.0F, 0.25F, 0.5F, 1.0F, ramp})
                {
                    CAPTURE(weight);
                    auto pose = sampledPose;
                    for (const auto& chain : chains)
                    {
                        CAPTURE(skeleton->Bones()[chain.front()].Name);
                        const auto root = Keire::Math::TransformPoint(sampled[chain[0]], {});
                        const auto middle = Keire::Math::TransformPoint(sampled[chain[1]], {});
                        const auto foot = Keire::Math::TransformPoint(sampled[chain[2]], {});
                        const float upper = QuadrupedDistance(root, middle);
                        const float lower = QuadrupedDistance(middle, foot);
                        const float reach = upper + lower;
                        REQUIRE(reach > 0.0F);
                        const float phase = static_cast<float>(frame) * 0.2F;
                        const Keire::Vector3 target{foot.X + reach * 0.05F * std::sin(phase),
                                                    foot.Y + reach * 0.05F * std::cos(phase), foot.Z};
                        const auto before = pose;
                        const auto solve = [&](Keire::Vector3 requested)
                        {
                            if (fabrik)
                                return Keire::SolveFabrikIk(*skeleton, pose,
                                                            {chain, requested, 128, reach * 0.0001F, weight});
                            return Keire::SolveTwoBoneIk(*skeleton, pose,
                                                         {chain[0], chain[1], chain[2], requested, middle, weight});
                        };
                        if (frame == 47)
                        {
                            auto invalid = target;
                            invalid.Y = std::numeric_limits<float>::quiet_NaN();
                            CHECK_FALSE(solve(invalid));
                            CHECK(pose == before);
                        }
                        REQUIRE(solve(target));
                        const auto after = QuadrupedMatrices(*skeleton, pose);
                        if (weight == 0.0F)
                            CHECK(pose == before);
                        if (weight == 1.0F)
                            CHECK(QuadrupedDistance(Keire::Math::TransformPoint(after[chain[2]], {}), target) <=
                                  QuadrupedDistance(foot, target) + reach * 0.001F);
                        CHECK(QuadrupedDistance(Keire::Math::TransformPoint(after[chain[0]], {}), root) <
                              reach * 0.001F);
                        CHECK(std::abs(QuadrupedDistance(Keire::Math::TransformPoint(after[chain[0]], {}),
                                                         Keire::Math::TransformPoint(after[chain[1]], {})) -
                                       upper) < reach * 0.001F);
                        CHECK(std::abs(QuadrupedDistance(Keire::Math::TransformPoint(after[chain[1]], {}),
                                                         Keire::Math::TransformPoint(after[chain[2]], {})) -
                                       lower) < reach * 0.001F);
                        for (std::size_t index = 0; index < pose.size(); ++index)
                        {
                            for (const auto value : after[index].Elements)
                                REQUIRE(std::isfinite(value));
                            if (std::ranges::find(chain, index) == chain.end())
                                CHECK(pose[index] == before[index]);
                        }
                    }
                }
            }
        }
        CHECK(sawTransition);
        ++clips;
    }
    CHECK(clips == 3);
}
