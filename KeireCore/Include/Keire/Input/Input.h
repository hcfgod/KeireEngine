#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Event.h"
#include "Keire/Ref.h"
#include "Keire/Time.h"
#include "Keire/Ui.h"
#include "Keire/Window.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class InputMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    enum class InputDeviceType : std::uint8_t
    {
        Keyboard,
        Mouse,
        Gamepad
    };

    enum class InputActionPhase : std::uint8_t
    {
        Disabled,
        Waiting,
        Started,
        Performed,
        Canceled
    };

    enum class InputContextRole : std::uint8_t
    {
        Gameplay,
        EditorControl
    };

    class InputDeviceId final
    {
      public:
        constexpr InputDeviceId() noexcept = default;
        explicit constexpr InputDeviceId(const std::uint32_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const InputDeviceId&) const noexcept = default;

      private:
        std::uint32_t m_Value = 0;
    };

    class InputUserId final
    {
      public:
        constexpr InputUserId() noexcept = default;
        explicit constexpr InputUserId(const std::uint32_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const InputUserId&) const noexcept = default;

      private:
        std::uint32_t m_Value = 0;
    };

    struct InputVector2
    {
        float X = 0.0F;
        float Y = 0.0F;
        [[nodiscard]] auto operator<=>(const InputVector2&) const noexcept = default;
    };

    struct InputValue
    {
        InputValueType Type = InputValueType::Boolean;
        float X = 0.0F;
        float Y = 0.0F;

        [[nodiscard]] bool AsBoolean(float threshold = 0.5F) const noexcept;
        [[nodiscard]] float AsAxis1D() const noexcept { return X; }
        [[nodiscard]] InputVector2 AsAxis2D() const noexcept { return {X, Y}; }
        [[nodiscard]] float Magnitude() const noexcept;
        [[nodiscard]] bool NearlyEquals(const InputValue& other, float epsilon = 0.0001F) const noexcept;
    };

    struct InputDeviceDescriptor
    {
        InputDeviceId Id;
        InputDeviceType Type = InputDeviceType::Keyboard;
        std::string Name;
        std::string HardwareKey;
        bool Connected = false;
        bool Paired = false;
    };

    struct InputUserDescriptor
    {
        InputUserId Id;
        std::string Name;
        std::string ControlScheme;
        std::vector<InputDeviceId> Devices;
    };

    struct InputDeviceConnectedEvent
    {
        InputDeviceDescriptor Device;
    };

    struct InputDeviceDisconnectedEvent
    {
        InputDeviceId Device;
        InputDeviceType Type = InputDeviceType::Keyboard;
    };

    struct InputUserJoinedEvent
    {
        InputUserId User;
        InputDeviceId JoiningDevice;
    };

    struct InputUserLeftEvent
    {
        InputUserId User;
    };

    struct InputActionEvent
    {
        AssetId Asset;
        AssetId Map;
        AssetId Action;
        InputUserId User;
        InputDeviceId Device;
        InputActionPhase Phase = InputActionPhase::Waiting;
        InputValue Value;
        std::uint64_t Frame = 0;
        std::uint64_t TimestampNanoseconds = 0;
        double DurationSeconds = 0.0;
    };

    struct FixedTickInputAction
    {
        std::uint64_t Context = 0;
        AssetId ContextAsset;
        AssetId Map;
        AssetId Action;
        InputUserId User;
        InputActionPhase Phase = InputActionPhase::Waiting;
        InputValue Value;
        bool Started = false;
        bool Performed = false;
        bool Canceled = false;
    };

    struct FixedTickInputSnapshot
    {
        std::uint64_t Tick = 0;
        std::uint64_t InputMapFingerprint = 0;
        std::vector<FixedTickInputAction> Actions;
    };

    struct InputSystemSpecification
    {
        InputMode Mode = InputMode::Disabled;
        std::size_t MaximumUsers = 4;
        std::size_t EventCapacity = 4096;
        bool AutoJoin = true;
        bool AllowSharedDevices = false;
        std::filesystem::path BindingOverrideDirectory;
    };

    struct InteractiveRebindOptions
    {
        float MagnitudeThreshold = 0.5F;
        double TimeoutSeconds = 5.0;
        std::vector<InputDeviceType> AllowedDevices;
        std::vector<std::string> ExcludedControls{"<Keyboard>/escape"};
        bool SequenceCompositeParts = true;
    };

    enum class RebindStatus : std::uint8_t
    {
        Listening,
        Candidate,
        Completed,
        Cancelled,
        TimedOut
    };

    enum class RebindConflictResolution : std::uint8_t
    {
        Replace,
        KeepBoth,
        Cancel
    };

    struct RebindConflict
    {
        AssetId Binding;
        AssetId Action;
        std::string Path;
    };

    namespace Detail
    {
        class InputContextState;
        class InputSubscriptionState;
    } // namespace Detail

    class KEIRE_API InputActionSubscription final
    {
      public:
        InputActionSubscription() noexcept = default;
        InputActionSubscription(const InputActionSubscription&) = delete;
        InputActionSubscription& operator=(const InputActionSubscription&) = delete;
        InputActionSubscription(InputActionSubscription&& other) noexcept;
        InputActionSubscription& operator=(InputActionSubscription&& other) noexcept;
        ~InputActionSubscription();

        void Disconnect() noexcept;
        [[nodiscard]] bool Connected() const noexcept;

      private:
        friend class InputActionContext;
        InputActionSubscription(WeakRef<Detail::InputSubscriptionState> state, std::uint64_t id) noexcept;

        WeakRef<Detail::InputSubscriptionState> m_State;
        std::uint64_t m_Id = 0;
    };

    class KEIRE_API InputCaptureOverride final
    {
      public:
        InputCaptureOverride() noexcept = default;
        InputCaptureOverride(const InputCaptureOverride&) = delete;
        InputCaptureOverride& operator=(const InputCaptureOverride&) = delete;
        InputCaptureOverride(InputCaptureOverride&& other) noexcept;
        InputCaptureOverride& operator=(InputCaptureOverride&& other) noexcept;
        ~InputCaptureOverride();

        void Reset() noexcept;
        [[nodiscard]] bool Active() const noexcept;

      private:
        friend class InputActionContext;
        InputCaptureOverride(WeakRef<Detail::InputContextState> context, AssetId map) noexcept;
        WeakRef<Detail::InputContextState> m_Context;
        AssetId m_Map;
    };

    class KEIRE_API InputActionHandle final
    {
      public:
        InputActionHandle() noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] AssetId Id() const noexcept { return m_Action; }
        [[nodiscard]] InputActionPhase Phase() const noexcept;
        [[nodiscard]] InputValue Value() const noexcept;
        [[nodiscard]] bool WasStartedThisFrame() const noexcept;
        [[nodiscard]] bool WasPerformedThisFrame() const noexcept;
        [[nodiscard]] bool WasCanceledThisFrame() const noexcept;

      private:
        friend class InputActionContext;
        InputActionHandle(WeakRef<Detail::InputContextState> context, AssetId action) noexcept;
        WeakRef<Detail::InputContextState> m_Context;
        AssetId m_Action;
    };

    class KEIRE_API InputActionContext final : public RefCounted
    {
      public:
        class Impl;
        ~InputActionContext() override;

        [[nodiscard]] InputUserId User() const noexcept;
        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] InputActionHandle FindAction(AssetId id) const noexcept;
        [[nodiscard]] InputActionHandle FindAction(std::string_view map, std::string_view action) const noexcept;
        [[nodiscard]] bool EnableMap(AssetId id);
        [[nodiscard]] bool EnableMap(std::string_view name);
        [[nodiscard]] bool DisableMap(AssetId id);
        void DisableAll() noexcept;
        [[nodiscard]] bool MapEnabled(AssetId id) const noexcept;
        [[nodiscard]] InputCaptureOverride OverrideUiCapture(AssetId map);
        [[nodiscard]] InputCaptureOverride OverrideUiCapture(std::string_view map);
        [[nodiscard]] InputActionSubscription Subscribe(AssetId action,
                                                        std::function<void(const InputActionEvent&)> callback);
        void SaveBindingOverrides(std::string_view profile) const;
        [[nodiscard]] std::size_t LoadBindingOverrides(std::string_view profile);
        void ClearBindingOverrides();

      private:
        friend class InputSystem;
        friend class InteractiveRebindOperation;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit InputActionContext(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API InteractiveRebindOperation final : public RefCounted
    {
      public:
        class Impl;
        ~InteractiveRebindOperation() override;
        [[nodiscard]] RebindStatus Status() const noexcept;
        [[nodiscard]] AssetId TargetBinding() const noexcept;
        [[nodiscard]] std::string CandidatePath() const;
        [[nodiscard]] std::vector<RebindConflict> Conflicts() const;
        [[nodiscard]] double RemainingSeconds() const noexcept;
        void Apply(RebindConflictResolution resolution);
        void Cancel() noexcept;

      private:
        friend class InputSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit InteractiveRebindOperation(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API InputSystem final : public RefCounted
    {
      public:
        InputSystem(InputSystemSpecification specification, const Ref<WindowSystem>& windows, Ref<AssetSystem> assets,
                    Ref<EventBus> events = {});
        ~InputSystem() override;

        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        [[nodiscard]] std::vector<InputDeviceDescriptor> Devices() const;
        [[nodiscard]] std::vector<InputUserDescriptor> Users() const;
        [[nodiscard]] InputUserId CreateUser(std::string name = {});
        [[nodiscard]] bool RemoveUser(InputUserId user);
        [[nodiscard]] bool PairDevice(InputUserId user, InputDeviceId device, bool shared = false);
        [[nodiscard]] bool UnpairDevice(InputUserId user, InputDeviceId device);
        [[nodiscard]] bool SetControlScheme(InputUserId user, std::string scheme, bool locked = true);
        [[nodiscard]] bool ClearControlSchemeLock(InputUserId user);
        [[nodiscard]] Ref<InputActionContext> CreateActionContext(AssetId asset, InputUserId user,
                                                                  InputContextRole role = InputContextRole::Gameplay);
        [[nodiscard]] Ref<InputActionContext> CreateActionContext(InputActionAssetDefinition definition,
                                                                  InputUserId user,
                                                                  InputContextRole role = InputContextRole::Gameplay);
        [[nodiscard]] Ref<InteractiveRebindOperation> BeginInteractiveRebind(const Ref<InputActionContext>& context,
                                                                             AssetId binding,
                                                                             InteractiveRebindOptions options = {});
        [[nodiscard]] std::uint64_t Frame() const noexcept;
        [[nodiscard]] FixedTickInputSnapshot CaptureFixedTick(std::uint64_t tick);
        void ApplyFixedTick(const FixedTickInputSnapshot& snapshot);
        void SetGameplayPlayback(bool enabled);
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        friend class Application;
        class Impl;
        void AdvanceFrame(TimeStep unscaledDelta, UiCaptureState capture, bool suspended);
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire

template <> struct std::hash<Keire::InputDeviceId>
{
    std::size_t operator()(const Keire::InputDeviceId value) const noexcept
    {
        return std::hash<std::uint32_t>{}(value.Value());
    }
};

template <> struct std::hash<Keire::InputUserId>
{
    std::size_t operator()(const Keire::InputUserId value) const noexcept
    {
        return std::hash<std::uint32_t>{}(value.Value());
    }
};
