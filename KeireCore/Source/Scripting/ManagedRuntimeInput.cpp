#include "KeireInternal/Scripting/ManagedRuntimeInput.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <ranges>

namespace Keire::Detail
{
    namespace
    {
        inline constexpr std::size_t MaximumManagedInputDevices = 32;
        inline constexpr std::size_t MaximumManagedRebindOperations = 16;

        struct NativeInputDevice
        {
            std::uint32_t Id = 0;
            std::uint8_t Type = 0;
            std::uint8_t Connected = 0;
            std::uint8_t Paired = 0;
            std::uint8_t Reserved = 0;
        };
        static_assert(sizeof(NativeInputDevice) == 8);

        struct NativeInputRebindSnapshot
        {
            std::uint64_t BindingHigh = 0;
            std::uint64_t BindingLow = 0;
            double RemainingSeconds = 0.0;
            std::uint32_t ConflictCount = 0;
            std::uint8_t Status = 0;
            std::uint8_t Reserved[3]{};
        };
        static_assert(sizeof(NativeInputRebindSnapshot) == 32);

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

        [[nodiscard]] std::vector<ManagedInputDevice> InputDevices()
        {
            if (!ActiveServices)
                return {};
            auto devices = ActiveServices->ManagedInputDevices();
            if (devices.size() > MaximumManagedInputDevices)
                devices.resize(MaximumManagedInputDevices);
            return devices;
        }

        [[nodiscard]] int GetInputDeviceCount() noexcept
        {
            try
            {
                return static_cast<int>(InputDevices().size());
            }
            catch (...)
            {
                return -1;
            }
        }

        [[nodiscard]] std::uint8_t GetInputDevice(const int index, NativeInputDevice* destination) noexcept
        {
            if (!destination || index < 0)
                return 0;
            try
            {
                const auto devices = InputDevices();
                if (static_cast<std::size_t>(index) >= devices.size())
                    return 0;
                const auto& device = devices[static_cast<std::size_t>(index)];
                *destination = {device.Id, static_cast<std::uint8_t>(device.Type),
                                device.Connected ? std::uint8_t{1} : std::uint8_t{0},
                                device.Paired ? std::uint8_t{1} : std::uint8_t{0}, 0};
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] int GetInputDeviceName(const std::uint32_t id, std::uint8_t* destination,
                                             const int capacity) noexcept
        {
            try
            {
                const auto devices = InputDevices();
                const auto found = std::ranges::find(devices, id, &ManagedInputDevice::Id);
                return found == devices.end() ? -1 : CopyText(found->Name, destination, capacity);
            }
            catch (...)
            {
                return -1;
            }
        }

        [[nodiscard]] int GetInputControlScheme(std::uint8_t* destination, const int capacity) noexcept
        {
            try
            {
                return ActiveServices ? CopyText(ActiveServices->ManagedInputControlScheme(), destination, capacity)
                                      : CopyText({}, destination, capacity);
            }
            catch (...)
            {
                return -1;
            }
        }

        [[nodiscard]] std::uint8_t SetInputControlScheme(const Coral::String scheme, const std::uint8_t locked) noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->SetManagedInputControlScheme(static_cast<std::string>(scheme),
                                                                                      locked != 0)
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::uint8_t ClearInputControlSchemeLock() noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->ClearManagedInputControlSchemeLock() ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::uint8_t SetGamepadRumble(const std::uint32_t device, const float lowFrequency,
                                                    const float highFrequency, const float durationSeconds) noexcept
        {
            return ActiveServices &&
                           ActiveServices->SetManagedGamepadRumble(device, lowFrequency, highFrequency, durationSeconds)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint64_t BeginInputRebind(const std::uint64_t bindingHigh, const std::uint64_t bindingLow,
                                                     const float threshold, const double timeoutSeconds,
                                                     const std::uint8_t allowedDeviceMask) noexcept
        {
            return ActiveServices
                       ? ActiveServices->BeginManagedInputRebind(AssetId(bindingHigh, bindingLow),
                                                                 {threshold, timeoutSeconds, allowedDeviceMask})
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetInputRebindSnapshot(const std::uint64_t operation,
                                                          NativeInputRebindSnapshot* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto snapshot = ActiveServices->ManagedInputRebind(operation);
            if (!snapshot)
                return 0;
            destination->BindingHigh = snapshot->Binding.High();
            destination->BindingLow = snapshot->Binding.Low();
            destination->RemainingSeconds = snapshot->RemainingSeconds;
            destination->ConflictCount = snapshot->ConflictCount;
            destination->Status = static_cast<std::uint8_t>(snapshot->Status);
            return 1;
        }

        [[nodiscard]] int GetInputRebindCandidate(const std::uint64_t operation, std::uint8_t* destination,
                                                  const int capacity) noexcept
        {
            if (!ActiveServices)
                return -1;
            const auto snapshot = ActiveServices->ManagedInputRebind(operation);
            return snapshot ? CopyText(snapshot->CandidatePath, destination, capacity) : -1;
        }

        [[nodiscard]] std::uint8_t ResolveInputRebind(const std::uint64_t operation,
                                                      const std::uint8_t resolution) noexcept
        {
            return ActiveServices && resolution <= static_cast<std::uint8_t>(ManagedInputRebindResolution::Cancel) &&
                           ActiveServices->ResolveManagedInputRebind(
                               operation, static_cast<ManagedInputRebindResolution>(resolution))
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t CancelInputRebind(const std::uint64_t operation) noexcept
        {
            return ActiveServices && ActiveServices->CancelManagedInputRebind(operation) ? 1 : 0;
        }

        [[nodiscard]] std::uint8_t SaveInputBindings(const Coral::String profile) noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->SaveManagedInputBindings(static_cast<std::string>(profile))
                           ? 1
                           : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] int LoadInputBindings(const Coral::String profile) noexcept
        {
            try
            {
                return ActiveServices ? ActiveServices->LoadManagedInputBindings(static_cast<std::string>(profile))
                                      : -1;
            }
            catch (...)
            {
                return -1;
            }
        }

        [[nodiscard]] std::uint8_t ClearInputBindings() noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->ClearManagedInputBindings() ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }
    } // namespace

    class ManagedInputOperationStore::Impl final
    {
      public:
        std::uint64_t NextOperation = 1;
        std::map<std::uint64_t, Ref<InteractiveRebindOperation>> Operations;
    };

    ManagedInputOperationStore::ManagedInputOperationStore() : m_Impl(std::make_unique<Impl>()) {}
    ManagedInputOperationStore::~ManagedInputOperationStore() { CancelAll(); }

    std::uint64_t ManagedInputOperationStore::Begin(const Ref<InputSystem>& input,
                                                    const Ref<InputActionContext>& context, const AssetId binding,
                                                    const ManagedInputRebindOptions options) noexcept
    {
        try
        {
            std::erase_if(m_Impl->Operations,
                          [](const auto& entry)
                          {
                              const auto status = entry.second->Status();
                              return status != RebindStatus::Listening && status != RebindStatus::Candidate;
                          });
            if (!input || !context || !binding || m_Impl->Operations.size() >= MaximumManagedRebindOperations ||
                options.AllowedDeviceMask == 0 || (options.AllowedDeviceMask & ~std::uint8_t{0x07}) != 0)
            {
                return 0;
            }
            InteractiveRebindOptions native;
            native.MagnitudeThreshold = options.MagnitudeThreshold;
            native.TimeoutSeconds = options.TimeoutSeconds;
            if ((options.AllowedDeviceMask & 0x01U) != 0)
                native.AllowedDevices.push_back(InputDeviceType::Keyboard);
            if ((options.AllowedDeviceMask & 0x02U) != 0)
                native.AllowedDevices.push_back(InputDeviceType::Mouse);
            if ((options.AllowedDeviceMask & 0x04U) != 0)
                native.AllowedDevices.push_back(InputDeviceType::Gamepad);
            const auto operation = m_Impl->NextOperation++;
            m_Impl->Operations.emplace(operation, input->BeginInteractiveRebind(context, binding, std::move(native)));
            return operation;
        }
        catch (...)
        {
            return 0;
        }
    }

    std::optional<ManagedInputRebindSnapshot>
    ManagedInputOperationStore::Status(const std::uint64_t operation) const noexcept
    {
        try
        {
            const auto found = m_Impl->Operations.find(operation);
            if (found == m_Impl->Operations.end())
                return std::nullopt;
            const auto& native = *found->second;
            return ManagedInputRebindSnapshot{operation,
                                              native.TargetBinding(),
                                              static_cast<ManagedInputRebindStatus>(native.Status()),
                                              native.CandidatePath(),
                                              native.RemainingSeconds(),
                                              static_cast<std::uint32_t>(native.Conflicts().size())};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ManagedInputOperationStore::Resolve(const std::uint64_t operation,
                                             const ManagedInputRebindResolution resolution) noexcept
    {
        try
        {
            const auto found = m_Impl->Operations.find(operation);
            if (found == m_Impl->Operations.end())
                return false;
            found->second->Apply(static_cast<RebindConflictResolution>(resolution));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedInputOperationStore::Cancel(const std::uint64_t operation) noexcept
    {
        const auto found = m_Impl->Operations.find(operation);
        if (found == m_Impl->Operations.end())
            return false;
        found->second->Cancel();
        return true;
    }

    void ManagedInputOperationStore::CancelAll() noexcept
    {
        for (const auto& [operation, value] : m_Impl->Operations)
        {
            (void)operation;
            value->Cancel();
        }
        m_Impl->Operations.clear();
    }

    ManagedRuntimeInputScope::ManagedRuntimeInputScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeInputScope::~ManagedRuntimeInputScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeInput(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeInput", "GetDeviceCountIcall",
                                 reinterpret_cast<void*>(&GetInputDeviceCount));
        assembly.AddInternalCall("Keire.NativeInput", "GetDeviceIcall", reinterpret_cast<void*>(&GetInputDevice));
        assembly.AddInternalCall("Keire.NativeInput", "GetDeviceNameIcall",
                                 reinterpret_cast<void*>(&GetInputDeviceName));
        assembly.AddInternalCall("Keire.NativeInput", "GetControlSchemeIcall",
                                 reinterpret_cast<void*>(&GetInputControlScheme));
        assembly.AddInternalCall("Keire.NativeInput", "SetControlSchemeIcall",
                                 reinterpret_cast<void*>(&SetInputControlScheme));
        assembly.AddInternalCall("Keire.NativeInput", "ClearControlSchemeLockIcall",
                                 reinterpret_cast<void*>(&ClearInputControlSchemeLock));
        assembly.AddInternalCall("Keire.NativeInput", "SetGamepadRumbleIcall",
                                 reinterpret_cast<void*>(&SetGamepadRumble));
        assembly.AddInternalCall("Keire.NativeInput", "BeginRebindIcall", reinterpret_cast<void*>(&BeginInputRebind));
        assembly.AddInternalCall("Keire.NativeInput", "GetRebindSnapshotIcall",
                                 reinterpret_cast<void*>(&GetInputRebindSnapshot));
        assembly.AddInternalCall("Keire.NativeInput", "GetRebindCandidateIcall",
                                 reinterpret_cast<void*>(&GetInputRebindCandidate));
        assembly.AddInternalCall("Keire.NativeInput", "ResolveRebindIcall",
                                 reinterpret_cast<void*>(&ResolveInputRebind));
        assembly.AddInternalCall("Keire.NativeInput", "CancelRebindIcall", reinterpret_cast<void*>(&CancelInputRebind));
        assembly.AddInternalCall("Keire.NativeInput", "SaveBindingsIcall", reinterpret_cast<void*>(&SaveInputBindings));
        assembly.AddInternalCall("Keire.NativeInput", "LoadBindingsIcall", reinterpret_cast<void*>(&LoadInputBindings));
        assembly.AddInternalCall("Keire.NativeInput", "ClearBindingsIcall",
                                 reinterpret_cast<void*>(&ClearInputBindings));
    }
} // namespace Keire::Detail
