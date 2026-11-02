#pragma once

#include "Keire/Assets/Asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Keire::Detail
{
    struct PlanetWeatherTransport
    {
        std::array<double, 3> Radiance{};
        std::array<double, 3> Transmission{1.0, 1.0, 1.0};
    };

    struct PlanetWeatherShellEvent
    {
        double Distance = 0.0;
        AssetId Planet;
        std::uint32_t Layer = 0;
        std::uint32_t Crossing = 0;
        // Premultiplied emitted/scattered radiance and transmission of this shell alone.
        PlanetWeatherTransport Transport;
    };

    inline void AppendPlanetWeatherTransport(PlanetWeatherTransport& front, const PlanetWeatherTransport& behind)
    {
        auto combined = front;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!std::isfinite(front.Radiance[channel]) || front.Radiance[channel] < 0.0 ||
                !std::isfinite(front.Transmission[channel]) || front.Transmission[channel] < 0.0 ||
                front.Transmission[channel] > 1.0 || !std::isfinite(behind.Radiance[channel]) ||
                behind.Radiance[channel] < 0.0 || !std::isfinite(behind.Transmission[channel]) ||
                behind.Transmission[channel] < 0.0 || behind.Transmission[channel] > 1.0)
            {
                throw std::invalid_argument("Planet weather transport must be finite and nonnegative.");
            }
            combined.Radiance[channel] += front.Transmission[channel] * behind.Radiance[channel];
            combined.Transmission[channel] *= behind.Transmission[channel];
            if (!std::isfinite(combined.Radiance[channel]))
            {
                throw std::overflow_error("Planet weather radiance exceeds the transport range.");
            }
        }
        front = combined;
    }

    // Reference for GPU event ordering. The segment evaluator integrates atmosphere/fog over precisely
    // [begin, end], including overlapping media, without clouds or a second camera-to-event foreground.
    // maximumDistance is the nearest opaque surface (or the finite ray endpoint).
    template <typename SegmentEvaluator>
    [[nodiscard]] PlanetWeatherTransport ComposePlanetWeather(const std::span<const PlanetWeatherShellEvent> events,
                                                              const double maximumDistance,
                                                              SegmentEvaluator&& evaluateSegment)
    {
        constexpr std::size_t maximumEvents = 256;
        if (!std::isfinite(maximumDistance) || maximumDistance < 0.0 || events.size() > maximumEvents)
        {
            throw std::invalid_argument("Planet weather ray distance or event count is invalid.");
        }
        std::vector<PlanetWeatherShellEvent> ordered(events.begin(), events.end());
        std::set<std::tuple<AssetId, std::uint32_t, std::uint32_t>> identities;
        for (const auto& event : ordered)
        {
            if (!event.Planet || !std::isfinite(event.Distance) || event.Distance < 0.0)
            {
                throw std::invalid_argument("Planet weather shell identity or distance is invalid.");
            }
            if (!identities.emplace(event.Planet, event.Layer, event.Crossing).second)
            {
                throw std::invalid_argument("Planet weather shell crossing is duplicated.");
            }
            PlanetWeatherTransport validation;
            AppendPlanetWeatherTransport(validation, event.Transport);
        }
        std::ranges::sort(ordered,
                          [](const auto& left, const auto& right)
                          {
                              if (left.Distance != right.Distance)
                              {
                                  return left.Distance < right.Distance;
                              }
                              if (left.Planet != right.Planet)
                              {
                                  return left.Planet < right.Planet;
                              }
                              if (left.Layer != right.Layer)
                              {
                                  return left.Layer < right.Layer;
                              }
                              return left.Crossing < right.Crossing;
                          });
        PlanetWeatherTransport result;
        double current = 0.0;
        for (const auto& event : ordered)
        {
            if (event.Distance >= maximumDistance)
            {
                break;
            }
            if (event.Distance > current)
            {
                AppendPlanetWeatherTransport(result, evaluateSegment(current, event.Distance));
            }
            AppendPlanetWeatherTransport(result, event.Transport);
            current = event.Distance;
        }
        if (current < maximumDistance)
        {
            AppendPlanetWeatherTransport(result, evaluateSegment(current, maximumDistance));
        }
        return result;
    }
} // namespace Keire::Detail
