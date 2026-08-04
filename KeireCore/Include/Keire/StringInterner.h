#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace Keire
{
    class InternedString final
    {
      public:
        constexpr InternedString() noexcept = default;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] constexpr bool operator==(const InternedString&) const noexcept = default;

      private:
        friend class StringInterner;

        explicit constexpr InternedString(const std::uint32_t value) noexcept : m_Value(value) {}
        std::uint32_t m_Value = 0;
    };

    class KEIRE_API StringInterner final : public RefCounted
    {
      public:
        StringInterner();
        ~StringInterner() override;

        StringInterner(const StringInterner&) = delete;
        StringInterner& operator=(const StringInterner&) = delete;

        [[nodiscard]] InternedString Intern(std::string_view value);
        [[nodiscard]] InternedString Find(std::string_view value) const noexcept;
        [[nodiscard]] std::string_view Resolve(InternedString value) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
