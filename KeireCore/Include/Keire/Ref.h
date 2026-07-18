#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace Keire
{
    class RefCounted;

    template <typename T> class Ref;

    template <typename T> class WeakRef;

    template <typename T, typename... Args> Ref<T> CreateRef(Args&&... args);

    template <typename T, typename U> Ref<T> DynamicRefCast(const Ref<U>& reference) noexcept;

    namespace Detail
    {
        struct RefControlBlock
        {
            std::atomic<std::size_t> StrongCount{1};
            std::atomic<std::size_t> WeakCount{1};
            void* Instance = nullptr;
            void (*DestroyInstance)(void*) noexcept = nullptr;
        };

        inline void RetainStrong(RefControlBlock* control) noexcept
        {
            [[maybe_unused]] const auto previous = control->StrongCount.fetch_add(1, std::memory_order_relaxed);
            assert(previous > 0);
        }

        inline bool TryRetainStrong(RefControlBlock* control) noexcept
        {
            auto count = control->StrongCount.load(std::memory_order_acquire);
            while (count != 0)
            {
                if (control->StrongCount.compare_exchange_weak(count, count + 1, std::memory_order_acquire,
                                                               std::memory_order_relaxed))
                {
                    return true;
                }
            }
            return false;
        }

        inline void RetainWeak(RefControlBlock* control) noexcept
        {
            [[maybe_unused]] const auto previous = control->WeakCount.fetch_add(1, std::memory_order_relaxed);
            assert(previous > 0);
        }

        inline void ReleaseWeak(RefControlBlock* control) noexcept
        {
            const auto previous = control->WeakCount.fetch_sub(1, std::memory_order_release);
            assert(previous > 0);
            if (previous == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                delete control;
            }
        }

        inline void ReleaseStrong(RefControlBlock* control) noexcept
        {
            const auto previous = control->StrongCount.fetch_sub(1, std::memory_order_release);
            assert(previous > 0);
            if (previous == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                void* instance = control->Instance;
                control->Instance = nullptr;
                control->DestroyInstance(instance);
                ReleaseWeak(control);
            }
        }
    } // namespace Detail

    class RefCounted
    {
      public:
        RefCounted(const RefCounted&) = delete;
        RefCounted& operator=(const RefCounted&) = delete;
        RefCounted(RefCounted&&) = delete;
        RefCounted& operator=(RefCounted&&) = delete;

      protected:
        RefCounted() noexcept = default;
        virtual ~RefCounted()
        {
            assert(m_ControlBlock == nullptr || m_ControlBlock->StrongCount.load(std::memory_order_relaxed) == 0);
        }

      private:
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);

        Detail::RefControlBlock* m_ControlBlock = nullptr;
    };

    template <typename T> class Ref
    {
      public:
        using ElementType = T;

        constexpr Ref() noexcept = default;
        constexpr Ref(std::nullptr_t) noexcept {}

        Ref(const Ref& other) noexcept : m_Instance(other.m_Instance), m_ControlBlock(other.m_ControlBlock)
        {
            Retain();
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        Ref(const Ref<U>& other) noexcept : m_Instance(other.m_Instance), m_ControlBlock(other.m_ControlBlock)
        {
            Retain();
        }

        Ref(Ref&& other) noexcept
            : m_Instance(std::exchange(other.m_Instance, nullptr)),
              m_ControlBlock(std::exchange(other.m_ControlBlock, nullptr))
        {
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        Ref(Ref<U>&& other) noexcept
            : m_Instance(std::exchange(other.m_Instance, nullptr)),
              m_ControlBlock(std::exchange(other.m_ControlBlock, nullptr))
        {
        }

        ~Ref() { Release(); }

        Ref& operator=(const Ref& other) noexcept
        {
            if (this != std::addressof(other))
            {
                Ref(other).Swap(*this);
            }
            return *this;
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        Ref& operator=(const Ref<U>& other) noexcept
        {
            Ref(other).Swap(*this);
            return *this;
        }

        Ref& operator=(Ref&& other) noexcept
        {
            if (this != std::addressof(other))
            {
                Ref(std::move(other)).Swap(*this);
            }
            return *this;
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        Ref& operator=(Ref<U>&& other) noexcept
        {
            Ref(std::move(other)).Swap(*this);
            return *this;
        }

        Ref& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        [[nodiscard]] T* Get() const noexcept { return m_Instance; }
        [[nodiscard]] std::size_t UseCount() const noexcept
        {
            return m_ControlBlock ? m_ControlBlock->StrongCount.load(std::memory_order_acquire) : 0;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_Instance != nullptr; }

        T& operator*() const noexcept
        {
            assert(m_Instance != nullptr);
            return *m_Instance;
        }

        T* operator->() const noexcept
        {
            assert(m_Instance != nullptr);
            return m_Instance;
        }

        void Reset() noexcept { Ref{}.Swap(*this); }

        void Swap(Ref& other) noexcept
        {
            std::swap(m_Instance, other.m_Instance);
            std::swap(m_ControlBlock, other.m_ControlBlock);
        }

        template <typename U> [[nodiscard]] bool operator==(const Ref<U>& other) const noexcept
        {
            return m_Instance == other.Get();
        }

        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return m_Instance == nullptr; }

      private:
        struct AdoptStrongTag
        {
        };

        template <typename U> friend class Ref;
        template <typename U> friend class WeakRef;
        template <typename U, typename... Args> friend Ref<U> CreateRef(Args&&... args);
        template <typename U, typename V> friend Ref<U> DynamicRefCast(const Ref<V>& reference) noexcept;

        Ref(T* instance, Detail::RefControlBlock* controlBlock, AdoptStrongTag) noexcept
            : m_Instance(instance), m_ControlBlock(controlBlock)
        {
        }

        void Retain() noexcept
        {
            if (m_ControlBlock)
            {
                Detail::RetainStrong(m_ControlBlock);
            }
        }

        void Release() noexcept
        {
            if (m_ControlBlock)
            {
                Detail::ReleaseStrong(m_ControlBlock);
            }
        }

        T* m_Instance = nullptr;
        Detail::RefControlBlock* m_ControlBlock = nullptr;
    };

    template <typename T> class WeakRef
    {
      public:
        using ElementType = T;

        constexpr WeakRef() noexcept = default;
        constexpr WeakRef(std::nullptr_t) noexcept {}

        WeakRef(const Ref<T>& reference) noexcept
            : m_Instance(reference.m_Instance), m_ControlBlock(reference.m_ControlBlock)
        {
            Retain();
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        WeakRef(const Ref<U>& reference) noexcept
            : m_Instance(reference.m_Instance), m_ControlBlock(reference.m_ControlBlock)
        {
            Retain();
        }

        WeakRef(const WeakRef& other) noexcept : m_Instance(other.m_Instance), m_ControlBlock(other.m_ControlBlock)
        {
            Retain();
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        WeakRef(const WeakRef<U>& other) noexcept : m_Instance(other.m_Instance), m_ControlBlock(other.m_ControlBlock)
        {
            Retain();
        }

        WeakRef(WeakRef&& other) noexcept
            : m_Instance(std::exchange(other.m_Instance, nullptr)),
              m_ControlBlock(std::exchange(other.m_ControlBlock, nullptr))
        {
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        WeakRef(WeakRef<U>&& other) noexcept
            : m_Instance(std::exchange(other.m_Instance, nullptr)),
              m_ControlBlock(std::exchange(other.m_ControlBlock, nullptr))
        {
        }

        ~WeakRef() { Release(); }

        WeakRef& operator=(const WeakRef& other) noexcept
        {
            if (this != std::addressof(other))
            {
                WeakRef(other).Swap(*this);
            }
            return *this;
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        WeakRef& operator=(const WeakRef<U>& other) noexcept
        {
            WeakRef(other).Swap(*this);
            return *this;
        }

        WeakRef& operator=(WeakRef&& other) noexcept
        {
            if (this != std::addressof(other))
            {
                WeakRef(std::move(other)).Swap(*this);
            }
            return *this;
        }

        template <typename U>
            requires(!std::same_as<T, U> && std::convertible_to<U*, T*>)
        WeakRef& operator=(WeakRef<U>&& other) noexcept
        {
            WeakRef(std::move(other)).Swap(*this);
            return *this;
        }

        WeakRef& operator=(const Ref<T>& reference) noexcept
        {
            WeakRef(reference).Swap(*this);
            return *this;
        }

        template <typename U>
            requires std::convertible_to<U*, T*>
        WeakRef& operator=(const Ref<U>& reference) noexcept
        {
            WeakRef(reference).Swap(*this);
            return *this;
        }

        WeakRef& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        [[nodiscard]] Ref<T> Lock() const noexcept
        {
            if (!m_ControlBlock || !Detail::TryRetainStrong(m_ControlBlock))
            {
                return {};
            }
            return Ref<T>(m_Instance, m_ControlBlock, typename Ref<T>::AdoptStrongTag{});
        }

        [[nodiscard]] bool Expired() const noexcept { return UseCount() == 0; }
        [[nodiscard]] std::size_t UseCount() const noexcept
        {
            return m_ControlBlock ? m_ControlBlock->StrongCount.load(std::memory_order_acquire) : 0;
        }

        void Reset() noexcept { WeakRef{}.Swap(*this); }

        void Swap(WeakRef& other) noexcept
        {
            std::swap(m_Instance, other.m_Instance);
            std::swap(m_ControlBlock, other.m_ControlBlock);
        }

      private:
        template <typename U> friend class WeakRef;

        void Retain() noexcept
        {
            if (m_ControlBlock)
            {
                Detail::RetainWeak(m_ControlBlock);
            }
        }

        void Release() noexcept
        {
            if (m_ControlBlock)
            {
                Detail::ReleaseWeak(m_ControlBlock);
            }
        }

        T* m_Instance = nullptr;
        Detail::RefControlBlock* m_ControlBlock = nullptr;
    };

    template <typename T, typename... Args> Ref<T> CreateRef(Args&&... args)
    {
        static_assert(std::derived_from<T, RefCounted>, "CreateRef<T> requires public RefCounted inheritance.");
        static_assert(!std::is_array_v<T>, "CreateRef<T> does not support array types.");

        // Construct directly in the befriended factory so ref-counted types can keep constructors private.
        auto instance = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        auto controlBlock = std::make_unique<Detail::RefControlBlock>();
        controlBlock->Instance = instance.get();
        controlBlock->DestroyInstance = [](void* value) noexcept { delete static_cast<T*>(value); };

        auto* refCounted = static_cast<RefCounted*>(instance.get());
        assert(refCounted->m_ControlBlock == nullptr);
        refCounted->m_ControlBlock = controlBlock.get();

        T* releasedInstance = instance.release();
        Detail::RefControlBlock* releasedControlBlock = controlBlock.release();
        return Ref<T>(releasedInstance, releasedControlBlock, typename Ref<T>::AdoptStrongTag{});
    }

    template <typename T, typename U> Ref<T> DynamicRefCast(const Ref<U>& reference) noexcept
    {
        static_assert(std::is_polymorphic_v<std::remove_const_t<U>>, "DynamicRefCast requires a polymorphic source.");
        if (!reference.m_Instance)
        {
            return {};
        }

        auto* instance = dynamic_cast<T*>(reference.m_Instance);
        if (!instance)
        {
            return {};
        }

        Detail::RetainStrong(reference.m_ControlBlock);
        return Ref<T>(instance, reference.m_ControlBlock, typename Ref<T>::AdoptStrongTag{});
    }

    template <typename T> void swap(Ref<T>& left, Ref<T>& right) noexcept { left.Swap(right); }

    template <typename T> void swap(WeakRef<T>& left, WeakRef<T>& right) noexcept { left.Swap(right); }
} // namespace Keire
