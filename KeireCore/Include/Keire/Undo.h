#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Keire
{
    struct UndoSpecification
    {
        std::size_t DefaultMaximumCommands = 256;
        std::size_t DefaultMaximumBytes = 64U * 1024U * 1024U;
        std::size_t MaximumContexts = 64;
        std::size_t MaximumTransactionDepth = 16;
    };

    struct UndoContextSpecification
    {
        std::string Name;
        std::size_t MaximumCommands = 0;
        std::size_t MaximumBytes = 0;
    };

    class KEIRE_API UndoContextId final
    {
      public:
        constexpr UndoContextId() noexcept = default;
        explicit constexpr UndoContextId(const std::uint64_t value) noexcept : m_Value(value) {}

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const UndoContextId&) const noexcept = default;

      private:
        std::uint64_t m_Value = 0;
    };

    class KEIRE_API UndoCommand
    {
      public:
        UndoCommand() = default;
        virtual ~UndoCommand();

        UndoCommand(const UndoCommand&) = delete;
        UndoCommand& operator=(const UndoCommand&) = delete;
        UndoCommand(UndoCommand&&) = delete;
        UndoCommand& operator=(UndoCommand&&) = delete;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;
        [[nodiscard]] virtual bool Available() const noexcept { return true; }
        virtual void Redo() = 0;
        virtual void Undo() = 0;
        [[nodiscard]] virtual bool TryMerge(const UndoCommand&) { return false; }
    };

    using UndoOperation = std::function<void()>;
    using UndoAvailability = std::function<bool()>;

    [[nodiscard]] KEIRE_API std::unique_ptr<UndoCommand> CreateUndoCommand(std::string name, UndoOperation redo,
                                                                           UndoOperation undo,
                                                                           std::size_t estimatedBytes = 1,
                                                                           UndoAvailability available = {});

    class UndoContext;

    class KEIRE_API UndoTransaction final
    {
      public:
        UndoTransaction(const UndoTransaction&) = delete;
        UndoTransaction& operator=(const UndoTransaction&) = delete;
        UndoTransaction(UndoTransaction&& other) noexcept;
        UndoTransaction& operator=(UndoTransaction&& other) noexcept;
        ~UndoTransaction();

        void Commit();
        void Cancel();
        [[nodiscard]] bool Active() const noexcept;

      private:
        friend class UndoContext;
        class Impl;

        explicit UndoTransaction(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API UndoContext final : public RefCounted
    {
      public:
        ~UndoContext() override;

        [[nodiscard]] UndoContextId Id() const noexcept;
        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] bool CanUndo() const noexcept;
        [[nodiscard]] bool CanRedo() const noexcept;
        [[nodiscard]] std::string UndoName() const;
        [[nodiscard]] std::string RedoName() const;
        [[nodiscard]] std::size_t UndoCount() const noexcept;
        [[nodiscard]] std::size_t RedoCount() const noexcept;
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept;

        void Execute(std::unique_ptr<UndoCommand> command);
        void RecordApplied(std::unique_ptr<UndoCommand> command);
        [[nodiscard]] std::unique_ptr<UndoTransaction> BeginTransaction(std::string name);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Clear();
        void Close() noexcept;

      private:
        friend class UndoService;
        friend class UndoTransaction::Impl;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        class Impl;

        explicit UndoContext(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API UndoService final : public RefCounted
    {
      public:
        explicit UndoService(UndoSpecification specification = {});
        ~UndoService() override;

        UndoService(const UndoService&) = delete;
        UndoService& operator=(const UndoService&) = delete;

        [[nodiscard]] Ref<UndoContext> CreateContext(UndoContextSpecification specification);
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] std::size_t ContextCount() const noexcept;
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
