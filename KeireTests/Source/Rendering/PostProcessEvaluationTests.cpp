#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "KeireInternal/Rendering/PostProcessEnvironment.h"
#include "KeireInternal/Rendering/PostProcessEvaluation.h"
#include "KeireInternal/Rendering/PostProcessLens.h"
#include "KeireInternal/Rendering/PostProcessLookup.h"
#include "KeireInternal/Rendering/PostProcessProfileCache.h"
#include "KeireTests/AssetTestProject.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>

TEST_CASE("lookup GPU parameters preserve identity fallback and address strip texel centers")
{
    using namespace Keire::Detail;
    CHECK(PostProcessLookupParameters(16, 1.0, 1.0, false).Y == 0.0F);
    CHECK(PostProcessLookupParameters(17, 1.0, 1.0, true).Y == 0.0F);
    CHECK(PostProcessLookupParameters(16, std::numeric_limits<double>::quiet_NaN(), 1.0, true).Y == 0.0F);
    for (const auto edge : {16U, 32U, 64U})
    {
        const auto parameters = PostProcessLookupParameters(edge, 0.8, 0.5, true);
        CHECK(parameters.Y == doctest::Approx(0.4));
        CHECK(parameters.X == static_cast<float>(edge));
        // Every blue slice remains within its tile, including black/white and the final strip edge.
        for (std::uint32_t slice = 0; slice < edge; ++slice)
        {
            const float first = (slice * edge + 0.5F) * parameters.Z;
            const float last = (slice * edge + edge - 0.5F) * parameters.Z;
            CHECK(first > static_cast<float>(slice) / edge);
            CHECK(last < static_cast<float>(slice + 1) / edge);
            CHECK((last - first) * edge * edge == doctest::Approx(edge - 1));
        }
        CHECK(0.5F * parameters.W > 0.0F);
        CHECK((edge - 0.5F) * parameters.W < 1.0F);
        // Reconstruct an identity strip at fractional blue positions using the shader's two bilinear samples.
        for (const auto channel : {0.0F, 0.13F, 0.5F, 0.97F, 1.0F})
        {
            const float lattice = channel * (edge - 1);
            const float low = std::floor(lattice);
            const float high = std::min(low + 1.0F, static_cast<float>(edge - 1));
            const float fraction = lattice - low;
            const float blue = (low * (1.0F - fraction) + high * fraction) / (edge - 1);
            const float redUv = (low * edge + lattice + 0.5F) * parameters.Z;
            const float red = (redUv / parameters.Z - low * edge - 0.5F) / (edge - 1);
            const float greenUv = (lattice + 0.5F) * parameters.W;
            const float green = (greenUv / parameters.W - 0.5F) / (edge - 1);
            CHECK(blue == doctest::Approx(channel));
            CHECK(red == doctest::Approx(channel).epsilon(0.0001));
            CHECK(green == doctest::Approx(channel));
        }
        CHECK(PostProcessLookupParameters(edge, 0.0, 1.0, true).Y == 0.0F);
        CHECK(PostProcessLookupParameters(edge, 1.0, 0.0, true).Y == 0.0F);
    }
}

TEST_CASE("post process lookup reference selection is discrete while strength blends independently")
{
    using namespace Keire;
    using namespace Keire::Detail;
    EvaluatedPostProcessing baseline;
    baseline.ColorLookup = AssetId(7, 1);
    baseline.ColorLookupWeight = 0.8;
    PostProcessProfileDefinition local;
    local.ColorLookup = {true, AssetId(7, 2)};
    local.ColorLookupWeight = {true, 0.2};
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 0.25, &local}};
    auto result = BlendPostProcessProfiles(contributions, baseline);
    CHECK(result.ColorLookup == baseline.ColorLookup);
    CHECK(result.ColorLookupWeight == doctest::Approx(0.65));
    contributions[0].Weight = 0.5;
    result = BlendPostProcessProfiles(contributions, baseline);
    CHECK(result.ColorLookup == local.ColorLookup.Value);
    CHECK(result.ColorLookupWeight == doctest::Approx(0.5));
    local.ColorLookup.Override = false;
    contributions[0].Weight = 1.0;
    result = BlendPostProcessProfiles(contributions, baseline);
    CHECK(result.ColorLookup == baseline.ColorLookup);
    CHECK(result.ColorLookupWeight == doctest::Approx(0.2));
    local.ColorLookup.Override = true;
    local.ColorLookupWeight.Override = false;
    result = BlendPostProcessProfiles(contributions, baseline);
    CHECK(result.ColorLookup == local.ColorLookup.Value);
    CHECK(result.ColorLookupWeight == baseline.ColorLookupWeight);
    contributions[0].Weight = 0.0;
    result = BlendPostProcessProfiles(contributions, baseline);
    CHECK(result.ColorLookup == baseline.ColorLookup);
    CHECK(result.ColorLookupWeight == baseline.ColorLookupWeight);
}

TEST_CASE("post process lookup null override clears inherited selection without resetting strength")
{
    using namespace Keire;
    using namespace Keire::Detail;
    EvaluatedPostProcessing baseline;
    baseline.ColorLookup = AssetId(7, 1);
    baseline.ColorLookupWeight = 0.4;
    PostProcessProfileDefinition clear;
    clear.ColorLookup = {true, {}};
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 0.49, &clear}};
    CHECK(BlendPostProcessProfiles(contributions, baseline).ColorLookup == baseline.ColorLookup);
    contributions[0].Weight = 0.5;
    const auto result = BlendPostProcessProfiles(contributions, baseline);
    CHECK_FALSE(result.ColorLookup);
    CHECK(result.ColorLookupWeight == baseline.ColorLookupWeight);
    clear.SchemaVersion = 1;
    clear.ColorLookup.Override = false;
    CHECK(BlendPostProcessProfiles(contributions, baseline).ColorLookup == baseline.ColorLookup);
}

TEST_CASE("post process lookup selection follows stable scene and volume priority independent of input order")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PostProcessProfileDefinition first;
    first.ColorLookup = {true, AssetId(7, 1)};
    PostProcessProfileDefinition second;
    second.ColorLookup = {true, AssetId(7, 2)};
    const AssetId sharedVolume(1, 1);
    std::array contributions{WeightedPostProcessProfile{sharedVolume, 0, 1.0, &first, AssetId(2, 1)},
                             WeightedPostProcessProfile{sharedVolume, 0, 0.5, &second, AssetId(2, 2)}};
    CHECK(BlendPostProcessProfiles(contributions).ColorLookup == second.ColorLookup.Value);
    std::ranges::reverse(contributions);
    CHECK(BlendPostProcessProfiles(contributions).ColorLookup == second.ColorLookup.Value);
    contributions[0].Priority = 10;
    CHECK(BlendPostProcessProfiles(contributions).ColorLookup == first.ColorLookup.Value);
}

TEST_CASE("post process lookup rejects invalid baseline strengths before evaluating references")
{
    using namespace Keire;
    using namespace Keire::Detail;
    for (const auto weight :
         {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        EvaluatedPostProcessing baseline;
        baseline.ColorLookupWeight = weight;
        CHECK_THROWS_AS((void)BlendPostProcessProfiles({}, baseline), std::invalid_argument);
    }
}

TEST_CASE("post process thin lens preserves focus aperture response and resolution scaling")
{
    using namespace Keire::Detail;
    PostProcessLens lens;
    lens.ApertureFNumber = 2.0;
    CHECK(PostProcessCircleOfConfusion(lens, 10.0, 1080.0) == 0.0);
    CHECK(PostProcessCircleOfConfusion(lens, 5.0, 1080.0) == doctest::Approx(2.8266331658));
    CHECK(PostProcessCircleOfConfusion(lens, 20.0, 1080.0) == doctest::Approx(-1.4133165829));
    const double nearRadius = PostProcessCircleOfConfusion(lens, 5.0, 1080.0);
    CHECK(PostProcessCircleOfConfusion(lens, 5.0, 2160.0) == doctest::Approx(nearRadius * 2.0));
    lens.ApertureFNumber = 4.0;
    CHECK(PostProcessCircleOfConfusion(lens, 5.0, 1080.0) == doctest::Approx(nearRadius * 0.5));
    lens.Strength = 0.0;
    CHECK(PostProcessCircleOfConfusion(lens, 5.0, 1080.0) == 0.0);
}

TEST_CASE("post process thin lens bounds near blur and handles planetary focus and sky")
{
    using namespace Keire::Detail;
    PostProcessLens lens;
    CHECK(PostProcessCircleOfConfusion(lens, 0.000001, 1080.0) == doctest::Approx(54.0));
    lens.FocusDistanceMetres = 1.0e12;
    CHECK(PostProcessCircleOfConfusion(lens, 1.0e12, 1080.0) == 0.0);
    const auto skyRadius = PostProcessCircleOfConfusion(lens, std::numeric_limits<double>::infinity(), 1080.0);
    CHECK(std::isfinite(skyRadius));
    CHECK(skyRadius < 0.0);
    CHECK(PostProcessCircleOfConfusion(lens, 5.0e11, 1080.0) > 0.0);
    CHECK_THROWS_AS((void)PostProcessCircleOfConfusion(lens, 0.0, 1080.0), std::invalid_argument);
    CHECK_THROWS_AS((void)PostProcessCircleOfConfusion(lens, std::numeric_limits<double>::quiet_NaN(), 1080.0),
                    std::invalid_argument);
    lens.FocusDistanceMetres = 0.05;
    CHECK_THROWS_AS((void)PostProcessCircleOfConfusion(lens, 1.0, 1080.0), std::invalid_argument);
    lens.FocusDistanceMetres = 0.01;
    CHECK_THROWS_AS((void)PostProcessCircleOfConfusion(lens, 1.0, 1080.0), std::invalid_argument);
}

TEST_CASE("post process cache retries failed loads without replacing a valid cached profile")
{
    using namespace Keire;
    std::unordered_map<AssetId, Ref<PostProcessProfileAsset>> cache;
    const AssetId profileId(7, 1);
    int attempts = 0;
    const auto load = [&]() -> Ref<PostProcessProfileAsset>
    {
        ++attempts;
        if (attempts == 1)
        {
            throw std::runtime_error("injected load submission failure");
        }
        return CreateRef<PostProcessProfileAsset>();
    };
    CHECK_THROWS_AS((void)Detail::GetOrLoadPostProcessAsset(cache, profileId, load), std::runtime_error);
    CHECK(cache.empty());
    const auto profile = Detail::GetOrLoadPostProcessAsset(cache, profileId, load);
    REQUIRE(profile);
    CHECK(attempts == 2);
    CHECK(cache.size() == 1);
    CHECK(Detail::GetOrLoadPostProcessAsset(cache, profileId, load).Get() == profile.Get());
    CHECK(attempts == 2);
}

TEST_CASE("post process profile retention tolerates alternating views and frame counter boundaries")
{
    using Keire::Detail::PostProcessProfileCacheExpired;
    CHECK_FALSE(PostProcessProfileCacheExpired(100, 100));
    CHECK_FALSE(PostProcessProfileCacheExpired(100, 101));
    CHECK_FALSE(PostProcessProfileCacheExpired(100, 700));
    CHECK(PostProcessProfileCacheExpired(100, 701));
    CHECK_FALSE(PostProcessProfileCacheExpired(100, 0));
    constexpr auto maximumFrame = std::numeric_limits<std::uint64_t>::max();
    CHECK_FALSE(PostProcessProfileCacheExpired(maximumFrame - 600, maximumFrame));
    CHECK(PostProcessProfileCacheExpired(maximumFrame - 601, maximumFrame));
}

TEST_CASE("inactive post process caches expire without new loads and preserve outstanding leases")
{
    struct Entry
    {
        std::uint64_t LastUsedFrame = 0;
        std::shared_ptr<int> Resource;
    };
    std::unordered_map<unsigned, Entry> cache;
    cache.emplace(1, Entry{100, std::make_shared<int>(1)});
    cache.emplace(2, Entry{101, std::make_shared<int>(2)});
    auto packetLease = cache.at(1).Resource;
    const std::weak_ptr<int> firstResource = packetLease;
    const std::weak_ptr<int> secondResource = cache.at(2).Resource;
    unsigned retired = 0;
    const auto retire = [&](Entry& entry)
    {
        REQUIRE(entry.Resource);
        ++retired;
    };
    Keire::Detail::PrunePostProcessCache(cache, 700, retire);
    CHECK(cache.size() == 2);
    CHECK(retired == 0);
    Keire::Detail::PrunePostProcessCache(cache, 701, retire);
    CHECK_FALSE(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(retired == 1);
    CHECK_FALSE(firstResource.expired());
    packetLease.reset();
    CHECK(firstResource.expired());
    Keire::Detail::PrunePostProcessCache(cache, 702, retire);
    CHECK(cache.empty());
    CHECK(retired == 2);
    CHECK(secondResource.expired());
    Keire::Detail::PrunePostProcessCache(cache, 900, retire);
    CHECK(retired == 2);
}

TEST_CASE("post process cache eviction retains a failed release for retry and respects refreshed cameras")
{
    struct Entry
    {
        std::uint64_t LastUsedFrame = 0;
    };
    std::unordered_map<unsigned, Entry> cache{{1, {100}}, {2, {101}}};
    cache.at(2).LastUsedFrame = 700;
    unsigned attempts = 0;
    const auto release = [&](Entry&)
    {
        if (++attempts == 1)
        {
            throw std::runtime_error("injected retirement failure");
        }
    };
    CHECK_THROWS_AS(Keire::Detail::PrunePostProcessCache(cache, 701, release), std::runtime_error);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    Keire::Detail::PrunePostProcessCache(cache, 701, release);
    CHECK_FALSE(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(attempts == 2);
    Keire::Detail::PrunePostProcessCache(cache, 600, release);
    CHECK(cache.contains(2));
    CHECK(attempts == 2);
}

TEST_CASE("lookup cache failed submission can retry after inactive entries expire")
{
    struct Entry
    {
        std::uint64_t LastUsedFrame = 0;
        unsigned Handle = 0;
    };
    std::unordered_map<unsigned, Entry> cache{{1, {100, 11}}};
    const auto load = []() -> Entry { throw std::runtime_error("injected lookup submission failure"); };
    CHECK_THROWS_AS((void)Keire::Detail::GetOrLoadPostProcessAsset(cache, 2U, load), std::runtime_error);
    CHECK_FALSE(cache.contains(2));
    CHECK(cache.at(1).Handle == 11);
    auto& loaded = Keire::Detail::GetOrLoadPostProcessAsset(cache, 2U, [] { return Entry{701, 22}; });
    CHECK(loaded.Handle == 22);
    Keire::Detail::PrunePostProcessCache(cache, 701, [](Entry&) {});
    CHECK_FALSE(cache.contains(1));
    REQUIRE(cache.contains(2));
    auto& reused = Keire::Detail::GetOrLoadPostProcessAsset(cache, 2U, load);
    CHECK(reused.Handle == 22);
    Keire::Detail::PrunePostProcessCache(cache, 1302, [](Entry&) {});
    CHECK(cache.empty());
    CHECK(Keire::Detail::GetOrLoadPostProcessAsset(cache, 2U, [] { return Entry{1302, 33}; }).Handle == 33);
}

TEST_CASE("post process exposure sweep stays finite without accumulating between camera captures")
{
    using namespace Keire;
    using namespace Keire::Detail;
    constexpr auto exposureIndex = static_cast<std::size_t>(PostProcessParameter::ExposureStops);
    PostProcessProfileDefinition profile;
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 1.0, &profile}};
    for (const float authoredExposure : {0.01F, 1.0F, 16.0F})
    {
        RenderEnvironmentSettings authored;
        authored.Exposure = authoredExposure;
        const auto baseline = MakePostProcessBaseline(authored);
        // Exercise repeated interactive edits, including the reported 3.9 EV setting and both limits.
        for (int step = -200; step <= 200; ++step)
        {
            const double stops = static_cast<double>(step) / 10.0;
            profile.Parameters[exposureIndex] = {true, stops};
            const auto evaluated = BlendPostProcessProfiles(contributions, baseline);
            auto captured = authored;
            ApplyPostProcessEnvironment(captured, evaluated);
            CHECK(std::isfinite(captured.Exposure));
            CHECK(captured.Exposure > 0.0F);
            CHECK(captured.Exposure == doctest::Approx(static_cast<double>(authoredExposure) * std::exp2(stops)));
            CHECK(authored.Exposure == authoredExposure);
        }
    }
}

TEST_CASE("post process environment preserves legacy bloom and applies exposure once to a capture")
{
    using namespace Keire;
    using namespace Keire::Detail;
    RenderEnvironmentSettings authored;
    authored.Exposure = 2.0F;
    authored.Bloom = {.Enabled = true, .Intensity = 0.7, .Threshold = 3.0, .SoftKnee = 0.2, .Radius = 3.5};
    auto baseline = MakePostProcessBaseline(authored);
    auto captured = authored;
    ApplyPostProcessEnvironment(captured, baseline);
    CHECK(captured.Exposure == authored.Exposure);
    CHECK(captured.Bloom == authored.Bloom);
    baseline.Parameters[static_cast<std::size_t>(PostProcessParameter::ExposureStops)] = 1.0;
    baseline.EffectWeights[static_cast<std::size_t>(PostProcessEffect::Bloom)] = 0.5;
    captured = authored;
    ApplyPostProcessEnvironment(captured, baseline);
    CHECK(captured.Exposure == 4.0F);
    CHECK(captured.Bloom.Intensity == doctest::Approx(0.35));
    CHECK(captured.Bloom.Threshold == authored.Bloom.Threshold);
    CHECK(authored.Exposure == 2.0F);
    const auto previous = captured;
    baseline.Parameters[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(ApplyPostProcessEnvironment(captured, baseline), std::invalid_argument);
    CHECK(captured.Exposure == previous.Exposure);
    CHECK(captured.Bloom == previous.Bloom);
}

TEST_CASE("post process volume blending preserves metre distances at distant world origins")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PostProcessVolumeSettings settings;
    settings.Global = false;
    settings.HalfExtents = {2.0, 2.0, 2.0};
    settings.Radius = 2.0;
    settings.BlendDistance = 2.0;
    settings.Weight = 0.8;
    for (const double origin : {0.0, 1.0e12, -1.0e12})
    {
        PostProcessVolumePose pose;
        pose.Center = {origin, origin, origin};
        for (const auto shape : {PostProcessVolumeShape::Box, PostProcessVolumeShape::Sphere})
        {
            settings.Shape = shape;
            CHECK(PostProcessVolumeWeight(settings, pose, pose.Center) == doctest::Approx(0.8));
            CHECK(PostProcessVolumeWeight(settings, pose, {origin + 2.0, origin, origin}) == doctest::Approx(0.8));
            CHECK(PostProcessVolumeWeight(settings, pose, {origin + 3.0, origin, origin}) == doctest::Approx(0.4));
            CHECK(PostProcessVolumeWeight(settings, pose, {origin + 4.0, origin, origin}) == 0.0);
        }
    }
}

TEST_CASE("post process local bounds respect rotation scale and hard blend boundaries")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PostProcessVolumeSettings settings;
    settings.Global = false;
    settings.HalfExtents = {4.0, 1.0, 1.0};
    settings.BlendDistance = 0.0;
    PostProcessVolumePose pose;
    CHECK(PostProcessVolumeWeight(settings, pose, {3.0, 0.0, 0.0}) == 1.0);
    pose.Rotation = Math::EulerDegreesToQuaternion({0.0F, 0.0F, 90.0F});
    CHECK(PostProcessVolumeWeight(settings, pose, {3.0, 0.0, 0.0}) == 0.0);
    CHECK(PostProcessVolumeWeight(settings, pose, {0.0, 3.0, 0.0}) == 1.0);
    pose.Scale = {-2.0F, 1.0F, 1.0F};
    CHECK(PostProcessVolumeWeight(settings, pose, {0.0, 6.0, 0.0}) == 1.0);
    settings.Shape = PostProcessVolumeShape::Sphere;
    settings.Radius = 1.0;
    CHECK(PostProcessVolumeWeight(settings, pose, {0.0, 1.9, 0.0}) == 1.0);
    CHECK(PostProcessVolumeWeight(settings, pose, {0.0, 2.1, 0.0}) == 0.0);
}

TEST_CASE("post process priorities and stable identity produce deterministic selective overrides")
{
    using namespace Keire;
    using namespace Keire::Detail;
    constexpr auto exposure = static_cast<std::size_t>(PostProcessParameter::ExposureStops);
    constexpr auto saturation = static_cast<std::size_t>(PostProcessParameter::Saturation);
    constexpr auto bloom = static_cast<std::size_t>(PostProcessEffect::Bloom);
    PostProcessProfileDefinition global;
    global.Parameters[exposure] = {true, 2.0};
    global.Parameters[saturation] = {true, 0.5};
    global.Effects[bloom] = {true, true};
    PostProcessProfileDefinition local;
    local.Parameters[exposure] = {true, -2.0};
    local.Effects[bloom] = {true, false};
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 1.0, &global},
                             WeightedPostProcessProfile{AssetId(1, 2), 10, 0.25, &local}};
    const auto result = BlendPostProcessProfiles(contributions);
    CHECK(result.Parameters[exposure] == doctest::Approx(1.0));
    CHECK(result.Parameters[saturation] == doctest::Approx(0.5));
    CHECK(result.EffectWeights[bloom] == doctest::Approx(0.75));
    std::ranges::reverse(contributions);
    const auto reversed = BlendPostProcessProfiles(contributions);
    CHECK(reversed.Parameters == result.Parameters);
    CHECK(reversed.EffectWeights == result.EffectWeights);
    contributions[1].Priority = 0;
    std::ranges::reverse(contributions);
    const auto tied = BlendPostProcessProfiles(contributions);
    CHECK(tied.Parameters == result.Parameters);
    CHECK(tied.EffectWeights == result.EffectWeights);
}

TEST_CASE("post process priority ties remain deterministic across scenes with matching entity identities")
{
    using namespace Keire;
    using namespace Keire::Detail;
    constexpr auto exposure = static_cast<std::size_t>(PostProcessParameter::ExposureStops);
    PostProcessProfileDefinition first;
    first.Parameters[exposure] = {true, 2.0};
    PostProcessProfileDefinition second;
    second.Parameters[exposure] = {true, -2.0};
    const AssetId sharedEntity(1, 1);
    std::array contributions{WeightedPostProcessProfile{sharedEntity, 0, 1.0, &first, AssetId(2, 1)},
                             WeightedPostProcessProfile{sharedEntity, 0, 0.25, &second, AssetId(2, 2)}};
    const auto expected = BlendPostProcessProfiles(contributions);
    CHECK(expected.Parameters[exposure] == doctest::Approx(1.0));
    std::ranges::reverse(contributions);
    const auto reversed = BlendPostProcessProfiles(contributions);
    CHECK(reversed.Parameters == expected.Parameters);
    CHECK(reversed.EffectWeights == expected.EffectWeights);
}

TEST_CASE("post process custom stacks select deterministically and can clear inherited effects")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PostProcessProfileDefinition global;
    global.CustomEffects = {true,
                            {{AssetId(8, 1), PostProcessCustomStage::BeforeToneMap, true},
                             {AssetId(8, 2), PostProcessCustomStage::AfterUi, false}}};
    PostProcessProfileDefinition local;
    local.CustomEffects = {true, {{AssetId(8, 3), PostProcessCustomStage::AfterToneMap, true}}};
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 1.0, &global},
                             WeightedPostProcessProfile{AssetId(1, 2), 1, 0.49, &local}};
    CHECK(BlendPostProcessProfiles(contributions).CustomEffects == global.CustomEffects.Value);
    contributions[1].Weight = 0.5;
    CHECK(BlendPostProcessProfiles(contributions).CustomEffects == local.CustomEffects.Value);
    std::ranges::reverse(contributions);
    CHECK(BlendPostProcessProfiles(contributions).CustomEffects == local.CustomEffects.Value);
    local.CustomEffects.Value.clear();
    CHECK(BlendPostProcessProfiles(contributions).CustomEffects.empty());
    local.CustomEffects.Override = false;
    CHECK(BlendPostProcessProfiles(contributions).CustomEffects == global.CustomEffects.Value);
    EvaluatedPostProcessing invalid;
    invalid.CustomEffects.push_back({});
    CHECK_THROWS_AS((void)BlendPostProcessProfiles({}, invalid), std::invalid_argument);
}

TEST_CASE("post process evaluation rejects nonfinite inputs rather than propagating invalid frames")
{
    using namespace Keire;
    using namespace Keire::Detail;
    PostProcessVolumeSettings settings;
    PostProcessVolumePose pose;
    CHECK_THROWS_AS((void)PostProcessVolumeWeight(settings, pose, {std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                    std::invalid_argument);
    settings.Global = false;
    pose.Scale.X = 0.0F;
    CHECK_THROWS_AS((void)PostProcessVolumeWeight(settings, pose, {}), std::invalid_argument);
    EvaluatedPostProcessing baseline;
    baseline.EffectWeights[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)BlendPostProcessProfiles({}, baseline), std::invalid_argument);
    PostProcessProfileDefinition profile;
    std::array contributions{WeightedPostProcessProfile{AssetId(1, 1), 0, 1.1, &profile}};
    CHECK_THROWS_AS((void)BlendPostProcessProfiles(contributions), std::invalid_argument);
}

TEST_CASE("post process optional typed loads reject malformed references without poisoning retries")
{
    using namespace Keire::Detail;
    std::unordered_map<int, int> cache;
    unsigned attempts = 0;
    const auto rejected = [&]() -> int
    {
        ++attempts;
        throw std::invalid_argument("Existing handle has another asset type");
    };
    CHECK(TryGetOrLoadPostProcessAsset(cache, 1, rejected) == nullptr);
    CHECK(TryGetOrLoadPostProcessAsset(cache, 1, rejected) == nullptr);
    CHECK(attempts == 2);
    CHECK(cache.empty());
    auto* recovered = TryGetOrLoadPostProcessAsset(cache, 1, [] { return 42; });
    REQUIRE(recovered != nullptr);
    CHECK(*recovered == 42);
    CHECK(TryGetOrLoadPostProcessAsset(cache, 1, rejected) == recovered);
    CHECK(attempts == 2);
    CHECK(cache.size() == 1);
}

TEST_CASE("post process optional typed loads propagate allocation and service failures")
{
    using namespace Keire::Detail;
    std::unordered_map<int, int> cache;
    const auto unavailable = []() -> int { throw std::logic_error("Asset system closed"); };
    const auto exhausted = []() -> int { throw std::bad_alloc(); };
    const auto failed = []() -> int { throw std::runtime_error("Submission failed"); };
    CHECK_THROWS_AS((void)TryGetOrLoadPostProcessAsset(cache, 1, unavailable), std::logic_error);
    CHECK_THROWS_AS((void)TryGetOrLoadPostProcessAsset(cache, 1, exhausted), std::bad_alloc);
    CHECK_THROWS_AS((void)TryGetOrLoadPostProcessAsset(cache, 1, failed), std::runtime_error);
    CHECK(cache.empty());
}

TEST_CASE("post process optional loads tolerate real asset handle type collisions")
{
    KeireTests::TemporaryAssetProject project;
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.DevelopmentCatalog = project.Root / "AbsentCatalog.json";
    specification.WorkerCount = 1;
    specification.Decoders = {Keire::CreateTexture2DAssetDecoder(), Keire::CreatePostProcessProfileAssetDecoder()};
    auto assets = Keire::CreateRef<Keire::AssetSystem>(specification);
    const Keire::AssetId conflicting(43, 1);
    const auto original = assets->Load<Keire::TextAsset>(conflicting);
    CHECK_THROWS_AS((void)assets->Load<Keire::Texture2DAsset>(conflicting), std::invalid_argument);
    CHECK_THROWS_AS((void)assets->Load<Keire::PostProcessProfileAsset>(conflicting), std::invalid_argument);

    std::unordered_map<int, Keire::AssetHandle<Keire::Texture2DAsset>> textures;
    std::unordered_map<int, Keire::AssetHandle<Keire::PostProcessProfileAsset>> profiles;
    CHECK(Keire::Detail::TryGetOrLoadPostProcessAsset(
              textures, 1, [&] { return assets->Load<Keire::Texture2DAsset>(conflicting); }) == nullptr);
    CHECK(Keire::Detail::TryGetOrLoadPostProcessAsset(
              profiles, 1, [&] { return assets->Load<Keire::PostProcessProfileAsset>(conflicting); }) == nullptr);
    CHECK(textures.empty());
    CHECK(profiles.empty());
    const Keire::AssetId repaired(43, 2);
    const auto* accepted = Keire::Detail::TryGetOrLoadPostProcessAsset(
        textures, 2, [&] { return assets->Load<Keire::Texture2DAsset>(repaired); });
    REQUIRE(accepted != nullptr);
    CHECK(accepted->Id() == repaired);
    CHECK(original.Id() == conflicting);
    (void)assets->PumpCompletions();
    CHECK(original.State() == Keire::AssetState::Failed);
    CHECK(accepted->State() == Keire::AssetState::Failed);
    // The repaired ID is type-correct but absent from the catalog; no texture-readiness claim is made.
    assets->Close();
}
