#pragma once

#include "Keire/Api.h"
#include "Keire/ECS/Component.h"

#include <cstdint>

namespace Keire
{
    class KEIRE_API UiDocumentComponent final : public Component
    {
      public:
        UiDocumentComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245554944ULL, 0x4f43554d454e5401ULL));
        }

        [[nodiscard]] AssetId VisualTree() const noexcept { return m_VisualTree; }
        [[nodiscard]] AssetId PanelSettings() const noexcept { return m_PanelSettings; }
        [[nodiscard]] std::int32_t SortingOrder() const noexcept { return m_SortingOrder; }
        [[nodiscard]] bool ReceivesInput() const noexcept { return m_ReceivesInput; }

        [[nodiscard]] AssetId Material() const noexcept { return m_Material; }

        void SetMaterial(AssetId value);
        void SetVisualTree(AssetId value);
        void SetPanelSettings(AssetId value);
        void SetSortingOrder(std::int32_t value);
        void SetReceivesInput(bool value);

      private:
        friend ComponentRegistration CreateUiDocumentComponentRegistration();
        AssetId m_VisualTree;
        AssetId m_PanelSettings;
        AssetId m_Material;
        std::int32_t m_SortingOrder = 0;
        bool m_ReceivesInput = true;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateUiDocumentComponentRegistration();
} // namespace Keire
