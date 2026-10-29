#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"
#include "KeireInternal/Rendering/RenderShaderDataInternal.h"

#include <doctest/doctest.h>

#include <cstddef>

TEST_CASE("Cloud controls preserve every legacy material uniform prefix")
{
    using namespace Keire::RenderBackend;

    CHECK(offsetof(CloudVolumeViewUniforms, ShapeControls) == 272U);
    CHECK(offsetof(CloudVolumeViewUniforms, RegionalControls) == 288U);
    CHECK(offsetof(CloudVolumeViewUniforms, Atmosphere) == 304U);
    CHECK(sizeof(CloudVolumeViewUniforms) == 1616U);
    CHECK(offsetof(CloudVolumeViewUniforms, PlanetaryCloud) == 1520U);
    CHECK(CloudVolumeUniformByteSize(1U) == 272U);
    CHECK(CloudVolumeUniformByteSize(2U) == 272U);
    CHECK(CloudVolumeUniformByteSize(3U) == 288U);
    CHECK(CloudVolumeUniformByteSize(4U) == 304U);
    CHECK(CloudVolumeUniformByteSize(5U) == 416U);
    CHECK(CloudVolumeUniformByteSize(6U) == 432U);
    CHECK(CloudVolumeUniformByteSize(7U) == 448U);
    CHECK(CloudVolumeUniformByteSize(8U) == 1008U);
    CHECK(CloudVolumeUniformByteSize(9U) == 1520U);
    CHECK(CloudVolumeUniformByteSize(10U) == 1520U);
    CHECK(CloudVolumeUniformByteSize(11U) == 1520U);
    CHECK(CloudVolumeUniformByteSize(12U) == 1520U);
    CHECK(CloudVolumeUniformByteSize(13U) == 1616U);
    CHECK(CloudVolumeUniformByteSize(14U) == 1616U);
    CHECK(offsetof(CloudVolumeViewUniforms, Fog) == 448U);
    CHECK(offsetof(CloudVolumeViewUniforms, WeatherControls) == 432U);
    CHECK(offsetof(CloudVolumeViewUniforms, HeightProfile) == 416U);
    CHECK(offsetof(AssetEnvironmentVolumeUniforms, Volume) == 944U);
    CHECK(sizeof(AssetEnvironmentVolumeUniforms) == 2560U);
}
