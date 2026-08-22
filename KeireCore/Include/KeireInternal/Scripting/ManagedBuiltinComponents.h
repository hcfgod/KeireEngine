#pragma once

#include "Keire/ECS/Entity.h"

#include <cstdint>
#include <string_view>

namespace Coral
{
    class String;
} // namespace Coral

namespace Keire::Detail
{
    using ManagedRuntimeEntityResolver = Entity (*)(std::uint64_t, std::uint64_t, std::uint64_t);

    class ManagedBuiltinComponentResolverScope final
    {
      public:
        explicit ManagedBuiltinComponentResolverScope(ManagedRuntimeEntityResolver resolver) noexcept;
        ~ManagedBuiltinComponentResolverScope();

        ManagedBuiltinComponentResolverScope(const ManagedBuiltinComponentResolverScope&) = delete;
        ManagedBuiltinComponentResolverScope& operator=(const ManagedBuiltinComponentResolverScope&) = delete;

      private:
        ManagedRuntimeEntityResolver m_Previous = nullptr;
    };

    enum class NativeBuiltinPropertyKind : std::uint32_t
    {
        Boolean,
        Integer,
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Asset,
        Entity
    };

    struct NativeBuiltinProperty
    {
        NativeBuiltinPropertyKind Kind = NativeBuiltinPropertyKind::Boolean;
        std::int64_t Integer = 0;
        double Scalar = 0.0;
        Vector4 Vector;
        std::uint64_t High = 0;
        std::uint64_t Low = 0;
    };

    static_assert(sizeof(NativeBuiltinProperty) == 56);

    [[nodiscard]] bool GetManagedBuiltinComponentProperty(const Entity& entity, ComponentTypeId type,
                                                          std::string_view key,
                                                          NativeBuiltinProperty& destination) noexcept;
    [[nodiscard]] bool SetManagedBuiltinComponentProperty(const Entity& entity, ComponentTypeId type,
                                                          std::string_view key,
                                                          const NativeBuiltinProperty& source) noexcept;
    [[nodiscard]] std::uint8_t GetManagedBuiltinComponentPropertyIcall(std::uint64_t world, std::uint64_t entityHigh,
                                                                       std::uint64_t entityLow, std::uint64_t typeHigh,
                                                                       std::uint64_t typeLow, Coral::String key,
                                                                       NativeBuiltinProperty* destination) noexcept;
    [[nodiscard]] std::uint8_t SetManagedBuiltinComponentPropertyIcall(std::uint64_t world, std::uint64_t entityHigh,
                                                                       std::uint64_t entityLow, std::uint64_t typeHigh,
                                                                       std::uint64_t typeLow, Coral::String key,
                                                                       const NativeBuiltinProperty* source) noexcept;
    [[nodiscard]] std::int32_t GetManagedBuiltinComponentTextIcall(std::uint64_t world, std::uint64_t entityHigh,
                                                                   std::uint64_t entityLow, std::uint64_t typeHigh,
                                                                   std::uint64_t typeLow, Coral::String key,
                                                                   char* destination, std::int32_t capacity) noexcept;
    [[nodiscard]] std::uint8_t SetManagedBuiltinComponentTextIcall(std::uint64_t world, std::uint64_t entityHigh,
                                                                   std::uint64_t entityLow, std::uint64_t typeHigh,
                                                                   std::uint64_t typeLow, Coral::String key,
                                                                   Coral::String value) noexcept;
} // namespace Keire::Detail
