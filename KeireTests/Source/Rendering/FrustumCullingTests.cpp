#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

namespace
{
    [[nodiscard]] bool ReferenceIntersectsFrustum(const Keire::Matrix4& clipFromLocal,
                                                  const Keire::MeshBounds bounds) noexcept
    {
        using Keire::RenderBackend::GeometryDetail::TransformClip;
        const std::array corners{Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Maximum.Z},
                                 Keire::Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
        std::array<Keire::RenderBackend::GeometryDetail::ClipPoint, corners.size()> clip{};
        std::ranges::transform(corners, clip.begin(),
                               [&](const auto corner) { return TransformClip(clipFromLocal, corner); });
        const auto all = [&](const auto predicate) { return std::ranges::all_of(clip, predicate); };
        return !all([](const auto point) { return point.X < -point.W; }) &&
               !all([](const auto point) { return point.X > point.W; }) &&
               !all([](const auto point) { return point.Y < -point.W; }) &&
               !all([](const auto point) { return point.Y > point.W; }) &&
               !all([](const auto point) { return point.Z < 0.0F; }) &&
               !all([](const auto point) { return point.Z > point.W; });
    }
} // namespace

TEST_CASE("always-visible geometry bypasses only camera-frustum bounds rejection")
{
    const Keire::Matrix4 clipFromLocal;
    const Keire::MeshBounds visible{{-0.5F, -0.5F, 0.25F}, {0.5F, 0.5F, 0.75F}};
    const Keire::MeshBounds outside{{2.0F, -0.5F, 0.25F}, {3.0F, 0.5F, 0.75F}};

    CHECK(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, visible, false));
    CHECK_FALSE(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, outside, false));
    CHECK(Keire::RenderBackend::GeometryDetail::IsFrustumVisible(clipFromLocal, outside, true));
}

TEST_CASE("aggregate bounds are trusted only when they enclose ordered child bounds")
{
    using Keire::RenderBackend::GeometryDetail::Encloses;
    const Keire::MeshBounds outer{{-2.0F, -3.0F, -4.0F}, {2.0F, 3.0F, 4.0F}};
    CHECK(Encloses(outer, {{-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F}}));
    CHECK_FALSE(Encloses(outer, {{-3.0F, -2.0F, -1.0F}, {1.0F, 2.0F, 3.0F}}));
    CHECK_FALSE(Encloses(outer, {{1.0F, 0.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}}));
    CHECK_FALSE(Encloses({{2.0F, 0.0F, 0.0F}, {-2.0F, 3.0F, 4.0F}}, outer));
}

TEST_CASE("plane-based frustum rejection matches homogeneous corner classification")
{
    const auto projection = Keire::Math::Perspective(65.0F, 16.0F / 9.0F, 0.1F, 100.0F);
    const auto view = Keire::Math::LookAt({3.0F, 2.0F, -7.0F}, {0.0F, 0.0F, 5.0F}, {0.0F, 1.0F, 0.0F});
    const Keire::MeshBounds bounds{{-0.75F, -0.5F, -1.0F}, {0.75F, 0.5F, 1.0F}};
    for (int x = -12; x <= 12; x += 2)
    {
        for (int y = -8; y <= 8; y += 2)
        {
            for (int z = -2; z <= 30; z += 2)
            {
                const auto model = Keire::Math::ComposeTransform(
                    {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                    Keire::Math::EulerDegreesToQuaternion({15.0F, 25.0F, 5.0F}), {-1.5F, 0.75F, 2.0F});
                const auto clipFromLocal = Keire::Math::Multiply(projection, Keire::Math::Multiply(view, model));
                CHECK(Keire::RenderBackend::GeometryDetail::IntersectsFrustum(clipFromLocal, bounds) ==
                      ReferenceIntersectsFrustum(clipFromLocal, bounds));
            }
        }
    }
}
