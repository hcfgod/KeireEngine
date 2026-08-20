#include "KeireInternal/Scripting/ManagedRuntimeUi.h"

#include "Keire/Ui/RuntimeUi.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstring>
#include <limits>

namespace Keire::Detail
{
    namespace
    {
        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

        [[nodiscard]] int CopyText(const std::string_view text, std::uint8_t* destination, const int capacity) noexcept
        {
            if (capacity < 0 || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return -1;
            const auto size = static_cast<int>(text.size());
            if (!destination || capacity == 0)
                return size;
            if (capacity < size)
                return -1;
            if (size > 0)
                std::memcpy(destination, text.data(), text.size());
            return size;
        }

        [[nodiscard]] std::uint8_t GetScalar(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t property, float* value) noexcept
        {
            if (!ActiveServices || !value || property > static_cast<std::uint8_t>(ManagedUiScalarProperty::Value))
                return 0;
            const auto result =
                ActiveServices->ReadManagedUiScalar(AssetId(high, low), static_cast<ManagedUiScalarProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetScalar(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t property, const float value) noexcept
        {
            return ActiveServices && property <= static_cast<std::uint8_t>(ManagedUiScalarProperty::Value) &&
                           ActiveServices->SetManagedUiScalar(AssetId(high, low),
                                                              static_cast<ManagedUiScalarProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetFlag(const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t property, std::uint8_t* value) noexcept
        {
            if (!ActiveServices || !value || property > static_cast<std::uint8_t>(ManagedUiFlagProperty::Focused))
                return 0;
            const auto result =
                ActiveServices->ReadManagedUiFlag(AssetId(high, low), static_cast<ManagedUiFlagProperty>(property));
            if (!result)
                return 0;
            *value = *result ? 1 : 0;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetFlag(const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t property, const std::uint8_t value) noexcept
        {
            return ActiveServices && property <= static_cast<std::uint8_t>(ManagedUiFlagProperty::Focused) &&
                           ActiveServices->SetManagedUiFlag(AssetId(high, low),
                                                            static_cast<ManagedUiFlagProperty>(property), value != 0)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetVector(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t property, Vector2* value) noexcept
        {
            if (!ActiveServices || !value || property > static_cast<std::uint8_t>(ManagedUiVectorProperty::ContentSize))
                return 0;
            const auto result =
                ActiveServices->ReadManagedUiVector(AssetId(high, low), static_cast<ManagedUiVectorProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetVector(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t property, const Vector2 value) noexcept
        {
            return ActiveServices && property <= static_cast<std::uint8_t>(ManagedUiVectorProperty::ContentSize) &&
                           ActiveServices->SetManagedUiVector(AssetId(high, low),
                                                              static_cast<ManagedUiVectorProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] int GetInputText(const std::uint64_t high, const std::uint64_t low, std::uint8_t* destination,
                                       const int capacity) noexcept
        {
            if (!ActiveServices)
                return CopyText({}, destination, capacity);
            const auto value = ActiveServices->ReadManagedUiInputText(AssetId(high, low));
            return value ? CopyText(*value, destination, capacity) : -1;
        }

        [[nodiscard]] std::uint8_t SetInputText(const std::uint64_t high, const std::uint64_t low,
                                                const Coral::String text) noexcept
        {
            return ActiveServices &&
                           ActiveServices->SetManagedUiInputText(AssetId(high, low), static_cast<std::string>(text))
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t ConsumeEvent(const std::uint64_t high, const std::uint64_t low,
                                                const std::uint8_t type) noexcept
        {
            return ActiveServices && type <= static_cast<std::uint8_t>(RuntimeUiEventType::TextChanged) &&
                           ActiveServices->ConsumeManagedUiEvent(AssetId(high, low),
                                                                 static_cast<RuntimeUiEventType>(type))
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t Focus(const std::uint64_t high, const std::uint64_t low) noexcept
        {
            return ActiveServices && ActiveServices->FocusManagedUi(AssetId(high, low)) ? 1 : 0;
        }
    } // namespace

    ManagedRuntimeUiScope::ManagedRuntimeUiScope(IScriptRuntimeServices* services) noexcept : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeUiScope::~ManagedRuntimeUiScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeUi(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetScalarIcall", reinterpret_cast<void*>(&GetScalar));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetScalarIcall", reinterpret_cast<void*>(&SetScalar));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetFlagIcall", reinterpret_cast<void*>(&GetFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetFlagIcall", reinterpret_cast<void*>(&SetFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetVectorIcall", reinterpret_cast<void*>(&GetVector));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetVectorIcall", reinterpret_cast<void*>(&SetVector));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetInputTextIcall", reinterpret_cast<void*>(&GetInputText));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetInputTextIcall", reinterpret_cast<void*>(&SetInputText));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "ConsumeEventIcall", reinterpret_cast<void*>(&ConsumeEvent));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "FocusIcall", reinterpret_cast<void*>(&Focus));
    }
} // namespace Keire::Detail
