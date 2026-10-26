#include "KeireInternal/Spatial/FrameRotation.h"
#include "KeireInternal/Spatial/FrameSnapshot.h"
#include "KeireInternal/Spatial/SectorCoordinate.h"
#include "KeireInternal/Spatial/SectorPosition.h"
#include "KeireInternal/Spatial/SectorPositionCodec.h"

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

TEST_CASE("Frame acceleration includes angular centripetal and Coriolis terms")
{
    using namespace Keire::Detail;
    const SectorPosition origin({1, 1}, {1000000000000LL, 0, 0}, {});
    const FrameSnapshot frame({1, 2}, origin, {}, {100.0, 20.0, -3.0}, {0.0, 0.0, 2.0}, 0.0, 1, {3.0, 4.0, 5.0},
                              {0.0, 0.0, 0.5});
    const SpatialMotion local{SectorPosition({1, 2}, {}, {10.0, 0.0, 0.0}), {1.0, 2.0, 3.0}, {6.0, 7.0, 8.0}};
    const auto parent = frame.ToParent(local);
    CHECK(parent.Acceleration == std::array<double, 3>{-39.0, 20.0, 13.0});
    const auto restored = frame.ToChild(parent);
    CHECK(restored.Acceleration == local.Acceleration);
    const FrameSnapshot spinning({1, 2}, origin, {}, {}, {0.0, 0.0, 2.0}, 0.0, 2);
    const SpatialMotion stationary{local.Position, {}, {}};
    CHECK(spinning.ToParent(stationary).Acceleration == std::array<double, 3>{-40.0, 0.0, 0.0});
    const SpatialMotion inertial{origin.Translated({10.0, 0.0, 0.0}), {}, {}};
    const auto relative = spinning.ToChild(inertial);
    CHECK(relative.Velocity == std::array<double, 3>{0.0, -20.0, 0.0});
    CHECK(relative.Acceleration == std::array<double, 3>{-40.0, 0.0, 0.0});
    CHECK(spinning.ToParent(relative).Acceleration == inertial.Acceleration);
    auto invalid = local;
    invalid.Acceleration[2] = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)frame.ToParent(invalid), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)FrameSnapshot({1, 2}, origin, {}, {}, {}, 0.0, 1, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}),
        std::invalid_argument);
}

TEST_CASE("Frame acceleration agrees with a differentiated moving trajectory")
{
    using namespace Keire::Detail;
    const SectorPosition origin({1, 1}, {}, {});
    const double initialAngle = 0.35;
    const FrameSnapshot frame(
        {1, 2}, origin, FrameRotation::Normalize(0.0, 0.0, std::sin(initialAngle / 2.0), std::cos(initialAngle / 2.0)),
        {3.0, -2.0, 1.0}, {0.0, 0.0, 0.7}, 0.0, 1, {0.4, -0.6, 0.8}, {0.0, 0.0, 0.2});
    const SpatialMotion local{SectorPosition({1, 2}, {}, {5.0, 2.0, -1.0}), {0.3, -0.4, 0.5}, {0.1, 0.2, -0.3}};
    const auto actual = frame.ToParent(local);
    const auto trajectory = [](const double time)
    {
        const double angle = 0.35 + 0.7 * time + 0.1 * time * time;
        const double x = 5.0 + 0.3 * time + 0.05 * time * time;
        const double y = 2.0 - 0.4 * time + 0.1 * time * time;
        return std::array{3.0 * time + 0.2 * time * time + std::cos(angle) * x - std::sin(angle) * y,
                          -2.0 * time - 0.3 * time * time + std::sin(angle) * x + std::cos(angle) * y,
                          time + 0.4 * time * time - 1.0 + 0.5 * time - 0.15 * time * time};
    };
    constexpr double step = 0.0001;
    const auto before = trajectory(-step);
    const auto current = trajectory(0.0);
    const auto after = trajectory(step);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(std::abs(actual.Velocity[axis] - (after[axis] - before[axis]) / (2.0 * step)) < 1e-6);
        CHECK(std::abs(actual.Acceleration[axis] - (after[axis] - 2.0 * current[axis] + before[axis]) / (step * step)) <
              1e-5);
    }
}

TEST_CASE("Moving rotating frame snapshots preserve position and physical velocity")
{
    using namespace Keire::Detail;
    const SectorPosition parentOrigin({1, 1}, {1000000000000LL, -1000000000000LL, 0}, {0.001, 0.002, 0.003});
    const FrameSnapshot snapshot({1, 2}, parentOrigin, {}, {100.0, 20.0, -3.0}, {0.0, 0.0, 2.0}, 123.0, 7);
    const SpatialMotion local{SectorPosition({1, 2}, {}, {10.0, 0.0, 0.0}), {1.0, 2.0, 3.0}};
    const auto parent = snapshot.ToParent(local);
    CHECK(parent.Velocity == std::array<double, 3>{101.0, 42.0, 0.0});
    CHECK(parent.Position.Identity() == parentOrigin.Identity());
    const auto restored = snapshot.ToChild(parent);
    const auto error = restored.Position.RelativeTo(local.Position);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(std::abs(error[axis]) < 1e-10);
        CHECK(restored.Velocity[axis] == doctest::Approx(local.Velocity[axis]).epsilon(1e-10));
    }
    CHECK(snapshot.Time() == 123.0);
    CHECK(snapshot.Revision() == 7);
    const FrameSnapshot rotated({1, 2}, parentOrigin, FrameRotation::Normalize(0.0, 0.0, 1.0, 1.0), {}, {0.0, 0.0, 2.0},
                                123.0, 8);
    const auto turned = rotated.ToParent(local);
    CHECK(turned.Velocity[0] == doctest::Approx(-22.0).epsilon(1e-10));
    CHECK(turned.Velocity[1] == doctest::Approx(1.0).epsilon(1e-10));
    CHECK(turned.Velocity[2] == doctest::Approx(3.0).epsilon(1e-10));
    const auto turnedBack = rotated.ToChild(turned);
    const auto turnedError = turnedBack.Position.RelativeTo(local.Position);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(std::abs(turnedError[axis]) < 1e-10);
        CHECK(turnedBack.Velocity[axis] == doctest::Approx(local.Velocity[axis]).epsilon(1e-10));
    }
    CHECK_THROWS_AS((void)snapshot.ToParent(parent), std::invalid_argument);
    CHECK_THROWS_AS((void)snapshot.ToChild(local), std::invalid_argument);
    CHECK_THROWS_AS((void)FrameSnapshot({2, 2}, parentOrigin, {}, {}, {}, 0.0, 1), std::invalid_argument);
    CHECK_THROWS_AS((void)FrameSnapshot({1, 1}, parentOrigin, {}, {}, {}, 0.0, 1), std::invalid_argument);
    CHECK_THROWS_AS((void)FrameSnapshot({1, 2}, parentOrigin, {}, {}, {}, 0.0, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)FrameSnapshot({1, 2}, parentOrigin, {}, {}, {}, std::numeric_limits<double>::infinity(), 1),
                    std::invalid_argument);
    const SpatialMotion invalidMotion{local.Position, {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}};
    CHECK_THROWS_AS((void)snapshot.ToParent(invalidMotion), std::invalid_argument);
}

TEST_CASE("Double frame rotations compose and invert without float narrowing")
{
    using Keire::Detail::FrameRotation;
    const double sine = std::sin(std::numbers::pi / 4.0);
    const double cosine = std::cos(std::numbers::pi / 4.0);
    const auto aroundZ = FrameRotation::Normalize(0.0, 0.0, sine, cosine);
    const auto aroundX = FrameRotation::Normalize(sine, 0.0, 0.0, cosine);
    const auto rotated = aroundZ.Apply({1.0, 0.0, 0.0});
    CHECK(rotated[0] == doctest::Approx(0.0).epsilon(1e-14));
    CHECK(rotated[1] == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(rotated[2] == 0.0);
    for (int step = -100; step <= 100; ++step)
    {
        const std::array vector{static_cast<double>(step) + 0.000000001, 1234.567891234, -88.000000001};
        const auto rotation = FrameRotation::Normalize(0.1 * step, 0.2, -0.3, 0.4);
        const auto restored = rotation.Inverse().Apply(rotation.Apply(vector));
        const auto sequential = aroundZ.Apply(aroundX.Apply(vector));
        const auto composed = aroundZ.Compose(aroundX).Apply(vector);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            CHECK(restored[axis] == doctest::Approx(vector[axis]).epsilon(1e-11));
            CHECK(composed[axis] == doctest::Approx(sequential[axis]).epsilon(1e-11));
        }
    }
    const auto tiny = FrameRotation::Normalize(0.0, 0.0, 0.0, std::numeric_limits<double>::denorm_min());
    CHECK(tiny.Components() == FrameRotation{}.Components());
    const auto huge =
        FrameRotation::Normalize(0.0, 0.0, std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    CHECK(huge.Apply({1.0, 0.0, 0.0})[1] == doctest::Approx(1.0).epsilon(1e-14));
    CHECK_THROWS_AS((void)FrameRotation::Normalize(0.0, 0.0, 0.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)FrameRotation::Normalize(0.0, 0.0, 0.0, std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)aroundZ.Apply({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}), std::invalid_argument);
}

TEST_CASE("Sector coordinates canonicalize signed boundaries without changing their value")
{
    using Keire::Detail::SectorCoordinate;
    for (int sector = -8; sector <= 8; ++sector)
    {
        for (int step = -128; step <= 128; ++step)
        {
            const double offset = static_cast<double>(step) * 1024.0 + 0.125;
            const auto value = SectorCoordinate::Normalize(sector, offset);
            CHECK(value.Offset() >= -SectorCoordinate::HalfSector);
            CHECK(value.Offset() < SectorCoordinate::HalfSector);
            CHECK(static_cast<double>(value.Sector()) * SectorCoordinate::SectorSize + value.Offset() ==
                  static_cast<double>(sector) * SectorCoordinate::SectorSize + offset);
            CHECK(SectorCoordinate::Normalize(value.Sector(), value.Offset()) == value);
        }
    }
    CHECK(SectorCoordinate::Normalize(0, 32768.0) == SectorCoordinate::Normalize(1, -32768.0));
    CHECK(SectorCoordinate::Normalize(0, -32768.0).Sector() == 0);
    CHECK_FALSE(std::signbit(SectorCoordinate::Normalize(0, -0.0).Offset()));
    CHECK(SectorCoordinate::Normalize(0, std::numeric_limits<double>::denorm_min()).Offset() ==
          std::numeric_limits<double>::denorm_min());
}

TEST_CASE("Sector differences preserve nearby detail at extreme integer addresses")
{
    using Keire::Detail::SectorCoordinate;
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    for (const auto sector : std::array<std::int64_t, 5>{minimum, -1000000000000LL, 0, 1000000000000LL, maximum})
    {
        const auto origin = SectorCoordinate::Normalize(sector, 0.0);
        const auto nearby = SectorCoordinate::Normalize(sector, 0.001);
        CHECK(nearby.RelativeTo(origin) == 0.001);
        CHECK(origin.RelativeTo(nearby) == -0.001);
        CHECK(origin.RelativeTo(origin) == 0.0);
    }
    const auto left = SectorCoordinate::Normalize(maximum - 1, 32767.999);
    const auto right = SectorCoordinate::Normalize(maximum, -32768.0);
    CHECK(right.RelativeTo(left) == doctest::Approx(0.001).epsilon(0.000001));
    const auto low = SectorCoordinate::Normalize(minimum, 0.0);
    const auto high = SectorCoordinate::Normalize(maximum, 0.0);
    CHECK(std::isfinite(high.RelativeTo(low)));
    CHECK(high.RelativeTo(low) == -low.RelativeTo(high));
}

TEST_CASE("Sector normalization rejects nonfinite offsets and address overflow")
{
    using Keire::Detail::SectorCoordinate;
    for (const auto offset :
         std::array{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS((void)SectorCoordinate::Normalize(0, offset), std::invalid_argument);
    }
    CHECK_THROWS_AS((void)SectorCoordinate::Normalize(0, std::numeric_limits<double>::max()), std::out_of_range);
    CHECK_THROWS_AS((void)SectorCoordinate::Normalize(std::numeric_limits<std::int64_t>::max(), 32768.0),
                    std::out_of_range);
    CHECK_THROWS_AS((void)SectorCoordinate::Normalize(std::numeric_limits<std::int64_t>::min(), -32769.0),
                    std::out_of_range);
}

TEST_CASE("Sector translation retains local detail across huge displacement and sector boundaries")
{
    using Keire::Detail::SectorCoordinate;
    const auto origin = SectorCoordinate::Normalize(0, 0.001);
    const auto moved = origin.Translated(65536.0 * 1000000000000.0);
    CHECK(moved.Sector() == 1000000000000LL);
    CHECK(moved.Offset() == 0.001);
    CHECK(moved.Translated(-65536.0 * 1000000000000.0) == origin);
    const auto boundary = SectorCoordinate::Normalize(std::numeric_limits<std::int64_t>::max(), -32768.0);
    CHECK(boundary.Translated(32768.0).Offset() == 0.0);
    CHECK_THROWS_AS((void)boundary.Translated(65536.0), std::out_of_range);
    CHECK(boundary.Offset() == -32768.0);
}

TEST_CASE("Three dimensional positions enforce spatial identity and transactional movement")
{
    using Keire::Detail::SectorPosition;
    const SectorPosition origin({1, 2}, {1000000000000LL, -1000000000000LL, 0}, {0.001, -0.002, 0.003});
    const auto moved = origin.Translated({65536.0, -65536.0, 131072.0});
    CHECK(moved.Identity() == origin.Identity());
    CHECK(moved.RelativeTo(origin) == std::array<double, 3>{65536.0, -65536.0, 131072.0});
    CHECK(moved.Translated({-65536.0, 65536.0, -131072.0}) == origin);
    const SectorPosition otherWorld({2, 2}, {}, {});
    const SectorPosition otherFrame({1, 3}, {}, {});
    CHECK_THROWS_AS((void)origin.RelativeTo(otherWorld), std::invalid_argument);
    CHECK_THROWS_AS((void)origin.RelativeTo(otherFrame), std::invalid_argument);
    CHECK_THROWS_AS((void)SectorPosition({0, 2}, {}, {}), std::invalid_argument);
    CHECK_THROWS_AS((void)SectorPosition({1, 0}, {}, {}), std::invalid_argument);
    const SectorPosition edge({1, 2}, {0, std::numeric_limits<std::int64_t>::max(), 0}, {});
    const auto before = edge;
    CHECK_THROWS_AS((void)edge.Translated({1.0, 65536.0, 1.0}), std::out_of_range);
    CHECK(edge == before);
    CHECK_THROWS_AS((void)edge.Translated({1.0, 0.0, std::numeric_limits<double>::quiet_NaN()}), std::invalid_argument);
    CHECK(edge == before);
}

TEST_CASE("Spatial coordinate records preserve extreme addresses and exact double bits")
{
    using Keire::Detail::SectorPosition;
    using Keire::Detail::SectorPositionCodec;
    const SectorPosition position(
        {std::numeric_limits<std::uint64_t>::max(), 2},
        {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), -1},
        {0.001, -32768.0, std::numeric_limits<double>::denorm_min()});
    const auto bytes = SectorPositionCodec::Encode(position);
    CHECK(bytes[0] == std::byte{0x4B});
    CHECK(bytes[3] == std::byte{1});
    CHECK(bytes[39] == std::byte{0x80});
    CHECK(bytes[48] == std::byte{0xFF});
    const auto restored = SectorPositionCodec::Decode(bytes);
    CHECK(restored == position);
    CHECK(SectorPositionCodec::Encode(restored) == bytes);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        CHECK(std::bit_cast<std::uint64_t>(restored.Axes()[axis].Offset()) ==
              std::bit_cast<std::uint64_t>(position.Axes()[axis].Offset()));
    }
}

TEST_CASE("Spatial coordinate records reject truncation unsupported formats and malformed offsets")
{
    using Keire::Detail::SectorPosition;
    using Keire::Detail::SectorPositionCodec;
    const auto valid = SectorPositionCodec::Encode(SectorPosition({1, 2}, {}, {}));
    for (std::size_t size = 0; size < valid.size(); ++size)
    {
        CHECK_THROWS_AS((void)SectorPositionCodec::Decode(std::span(valid).first(size)), std::invalid_argument);
    }
    auto extended = std::vector<std::byte>(valid.begin(), valid.end());
    extended.push_back(std::byte{});
    CHECK_THROWS_AS((void)SectorPositionCodec::Decode(extended), std::invalid_argument);
    for (const auto index : std::array<std::size_t, 5>{0, 3, 7, 8, 15})
    {
        auto changed = valid;
        changed[index] ^= std::byte{0x80};
        CHECK_THROWS_AS((void)SectorPositionCodec::Decode(changed), std::invalid_argument);
    }
    for (const auto offset : std::array{-0.0, 32768.0, -32769.0, std::numeric_limits<double>::infinity(),
                                        std::numeric_limits<double>::quiet_NaN()})
    {
        auto changed = valid;
        const auto bits = std::bit_cast<std::uint64_t>(offset);
        for (std::size_t byte = 0; byte < 8; ++byte)
        {
            changed[40 + byte] = static_cast<std::byte>((bits >> (byte * 8)) & 0xFFU);
        }
        CHECK_THROWS_AS((void)SectorPositionCodec::Decode(changed), std::invalid_argument);
    }
    auto missingWorld = valid;
    missingWorld[16] = std::byte{};
    CHECK_THROWS_AS((void)SectorPositionCodec::Decode(missingWorld), std::invalid_argument);
}
