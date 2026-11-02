#include "KeireClientInternal/Editor/WaterBodyInspectorEditing.h"

#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
    struct WaterEditorFixture
    {
        Keire::Ref<Keire::UndoService> Undo = Keire::CreateRef<Keire::UndoService>();
        Keire::Ref<Keire::UndoContext> History = Undo->CreateContext({"Water tests"});
        Keire::Ref<Keire::ComponentRegistry> Registry = Keire::ComponentRegistry::CreateDefault();
        KeireEditor::SceneDocument Document;
        std::array<Keire::AssetId, 2> Entities;

        WaterEditorFixture()
        {
            if (!Registry->Contains(Keire::WaterBodyComponent::StaticType()))
            {
                Registry->Register(Keire::CreateWaterBodyComponentRegistration());
            }
            const auto scene = Keire::CreateRef<Keire::Scene>(
                Keire::AssetId::Generate(), Keire::SceneDefinition{.Name = "Water authoring fixture"}, Registry);
            for (std::size_t index = 0; index < Entities.size(); ++index)
            {
                auto entity = scene->CreateEntity("Water " + std::to_string(index));
                Entities[index] = entity.Id().Value();
                const auto component = entity.AddComponent<Keire::WaterBodyComponent>();
                auto settings = component->Settings();
                settings.Material = Keire::AssetId::Generate();
                settings.DepthMetres = 10.0 + static_cast<double>(index);
                settings.Roughness = 0.2F + static_cast<float>(index) * 0.1F;
                settings.GridSpacingMetres = 1.5 + static_cast<double>(index);
                settings.Waves = KeireEditor::Detail::WaterPresetWaves(KeireEditor::Detail::WaterWavePreset::Gentle);
                settings.Waves[0].AmplitudeMetres += static_cast<double>(index);
                component->Configure(settings);
            }
            Document.Open(scene, scene->Asset(), {}, History);
        }

        void Record(const std::string_view name)
        {
            const auto scene = Document.ActiveScene();
            const auto asset = scene->Asset();
            const auto before = scene->Snapshot();
            auto after = std::make_shared<std::optional<Keire::SceneDefinition>>();
            const auto restore = [this, asset](const Keire::SceneDefinition& definition)
            { Document.ReplaceEditingScene(Keire::CreateRef<Keire::Scene>(asset, definition, Registry)); };
            History->RecordApplied(Keire::CreateUndoCommand(
                std::string(name), [after, restore] { restore(after->value()); },
                [this, before, after, restore]
                {
                    if (!after->has_value())
                    {
                        *after = Document.ActiveScene()->Snapshot();
                    }
                    restore(before);
                }));
        }

        [[nodiscard]] std::vector<KeireEditor::Detail::WaterEditTarget> Targets()
        {
            return KeireEditor::Detail::ResolveWaterTargets(Document.ActiveScene(), Entities, 0);
        }
    };
} // namespace

TEST_CASE("water wave presets and list edits preserve authored order and enforce capacity")
{
    using namespace KeireEditor::Detail;
    for (const auto preset : {WaterWavePreset::Still, WaterWavePreset::Gentle, WaterWavePreset::Ocean})
    {
        Keire::WaterBodySettings settings;
        settings.Waves = WaterPresetWaves(preset);
        CHECK_NOTHROW(Keire::WaterBodyComponent::Validate(settings));
    }
    Keire::WaterBodySettings settings;
    settings.Waves = WaterPresetWaves(WaterWavePreset::Ocean);
    const auto first = settings.Waves.front().WavelengthMetres;
    const auto second = settings.Waves[1].WavelengthMetres;
    MoveWaterWave(settings, 0, 3);
    CHECK(settings.Waves.back().WavelengthMetres == first);
    CHECK(settings.Waves.front().WavelengthMetres == second);
    MoveWaterWave(settings, 3, 0);
    CHECK(settings.Waves.front().WavelengthMetres == first);
    RemoveWaterWave(settings, 0);
    CHECK(settings.Waves.front().WavelengthMetres == second);
    settings.Waves.resize(Keire::WaterWaveField::MaximumWaves);
    CHECK_THROWS_AS(AddWaterWave(settings), std::invalid_argument);
    CHECK(settings.Waves.size() == Keire::WaterWaveField::MaximumWaves);
    CHECK_THROWS_AS(RemoveWaterWave(settings, settings.Waves.size()), std::out_of_range);
    CHECK_THROWS_AS(MoveWaterWave(settings, 0, settings.Waves.size()), std::out_of_range);
}

TEST_CASE("water inspector multi edits preserve per target values and undo redo in one transaction")
{
    WaterEditorFixture fixture;
    const auto targets = fixture.Targets();
    const auto first = targets[0].Component->Settings();
    const auto second = targets[1].Component->Settings();
    KeireEditor::Detail::ApplyWaterEdit(
        fixture.Document, targets, 0, "Move water waves", [](auto& settings)
        { KeireEditor::Detail::MoveWaterWave(settings, 0, 1); }, [&](const auto name) { fixture.Record(name); });
    CHECK(fixture.History->UndoCount() == 1);
    auto edited = fixture.Targets();
    CHECK(edited[0].Component->Settings().Material == first.Material);
    CHECK(edited[1].Component->Settings().Material == second.Material);
    CHECK(edited[0].Component->Settings().DepthMetres == first.DepthMetres);
    CHECK(edited[1].Component->Settings().DepthMetres == second.DepthMetres);
    CHECK(edited[0].Component->Settings().Roughness == first.Roughness);
    CHECK(edited[1].Component->Settings().Roughness == second.Roughness);
    CHECK(edited[0].Component->Settings().GridSpacingMetres == first.GridSpacingMetres);
    CHECK(edited[1].Component->Settings().GridSpacingMetres == second.GridSpacingMetres);
    CHECK(edited[0].Component->Settings().Waves[1].AmplitudeMetres == first.Waves[0].AmplitudeMetres);
    CHECK(edited[1].Component->Settings().Waves[1].AmplitudeMetres == second.Waves[0].AmplitudeMetres);
    REQUIRE(fixture.History->Undo());
    CHECK(fixture.Targets()[0].Component->Settings() == first);
    CHECK(fixture.Targets()[1].Component->Settings() == second);
    REQUIRE(fixture.History->Redo());
    CHECK(fixture.Targets()[1].Component->Settings().Waves[1].AmplitudeMetres == second.Waves[0].AmplitudeMetres);
}

TEST_CASE("water inspector rejects invalid batches before history and restores partial failed document edits")
{
    WaterEditorFixture fixture;
    const auto targets = fixture.Targets();
    const auto first = targets[0].Component->Settings();
    const auto second = targets[1].Component->Settings();
    CHECK_THROWS_AS(KeireEditor::Detail::ApplyWaterEdit(
                        fixture.Document, targets, 0, "Invalid water edit", [](auto& settings)
                        { settings.DepthMetres = -1.0; }, [&](const auto name) { fixture.Record(name); }),
                    std::invalid_argument);
    CHECK(fixture.History->UndoCount() == 0);
    CHECK(fixture.Targets()[0].Component->Settings() == first);
    CHECK_THROWS_AS(
        KeireEditor::Detail::ApplyWaterEdit(
            fixture.Document, targets, 0, "Interrupted water edit", [](auto& settings) { settings.DepthMetres = 25.0; },
            [&](const auto name)
            {
                fixture.Record(name);
                // Simulate selection invalidation after preparation: the first target updates, the second fails.
                auto entity = fixture.Document.ActiveScene()->FindEntity(Keire::EntityId(fixture.Entities[1]));
                REQUIRE(entity.Destroy());
            }),
        std::invalid_argument);
    CHECK(fixture.History->UndoCount() == 0);
    CHECK(fixture.Targets()[0].Component->Settings() == first);
    CHECK(fixture.Targets()[1].Component->Settings() == second);
}

TEST_CASE("water property changes contain no unrelated geometry or appearance")
{
    Keire::WaterBodySettings before;
    before.Material = Keire::AssetId::Generate();
    auto after = before;
    after.Waves = KeireEditor::Detail::WaterPresetWaves(KeireEditor::Detail::WaterWavePreset::Gentle);
    const auto changes = KeireEditor::Detail::WaterBodyPropertyChanges(before, after);
    CHECK(changes.contains("waveCount"));
    CHECK_FALSE(changes.contains("material"));
    CHECK_FALSE(changes.contains("shape"));
    CHECK_FALSE(changes.contains("depth"));
    CHECK_FALSE(changes.contains("surfaceColor"));
    CHECK_FALSE(changes.contains("roughness"));
    CHECK_FALSE(changes.contains("crestFoamStrength"));
    CHECK_FALSE(changes.contains("gridSpacing"));
    CHECK_FALSE(changes.contains("renderDistance"));
    CHECK(KeireEditor::Detail::WaterBodyPropertyChanges(before, before).empty());
}

TEST_CASE("water quick setups prepare complete valid surfaces and restore all selected settings through undo")
{
    using namespace KeireEditor::Detail;
    for (const auto preset : {WaterSetupPreset::Sea, WaterSetupPreset::Lake, WaterSetupPreset::Pond})
    {
        WaterEditorFixture fixture;
        const auto targets = fixture.Targets();
        const auto first = targets[0].Component->Settings();
        const auto second = targets[1].Component->Settings();
        ApplyWaterEdit(
            fixture.Document, targets, 0, "Set Up Water", [preset](auto& settings)
            { ApplyWaterSetup(settings, preset); }, [&](const auto name) { fixture.Record(name); });
        REQUIRE(fixture.History->UndoCount() == 1);
        const auto configured = fixture.Targets()[0].Component->Settings();
        CHECK_NOTHROW(Keire::WaterBodyComponent::Validate(configured));
        CHECK(configured.Shape == (preset == WaterSetupPreset::Sea ? Keire::WaterSurfaceShape::InfinitePlane
                                                                   : Keire::WaterSurfaceShape::Ellipse));
        CHECK(WaterWaveHeight(configured) > 0.0);
        CHECK(configured.Material == Keire::AssetId{});
        CHECK(fixture.Targets()[1].Component->Settings() == configured);
        REQUIRE(fixture.History->Undo());
        CHECK(fixture.Targets()[0].Component->Settings() == first);
        CHECK(fixture.Targets()[1].Component->Settings() == second);
        REQUIRE(fixture.History->Redo());
        CHECK(fixture.Targets()[0].Component->Settings() == configured);
        CHECK(fixture.Targets()[1].Component->Settings() == configured);
    }
}

TEST_CASE("water surface looks preserve all geometry fluid wave and material choices")
{
    using namespace KeireEditor::Detail;
    WaterEditorFixture fixture;
    const auto targets = fixture.Targets();
    const auto first = targets[0].Component->Settings();
    const auto second = targets[1].Component->Settings();
    ApplyWaterEdit(
        fixture.Document, targets, 0, "Style Water", [](auto& settings)
        { ApplyWaterLook(settings, WaterLookPreset::Stylized); }, [&](const auto name) { fixture.Record(name); });
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        auto expected = index == 0 ? first : second;
        ApplyWaterLook(expected, WaterLookPreset::Stylized);
        const auto actual = fixture.Targets()[index].Component->Settings();
        CHECK(actual == expected);
        const auto changes = WaterBodyPropertyChanges(index == 0 ? first : second, actual);
        for (const auto& [name, value] : changes)
        {
            CHECK((name == "surfaceColor" || name == "roughness" || name == "crestFoamStrength"));
        }
    }
    REQUIRE(fixture.History->Undo());
    CHECK(fixture.Targets()[0].Component->Settings() == first);
    CHECK(fixture.Targets()[1].Component->Settings() == second);
    REQUIRE(fixture.History->Redo());
    CHECK(fixture.Targets()[0].Component->Settings().SurfaceColor.Blue > first.SurfaceColor.Blue);
}

TEST_CASE("simple water motion controls preserve spectrum relationships and reject unsafe inputs unchanged")
{
    using namespace KeireEditor::Detail;
    Keire::WaterBodySettings settings;
    ApplyWaterSetup(settings, WaterSetupPreset::Sea);
    const auto before = settings;
    SetWaterWaveHeight(settings, WaterWaveHeight(before) * 0.5);
    CHECK(WaterWaveHeight(settings) == doctest::Approx(WaterWaveHeight(before) * 0.5));
    for (std::size_t index = 0; index < settings.Waves.size(); ++index)
    {
        const auto& actual = settings.Waves[index];
        const auto& original = before.Waves[index];
        CHECK(actual.AmplitudeMetres == doctest::Approx(original.AmplitudeMetres * 0.5));
        CHECK(actual.WavelengthMetres == original.WavelengthMetres);
        CHECK(actual.SpeedMetresPerSecond == original.SpeedMetresPerSecond);
        CHECK(actual.PhaseDegrees == original.PhaseDegrees);
    }
    SetWaterWaveDirection(settings, -80.0);
    CHECK(settings.Waves.front().DirectionDegrees == doctest::Approx(-80.0));
    CHECK(settings.Waves[1].DirectionDegrees - settings.Waves[0].DirectionDegrees ==
          doctest::Approx(before.Waves[1].DirectionDegrees - before.Waves[0].DirectionDegrees));
    const auto valid = settings;
    CHECK_THROWS_AS(SetWaterWaveHeight(settings, -1.0), std::invalid_argument);
    CHECK(settings == valid);
    CHECK_THROWS_AS(SetWaterWaveHeight(settings, std::numeric_limits<double>::infinity()), std::invalid_argument);
    CHECK(settings == valid);
    CHECK_THROWS_AS(SetWaterWaveDirection(settings, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    CHECK(settings == valid);
    SetWaterWaveHeight(settings, 0.0);
    CHECK(WaterWaveHeight(settings) == 0.0);
    SetWaterWaveHeight(settings, 0.2);
    CHECK(WaterWaveHeight(settings) == doctest::Approx(0.2));
    CHECK_NOTHROW(Keire::WaterBodyComponent::Validate(settings));
    settings.Shape = Keire::WaterSurfaceShape::Sphere;
    settings.Waves.clear();
    const auto sphere = settings;
    CHECK_THROWS_AS(SetWaterWaveHeight(settings, 0.2), std::invalid_argument);
    CHECK(settings == sphere);
}

TEST_CASE("creating preset water records complete root and child surfaces in one undo transaction")
{
    using namespace KeireEditor::Detail;
    for (const auto preset : {WaterSetupPreset::Sea, WaterSetupPreset::Lake, WaterSetupPreset::Pond})
    {
        for (const bool child : {false, true})
        {
            WaterEditorFixture fixture;
            const auto parent = child ? Keire::EntityId(fixture.Entities[0]) : Keire::EntityId{};
            const auto beforeCount = fixture.Document.ActiveScene()->HierarchySnapshot().Objects.size();
            const auto created =
                CreateWaterEntity(fixture.Document, preset, parent, [&](const auto name) { fixture.Record(name); });
            REQUIRE(fixture.History->UndoCount() == 1);
            Keire::WaterBodySettings expected;
            ApplyWaterSetup(expected, preset);
            const auto checkCreated = [&]
            {
                const auto entity = fixture.Document.ActiveScene()->FindEntity(created);
                REQUIRE(entity);
                CHECK(entity.Parent().Id() == parent);
                const auto water = entity.GetComponent<Keire::WaterBodyComponent>();
                REQUIRE(water);
                CHECK(water->Settings() == expected);
                CHECK(fixture.Document.ActiveScene()->HierarchySnapshot().Objects.size() == beforeCount + 1);
            };
            checkCreated();
            REQUIRE(fixture.History->Undo());
            CHECK_FALSE(fixture.Document.ActiveScene()->FindEntity(created));
            CHECK(fixture.Document.ActiveScene()->HierarchySnapshot().Objects.size() == beforeCount);
            REQUIRE(fixture.History->Redo());
            checkCreated();
        }
    }
}

TEST_CASE("failed water creation cancels history and restores the scene after parent invalidation")
{
    using namespace KeireEditor::Detail;
    WaterEditorFixture fixture;
    const auto first = fixture.Targets()[0].Component->Settings();
    const auto second = fixture.Targets()[1].Component->Settings();
    CHECK_THROWS_AS(CreateWaterEntity(fixture.Document, WaterSetupPreset::Sea, Keire::EntityId(fixture.Entities[0]),
                                      [&](const auto name)
                                      {
                                          fixture.Record(name);
                                          // The snapshot already exists, but the parent disappears before entity setup.
                                          auto parent = fixture.Document.ActiveScene()->FindEntity(
                                              Keire::EntityId(fixture.Entities[0]));
                                          REQUIRE(parent.Destroy());
                                      }),
                    std::invalid_argument);
    CHECK(fixture.History->UndoCount() == 0);
    CHECK(fixture.Document.ActiveScene()->HierarchySnapshot().Objects.size() == 2);
    CHECK(fixture.Targets()[0].Component->Settings() == first);
    CHECK(fixture.Targets()[1].Component->Settings() == second);
    CHECK_THROWS_AS(CreateWaterEntity(fixture.Document, static_cast<WaterSetupPreset>(99), {},
                                      [&](const auto name) { fixture.Record(name); }),
                    std::invalid_argument);
    CHECK(fixture.History->UndoCount() == 0);
}
