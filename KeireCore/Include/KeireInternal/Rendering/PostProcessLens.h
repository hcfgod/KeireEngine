#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Keire::Detail
{
    struct PostProcessLens
    {
        double FocusDistanceMetres = 10.0;
        double FocalLengthMillimetres = 50.0;
        double ApertureFNumber = 5.6;
        double SensorHeightMillimetres = 24.0;
        double Strength = 1.0;
        double MaximumRadiusFraction = 0.05;
    };

    [[nodiscard]] constexpr std::uint32_t PostProcessDofReducedExtent(const std::uint32_t full,
                                                                      const std::uint32_t divisor)
    {
        if (divisor == 0U)
        {
            throw std::invalid_argument("Depth-of-field reduction divisor must be positive.");
        }
        return std::max(1U, full / divisor + (full % divisor != 0U ? 1U : 0U));
    }

    [[nodiscard]] inline double PostProcessDofFullToReducedUv(const double uv, const std::uint32_t fullExtent,
                                                              const std::uint32_t divisor)
    {
        if (!std::isfinite(uv) || fullExtent == 0U || divisor == 0U)
        {
            throw std::invalid_argument("Depth-of-field UV mapping requires finite coordinates and positive extents.");
        }
        const double count = PostProcessDofReducedExtent(fullExtent, divisor);
        if (count <= 1.0)
        {
            return 0.5;
        }
        const double coordinate = uv * fullExtent;
        const double remainder = fullExtent - (count - 1.0) * divisor;
        const double previousCenter = (count - 1.5) * divisor;
        const double lastCenter = (count - 1.0) * divisor + remainder * 0.5;
        if (remainder < divisor && coordinate > previousCenter)
        {
            const double index = count - 2.0 + (coordinate - previousCenter) / (lastCenter - previousCenter);
            return (index + 0.5) / count;
        }
        return coordinate / (divisor * count);
    }

    [[nodiscard]] inline double PostProcessDofReducedToFullUv(const double uv, const std::uint32_t fullExtent,
                                                              const std::uint32_t divisor)
    {
        if (!std::isfinite(uv) || fullExtent == 0U || divisor == 0U)
        {
            throw std::invalid_argument("Depth-of-field UV mapping requires finite coordinates and positive extents.");
        }
        const double count = PostProcessDofReducedExtent(fullExtent, divisor);
        if (count <= 1.0)
        {
            return 0.5;
        }
        const double index = uv * count - 0.5;
        const double remainder = fullExtent - (count - 1.0) * divisor;
        const double previousCenter = (count - 1.5) * divisor;
        const double lastCenter = (count - 1.0) * divisor + remainder * 0.5;
        const double coordinate = remainder < divisor && index > count - 2.0
                                      ? std::lerp(previousCenter, lastCenter, index - (count - 2.0))
                                      : (index + 0.5) * divisor;
        return std::clamp(coordinate, 0.5, fullExtent - 0.5) / fullExtent;
    }

    // Reference near-layer quadrature weight. A foreground source may cover an in-focus destination,
    // while farther sources must not cover a closer foreground destination.
    [[nodiscard]] inline double PostProcessDofNearWeight(const double sourceRadius, const double destinationRadius,
                                                         const double distancePixels, const double searchRadius)
    {
        if (!std::isfinite(sourceRadius) || !std::isfinite(destinationRadius) || !std::isfinite(distancePixels) ||
            distancePixels < 0.0 || !std::isfinite(searchRadius) || searchRadius < 0.0)
        {
            throw std::invalid_argument("Depth-of-field coverage requires finite pixel radii and distances.");
        }
        if (sourceRadius <= 0.5 || sourceRadius < destinationRadius - 1.0)
        {
            return 0.0;
        }
        const double support = std::clamp(sourceRadius - distancePixels + 1.0, 0.0, 1.0);
        return support * searchRadius * searchRadius / std::max(sourceRadius * sourceRadius, 1.0);
    }

    struct PostProcessLensCoefficients
    {
        double ReciprocalDepthScale = 0.0;
        double Bias = 0.0;
        double MaximumRadiusPixels = 0.0;
    };

    [[nodiscard]] inline PostProcessLensCoefficients MakePostProcessLensCoefficients(const PostProcessLens& lens,
                                                                                     const double imageHeightPixels)
    {
        const double focal = lens.FocalLengthMillimetres * 0.001;
        const double sensor = lens.SensorHeightMillimetres * 0.001;
        if (!std::isfinite(lens.FocusDistanceMetres) || !std::isfinite(focal) || focal <= 0.0 ||
            lens.FocusDistanceMetres <= focal || !std::isfinite(sensor) || sensor <= 0.0 ||
            !std::isfinite(lens.ApertureFNumber) || lens.ApertureFNumber <= 0.0 || !std::isfinite(imageHeightPixels) ||
            imageHeightPixels <= 0.0 || !std::isfinite(lens.Strength) || lens.Strength < 0.0 || lens.Strength > 1.0 ||
            !std::isfinite(lens.MaximumRadiusFraction) || lens.MaximumRadiusFraction < 0.0 ||
            lens.MaximumRadiusFraction > 1.0)
        {
            throw std::invalid_argument("Depth-of-field lens coefficients require valid physical units.");
        }
        const double scale = (focal / lens.ApertureFNumber) * 0.5 * (focal / (lens.FocusDistanceMetres - focal)) *
                             (imageHeightPixels / sensor) * lens.Strength;
        const PostProcessLensCoefficients result{scale * lens.FocusDistanceMetres, scale,
                                                 imageHeightPixels * lens.MaximumRadiusFraction};
        if (!std::isfinite(result.ReciprocalDepthScale) || !std::isfinite(result.Bias) ||
            !std::isfinite(result.MaximumRadiusPixels))
        {
            throw std::invalid_argument("Depth-of-field coefficients exceed the supported numeric range.");
        }
        return result;
    }

    /// Full-resolution pixel radius: positive for near field, negative for far field.
    /// Distances are positive camera-space distances, never absolute world coordinates.
    [[nodiscard]] inline double PostProcessCircleOfConfusion(const PostProcessLens& lens,
                                                             const double objectDistanceMetres,
                                                             const double imageHeightPixels)
    {
        const auto positiveFinite = [](const double value) { return std::isfinite(value) && value > 0.0; };
        const double focalLengthMetres = lens.FocalLengthMillimetres * 0.001;
        const double sensorHeightMetres = lens.SensorHeightMillimetres * 0.001;
        if (!positiveFinite(lens.FocusDistanceMetres) || !positiveFinite(focalLengthMetres) ||
            !positiveFinite(sensorHeightMetres) || !positiveFinite(lens.ApertureFNumber) ||
            !positiveFinite(imageHeightPixels) || lens.FocusDistanceMetres <= focalLengthMetres ||
            !std::isfinite(lens.Strength) || lens.Strength < 0.0 || lens.Strength > 1.0 ||
            !std::isfinite(lens.MaximumRadiusFraction) || lens.MaximumRadiusFraction < 0.0 ||
            lens.MaximumRadiusFraction > 1.0 || std::isnan(objectDistanceMetres) || objectDistanceMetres <= 0.0)
        {
            throw std::invalid_argument(
                "Depth of field requires valid lens units and a focus beyond the focal length.");
        }
        if (lens.Strength == 0.0 || lens.MaximumRadiusFraction == 0.0 ||
            objectDistanceMetres == lens.FocusDistanceMetres)
        {
            return 0.0;
        }
        // Thin-lens CoC, rearranged to avoid multiplying two potentially planetary-scale distances.
        // Positive infinity represents sky/background and converges to the negative far-field limit.
        const double apertureRadiusMetres = (focalLengthMetres / lens.ApertureFNumber) * 0.5;
        const double scale = apertureRadiusMetres *
                             (focalLengthMetres / (lens.FocusDistanceMetres - focalLengthMetres)) *
                             (imageHeightPixels / sensorHeightMetres) * lens.Strength;
        if (!std::isfinite(scale))
        {
            throw std::invalid_argument("Depth-of-field lens coefficients exceed the supported numeric range.");
        }
        if (scale == 0.0)
        {
            return 0.0;
        }
        const double radius = scale * (lens.FocusDistanceMetres / objectDistanceMetres - 1.0);
        const double limit = imageHeightPixels * lens.MaximumRadiusFraction;
        return std::clamp(radius, -limit, limit);
    }
} // namespace Keire::Detail
