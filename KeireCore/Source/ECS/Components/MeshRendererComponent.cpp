#include "Keire/ECS/Components/MeshRendererComponent.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadMeshProperty(const ComponentPropertyBag& values, const std::string_view key,
                                         const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Mesh Renderer property has an incompatible type.");
        }

        [[nodiscard]] bool ValidTint(const Color color) noexcept
        {
            return Math::IsFinite(color) && color.Red >= 0.0F && color.Red <= 1.0F && color.Green >= 0.0F &&
                   color.Green <= 1.0F && color.Blue >= 0.0F && color.Blue <= 1.0F && color.Alpha >= 0.0F &&
                   color.Alpha <= 1.0F;
        }
    } // namespace

    MeshRendererComponent::MeshRendererComponent() : Component(StaticType()) {}

    void MeshRendererComponent::SetMesh(const AssetId mesh)
    {
        m_Mesh = mesh;
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterial(const AssetId material) { SetMaterial(0, material); }

    void MeshRendererComponent::SetMaterial(const std::size_t slot, const AssetId material)
    {
        if (slot >= 256U)
            throw std::out_of_range("Mesh Renderer material slot exceeds its limit.");
        if (m_Materials.size() <= slot)
            m_Materials.resize(slot + 1U);
        m_Materials[slot] = material;
        while (!m_Materials.empty() && !m_Materials.back())
            m_Materials.pop_back();
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterials(const std::span<const AssetId> materials)
    {
        if (materials.size() > 256U)
            throw std::invalid_argument("Mesh Renderer material overrides exceed their limit.");
        m_Materials.assign(materials.begin(), materials.end());
        while (!m_Materials.empty() && !m_Materials.back())
            m_Materials.pop_back();
        NotifyChanged();
    }

    void MeshRendererComponent::SetTint(const Color tint)
    {
        if (!ValidTint(tint))
            throw std::invalid_argument("Mesh Renderer tint must contain finite values in 0..1.");
        m_Tint = tint;
        NotifyChanged();
    }

    void MeshRendererComponent::SetVisible(const bool visible)
    {
        m_Visible = visible;
        NotifyChanged();
    }

    void MeshRendererComponent::SetCastShadows(const bool enabled)
    {
        m_CastShadows = enabled;
        NotifyChanged();
    }

    void MeshRendererComponent::SetReceiveShadows(const bool enabled)
    {
        m_ReceiveShadows = enabled;
        NotifyChanged();
    }

    void MeshRendererComponent::SetStaticLighting(const bool enabled)
    {
        m_StaticLighting = enabled;
        NotifyChanged();
    }

    void MeshRendererComponent::SetGIReceive(const GIReceiveMode value)
    {
        m_GIReceive = value;
        NotifyChanged();
    }

    void MeshRendererComponent::SetLightmapScale(const float value)
    {
        if (!std::isfinite(value) || value <= 0.0F || value > 64.0F)
            throw std::invalid_argument("Mesh Renderer lightmap scale must be in the range (0, 64].");
        m_LightmapScale = value;
        NotifyChanged();
    }

    void MeshRendererComponent::SetPreserveLightmapUVs(const bool enabled)
    {
        m_PreserveLightmapUVs = enabled;
        NotifyChanged();
    }

    void MeshRendererComponent::Reset()
    {
        m_Mesh = {};
        m_Materials.clear();
        m_Tint = {0.25F, 0.55F, 1.0F, 1.0F};
        m_Visible = true;
        m_CastShadows = true;
        m_ReceiveShadows = true;
        m_StaticLighting = false;
        m_GIReceive = GIReceiveMode::LightProbes;
        m_LightmapScale = 1.0F;
        m_PreserveLightmapUVs = true;
        NotifyChanged();
    }

    ComponentRegistration CreateMeshRendererComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = MeshRendererComponent::StaticType();
        result.Name = "Mesh Renderer";
        result.Category = "Rendering";
        result.SchemaVersion = 3;
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"mesh", "Mesh", "Rendering", ComponentPropertyKind::Asset, false, {}, {}, 0.1, MeshAsset::StaticType()},
            {"material",
             "Material",
             "Rendering",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             MaterialAsset::StaticType()},
            {"tint", "Tint", "Rendering", ComponentPropertyKind::Color},
            {"visible", "Visible", "Rendering", ComponentPropertyKind::Boolean},
            {"castShadows", "Cast Shadows", "Lighting", ComponentPropertyKind::Boolean},
            {"receiveShadows", "Receive Shadows", "Lighting", ComponentPropertyKind::Boolean},
            {"staticLighting", "Static Lighting", "Baked Lighting", ComponentPropertyKind::Boolean},
            {"giReceive", "Receive GI", "Baked Lighting", ComponentPropertyKind::Integer},
            {"lightmapScale", "Lightmap Scale", "Baked Lighting", ComponentPropertyKind::Scalar, false, 0.01, 64.0,
             0.05},
            {"preserveLightmapUvs", "Preserve Lightmap UVs", "Baked Lighting", ComponentPropertyKind::Boolean}};
        result.Factory = [] { return Ref<Component>(CreateRef<MeshRendererComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& renderer = dynamic_cast<const MeshRendererComponent&>(component);
            ComponentPropertyBag values{{"mesh", renderer.m_Mesh},
                                        {"material", renderer.Material()},
                                        {"tint", renderer.m_Tint},
                                        {"visible", renderer.m_Visible},
                                        {"castShadows", renderer.m_CastShadows},
                                        {"receiveShadows", renderer.m_ReceiveShadows},
                                        {"staticLighting", renderer.m_StaticLighting},
                                        {"giReceive", static_cast<std::int64_t>(renderer.m_GIReceive)},
                                        {"lightmapScale", static_cast<double>(renderer.m_LightmapScale)},
                                        {"preserveLightmapUvs", renderer.m_PreserveLightmapUVs}};
            for (std::size_t slot = 1; slot < renderer.m_Materials.size(); ++slot)
                values.emplace("material." + std::to_string(slot), renderer.m_Materials[slot]);
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 3)
                throw std::invalid_argument("Unsupported Mesh Renderer component schema version.");
            auto& renderer = dynamic_cast<MeshRendererComponent&>(component);
            renderer.SetMesh(ReadMeshProperty(values, "mesh", AssetId{}));
            renderer.SetMaterial(ReadMeshProperty(values, "material", AssetId{}));
            if (version >= 2)
            {
                for (std::size_t slot = 1; slot < 256U; ++slot)
                {
                    const auto found = values.find("material." + std::to_string(slot));
                    if (found == values.end())
                        continue;
                    if (const auto* material = std::get_if<AssetId>(&found->second))
                        renderer.SetMaterial(slot, *material);
                    else
                        throw std::invalid_argument("Mesh Renderer material override has an incompatible type.");
                }
            }
            renderer.SetTint(ReadMeshProperty(values, "tint", Color{0.25F, 0.55F, 1.0F, 1.0F}));
            renderer.SetVisible(ReadMeshProperty(values, "visible", true));
            renderer.SetCastShadows(ReadMeshProperty(values, "castShadows", true));
            renderer.SetReceiveShadows(ReadMeshProperty(values, "receiveShadows", true));
            renderer.SetStaticLighting(ReadMeshProperty(values, "staticLighting", false));
            const auto giReceive = ReadMeshProperty(values, "giReceive", std::int64_t{0});
            if (giReceive < 0 || giReceive > 2)
                throw std::invalid_argument("Mesh Renderer GI receive mode is invalid.");
            renderer.SetGIReceive(static_cast<GIReceiveMode>(giReceive));
            renderer.SetLightmapScale(static_cast<float>(ReadMeshProperty(values, "lightmapScale", 1.0)));
            renderer.SetPreserveLightmapUVs(ReadMeshProperty(values, "preserveLightmapUvs", true));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1 && version != 2)
                throw std::invalid_argument("Unsupported Mesh Renderer component schema migration.");
            auto migrated = values;
            if (version == 1)
            {
                migrated.emplace("castShadows", true);
                migrated.emplace("receiveShadows", true);
            }
            migrated.emplace("staticLighting", false);
            migrated.emplace("giReceive", std::int64_t{0});
            migrated.emplace("lightmapScale", 1.0);
            migrated.emplace("preserveLightmapUvs", true);
            return migrated;
        };
        return result;
    }
} // namespace Keire
