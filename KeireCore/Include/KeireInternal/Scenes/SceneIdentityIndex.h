#pragma once

#include "Keire/ECS/Entity.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    using SceneIdentityIndex = std::unordered_map<std::string, std::unordered_set<EntityId>>;

    inline void EraseIndexedIdentity(SceneIdentityIndex& index, const std::string& value, const EntityId id)
    {
        const auto found = index.find(value);
        if (found == index.end())
            return;
        found->second.erase(id);
        if (found->second.empty())
            index.erase(found);
    }

    inline void ReplaceIndexedName(SceneIdentityIndex& index, const EntityId id, std::string& current,
                                   std::string replacement)
    {
        auto bucket = index.try_emplace(replacement).first;
        try
        {
            bucket->second.insert(id);
        }
        catch (...)
        {
            if (bucket->second.empty())
                index.erase(bucket);
            throw;
        }
        current.swap(replacement);
        EraseIndexedIdentity(index, replacement, id);
    }

    inline void ReplaceIndexedTags(SceneIdentityIndex& index, const EntityId id, std::vector<std::string>& current,
                                   std::vector<std::string> replacement)
    {
        std::vector<std::size_t> inserted;
        inserted.reserve(replacement.size());
        try
        {
            for (std::size_t position = 0; position < replacement.size(); ++position)
            {
                const auto& tag = replacement[position];
                if (std::ranges::find(current, tag) != current.end())
                    continue;
                auto bucket = index.try_emplace(tag).first;
                try
                {
                    if (bucket->second.insert(id).second)
                        inserted.push_back(position);
                }
                catch (...)
                {
                    if (bucket->second.empty())
                        index.erase(bucket);
                    throw;
                }
            }
        }
        catch (...)
        {
            for (const auto position : inserted)
                EraseIndexedIdentity(index, replacement[position], id);
            throw;
        }
        current.swap(replacement);
        for (const auto& oldTag : replacement)
            if (std::ranges::find(current, oldTag) == current.end())
                EraseIndexedIdentity(index, oldTag, id);
    }

    inline void AddIndexedTag(SceneIdentityIndex& index, const EntityId id, std::vector<std::string>& tags,
                              std::string tag)
    {
        auto bucket = index.try_emplace(tag).first;
        bool inserted = false;
        try
        {
            inserted = bucket->second.insert(id).second;
            tags.push_back(std::move(tag));
        }
        catch (...)
        {
            if (inserted)
                bucket->second.erase(id);
            if (bucket->second.empty())
                index.erase(bucket);
            throw;
        }
    }

    inline bool RemoveIndexedTag(SceneIdentityIndex& index, const EntityId id, std::vector<std::string>& tags,
                                 const std::string_view tag)
    {
        const auto found = std::ranges::find(tags, tag);
        if (found == tags.end())
            return false;
        EraseIndexedIdentity(index, *found, id);
        tags.erase(found);
        return true;
    }
} // namespace Keire::Detail
