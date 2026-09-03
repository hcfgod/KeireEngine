#pragma once

#include "Keire/Math/Math.h"
#include "KeireInternal/Rendering/GlobalIlluminationPolicyInternal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr std::size_t MaximumIrradynSceneCards = 16U;
    inline constexpr std::size_t MaximumIrradynSceneCacheEntries = 2048U;

    struct IrradynSceneCard final
    {
        Vector4 CenterRadius;
        Vector4 RadianceDensity;
        Vector4 ExtentsFlags;

        bool operator==(const IrradynSceneCard&) const noexcept = default;
    };

    static_assert(sizeof(IrradynSceneCard) == sizeof(float) * 12U);

    struct IrradynSceneCardCandidate final
    {
        std::uint64_t Key = 0;
        IrradynSceneCard Card;
    };

    struct IrradynSceneCacheUpdate final
    {
        std::uint32_t Updated = 0;
        std::uint32_t Removed = 0;
    };

    /// Render-thread-owned, bounded-update cache used for coarse off-screen and non-surface GI participation.
    class IrradynSceneCache final
    {
      public:
        [[nodiscard]] IrradynSceneCacheUpdate Update(const std::span<const IrradynSceneCardCandidate> candidates,
                                                     const std::uint32_t updateBudget)
        {
            IrradynSceneCacheUpdate result;
            std::unordered_set<std::uint64_t> present;
            present.reserve(candidates.size());
            for (const auto& candidate : candidates)
                present.insert(candidate.Key);
            for (auto iterator = m_Cards.begin(); iterator != m_Cards.end();)
            {
                if (!present.contains(iterator->first))
                {
                    iterator = m_Cards.erase(iterator);
                    ++result.Removed;
                }
                else
                {
                    ++iterator;
                }
            }

            if (candidates.empty() || updateBudget == 0U)
            {
                m_UpdateCursor = 0U;
                return result;
            }

            const auto budget = std::min<std::size_t>(updateBudget, candidates.size());
            for (std::size_t offset = 0; offset < budget; ++offset)
            {
                const auto index = (m_UpdateCursor + offset) % candidates.size();
                const auto& candidate = candidates[index];
                const auto found = m_Cards.find(candidate.Key);
                if (found != m_Cards.end())
                {
                    found->second.Card = candidate.Card;
                    found->second.LastUpdate = NextUpdateSerial();
                }
                else
                {
                    if (m_Cards.size() >= MaximumIrradynSceneCacheEntries)
                    {
                        const auto eviction =
                            std::ranges::min_element(m_Cards,
                                                     [](const auto& left, const auto& right)
                                                     {
                                                         if (left.second.LastUpdate != right.second.LastUpdate)
                                                             return left.second.LastUpdate < right.second.LastUpdate;
                                                         return left.first < right.first;
                                                     });
                        m_Cards.erase(eviction);
                    }
                    m_Cards.emplace(candidate.Key, CacheEntry{candidate.Card, NextUpdateSerial()});
                }
                ++result.Updated;
            }
            m_UpdateCursor = (m_UpdateCursor + budget) % candidates.size();
            return result;
        }

        [[nodiscard]] std::vector<IrradynSceneCard> Select(const Vector3 cameraPosition,
                                                           const std::uint32_t maximumCards) const
        {
            struct RankedCard final
            {
                const IrradynSceneCard* Card = nullptr;
                std::uint64_t Key = 0;
                float DistanceSquared = 0.0F;
            };

            std::vector<RankedCard> ranked;
            ranked.reserve(m_Cards.size());
            for (const auto& [key, entry] : m_Cards)
            {
                const auto& card = entry.Card;
                const Vector3 offset{card.CenterRadius.X - cameraPosition.X, card.CenterRadius.Y - cameraPosition.Y,
                                     card.CenterRadius.Z - cameraPosition.Z};
                ranked.push_back(
                    {std::addressof(card), key, offset.X * offset.X + offset.Y * offset.Y + offset.Z * offset.Z});
            }
            std::ranges::sort(ranked,
                              [](const RankedCard& left, const RankedCard& right)
                              {
                                  if (left.DistanceSquared != right.DistanceSquared)
                                      return left.DistanceSquared < right.DistanceSquared;
                                  return left.Key < right.Key;
                              });

            std::vector<IrradynSceneCard> result;
            result.reserve(std::min<std::size_t>(maximumCards, ranked.size()));
            for (std::size_t index = 0; index < std::min<std::size_t>(maximumCards, ranked.size()); ++index)
                result.push_back(*ranked[index].Card);
            return result;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Cards.size(); }

        void Clear() noexcept
        {
            m_Cards.clear();
            m_UpdateCursor = 0U;
            m_UpdateSerial = 0U;
        }

      private:
        struct CacheEntry final
        {
            IrradynSceneCard Card;
            std::uint64_t LastUpdate = 0U;
        };

        [[nodiscard]] std::uint64_t NextUpdateSerial() noexcept
        {
            if (m_UpdateSerial == std::numeric_limits<std::uint64_t>::max())
            {
                for (auto& [key, entry] : m_Cards)
                {
                    (void)key;
                    entry.LastUpdate = 0U;
                }
                m_UpdateSerial = 0U;
            }
            return ++m_UpdateSerial;
        }

        std::unordered_map<std::uint64_t, CacheEntry> m_Cards;
        std::size_t m_UpdateCursor = 0U;
        std::uint64_t m_UpdateSerial = 0U;
    };
} // namespace Keire::RenderBackend
