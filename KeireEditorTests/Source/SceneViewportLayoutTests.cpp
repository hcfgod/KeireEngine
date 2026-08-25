#include "KeireClient/Editor/SceneViewportLayout.h"

#include <doctest/doctest.h>

TEST_CASE("Scene viewport right toolbar drops optional controls before overlapping the gizmo toolbar")
{
    const Keire::UiItemRect leftToolbar{{8.0F, 8.0F}, {284.0F, 36.0F}};

    const auto wide =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {520.0F, 550.0F}}, leftToolbar);
    CHECK(wide.ButtonCount == 7U);
    CHECK(wide.ShowAxes);
    CHECK(wide.ShowOcclusionMetadata);
    CHECK(wide.Rectangle.Minimum.X > leftToolbar.Maximum.X);

    const auto ordinaryNarrow =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {480.0F, 550.0F}}, leftToolbar);
    CHECK(ordinaryNarrow.ButtonCount == 6U);
    CHECK(ordinaryNarrow.ShowAxes);
    CHECK(ordinaryNarrow.ShowOcclusionVisibility);
    CHECK_FALSE(ordinaryNarrow.ShowOcclusionMetadata);
    CHECK(ordinaryNarrow.Rectangle.Minimum.X > leftToolbar.Maximum.X);

    const auto narrow =
        KeireEditor::CalculateSceneViewportRightToolbarLayout({{0.0F, 0.0F}, {440.0F, 550.0F}}, leftToolbar);
    CHECK(narrow.ButtonCount == 4U);
    CHECK_FALSE(narrow.ShowAxes);
    CHECK(narrow.ShowCameraPreview);
    CHECK(narrow.ShowOcclusionVisibility);
    CHECK(narrow.ShowOcclusionMetadata);
    CHECK(narrow.Rectangle.Minimum.X > leftToolbar.Maximum.X);
}

TEST_CASE("Scene occlusion diagnostics avoid performance and preview reservations")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {1200.0F, 550.0F}};
    const Keire::UiItemRect performance{{870.0F, 48.0F}, {1188.0F, 285.0F}};
    const auto diagnostics = KeireEditor::PlaceSceneOcclusionDiagnostics(viewport, 328.0F, performance);
    REQUIRE(diagnostics);
    CHECK(diagnostics->Maximum.X <= performance.Minimum.X - 8.0F);
    CHECK(diagnostics->Minimum.Y >= viewport.Minimum.Y + 48.0F);
    CHECK(diagnostics->Maximum.Y <= 328.0F);

    const auto tooShort = KeireEditor::PlaceSceneOcclusionDiagnostics({{0.0F, 0.0F}, {440.0F, 180.0F}}, 48.0F);
    CHECK_FALSE(tooShort);

    const auto noAvailableSlot = KeireEditor::PlaceSceneOcclusionDiagnostics(
        {{0.0F, 0.0F}, {440.0F, 350.0F}}, 218.0F, Keire::UiItemRect{{110.0F, 48.0F}, {428.0F, 285.0F}});
    CHECK_FALSE(noAvailableSlot);
}

TEST_CASE("Scene performance overlays avoid the main camera preview")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {1200.0F, 400.0F}};
    const Keire::UiItemRect preview{{868.0F, 186.0F}, {1188.0F, 366.0F}};
    const auto advanced =
        KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 237.0F}, 48.0F, true, preview);
    REQUIRE(advanced);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*advanced, preview));
    CHECK(advanced->Maximum.X < preview.Minimum.X);

    const Keire::UiItemRect narrowViewport{{0.0F, 0.0F}, {380.0F, 400.0F}};
    const Keire::UiItemRect narrowPreview{{208.0F, 276.0F}, {368.0F, 366.0F}};
    CHECK_FALSE(
        KeireEditor::PlaceViewportPerformanceOverlay(narrowViewport, {318.0F, 237.0F}, 48.0F, true, narrowPreview));
    const auto compact =
        KeireEditor::PlaceViewportPerformanceOverlay(narrowViewport, {166.0F, 44.0F}, 48.0F, false, narrowPreview);
    REQUIRE(compact);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*compact, narrowPreview));

    const auto statusReserved =
        KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 319.0F}, 48.0F, true, preview, 34.0F);
    CHECK_FALSE(statusReserved);
}

TEST_CASE("Scene camera preview fits between viewport toolbars and status")
{
    const auto ordinary = KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {1200.0F, 400.0F}});
    REQUIRE(ordinary);
    CHECK(ordinary->Minimum.Y >= 48.0F);
    CHECK(ordinary->Maximum.Y <= 366.0F);

    CHECK_FALSE(KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {440.0F, 150.0F}}));
    CHECK_FALSE(KeireEditor::PlaceSceneCameraPreview({{0.0F, 0.0F}, {440.0F, 100.0F}}));
}

TEST_CASE("Empty Scene actions force a non-overlapping compact performance overlay")
{
    const Keire::UiItemRect viewport{{0.0F, 0.0F}, {900.0F, 500.0F}};
    const auto centered = KeireEditor::CalculateSceneViewportCenteredStateLayout(viewport, 400.0F, true);
    CHECK(centered.ShowActions);
    CHECK_FALSE(KeireEditor::PlaceViewportPerformanceOverlay(viewport, {318.0F, 289.0F}, 48.0F, true,
                                                             centered.Reservation, 34.0F));
    const auto compact = KeireEditor::PlaceViewportPerformanceOverlay(viewport, {166.0F, 44.0F}, 48.0F, false,
                                                                      centered.Reservation, 34.0F);
    REQUIRE(compact);
    CHECK_FALSE(KeireEditor::SceneViewportRectanglesOverlap(*compact, centered.Reservation));

    const auto shortState =
        KeireEditor::CalculateSceneViewportCenteredStateLayout({{0.0F, 0.0F}, {440.0F, 100.0F}}, 400.0F, true);
    CHECK_FALSE(shortState.ShowActions);
    CHECK(shortState.Reservation.Minimum.Y >= 8.0F);
    CHECK(shortState.Reservation.Maximum.Y <= 92.0F);

    const Keire::UiItemRect unavailableViewport{{0.0F, 0.0F}, {700.0F, 550.0F}};
    const auto rendererUnavailable =
        KeireEditor::CalculateSceneViewportCenteredStateLayout(unavailableViewport, 440.0F, false);
    CHECK_FALSE(KeireEditor::PlaceViewportPerformanceOverlay(unavailableViewport, {318.0F, 234.0F}, 48.0F, true,
                                                             rendererUnavailable.Reservation, 34.0F));
}

TEST_CASE("Scene status hides before overlapping the top toolbar")
{
    CHECK_FALSE(KeireEditor::CanPlaceSceneViewportStatus({{0.0F, 0.0F}, {440.0F, 60.0F}}));
    CHECK(KeireEditor::CanPlaceSceneViewportStatus({{0.0F, 0.0F}, {440.0F, 100.0F}}));
}
