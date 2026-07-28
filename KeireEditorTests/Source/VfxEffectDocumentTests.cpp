#include "KeireClient/Editor/VfxEffectDocument.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId Id(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x564658444f435445ULL, value);
    }

    [[nodiscard]] Keire::VfxEffectDefinition Definition()
    {
        auto result = Keire::VfxEffectAsset::DefaultDefinition();
        result.EmitterId = Id(1);
        result.Name = "Sparks";
        for (std::size_t index = 0; index < result.Modules.size(); ++index)
            result.Modules[index].Id = Id(index + 2);
        return result;
    }

    [[nodiscard]] Keire::AssetId RendererId(const Keire::VfxEffectDefinition& definition)
    {
        const auto found =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxRendererModule>(module.Payload); });
        if (found == definition.Modules.end())
            throw std::logic_error("Expected renderer module was unavailable.");
        return found->Id;
    }
} // namespace

TEST_CASE("VFX effect document coordinates stable modules preview undo save discard and reload")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Effect"});
    Keire::VfxEffectDefinition preview;
    std::vector<std::byte> persisted;
    std::size_t previewCount = 0;
    std::size_t stopCount = 0;
    std::size_t persistCount = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview =
             [&](const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
         {
             CHECK(asset == Id(100));
             preview = definition;
             ++previewCount;
         },
         .StopPreview =
             [&](const Keire::AssetId asset)
         {
             CHECK(asset == Id(100));
             ++stopCount;
         },
         .Persist =
             [&](const Keire::AssetId asset, const std::span<const std::byte> bytes)
         {
             CHECK(asset == Id(100));
             persisted.assign(bytes.begin(), bytes.end());
             ++persistCount;
         }});

    const auto authored = Definition();
    document.Open(Id(100), Keire::VfxEffectAsset::Encode(authored), 1, undo);
    CHECK_FALSE(document.Dirty());
    CHECK(preview == authored);
    CHECK(previewCount == 1);
    CHECK(persistCount == 0);

    const Keire::VfxModuleDefinition burst{
        .Id = Id(20),
        .Payload = Keire::VfxBurstModule{.Time = 0.1F, .Count = 4, .Cycles = 1, .Interval = 0.1F},
    };
    CHECK(document.AddModule(burst));
    CHECK(document.Definition().Modules.back().Id == Id(20));
    CHECK(document.EditModule(Id(20), [](Keire::VfxModuleDefinition& module)
                              { std::get<Keire::VfxBurstModule>(module.Payload).Count = 8; }));
    CHECK(std::get<Keire::VfxBurstModule>(document.Definition().Modules.back().Payload).Count == 8);
    CHECK(document.MoveModule(Id(20), 0));
    CHECK(document.Definition().Modules.front().Id == Id(20));
    CHECK(document.Definition().EmitterId == Id(1));
    CHECK(preview == document.Definition());

    CHECK(document.Undo());
    CHECK(document.Definition().Modules.back().Id == Id(20));
    CHECK(document.Redo());
    CHECK(document.Definition().Modules.front().Id == Id(20));
    CHECK(document.RemoveModule(Id(20)));
    CHECK(std::ranges::find(document.Definition().Modules, Id(20), &Keire::VfxModuleDefinition::Id) ==
          document.Definition().Modules.end());
    CHECK(document.Undo());
    CHECK(document.Definition().Modules.front().Id == Id(20));

    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(persistCount == 1);
    CHECK(Keire::VfxEffectAsset::Decode(persisted)->Definition() == document.Definition());
    const auto previewCountAfterSave = previewCount;
    CHECK(document.Reload(document.Definition(), 2) == KeireEditor::AssetDocumentReloadResult::Unchanged);
    CHECK(document.Revision() == 2);
    CHECK(previewCount == previewCountAfterSave);

    auto external = document.Definition();
    external.Name = "External Sparks";
    CHECK(document.Reload(external, 3) == KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(document.Definition().Name == "External Sparks");
    CHECK_FALSE(document.Dirty());

    CHECK(document.Edit("Rename VFX effect",
                        [](Keire::VfxEffectDefinition& definition) { definition.Name = "Local Sparks"; }));
    auto newerExternal = external;
    newerExternal.Name = "Newer External Sparks";
    CHECK(document.Reload(newerExternal, 4) == KeireEditor::AssetDocumentReloadResult::LocalChanges);
    document.Discard();
    CHECK(document.Definition().Name == "External Sparks");
    CHECK_FALSE(document.Dirty());

    document.Close();
    CHECK(stopCount == 1);
    CHECK_FALSE(document.IsOpen());
    undoService->Close();
}

TEST_CASE("VFX effect document rejects invalid identity module and preview edits transactionally")
{
    Keire::VfxEffectDefinition preview;
    std::size_t previews = 0;
    std::size_t stops = 0;
    KeireEditor::VfxEffectDocument document({.Preview =
                                                 [&](const Keire::AssetId, const Keire::VfxEffectDefinition& definition)
                                             {
                                                 ++previews;
                                                 if (definition.Name == "Reject Preview")
                                                     throw std::runtime_error("Preview rejected.");
                                                 preview = definition;
                                             },
                                             .StopPreview = [&](const Keire::AssetId) { ++stops; },
                                             .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    const auto authored = Definition();
    document.Open(Id(101), authored, 1);
    const auto baselinePreviews = previews;

    auto duplicate = authored.Modules.front();
    duplicate.Payload = Keire::VfxBurstModule{.Time = 0.1F, .Count = 1};
    CHECK_THROWS_AS((void)document.AddModule(duplicate), std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    const auto module = authored.Modules.front().Id;
    CHECK_THROWS_AS(
        (void)document.EditModule(module, [](Keire::VfxModuleDefinition& definition) { definition.Id = Id(99); }),
        std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    CHECK_THROWS_AS((void)document.RemoveModule(RendererId(authored)), std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);
    CHECK_THROWS_AS((void)document.MoveModule(Id(99), 0), std::invalid_argument);
    CHECK_THROWS_AS((void)document.MoveModule(module, authored.Modules.size()), std::invalid_argument);

    const std::string malformed = "{\"schemaVersion\":99}";
    CHECK_THROWS_AS(document.Open(Id(102), std::as_bytes(std::span(malformed)), 1), std::runtime_error);
    CHECK(document.Asset() == Id(101));
    CHECK(document.Definition() == authored);

    CHECK_THROWS_AS((void)document.Edit("Rejected VFX preview", [](Keire::VfxEffectDefinition& definition)
                                        { definition.Name = "Reject Preview"; }),
                    std::runtime_error);
    CHECK(document.Definition() == authored);
    CHECK(preview == authored);
    CHECK(previews == baselinePreviews + 2);
    CHECK(document.Diagnostic() == "Preview rejected.");

    document.Close();
    CHECK(stops == 1);
}

TEST_CASE("Discarding a newly created VFX effect stops preview without persistence")
{
    std::size_t previews = 0;
    std::size_t stops = 0;
    std::size_t persists = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview = [&](const Keire::AssetId, const Keire::VfxEffectDefinition&) { ++previews; },
         .StopPreview = [&](const Keire::AssetId) { ++stops; },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte>) { ++persists; }});
    document.Create(Id(102), Definition());
    CHECK(document.Dirty());
    CHECK(previews == 1);
    document.Discard();
    CHECK_FALSE(document.IsOpen());
    CHECK(stops == 1);
    CHECK(persists == 0);
}

TEST_CASE("VFX effect document preserves curve and gradient edits through undo and save")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Curves"});
    std::vector<std::byte> persisted;
    KeireEditor::VfxEffectDocument document(
        {.Persist = [&](const Keire::AssetId, const std::span<const std::byte> bytes)
         { persisted.assign(bytes.begin(), bytes.end()); }});
    const auto authored = Definition();
    const auto size =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxSizeOverLifetimeModule>(module.Payload); });
    const auto color =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxColorOverLifetimeModule>(module.Payload); });
    REQUIRE(size != authored.Modules.end());
    REQUIRE(color != authored.Modules.end());

    document.Open(Id(103), authored, 1, undo);
    CHECK(document.EditModule(
        size->Id, [](Keire::VfxModuleDefinition& module)
        { std::get<Keire::VfxSizeOverLifetimeModule>(module.Payload).Size = Keire::Curve1D::Linear(0.5F, 2.0F); }));
    CHECK(document.EditModule(color->Id,
                              [](Keire::VfxModuleDefinition& module)
                              {
                                  std::get<Keire::VfxColorOverLifetimeModule>(module.Payload).Color =
                                      Keire::ColorGradient(
                                          {{0.0F, {1.0F, 0.5F, 0.0F, 1.0F}}, {1.0F, {0.1F, 0.0F, 0.0F, 0.0F}}});
                              }));
    CHECK(document.Definition().Modules[size - authored.Modules.begin()].Id == size->Id);
    CHECK(document.Definition().Modules[color - authored.Modules.begin()].Id == color->Id);
    CHECK(document.Undo());
    CHECK(std::get<Keire::VfxColorOverLifetimeModule>(
              document.Definition().Modules[color - authored.Modules.begin()].Payload)
              .Color == std::get<Keire::VfxColorOverLifetimeModule>(color->Payload).Color);
    CHECK(document.Redo());

    document.Save();
    REQUIRE_FALSE(persisted.empty());
    CHECK(Keire::VfxEffectAsset::Decode(persisted)->Definition() == document.Definition());
    document.Close();
    undoService->Close();
}
