#pragma once

#include "Keire/ECS/Component.h"

#include <span>
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

        void SetMesh(AssetId mesh);
        void SetMaterial(AssetId material);
        void SetMaterial(std::size_t slot, AssetId material);
        void SetMaterials(std::span<const AssetId> materials);
        void SetTint(Color tint);
        void SetVisible(bool visible);
        void SetCastShadows(bool enabled);
        void SetReceiveShadows(bool enabled);
        void Reset();

      private:
        friend ComponentRegistration CreateMeshRendererComponentRegistration();
        AssetId m_Mesh;
        std::vector<AssetId> m_Materials;
        Color m_Tint{0.25F, 0.55F, 1.0F, 1.0F};
        bool m_Visible = true;
        bool m_CastShadows = true;
        bool m_ReceiveShadows = true;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateMeshRendererComponentRegistration();
} // namespace Keire
