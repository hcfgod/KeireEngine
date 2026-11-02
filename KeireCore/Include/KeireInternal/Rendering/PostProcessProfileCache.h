#pragma once

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    /// Keep recently visible assets across alternating cameras and brief volume-boundary crossings.
    [[nodiscard]] inline bool PostProcessProfileCacheExpired(const std::uint64_t lastUsedFrame,
                                                             const std::uint64_t currentFrame) noexcept
    {
        constexpr std::uint64_t retentionFrames = 600;
        return currentFrame >= lastUsedFrame && currentFrame - lastUsedFrame > retentionFrames;
    }

    // Run before participation early-outs. GPU owners retire resources before their cache entries disappear.
    template <typename Cache, typename Release>
    void PrunePostProcessCache(Cache& cache, const std::uint64_t currentFrame, Release&& release)
    {
        for (auto iterator = cache.begin(); iterator != cache.end();)
        {
            if (PostProcessProfileCacheExpired(iterator->second.LastUsedFrame, currentFrame))
            {
                release(iterator->second);
                iterator = cache.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    /// A failed load must not leave an empty entry that suppresses future retries.
    template <typename Cache, typename Key, typename Loader>
    [[nodiscard]] auto& GetOrLoadPostProcessAsset(Cache& cache, const Key& key, Loader&& load)
    {
        const auto existing = cache.find(key);
        if (existing != cache.end())
        {
            return existing->second;
        }
        return cache.emplace(key, load()).first->second;
    }

    /// Malformed typed references are optional effects; allocation and service failures still propagate.
    template <typename Cache, typename Key, typename Loader>
    [[nodiscard]] typename Cache::mapped_type* TryGetOrLoadPostProcessAsset(Cache& cache, const Key& key, Loader&& load)
    {
        try
        {
            return &GetOrLoadPostProcessAsset(cache, key, std::forward<Loader>(load));
        }
        catch (const std::invalid_argument&)
        {
            return nullptr;
        }
    }
} // namespace Keire::Detail
