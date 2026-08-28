#include "Keire/Input/Input.h"

#include "KeireInternal/Input/InputContextState.h"

#include <utility>

namespace Keire
{
    InputActionSubscription::InputActionSubscription(WeakRef<Detail::InputSubscriptionState> state,
                                                     const std::uint64_t id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    InputActionSubscription::InputActionSubscription(InputActionSubscription&& other) noexcept
        : m_State(std::move(other.m_State)), m_Id(std::exchange(other.m_Id, 0))
    {
    }

    InputActionSubscription& InputActionSubscription::operator=(InputActionSubscription&& other) noexcept
    {
        if (this != &other)
        {
            Disconnect();
            m_State = std::move(other.m_State);
            m_Id = std::exchange(other.m_Id, 0);
        }
        return *this;
    }

    InputActionSubscription::~InputActionSubscription() { Disconnect(); }

    void InputActionSubscription::Disconnect() noexcept
    {
        if (m_Id)
        {
            if (const auto state = m_State.Lock())
                state->Disconnect(m_Id);
            m_Id = 0;
            m_State.Reset();
        }
    }

    bool InputActionSubscription::Connected() const noexcept
    {
        if (const auto state = m_State.Lock())
            return m_Id && state->Connected(m_Id);
        return false;
    }

    InputCaptureOverride::InputCaptureOverride(WeakRef<Detail::InputContextState> context, const AssetId map) noexcept
        : m_Context(std::move(context)), m_Map(map)
    {
    }

    InputCaptureOverride::InputCaptureOverride(InputCaptureOverride&& other) noexcept
        : m_Context(std::move(other.m_Context)), m_Map(std::exchange(other.m_Map, {}))
    {
    }

    InputCaptureOverride& InputCaptureOverride::operator=(InputCaptureOverride&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Context = std::move(other.m_Context);
            m_Map = std::exchange(other.m_Map, {});
        }
        return *this;
    }

    InputCaptureOverride::~InputCaptureOverride() { Reset(); }

    void InputCaptureOverride::Reset() noexcept
    {
        if (m_Map)
        {
            if (const auto context = m_Context.Lock())
                context->CaptureBypassMaps.erase(m_Map);
            m_Map = {};
            m_Context.Reset();
        }
    }

    bool InputCaptureOverride::Active() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->Open && m_Map && context->CaptureBypassMaps.contains(m_Map);
    }

    InputActionHandle::InputActionHandle(WeakRef<Detail::InputContextState> context, const AssetId action) noexcept
        : m_Context(std::move(context)), m_Action(action)
    {
    }

    InputActionHandle::operator bool() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->Open && context->Actions.contains(m_Action);
    }

    InputActionPhase InputActionHandle::Phase() const noexcept
    {
        if (const auto context = m_Context.Lock(); context && context->Open && context->Actions.contains(m_Action))
            return context->Actions.at(m_Action).Phase;
        return InputActionPhase::Disabled;
    }

    InputValue InputActionHandle::Value() const noexcept
    {
        if (const auto context = m_Context.Lock(); context && context->Open && context->Actions.contains(m_Action))
            return context->Actions.at(m_Action).Value;
        return {};
    }

    bool InputActionHandle::WasStartedThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->OccurredThisFrame(m_Action, Detail::ActionOccurrence::Started);
    }

    bool InputActionHandle::WasPerformedThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->OccurredThisFrame(m_Action, Detail::ActionOccurrence::Performed);
    }

    bool InputActionHandle::WasCanceledThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->OccurredThisFrame(m_Action, Detail::ActionOccurrence::Canceled);
    }
} // namespace Keire
