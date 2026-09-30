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
    std::vector<Keire::Matrix4> SpiderMatrices(const Keire::SkeletonAsset& skeleton,
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

    float SpiderDistance(const Keire::Vector3 a, const Keire::Vector3 b)
    {
        return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
    }
} // namespace

TEST_CASE("FABRIK zero weight preserves authored rotations exactly and still rejects invalid chains")
{
    const Keire::SkeletonAsset skeleton({{"Root", -1, {{}, {0.2F, 0.3F, 0.1F, 0.9F}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"Middle", 0, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}},
                                         {"End", 1, {{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {}}});
    for (const float weight : {0.0F, -1.0F})
    {
        CAPTURE(weight);
        std::vector<Keire::BoneTransform> pose;
        for (const auto& bone : skeleton.Bones())
            pose.push_back(bone.BindPose);
        const auto original = pose;
        Keire::FabrikIkRequest request{{0, 1, 2}, {1.0F, 1.0F, 0.0F}, 12, 0.001F, weight};
        REQUIRE(Keire::SolveFabrikIk(skeleton, pose, request));
        for (std::size_t index = 0; index < pose.size(); ++index)
        {
            CHECK(pose[index].Translation == original[index].Translation);
            CHECK(pose[index].Rotation == original[index].Rotation);
            CHECK(pose[index].Scale == original[index].Scale);
        }
        request.Chain = {0, 2};
        CHECK_FALSE(Keire::SolveFabrikIk(skeleton, pose, request));
    }
}

TEST_CASE("downloaded spider full leg chains track moving goals and recover after invalid input" *
          doctest::skip(!std::filesystem::is_regular_file("Build/Validation/RiggingModels/WolfSpider/WolfSpider.glb")))
{
    const auto path = std::filesystem::absolute("Build/Validation/RiggingModels/WolfSpider/WolfSpider.glb");
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
    context.ImportSettings["rigProfile"] = std::string("custom");
    std::map<std::string, Keire::AssetId> identities;
    context.ResolveSubAssetId = [&identities](const std::string_view key)
    { return identities.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto imported = Keire::CreateMeshAssetImporter().ContextualImport(context, bytes);
    const auto data =
        std::ranges::find(imported.SubAssets, Keire::SkeletonAsset::StaticType(), &Keire::AssetGeneratedSubAsset::Type);
    REQUIRE(data != imported.SubAssets.end());
    const auto skeleton = Keire::SkeletonAsset::Decode(data->Bytes);
    std::vector<Keire::BoneTransform> pose;
    for (const auto& bone : skeleton->Bones())
        pose.push_back(bone.BindPose);
    const auto bind = SpiderMatrices(*skeleton, pose);
    std::vector<std::vector<std::uint32_t>> chains;
    for (std::uint32_t end = 0; end < skeleton->Bones().size(); ++end)
    {
        const auto& name = skeleton->Bones()[end].Name;
        if (!name.starts_with("LegSegmentF.") || name.find("_end_") == std::string::npos)
            continue;
        std::vector<std::uint32_t> chain;
        for (auto bone = static_cast<std::int32_t>(end); bone >= 0; bone = skeleton->Bones()[bone].Parent)
        {
            chain.push_back(static_cast<std::uint32_t>(bone));
            if (skeleton->Bones()[bone].Name.starts_with("LegSegmentA."))
                break;
        }
        REQUIRE(skeleton->Bones()[chain.back()].Name.starts_with("LegSegmentA."));
        std::ranges::reverse(chain);
        REQUIRE(chain.size() >= 5);
        chains.push_back(std::move(chain));
    }
    REQUIRE(chains.size() == 8);
    for (int frame = 0; frame < 48; ++frame)
    {
        CAPTURE(frame);
        for (const auto& chain : chains)
        {
            CAPTURE(skeleton->Bones()[chain.front()].Name);
            float reach = 0.0F;
            for (std::size_t index = 1; index < chain.size(); ++index)
                reach += SpiderDistance(Keire::Math::TransformPoint(bind[chain[index - 1]], {}),
                                        Keire::Math::TransformPoint(bind[chain[index]], {}));
            REQUIRE(reach > 0.0F);
            const auto origin = Keire::Math::TransformPoint(bind[chain.front()], {});
            const auto foot = Keire::Math::TransformPoint(bind[chain.back()], {});
            const float phase = static_cast<float>(frame) * 0.2F;
            const Keire::Vector3 target{foot.X + reach * 0.08F * std::sin(phase),
                                        foot.Y + reach * 0.08F * std::cos(phase),
                                        foot.Z + reach * 0.06F * std::sin(phase * 0.7F)};
            const auto before = SpiderMatrices(*skeleton, pose);
            Keire::FabrikIkRequest request{chain, target, 128, reach * 0.0001F, 1.0F};
            if (frame == 0)
            {
                auto disabled = request;
                disabled.Weight = 0.0F;
                auto unchanged = pose;
                REQUIRE(Keire::SolveFabrikIk(*skeleton, unchanged, disabled));
                for (std::size_t index = 0; index < pose.size(); ++index)
                {
                    CHECK(unchanged[index].Translation == pose[index].Translation);
                    CHECK(unchanged[index].Rotation == pose[index].Rotation);
                    CHECK(unchanged[index].Scale == pose[index].Scale);
                }
                auto unreachable = request;
                unreachable.Target = {origin.X + reach * 2.0F, origin.Y, origin.Z};
                auto stretched = pose;
                REQUIRE(Keire::SolveFabrikIk(*skeleton, stretched, unreachable));
                const auto stretchedMatrices = SpiderMatrices(*skeleton, stretched);
                const auto stretchedEnd = Keire::Math::TransformPoint(stretchedMatrices[chain.back()], {});
                CHECK(SpiderDistance(stretchedEnd, {origin.X + reach, origin.Y, origin.Z}) < reach * 0.001F);
            }
            if (frame == 24)
            {
                auto invalid = request;
                invalid.Target.X = std::numeric_limits<float>::quiet_NaN();
                CHECK_FALSE(Keire::SolveFabrikIk(*skeleton, pose, invalid));
                const auto afterFailure = SpiderMatrices(*skeleton, pose);
                for (std::size_t index = 0; index < before.size(); ++index)
                    CHECK(before[index].Elements == afterFailure[index].Elements);
            }
            REQUIRE(Keire::SolveFabrikIk(*skeleton, pose, request));
            const auto after = SpiderMatrices(*skeleton, pose);
            const auto endpoint = Keire::Math::TransformPoint(after[chain.back()], {});
            CHECK(SpiderDistance(endpoint, target) <=
                  SpiderDistance(Keire::Math::TransformPoint(before[chain.back()], {}), target) + reach * 0.001F);
            CHECK(SpiderDistance(Keire::Math::TransformPoint(after[chain.front()], {}), origin) < reach * 0.001F);
            for (std::size_t index = 1; index < chain.size(); ++index)
            {
                const auto length = SpiderDistance(Keire::Math::TransformPoint(bind[chain[index - 1]], {}),
                                                   Keire::Math::TransformPoint(bind[chain[index]], {}));
                const auto solvedLength = SpiderDistance(Keire::Math::TransformPoint(after[chain[index - 1]], {}),
                                                         Keire::Math::TransformPoint(after[chain[index]], {}));
                CHECK(std::abs(solvedLength - length) < reach * 0.001F);
            }
            for (std::size_t index = 0; index < after.size(); ++index)
            {
                for (const auto element : after[index].Elements)
                    REQUIRE(std::isfinite(element));
                // Solving one leg must not disturb other legs or the body.
                if (std::ranges::find(chain, index) == chain.end())
                    CHECK(before[index].Elements == after[index].Elements);
            }
        }
    }

    std::size_t animatedClips = 0;
    for (const auto& asset : imported.SubAssets)
    {
        if (asset.Type != Keire::AnimationClipAsset::StaticType())
            continue;
        CAPTURE(asset.Name);
        const auto clip = Keire::AnimationClipAsset::Decode(asset.Bytes);
        Keire::AnimationGraphDefinition graph;
        graph.EntryState = "Creature";
        graph.States = {{"Creature", asset.Id}};
        Keire::AnimatorInstance animator(skeleton, Keire::CreateRef<Keire::AnimationGraphAsset>(graph),
                                         [clip](Keire::AssetId) { return clip; });
        // Sample through two loop boundaries. Goals follow the animated pose rather than the bind pose.
        for (int frame = 0; frame < 96; ++frame)
        {
            CAPTURE(frame);
            const auto sampledPose = animator.Update(clip->Duration() / 40.0F).LocalPose;
            const auto sampled = SpiderMatrices(*skeleton, sampledPose);
            for (const float weight : {0.0F, 0.25F, 0.5F, 1.0F})
            {
                CAPTURE(weight);
                auto solvedPose = sampledPose;
                for (const auto& chain : chains)
                {
                    CAPTURE(skeleton->Bones()[chain.front()].Name);
                    float reach = 0.0F;
                    for (std::size_t index = 1; index < chain.size(); ++index)
                        reach += SpiderDistance(Keire::Math::TransformPoint(sampled[chain[index - 1]], {}),
                                                Keire::Math::TransformPoint(sampled[chain[index]], {}));
                    REQUIRE(reach > 0.0F);
                    const auto foot = Keire::Math::TransformPoint(sampled[chain.back()], {});
                    const float phase = static_cast<float>(frame) * 0.2F;
                    const Keire::Vector3 target{foot.X + reach * 0.05F * std::sin(phase),
                                                foot.Y + reach * 0.05F * std::cos(phase), foot.Z};
                    const auto before = solvedPose;
                    REQUIRE(Keire::SolveFabrikIk(*skeleton, solvedPose, {chain, target, 128, reach * 0.0001F, weight}));
                    const auto after = SpiderMatrices(*skeleton, solvedPose);
                    if (weight == 0.0F)
                        CHECK(solvedPose == before);
                    if (weight == 1.0F)
                        CHECK(SpiderDistance(Keire::Math::TransformPoint(after[chain.back()], {}), target) <=
                              SpiderDistance(foot, target) + reach * 0.001F);
                    CHECK(SpiderDistance(Keire::Math::TransformPoint(after[chain.front()], {}),
                                         Keire::Math::TransformPoint(sampled[chain.front()], {})) < reach * 0.001F);
                    for (std::size_t index = 1; index < chain.size(); ++index)
                    {
                        const auto length = SpiderDistance(Keire::Math::TransformPoint(sampled[chain[index - 1]], {}),
                                                           Keire::Math::TransformPoint(sampled[chain[index]], {}));
                        const auto solvedLength =
                            SpiderDistance(Keire::Math::TransformPoint(after[chain[index - 1]], {}),
                                           Keire::Math::TransformPoint(after[chain[index]], {}));
                        CHECK(std::abs(solvedLength - length) < reach * 0.001F);
                    }
                    for (std::size_t index = 0; index < solvedPose.size(); ++index)
                    {
                        for (const auto element : after[index].Elements)
                            REQUIRE(std::isfinite(element));
                        if (std::ranges::find(chain, index) == chain.end())
                            CHECK(solvedPose[index] == before[index]);
                    }
                }
            }
        }
        ++animatedClips;
    }
    CHECK(animatedClips == 7);
}
