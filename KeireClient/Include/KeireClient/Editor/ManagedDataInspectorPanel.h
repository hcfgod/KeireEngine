#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Scripting/ManagedDataAsset.h"
#include "KeireClient/Editor/ManagedReferenceGraphInspector.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
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

        [[nodiscard]] static bool ApplyReferenceGraphEdit(ManagedReferenceGraphEditController& controller,
                                                          ManagedDataDocument& document,
                                                          const Keire::ManagedAssetPropertyDescriptor& property,
                                                          Keire::ManagedReferenceGraph value,
                                                          std::string_view undoName = "Edit Managed Reference Graph")
        {
            return controller.CommitPersistent(document, property, std::move(value), undoName);
        }
        [[nodiscard]] static bool FocusReferenceGraphObject(ManagedReferenceGraphEditController& controller,
                                                            std::uint32_t& focusedObject,
                                                            const Keire::ManagedReferenceGraph& graph,
                                                            std::uint32_t object)
        {
            return controller.Focus(focusedObject, graph, object);
        }

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
        ManagedReferenceGraphEditController m_GraphEdits;
        std::unordered_map<std::string, std::uint32_t> m_GraphFocus;
        Keire::AssetId m_Asset;
        std::uint64_t m_Revision = 0;
    };
} // namespace KeireEditor
