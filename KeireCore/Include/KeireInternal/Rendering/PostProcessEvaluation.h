#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"
#include "Keire/ECS/Components/PostProcessVolumeComponent.h"
#include "KeireInternal/Rendering/PostProcessProfileCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    struct PostProcessVolumePose
    {
        Vector3d Center;
        Quaternion Rotation;
        Vector3 Scale{1.0F, 1.0F, 1.0F};
    };

    /// Distance is measured outside the boundary in metres; inside receives the full authored weight.
    /// Sphere volumes remain spheres under nonuniform scale, using the largest absolute scale axis.
    [[nodiscard]] inline double PostProcessVolumeWeight(const PostProcessVolumeSettings& settings,
                                                        const PostProcessVolumePose& pose, const Vector3d camera)
    {
        PostProcessVolumeComponent::Validate(settings);
        if (!Math::IsFinite(camera) || !Math::IsFinite(pose.Center) || !Math::IsFinite(pose.Rotation) ||
            !Math::IsFinite(pose.Scale))
        {
            throw std::invalid_argument("Post-processing volume pose and camera must be finite.");
        }
        if (settings.Global)
        {
            return settings.Weight;
        }
        const double scaleX = std::abs(static_cast<double>(pose.Scale.X));
        const double scaleY = std::abs(static_cast<double>(pose.Scale.Y));
        const double scaleZ = std::abs(static_cast<double>(pose.Scale.Z));
        if (scaleX == 0.0 || scaleY == 0.0 || scaleZ == 0.0)
        {
            throw std::invalid_argument("Local post-processing volumes require nonzero scale.");
        }
        const double qLength =
            std::hypot(std::hypot(pose.Rotation.X, pose.Rotation.Y), std::hypot(pose.Rotation.Z, pose.Rotation.W));
        if (!std::isfinite(qLength) || qLength == 0.0)
        {
            throw std::invalid_argument("Local post-processing volumes require a nonzero rotation.");
        }
        // Subtract absolute coordinates before rotating; never narrow the world position to float.
        const Vector3d relative{camera.X - pose.Center.X, camera.Y - pose.Center.Y, camera.Z - pose.Center.Z};
        if (!Math::IsFinite(relative))
        {
            return 0.0;
        }
        double distance = 0.0;
        if (settings.Shape == PostProcessVolumeShape::Sphere)
        {
            const double radius = settings.Radius * std::max({scaleX, scaleY, scaleZ});
            if (!std::isfinite(radius))
            {
                throw std::invalid_argument("Scaled post-processing sphere exceeds the supported range.");
            }
            distance = std::max(0.0, std::hypot(relative.X, relative.Y, relative.Z) - radius);
        }
        else
        {
            const double x = pose.Rotation.X / qLength;
            const double y = pose.Rotation.Y / qLength;
            const double z = pose.Rotation.Z / qLength;
            const double w = pose.Rotation.W / qLength;
            // Transpose the normalized rotation to measure distance in the oriented box's axes.
            const Vector3d local{(1.0 - 2.0 * (y * y + z * z)) * relative.X + 2.0 * (x * y + z * w) * relative.Y +
                                     2.0 * (x * z - y * w) * relative.Z,
                                 2.0 * (x * y - z * w) * relative.X + (1.0 - 2.0 * (x * x + z * z)) * relative.Y +
                                     2.0 * (y * z + x * w) * relative.Z,
                                 2.0 * (x * z + y * w) * relative.X + 2.0 * (y * z - x * w) * relative.Y +
                                     (1.0 - 2.0 * (x * x + y * y)) * relative.Z};
            const Vector3d extents{settings.HalfExtents.X * scaleX, settings.HalfExtents.Y * scaleY,
                                   settings.HalfExtents.Z * scaleZ};
            if (!Math::IsFinite(extents))
            {
                throw std::invalid_argument("Scaled post-processing box exceeds the supported range.");
            }
            if (!Math::IsFinite(local))
            {
                return 0.0;
            }
            distance =
                std::hypot(std::max(0.0, std::abs(local.X) - extents.X), std::max(0.0, std::abs(local.Y) - extents.Y),
                           std::max(0.0, std::abs(local.Z) - extents.Z));
        }
        if (distance == 0.0)
        {
            return settings.Weight;
        }
        if (settings.BlendDistance == 0.0 || distance >= settings.BlendDistance)
        {
            return 0.0;
        }
        const double t = 1.0 - distance / settings.BlendDistance;
        return settings.Weight * t * t * (3.0 - 2.0 * t);
    }

    struct EvaluatedPostProcessing
    {
        std::array<double, static_cast<std::size_t>(PostProcessParameter::Count)> Parameters{};
        // Continuous enable weights avoid a visible Boolean switch at a volume boundary.
        std::array<double, static_cast<std::size_t>(PostProcessEffect::Count)> EffectWeights{};
        AssetId ColorLookup;
        double ColorLookupWeight = 1.0;
        std::vector<PostProcessCustomEffect> CustomEffects;

        EvaluatedPostProcessing()
        {
            const auto descriptions = PostProcessParameterDescriptions();
            for (std::size_t index = 0; index < Parameters.size(); ++index)
            {
                Parameters[index] = descriptions[index].Default;
            }
        }
    };

    struct WeightedPostProcessProfile
    {
        AssetId StableVolumeId;
        std::int32_t Priority = 0;
        double Weight = 0.0;
        const PostProcessProfileDefinition* Profile = nullptr;
        AssetId StableSceneId;
    };

    /// Caller owns profile lifetimes and reuses contribution storage. Higher priority is applied last;
    /// equal priorities use stable entity identity, independent of registry or asset completion order.
    /// LUT reference overrides switch discretely at volume weight >= 0.5, including explicit null clears.
    /// LUT strength blends independently; different LUT resources are never numerically interpolated.
    /// Custom stacks also switch at weight >= 0.5; shader programs cannot be numerically interpolated.
    [[nodiscard]] inline EvaluatedPostProcessing
    BlendPostProcessProfiles(std::span<WeightedPostProcessProfile> contributions, EvaluatedPostProcessing baseline = {})
    {
        const auto descriptions = PostProcessParameterDescriptions();
        if (baseline.CustomEffects.size() > MaximumPostProcessCustomEffects ||
            std::ranges::any_of(baseline.CustomEffects, [](const auto& effect)
                                { return !effect.Material || effect.Stage > PostProcessCustomStage::AfterUi; }))
        {
            throw std::invalid_argument("Invalid post-processing baseline custom stack.");
        }
        for (std::size_t index = 0; index < baseline.Parameters.size(); ++index)
        {
            const double value = baseline.Parameters[index];
            if (!std::isfinite(value) || value < descriptions[index].Minimum || value > descriptions[index].Maximum)
            {
                throw std::invalid_argument("Invalid post-processing baseline parameter.");
            }
        }
        for (const double weight : baseline.EffectWeights)
        {
            if (!std::isfinite(weight) || weight < 0.0 || weight > 1.0)
            {
                throw std::invalid_argument("Invalid post-processing baseline effect weight.");
            }
        }
        if (!std::isfinite(baseline.ColorLookupWeight) || baseline.ColorLookupWeight < 0.0 ||
            baseline.ColorLookupWeight > 1.0)
        {
            throw std::invalid_argument("Invalid post-processing baseline color lookup weight.");
        }
        for (const auto& contribution : contributions)
        {
            if (!contribution.StableVolumeId || !contribution.Profile || !std::isfinite(contribution.Weight) ||
                contribution.Weight < 0.0 || contribution.Weight > 1.0)
            {
                throw std::invalid_argument("Invalid post-processing profile contribution.");
            }
            PostProcessProfileAsset::Validate(*contribution.Profile);
        }
        std::ranges::sort(contributions,
                          [](const auto& left, const auto& right)
                          {
                              if (left.Priority != right.Priority)
                              {
                                  return left.Priority < right.Priority;
                              }
                              if (left.StableSceneId != right.StableSceneId)
                              {
                                  return left.StableSceneId < right.StableSceneId;
                              }
                              return left.StableVolumeId < right.StableVolumeId;
                          });
        const std::vector<PostProcessCustomEffect>* selectedCustomStack = nullptr;
        for (const auto& contribution : contributions)
        {
            if (contribution.Weight == 0.0)
            {
                continue;
            }
            if (contribution.Profile->ColorLookup.Override && contribution.Weight >= 0.5)
            {
                baseline.ColorLookup = contribution.Profile->ColorLookup.Value;
            }
            if (contribution.Profile->CustomEffects.Override && contribution.Weight >= 0.5)
            {
                selectedCustomStack = &contribution.Profile->CustomEffects.Value;
            }
            if (contribution.Profile->ColorLookupWeight.Override)
            {
                baseline.ColorLookupWeight = std::lerp(
                    baseline.ColorLookupWeight, contribution.Profile->ColorLookupWeight.Value, contribution.Weight);
            }
            for (std::size_t index = 0; index < baseline.Parameters.size(); ++index)
            {
                const auto& parameter = contribution.Profile->Parameters[index];
                if (parameter.Override)
                {
                    baseline.Parameters[index] =
                        std::lerp(baseline.Parameters[index], parameter.Value, contribution.Weight);
                }
            }
            for (std::size_t index = 0; index < baseline.EffectWeights.size(); ++index)
            {
                const auto& effect = contribution.Profile->Effects[index];
                if (effect.Override)
                {
                    baseline.EffectWeights[index] =
                        std::lerp(baseline.EffectWeights[index], effect.Value ? 1.0 : 0.0, contribution.Weight);
                }
            }
        }
        if (selectedCustomStack)
        {
            baseline.CustomEffects = *selectedCustomStack;
        }
        return baseline;
    }
} // namespace Keire::Detail
