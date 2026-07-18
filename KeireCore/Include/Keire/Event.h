#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace Keire
{
    enum class EventFlow : std::uint8_t
    {
        Continue,
        Handled
    };

    using EventPriority = std::int32_t;

    namespace EventPriorities
    {
        inline constexpr EventPriority Lowest = -1000;
        inline constexpr EventPriority Low = -500;
        inline constexpr EventPriority Normal = 0;
        inline constexpr EventPriority High = 500;
        inline constexpr EventPriority Highest = 1000;
    } // namespace EventPriorities

    struct EventBusSpecification
    {
        std::size_t QueueCapacity = 4096;
    };

    struct EventBusStatistics
    {
        std::size_t QueueSize = 0;
        std::size_t QueueCapacity = 0;
        std::size_t QueueHighWaterMark = 0;
        std::uint64_t DispatchedEvents = 0;
        std::uint64_t DroppedEvents = 0;
    };

    class EventBus;

    namespace Detail
    {
        class EventBusState;
    }

    class KEIRE_API EventView final
    {
      public:
        [[nodiscard]] const std::type_info& Type() const noexcept { return *m_Type; }

        template <typename T> [[nodiscard]] bool Is() const noexcept
        {
            using Event = std::remove_cvref_t<T>;
            return *m_Type == typeid(Event);
        }

        template <typename T> [[nodiscard]] const std::remove_cvref_t<T>* TryGet() const noexcept
        {
            using Event = std::remove_cvref_t<T>;
            return Is<Event>() ? static_cast<const Event*>(m_Payload) : nullptr;
        }

      private:
        friend class EventBus;
        friend class Detail::EventBusState;

        EventView(const std::type_info& type, const void* payload) noexcept : m_Type(&type), m_Payload(payload) {}

        const std::type_info* m_Type;
        const void* m_Payload;
    };

    class KEIRE_API EventSubscription final
    {
      public:
        EventSubscription() noexcept = default;
        EventSubscription(const EventSubscription&) = delete;
        EventSubscription& operator=(const EventSubscription&) = delete;
        EventSubscription(EventSubscription&& other) noexcept;
        EventSubscription& operator=(EventSubscription&& other) noexcept;
        ~EventSubscription();

        void Disconnect() noexcept;
        [[nodiscard]] bool Connected() const;

      private:
        friend class EventBus;
        friend class Detail::EventBusState;

        EventSubscription(WeakRef<Detail::EventBusState> state, std::uint64_t id) noexcept;

        WeakRef<Detail::EventBusState> m_State;
        std::uint64_t m_Id = 0;
    };

    class KEIRE_API EventBus final : public RefCounted
    {
      public:
        using AnyCallback = std::function<EventFlow(const EventView&)>;

        explicit EventBus(const EventBusSpecification& specification = {});
        ~EventBus() override;

        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        template <typename T>
        [[nodiscard]] EventSubscription Subscribe(std::function<EventFlow(const std::remove_cvref_t<T>&)> callback,
                                                  const EventPriority priority = EventPriorities::Normal)
        {
            using Event = std::remove_cvref_t<T>;
            if (!callback)
            {
                throw std::invalid_argument("Event callback must not be empty.");
            }

            return SubscribeErased(
                typeid(Event), false, [callback = std::move(callback)](const EventView& view)
                { return callback(*view.TryGet<Event>()); }, priority);
        }

        [[nodiscard]] EventSubscription SubscribeAny(AnyCallback callback,
                                                     EventPriority priority = EventPriorities::Normal);

        template <typename T> [[nodiscard]] bool Dispatch(const T& event)
        {
            using Event = std::remove_cvref_t<T>;
            return DispatchErased(typeid(Event), std::addressof(event));
        }

        template <typename T> [[nodiscard]] bool TryEnqueue(T&& event)
        {
            using Event = std::remove_cvref_t<T>;
            static_assert(std::constructible_from<Event, T&&>, "Queued events must be constructible from the value.");
            auto payload = std::make_shared<Event>(std::forward<T>(event));
            return TryEnqueueErased(typeid(Event), std::move(payload));
        }

        [[nodiscard]] std::size_t DispatchQueued();
        [[nodiscard]] EventBusStatistics Statistics() const;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close();

      private:
        [[nodiscard]] EventSubscription SubscribeErased(const std::type_info& type, bool any, AnyCallback callback,
                                                        EventPriority priority);
        [[nodiscard]] bool DispatchErased(const std::type_info& type, const void* payload);
        [[nodiscard]] bool TryEnqueueErased(const std::type_info& type, std::shared_ptr<const void> payload);

        Ref<Detail::EventBusState> m_State;
    };
} // namespace Keire
