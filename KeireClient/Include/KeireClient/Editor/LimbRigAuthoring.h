#pragma once

#include "Keire/Animation/RiggingSystem.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    enum class LimbRigLoadProgress
    {
        Idle,
        Loading,
        Loaded,
        Cancelled
    };

    class LimbRigDraftLoad final
    {
      public:
        void Begin(Keire::AssetId asset, Keire::Ref<const Keire::SkeletonAsset> skeleton)
        {
            m_Asset = asset;
            m_Skeleton = std::move(skeleton);
        }
        void Cancel() noexcept
        {
            m_Asset = {};
            m_Skeleton = {};
        }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] LimbRigLoadProgress Complete(const Keire::Ref<const Keire::SkeletonAsset>& skeleton,
                                                   const Keire::Ref<const Keire::RigDefinitionAsset>& asset,
                                                   std::vector<Keire::LimbDefinition>& draft)
        {
            if (!m_Asset)
                return LimbRigLoadProgress::Idle;
            if (skeleton != m_Skeleton)
            {
                Cancel();
                return LimbRigLoadProgress::Cancelled;
            }
            if (!asset)
                return LimbRigLoadProgress::Loading;
            try
            {
                if (asset->Definition().Limbs.empty())
                    throw std::invalid_argument(
                        "This rig has no explicit limb chains. Select a saved schema-2 limb rig.");
                const Keire::BoundLimbRig validated(skeleton, asset->Definition().Limbs);
                (void)validated;
                auto candidate = asset->Definition().Limbs;
                draft = std::move(candidate);
            }
            catch (...)
            {
                Cancel();
                throw;
            }
            Cancel();
            return LimbRigLoadProgress::Loaded;
        }

      private:
        Keire::AssetId m_Asset;
        Keire::Ref<const Keire::SkeletonAsset> m_Skeleton;
    };

    inline void UpsertAuthoredLimb(const Keire::Ref<const Keire::SkeletonAsset>& skeleton,
                                   std::vector<Keire::LimbDefinition>& limbs, Keire::LimbDefinition limb)
    {
        auto candidate = limbs;
        const auto found = std::ranges::find(candidate, limb.Id, &Keire::LimbDefinition::Id);
        if (found == candidate.end())
            candidate.push_back(std::move(limb));
        else
            *found = std::move(limb);
        const Keire::BoundLimbRig validated(skeleton, candidate);
        (void)validated;
        limbs = std::move(candidate);
    }

    [[nodiscard]] inline Keire::LimbDefinition
    MirrorAuthoredLimbNames(const Keire::LimbDefinition& source, const std::string_view from, const std::string_view to,
                            const Keire::LimbId id, const std::string_view name)
    {
        if (from.empty() || to.empty() || from == to)
            throw std::invalid_argument("Enter different non-empty source and destination bone-name tokens.");
        auto result = source;
        result.Id = id;
        result.Name = name;
        for (auto& bone : result.Bones)
        {
            const auto at = bone.find(from);
            if (at == std::string::npos || bone.find(from, at + from.size()) != std::string::npos)
                throw std::invalid_argument(
                    "Every source bone must contain the source token exactly once; no bones were changed.");
            bone.replace(at, from.size(), to);
        }
        // Mirroring names establishes topology only. It cannot infer a geometric reflection plane.
        result.PreferredBendDirection.reset();
        return result;
    }
} // namespace KeireEditor
