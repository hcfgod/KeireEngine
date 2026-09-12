#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/MaterialSelectionDocument.h"

#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    bool MaterialInspectorPanel::IsGeneratedShaderSource(const Keire::AssetSourceRecord& source,
                                                         const std::span<const Keire::AssetSourceRecord> records)
    {
        if (source.Type != Keire::ShaderAsset::StaticType())
            return false;
        const auto parent = source.RelativePath.lexically_normal().parent_path();
        auto directory = parent.parent_path().generic_string();
        auto owner = parent.filename().string();
        const auto lower = [](const unsigned char character) { return static_cast<char>(std::tolower(character)); };
        std::ranges::transform(directory, directory.begin(), lower);
        std::ranges::transform(owner, owner.begin(), lower);
        if (directory != "generated/shadergraphs")
            return false;
        return std::ranges::any_of(
            records, [&](const auto& record)
            { return record.Type == Keire::ShaderGraphAsset::StaticType() && record.Id.ToString() == owner; });
    }

    bool MaterialInspectorPanel::AcceptsSurfaceShaderGraph(const std::span<const std::byte> source)
    {
        try
        {
            return Keire::ShaderGraphAsset::DecodeSource(source).Target.Target == Keire::ShaderGraphTarget::Material;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

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
        return Draw(editor, document, nullptr);
    }

    bool MaterialInspectorPanel::Draw(IPropertyEditor& editor, MaterialSelectionDocument& selection) const
    {
        auto document = selection.Documents().front();
        return Draw(editor, document, &selection);
    }

    bool MaterialInspectorPanel::Draw(IPropertyEditor& editor, MaterialDocument& document,
                                      MaterialSelectionDocument* selection) const
    {
        bool changed = false;
        auto surface = document.Surface();
        std::int64_t alphaMode = static_cast<std::int64_t>(surface.AlphaMode);
        constexpr std::array<std::string_view, 7> alphaModes{
            "Opaque", "Mask", "Blend", "Additive", "Modulate", "Alpha Composite", "Alpha Holdout"};
        const auto surfaceLabel = [&](std::string_view label, const auto member)
        {
            if (!selection)
                return std::string(label);
            const bool mixed =
                selection && std::ranges::any_of(selection->Documents(), [&](const auto& selected)
                                                 { return selected.Surface().*member != surface.*member; });
            return std::string(label) + (mixed ? " (Mixed)###" : "###") + std::string(label);
        };
        const bool modeChanged = editor.EditChoice(
            surfaceLabel("Surface Mode", &Keire::MaterialSurfaceState::AlphaMode), alphaMode, alphaModes);
        bool cutoffChanged = false;
        if (modeChanged)
            surface.AlphaMode = static_cast<Keire::MaterialAlphaMode>(alphaMode);
        if (surface.AlphaMode == Keire::MaterialAlphaMode::Mask)
        {
            double cutoff = surface.AlphaCutoff;
            if (editor.EditScalar(surfaceLabel("Alpha Cutoff", &Keire::MaterialSurfaceState::AlphaCutoff), cutoff, 0.01,
                                  0.0, 1.0))
            {
                surface.AlphaCutoff = static_cast<float>(cutoff);
                cutoffChanged = true;
            }
        }
        const bool sidedChanged = editor.EditBoolean(
            surfaceLabel("Double Sided", &Keire::MaterialSurfaceState::DoubleSided), surface.DoubleSided);
        if (modeChanged || cutoffChanged || sidedChanged)
        {
            changed = selection
                          ? selection->SetSurface(modeChanged ? std::optional{surface.AlphaMode} : std::nullopt,
                                                  cutoffChanged ? std::optional{surface.AlphaCutoff} : std::nullopt,
                                                  sidedChanged ? std::optional{surface.DoubleSided} : std::nullopt)
                          : document.SetSurface(surface);
        }

        std::vector<const Keire::ShaderPropertyDefinition*> properties;
        for (const auto& property : document.Properties())
            properties.push_back(&property);
        std::stable_sort(properties.begin(), properties.end(),
                         [](const auto* left, const auto* right) { return left->Category < right->Category; });
        std::optional<std::string> category;
        for (const auto* declaration : properties)
        {
            const auto& property = *declaration;
            const auto selected =
                selection ? selection->Property(property) : std::optional<MaterialSelectionDocument::PropertyState>{};
            if (selection && !selected)
                continue;
            if (!category || *category != property.Category)
            {
                category = property.Category;
                editor.PropertyCategory(category->empty() ? "Properties" : *category);
            }
            const std::string label = (property.DisplayName.empty() ? property.Name : property.DisplayName) +
                                      (selected && selected->Mixed ? " (Mixed)" : "") + "###material-property-" +
                                      (property.Id ? property.Id.ToString() : property.Name);
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
            {
                auto& color = std::get<Keire::Color>(value);
                const auto outsideDisplayRange = [](float component) { return component < 0.0F || component > 1.0F; };
                const bool hdr = property.HighDynamicRange || outsideDisplayRange(color.Red) ||
                                 outsideDisplayRange(color.Green) || outsideDisplayRange(color.Blue) ||
                                 outsideDisplayRange(color.Alpha);
                edited = hdr ? editor.EditHdrColor(label, color) : editor.EditColor(label, color);
                break;
            }
            case Keire::ShaderPropertyType::Texture2D:
                edited = editor.EditTextureAsset(label, std::get<Keire::AssetId>(value), property.TextureSemantic);
                break;
            }
            editor.PropertyTooltip(property.Description);
            if (edited)
                changed = (selection ? selection->SetProperty(property, value)
                                     : document.SetProperty(property.Name, value)) ||
                          changed;
        }
        return changed;
    }
} // namespace KeireEditor
