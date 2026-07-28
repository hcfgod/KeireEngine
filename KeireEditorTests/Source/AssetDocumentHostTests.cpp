#include "KeireClient/Editor/AssetDocumentHost.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct TestDefinition final
    {
        int Value = 0;
        std::string Label;

        bool operator==(const TestDefinition&) const = default;
    };

    [[nodiscard]] Keire::AssetId TestAsset(const std::uint64_t value)
    {
        return Keire::AssetId(0x444f43554d454e54ULL, value);
    }
} // namespace

TEST_CASE("Asset document host validates edits and coordinates undo save discard and reload")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Asset"});
    TestDefinition preview;
    std::vector<std::byte> persisted;
    std::size_t previewCancels = 0;
    KeireEditor::AssetDocumentHost<TestDefinition> document(
        {.Validate =
             [](const TestDefinition& definition)
         {
             if (definition.Value < 0)
                 throw std::invalid_argument("Value cannot be negative.");
         },
         .Encode =
             [](const TestDefinition& definition)
         {
             const auto text = std::to_string(definition.Value) + ":" + definition.Label;
             const auto bytes = std::as_bytes(std::span(text));
             return std::vector<std::byte>(bytes.begin(), bytes.end());
         },
         .Preview = [&](const Keire::AssetId, const TestDefinition& definition) { preview = definition; },
         .CancelPreview = [&](const Keire::AssetId) { ++previewCancels; },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte> bytes)
         { persisted.assign(bytes.begin(), bytes.end()); }});

    document.Open(TestAsset(1), {1, "baseline"}, 1, undo);
    CHECK_FALSE(document.Dirty());
    CHECK(preview == TestDefinition{1, "baseline"});

    CHECK(document.Edit("Change value", {2, "edited"}));
    CHECK(document.Dirty());
    CHECK(preview == TestDefinition{2, "edited"});
    CHECK(document.Undo());
    CHECK(document.Draft() == TestDefinition{1, "baseline"});
    CHECK(document.Redo());
    CHECK(document.Draft() == TestDefinition{2, "edited"});

    CHECK(document.Reload({3, "external"}, 2) == KeireEditor::AssetDocumentReloadResult::LocalChanges);
    document.Save();
    CHECK(std::string(reinterpret_cast<const char*>(persisted.data()), persisted.size()) == "2:edited");
    CHECK_FALSE(document.Dirty());
    CHECK(document.Reload({3, "external"}, 2) == KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(document.Draft() == TestDefinition{3, "external"});

    CHECK(document.Edit("Local edit", {4, "local"}));
    document.AcknowledgeRevision(3);
    CHECK(document.Revision() == 3);
    document.Discard();
    CHECK(document.Draft() == TestDefinition{3, "external"});
    CHECK_FALSE(document.Dirty());

    CHECK_THROWS_AS(document.Edit("Invalid", {-1, "invalid"}), std::invalid_argument);
    CHECK(document.Draft() == TestDefinition{3, "external"});
    document.Close();
    CHECK(previewCancels == 1);
    undoService->Close();
}

TEST_CASE("New asset document discard cancels the unsaved document")
{
    KeireEditor::AssetDocumentHost<TestDefinition> document(
        {.Validate = [](const TestDefinition&) {},
         .Encode = [](const TestDefinition&) { return std::vector<std::byte>{std::byte{1}}; },
         .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});

    document.Create(TestAsset(2), {5, "new"});
    CHECK(document.Dirty());
    document.Discard();
    CHECK_FALSE(document.IsOpen());
}

TEST_CASE("Asset document undo history becomes unavailable after close and destruction")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Retained asset history"});
    const auto specification = KeireEditor::AssetDocumentHost<TestDefinition>::Specification{
        .Validate = [](const TestDefinition&) {},
        .Encode = [](const TestDefinition&) { return std::vector<std::byte>{std::byte{1}}; },
        .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}};

    {
        KeireEditor::AssetDocumentHost<TestDefinition> document(specification);
        document.Open(TestAsset(3), {1, "first"}, 1, undo);
        REQUIRE(document.Edit("Edit before close", {2, "closed"}));
        REQUIRE(undo->CanUndo());

        document.Close();
        CHECK_FALSE(undo->CanUndo());
        CHECK_FALSE(undo->Undo());

        document.Open(TestAsset(4), {3, "second"}, 1, undo);
        REQUIRE(document.Edit("Edit before destruction", {4, "destroyed"}));
        REQUIRE(undo->CanUndo());
    }

    CHECK_FALSE(undo->CanUndo());
    CHECK_FALSE(undo->Undo());
    CHECK_FALSE(undo->CanRedo());
    undoService->Close();
}
