#include "KeireClient/Editor/MaterialDocument.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IsTextureProperty(const Keire::MaterialPropertyValue& value)
        {
            return std::holds_alternative<Keire::AssetId>(value);
        }
    } // namespace

    void MaterialDocument::Open(const std::span<const std::byte> source, const ShaderResolver& resolveShader)
    {
        auto definition = Keire::MaterialAsset::DecodeSource(source);
        auto shader =
            definition.Shader ? resolveShader(definition.Shader) : std::optional<Keire::ShaderAssetDefinition>{};
        m_Definition = std::move(definition);
        m_LastChangedProperty.clear();
        SetResolvedShader(std::move(shader));
    }

    bool MaterialDocument::SetShader(const Keire::AssetId shader, const ShaderResolver& resolveShader)
    {
        if (m_Definition.Shader == shader)
            return false;
        auto definition = shader ? resolveShader(shader) : std::optional<Keire::ShaderAssetDefinition>{};
        if (shader && !definition)
            throw std::invalid_argument("The selected shader asset could not be read.");

        if (definition)
        {
            std::erase_if(m_Definition.Properties,
                          [&](const auto& entry)
                          {
                              const auto property = std::ranges::find(definition->Properties, entry.first,
                                                                      &Keire::ShaderPropertyDefinition::Name);
                              if (property == definition->Properties.end())
                                  return true;
                              return (property->Type == Keire::ShaderPropertyType::Texture2D) !=
                                     IsTextureProperty(entry.second);
                          });
        }
        else
            m_Definition.Properties.clear();

        m_Definition.Shader = shader;
        m_LastChangedProperty = "$shader";
        SetResolvedShader(std::move(definition));
        return true;
    }

    bool MaterialDocument::SetTexture(const std::string_view property, const Keire::AssetId texture)
    {
        const auto declared = std::ranges::find(m_TextureProperties, property, &Keire::ShaderPropertyDefinition::Name);
        if (declared == m_TextureProperties.end())
            throw std::invalid_argument("The material shader does not declare that Texture2D property.");
        return SetProperty(property, texture);
    }

    bool MaterialDocument::SetProperty(const std::string_view property, Keire::MaterialPropertyValue value)
    {
        if (!m_ShaderDefinition)
            throw std::logic_error("The material has no resolved shader definition.");
        const auto declared =
            std::ranges::find(m_ShaderDefinition->Properties, property, &Keire::ShaderPropertyDefinition::Name);
        if (declared == m_ShaderDefinition->Properties.end())
            throw std::invalid_argument("The material shader does not declare that property.");
        if (const auto current = m_Definition.Properties.find(property);
            current != m_Definition.Properties.end() && current->second == value)
            return false;
        auto replacement = m_Definition;
        replacement.Properties.insert_or_assign(std::string(property), std::move(value));
        Keire::ValidateMaterialAgainstShader(replacement, *m_ShaderDefinition);
        m_Definition = std::move(replacement);
        m_LastChangedProperty = property;
        return true;
    }

    Keire::AssetId MaterialDocument::Texture(const std::string_view property) const
    {
        const auto declared = std::ranges::find(m_TextureProperties, property, &Keire::ShaderPropertyDefinition::Name);
        if (declared == m_TextureProperties.end())
            throw std::invalid_argument("The material shader does not declare that Texture2D property.");
        return m_Definition.Texture(property).value_or(declared->DefaultTexture);
    }

    Keire::MaterialPropertyValue MaterialDocument::Property(const std::string_view property) const
    {
        if (!m_ShaderDefinition)
            throw std::logic_error("The material has no resolved shader definition.");
        const auto declared =
            std::ranges::find(m_ShaderDefinition->Properties, property, &Keire::ShaderPropertyDefinition::Name);
        if (declared == m_ShaderDefinition->Properties.end())
            throw std::invalid_argument("The material shader does not declare that property.");
        if (const auto current = m_Definition.Properties.find(property); current != m_Definition.Properties.end())
            return current->second;
        switch (declared->Type)
        {
        case Keire::ShaderPropertyType::Scalar:
            return declared->DefaultValue.X;
        case Keire::ShaderPropertyType::Vector2:
            return Keire::Vector2{declared->DefaultValue.X, declared->DefaultValue.Y};
        case Keire::ShaderPropertyType::Vector3:
            return Keire::Vector3{declared->DefaultValue.X, declared->DefaultValue.Y, declared->DefaultValue.Z};
        case Keire::ShaderPropertyType::Vector4:
            return declared->DefaultValue;
        case Keire::ShaderPropertyType::Color:
            return Keire::Color{declared->DefaultValue.X, declared->DefaultValue.Y, declared->DefaultValue.Z,
                                declared->DefaultValue.W};
        case Keire::ShaderPropertyType::Texture2D:
            return declared->DefaultTexture;
        }
        throw std::logic_error("The material shader property type is invalid.");
    }

    std::span<const Keire::ShaderPropertyDefinition> MaterialDocument::Properties() const noexcept
    {
        return m_ShaderDefinition ? std::span<const Keire::ShaderPropertyDefinition>(m_ShaderDefinition->Properties)
                                  : std::span<const Keire::ShaderPropertyDefinition>{};
    }

    std::vector<std::byte> MaterialDocument::SaveSource() const
    {
        return Keire::MaterialAsset::EncodeSource(m_Definition);
    }

    void MaterialDocument::SetResolvedShader(std::optional<Keire::ShaderAssetDefinition> definition)
    {
        m_ShaderDefinition = std::move(definition);
        m_TextureProperties.clear();
        if (!m_ShaderDefinition)
            return;
        for (const auto& property : m_ShaderDefinition->Properties)
        {
            const auto current = m_Definition.Properties.find(property.Name);
            if (current == m_Definition.Properties.end())
                continue;
            const auto* packed = std::get_if<Keire::Vector4>(&current->second);
            if (!packed)
                continue;
            if (property.Type == Keire::ShaderPropertyType::Vector2)
                current->second = Keire::Vector2{packed->X, packed->Y};
            else if (property.Type == Keire::ShaderPropertyType::Vector3)
                current->second = Keire::Vector3{packed->X, packed->Y, packed->Z};
            else if (property.Type == Keire::ShaderPropertyType::Color)
                current->second = Keire::Color{packed->X, packed->Y, packed->Z, packed->W};
        }
        std::ranges::copy_if(m_ShaderDefinition->Properties, std::back_inserter(m_TextureProperties),
                             [](const Keire::ShaderPropertyDefinition& property)
                             { return property.Type == Keire::ShaderPropertyType::Texture2D; });
    }
} // namespace KeireEditor
