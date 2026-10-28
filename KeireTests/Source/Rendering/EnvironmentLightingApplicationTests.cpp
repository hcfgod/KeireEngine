#include "KeireInternal/Rendering/EnvironmentLightingApplication.h"

#include <doctest/doctest.h>

TEST_CASE("Fallback environment lighting follows evaluated ambient energy without changing authored maps")
{
    const Keire::RenderEnvironmentSettings authored;
    std::array<std::array<double, 2>, Keire::MaximumCloudLayers> cloudPhase{};
    double starPhase = 0.0;
    Keire::EnvironmentLightingSnapshot snapshot;
    snapshot.Ambient = Keire::EnvironmentAmbientLight{{1.0F, 1.0F, 1.0F}, 0.0F};
    auto night = authored;
    Keire::Detail::ApplyEnvironment(night, cloudPhase, starPhase, snapshot);
    CHECK(night.EnvironmentDiffuseIntensity == 0.0F);
    CHECK(night.EnvironmentSpecularIntensity == 0.0F);
    CHECK(authored.EnvironmentDiffuseIntensity == 1.0F);

    snapshot.Ambient->Intensity = 0.08F;
    snapshot.Ambient->LinearColor = {0.08F, 0.12F, 0.25F};
    auto moonlit = authored;
    Keire::Detail::ApplyEnvironment(moonlit, cloudPhase, starPhase, snapshot);
    CHECK(moonlit.EnvironmentDiffuseIntensity > 0.0F);
    CHECK(moonlit.EnvironmentDiffuseIntensity < 0.02F);
    CHECK(moonlit.EnvironmentSpecularIntensity == moonlit.EnvironmentDiffuseIntensity);

    snapshot.Ambient = Keire::EnvironmentAmbientLight{{1.0F, 1.0F, 1.0F}, 1.0F};
    auto daylight = authored;
    daylight.EnvironmentDiffuseIntensity = 2.0F;
    daylight.EnvironmentSpecularIntensity = 3.0F;
    Keire::Detail::ApplyEnvironment(daylight, cloudPhase, starPhase, snapshot);
    CHECK(daylight.EnvironmentDiffuseIntensity == doctest::Approx(2.0F));
    CHECK(daylight.EnvironmentSpecularIntensity == doctest::Approx(3.0F));

    snapshot.Ambient->LinearColor = {};
    auto blackAmbient = authored;
    Keire::Detail::ApplyEnvironment(blackAmbient, cloudPhase, starPhase, snapshot);
    CHECK(blackAmbient.EnvironmentDiffuseIntensity == 0.0F);
    CHECK(blackAmbient.EnvironmentSpecularIntensity == 0.0F);

    auto mapped = authored;
    mapped.Environment = Keire::AssetId(1, 2);
    mapped.EnvironmentDiffuseIntensity = 2.0F;
    mapped.EnvironmentSpecularIntensity = 3.0F;
    Keire::Detail::ApplyEnvironment(mapped, cloudPhase, starPhase, snapshot);
    CHECK(mapped.EnvironmentDiffuseIntensity == 2.0F);
    CHECK(mapped.EnvironmentSpecularIntensity == 3.0F);

    snapshot.Ambient.reset();
    auto disabled = authored;
    Keire::Detail::ApplyEnvironment(disabled, cloudPhase, starPhase, snapshot);
    CHECK(disabled.EnvironmentDiffuseIntensity == authored.EnvironmentDiffuseIntensity);
    CHECK(disabled.EnvironmentSpecularIntensity == authored.EnvironmentSpecularIntensity);
}
