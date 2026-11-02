#include "KeireInternal/Spatial/FrameConversion.h"
#include "KeireInternal/Spatial/PlanetFrameCache.h"

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

static_assert(!std::is_convertible_v<Keire::Detail::PreparedFrameSnapshot, Keire::Detail::BoundFrameSnapshot>);

TEST_CASE("Frame preparation evaluates future motion without invalidating the published epoch")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    const auto child = registry.Create(2, root);
    const auto evaluate = [&](const double time)
    {
        return FrameSnapshot(registry.Identity(child), SectorPosition(registry.Identity(root), {}, {time, 0.0, 0.0}),
                             {}, {}, {}, time, registry.Revision());
    };
    const std::array current{evaluate(0.0)};
    const auto published = registry.Publish(current, 0.0);
    const std::array next{evaluate(10.0)};
    {
        const auto preparation = registry.PreparePublication(next, 10.0);
        CHECK(registry.IsCurrent(published.front(), 0.0));
        const SpatialMotion local{SectorPosition(registry.Identity(child), {}, {0.125, 0.25, -0.5}), {}, {}};
        const auto result = ConvertFrameMotion(preparation, child, root, local, preparation.Snapshots(), 10.0);
        const SectorPosition expected(registry.Identity(root), {}, {10.125, 0.25, -0.5});
        CHECK(result.Position.RelativeTo(expected) == std::array<double, 3>{});
        const auto competing = registry.PreparePublication(next, 10.0);
        CHECK_FALSE(preparation.IsCurrent(competing.Snapshots().front(), 10.0));
        const auto copied = preparation.Snapshots().front();
        CHECK_FALSE(preparation.IsCurrent(copied, 10.0));
    }
    CHECK(registry.IsCurrent(published.front(), 0.0));
    CHECK_THROWS_AS((void)registry.PreparePublication(next, 11.0), std::invalid_argument);
    CHECK(registry.IsCurrent(published.front(), 0.0));
    CHECK_THROWS_AS((void)registry.PreparePublication({}, 10.0), std::invalid_argument);
    CHECK(registry.IsCurrent(published.front(), 0.0));
    auto accepted = registry.PreparePublication(next, 10.0);
    const auto committed = registry.CommitPublication(std::move(accepted));
    CHECK_FALSE(registry.IsCurrent(published.front(), 0.0));
    CHECK(registry.IsCurrent(committed.front(), 10.0));
    CHECK_THROWS_AS((void)registry.CommitPublication(std::move(accepted)), std::logic_error);
    CHECK(registry.IsCurrent(committed.front(), 10.0));
}

TEST_CASE("Frame publication rejects wrong owners and stale preparations without a second commit")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    FrameRegistry other(1);
    const auto root = registry.Find(1);
    const auto child = registry.Create(2, root);
    const std::array evaluations{FrameSnapshot(registry.Identity(child),
                                               SectorPosition(registry.Identity(root), {}, {}), {}, {}, {}, 0.0,
                                               registry.Revision())};
    const auto published = registry.Publish(evaluations, 0.0);
    auto preparation = registry.PreparePublication(evaluations, 0.0);
    CHECK_THROWS_AS((void)other.CommitPublication(std::move(preparation)), std::invalid_argument);
    CHECK(registry.IsCurrent(published.front(), 0.0));
    CHECK(preparation.IsCurrent(preparation.Snapshots().front(), 0.0));
    SUBCASE("intervening publication")
    {
        const auto newer = registry.Publish(evaluations, 0.0);
        CHECK_THROWS_AS((void)registry.CommitPublication(std::move(preparation)), std::logic_error);
        CHECK(registry.IsCurrent(newer.front(), 0.0));
    }
    SUBCASE("topology revision")
    {
        (void)registry.Create(3, root);
        CHECK_THROWS_AS((void)registry.CommitPublication(std::move(preparation)), std::logic_error);
    }
    SUBCASE("closed registry")
    {
        registry.Close();
        CHECK_THROWS_AS((void)preparation.Snapshots(), std::logic_error);
    }
    SUBCASE("moved preparation")
    {
        auto moved = std::move(preparation);
        CHECK_THROWS_AS((void)preparation.Snapshots(), std::logic_error);
        const auto committed = registry.CommitPublication(std::move(moved));
        CHECK(registry.IsCurrent(committed.front(), 0.0));
    }
}

TEST_CASE("Frame preparation detects registry destruction before dereferencing its borrowed owner")
{
    using namespace Keire::Detail;
    auto registry = std::make_unique<FrameRegistry>(1);
    auto preparation = registry->PreparePublication({}, 0.0);
    registry.reset();
    CHECK_THROWS_AS((void)preparation.Snapshots(), std::logic_error);
}

TEST_CASE("Planet frame cache keeps published storage on rejected preparation and commits once")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    (void)registry.Create(2, registry.Find(1));
    PlanetDefinition planet;
    planet.Planet = Keire::AssetId(1, 2);
    planet.Frame = 2;
    planet.AngularVelocity = {0.0, 0.0, 0.1};
    const std::array planets{planet};
    PlanetFrameCache cache;
    const auto published = cache.Resolve(registry, planets, 0.0);
    const auto original = published.front();
    const auto* storage = published.data();
    {
        const auto discarded = cache.Prepare(registry, planets, 1.0);
        CHECK(discarded.Snapshots().front().Evaluation().Time() == 1.0);
        CHECK(registry.IsCurrent(original, 0.0));
    }
    const auto unchanged = cache.Resolve(registry, planets, 0.0);
    CHECK(unchanged.data() == storage);
    auto stale = cache.Prepare(registry, planets, 1.0);
    auto accepted = cache.Prepare(registry, planets, 2.0);
    const auto committed = cache.Commit(registry, std::move(accepted));
    const auto committedSnapshot = committed.front();
    const auto* committedStorage = committed.data();
    CHECK_THROWS_AS((void)cache.Commit(registry, std::move(stale)), std::logic_error);
    CHECK(registry.IsCurrent(committedSnapshot, 2.0));
    CHECK(cache.Resolve(registry, planets, 2.0).data() == committedStorage);
}
