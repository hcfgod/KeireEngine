#include "KeireInternal/Spatial/FrameConversion.h"
#include "KeireInternal/Spatial/FrameRegistry.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE("Deep frame conversion preserves local detail at huge root sector addresses")
{
    using namespace Keire::Detail;
    bool quarterTurns = false;
    SUBCASE("Translation only") {}
    SUBCASE("Repeated quarter turns") { quarterTurns = true; }
    const auto rotation = quarterTurns ? FrameRotation::Normalize(0.0, 0.0, 1.0, 1.0) : FrameRotation{};
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    std::vector<LiveFrame> frames{root};
    constexpr std::size_t depth = 256;
    for (std::size_t index = 1; index <= depth; ++index)
    {
        frames.push_back(registry.Create(index + 1, frames.back()));
    }
    std::vector<FrameSnapshot> evaluations;
    for (std::size_t index = 1; index <= depth; ++index)
    {
        const std::array<std::int64_t, 3> sectors =
            index == 1 ? std::array<std::int64_t, 3>{1000000000000LL, -1000000000000LL, 0}
                       : std::array<std::int64_t, 3>{};
        evaluations.emplace_back(registry.Identity(frames[index]),
                                 SectorPosition(registry.Identity(frames[index - 1]), sectors, {0.125, -0.25, 0.5}),
                                 rotation, std::array<double, 3>{}, std::array<double, 3>{}, 10.0, registry.Revision());
    }
    const auto published = registry.Publish(evaluations, 10.0);
    const SpatialMotion motion{
        SectorPosition(registry.Identity(frames.back()), {}, {0.001, -0.002, 0.003}), {1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    const auto converted = ConvertFrameMotion(registry, frames.back(), root, motion, published, 10.0);
    // Four quarter turns cancel XY translation and restore vector orientation; Z accumulates normally.
    const std::array<double, 3> expectedOffset =
        quarterTurns ? std::array{0.001, -0.002, 128.003} : std::array{32.001, -64.002, 128.003};
    const SectorPosition expected(registry.Identity(root), {1000000000000LL, -1000000000000LL, 0}, expectedOffset);
    for (const double error : converted.Position.RelativeTo(expected))
    {
        CHECK(error == doctest::Approx(0.0).epsilon(1e-10));
    }
    const auto restored = ConvertFrameMotion(registry, root, frames.back(), converted, published, 10.0);
    for (const double error : restored.Position.RelativeTo(motion.Position))
    {
        CHECK(error == doctest::Approx(0.0).epsilon(1e-10));
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(converted.Velocity[axis] == doctest::Approx(motion.Velocity[axis]).epsilon(1e-10));
        CHECK(converted.Acceleration[axis] == doctest::Approx(motion.Acceleration[axis]).epsilon(1e-10));
        CHECK(restored.Velocity[axis] == doctest::Approx(motion.Velocity[axis]).epsilon(1e-10));
        CHECK(restored.Acceleration[axis] == doctest::Approx(motion.Acceleration[axis]).epsilon(1e-10));
    }
}

TEST_CASE("Deep rotating frames match aggregate angular velocity and acceleration reference")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    constexpr std::size_t depth = 256;
    std::vector<LiveFrame> frames{root};
    for (std::size_t index = 1; index <= depth; ++index)
    {
        frames.push_back(registry.Create(index + 1, frames.back()));
    }
    std::vector<FrameSnapshot> evaluations;
    for (std::size_t index = 1; index <= depth; ++index)
    {
        const std::array<std::int64_t, 3> sectors =
            index == 1 ? std::array<std::int64_t, 3>{1000000000000LL, 0, 0} : std::array<std::int64_t, 3>{};
        evaluations.emplace_back(registry.Identity(frames[index]),
                                 SectorPosition(registry.Identity(frames[index - 1]), sectors, {}), FrameRotation{},
                                 std::array<double, 3>{}, std::array{0.0, 0.0, 1.0 / depth}, 10.0, registry.Revision(),
                                 std::array<double, 3>{}, std::array{0.0, 0.0, 0.5 / depth});
    }
    const auto published = registry.Publish(evaluations, 10.0);
    const SpatialMotion motion{
        SectorPosition(registry.Identity(frames.back()), {}, {1.0, 2.0, 3.0}), {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}};
    const auto converted = ConvertFrameMotion(registry, frames.back(), root, motion, published, 10.0);
    CHECK(converted.Position == SectorPosition(registry.Identity(root), {1000000000000LL, 0, 0}, {1.0, 2.0, 3.0}));
    // Coincident origins and aligned axes at this instant: total omega=1 and alpha=0.5 about Z.
    // v'=v+omega cross r; a'=a+2omega cross v+alpha cross r+omega cross(omega cross r).
    const std::array expectedVelocity{2.0, 6.0, 6.0};
    const std::array expectedAcceleration{-5.0, 14.5, 9.0};
    const auto restored = ConvertFrameMotion(registry, root, frames.back(), converted, published, 10.0);
    CHECK(restored.Position == motion.Position);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(converted.Velocity[axis] == doctest::Approx(expectedVelocity[axis]).epsilon(1e-10));
        CHECK(converted.Acceleration[axis] == doctest::Approx(expectedAcceleration[axis]).epsilon(1e-10));
        CHECK(restored.Velocity[axis] == doctest::Approx(motion.Velocity[axis]).epsilon(1e-10));
        CHECK(restored.Acceleration[axis] == doctest::Approx(motion.Acceleration[axis]).epsilon(1e-10));
    }
}

TEST_CASE("Frame attachments block removal and release safely across moves and world closure")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto child = registry.Create(2, registry.Find(1));
    auto first = registry.Attach(child);
    auto second = registry.Attach(child);
    const auto revision = registry.Revision();
    CHECK_THROWS_AS(registry.Remove(child), std::logic_error);
    CHECK(registry.Revision() == revision);
    first = std::move(second);
    second.Reset();
    CHECK_THROWS_AS(registry.Remove(child), std::logic_error);
    std::thread release([attachment = std::move(first)]() mutable { attachment.Reset(); });
    release.join();
    CHECK_NOTHROW(registry.Remove(child));
    CHECK_FALSE(registry.IsLive(child));
    CHECK_THROWS_AS((void)registry.Attach(child), std::invalid_argument);
    FrameAttachment retained;
    {
        FrameRegistry temporary(1);
        retained = temporary.Attach(temporary.Find(1));
        temporary.Close();
        CHECK_THROWS_AS((void)temporary.Attach(temporary.Find(1)), std::logic_error);
    }
    retained.Reset();
    retained.Reset();
}

TEST_CASE("Frame depth limits reject creation and whole-subtree reparenting atomically")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1, 1, 3);
    const auto root = registry.Find(1);
    const auto first = registry.Create(2, root);
    const auto second = registry.Create(3, first);
    const auto third = registry.Create(4, second);
    const auto sibling = registry.Create(5, root);
    const auto revision = registry.Revision();
    CHECK_THROWS_AS((void)registry.Create(6, third), std::out_of_range);
    CHECK_THROWS_AS(registry.Reparent(first, sibling), std::out_of_range);
    CHECK(registry.Revision() == revision);
    REQUIRE(registry.Parent(first));
    CHECK(registry.Identity(*registry.Parent(first)) == registry.Identity(root));
    CHECK(registry.IsLive(third));
    CHECK_THROWS_AS((void)registry.Find(6), std::invalid_argument);
    registry.Reparent(second, sibling);
    CHECK(registry.Revision() == revision + 1);
    CHECK(registry.Identity(*registry.Parent(second)) == registry.Identity(sibling));
    FrameRegistry rootOnly(2, 1, 0);
    CHECK_THROWS_AS((void)rootOnly.Create(2, rootOnly.Find(1)), std::out_of_range);
}

TEST_CASE("Frame publication commits complete same-time evaluations and invalidates old poses")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    const auto first = registry.Create(2, root);
    const auto second = registry.Create(3, root);
    const auto evaluate = [&](const LiveFrame& child, const double time, const double offset)
    {
        return FrameSnapshot(registry.Identity(child), SectorPosition({1, 1}, {}, {offset, 0.0, 0.0}), {}, {}, {}, time,
                             registry.Revision());
    };
    const std::array initial{evaluate(first, 10.0, 0.0), evaluate(second, 10.0, 1.0)};
    const auto old = registry.Publish(initial, 10.0);
    CHECK(registry.IsCurrent(old[0], 10.0));
    CHECK(registry.IsCurrent(old[1], 10.0));
    CHECK_THROWS_AS((void)registry.Publish(std::span(initial).first(1), 10.0), std::invalid_argument);
    const std::array duplicate{initial[0], initial[0]};
    CHECK_THROWS_AS((void)registry.Publish(duplicate, 10.0), std::invalid_argument);
    const std::array mixed{initial[0], evaluate(second, 11.0, 1.0)};
    CHECK_THROWS_AS((void)registry.Publish(mixed, 10.0), std::invalid_argument);
    CHECK(registry.IsCurrent(old[0], 10.0));
    CHECK(registry.IsCurrent(old[1], 10.0));
    const auto topologyRevision = registry.Revision();
    const std::array changed{evaluate(first, 10.0, 5.0), initial[1]};
    const auto current = registry.Publish(changed, 10.0);
    CHECK_FALSE(registry.IsCurrent(old[0], 10.0));
    CHECK_FALSE(registry.IsCurrent(old[1], 10.0));
    CHECK(registry.IsCurrent(current[0], 10.0));
    CHECK(registry.IsCurrent(current[1], 10.0));
    CHECK(registry.Revision() == topologyRevision);
    const std::array mixedEpochs{old[0], current[1]};
    const SpatialMotion motion{SectorPosition({1, 2}, {}, {}), {}, {}};
    CHECK_THROWS_AS((void)ConvertFrameMotion(registry, first, second, motion, mixedEpochs, 10.0),
                    std::invalid_argument);
}

TEST_CASE("Frame conversion uses common ancestors and validates the complete route")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    const auto planet = registry.Create(2, root);
    const auto ship = registry.Create(3, planet);
    const auto station = registry.Create(4, planet);
    const auto evaluate = [&](const LiveFrame& child, const double x)
    {
        return FrameSnapshot(registry.Identity(child), SectorPosition({1, 2}, {}, {x, 0.0, 0.0}), {}, {}, {}, 10.0,
                             registry.Revision());
    };
    const std::array evaluations{evaluate(ship, 100.0), evaluate(station, 150.0),
                                 FrameSnapshot({1, 2}, SectorPosition({1, 1}, {1000000000000LL, 0, 0}, {}), {}, {}, {},
                                               10.0, registry.Revision())};
    const auto published = registry.Publish(evaluations, 10.0);
    const std::array edges{published[0], published[1]};
    const SpatialMotion motion{SectorPosition({1, 3}, {}, {2.0, 3.0, 4.0}), {1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    // No planet-to-root evaluation is provided: sibling conversion must not visit the distant root.
    const auto result = ConvertFrameMotion(registry, ship, station, motion, edges, 10.0);
    CHECK(result.Position == SectorPosition({1, 4}, {}, {-48.0, 3.0, 4.0}));
    CHECK(result.Velocity == motion.Velocity);
    CHECK(result.Acceleration == motion.Acceleration);
    const auto restored = ConvertFrameMotion(registry, station, ship, result, edges, 10.0);
    CHECK(restored.Position == motion.Position);
    CHECK(ConvertFrameMotion(registry, ship, ship, motion, {}, 10.0).Position == motion.Position);
    CHECK_THROWS_AS((void)ConvertFrameMotion(registry, ship, station, motion, std::span(edges).first(1), 10.0),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)ConvertFrameMotion(registry, ship, station, motion, edges, 11.0), std::invalid_argument);
    const std::array duplicate{edges[0], edges[0], edges[1]};
    CHECK_THROWS_AS((void)ConvertFrameMotion(registry, ship, station, motion, duplicate, 10.0), std::invalid_argument);
    registry.Reparent(ship, root);
    CHECK_THROWS_AS((void)ConvertFrameMotion(registry, ship, station, motion, edges, 10.0), std::invalid_argument);
}

TEST_CASE("Bound frame evaluations reject topology time and lifetime mismatches")
{
    using namespace Keire::Detail;
    FrameRegistry registry(7);
    FrameRegistry other(7);
    const auto root = registry.Find(1);
    const auto child = registry.Create(2, root);
    const auto evaluate = [&](const SpatialIdentity parent, const std::uint64_t revision)
    { return FrameSnapshot({7, 2}, SectorPosition(parent, {}, {}), {}, {}, {}, 12.0, revision); };
    const auto bound = registry.Publish(std::array{evaluate({7, 1}, registry.Revision())}, 12.0).front();
    CHECK(registry.IsCurrent(bound, 12.0));
    CHECK_FALSE(registry.IsCurrent(bound, 13.0));
    CHECK_FALSE(other.IsCurrent(bound, 12.0));
    CHECK_THROWS_AS((void)registry.Publish(std::array{evaluate({7, 9}, registry.Revision())}, 12.0),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)registry.Publish(std::array{evaluate({7, 1}, registry.Revision() + 1)}, 12.0),
                    std::invalid_argument);
    const auto sibling = registry.Create(3, root);
    CHECK_FALSE(registry.IsCurrent(bound, 12.0));
    registry.Reparent(child, sibling);
    const FrameSnapshot siblingEvaluation({7, 3}, SectorPosition({7, 1}, {}, {}), {}, {}, {}, 12.0,
                                          registry.Revision());
    CHECK_THROWS_AS((void)registry.Publish(std::array{evaluate({7, 1}, registry.Revision()), siblingEvaluation}, 12.0),
                    std::invalid_argument);
    const auto current =
        registry.Publish(std::array{evaluate({7, 3}, registry.Revision()), siblingEvaluation}, 12.0).front();
    CHECK(registry.IsCurrent(current, 12.0));
    registry.Close();
    CHECK_FALSE(registry.IsCurrent(current, 12.0));
    CHECK(current.Evaluation().ChildIdentity() == SpatialIdentity{7, 2});
}

TEST_CASE("Frame registries reject stale and foreign live handles despite matching logical keys")
{
    using namespace Keire::Detail;
    FrameRegistry edit(7);
    FrameRegistry play(7);
    const auto root = edit.Find(1);
    const auto old = edit.Create(2, root);
    CHECK(edit.IsLive(old));
    CHECK_FALSE(play.IsLive(old));
    CHECK_THROWS_AS((void)play.Identity(old), std::invalid_argument);
    edit.Remove(old);
    const auto replacement = edit.Create(2, root);
    CHECK_FALSE(edit.IsLive(old));
    CHECK(edit.IsLive(replacement));
    CHECK(edit.Identity(replacement) == SpatialIdentity{7, 2});
    CHECK_THROWS_AS(edit.Remove(old), std::invalid_argument);
    edit.Close();
    edit.Close();
    CHECK_FALSE(edit.IsLive(replacement));
    CHECK_THROWS_AS((void)edit.Find(1), std::logic_error);
    CHECK_THROWS_AS((void)edit.Create(3, root), std::logic_error);
    LiveFrame destroyed;
    {
        FrameRegistry temporary(7);
        destroyed = temporary.Find(1);
    }
    CHECK_FALSE(play.IsLive(destroyed));
}

TEST_CASE("Frame topology rejects cycles and parent removal without publishing partial changes")
{
    using namespace Keire::Detail;
    FrameRegistry registry(1);
    const auto root = registry.Find(1);
    const auto planet = registry.Create(2, root);
    const auto ship = registry.Create(3, planet);
    const auto revision = registry.Revision();
    CHECK_THROWS_AS(registry.Reparent(planet, ship), std::invalid_argument);
    CHECK_THROWS_AS(registry.Reparent(ship, ship), std::invalid_argument);
    CHECK_THROWS_AS(registry.Remove(planet), std::invalid_argument);
    CHECK_THROWS_AS(registry.Remove(root), std::invalid_argument);
    CHECK_THROWS_AS(registry.Reparent(root, ship), std::invalid_argument);
    CHECK_THROWS_AS((void)registry.Create(2, root), std::invalid_argument);
    CHECK(registry.Revision() == revision);
    registry.Reparent(ship, planet);
    CHECK(registry.Revision() == revision);
    registry.Reparent(ship, root);
    CHECK(registry.Revision() == revision + 1);
    registry.Remove(planet);
    CHECK(registry.IsLive(ship));
    CHECK_FALSE(registry.IsLive(planet));
}
