#include "KeireInternal/Scripting/ManagedRuntimeAssets.h"

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
        struct NativeRuntimeAssetStatus
        {
            std::uint64_t Revision = 0;
            std::uint8_t State = 0;
            std::uint8_t UsingFallback = 1;
        };
        static_assert(sizeof(NativeRuntimeAssetStatus) == 16);

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

        [[nodiscard]] std::uint64_t BeginRuntimeAssetLoad(const std::uint64_t generation, const std::uint64_t assetHigh,
                                                          const std::uint64_t assetLow, const std::uint64_t typeHigh,
                                                          const std::uint64_t typeLow,
                                                          const std::uint8_t priority) noexcept
        {
            if (!ActiveServices || priority > static_cast<std::uint8_t>(AssetPriority::Background))
                return 0;
            return ActiveServices->BeginManagedRuntimeAssetLoad(generation, AssetId(assetHigh, assetLow),
                                                                AssetTypeId(AssetId(typeHigh, typeLow)),
                                                                static_cast<AssetPriority>(priority));
        }

        [[nodiscard]] std::uint8_t GetRuntimeAssetStatus(const std::uint64_t handle,
                                                         NativeRuntimeAssetStatus* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto status = ActiveServices->ManagedRuntimeAsset(handle);
            if (!status)
                return 0;
            destination->Revision = status->Revision;
            destination->State = static_cast<std::uint8_t>(status->State);
            destination->UsingFallback = status->UsingFallback ? 1 : 0;
            return 1;
        }

        [[nodiscard]] int GetRuntimeAssetDiagnostic(const std::uint64_t handle, const std::uint8_t field,
                                                    std::uint8_t* destination, const int capacity) noexcept
        {
            if (!ActiveServices || field > 1)
                return -1;
            const auto status = ActiveServices->ManagedRuntimeAsset(handle);
            if (!status)
                return -1;
            const std::string_view text = field == 0 ? status->Diagnostic.Operation : status->Diagnostic.Message;
            return CopyText(text, destination, capacity);
        }

        [[nodiscard]] std::uint8_t ReleaseRuntimeAsset(const std::uint64_t handle) noexcept
        {
            return ActiveServices && ActiveServices->ReleaseManagedRuntimeAsset(handle) ? 1 : 0;
        }
    } // namespace

    ManagedRuntimeAssetsScope::ManagedRuntimeAssetsScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeAssetsScope::~ManagedRuntimeAssetsScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeAssets(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeAssets", "BeginRuntimeAssetLoadIcall",
                                 reinterpret_cast<void*>(&BeginRuntimeAssetLoad));
        assembly.AddInternalCall("Keire.NativeAssets", "GetRuntimeAssetStatusIcall",
                                 reinterpret_cast<void*>(&GetRuntimeAssetStatus));
        assembly.AddInternalCall("Keire.NativeAssets", "GetRuntimeAssetDiagnosticIcall",
                                 reinterpret_cast<void*>(&GetRuntimeAssetDiagnostic));
        assembly.AddInternalCall("Keire.NativeAssets", "ReleaseRuntimeAssetIcall",
                                 reinterpret_cast<void*>(&ReleaseRuntimeAsset));
    }
} // namespace Keire::Detail
