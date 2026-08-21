#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Scripting/ManagedDataAsset.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class UiFrame;
}

namespace KeireEditor
{
    class AssetPicker;
    class IInspectorController;
    class ManagedDataDocument;

    class ManagedDataInspectorPanel final
    {
      public:
        explicit ManagedDataInspectorPanel(IInspectorController& controller);
        ~ManagedDataInspectorPanel();

        void Draw(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record);
        void Clear() noexcept;

      private:
        bool DrawProperty(Keire::UiFrame& ui, Keire::ManagedAssetValueNode& value,
                          const Keire::ManagedAssetPropertyDescriptor& property,
                          std::span<const Keire::ManagedAssetTypeDescriptor> types);
        bool DrawValue(Keire::UiFrame& ui, Keire::ManagedAssetValueNode& value,
                       const Keire::ManagedAssetPropertyDescriptor& property,
                       std::span<const Keire::ManagedAssetTypeDescriptor> types);
        [[nodiscard]] std::optional<Keire::ManagedDataDefinition>
        LoadDefinition(const Keire::AssetSourceRecord& record) const;
        void DrawRawFallback(Keire::UiFrame& ui, const Keire::AssetSourceRecord& record, std::string_view reason) const;

        IInspectorController& m_Controller;
        std::unique_ptr<ManagedDataDocument> m_Document;
        std::unique_ptr<AssetPicker> m_AssetPicker;
        Keire::AssetId m_Asset;
        std::uint64_t m_Revision = 0;
    };
} // namespace KeireEditor
