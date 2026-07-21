#include "KeireClient/Editor/MaterialInspectorPanel.h"

#include <optional>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    bool MaterialInspectorPanel::AcceptsTexture(const Keire::AssetSourceRecord& texture,
                                                const Keire::ShaderTextureSemantic semantic)
    {
        if (texture.Type != Keire::Texture2DAsset::StaticType())
            return false;
        if (semantic == Keire::ShaderTextureSemantic::Generic || texture.ImportSettings.empty())
            return true;
        const auto semanticSetting = texture.ImportSettings.find("semantic");
        const auto colorSpaceSetting = texture.ImportSettings.find("colorSpace");
        if (semanticSetting == texture.ImportSettings.end() || colorSpaceSetting == texture.ImportSettings.end())
            return true;
        const auto* importedSemantic = std::get_if<std::string>(&semanticSetting->second);
        const auto* importedColorSpace = std::get_if<std::string>(&colorSpaceSetting->second);
        if (!importedSemantic || !importedColorSpace)
            return false;
        if (semantic == Keire::ShaderTextureSemantic::BaseColor || semantic == Keire::ShaderTextureSemantic::Emissive)
            return *importedSemantic == "color" && *importedColorSpace == "srgb";
        if (semantic == Keire::ShaderTextureSemantic::Normal)
            return *importedSemantic == "normal" && *importedColorSpace == "linear";
        return *importedSemantic == "data" && *importedColorSpace == "linear";
    }

    bool MaterialInspectorPanel::Draw(IPropertyEditor& editor, MaterialDocument& document) const
    {
        bool changed = false;
        for (const auto& property : document.Properties())
        {
            const auto label =
                property.DisplayName.empty() ? std::string_view(property.Name) : std::string_view(property.DisplayName);
            auto value = document.Property(property.Name);
            bool edited = false;
            switch (property.Type)
            {
            case Keire::ShaderPropertyType::Scalar:
            {
                double scalar = std::get<float>(value);
                const auto minimum =
                    property.Minimum ? std::optional<double>(*property.Minimum) : std::optional<double>{};
                const auto maximum =
                    property.Maximum ? std::optional<double>(*property.Maximum) : std::optional<double>{};
                edited = editor.EditScalar(label, scalar, property.Step.value_or(0.01F), minimum, maximum);
                if (edited)
                    value = static_cast<float>(scalar);
                break;
            }
            case Keire::ShaderPropertyType::Vector2:
                edited = editor.EditVector2(label, std::get<Keire::Vector2>(value), property.Step.value_or(0.01F));
                break;
            case Keire::ShaderPropertyType::Vector3:
                edited = editor.EditVector3(label, std::get<Keire::Vector3>(value), property.Step.value_or(0.01F));
                break;
            case Keire::ShaderPropertyType::Vector4:
                edited = editor.EditVector4(label, std::get<Keire::Vector4>(value), property.Step.value_or(0.01F));
                break;
            case Keire::ShaderPropertyType::Color:
                edited = editor.EditColor(label, std::get<Keire::Color>(value));
                break;
            case Keire::ShaderPropertyType::Texture2D:
                edited = editor.EditTextureAsset(label, std::get<Keire::AssetId>(value), property.TextureSemantic);
                break;
            }
            if (edited)
                changed = document.SetProperty(property.Name, std::move(value)) || changed;
        }
        return changed;
    }
} // namespace KeireEditor
