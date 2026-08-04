#include "Keire/StringInterner.h"

#include <deque>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Keire
{
    class StringInterner::Impl final
    {
      public:
        Impl()
        {
            Values.emplace_back();
            Index.emplace(std::string_view(Values.front()), 0);
        }

        mutable std::shared_mutex Mutex;
        std::deque<std::string> Values;
        std::unordered_map<std::string_view, std::uint32_t> Index;
    };

    StringInterner::StringInterner() : m_Impl(std::make_unique<Impl>()) {}

    StringInterner::~StringInterner() = default;

    InternedString StringInterner::Intern(const std::string_view value)
    {
        if (value.empty())
            return {};
        {
            std::shared_lock lock(m_Impl->Mutex);
            const auto found = m_Impl->Index.find(value);
            if (found != m_Impl->Index.end())
                return InternedString(found->second);
        }
        std::unique_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Index.find(value);
        if (found != m_Impl->Index.end())
            return InternedString(found->second);
        if (m_Impl->Values.size() >= std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("The string interner is full.");
        m_Impl->Values.emplace_back(value);
        const auto id = static_cast<std::uint32_t>(m_Impl->Values.size() - 1);
        m_Impl->Index.emplace(std::string_view(m_Impl->Values.back()), id);
        return InternedString(id);
    }

    InternedString StringInterner::Find(const std::string_view value) const noexcept
    {
        try
        {
            std::shared_lock lock(m_Impl->Mutex);
            const auto found = m_Impl->Index.find(value);
            return found == m_Impl->Index.end() || found->second == 0 ? InternedString{}
                                                                      : InternedString(found->second);
        }
        catch (...)
        {
            return {};
        }
    }

    std::string_view StringInterner::Resolve(const InternedString value) const noexcept
    {
        try
        {
            std::shared_lock lock(m_Impl->Mutex);
            return value.Value() < m_Impl->Values.size() ? std::string_view(m_Impl->Values[value.Value()])
                                                         : std::string_view{};
        }
        catch (...)
        {
            return {};
        }
    }

    std::size_t StringInterner::Size() const noexcept
    {
        try
        {
            std::shared_lock lock(m_Impl->Mutex);
            return m_Impl->Values.size() - 1;
        }
        catch (...)
        {
            return 0;
        }
    }
} // namespace Keire
