#include "Keire/ECS/Components/MeshRendererComponent.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

        [[nodiscard]] bool FiniteMaterialProperty(const MaterialPropertyValue& value) noexcept
        {
            return std::visit(
                [](const auto& property) noexcept
                {
                    using Value = std::decay_t<decltype(property)>;
                    if constexpr (std::is_same_v<Value, AssetId>)
                        return true;
                    else if constexpr (std::is_same_v<Value, float>)
                        return std::isfinite(property);
                    else
                        return Math::IsFinite(property);
                },
                value);
        }

        void ValidateMaterialProperty(const std::string_view name, const MaterialPropertyValue& value)
        {
            constexpr std::size_t maximumPropertyNameBytes = 128;
            if (name.empty() || name.size() > maximumPropertyNameBytes)
                throw std::invalid_argument("Material property names must contain 1..128 UTF-8 bytes.");
            if (!FiniteMaterialProperty(value))
                throw std::invalid_argument("Material property values must be finite.");
        }

        const std::map<std::string, MaterialPropertyValue, std::less<>> EmptyMaterialProperties;
    } // namespace

    MeshRendererComponent::MeshRendererComponent() : Component(StaticType()), m_Mesh(MeshAsset::CubeId()) {}

    void MeshRendererComponent::SetMesh(const AssetId mesh)
    {
        // An empty renderer mesh has always resolved to the built-in cube at runtime. Store that semantic explicitly
        // so authoring tools can expose its material layout without querying an unavailable database record.
        m_Mesh = mesh ? mesh : MeshAsset::CubeId();
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterial(const AssetId material) { SetMaterial(0, material); }

    void MeshRendererComponent::SetMaterial(const std::size_t slot, const AssetId material)
    {
        if (slot >= 256U)
            throw std::out_of_range("Mesh Renderer material slot exceeds its limit.");
        if (Material(slot) == material)
            return;
        if (m_Materials.size() <= slot)
            m_Materials.resize(slot + 1U);
        m_Materials[slot] = material;
        m_MaterialInstanceProperties.erase(slot);
        while (!m_Materials.empty() && !m_Materials.back())
            m_Materials.pop_back();
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterials(const std::span<const AssetId> materials)
    {
        if (materials.size() > 256U)
            throw std::invalid_argument("Mesh Renderer material overrides exceed their limit.");
        if (std::ranges::equal(m_Materials, materials))
            return;
        m_Materials.assign(materials.begin(), materials.end());
        while (!m_Materials.empty() && !m_Materials.back())
            m_Materials.pop_back();
        m_MaterialInstanceProperties.clear();
        NotifyChanged();
    }

    const std::map<std::string, MaterialPropertyValue, std::less<>>&
    MeshRendererComponent::MaterialInstanceProperties(const std::size_t slot) const noexcept
    {
        const auto found = m_MaterialInstanceProperties.find(slot);
        return found == m_MaterialInstanceProperties.end() ? EmptyMaterialProperties : found->second;
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

    void MeshRendererComponent::SetMaterialProperty(std::string name, MaterialPropertyValue value)
    {
        constexpr std::size_t maximumPropertyCount = 64;
        ValidateMaterialProperty(name, value);
        const auto existing = m_MaterialProperties.find(name);
        if (existing == m_MaterialProperties.end() && m_MaterialProperties.size() >= maximumPropertyCount)
            throw std::length_error("Mesh Renderer material property block exceeds its 64-property limit.");
        if (existing != m_MaterialProperties.end() && existing->second == value)
            return;
        m_MaterialProperties.insert_or_assign(std::move(name), std::move(value));
        NotifyChanged();
    }

    bool MeshRendererComponent::ResetMaterialProperty(const std::string_view name)
    {
        const auto existing = m_MaterialProperties.find(name);
        if (existing == m_MaterialProperties.end())
            return false;
        m_MaterialProperties.erase(existing);
        NotifyChanged();
        return true;
    }

    void MeshRendererComponent::ClearMaterialProperties()
    {
        if (m_MaterialProperties.empty())
            return;
        m_MaterialProperties.clear();
        NotifyChanged();
    }

    void MeshRendererComponent::SetMaterialInstanceProperty(const std::size_t slot, std::string name,
                                                            MaterialPropertyValue value)
    {
        constexpr std::size_t maximumPropertyCount = 64;
        if (slot >= 256U)
            throw std::out_of_range("Dynamic Material Instance slot exceeds its limit.");
        ValidateMaterialProperty(name, value);
        auto& properties = m_MaterialInstanceProperties[slot];
        const auto existing = properties.find(name);
        if (existing == properties.end() && properties.size() >= maximumPropertyCount)
            throw std::length_error("Dynamic Material Instance exceeds its 64-property limit.");
        if (existing != properties.end() && existing->second == value)
            return;
        properties.insert_or_assign(std::move(name), std::move(value));
        NotifyChanged();
    }

    bool MeshRendererComponent::ResetMaterialInstanceProperty(const std::size_t slot, const std::string_view name)
    {
        const auto instance = m_MaterialInstanceProperties.find(slot);
        if (instance == m_MaterialInstanceProperties.end())
            return false;
        const auto existing = instance->second.find(name);
        if (existing == instance->second.end())
            return false;
        instance->second.erase(existing);
        if (instance->second.empty())
            m_MaterialInstanceProperties.erase(instance);
        NotifyChanged();
        return true;
    }

    void MeshRendererComponent::ClearMaterialInstanceProperties(const std::size_t slot)
    {
        if (m_MaterialInstanceProperties.erase(slot) == 0)
            return;
        NotifyChanged();
    }

    void MeshRendererComponent::Reset()
    {
        m_Mesh = MeshAsset::CubeId();
        m_Materials.clear();
        m_Tint = {0.25F, 0.55F, 1.0F, 1.0F};
        m_Visible = true;
        m_CastShadows = true;
        m_ReceiveShadows = true;
        m_StaticLighting = false;
        m_GIReceive = GIReceiveMode::LightProbes;
        m_LightmapScale = 1.0F;
        m_PreserveLightmapUVs = true;
        m_MaterialProperties.clear();
        m_MaterialInstanceProperties.clear();
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
