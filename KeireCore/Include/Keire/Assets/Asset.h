#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class KEIRE_API AssetId final
    {
      public:
        constexpr AssetId() noexcept = default;
        constexpr AssetId(const std::uint64_t high, const std::uint64_t low) noexcept : m_High(high), m_Low(low) {}

        [[nodiscard]] static AssetId Generate();
        [[nodiscard]] static AssetId Parse(std::string_view value);
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] constexpr std::uint64_t High() const noexcept { return m_High; }
        [[nodiscard]] constexpr std::uint64_t Low() const noexcept { return m_Low; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_High != 0 || m_Low != 0; }
        [[nodiscard]] auto operator<=>(const AssetId&) const noexcept = default;

      private:
        std::uint64_t m_High = 0;
        std::uint64_t m_Low = 0;
    };

    class KEIRE_API AssetTypeId final
    {
      public:
        constexpr AssetTypeId() noexcept = default;
        explicit constexpr AssetTypeId(const AssetId value) noexcept : m_Value(value) {}

        [[nodiscard]] static AssetTypeId Parse(std::string_view value) { return AssetTypeId(AssetId::Parse(value)); }
        [[nodiscard]] std::string ToString() const { return m_Value.ToString(); }
        [[nodiscard]] constexpr const AssetId& Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const AssetTypeId&) const noexcept = default;

      private:
        AssetId m_Value;
    };

    enum class AssetState : std::uint8_t
    {
        Queued,
        Loading,
        Ready,
        Reloading,
        Failed,
        Cancelled
    };

    enum class AssetPriority : std::uint8_t
    {
        Critical,
        High,
        Normal,
        Low,
        Background
    };

    struct AssetDiagnostic
    {
        std::string Operation;
        std::string Message;
    };

    class KEIRE_API AssetLoadError final : public std::runtime_error
    {
      public:
        AssetLoadError(AssetId id, AssetDiagnostic diagnostic);

        [[nodiscard]] AssetId Id() const noexcept { return m_Id; }
        [[nodiscard]] const AssetDiagnostic& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        AssetId m_Id;
        AssetDiagnostic m_Diagnostic;
    };

    class KEIRE_API Asset : public RefCounted
    {
      public:
        ~Asset() override;

        [[nodiscard]] virtual AssetTypeId Type() const noexcept = 0;
        [[nodiscard]] virtual std::size_t ResidentBytes() const noexcept = 0;

      protected:
        Asset() noexcept = default;
    };

    class KEIRE_API BinaryAsset final : public Asset
    {
      public:
        explicit BinaryAsset(std::vector<std::byte> bytes = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b4549524542494eULL, 0x4152590000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return m_Bytes.size(); }
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return m_Bytes; }

      private:
        std::vector<std::byte> m_Bytes;
    };

    class KEIRE_API TextAsset final : public Asset
    {
      public:
        explicit TextAsset(std::string text = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245544558ULL, 0x5441535345540001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return m_Text.size(); }
        [[nodiscard]] std::string_view Text() const noexcept { return m_Text; }

      private:
        std::string m_Text;
    };

    namespace Detail
    {
        class KEIRE_API AssetHandleState final : public RefCounted
        {
          public:
            class Impl;

            AssetHandleState(AssetId id, AssetTypeId type, Ref<Asset> fallback, std::uint64_t ownerThreadHash);
            ~AssetHandleState() override;

            [[nodiscard]] AssetId Id() const noexcept;
            [[nodiscard]] AssetTypeId Type() const noexcept;
            [[nodiscard]] AssetState State() const noexcept;
            [[nodiscard]] bool UsingFallback() const noexcept;
            [[nodiscard]] std::uint64_t Revision() const noexcept;
            [[nodiscard]] AssetDiagnostic Diagnostic() const;
            [[nodiscard]] Ref<Asset> Current() const;
            void RequireTerminal() const;

            void SetLoading(bool reload);
            void Commit(Ref<Asset> asset);
            void Fail(AssetDiagnostic diagnostic, bool reload);
            void Cancel();

          private:
            std::unique_ptr<Impl> m_Impl;
        };
    } // namespace Detail

    template <typename T> class AssetHandle final
    {
      public:
        AssetHandle() noexcept = default;

        [[nodiscard]] AssetId Id() const noexcept { return m_State ? m_State->Id() : AssetId{}; }
        [[nodiscard]] AssetState State() const noexcept { return m_State ? m_State->State() : AssetState::Cancelled; }
        [[nodiscard]] bool UsingFallback() const noexcept { return !m_State || m_State->UsingFallback(); }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_State ? m_State->Revision() : 0; }
        [[nodiscard]] AssetDiagnostic Diagnostic() const { return m_State ? m_State->Diagnostic() : AssetDiagnostic{}; }
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_State); }

        [[nodiscard]] Ref<const T> Get() const noexcept
        {
            if (!m_State)
            {
                return {};
            }
            return DynamicRefCast<const T>(m_State->Current());
        }

        [[nodiscard]] Ref<const T> TryGetLoaded() const noexcept
        {
            if (!m_State || (m_State->State() != AssetState::Ready && m_State->State() != AssetState::Reloading))
            {
                return {};
            }
            return Get();
        }

        [[nodiscard]] Ref<const T> Require() const
        {
            if (!m_State)
            {
                throw std::logic_error("Cannot require an empty asset handle.");
            }
            m_State->RequireTerminal();
            if (m_State->State() == AssetState::Failed || m_State->State() == AssetState::Cancelled)
            {
                throw AssetLoadError(m_State->Id(), m_State->Diagnostic());
            }
            return Get();
        }

      private:
        friend class AssetSystem;
        explicit AssetHandle(Ref<Detail::AssetHandleState> state) noexcept : m_State(std::move(state)) {}

        Ref<Detail::AssetHandleState> m_State;
    };
} // namespace Keire

template <> struct std::hash<Keire::AssetId>
{
    std::size_t operator()(const Keire::AssetId value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.High()) ^ (std::hash<std::uint64_t>{}(value.Low()) << 1U);
    }
};

template <> struct std::hash<Keire::AssetTypeId>
{
    std::size_t operator()(const Keire::AssetTypeId value) const noexcept
    {
        return std::hash<Keire::AssetId>{}(value.Value());
    }
};
