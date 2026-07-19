#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
    class MergeValueCommand final : public Keire::UndoCommand
    {
      public:
        MergeValueCommand(int& target, const int before, const int after)
            : m_Target(target), m_Before(before), m_After(after)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return "Change Value"; }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(*this); }
        void Redo() override { m_Target = m_After; }
        void Undo() override { m_Target = m_Before; }
        [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
        {
            const auto* command = dynamic_cast<const MergeValueCommand*>(&newer);
            if (!command || &command->m_Target != &m_Target)
                return false;
            m_After = command->m_After;
            return true;
        }

      private:
        int& m_Target;
        int m_Before = 0;
        int m_After = 0;
    };
} // namespace

TEST_CASE("Undo contexts execute, merge, isolate, and invalidate redo history")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto first = service->CreateContext({.Name = "First"});
    auto second = service->CreateContext({.Name = "Second"});
    int value = 0;

    first->Execute(std::make_unique<MergeValueCommand>(value, 0, 1));
    first->Execute(std::make_unique<MergeValueCommand>(value, 1, 2));
    CHECK(value == 2);
    CHECK(first->UndoCount() == 1);
    CHECK(second->UndoCount() == 0);
    CHECK(first->UndoName() == "Change Value");

    CHECK(first->Undo());
    CHECK(value == 0);
    CHECK(first->CanRedo());
    CHECK(first->Redo());
    CHECK(value == 2);
    CHECK(first->Undo());

    first->Execute(Keire::CreateUndoCommand("Set Four", [&value] { value = 4; }, [&value] { value = 0; }));
    CHECK(value == 4);
    CHECK_FALSE(first->CanRedo());
}

TEST_CASE("Undo transactions collapse nested work and cancel in reverse order")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Transactions"});
    int value = 0;

    auto outer = context->BeginTransaction("Build Value");
    context->Execute(Keire::CreateUndoCommand("One", [&value] { value += 1; }, [&value] { value -= 1; }));
    auto inner = context->BeginTransaction("Inner");
    context->Execute(Keire::CreateUndoCommand("Two", [&value] { value += 2; }, [&value] { value -= 2; }));
    inner->Commit();
    context->Execute(Keire::CreateUndoCommand("Four", [&value] { value += 4; }, [&value] { value -= 4; }));
    outer->Commit();

    CHECK(value == 7);
    CHECK(context->UndoCount() == 1);
    CHECK(context->UndoName() == "Build Value");
    CHECK(context->Undo());
    CHECK(value == 0);
    CHECK(context->Redo());
    CHECK(value == 7);

    auto canceled = context->BeginTransaction("Canceled");
    context->Execute(Keire::CreateUndoCommand("Three", [&value] { value += 3; }, [&value] { value -= 3; }));
    canceled->Cancel();
    CHECK(value == 7);
    CHECK(context->UndoCount() == 1);
}

TEST_CASE("Undo failures preserve stack position and rejected threads leave state unchanged")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Failures"});
    int value = 0;
    context->Execute(
        Keire::CreateUndoCommand("Failure", [&value] { value = 1; }, [] { throw std::runtime_error("undo failed"); }));

    CHECK_THROWS_WITH_AS((void)context->Undo(), "undo failed", std::runtime_error);
    CHECK(value == 1);
    CHECK(context->UndoCount() == 1);
    CHECK(context->RedoCount() == 0);

    std::string diagnostic;
    std::thread worker(
        [&]
        {
            try
            {
                context->Clear();
            }
            catch (const std::exception& error)
            {
                diagnostic = error.what();
            }
        });
    worker.join();
    CHECK(diagnostic.find("owner thread") != std::string::npos);
    CHECK(context->UndoCount() == 1);
}

TEST_CASE("Undo history obeys count and byte limits and becomes inert after shutdown")
{
    Keire::UndoSpecification specification;
    specification.DefaultMaximumCommands = 2;
    specification.DefaultMaximumBytes = 16;
    auto service = Keire::CreateRef<Keire::UndoService>(specification);
    auto context = service->CreateContext({.Name = "Bounded"});
    int value = 0;
    for (int index = 1; index <= 3; ++index)
    {
        const int before = value;
        context->Execute(Keire::CreateUndoCommand(
            "Set", [&value, index] { value = index; }, [&value, before] { value = before; }, 8));
    }
    CHECK(context->UndoCount() == 2);
    CHECK(context->EstimatedBytes() == 16);

    service->Close();
    CHECK_FALSE(service->IsOpen());
    CHECK_FALSE(context->IsOpen());
    CHECK_FALSE(context->CanUndo());
    CHECK_THROWS_AS(context->Execute(Keire::CreateUndoCommand("Nope", [] {}, [] {})), std::logic_error);
}
