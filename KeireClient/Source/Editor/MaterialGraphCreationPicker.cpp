#include "KeireClient/Editor/MaterialGraphCreationPicker.h"

#include <algorithm>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IsMaterialShader(const Keire::AssetSourceRecord& record) noexcept
        {
            return record.Type == Keire::ShaderGraphAsset::StaticType() ||
                   record.Type == Keire::ShaderAsset::StaticType();
        }
    } // namespace

    void MaterialGraphCreationPicker::Begin(const Keire::AssetId selected,
                                            const std::span<const Keire::AssetSourceRecord> records)
    {
        Reset();
        const auto record = std::ranges::find(records, selected, &Keire::AssetSourceRecord::Id);
        if (record != records.end() && IsMaterialShader(*record))
            m_Shader = selected;
    }

    void MaterialGraphCreationPicker::Draw(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord> records,
                                           const Keire::UiThemeDefinition& theme)
    {
        const AssetPickerOptions options{
            .Label = "Shader",
            .EmptyLabel = "Choose Shader Graph or raw Shader",
            .Filter = IsMaterialShader,
            .AllowNone = false,
        };
        (void)m_Picker.Draw(ui, records, m_Shader, options);
        ui.TextColored(theme.MutedText, "The shader defines the Material Output inputs and runtime program.");
        if (!m_Picker.Diagnostic().empty())
            ui.TextColored(theme.Warning, m_Picker.Diagnostic());
    }

    void MaterialGraphCreationPicker::Reset() noexcept
    {
        m_Picker.Clear();
        m_Shader = {};
    }
} // namespace KeireEditor
