#include "Keire/Event.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <list>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace Keire::Detail
{
class EventBusState final : public RefCounted
{
  public:
    explicit EventBusState(const EventBusSpecification& specification)
        : m_OwnerThread(std::this_thread::get_id()), m_QueueCapacity(specification.QueueCapacity)
    {
        if (m_QueueCapacity == 0)
        {
            throw std::invalid_argument("Event queue capacity must be greater than zero.");
        }
    }

    ~EventBusState() override { ForceClose(); }

    EventSubscription Subscribe(const std::type_info& type, const bool any, EventBus::AnyCallback callback,
                                const EventPriority priority)
    {
        RequireOwner("Subscribe");
        if (!m_Open.load(std::memory_order_acquire))
        {
            throw std::logic_error("Cannot subscribe to a closed EventBus.");
        }

        auto listener = std::make_shared<Listener>();
        listener->Id = m_NextListenerId++;
        listener->Sequence = m_NextSequence++;
        listener->Type = &type;
        listener->Any = any;
        listener->Priority = priority;
        listener->Callback = std::move(callback);

        auto position = m_Listeners.end();
        for (auto iterator = m_Listeners.begin(); iterator != m_Listeners.end(); ++iterator)
        {
            if ((*iterator)->Priority < priority)
            {
                position = iterator;
                break;
            }
        }
        m_Listeners.insert(position, listener);
        {
            std::scoped_lock lock(m_ListenerMutex);
            m_ListenerLookup.emplace(listener->Id, listener);
        }
        return EventSubscription(WeakRef<EventBusState>(Self()), listener->Id);
    }

    bool Dispatch(const std::type_info& type, const void* payload)
    {
        RequireOwner("Dispatch");
        if (!m_Open.load(std::memory_order_acquire))
        {
            return false;
        }

        const auto maximumSequence = m_NextSequence - 1;
        ++m_DispatchDepth;
        try
        {
            const EventView view(type, payload);
            for (const auto& listener : m_Listeners)
            {
                if (listener->Sequence > maximumSequence || !listener->Active.load(std::memory_order_acquire) ||
                    (!listener->Any && *listener->Type != type))
                {
                    continue;
                }

                if (listener->Callback(view) == EventFlow::Handled)
                {
                    FinishDispatch();
                    m_DispatchedEvents.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
        }
        catch (...)
        {
            FinishDispatch();
            throw;
        }

        FinishDispatch();
        m_DispatchedEvents.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool TryEnqueue(const std::type_info& type, std::shared_ptr<const void> payload)
    {
        if (!m_Open.load(std::memory_order_acquire))
        {
            return false;
        }

        std::scoped_lock lock(m_QueueMutex);
        if (!m_Open.load(std::memory_order_relaxed))
        {
            return false;
        }
        if (m_Queue.size() >= m_QueueCapacity)
        {
            m_DroppedEvents.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        m_Queue.push_back({&type, std::move(payload)});
        const auto size = m_Queue.size();
        auto highWater = m_QueueHighWaterMark.load(std::memory_order_relaxed);
        while (size > highWater && !m_QueueHighWaterMark.compare_exchange_weak(
                                       highWater, size, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
        return true;
    }

    std::size_t DispatchQueued()
    {
        RequireOwner("DispatchQueued");
        if (!m_Open.load(std::memory_order_acquire))
        {
            return 0;
        }

        std::deque<QueuedEvent> queued;
        {
            std::scoped_lock lock(m_QueueMutex);
            queued.swap(m_Queue);
        }

        for (const auto& event : queued)
        {
            (void)Dispatch(*event.Type, event.Payload.get());
        }
        return queued.size();
    }

    EventBusStatistics Statistics() const noexcept
    {
        EventBusStatistics statistics;
        {
            std::scoped_lock lock(m_QueueMutex);
            statistics.QueueSize = m_Queue.size();
        }
        statistics.QueueCapacity = m_QueueCapacity;
        statistics.QueueHighWaterMark = m_QueueHighWaterMark.load(std::memory_order_relaxed);
        statistics.DispatchedEvents = m_DispatchedEvents.load(std::memory_order_relaxed);
        statistics.DroppedEvents = m_DroppedEvents.load(std::memory_order_relaxed);
        return statistics;
    }

    bool IsOpen() const noexcept { return m_Open.load(std::memory_order_acquire); }

    bool IsConnected(const std::uint64_t id) const noexcept
    {
        std::scoped_lock lock(m_ListenerMutex);
        const auto iterator = m_ListenerLookup.find(id);
        return iterator != m_ListenerLookup.end() && iterator->second->Active.load(std::memory_order_acquire);
    }

    void Disconnect(const std::uint64_t id) noexcept
    {
        std::scoped_lock lock(m_ListenerMutex);
        if (const auto iterator = m_ListenerLookup.find(id); iterator != m_ListenerLookup.end())
        {
            iterator->second->Active.store(false, std::memory_order_release);
            m_ListenerLookup.erase(iterator);
        }
    }

    void Close()
    {
        RequireOwner("Close");
        ForceClose();
    }

    Ref<EventBusState> Self()
    {
        // The state is always owned by EventBus before this method can be reached.
        return m_Self.Lock();
    }

    void BindSelf(const Ref<EventBusState>& self) noexcept { m_Self = self; }

  private:
    struct Listener
    {
        std::uint64_t Id = 0;
        std::uint64_t Sequence = 0;
        const std::type_info* Type = nullptr;
        EventPriority Priority = EventPriorities::Normal;
        bool Any = false;
        std::atomic<bool> Active{true};
        EventBus::AnyCallback Callback;
    };

    struct QueuedEvent
    {
        const std::type_info* Type;
        std::shared_ptr<const void> Payload;
    };

    void RequireOwner(const char* operation) const
    {
        if (std::this_thread::get_id() != m_OwnerThread)
        {
            throw std::logic_error(std::string("EventBus::") + operation + " must be called on its creating thread.");
        }
    }

    void FinishDispatch()
    {
        --m_DispatchDepth;
        if (m_DispatchDepth == 0)
        {
            m_Listeners.remove_if([](const auto& listener)
                                  { return !listener->Active.load(std::memory_order_acquire); });
        }
    }

    void ForceClose() noexcept
    {
        if (!m_Open.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        {
            std::scoped_lock lock(m_ListenerMutex);
            for (const auto& [id, listener] : m_ListenerLookup)
            {
                (void)id;
                listener->Active.store(false, std::memory_order_release);
            }
            m_ListenerLookup.clear();
        }
        {
            std::scoped_lock lock(m_QueueMutex);
            m_Queue.clear();
        }
        if (m_DispatchDepth == 0)
        {
            m_Listeners.clear();
        }
    }

    std::thread::id m_OwnerThread;
    const std::size_t m_QueueCapacity;
    std::atomic<bool> m_Open{true};
    WeakRef<EventBusState> m_Self;

    mutable std::mutex m_ListenerMutex;
    std::list<std::shared_ptr<Listener>> m_Listeners;
    std::unordered_map<std::uint64_t, std::shared_ptr<Listener>> m_ListenerLookup;
    std::uint64_t m_NextListenerId = 1;
    std::uint64_t m_NextSequence = 1;
    std::size_t m_DispatchDepth = 0;

    mutable std::mutex m_QueueMutex;
    std::deque<QueuedEvent> m_Queue;
    std::atomic<std::size_t> m_QueueHighWaterMark{0};
    std::atomic<std::uint64_t> m_DispatchedEvents{0};
    std::atomic<std::uint64_t> m_DroppedEvents{0};
};
} // namespace Keire::Detail

namespace Keire
{
EventSubscription::EventSubscription(WeakRef<Detail::EventBusState> state, const std::uint64_t id) noexcept
    : m_State(std::move(state)), m_Id(id)
{
}

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : m_State(std::move(other.m_State)), m_Id(std::exchange(other.m_Id, 0))
{
}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept
{
    if (this != &other)
    {
        Disconnect();
        m_State = std::move(other.m_State);
        m_Id = std::exchange(other.m_Id, 0);
    }
    return *this;
}

EventSubscription::~EventSubscription() { Disconnect(); }

void EventSubscription::Disconnect() noexcept
{
    if (m_Id != 0)
    {
        if (const auto state = m_State.Lock())
        {
            state->Disconnect(m_Id);
        }
        m_State.Reset();
        m_Id = 0;
    }
}

bool EventSubscription::Connected() const noexcept
{
    if (m_Id == 0)
    {
        return false;
    }
    const auto state = m_State.Lock();
    return state && state->IsConnected(m_Id);
}

EventBus::EventBus(const EventBusSpecification& specification)
{
    m_State = CreateRef<Detail::EventBusState>(specification);
    m_State->BindSelf(m_State);
}

EventBus::~EventBus() = default;

EventSubscription EventBus::SubscribeAny(AnyCallback callback, const EventPriority priority)
{
    if (!callback)
    {
        throw std::invalid_argument("Event callback must not be empty.");
    }
    return SubscribeErased(typeid(void), true, std::move(callback), priority);
}

std::size_t EventBus::DispatchQueued() { return m_State->DispatchQueued(); }

EventBusStatistics EventBus::Statistics() const noexcept { return m_State->Statistics(); }

bool EventBus::IsOpen() const noexcept { return m_State->IsOpen(); }

void EventBus::Close() { m_State->Close(); }

EventSubscription EventBus::SubscribeErased(const std::type_info& type, const bool any, AnyCallback callback,
                                            const EventPriority priority)
{
    return m_State->Subscribe(type, any, std::move(callback), priority);
}

bool EventBus::DispatchErased(const std::type_info& type, const void* payload)
{
    return m_State->Dispatch(type, payload);
}

bool EventBus::TryEnqueueErased(const std::type_info& type, std::shared_ptr<const void> payload)
{
    return m_State->TryEnqueue(type, std::move(payload));
}
} // namespace Keire
