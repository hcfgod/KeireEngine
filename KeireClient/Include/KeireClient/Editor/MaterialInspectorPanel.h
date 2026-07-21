#pragma once

#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"

namespace KeireEditor
{
    class MaterialInspectorPanel final
    {
      public:
        [[nodiscard]] static bool AcceptsTexture(const Keire::AssetSourceRecord& texture,
                                                 Keire::ShaderTextureSemantic semantic);
        [[nodiscard]] bool Draw(IPropertyEditor& editor, MaterialDocument& document) const;
    };
} // namespace KeireEditor
