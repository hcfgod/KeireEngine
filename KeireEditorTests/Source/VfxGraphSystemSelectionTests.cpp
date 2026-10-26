#include "KeireClient/Editor/VfxEffectPanelModel.h"

#include <doctest/doctest.h>

#include <array>
#include <span>

TEST_CASE("VFX graph selection resolves before compact or wide layout is drawn")
{
    const auto first = Keire::AssetId::Generate();
    const auto second = Keire::AssetId::Generate();
    const std::array<Keire::VfxGraphSystem, 2> systems{{{.Id = first}, {.Id = second}}};

    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem({}, {}) == Keire::AssetId{});
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem({}, first) == Keire::AssetId{});
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem(systems, {}) == first);
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem(systems, second) == second);
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem(systems, Keire::AssetId::Generate()) == first);

    const std::span<const Keire::VfxGraphSystem> remaining(systems.data() + 1, 1);
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem(remaining, first) == second);
    CHECK(KeireEditor::Detail::ResolveVfxGraphSystem(remaining, second) == second);
    CHECK(systems.front().Id == first);
    CHECK(systems.back().Id == second);
}
