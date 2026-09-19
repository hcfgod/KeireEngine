#include "KeireClient/Editor/MaterialGraphCreationPicker.h"
#include "KeireInternal/Rendering/MaterialPropertyReflection.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    std::optional<Keire::ShaderInterfaceDefinition> MaterialShaderInterface(const Keire::ShaderGraphDefinition& graph)
    {
        return MaterialShaderInterface(Keire::Detail::ReflectMaterialProperties(graph, {}));
    }

    std::optional<Keire::ShaderInterfaceDefinition> MaterialShaderInterface(const Keire::ShaderAssetDefinition& shader)
    {
        Keire::ShaderInterfaceDefinition result;
        if (shader.ProgramTarget == "Material")
            result.Domain = Keire::ShaderInterfaceDomain::Surface;
        else if (shader.ProgramTarget == "VFX")
            result.Domain = Keire::ShaderInterfaceDomain::Vfx;
        else if (shader.ProgramTarget == "Fullscreen")
            result.Domain = Keire::ShaderInterfaceDomain::Fullscreen;
        else if (shader.ProgramTarget == "Custom Graphics")
            result.Domain = Keire::ShaderInterfaceDomain::CustomGraphicsPass;
        else
            return std::nullopt;
        result.Properties = shader.Properties;
        result.Keywords = shader.Keywords;
        return result;
    }

    namespace
    {
        [[nodiscard]] bool IsMaterialShader(const Keire::AssetSourceRecord& record) noexcept
        {
            return record.Type == Keire::ShaderGraphAsset::StaticType() ||
                   record.Type == Keire::ShaderAsset::StaticType();
        }
    } // namespace

    Keire::MaterialAuthoringDefinition CreateMaterialForShader(const Keire::AssetSourceRecord& shader,
                                                               const Keire::ShaderInterfaceDefinition& shaderInterface)
    {
        if (!shader.Id || !IsMaterialShader(shader))
            throw std::invalid_argument("Choose a Shader Graph or code Shader to create this material.");
        if (shaderInterface.Domain != Keire::ShaderInterfaceDomain::Surface)
            throw std::invalid_argument("A mesh material requires a surface shader.");
        Keire::MaterialShaderReference reference;
        reference.Asset = shader.Id;
        reference.Kind = shader.Type == Keire::ShaderGraphAsset::StaticType()
                             ? Keire::MaterialShaderSourceKind::ShaderGraph
                             : Keire::MaterialShaderSourceKind::ShaderAsset;
        Keire::MaterialAuthoringDefinition result;
        result.Shader = std::move(reference);
        // Leave defaults on the shader so subsequent shader default edits are inherited.
        return result;
    }

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
        const bool hasShader = std::ranges::any_of(records, IsMaterialShader);
        const AssetPickerOptions options{
            .Label = "Shader",
            .EmptyLabel = "Choose Shader Graph or raw Shader",
            .Filter = IsMaterialShader,
            .AllowNone = false,
        };
        (void)m_Picker.Draw(ui, records, m_Shader, options);
        ui.TextColored(theme.MutedText, "The shader defines the properties shown in the Material Inspector.");
        if (!hasShader)
            ui.TextColored(theme.Warning,
                           "Create a Surface / Lit or Surface / Unlit Shader Graph before creating a material.");
        if (!m_Picker.Diagnostic().empty())
            ui.TextColored(theme.Warning, m_Picker.Diagnostic());
    }

    void MaterialGraphCreationPicker::Reset() noexcept
    {
        m_Picker.Clear();
        m_Shader = {};
    }
} // namespace KeireEditor
