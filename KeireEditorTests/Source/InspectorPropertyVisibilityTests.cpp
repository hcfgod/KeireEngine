#include "KeireClient/Editor/InspectorComponentUtilities.h"
#include "KeireClient/Editor/InspectorPropertyVisibility.h"
#include "KeireClient/Editor/RenderFeatureDiagnostics.h"

#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"

#include <array>

#include <doctest/doctest.h>

TEST_CASE("Audio Source Inspector exposes one stable bus authoring control")
{
    const auto audioSource = Keire::AudioSourceComponent::StaticType();
    CHECK(KeireEditor::IsInspectorPropertyVisible(audioSource, "busId"));
    CHECK_FALSE(KeireEditor::IsInspectorPropertyVisible(audioSource, "bus"));
    CHECK(KeireEditor::IsInspectorPropertyVisible(audioSource, "mixer"));

    CHECK(KeireEditor::IsInspectorPropertyVisible(Keire::AudioReverbZoneComponent::StaticType(), "bus"));
}

TEST_CASE("Inspector Add Component hides retired scene UI components")
{
    constexpr std::array retiredTypes{
        "4b454952-4555-4943-414e-564153000001",
        "4b454952-4555-4952-4543-545452410001",
        "4b454952-4555-4954-4558-540000000001",
        "4b454952-4555-4942-5554-544f4e000001",
    };
    for (const auto* value : retiredTypes)
    {
        const auto type = Keire::ComponentTypeId::Parse(value);
        REQUIRE(type);
        CHECK(KeireEditor::IsRetiredSceneUiComponent(type));
    }
    CHECK_FALSE(KeireEditor::IsRetiredSceneUiComponent(Keire::UiDocumentComponent::StaticType()));
    CHECK_FALSE(KeireEditor::IsRetiredSceneUiComponent(Keire::TransformComponent::StaticType()));
}

TEST_CASE("Mesh Renderer Inspector identifies every baked-lighting authoring property")
{
    CHECK(KeireEditor::IsMeshRendererBakedLightingProperty("staticLighting"));
    CHECK(KeireEditor::IsMeshRendererBakedLightingProperty("giReceive"));
    CHECK(KeireEditor::IsMeshRendererBakedLightingProperty("lightmapScale"));
    CHECK(KeireEditor::IsMeshRendererBakedLightingProperty("preserveLightmapUvs"));
    CHECK_FALSE(KeireEditor::IsMeshRendererBakedLightingProperty("mesh"));
    CHECK_FALSE(KeireEditor::IsMeshRendererBakedLightingProperty("castShadows"));
}

TEST_CASE("Project Settings explains every global-illumination fallback")
{
    using enum Keire::GlobalIlluminationFallbackReason;

    CHECK(KeireEditor::BuildGlobalIlluminationFallbackMessage(None, "Disabled").empty());
    CHECK_EQ(KeireEditor::BuildGlobalIlluminationFallbackMessage(BakedUnavailable, "Disabled (Direct Only)"),
             "Baked indirect lighting is unavailable on the active renderer. Rendering uses Disabled (Direct Only).");
    CHECK_EQ(KeireEditor::BuildGlobalIlluminationFallbackMessage(RealtimeUnavailable, "Disabled (Direct Only)"),
             "Realtime environment lighting is unavailable on the active renderer. Rendering uses Disabled (Direct "
             "Only).");
    CHECK_EQ(KeireEditor::BuildGlobalIlluminationFallbackMessage(IrradynUnavailable, "Realtime Environment"),
             "Irradyn Dynamic GI is unavailable on the active renderer. Rendering uses Realtime Environment.");
    CHECK_EQ(KeireEditor::BuildGlobalIlluminationFallbackMessage(IrradynRequiresDeferredHybrid, "Realtime Environment"),
             "Irradyn Dynamic GI requires Deferred Hybrid; the selected render path remains active. Rendering uses "
             "Realtime Environment.");
    CHECK_EQ(KeireEditor::BuildGlobalIlluminationFallbackMessage(HybridUnavailable, "Baked Indirect"),
             "Hybrid GI requires both baked lighting and Irradyn on Deferred Hybrid. Rendering uses Baked Indirect.");
}
