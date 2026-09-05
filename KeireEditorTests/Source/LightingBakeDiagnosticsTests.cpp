#include "KeireClient/Editor/LightingBakeDiagnostics.h"

#include <doctest/doctest.h>

TEST_CASE("Lighting bake diagnostics explain eligible and mismatched targets")
{
    const auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                      Keire::SceneAsset::EmptyDefinition("Lighting diagnostics"));
    auto staticProbeReceiver =
        scene->CreateEntity("Static probe receiver").AddComponent<Keire::MeshRendererComponent>();
    staticProbeReceiver->SetStaticLighting(true);

    auto nonStaticLightmap = scene->CreateEntity("Non-static lightmap").AddComponent<Keire::MeshRendererComponent>();
    nonStaticLightmap->SetGIReceive(Keire::GIReceiveMode::Lightmaps);

    auto lightmap = scene->CreateEntity("Lightmap").AddComponent<Keire::MeshRendererComponent>();
    lightmap->SetStaticLighting(true);
    lightmap->SetGIReceive(Keire::GIReceiveMode::Lightmaps);

    (void)scene->CreateEntity("Reflection probe").AddComponent<Keire::ReflectionProbeComponent>();
    auto disabledVolume = scene->CreateEntity("Disabled volume").AddComponent<Keire::LightProbeVolumeComponent>();
    disabledVolume->SetEnabled(false);

    const auto summary = KeireEditor::SummarizeLightingBakeTargets(*scene);
    CHECK(summary.LightmapReceivers == 1U);
    CHECK(summary.ReflectionProbes == 1U);
    CHECK(summary.LightProbeVolumes == 0U);
    CHECK(summary.StaticRenderersUsingProbes == 1U);
    CHECK(summary.NonStaticRenderersUsingLightmaps == 1U);
    CHECK(summary.TotalTargets() == 2U);
    scene->Close();
}
