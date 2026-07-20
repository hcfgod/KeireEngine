#pragma once

#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"

namespace KeireEditor
{
    class MaterialInspectorPanel final
    {
      public:
        [[nodiscard]] bool Draw(IPropertyEditor& editor, MaterialDocument& document) const;
    };
} // namespace KeireEditor
