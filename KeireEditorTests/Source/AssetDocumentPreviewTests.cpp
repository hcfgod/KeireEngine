#include "KeireClient/Editor/AssetDocumentHost.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <ostream>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using PreviewDocument = KeireEditor::AssetDocumentHost<int>;

    [[nodiscard]] PreviewDocument::Specification PreviewSpecification(std::vector<int>& previews)
    {
        return {.Validate =
                    [](const int value)
                {
                    if (value < 0)
                    {
                        throw std::invalid_argument("Negative preview.");
                    }
                },
                .Encode = [](const int) { return std::vector<std::byte>{std::byte{1}}; },
                .Preview = [&](Keire::AssetId, const int value) { previews.push_back(value); },
                .Persist = [](Keire::AssetId, std::span<const std::byte>) {}};
    }
} // namespace

TEST_CASE("Asset document transient preview preserves authored state revision and history")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Audition history"});
    std::vector<int> previews;
    int persisted = 0;
    auto specification = PreviewSpecification(previews);
    specification.Encode = [&](const int value)
    {
        persisted = value;
        return std::vector<std::byte>{std::byte{1}};
    };
    PreviewDocument document(std::move(specification));
    document.Open(Keire::AssetId(1, 1), 10, 7, undo);
    document.PreviewTransient(25);
    CHECK(previews.back() == 25);
    CHECK(document.Draft() == 10);
    CHECK(document.Baseline() == 10);
    CHECK(document.Revision() == 7);
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(undo->CanUndo());
    CHECK(persisted == 0);

    REQUIRE(document.Edit("Authored change", 12));
    const auto historyBytes = undo->EstimatedBytes();
    document.PreviewTransient(30);
    CHECK(document.Dirty());
    CHECK(document.Draft() == 12);
    CHECK(undo->EstimatedBytes() == historyBytes);
    document.RestorePreview();
    CHECK(previews.back() == 12);
    document.PreviewTransient(30);
    REQUIRE(document.Undo());
    CHECK(previews.back() == 10);
    document.PreviewTransient(40);
    REQUIRE(document.Redo());
    CHECK(previews.back() == 12);
    document.PreviewTransient(50);
    document.Save();
    CHECK(persisted == 12);
    CHECK_FALSE(document.Dirty());
    document.RestorePreview();
    CHECK(previews.back() == 12);
    document.PreviewTransient(60);
    CHECK(document.Reload(20, 8) == KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(previews.back() == 20);
    CHECK(document.Revision() == 8);
    CHECK_FALSE(undo->CanUndo());
    document.Close();
    undoService->Close();
}

TEST_CASE("Asset document transient preview validates before callbacks and restores draft on failure")
{
    std::vector<int> previews;
    auto specification = PreviewSpecification(previews);
    specification.Preview = [&](Keire::AssetId, const int value)
    {
        previews.push_back(value);
        if (value == 99)
        {
            throw std::runtime_error("Audition failed.");
        }
    };
    PreviewDocument document(std::move(specification));
    CHECK_THROWS_AS(document.PreviewTransient(1), std::logic_error);
    CHECK_THROWS_AS(document.RestorePreview(), std::logic_error);
    document.Open(Keire::AssetId(1, 2), 10, 1);
    document.PreviewTransient(20);
    const auto callbackCount = previews.size();
    CHECK_THROWS_AS(document.PreviewTransient(-1), std::invalid_argument);
    CHECK(previews.size() == callbackCount);
    CHECK_THROWS_WITH(document.PreviewTransient(99), "Audition failed.");
    CHECK(previews == std::vector<int>{10, 20, 99, 10});
    CHECK(document.Draft() == 10);
    CHECK_FALSE(document.Dirty());
    CHECK(document.Diagnostic() == "Audition failed.");
    document.RestorePreview();
    CHECK(document.Diagnostic().empty());
}

TEST_CASE("Asset document transient preview preserves its original failure when restoration also fails")
{
    std::vector<int> previews;
    bool failRestore = false;
    auto specification = PreviewSpecification(previews);
    specification.Preview = [&](Keire::AssetId, const int value)
    {
        if (value == 99)
        {
            failRestore = true;
            throw std::runtime_error("Original audition failure.");
        }
        if (failRestore)
        {
            throw std::runtime_error("Secondary restoration failure.");
        }
    };
    PreviewDocument document(std::move(specification));
    document.Open(Keire::AssetId(1, 3), 10, 1);
    CHECK_THROWS_WITH(document.PreviewTransient(99), "Original audition failure.");
    CHECK(document.Diagnostic() == "Original audition failure.");
    CHECK(document.Draft() == 10);
    CHECK_FALSE(document.Dirty());
}
