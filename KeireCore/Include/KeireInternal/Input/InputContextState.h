#pragma once

#include "Keire/Input/Input.h"
#include "KeireInternal/Assets/AssetInternal.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    class InputRuntimeState;

    struct ActionRuntime
    {
        InputActionPhase Phase = InputActionPhase::Waiting;
        InputValue Value;
        InputValue PendingRelativeValue;
        std::uint64_t StartedFrame = 0;
        std::uint64_t PerformedFrame = 0;
        std::uint64_t CanceledFrame = 0;
        std::uint64_t PressTimestamp = 0;
        std::uint64_t LastReleaseTimestamp = 0;
        std::uint32_t TapCount = 0;
        InputDeviceId ActiveDevice;
        bool Active = false;
        bool HoldPerformed = false;
        bool PendingStarted = false;
        bool PendingPerformed = false;
        bool PendingCanceled = false;
    };

    enum class ActionOccurrence
    {
        Started,
        Performed,
        Canceled
    };

    class InputContextState final : public RefCounted
    {
      public:
        WeakRef<InputRuntimeState> Runtime;
        Keire::AssetHandle<InputActionAsset> AssetHandle;
        AssetId AssetIdValue;
        InputUserId User;
        InputContextRole Role = InputContextRole::Gameplay;
        std::uint64_t ContextId = 0;
        InputActionAssetDefinition Definition;
        std::uint64_t AssetRevision = std::numeric_limits<std::uint64_t>::max();
        std::unordered_set<AssetId> EnabledMaps;
        std::unordered_set<AssetId> ExplicitlyEnabledActions;
        std::unordered_set<AssetId> ExplicitlyDisabledActions;
        std::unordered_set<AssetId> EnabledActions;
        std::unordered_set<AssetId> CaptureBypassMaps;
        std::unordered_map<AssetId, ActionRuntime> Actions;
        std::unordered_map<AssetId, std::string> Overrides;
        std::unordered_set<AssetId> DisabledBindings;
        std::map<std::uint64_t, std::pair<AssetId, std::function<void(const InputActionEvent&)>>> Subscribers;
        std::vector<std::uint64_t> PendingDisconnects;
        std::size_t EmitDepth = 0;
        std::uint64_t NextSubscriber = 1;
        bool Open = true;

        void Rebuild();
        [[nodiscard]] const InputActionDefinition* FindAction(AssetId id) const noexcept;
        [[nodiscard]] const InputBindingDefinition* FindBinding(AssetId id) const noexcept;
        [[nodiscard]] bool OccurredThisFrame(AssetId action, ActionOccurrence occurrence) const noexcept;
        void Emit(const InputActionEvent& event);
        void Disconnect(std::uint64_t id) noexcept
        {
            if (EmitDepth > 0)
                PendingDisconnects.push_back(id);
            else
                Subscribers.erase(id);
        }
    };

    class InputSubscriptionState final : public RefCounted
    {
      public:
        explicit InputSubscriptionState(WeakRef<InputContextState> context) : Context(std::move(context)) {}
        void Disconnect(const std::uint64_t id) noexcept
        {
            if (const auto context = Context.Lock())
                context->Disconnect(id);
        }
        [[nodiscard]] bool Connected(const std::uint64_t id) const noexcept
        {
            if (const auto context = Context.Lock())
                return context->Open && context->Subscribers.contains(id);
            return false;
        }
        WeakRef<InputContextState> Context;
    };

    struct RebindState final : public RefCounted
    {
        WeakRef<InputRuntimeState> Runtime;
        WeakRef<InputContextState> Context;
        std::vector<AssetId> Targets;
        std::size_t TargetIndex = 0;
        InteractiveRebindOptions Options;
        RebindStatus Status = RebindStatus::Listening;
        std::string Candidate;
        InputDeviceType CandidateDevice = InputDeviceType::Keyboard;
        std::vector<RebindConflict> Conflicts;
        double Remaining = 0.0;
    };
} // namespace Keire::Detail
