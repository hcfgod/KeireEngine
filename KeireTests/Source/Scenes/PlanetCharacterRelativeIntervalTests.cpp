#include "KeireInternal/Spatial/PlanetCharacterRelativeInterval.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

TEST_CASE("Planet character relative bounds enclose independently translating and rotating obstacles")
{
    using namespace Keire::Detail;
    for (const double width : {1.0, 0.1, 0.001})
    {
        const double begin = 0.0;
        const double end = width;
        const double midpoint = (begin + end) * 0.5;
        PlanetCharacterPoseInterval character;
        character.Midpoint.Center = {static_cast<float>(3.0 + midpoint), 2.0F, 0.0F};
        character.Midpoint.Radius = 0.25F;
        character.Midpoint.Height = 2.0F;
        character.CenterExcursion = width * 0.5 + 1.0e-6;
        PlanetCharacterPoseInterval obstacle;
        obstacle.Midpoint.Center = {static_cast<float>(0.5 * midpoint), 0.0F, 0.0F};
        obstacle.Midpoint.Rotation = {0.0F, 0.0F, static_cast<float>(std::sin(midpoint * 0.5)),
                                      static_cast<float>(std::cos(midpoint * 0.5))};
        obstacle.CenterExcursion = width * 0.25 + 1.0e-6;
        obstacle.AngularExcursion = width * 0.5 + 1.0e-6;
        const auto relative = PlanetCharacterRelativeInterval(character, obstacle, {}, {});
        const auto envelope =
            PlanetCharacterPoseEnvelope(relative.Midpoint, relative.AngularExcursion, relative.CenterExcursion);
        const auto rotation = FrameRotation::Normalize(envelope.Rotation.X, envelope.Rotation.Y, envelope.Rotation.Z,
                                                       envelope.Rotation.W);
        const auto up = rotation.Apply({0.0, 1.0, 0.0});
        const double halfSegment = static_cast<double>(envelope.Height) * 0.5 - envelope.Radius;
        for (int sample = 0; sample <= 100; ++sample)
        {
            const double time = width * sample / 100.0;
            for (const double tip : {-0.75, 0.75})
            {
                // Independent inverse Z rotation into the obstacle's original pose.
                const double x = (3.0 + 0.5 * time) * std::cos(time) + (2.0 + tip) * std::sin(time);
                const double y = -(3.0 + 0.5 * time) * std::sin(time) + (2.0 + tip) * std::cos(time);
                const std::array offset{x - envelope.Center.X, y - envelope.Center.Y,
                                        -static_cast<double>(envelope.Center.Z)};
                const double projection =
                    std::clamp(offset[0] * up[0] + offset[1] * up[1] + offset[2] * up[2], -halfSegment, halfSegment);
                const double distance = std::hypot(offset[0] - projection * up[0], offset[1] - projection * up[1],
                                                   offset[2] - projection * up[2]);
                CHECK(distance + character.Midpoint.Radius <= static_cast<double>(envelope.Radius));
            }
        }
        obstacle.CenterExcursion = -1.0;
        CHECK_THROWS_AS((void)PlanetCharacterRelativeInterval(character, obstacle, {}, {}), std::invalid_argument);
        obstacle.CenterExcursion = 0.0;
        obstacle.Midpoint.Center.X = std::numeric_limits<float>::infinity();
        CHECK_THROWS_AS((void)PlanetCharacterRelativeInterval(character, obstacle, {}, {}), std::invalid_argument);
    }
}
