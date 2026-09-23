#pragma once

#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include <cstddef>
#include <span>

namespace KeireEditor
{
    class MaterialSelectionDocument;
    class MaterialInspectorPanel final
    {
      public:
        [[nodiscard]] static bool AcceptsSurfaceShader(const Keire::ShaderAssetDefinition& shader) noexcept;
        [[nodiscard]] static bool AcceptsSurfaceShaderGraph(std::span<const std::byte> source);
        [[nodiscard]] static bool IsGeneratedShaderSource(const Keire::AssetSourceRecord& source,
                                                          std::span<const Keire::AssetSourceRecord> records);
        [[nodiscard]] static bool AcceptsTexture(const Keire::AssetSourceRecord& texture,
                                                 Keire::ShaderTextureSemantic semantic);
        [[nodiscard]] bool Draw(IPropertyEditor& editor, MaterialDocument& document) const;
        [[nodiscard]] bool Draw(IPropertyEditor& editor, MaterialSelectionDocument& selection) const;

      private:
        [[nodiscard]] bool Draw(IPropertyEditor& editor, MaterialDocument& document,
                                MaterialSelectionDocument* selection) const;
    };
} // namespace KeireEditor
