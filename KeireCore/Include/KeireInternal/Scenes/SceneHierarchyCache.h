#pragma once

#include "Keire/ECS/Component.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    class SceneHierarchyCache final
    {
      public:
        // Ordered and Children return cache-backed views. Callers that can invoke user callbacks must copy them first.
        void Invalidate() noexcept { m_Valid = false; }

        void Clear() noexcept
        {
            m_Order.clear();
            m_Children.clear();
            m_Traversal.clear();
            m_Valid = false;
        }

        template <typename ParentLookup>
        [[nodiscard]] const std::vector<EntityId>& Ordered(const std::span<const EntityId> authoredOrder,
                                                           ParentLookup&& parentOf) const
        {
            Refresh(authoredOrder, std::forward<ParentLookup>(parentOf));
            return m_Order;
        }

        template <typename ParentLookup>
        [[nodiscard]] std::span<const EntityId>
        Children(const EntityId parent, const std::span<const EntityId> authoredOrder, ParentLookup&& parentOf) const
        {
            Refresh(authoredOrder, std::forward<ParentLookup>(parentOf));
            const auto found = m_Children.find(parent);
            return found == m_Children.end() ? std::span<const EntityId>{} : std::span<const EntityId>{found->second};
        }

        template <typename ParentLookup, typename Callback>
        void VisitSubtree(const EntityId root, const std::span<const EntityId> authoredOrder, ParentLookup&& parentOf,
                          Callback&& callback) const
        {
            Refresh(authoredOrder, std::forward<ParentLookup>(parentOf));
            m_Traversal.clear();
            m_Traversal.push_back(root);
            while (!m_Traversal.empty())
            {
                const auto current = m_Traversal.back();
                m_Traversal.pop_back();
                callback(current);
                const auto children = m_Children.find(current);
                if (children != m_Children.end())
                    m_Traversal.insert(m_Traversal.end(), children->second.rbegin(), children->second.rend());
            }
        }

      private:
        template <typename ParentLookup>
        void Refresh(const std::span<const EntityId> authoredOrder, ParentLookup&& parentOf) const
        {
            if (m_Valid)
                return;

            m_Order.clear();
            m_Order.reserve(authoredOrder.size());
            m_Children.clear();
            m_Traversal.clear();
            m_Traversal.reserve(authoredOrder.size());
            for (const auto id : authoredOrder)
            {
                const auto parent = parentOf(id);
                if (parent)
                    m_Children[parent].push_back(id);
                else
                    m_Traversal.push_back(id);
            }
            std::ranges::reverse(m_Traversal);
            while (!m_Traversal.empty())
            {
                const auto current = m_Traversal.back();
                m_Traversal.pop_back();
                m_Order.push_back(current);
                const auto children = m_Children.find(current);
                if (children != m_Children.end())
                    m_Traversal.insert(m_Traversal.end(), children->second.rbegin(), children->second.rend());
            }
            m_Valid = true;
        }

        mutable std::vector<EntityId> m_Order;
        mutable std::unordered_map<EntityId, std::vector<EntityId>> m_Children;
        mutable std::vector<EntityId> m_Traversal;
        mutable bool m_Valid = false;
    };
} // namespace Keire::Detail
