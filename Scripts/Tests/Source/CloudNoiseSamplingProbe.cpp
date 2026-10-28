// CPU reference for repeat-addressed trilinear sampling. This does not qualify GPU sampler execution.
#include "CloudNoiseSamplingReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace
{
    using Point = std::array<double, 3>;

    using KeireTest::SampleCloudChannel;

    Point WarpPosition(const Keire::RenderBackend::CloudNoiseMip& mip, const Point& position,
                       const double strength = 0.75, const double period = 64.0)
    {
        const Point macro{position[0] / period, position[1] / period, position[2] / period};
        auto warped = position;
        for (std::size_t axis = 0; axis < warped.size(); ++axis)
        {
            warped[axis] += (SampleCloudChannel(mip, macro, axis + 1) - 0.5) * strength;
        }
        return warped;
    }

    double SampleWarpedShape(const Keire::RenderBackend::CloudNoiseMip& mip, const Point& position)
    {
        return SampleCloudChannel(mip, WarpPosition(mip, position));
    }

    void ReportWarpStretch(const Keire::RenderBackend::CloudNoiseMip& mip)
    {
        constexpr double interval = 0.0001;
        double maximumStretch = 0.0;
        double minimumStretch = 100.0;
        for (int z = 0; z < 16; ++z)
        {
            for (int y = 0; y < 16; ++y)
            {
                for (int x = 0; x < 16; ++x)
                {
                    const Point point{x * 4.0 + 0.37, y * 4.0 + 0.23, z * 4.0 + 0.61};
                    const auto origin = WarpPosition(mip, point);
                    for (std::size_t axis = 0; axis < point.size(); ++axis)
                    {
                        auto next = point;
                        next[axis] += interval;
                        const auto displaced = WarpPosition(mip, next);
                        double lengthSquared = 0.0;
                        for (std::size_t component = 0; component < point.size(); ++component)
                        {
                            const double derivative = (displaced[component] - origin[component]) / interval;
                            lengthSquared += derivative * derivative;
                        }
                        const double stretch = std::sqrt(lengthSquared);
                        maximumStretch = std::max(maximumStretch, stretch);
                        minimumStretch = std::min(minimumStretch, stretch);
                    }
                }
            }
        }
        std::cout << "sampled_axis_stretch_min=" << minimumStretch << " sampled_axis_stretch_max=" << maximumStretch
                  << '\n';
    }

    void Require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
{
    try
    {
        const auto volume = Keire::RenderBackend::BuildCloudNoiseVolume(64, 1);
        double customizationWrapError = 0.0;
        for (const double period : {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0})
        {
            for (const double strength : {0.0, 0.75, 1.0})
            {
                for (const double frequency : {1.0, 4.0, 16.0})
                {
                    for (int sample = -32; sample <= 32; ++sample)
                    {
                        const Point point{sample * 0.37, -sample * 0.23, sample * 0.61};
                        auto original = WarpPosition(volume.Mips.front(), point, strength, period);
                        for (auto& component : original)
                        {
                            component *= frequency;
                        }
                        for (std::size_t axis = 0; axis < point.size(); ++axis)
                        {
                            auto shifted = point;
                            shifted[axis] += 64.0;
                            auto wrapped = WarpPosition(volume.Mips.front(), shifted, strength, period);
                            for (auto& component : wrapped)
                            {
                                component *= frequency;
                            }
                            customizationWrapError = std::max(
                                customizationWrapError, std::abs(SampleCloudChannel(volume.Mips.front(), original) -
                                                                 SampleCloudChannel(volume.Mips.front(), wrapped)));
                        }
                    }
                }
            }
        }
        Require(customizationWrapError < 1.0e-10, "Custom cloud controls break the wrapped host coordinate domain");
        std::cout << "customization_wrap_error=" << customizationWrapError << '\n';
        ReportWarpStretch(volume.Mips.front());
        double adjacentDifference = 0.0;
        double wrappedError = 0.0;
        double warpedSeam = 0.0;
        for (int sample = 0; sample < 256; ++sample)
        {
            const Point point{(sample + 0.37) / 16.0, 0.271, -0.413};
            const auto& base = volume.Mips.front();
            const double original = SampleWarpedShape(base, point);
            for (std::size_t axis = 0; axis < point.size(); ++axis)
            {
                auto shifted = point;
                shifted[axis] += 1.0;
                adjacentDifference += std::abs(original - SampleWarpedShape(base, shifted));
                shifted = point;
                shifted[axis] += 64.0;
                wrappedError = std::max(wrappedError, std::abs(original - SampleWarpedShape(base, shifted)));
                auto left = point;
                auto right = point;
                left[axis] = -1.0e-7;
                right[axis] = 1.0e-7;
                warpedSeam =
                    std::max(warpedSeam, std::abs(SampleWarpedShape(base, left) - SampleWarpedShape(base, right)));
            }
        }
        Require(adjacentDifference / 768.0 > 0.01, "Warped field still repeats in adjacent tiles");
        Require(wrappedError < 1.0e-11, "Warped field breaks the host phase period");
        Require(warpedSeam < 1.0e-4, "Warped field has a coordinate seam");
        std::cout << "warped_adjacent_mean_delta=" << adjacentDifference / 768.0 << " host_wrap_error=" << wrappedError
                  << " seam_delta=" << warpedSeam << '\n';
        for (const auto& mip : volume.Mips)
        {
            double repeatError = 0.0;
            double seamDifference = 0.0;
            double quarterPeriodDifference = 0.0;
            for (int sample = 0; sample < 256; ++sample)
            {
                const Point point{(sample + 0.37) / 256.0, 0.271, -0.413};
                const double original = SampleCloudChannel(mip, point);
                for (std::size_t axis = 0; axis < point.size(); ++axis)
                {
                    auto shifted = point;
                    shifted[axis] += 1.0;
                    repeatError = std::max(repeatError, std::abs(original - SampleCloudChannel(mip, shifted)));
                    shifted = point;
                    shifted[axis] += 0.25;
                    quarterPeriodDifference += std::abs(original - SampleCloudChannel(mip, shifted));
                    auto left = point;
                    auto right = point;
                    left[axis] = -1.0e-7;
                    right[axis] = 1.0e-7;
                    seamDifference = std::max(seamDifference,
                                              std::abs(SampleCloudChannel(mip, left) - SampleCloudChannel(mip, right)));
                }
                Require(std::isfinite(original) && original >= 0.0 && original <= 1.0, "Invalid sampled shape");
            }
            Require(repeatError < 1.0e-12, "Repeat addressing is inconsistent");
            Require(seamDifference < 1.0e-4, "Periodic sampling is discontinuous");
            std::cout << "dimension=" << mip.Dimension << " repeat_error=" << repeatError
                      << " seam_delta=" << seamDifference
                      << " quarter_period_mean_delta=" << quarterPeriodDifference / 768.0 << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
