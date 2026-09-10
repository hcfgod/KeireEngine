#include "Keire/Undo.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

TEST_CASE("Undo merged edits release the byte count of discarded redo commands")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Merged Branch"});
    int value = 0;
    int other = 0;
    context->Execute(std::make_unique<MergeValueCommand>(value, 0, 1));
    const auto retainedBytes = context->EstimatedBytes();
    context->Execute(std::make_unique<MergeValueCommand>(other, 0, 1));
    REQUIRE(context->Undo());
    REQUIRE(context->RedoCount() == 1);

    SUBCASE("Execute") { context->Execute(std::make_unique<MergeValueCommand>(value, 1, 2)); }
    SUBCASE("RecordApplied")
    {
        value = 2;
        context->RecordApplied(std::make_unique<MergeValueCommand>(value, 1, 2));
    }

    CHECK(value == 2);
    CHECK(other == 0);
    CHECK(context->UndoCount() == 1);
    CHECK(context->RedoCount() == 0);
    CHECK(context->EstimatedBytes() == retainedBytes);
    REQUIRE(context->Undo());
    CHECK(value == 0);
    CHECK(context->EstimatedBytes() == retainedBytes);
    REQUIRE(context->Redo());
    CHECK(value == 2);
    CHECK(context->EstimatedBytes() == retainedBytes);
    context->Clear();
    CHECK(context->EstimatedBytes() == 0);
}

TEST_CASE("Undo services reuse closed context slots while callers retain references")
{
    Keire::UndoSpecification specification;
    specification.MaximumContexts = 1;
    auto service = Keire::CreateRef<Keire::UndoService>(specification);
    auto first = service->CreateContext({.Name = "First"});
    REQUIRE(service->ContextCount() == 1);
    CHECK_THROWS_AS((void)service->CreateContext({.Name = "Over Limit"}), std::length_error);
    first->Close();
    CHECK(service->ContextCount() == 0);

    std::vector<Keire::Ref<Keire::UndoContext>> retained{first};
    for (int index = 0; index < 3; ++index)
    {
        auto replacement = service->CreateContext({.Name = "Replacement"});
        CHECK(replacement->Id() != retained.back()->Id());
        CHECK(service->ContextCount() == 1);
        replacement->Close();
        CHECK(service->ContextCount() == 0);
        retained.push_back(replacement);
    }

    auto active = service->CreateContext({.Name = "Active"});
    service->Close();
    CHECK_FALSE(active->IsOpen());
    CHECK(service->ContextCount() == 0);
    for (const auto& context : retained)
    {
        CHECK_FALSE(context->IsOpen());
        CHECK_FALSE(context->CanUndo());
        CHECK_THROWS_AS((void)context->BeginTransaction("Closed"), std::logic_error);
    }
}

TEST_CASE("Undo canceled transactions become inactive even when rollback callbacks fail")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Rollback Failure"});
    std::vector<int> rollbackOrder;
    int value = 0;
    auto transaction = context->BeginTransaction("Cancel Failing Commands");
    context->Execute(Keire::CreateUndoCommand(
        "One", [&] { value += 1; },
        [&]
        {
            rollbackOrder.push_back(1);
            value -= 1;
        }));
    context->Execute(Keire::CreateUndoCommand(
        "Two", [&] { value += 2; },
        [&]
        {
            rollbackOrder.push_back(2);
            throw std::runtime_error("later rollback failure");
        }));
    context->Execute(Keire::CreateUndoCommand(
        "Four", [&] { value += 4; },
        [&]
        {
            rollbackOrder.push_back(3);
            throw std::runtime_error("first rollback failure");
        }));

    CHECK_THROWS_WITH_AS(transaction->Cancel(), "first rollback failure", std::runtime_error);
    CHECK_FALSE(transaction->Active());
    CHECK(rollbackOrder == std::vector<int>{3, 2, 1});
    CHECK(value == 6);
    CHECK(context->UndoCount() == 0);
    CHECK(context->RedoCount() == 0);
    CHECK_THROWS_WITH_AS(transaction->Cancel(), "Undo transaction is no longer active.", std::logic_error);
    CHECK_THROWS_WITH_AS(transaction->Commit(), "Undo transaction is no longer active.", std::logic_error);
    transaction.reset();
    CHECK(rollbackOrder == std::vector<int>{3, 2, 1});

    auto next = context->BeginTransaction("Next Transaction");
    context->Execute(Keire::CreateUndoCommand("Eight", [&] { value += 8; }, [&] { value -= 8; }));
    next->Cancel();
    CHECK(value == 6);
    CHECK_FALSE(next->Active());
}

TEST_CASE("Undo transaction finish rejection preserves active nested transactions")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Finish Rejection"});
    int value = 0;
    auto outer = context->BeginTransaction("Outer");
    context->Execute(Keire::CreateUndoCommand("One", [&] { value += 1; }, [&] { value -= 1; }));
    auto inner = context->BeginTransaction("Inner");
    context->Execute(Keire::CreateUndoCommand("Two", [&] { value += 2; }, [&] { value -= 2; }));
    CHECK_THROWS_AS(outer->Commit(), std::logic_error);
    CHECK_THROWS_AS(outer->Cancel(), std::logic_error);

    bool rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                inner->Cancel();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected);
    CHECK(outer->Active());
    CHECK(inner->Active());
    CHECK(value == 3);
    CHECK(context->UndoCount() == 0);
    CHECK(context->RedoCount() == 0);
    inner->Commit();
    outer->Commit();
    REQUIRE(context->Undo());
    CHECK(value == 0);
}

TEST_CASE("Undo transaction handles become inactive when their context closes")
{
    auto service = Keire::CreateRef<Keire::UndoService>();
    auto context = service->CreateContext({.Name = "Closed Transaction"});
    int value = 0;
    int rollbackCalls = 0;
    auto transaction = context->BeginTransaction("Pending");
    context->Execute(Keire::CreateUndoCommand(
        "One", [&] { value = 1; },
        [&]
        {
            value = 0;
            ++rollbackCalls;
        }));

    SUBCASE("Context closes") { context->Close(); }
    SUBCASE("Service closes") { service->Close(); }

    CHECK_FALSE(transaction->Active());
    CHECK(value == 0);
    CHECK(rollbackCalls == 1);
    CHECK(context->UndoCount() == 0);
    CHECK(context->EstimatedBytes() == 0);
    CHECK_THROWS_AS(transaction->Commit(), std::logic_error);
    CHECK_THROWS_AS(transaction->Cancel(), std::logic_error);
    transaction.reset();
    context->Close();
    service->Close();
    CHECK(rollbackCalls == 1);
}
