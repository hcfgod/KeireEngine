#pragma once

#include "Keire/StableHandle.h"

#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Internal
{
    template <typename Tag, typename T> class StableHandleTable final
    {
      public:
        using Handle = StableHandle<Tag>;

        template <typename... Args> [[nodiscard]] Handle Emplace(Args&&... args)
        {
            std::scoped_lock lock(m_Mutex);
            std::uint32_t index = 0;
            if (m_Free.empty())
            {
                if (m_Slots.size() >= std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("The stable handle table is full.");
                index = static_cast<std::uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }
            else
            {
                index = m_Free.back();
                m_Free.pop_back();
            }
            auto& slot = m_Slots[index];
            slot.Value.emplace(std::forward<Args>(args)...);
            return Handle::FromParts(index, slot.Generation);
        }

        [[nodiscard]] bool Erase(const Handle handle)
        {
            std::scoped_lock lock(m_Mutex);
            if (!Matches(handle))
                return false;
            auto& slot = m_Slots[handle.Index()];
            slot.Value.reset();
            ++slot.Generation;
            if (slot.Generation == 0)
                ++slot.Generation;
            m_Free.push_back(handle.Index());
            return true;
        }

        template <typename Function> [[nodiscard]] bool With(const Handle handle, Function&& function)
        {
            std::scoped_lock lock(m_Mutex);
            if (!Matches(handle))
                return false;
            std::forward<Function>(function)(*m_Slots[handle.Index()].Value);
            return true;
        }

        [[nodiscard]] bool Contains(const Handle handle) const
        {
            std::scoped_lock lock(m_Mutex);
            return Matches(handle);
        }

      private:
        struct Slot final
        {
            std::optional<T> Value;
            std::uint32_t Generation = 1;
        };

        [[nodiscard]] bool Matches(const Handle handle) const noexcept
        {
            return handle.IsValid() && handle.Index() < m_Slots.size() &&
                   m_Slots[handle.Index()].Generation == handle.Generation() &&
                   m_Slots[handle.Index()].Value.has_value();
        }

        mutable std::mutex m_Mutex;
        std::vector<Slot> m_Slots;
        std::vector<std::uint32_t> m_Free;
    };
} // namespace Keire::Internal
