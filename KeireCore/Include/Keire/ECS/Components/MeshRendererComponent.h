#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Component.h"
#include "Keire/Rendering/Lighting.h"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class KEIRE_API MeshRendererComponent final : public Component
    {
      public:
        MeshRendererComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b454952454d4553ULL, 0x4852454e44455201ULL));
        }

        [[nodiscard]] AssetId Mesh() const noexcept { return m_Mesh; }
        [[nodiscard]] AssetId Material() const noexcept { return m_Materials.empty() ? AssetId{} : m_Materials[0]; }
        [[nodiscard]] AssetId Material(std::size_t slot) const noexcept
        {
            return slot < m_Materials.size() ? m_Materials[slot] : AssetId{};
        }
        [[nodiscard]] std::span<const AssetId> Materials() const noexcept { return m_Materials; }
        [[nodiscard]] Color Tint() const noexcept { return m_Tint; }
        [[nodiscard]] bool Visible() const noexcept { return m_Visible; }
        [[nodiscard]] bool CastShadows() const noexcept { return m_CastShadows; }
        [[nodiscard]] bool ReceiveShadows() const noexcept { return m_ReceiveShadows; }
        [[nodiscard]] bool StaticLighting() const noexcept { return m_StaticLighting; }
        [[nodiscard]] GIReceiveMode GIReceive() const noexcept { return m_GIReceive; }
        [[nodiscard]] float LightmapScale() const noexcept { return m_LightmapScale; }
        [[nodiscard]] bool PreserveLightmapUVs() const noexcept { return m_PreserveLightmapUVs; }
        [[nodiscard]] const std::map<std::string, MaterialPropertyValue, std::less<>>&
        MaterialProperties() const noexcept
        {
            return m_MaterialProperties;
        }
        [[nodiscard]] const std::map<std::string, MaterialPropertyValue, std::less<>>&
        MaterialInstanceProperties(std::size_t slot) const noexcept;
        [[nodiscard]] const auto& AllMaterialInstanceProperties() const noexcept
        {
            return m_MaterialInstanceProperties;
        }

        void SetMesh(AssetId mesh);
        void SetMaterial(AssetId material);
        void SetMaterial(std::size_t slot, AssetId material);
        void SetMaterials(std::span<const AssetId> materials);
        void SetTint(Color tint);
        void SetVisible(bool visible);
        void SetCastShadows(bool enabled);
        void SetReceiveShadows(bool enabled);
        void SetStaticLighting(bool enabled);
        void SetGIReceive(GIReceiveMode value);
        void SetLightmapScale(float value);
        void SetPreserveLightmapUVs(bool enabled);
        void SetMaterialProperty(std::string name, MaterialPropertyValue value);
        [[nodiscard]] bool ResetMaterialProperty(std::string_view name);
        void ClearMaterialProperties();
        void SetMaterialInstanceProperty(std::size_t slot, std::string name, MaterialPropertyValue value);
        [[nodiscard]] bool ResetMaterialInstanceProperty(std::size_t slot, std::string_view name);
        void ClearMaterialInstanceProperties(std::size_t slot);
        void Reset();

      private:
        friend ComponentRegistration CreateMeshRendererComponentRegistration();
        AssetId m_Mesh;
        std::vector<AssetId> m_Materials;
        Color m_Tint{0.25F, 0.55F, 1.0F, 1.0F};
        bool m_Visible = true;
        bool m_CastShadows = true;
        bool m_ReceiveShadows = true;
        bool m_StaticLighting = false;
        GIReceiveMode m_GIReceive = GIReceiveMode::LightProbes;
        float m_LightmapScale = 1.0F;
        bool m_PreserveLightmapUVs = true;
        // Property blocks are transient runtime state and intentionally do not participate in scene serialization.
        std::map<std::string, MaterialPropertyValue, std::less<>> m_MaterialProperties;
        std::map<std::size_t, std::map<std::string, MaterialPropertyValue, std::less<>>> m_MaterialInstanceProperties;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateMeshRendererComponentRegistration();
} // namespace Keire
