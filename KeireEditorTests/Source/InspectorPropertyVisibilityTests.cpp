#include "KeireClient/Editor/InspectorPropertyVisibility.h"

#include "Keire/ECS/Components/AudioComponents.h"

#include <doctest/doctest.h>

TEST_CASE("Audio Source Inspector exposes one stable bus authoring control")
{
    const auto audioSource = Keire::AudioSourceComponent::StaticType();
    CHECK(KeireEditor::IsInspectorPropertyVisible(audioSource, "busId"));
    CHECK_FALSE(KeireEditor::IsInspectorPropertyVisible(audioSource, "bus"));
    CHECK(KeireEditor::IsInspectorPropertyVisible(audioSource, "mixer"));

    CHECK(KeireEditor::IsInspectorPropertyVisible(Keire::AudioReverbZoneComponent::StaticType(), "bus"));
}
