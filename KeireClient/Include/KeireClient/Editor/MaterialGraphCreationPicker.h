#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "KeireClient/Editor/AssetPicker.h"

#include <optional>

namespace KeireEditor
{
    [[nodiscard]] std::optional<Keire::ShaderInterfaceDefinition>
    MaterialShaderInterface(const Keire::ShaderGraphDefinition& graph);
    [[nodiscard]] std::optional<Keire::ShaderInterfaceDefinition>
    MaterialShaderInterface(const Keire::ShaderAssetDefinition& shader);

    [[nodiscard]] Keire::MaterialAuthoringDefinition
    CreateMaterialForShader(const Keire::AssetSourceRecord& shader,
                            const Keire::ShaderInterfaceDefinition& shaderInterface);

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
