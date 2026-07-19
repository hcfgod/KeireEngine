#pragma once

#include "Keire/ECS/Component.h"

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
        [[nodiscard]] AssetId Material() const noexcept { return m_Material; }
        [[nodiscard]] Color Tint() const noexcept { return m_Tint; }
        [[nodiscard]] bool Visible() const noexcept { return m_Visible; }

        void SetMesh(AssetId mesh);
        void SetMaterial(AssetId material);
        void SetTint(Color tint);
        void SetVisible(bool visible);
        void Reset();

      private:
        friend ComponentRegistration CreateMeshRendererComponentRegistration();
        AssetId m_Mesh;
        AssetId m_Material;
        Color m_Tint{0.25F, 0.55F, 1.0F, 1.0F};
        bool m_Visible = true;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateMeshRendererComponentRegistration();
} // namespace Keire
