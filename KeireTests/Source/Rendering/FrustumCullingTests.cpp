#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <doctest/doctest.h>

TEST_CASE("always-visible geometry bypasses only camera-frustum bounds rejection")
{
    const Keire::Matrix4 clipFromLocal;
    const Keire::MeshBounds visible{{-0.5F, -0.5F, 0.25F}, {0.5F, 0.5F, 0.75F}};
    const Keire::MeshBounds outside{{2.0F, -0.5F, 0.25F}, {3.0F, 0.5F, 0.75F}};

    CHECK(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, visible, false));
    CHECK_FALSE(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, outside, false));
    CHECK(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, outside, true));
}
