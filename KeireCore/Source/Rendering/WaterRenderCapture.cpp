#include "KeireInternal/Rendering/WaterRenderCapture.h"

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/WaterBodyComponent.h"
#include "KeireInternal/Spatial/FrameRotation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::uint32_t MaximumCellsPerAxis = 128;

        [[nodiscard]] Vector4 ToVector(const std::array<double, 3>& value)
        {
            Vector4 result{static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]), 0};
            if (!Math::IsFinite(result))
            {
                throw std::overflow_error("Water render coordinates exceed float range.");
            }
            return result;
        }

        [[nodiscard]] bool OutsideFrustum(const WaterRenderPatch& patch, const Matrix4& matrix,
                                          const double displacement)
        {
            // Test an expanded oriented box against all clip planes. A plane rejects only when every
            // corner is outside, retaining intersecting patches and wave crests at the frustum edge.
            std::array<unsigned int, 6> outside{};
            for (unsigned int corner = 0; corner < 8; ++corner)
            {
                const double x = (corner & 1U) ? patch.Grid.X + displacement : -displacement;
                const double y = (corner & 2U) ? displacement : -displacement;
                const double z = (corner & 4U) ? patch.Grid.Y + displacement : -displacement;
                const std::array<double, 4> point{
                    patch.Origin.X + patch.AxisX.X * x + patch.AxisY.X * y + patch.AxisZ.X * z,
                    patch.Origin.Y + patch.AxisX.Y * x + patch.AxisY.Y * y + patch.AxisZ.Y * z,
                    patch.Origin.Z + patch.AxisX.Z * x + patch.AxisY.Z * y + patch.AxisZ.Z * z, 1.0};
                std::array<double, 4> clip{};
                for (unsigned int row = 0; row < 4; ++row)
                {
                    for (unsigned int column = 0; column < 4; ++column)
                    {
                        clip[row] += matrix.Elements[column * 4 + row] * point[column];
                    }
                }
                const std::array distances{clip[0] + clip[3], clip[3] - clip[0], clip[1] + clip[3],
                                           clip[3] - clip[1], clip[2],           clip[3] - clip[2]};
                for (unsigned int plane = 0; plane < 6; ++plane)
                {
                    outside[plane] += distances[plane] < 0.0 ? 1U : 0U;
                }
            }
            return std::ranges::any_of(outside, [](const auto count) { return count == 8; });
        }
    } // namespace

    void SortWaterPatches(std::vector<WaterRenderPatch>& patches)
    {
        for (const auto& patch : patches)
        {
            if (!std::isfinite(patch.SortDepth))
            {
                throw std::invalid_argument("Water draw depth must be finite.");
            }
        }
        if (patches.size() < 2)
        {
            return;
        }
        // Sort small indices rather than moving aligned shader blocks during every comparison.
        // The source index preserves capture order at equal depth without MSVC's aligned temporary buffer.
        std::vector<std::size_t> order(patches.size());
        std::iota(order.begin(), order.end(), std::size_t{});
        std::ranges::sort(order,
                          [&](const auto left, const auto right)
                          {
                              if (patches[left].SortDepth != patches[right].SortDepth)
                              {
                                  return patches[left].SortDepth > patches[right].SortDepth;
                              }
                              return left < right;
                          });
        std::vector<WaterRenderPatch> sorted;
        sorted.reserve(patches.size());
        for (const auto index : order)
        {
            sorted.push_back(std::move(patches[index]));
        }
        patches.swap(sorted);
    }

    void CaptureWaterBodies(const Scene& scene, const Vector3d worldOrigin, const Vector3 observerRelative,
                            const Matrix4& view, const Matrix4& viewProjection, const double timeSeconds,
                            WaterRenderCapture& output)
    {
        for (const auto& entity : scene.Query<WaterBodyComponent>())
        {
            const auto body = entity.GetComponent<WaterBodyComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!entity.ActiveInHierarchy() || !body || !body->Enabled() || !transform)
            {
                continue;
            }
            const auto& settings = body->Settings();
            if (settings.Shape == WaterSurfaceShape::Sphere)
            {
                ++output.UnsupportedSphereBodies;
                continue;
            }
            if (settings.Material)
            {
                ++output.UnsupportedMaterialBodies;
                continue;
            }
            const auto position = transform->PresentationWorldPositionPrecise();
            const auto quaternion = transform->PresentationWorldRotation();
            const auto rotation =
                Detail::FrameRotation::Normalize(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W);
            const std::array<double, 3> relative{position.X - worldOrigin.X, position.Y - worldOrigin.Y,
                                                 position.Z - worldOrigin.Z};
            const auto localObserver = rotation.Inverse().Apply(
                {observerRelative.X - relative[0], observerRelative.Y - relative[1], observerRelative.Z - relative[2]});
            double maximumDisplacement = 0.0;
            for (const auto& wave : settings.Waves)
            {
                maximumDisplacement += wave.AmplitudeMetres;
            }
            const double distance = settings.RenderDistanceMetres;
            if (distance > std::numeric_limits<float>::max() || static_cast<float>(distance) <= 0.0F)
            {
                ++output.UnrepresentableBodies;
                continue;
            }
            if (std::abs(localObserver[1]) > distance + maximumDisplacement)
            {
                continue;
            }
            // Snap to a stable local lattice. The bounded grid can coarsen the requested spacing,
            // but the phase is always evaluated at the exact local origin, so moving it never resets waves.
            const double spacing = std::max(settings.GridSpacingMetres, 2.0 * distance / MaximumCellsPerAxis);
            const double centerX = std::floor(localObserver[0] / spacing) * spacing;
            const double centerZ = std::floor(localObserver[2] / spacing) * spacing;
            double minimumX = centerX - distance;
            double maximumX = centerX + distance;
            double minimumZ = centerZ - distance;
            double maximumZ = centerZ + distance;
            if (settings.Shape != WaterSurfaceShape::InfinitePlane)
            {
                minimumX = std::max(minimumX, -settings.HalfExtentXMetres);
                maximumX = std::min(maximumX, settings.HalfExtentXMetres);
                minimumZ = std::max(minimumZ, -settings.HalfExtentZMetres);
                maximumZ = std::min(maximumZ, settings.HalfExtentZMetres);
            }
            if (minimumX >= maximumX || minimumZ >= maximumZ)
            {
                continue;
            }
            const auto localOffset = rotation.Apply({minimumX, 0.0, minimumZ});
            WaterRenderPatch patch;
            patch.Origin =
                ToVector({relative[0] + localOffset[0], relative[1] + localOffset[1], relative[2] + localOffset[2]});
            patch.AxisX = ToVector(rotation.Apply({1.0, 0.0, 0.0}));
            patch.AxisY = ToVector(rotation.Apply({0.0, 1.0, 0.0}));
            patch.AxisZ = ToVector(rotation.Apply({0.0, 0.0, 1.0}));
            const auto cellsX = static_cast<std::uint32_t>(std::clamp(
                std::ceil((maximumX - minimumX) / settings.GridSpacingMetres), 1.0, double(MaximumCellsPerAxis)));
            const auto cellsZ = static_cast<std::uint32_t>(std::clamp(
                std::ceil((maximumZ - minimumZ) / settings.GridSpacingMetres), 1.0, double(MaximumCellsPerAxis)));
            patch.Grid = {static_cast<float>(maximumX - minimumX), static_cast<float>(maximumZ - minimumZ),
                          static_cast<float>(cellsX), static_cast<float>(cellsZ)};
            patch.VertexCount = cellsX * cellsZ * 6;
            if (!Math::IsFinite(patch.Grid) || OutsideFrustum(patch, viewProjection, maximumDisplacement))
            {
                continue;
            }
            if (settings.Shape == WaterSurfaceShape::Ellipse)
            {
                patch.Footprint = {static_cast<float>(minimumX / settings.HalfExtentXMetres),
                                   static_cast<float>(minimumZ / settings.HalfExtentZMetres),
                                   static_cast<float>(1.0 / settings.HalfExtentXMetres),
                                   static_cast<float>(1.0 / settings.HalfExtentZMetres)};
            }
            patch.Appearance = {settings.Roughness, settings.CrestFoamStrength, static_cast<float>(distance),
                                settings.Shape == WaterSurfaceShape::Ellipse ? 1.0F : 0.0F};
            if (!Math::IsFinite(patch.Footprint) || patch.Grid.X <= 0.0F || patch.Grid.Y <= 0.0F)
            {
                ++output.UnrepresentableBodies;
                continue;
            }
            patch.SurfaceColor = settings.SurfaceColor;
            patch.Waves =
                body->CreateWaveField()
                    .CaptureRenderPatch({minimumX, 0.0, minimumZ}, body->SimulationTimeSeconds().value_or(timeSeconds))
                    .Parameters;
            const Vector3 center{
                patch.Origin.X + patch.AxisX.X * patch.Grid.X * 0.5F + patch.AxisZ.X * patch.Grid.Y * 0.5F,
                patch.Origin.Y + patch.AxisX.Y * patch.Grid.X * 0.5F + patch.AxisZ.Y * patch.Grid.Y * 0.5F,
                patch.Origin.Z + patch.AxisX.Z * patch.Grid.X * 0.5F + patch.AxisZ.Z * patch.Grid.Y * 0.5F};
            patch.SortDepth = Math::TransformPoint(view, center).Z;
            output.Patches.push_back(patch);
        }
    }
} // namespace Keire::RenderBackend
