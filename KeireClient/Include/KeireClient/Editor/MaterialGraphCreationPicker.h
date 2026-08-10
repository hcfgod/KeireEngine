#pragma once

#include "KeireClient/Editor/AssetPicker.h"

namespace KeireEditor
{
    class MaterialGraphCreationPicker final
    {
      public:
        void Begin(Keire::AssetId selected, std::span<const Keire::AssetSourceRecord> records);
        void Draw(Keire::UiFrame& ui, std::span<const Keire::AssetSourceRecord> records,
                  const Keire::UiThemeDefinition& theme);
        void Reset() noexcept;

        [[nodiscard]] Keire::AssetId Shader() const noexcept { return m_Shader; }

      private:
        AssetPicker m_Picker;
        Keire::AssetId m_Shader;
    };
} // namespace KeireEditor
