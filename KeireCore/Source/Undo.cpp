#include "Keire/Undo.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        class FunctionalUndoCommand final : public UndoCommand
        {
          public:
            FunctionalUndoCommand(std::string name, UndoOperation redo, UndoOperation undo,
                                  const std::size_t estimatedBytes, UndoAvailability available)
                : m_Name(std::move(name)), m_Redo(std::move(redo)), m_Undo(std::move(undo)),
                  m_EstimatedBytes(std::max<std::size_t>(estimatedBytes, 1)), m_Available(std::move(available))
            {
                if (m_Name.empty() || !m_Redo || !m_Undo)
                    throw std::invalid_argument("Undo commands require a name and both operations.");
            }

            [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_EstimatedBytes; }
            [[nodiscard]] bool Available() const noexcept override
            {
                if (!m_Available)
                    return true;
                try
                {
                    return m_Available();
                }
                catch (...)
                {
                    return false;
                }
            }
            void Redo() override { m_Redo(); }
            void Undo() override { m_Undo(); }

          private:
            std::string m_Name;
            UndoOperation m_Redo;
            UndoOperation m_Undo;
            std::size_t m_EstimatedBytes = 1;
            UndoAvailability m_Available;
        };

        class CompositeUndoCommand final : public UndoCommand
        {
          public:
            CompositeUndoCommand(std::string name, std::vector<std::unique_ptr<UndoCommand>> commands)
                : m_Name(std::move(name)), m_Commands(std::move(commands))
            {
                if (m_Name.empty() || m_Commands.empty())
                    throw std::invalid_argument("Undo transactions require a name and at least one command.");
                for (const auto& command : m_Commands)
                    m_EstimatedBytes += command->EstimatedBytes();
            }

            [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_EstimatedBytes; }
            [[nodiscard]] bool Available() const noexcept override
            {
                return std::ranges::all_of(m_Commands, [](const auto& command) { return command->Available(); });
            }

            void Redo() override
            {
                std::size_t applied = 0;
                try
                {
                    for (; applied < m_Commands.size(); ++applied)
                        m_Commands[applied]->Redo();
                }
                catch (...)
                {
                    const auto original = std::current_exception();
                    while (applied > 0)
                    {
                        --applied;
                        try
                        {
                            m_Commands[applied]->Undo();
                        }
                        catch (...)
                        {
                        }
                    }
                    std::rethrow_exception(original);
                }
            }

            void Undo() override
            {
                std::size_t undone = 0;
                try
                {
                    for (auto iterator = m_Commands.rbegin(); iterator != m_Commands.rend(); ++iterator)
                    {
                        (*iterator)->Undo();
                        ++undone;
                    }
                }
                catch (...)
                {
                    const auto original = std::current_exception();
                    const auto firstUndone = m_Commands.size() - undone;
                    for (std::size_t index = firstUndone; index < m_Commands.size(); ++index)
                    {
                        try
                        {
                            m_Commands[index]->Redo();
                        }
                        catch (...)
                        {
                        }
                    }
                    std::rethrow_exception(original);
                }
            }

          private:
            std::string m_Name;
            std::vector<std::unique_ptr<UndoCommand>> m_Commands;
            std::size_t m_EstimatedBytes = 0;
        };

        [[nodiscard]] std::uint64_t NextContextId() noexcept
        {
            static std::atomic<std::uint64_t> next{1};
            return next.fetch_add(1, std::memory_order_relaxed);
        }
    } // namespace

    UndoCommand::~UndoCommand() = default;

    std::unique_ptr<UndoCommand> CreateUndoCommand(std::string name, UndoOperation redo, UndoOperation undo,
                                                   const std::size_t estimatedBytes, UndoAvailability available)
    {
        return std::make_unique<FunctionalUndoCommand>(std::move(name), std::move(redo), std::move(undo),
                                                       estimatedBytes, std::move(available));
    }

    class UndoContext::Impl final
    {
      public:
        struct TransactionState final
        {
            std::uint64_t Token = 0;
            std::string Name;
            std::vector<std::unique_ptr<UndoCommand>> Commands;
        };

        Impl(UndoContextSpecification specification, const UndoSpecification& defaults)
            : Id(NextContextId()), Name(std::move(specification.Name)),
              MaximumCommands(specification.MaximumCommands == 0 ? defaults.DefaultMaximumCommands
                                                                 : specification.MaximumCommands),
              MaximumBytes(specification.MaximumBytes == 0 ? defaults.DefaultMaximumBytes : specification.MaximumBytes),
              MaximumTransactionDepth(defaults.MaximumTransactionDepth), OwnerThread(std::this_thread::get_id())
        {
            if (Name.empty() || MaximumCommands == 0 || MaximumBytes == 0)
                throw std::invalid_argument("Undo contexts require a name and non-zero history limits.");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string(operation) + " must run on the undo owner thread.");
            if (!Open)
                throw std::logic_error(std::string(operation) + " requires an open undo context.");
        }

        void Push(std::unique_ptr<UndoCommand> command)
        {
            if (!command)
                throw std::invalid_argument("Undo history cannot record a null command.");
            if (!Transactions.empty())
            {
                Transactions.back().Commands.push_back(std::move(command));
                return;
            }

            if (!UndoStack.empty() && UndoStack.back()->TryMerge(*command))
            {
                RecalculateBytes();
                RedoStack.clear();
                Trim();
                return;
            }
            UndoBytes += command->EstimatedBytes();
            UndoStack.push_back(std::move(command));
            RedoStack.clear();
            RedoBytes = 0;
            Trim();
        }

        void Trim()
        {
            while (!UndoStack.empty() && (UndoStack.size() > MaximumCommands || UndoBytes > MaximumBytes))
            {
                UndoBytes -= UndoStack.front()->EstimatedBytes();
                UndoStack.erase(UndoStack.begin());
            }
        }

        void RecalculateBytes() noexcept
        {
            UndoBytes = 0;
            RedoBytes = 0;
            for (const auto& command : UndoStack)
                UndoBytes += command->EstimatedBytes();
            for (const auto& command : RedoStack)
                RedoBytes += command->EstimatedBytes();
        }

        void Rollback(std::vector<std::unique_ptr<UndoCommand>>& commands)
        {
            std::exception_ptr failure;
            for (auto iterator = commands.rbegin(); iterator != commands.rend(); ++iterator)
            {
                try
                {
                    (*iterator)->Undo();
                }
                catch (...)
                {
                    if (!failure)
                        failure = std::current_exception();
                }
            }
            if (failure)
                std::rethrow_exception(failure);
        }

        UndoContextId Id;
        std::string Name;
        std::size_t MaximumCommands = 0;
        std::size_t MaximumBytes = 0;
        std::size_t MaximumTransactionDepth = 0;
        std::thread::id OwnerThread;
        std::vector<std::unique_ptr<UndoCommand>> UndoStack;
        std::vector<std::unique_ptr<UndoCommand>> RedoStack;
        std::vector<TransactionState> Transactions;
        WeakRef<UndoContext> Self;
        std::size_t UndoBytes = 0;
        std::size_t RedoBytes = 0;
        std::uint64_t NextTransaction = 1;
        bool Open = true;
    };

    class UndoTransaction::Impl final
    {
      public:
        Impl(Ref<UndoContext> context, const std::uint64_t token) : Context(std::move(context)), Token(token) {}
        ~Impl() { CancelNoexcept(); }

        void Finish(const bool commit)
        {
            if (!Active)
                throw std::logic_error("Undo transaction is no longer active.");
            auto context = Context;
            context->m_Impl->RequireOwner(commit ? "Commit" : "Cancel");
            if (context->m_Impl->Transactions.empty() || context->m_Impl->Transactions.back().Token != Token)
                throw std::logic_error("Undo transactions must finish in last-in, first-out order.");

            auto state = std::move(context->m_Impl->Transactions.back());
            context->m_Impl->Transactions.pop_back();
            if (commit)
            {
                if (!state.Commands.empty())
                    context->m_Impl->Push(
                        std::make_unique<CompositeUndoCommand>(std::move(state.Name), std::move(state.Commands)));
            }
            else
                context->m_Impl->Rollback(state.Commands);
            Active = false;
            Context.Reset();
        }

        void CancelNoexcept() noexcept
        {
            if (!Active || !Context)
                return;
            try
            {
                Finish(false);
            }
            catch (...)
            {
                Active = false;
                Context.Reset();
            }
        }

        Ref<UndoContext> Context;
        std::uint64_t Token = 0;
        bool Active = true;
    };

    UndoTransaction::UndoTransaction(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    UndoTransaction::UndoTransaction(UndoTransaction&& other) noexcept = default;
    UndoTransaction& UndoTransaction::operator=(UndoTransaction&& other) noexcept = default;
    UndoTransaction::~UndoTransaction()
    {
        if (m_Impl)
            m_Impl->CancelNoexcept();
    }
    void UndoTransaction::Commit() { m_Impl->Finish(true); }
    void UndoTransaction::Cancel() { m_Impl->Finish(false); }
    bool UndoTransaction::Active() const noexcept { return m_Impl && m_Impl->Active; }

    UndoContext::UndoContext(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    UndoContext::~UndoContext() { Close(); }
    UndoContextId UndoContext::Id() const noexcept { return m_Impl->Id; }
    std::string_view UndoContext::Name() const noexcept { return m_Impl->Name; }
    bool UndoContext::IsOpen() const noexcept { return m_Impl->Open; }
    bool UndoContext::CanUndo() const noexcept
    {
        return m_Impl->Open && m_Impl->Transactions.empty() && !m_Impl->UndoStack.empty() &&
               m_Impl->UndoStack.back()->Available();
    }
    bool UndoContext::CanRedo() const noexcept
    {
        return m_Impl->Open && m_Impl->Transactions.empty() && !m_Impl->RedoStack.empty() &&
               m_Impl->RedoStack.back()->Available();
    }
    std::string UndoContext::UndoName() const
    {
        return CanUndo() ? std::string(m_Impl->UndoStack.back()->Name()) : std::string{};
    }
    std::string UndoContext::RedoName() const
    {
        return CanRedo() ? std::string(m_Impl->RedoStack.back()->Name()) : std::string{};
    }
    std::size_t UndoContext::UndoCount() const noexcept { return m_Impl->UndoStack.size(); }
    std::size_t UndoContext::RedoCount() const noexcept { return m_Impl->RedoStack.size(); }
    std::size_t UndoContext::EstimatedBytes() const noexcept { return m_Impl->UndoBytes + m_Impl->RedoBytes; }

    void UndoContext::Execute(std::unique_ptr<UndoCommand> command)
    {
        m_Impl->RequireOwner("Execute");
        if (!command)
            throw std::invalid_argument("UndoContext::Execute requires a command.");
        if (!command->Available())
            throw std::logic_error("UndoContext::Execute command target is unavailable.");
        command->Redo();
        m_Impl->Push(std::move(command));
    }

    void UndoContext::RecordApplied(std::unique_ptr<UndoCommand> command)
    {
        m_Impl->RequireOwner("RecordApplied");
        if (!command)
            throw std::invalid_argument("UndoContext::RecordApplied requires a command.");
        if (!command->Available())
            throw std::logic_error("UndoContext::RecordApplied command target is unavailable.");
        m_Impl->Push(std::move(command));
    }

    std::unique_ptr<UndoTransaction> UndoContext::BeginTransaction(std::string name)
    {
        m_Impl->RequireOwner("BeginTransaction");
        if (name.empty())
            throw std::invalid_argument("Undo transactions require a name.");
        if (m_Impl->Transactions.size() >= m_Impl->MaximumTransactionDepth)
            throw std::length_error("Undo transaction nesting limit exceeded.");
        const auto token = m_Impl->NextTransaction++;
        m_Impl->Transactions.push_back({token, std::move(name), {}});
        try
        {
            auto self = m_Impl->Self.Lock();
            if (!self)
                throw std::logic_error("Undo context ownership is unavailable.");
            return std::unique_ptr<UndoTransaction>(
                new UndoTransaction(std::make_unique<UndoTransaction::Impl>(std::move(self), token)));
        }
        catch (...)
        {
            m_Impl->Transactions.pop_back();
            throw;
        }
    }

    bool UndoContext::Undo()
    {
        m_Impl->RequireOwner("Undo");
        if (!CanUndo())
            return false;
        auto& command = m_Impl->UndoStack.back();
        command->Undo();
        m_Impl->UndoBytes -= command->EstimatedBytes();
        m_Impl->RedoBytes += command->EstimatedBytes();
        m_Impl->RedoStack.push_back(std::move(command));
        m_Impl->UndoStack.pop_back();
        return true;
    }

    bool UndoContext::Redo()
    {
        m_Impl->RequireOwner("Redo");
        if (!CanRedo())
            return false;
        auto& command = m_Impl->RedoStack.back();
        command->Redo();
        m_Impl->RedoBytes -= command->EstimatedBytes();
        m_Impl->UndoBytes += command->EstimatedBytes();
        m_Impl->UndoStack.push_back(std::move(command));
        m_Impl->RedoStack.pop_back();
        m_Impl->Trim();
        return true;
    }

    void UndoContext::Clear()
    {
        m_Impl->RequireOwner("Clear");
        if (!m_Impl->Transactions.empty())
            throw std::logic_error("Undo history cannot be cleared during a transaction.");
        m_Impl->UndoStack.clear();
        m_Impl->RedoStack.clear();
        m_Impl->UndoBytes = 0;
        m_Impl->RedoBytes = 0;
    }

    void UndoContext::Close() noexcept
    {
        if (!m_Impl || !m_Impl->Open)
            return;
        if (std::this_thread::get_id() != m_Impl->OwnerThread)
            return;
        while (!m_Impl->Transactions.empty())
        {
            auto commands = std::move(m_Impl->Transactions.back().Commands);
            m_Impl->Transactions.pop_back();
            try
            {
                m_Impl->Rollback(commands);
            }
            catch (...)
            {
            }
        }
        m_Impl->UndoStack.clear();
        m_Impl->RedoStack.clear();
        m_Impl->UndoBytes = 0;
        m_Impl->RedoBytes = 0;
        m_Impl->Open = false;
    }

    class UndoService::Impl final
    {
      public:
        explicit Impl(UndoSpecification value)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.DefaultMaximumCommands == 0 || Specification.DefaultMaximumBytes == 0 ||
                Specification.MaximumContexts == 0 || Specification.MaximumTransactionDepth == 0 ||
                Specification.MaximumTransactionDepth > 64)
                throw std::invalid_argument("Undo service limits must be non-zero and bounded.");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string(operation) + " must run on the undo owner thread.");
            if (!Open)
                throw std::logic_error(std::string(operation) + " requires an open undo service.");
        }

        void Prune()
        {
            std::erase_if(Contexts, [](const WeakRef<UndoContext>& context) { return context.Expired(); });
        }

        UndoSpecification Specification;
        std::thread::id OwnerThread;
        std::vector<WeakRef<UndoContext>> Contexts;
        bool Open = true;
    };

    UndoService::UndoService(UndoSpecification specification) : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }
    UndoService::~UndoService() { Close(); }

    Ref<UndoContext> UndoService::CreateContext(UndoContextSpecification specification)
    {
        m_Impl->RequireOwner("CreateContext");
        m_Impl->Prune();
        if (m_Impl->Contexts.size() >= m_Impl->Specification.MaximumContexts)
            throw std::length_error("Undo service context limit exceeded.");
        auto context = CreateRef<UndoContext>(
            std::make_unique<UndoContext::Impl>(std::move(specification), m_Impl->Specification));
        context->m_Impl->Self = context;
        m_Impl->Contexts.emplace_back(context);
        return context;
    }

    bool UndoService::IsOpen() const noexcept { return m_Impl->Open; }
    std::size_t UndoService::ContextCount() const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(m_Impl->Contexts, [](const WeakRef<UndoContext>& context)
                                                              { return !context.Expired(); }));
    }

    void UndoService::Close() noexcept
    {
        if (!m_Impl || !m_Impl->Open)
            return;
        if (std::this_thread::get_id() != m_Impl->OwnerThread)
            return;
        for (const auto& weak : m_Impl->Contexts)
            if (const auto context = weak.Lock())
                context->Close();
        m_Impl->Contexts.clear();
        m_Impl->Open = false;
    }
} // namespace Keire
