#include "KeireInternal/Assets/BuiltinMeshes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846F;
        constexpr Color White{};

        using Geometry = std::pair<std::vector<MeshVertex>, std::vector<std::uint32_t>>;

        void AppendGridTriangles(std::vector<std::uint32_t>& indices, const std::uint32_t rows,
                                 const std::uint32_t columns, const bool reverse)
        {
            const auto stride = columns + 1;
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t column = 0; column < columns; ++column)
                {
                    const auto a = row * stride + column;
                    const auto b = a + 1;
                    const auto c = a + stride;
                    const auto d = c + 1;
                    if (reverse)
                        indices.insert(indices.end(), {a, c, b, b, c, d});
                    else
                        indices.insert(indices.end(), {a, b, c, b, d, c});
                }
        }

        void AppendLatLongTriangles(std::vector<std::uint32_t>& indices, const std::uint32_t rows,
                                    const std::uint32_t columns)
        {
            const auto stride = columns + 1;
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t column = 0; column < columns; ++column)
                {
                    const auto a = row * stride + column;
                    const auto b = a + 1;
                    const auto c = a + stride;
                    const auto d = c + 1;
                    if (row != 0)
                        indices.insert(indices.end(), {a, b, c});
                    if (row + 1 != rows)
                        indices.insert(indices.end(), {b, d, c});
                }
        }

        [[nodiscard]] Geometry CubeGeometry(const Color color)
        {
            struct Face final
            {
                Vector3 Normal;
                Vector4 Tangent;
                std::array<Vector3, 4> Positions;
            };
            constexpr std::array faces{
                Face{{0.0F, 0.0F, 1.0F},
                     {1.0F, 0.0F, 0.0F, 1.0F},
                     {{{-0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}}}},
                Face{{0.0F, 0.0F, -1.0F},
                     {-1.0F, 0.0F, 0.0F, 1.0F},
                     {{{0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}}}},
                Face{{1.0F, 0.0F, 0.0F},
                     {0.0F, 0.0F, -1.0F, 1.0F},
                     {{{0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}}},
                Face{{-1.0F, 0.0F, 0.0F},
                     {0.0F, 0.0F, 1.0F, 1.0F},
                     {{{-0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, -0.5F}}}},
                Face{{0.0F, 1.0F, 0.0F},
                     {1.0F, 0.0F, 0.0F, 1.0F},
                     {{{-0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}}}},
                Face{{0.0F, -1.0F, 0.0F},
                     {1.0F, 0.0F, 0.0F, -1.0F},
                     {{{-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, 0.5F}, {-0.5F, -0.5F, 0.5F}}}}};
            constexpr std::array uvs{Vector2{0.0F, 1.0F}, Vector2{1.0F, 1.0F}, Vector2{1.0F, 0.0F},
                                     Vector2{0.0F, 0.0F}};
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            vertices.reserve(24);
            indices.reserve(36);
            for (const auto& face : faces)
            {
                const auto base = static_cast<std::uint32_t>(vertices.size());
                for (std::size_t corner = 0; corner < face.Positions.size(); ++corner)
                    vertices.push_back({face.Positions[corner], face.Normal, uvs[corner], color, face.Tangent});
                indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            return {std::move(vertices), std::move(indices)};
        }

        [[nodiscard]] Geometry SphereGeometry()
        {
            constexpr std::uint32_t latitudeSegments = 16;
            constexpr std::uint32_t longitudeSegments = 24;
            std::vector<MeshVertex> vertices;
            vertices.reserve((latitudeSegments + 1) * (longitudeSegments + 1));
            for (std::uint32_t latitude = 0; latitude <= latitudeSegments; ++latitude)
            {
                const float v = static_cast<float>(latitude) / static_cast<float>(latitudeSegments);
                const float theta = v * Pi;
                const float ring = std::sin(theta);
                const float y = std::cos(theta);
                for (std::uint32_t longitude = 0; longitude <= longitudeSegments; ++longitude)
                {
                    const float u = static_cast<float>(longitude) / static_cast<float>(longitudeSegments);
                    const float phi = u * Pi * 2.0F;
                    const Vector3 normal{ring * std::cos(phi), y, ring * std::sin(phi)};
                    vertices.push_back({{normal.X * 0.5F, normal.Y * 0.5F, normal.Z * 0.5F},
                                        normal,
                                        {u, v},
                                        White,
                                        {-std::sin(phi), 0.0F, std::cos(phi), 1.0F}});
                }
            }
            std::vector<std::uint32_t> indices;
            AppendLatLongTriangles(indices, latitudeSegments, longitudeSegments);
            return {std::move(vertices), std::move(indices)};
        }

        [[nodiscard]] Geometry CapsuleGeometry()
        {
            constexpr std::uint32_t hemisphereSegments = 8;
            constexpr std::uint32_t longitudeSegments = 24;
            std::vector<MeshVertex> vertices;
            const auto appendRing = [&](const float angle, const float centerY, const float v)
            {
                const float ring = std::sin(angle);
                const float normalY = std::cos(angle);
                for (std::uint32_t longitude = 0; longitude <= longitudeSegments; ++longitude)
                {
                    const float u = static_cast<float>(longitude) / static_cast<float>(longitudeSegments);
                    const float phi = u * Pi * 2.0F;
                    const Vector3 normal{ring * std::cos(phi), normalY, ring * std::sin(phi)};
                    vertices.push_back({{normal.X * 0.25F, centerY + normal.Y * 0.25F, normal.Z * 0.25F},
                                        normal,
                                        {u, v},
                                        White,
                                        {-std::sin(phi), 0.0F, std::cos(phi), 1.0F}});
                }
            };
            for (std::uint32_t ring = 0; ring <= hemisphereSegments; ++ring)
            {
                const float amount = static_cast<float>(ring) / static_cast<float>(hemisphereSegments);
                appendRing(amount * Pi * 0.5F, 0.25F, amount * 0.25F);
            }
            for (std::uint32_t ring = 0; ring <= hemisphereSegments; ++ring)
            {
                const float amount = static_cast<float>(ring) / static_cast<float>(hemisphereSegments);
                appendRing(Pi * 0.5F + amount * Pi * 0.5F, -0.25F, 0.75F + amount * 0.25F);
            }
            std::vector<std::uint32_t> indices;
            AppendLatLongTriangles(indices, hemisphereSegments * 2 + 1, longitudeSegments);
            return {std::move(vertices), std::move(indices)};
        }

        [[nodiscard]] Geometry CylinderGeometry()
        {
            constexpr std::uint32_t segments = 24;
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float angle = u * Pi * 2.0F;
                const Vector3 normal{std::cos(angle), 0.0F, std::sin(angle)};
                const Vector4 tangent{-std::sin(angle), 0.0F, std::cos(angle), 1.0F};
                vertices.push_back({{normal.X * 0.5F, -0.5F, normal.Z * 0.5F}, normal, {u, 1.0F}, White, tangent});
                vertices.push_back({{normal.X * 0.5F, 0.5F, normal.Z * 0.5F}, normal, {u, 0.0F}, White, tangent});
            }
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const auto base = segment * 2;
                indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
            }
            for (const float y : {-0.5F, 0.5F})
            {
                const auto center = static_cast<std::uint32_t>(vertices.size());
                const Vector3 normal{0.0F, y < 0.0F ? -1.0F : 1.0F, 0.0F};
                vertices.push_back({{0.0F, y, 0.0F}, normal, {0.5F, 0.5F}, White});
                for (std::uint32_t segment = 0; segment <= segments; ++segment)
                {
                    const float angle = static_cast<float>(segment) / static_cast<float>(segments) * Pi * 2.0F;
                    const float x = std::cos(angle);
                    const float z = std::sin(angle);
                    vertices.push_back({{x * 0.5F, y, z * 0.5F}, normal, {x * 0.5F + 0.5F, z * 0.5F + 0.5F}, White});
                }
                for (std::uint32_t segment = 0; segment < segments; ++segment)
                {
                    const auto current = center + 1 + segment;
                    const auto next = current + 1;
                    if (y < 0.0F)
                        indices.insert(indices.end(), {center, current, next});
                    else
                        indices.insert(indices.end(), {center, next, current});
                }
            }
            return {std::move(vertices), std::move(indices)};
        }

        [[nodiscard]] Geometry ConeGeometry()
        {
            constexpr std::uint32_t segments = 24;
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float angle = u * Pi * 2.0F;
                const float x = std::cos(angle);
                const float z = std::sin(angle);
                const float inverseLength = 1.0F / std::sqrt(1.25F);
                const Vector3 normal{x * inverseLength, 0.5F * inverseLength, z * inverseLength};
                const Vector4 tangent{-z, 0.0F, x, 1.0F};
                vertices.push_back({{x * 0.5F, -0.5F, z * 0.5F}, normal, {u, 1.0F}, White, tangent});
                vertices.push_back({{0.0F, 0.5F, 0.0F}, normal, {u, 0.0F}, White, tangent});
            }
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const auto base = segment * 2;
                indices.insert(indices.end(), {base, base + 1, base + 2});
            }
            const auto center = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({{0.0F, -0.5F, 0.0F}, {0.0F, -1.0F, 0.0F}, {0.5F, 0.5F}, White});
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float angle = static_cast<float>(segment) / static_cast<float>(segments) * Pi * 2.0F;
                const float x = std::cos(angle);
                const float z = std::sin(angle);
                vertices.push_back(
                    {{x * 0.5F, -0.5F, z * 0.5F}, {0.0F, -1.0F, 0.0F}, {x * 0.5F + 0.5F, z * 0.5F + 0.5F}, White});
            }
            for (std::uint32_t segment = 0; segment < segments; ++segment)
                indices.insert(indices.end(), {center, center + 1 + segment, center + 2 + segment});
            return {std::move(vertices), std::move(indices)};
        }

        [[nodiscard]] Geometry PlaneGeometry(const bool vertical)
        {
            if (vertical)
            {
                std::vector<MeshVertex> vertices{
                    {{-0.5F, -0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}, White},
                    {{0.5F, -0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}, White},
                    {{0.5F, 0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}, White},
                    {{-0.5F, 0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}, White},
                };
                return {std::move(vertices), {0, 1, 2, 0, 2, 3}};
            }
            std::vector<MeshVertex> vertices{
                {{-0.5F, 0.0F, 0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}, White},
                {{0.5F, 0.0F, 0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}, White},
                {{0.5F, 0.0F, -0.5F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}, White},
                {{-0.5F, 0.0F, -0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}, White},
            };
            return {std::move(vertices), {0, 1, 2, 0, 2, 3}};
        }

        [[nodiscard]] Geometry TorusGeometry()
        {
            constexpr std::uint32_t majorSegments = 32;
            constexpr std::uint32_t minorSegments = 12;
            constexpr float majorRadius = 0.35F;
            constexpr float minorRadius = 0.15F;
            std::vector<MeshVertex> vertices;
            vertices.reserve((majorSegments + 1) * (minorSegments + 1));
            for (std::uint32_t major = 0; major <= majorSegments; ++major)
            {
                const float u = static_cast<float>(major) / static_cast<float>(majorSegments);
                const float majorAngle = u * Pi * 2.0F;
                for (std::uint32_t minor = 0; minor <= minorSegments; ++minor)
                {
                    const float v = static_cast<float>(minor) / static_cast<float>(minorSegments);
                    const float minorAngle = v * Pi * 2.0F;
                    const float radial = majorRadius + minorRadius * std::cos(minorAngle);
                    const Vector3 normal{std::cos(minorAngle) * std::cos(majorAngle), std::sin(minorAngle),
                                         std::cos(minorAngle) * std::sin(majorAngle)};
                    vertices.push_back({{radial * std::cos(majorAngle), minorRadius * std::sin(minorAngle),
                                         radial * std::sin(majorAngle)},
                                        normal,
                                        {u, v},
                                        White,
                                        {-std::sin(majorAngle), 0.0F, std::cos(majorAngle), 1.0F}});
                }
            }
            std::vector<std::uint32_t> indices;
            AppendGridTriangles(indices, majorSegments, minorSegments, true);
            return {std::move(vertices), std::move(indices)};
        }
    } // namespace

    Geometry CreateBuiltinMeshGeometry(const BuiltinMesh mesh)
    {
        switch (mesh)
        {
        case BuiltinMesh::Error:
            return CubeGeometry({1.0F, 0.0F, 1.0F, 1.0F});
        case BuiltinMesh::Cube:
            return CubeGeometry(White);
        case BuiltinMesh::Sphere:
            return SphereGeometry();
        case BuiltinMesh::Capsule:
            return CapsuleGeometry();
        case BuiltinMesh::Cylinder:
            return CylinderGeometry();
        case BuiltinMesh::Cone:
            return ConeGeometry();
        case BuiltinMesh::Plane:
            return PlaneGeometry(false);
        case BuiltinMesh::Quad:
            return PlaneGeometry(true);
        case BuiltinMesh::Torus:
            return TorusGeometry();
        }
        throw std::invalid_argument("Built-in mesh kind is invalid.");
    }
} // namespace Keire::Detail

namespace Keire
{
    std::span<const BuiltinMeshDescriptor> BuiltinMeshCatalog() noexcept
    {
        static constexpr std::array catalog{
            BuiltinMeshDescriptor{BuiltinMesh::Cube, MeshAsset::CubeId(), "Cube", "Box or convex collider", true},
            BuiltinMeshDescriptor{BuiltinMesh::Sphere, MeshAsset::SphereId(), "Sphere", "Sphere collider", true},
            BuiltinMeshDescriptor{BuiltinMesh::Capsule, MeshAsset::CapsuleId(), "Capsule", "Capsule collider", true},
            BuiltinMeshDescriptor{BuiltinMesh::Cylinder, MeshAsset::CylinderId(), "Cylinder", "Convex collider", true},
            BuiltinMeshDescriptor{BuiltinMesh::Cone, MeshAsset::ConeId(), "Cone", "Convex collider", true},
            BuiltinMeshDescriptor{BuiltinMesh::Plane, MeshAsset::PlaneId(), "Plane", "Static plane; no volume", false},
            BuiltinMeshDescriptor{BuiltinMesh::Quad, MeshAsset::QuadId(), "Quad", "Static plane; no volume", false},
            BuiltinMeshDescriptor{BuiltinMesh::Torus, MeshAsset::TorusId(), "Torus", "Static mesh or compound collider",
                                  true},
        };
        return catalog;
    }

    AssetId MeshAsset::BuiltinId(const BuiltinMesh mesh) noexcept
    {
        if (mesh == BuiltinMesh::Error)
            return ErrorId();
        const auto found = std::ranges::find(BuiltinMeshCatalog(), mesh, &BuiltinMeshDescriptor::Mesh);
        return found == BuiltinMeshCatalog().end() ? AssetId{} : found->Id;
    }

    std::optional<BuiltinMesh> MeshAsset::BuiltinKind(const AssetId id) noexcept
    {
        if (id == ErrorId())
            return BuiltinMesh::Error;
        const auto found = std::ranges::find(BuiltinMeshCatalog(), id, &BuiltinMeshDescriptor::Id);
        return found == BuiltinMeshCatalog().end() ? std::nullopt : std::optional(found->Mesh);
    }

    Ref<MeshAsset> MeshAsset::Builtin(const BuiltinMesh mesh) { return CreateRef<MeshAsset>(mesh); }

    Ref<MeshAsset> MeshAsset::ResolveBuiltin(const AssetId id)
    {
        const auto mesh = BuiltinKind(id);
        return mesh ? Builtin(*mesh) : Ref<MeshAsset>{};
    }

    Ref<MeshAsset> MeshAsset::Cube() { return Builtin(BuiltinMesh::Cube); }
    Ref<MeshAsset> MeshAsset::Sphere() { return Builtin(BuiltinMesh::Sphere); }
    Ref<MeshAsset> MeshAsset::Capsule() { return Builtin(BuiltinMesh::Capsule); }
    Ref<MeshAsset> MeshAsset::Cylinder() { return Builtin(BuiltinMesh::Cylinder); }
    Ref<MeshAsset> MeshAsset::Cone() { return Builtin(BuiltinMesh::Cone); }
    Ref<MeshAsset> MeshAsset::Plane() { return Builtin(BuiltinMesh::Plane); }
    Ref<MeshAsset> MeshAsset::Quad() { return Builtin(BuiltinMesh::Quad); }
    Ref<MeshAsset> MeshAsset::Torus() { return Builtin(BuiltinMesh::Torus); }
    Ref<MeshAsset> MeshAsset::Error() { return Builtin(BuiltinMesh::Error); }
} // namespace Keire
