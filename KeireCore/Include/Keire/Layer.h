#pragma once

#include "Keire/Api.h"
#include "Keire/Event.h"
#include "Keire/Time.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Keire
{
    class Application;

    class LayerId final
    {
      public:
        constexpr LayerId() noexcept = default;
        explicit constexpr LayerId(const std::uint64_t value) noexcept : m_Value(value) {}

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const LayerId&) const noexcept = default;

      private:
        std::uint64_t m_Value = 0;
    };

    class KEIRE_API Layer
    {
      public:
        explicit Layer(std::string name = "Layer");
        virtual ~Layer();

        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;
        Layer(Layer&&) = delete;
        Layer& operator=(Layer&&) = delete;

        [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }
        [[nodiscard]] bool IsAttached() const noexcept { return m_Application != nullptr; }

      protected:
        virtual void OnAttach() {}
        virtual void OnDetach() noexcept {}
        virtual void OnFixedUpdate(const Time&) {}
        virtual void OnUpdate(const Time&) {}
        virtual EventFlow OnEvent(const EventView&) { return EventFlow::Continue; }

        [[nodiscard]] Application& Owner();
        [[nodiscard]] const Application& Owner() const;

        template <typename T>
        void Listen(std::function<EventFlow(const std::remove_cvref_t<T>&)> callback,
                    const EventPriority priority = EventPriorities::Normal)
        {
            if (m_Detaching)
            {
                return;
            }
            m_Subscriptions.push_back(EventSystem()->Subscribe<T>(std::move(callback), priority));
        }

        void ListenAny(EventBus::AnyCallback callback, EventPriority priority = EventPriorities::Normal);

      private:
        friend class LayerStack;

        [[nodiscard]] Ref<EventBus> EventSystem() const;
        void Attach(Application& application);
        void Detach() noexcept;

        std::string m_Name;
        Application* m_Application = nullptr;
        bool m_Detaching = false;
        std::vector<EventSubscription> m_Subscriptions;
    };

    class KEIRE_API LayerStack final
    {
      public:
        ~LayerStack();

        LayerStack(const LayerStack&) = delete;
        LayerStack& operator=(const LayerStack&) = delete;
        LayerStack(LayerStack&&) = delete;
        LayerStack& operator=(LayerStack&&) = delete;

        [[nodiscard]] LayerId PushLayer(std::unique_ptr<Layer> layer);
        [[nodiscard]] LayerId PushOverlay(std::unique_ptr<Layer> overlay);
        [[nodiscard]] bool Remove(LayerId id);

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] bool Empty() const noexcept { return Size() == 0; }
        [[nodiscard]] bool Active() const noexcept;

      private:
        friend class Application;
        class Impl;

        explicit LayerStack(Application& application);
        void Activate();
        void ApplyPending();
        void Deactivate() noexcept;
        void FixedUpdate(const Time& time);
        void Update(const Time& time);
        EventFlow Dispatch(const EventView& event);

        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
