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

        [[nodiscard]] std::uint8_t ResolveDocumentRoot(const std::uint64_t high, const std::uint64_t low,
                                                       ManagedUiDocumentElement* value) noexcept
        {
            if (!ActiveServices || !value)
                return 0;
            const auto result = ActiveServices->ManagedUiDocumentRoot(AssetId(high, low));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t ResolveDocumentElementById(const std::uint64_t high, const std::uint64_t low,
                                                              const std::uint64_t stableHigh,
                                                              const std::uint64_t stableLow,
                                                              ManagedUiDocumentElement* value) noexcept
        {
            if (!ActiveServices || !value)
                return 0;
            const auto result =
                ActiveServices->FindManagedUiDocumentElement(AssetId(high, low), AssetId(stableHigh, stableLow));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t ResolveDocumentElementByName(const std::uint64_t high, const std::uint64_t low,
                                                                const Coral::String name,
                                                                ManagedUiDocumentElement* value) noexcept
        {
            try
            {
                if (!ActiveServices || !value)
                    return 0;
                const auto result =
                    ActiveServices->FindManagedUiDocumentElement(AssetId(high, low), static_cast<std::string>(name));
                if (!result)
                    return 0;
                *value = *result;
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::uint8_t DocumentElementAlive(const std::uint64_t high, const std::uint64_t low,
                                                        const std::uint64_t generation,
                                                        const std::uint64_t element) noexcept
        {
            return ActiveServices &&
                           ActiveServices->ManagedUiDocumentElementAlive(AssetId(high, low), generation, element)
                       ? 1
                       : 0;
        }

        [[nodiscard]] int GetDocumentElementText(const std::uint64_t high, const std::uint64_t low,
                                                 const std::uint64_t generation, const std::uint64_t element,
                                                 std::uint8_t* destination, const int capacity) noexcept
        {
            if (!ActiveServices)
                return -1;
            const auto value =
                ActiveServices->ReadManagedUiDocumentElementText(AssetId(high, low), generation, element);
            return value ? CopyText(*value, destination, capacity) : -1;
        }

        [[nodiscard]] std::uint8_t SetDocumentElementText(const std::uint64_t high, const std::uint64_t low,
                                                          const std::uint64_t generation, const std::uint64_t element,
                                                          const Coral::String text) noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->SetManagedUiDocumentElementText(
                                             AssetId(high, low), generation, element, static_cast<std::string>(text))
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::uint8_t GetDocumentElementValue(const std::uint64_t high, const std::uint64_t low,
                                                           const std::uint64_t generation, const std::uint64_t element,
                                                           float* value) noexcept
        {
            if (!ActiveServices || !value)
                return 0;
            const auto result =
                ActiveServices->ReadManagedUiDocumentElementValue(AssetId(high, low), generation, element);
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetDocumentElementValue(const std::uint64_t high, const std::uint64_t low,
                                                           const std::uint64_t generation, const std::uint64_t element,
                                                           const float value) noexcept
        {
            return ActiveServices && ActiveServices->SetManagedUiDocumentElementValue(AssetId(high, low), generation,
                                                                                      element, value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetDocumentElementFlag(const std::uint64_t high, const std::uint64_t low,
                                                          const std::uint64_t generation, const std::uint64_t element,
                                                          const std::uint8_t property, std::uint8_t* value) noexcept
        {
            if (!ActiveServices || !value || property > static_cast<std::uint8_t>(ManagedUiDocumentFlag::Enabled))
                return 0;
            const auto result = ActiveServices->ReadManagedUiDocumentElementFlag(
                AssetId(high, low), generation, element, static_cast<ManagedUiDocumentFlag>(property));
            if (!result)
                return 0;
            *value = *result ? 1 : 0;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetDocumentElementFlag(const std::uint64_t high, const std::uint64_t low,
                                                          const std::uint64_t generation, const std::uint64_t element,
                                                          const std::uint8_t property,
                                                          const std::uint8_t value) noexcept
        {
            return ActiveServices && property <= static_cast<std::uint8_t>(ManagedUiDocumentFlag::Enabled) &&
                           ActiveServices->SetManagedUiDocumentElementFlag(AssetId(high, low), generation, element,
                                                                           static_cast<ManagedUiDocumentFlag>(property),
                                                                           value != 0)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t ConsumeDocumentElementEvent(const std::uint64_t high, const std::uint64_t low,
                                                               const std::uint64_t generation,
                                                               const std::uint64_t element,
                                                               const std::uint8_t type) noexcept
        {
            return ActiveServices && type <= static_cast<std::uint8_t>(RuntimeUiEventType::TextChanged) &&
                           ActiveServices->ConsumeManagedUiDocumentElementEvent(AssetId(high, low), generation, element,
                                                                                static_cast<RuntimeUiEventType>(type))
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t FocusDocumentElement(const std::uint64_t high, const std::uint64_t low,
                                                        const std::uint64_t generation,
                                                        const std::uint64_t element) noexcept
        {
            return ActiveServices &&
                           ActiveServices->FocusManagedUiDocumentElement(AssetId(high, low), generation, element)
                       ? 1
                       : 0;
        }
    } // namespace

    ManagedRuntimeUiScope::ManagedRuntimeUiScope(IScriptRuntimeServices* services) noexcept : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeUiScope::~ManagedRuntimeUiScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeUi(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "ResolveDocumentRootIcall",
                                 reinterpret_cast<void*>(&ResolveDocumentRoot));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "ResolveDocumentElementByIdIcall",
                                 reinterpret_cast<void*>(&ResolveDocumentElementById));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "ResolveDocumentElementByNameIcall",
                                 reinterpret_cast<void*>(&ResolveDocumentElementByName));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "DocumentElementAliveIcall",
                                 reinterpret_cast<void*>(&DocumentElementAlive));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetDocumentElementTextIcall",
                                 reinterpret_cast<void*>(&GetDocumentElementText));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetDocumentElementTextIcall",
                                 reinterpret_cast<void*>(&SetDocumentElementText));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetDocumentElementValueIcall",
                                 reinterpret_cast<void*>(&GetDocumentElementValue));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetDocumentElementValueIcall",
                                 reinterpret_cast<void*>(&SetDocumentElementValue));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "GetDocumentElementFlagIcall",
                                 reinterpret_cast<void*>(&GetDocumentElementFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "SetDocumentElementFlagIcall",
                                 reinterpret_cast<void*>(&SetDocumentElementFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "ConsumeDocumentElementEventIcall",
                                 reinterpret_cast<void*>(&ConsumeDocumentElementEvent));
        assembly.AddInternalCall("Keire.NativeRuntimeUi", "FocusDocumentElementIcall",
                                 reinterpret_cast<void*>(&FocusDocumentElement));
    }
} // namespace Keire::Detail
