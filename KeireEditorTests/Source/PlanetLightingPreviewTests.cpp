#include "KeireClientInternal/Editor/EnvironmentAuthoringPresets.h"
#include "KeireClientInternal/Editor/EnvironmentLightingEditing.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireClientInternal/Editor/EnvironmentSkyEditing.h"

#include <doctest/doctest.h>

TEST_CASE("Daily-light editing preserves planetary frame ownership and invalidates preview history")
{
    using namespace Keire;
    using namespace Keire::Detail;
    using namespace KeireEditor::Detail;
    const EnvironmentLightingProfile initial(EnvironmentDirectionalOwnership::Environment,
                                             {{0.0, 90.0, 0.0}, {0.5, 90.0, 0.0}});
    KeireEditor::EnvironmentPreviewClock preview;
    preview.Bind({});
    const auto first = preview.CaptureLighting(initial, AssetId(1, 2));
    const EnvironmentLightingProfile planetary(initial.Ownership(), initial.Keyframes(), initial.Shadows(),
                                               initial.Sky(), EnvironmentDirectionalFrame::InertialRoot);
    const auto changed = preview.CaptureLighting(planetary, AssetId(1, 2));
    CHECK(changed.Revision == first.Revision + 1);
    CHECK(changed.DirectionFrame == EnvironmentDirectionalFrame::InertialRoot);
    CHECK(preview.CaptureLighting(planetary, AssetId(1, 2)).Revision == changed.Revision);
    for (const auto& edited :
         {SetAmbientAnimation(planetary, true, {}), ResetLightingShadows(planetary), AddLightingKey(planetary),
          SetLightingOwnership(planetary, false), ReplaceSkyProfile(planetary, {}), NightFillPreset(planetary, {}),
          DaylightPreset(planetary.Shadows(), planetary.Sky(), planetary.DirectionFrame())})
    {
        CHECK(edited.DirectionFrame() == EnvironmentDirectionalFrame::InertialRoot);
    }
}
