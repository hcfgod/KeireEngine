#pragma once

#include "Keire/Ref.h"
#include "KeireInternal/Spatial/FrameAttachment.h"
#include "KeireInternal/Spatial/FrameSnapshot.h"
#include "KeireInternal/Spatial/SectorPosition.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    class FrameRegistryLifetime final : public RefCounted
    {
      public:
        FrameRegistryLifetime() = default;
    };

    class LiveFrame final
    {
      public:
        LiveFrame() = default;

      private:
        friend class FrameRegistry;
        WeakRef<FrameRegistryLifetime> m_Owner;
        std::uint64_t m_Key = 0;
        std::uint64_t m_Serial = 0;
    };

    class BoundFrameSnapshot final
    {
      public:
        [[nodiscard]] const FrameSnapshot& Evaluation() const noexcept { return m_Evaluation; }

      private:
        friend class FrameRegistry;
        BoundFrameSnapshot(LiveFrame frame, FrameSnapshot evaluation, const std::uint64_t epoch)
            : m_Frame(std::move(frame)), m_Evaluation(std::move(evaluation)), m_Epoch(epoch)
        {
        }

        LiveFrame m_Frame;
        FrameSnapshot m_Evaluation;
        std::uint64_t m_Epoch;
    };

    // Owner-serialized topology. Handles borrow a lifetime token, never the registry or a node address.
    // Logical keys can be restored; runtime serials and the lifetime token must never be persisted.
    class FrameRegistry final
    {
      public:
        static constexpr std::size_t DefaultMaximumDepth = 256;

        explicit FrameRegistry(const std::uint64_t world, const std::uint64_t rootKey = 1,
                               const std::size_t maximumDepth = DefaultMaximumDepth)
            : m_World(world), m_Root(rootKey), m_MaximumDepth(maximumDepth),
              m_Lifetime(CreateRef<FrameRegistryLifetime>())
        {
            if (world == 0 || rootKey == 0)
            {
                throw std::invalid_argument("Frame registry requires nonzero logical world and root keys.");
            }
            m_Nodes.emplace(rootKey, Node{0, 1});
        }

        ~FrameRegistry() { Close(); }

        FrameRegistry(const FrameRegistry&) = delete;
        FrameRegistry& operator=(const FrameRegistry&) = delete;
        FrameRegistry(FrameRegistry&&) = delete;
        FrameRegistry& operator=(FrameRegistry&&) = delete;

        [[nodiscard]] LiveFrame Find(const std::uint64_t key) const
        {
            RequireOpen();
            const auto found = m_Nodes.find(key);
            if (found == m_Nodes.end())
            {
                throw std::invalid_argument("Unknown logical frame key.");
            }
            LiveFrame result;
            result.m_Owner = m_Lifetime;
            result.m_Key = key;
            result.m_Serial = found->second.Serial;
            return result;
        }

        [[nodiscard]] bool IsLive(const LiveFrame& frame) const noexcept
        {
            const auto owner = frame.m_Owner.Lock();
            if (!m_Lifetime || owner.Get() != m_Lifetime.Get())
            {
                return false;
            }
            const auto found = m_Nodes.find(frame.m_Key);
            return found != m_Nodes.end() && found->second.Serial == frame.m_Serial;
        }

        [[nodiscard]] SpatialIdentity Identity(const LiveFrame& frame) const
        {
            RequireLive(frame);
            return {m_World, frame.m_Key};
        }

        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }

        [[nodiscard]] FrameAttachment Attach(const LiveFrame& frame) const
        {
            RequireLive(frame);
            return FrameAttachment(m_Nodes.at(frame.m_Key).Usage);
        }

        [[nodiscard]] std::uint64_t AttachmentCount(const LiveFrame& frame) const
        {
            RequireLive(frame);
            return m_Nodes.at(frame.m_Key).Usage->Attachments.load(std::memory_order_acquire);
        }

        [[nodiscard]] std::optional<LiveFrame> Parent(const LiveFrame& frame) const
        {
            RequireLive(frame);
            const auto parent = m_Nodes.at(frame.m_Key).Parent;
            return parent == 0 ? std::nullopt : std::optional<LiveFrame>{Find(parent)};
        }

        [[nodiscard]] std::vector<BoundFrameSnapshot> Publish(const std::span<const FrameSnapshot> evaluations,
                                                              const double time)
        {
            RequireOpen();
            if (!std::isfinite(time) || evaluations.size() != m_Nodes.size() - 1)
            {
                throw std::invalid_argument("Frame publication requires one evaluation per nonroot frame.");
            }
            RequireIncrement(m_Epoch);
            const auto nextEpoch = m_Epoch + 1;
            std::set<std::uint64_t> keys;
            std::vector<BoundFrameSnapshot> prepared;
            prepared.reserve(evaluations.size());
            for (const auto& evaluation : evaluations)
            {
                if (evaluation.Time() != time || !keys.insert(evaluation.ChildIdentity().Frame).second)
                {
                    throw std::invalid_argument("Frame publication contains mixed times or duplicate frames.");
                }
                auto bound = Bind(Find(evaluation.ChildIdentity().Frame), evaluation);
                bound.m_Epoch = nextEpoch;
                prepared.push_back(std::move(bound));
            }
            // All validation/allocation precedes this owner-side commit, including replacement at the same time.
            m_Epoch = nextEpoch;
            return prepared;
        }

        // Call on the serialized owner before accepting worker results; numerical snapshots alone confer no authority.
        [[nodiscard]] bool IsCurrent(const BoundFrameSnapshot& snapshot, const double time) const noexcept
        {
            return IsLive(snapshot.m_Frame) && snapshot.m_Epoch == m_Epoch &&
                   snapshot.m_Evaluation.Revision() == m_Revision && snapshot.m_Evaluation.Time() == time;
        }

        [[nodiscard]] LiveFrame Create(const std::uint64_t key, const LiveFrame& parent)
        {
            RequireLive(parent);
            if (key == 0 || m_Nodes.contains(key))
            {
                throw std::invalid_argument("Frame key must be nonzero and unique in its world.");
            }
            if (Depth(parent.m_Key) >= m_MaximumDepth)
            {
                throw std::out_of_range("Frame creation exceeds the configured hierarchy depth.");
            }
            RequireIncrement(m_Revision);
            RequireIncrement(m_LastSerial);
            m_Nodes.emplace(key, Node{parent.m_Key, m_LastSerial + 1});
            ++m_LastSerial;
            ++m_Revision;
            return Find(key);
        }

        void Reparent(const LiveFrame& frame, const LiveFrame& parent)
        {
            RequireLive(frame);
            RequireLive(parent);
            if (frame.m_Key == m_Root)
            {
                throw std::invalid_argument("The spatial root cannot be reparented.");
            }
            for (auto ancestor = parent.m_Key; ancestor != 0; ancestor = m_Nodes.at(ancestor).Parent)
            {
                if (ancestor == frame.m_Key)
                {
                    throw std::invalid_argument("Frame parenting would create a cycle.");
                }
            }
            auto& node = m_Nodes.at(frame.m_Key);
            if (node.Parent == parent.m_Key)
            {
                return;
            }
            // Preflight every affected descendant using the proposed edge, without publishing it.
            for (const auto& [key, entry] : m_Nodes)
            {
                (void)Depth(key, frame.m_Key, parent.m_Key);
            }
            RequireIncrement(m_Revision);
            node.Parent = parent.m_Key;
            ++m_Revision;
        }

        void Remove(const LiveFrame& frame)
        {
            RequireLive(frame);
            if (m_Nodes.at(frame.m_Key).Usage->Attachments.load(std::memory_order_acquire) != 0)
            {
                throw std::logic_error("Release or transfer frame attachments before removal.");
            }
            if (frame.m_Key == m_Root)
            {
                throw std::invalid_argument("The spatial root cannot be removed.");
            }
            for (const auto& [key, node] : m_Nodes)
            {
                if (node.Parent == frame.m_Key)
                {
                    throw std::invalid_argument("Transfer or remove child frames before removing their parent.");
                }
            }
            RequireIncrement(m_Revision);
            m_Nodes.erase(frame.m_Key);
            ++m_Revision;
        }

        void Close() noexcept
        {
            m_Nodes.clear();
            m_Lifetime = {};
        }

      private:
        [[nodiscard]] BoundFrameSnapshot Bind(const LiveFrame& frame, FrameSnapshot evaluation) const
        {
            RequireLive(frame);
            const auto parent = m_Nodes.at(frame.m_Key).Parent;
            if (parent == 0 || evaluation.ChildIdentity() != Identity(frame) ||
                evaluation.ParentIdentity() != SpatialIdentity{m_World, parent} || evaluation.Revision() != m_Revision)
            {
                throw std::invalid_argument("Frame evaluation does not match the current topology.");
            }
            return BoundFrameSnapshot(frame, std::move(evaluation), m_Epoch);
        }

        [[nodiscard]] std::size_t Depth(std::uint64_t key, const std::uint64_t moved = 0,
                                        const std::uint64_t proposedParent = 0) const
        {
            std::size_t depth = 0;
            while (key != m_Root)
            {
                if (depth == m_MaximumDepth)
                {
                    throw std::out_of_range("Frame topology exceeds the configured hierarchy depth.");
                }
                ++depth;
                key = key == moved ? proposedParent : m_Nodes.at(key).Parent;
            }
            return depth;
        }

        struct Node
        {
            std::uint64_t Parent;
            std::uint64_t Serial;
            Ref<FrameUsage> Usage = CreateRef<FrameUsage>();
        };

        void RequireOpen() const
        {
            if (!m_Lifetime)
            {
                throw std::logic_error("Spatial frame registry is closed.");
            }
        }

        void RequireLive(const LiveFrame& frame) const
        {
            RequireOpen();
            if (!IsLive(frame))
            {
                throw std::invalid_argument("Frame handle is stale or belongs to another runtime world.");
            }
        }

        static void RequireIncrement(const std::uint64_t value)
        {
            if (value == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error("Spatial frame counter exhausted.");
            }
        }

        std::uint64_t m_World;
        std::uint64_t m_Root;
        std::size_t m_MaximumDepth;
        Ref<FrameRegistryLifetime> m_Lifetime;
        std::map<std::uint64_t, Node> m_Nodes;
        std::uint64_t m_LastSerial = 1;
        std::uint64_t m_Revision = 1;
        std::uint64_t m_Epoch = 1;
    };
} // namespace Keire::Detail
