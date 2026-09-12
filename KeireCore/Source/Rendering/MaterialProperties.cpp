#include "Keire/Rendering/MaterialGraph.h"

#include "KeireInternal/Assets/RenderingAssetValidation.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <iterator>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] MaterialPropertyValue Normalize(MaterialPropertyValue value, const ShaderPropertyType type)
        {
            if (const auto* color = std::get_if<Color>(&value); color && type == ShaderPropertyType::Vector4)
                return Vector4{color->Red, color->Green, color->Blue, color->Alpha};
            if (const auto* packed = std::get_if<Vector4>(&value))
            {
                if (type == ShaderPropertyType::Vector2)
                    return Vector2{packed->X, packed->Y};
                if (type == ShaderPropertyType::Vector3)
                    return Vector3{packed->X, packed->Y, packed->Z};
                if (type == ShaderPropertyType::Color)
                    return Color{packed->X, packed->Y, packed->Z, packed->W};
            }
            return value;
        }
    } // namespace

    void ValidateMaterialAgainstShader(const MaterialAssetDefinition& material, const ShaderAssetDefinition& shader)
    {
        Detail::ValidateMaterialDefinition(material);
        Detail::ValidateShaderDefinition(shader, false, true);
        for (const auto& [name, value] : material.Properties)
        {
            const auto found = std::ranges::find(shader.Properties, name, &ShaderPropertyDefinition::Name);
            if (found == shader.Properties.end())
                throw std::invalid_argument("Material property is not declared by its shader: " + name);
            const bool correctType =
                (found->Type == ShaderPropertyType::Scalar && std::holds_alternative<float>(value)) ||
                (found->Type == ShaderPropertyType::Vector2 &&
                 (std::holds_alternative<Vector2>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Vector3 &&
                 (std::holds_alternative<Vector3>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Vector4 &&
                 (std::holds_alternative<Vector4>(value) || std::holds_alternative<Color>(value))) ||
                (found->Type == ShaderPropertyType::Color &&
                 (std::holds_alternative<Color>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Texture2D && std::holds_alternative<AssetId>(value));
            if (!correctType)
                throw std::invalid_argument("Material property type does not match its shader declaration: " + name);
            if (found->Type != ShaderPropertyType::Texture2D && (found->Minimum || found->Maximum))
            {
                std::array<float, 4> components{};
                std::size_t count = 0;
                std::visit(
                    [&](const auto& typed)
                    {
                        using T = std::decay_t<decltype(typed)>;
                        if constexpr (std::same_as<T, float>)
                        {
                            components[0] = typed;
                            count = 1;
                        }
                        else if constexpr (std::same_as<T, Vector2>)
                        {
                            components = {typed.X, typed.Y, 0.0F, 0.0F};
                            count = 2;
                        }
                        else if constexpr (std::same_as<T, Vector3>)
                        {
                            components = {typed.X, typed.Y, typed.Z, 0.0F};
                            count = 3;
                        }
                        else if constexpr (std::same_as<T, Vector4>)
                        {
                            components = {typed.X, typed.Y, typed.Z, typed.W};
                            count = 4;
                        }
                        else if constexpr (std::same_as<T, Color>)
                        {
                            components = {typed.Red, typed.Green, typed.Blue, typed.Alpha};
                            count = 4;
                        }
                    },
                    value);
                for (std::size_t component = 0; component < count; ++component)
                {
                    if ((found->Minimum && components[component] < *found->Minimum) ||
                        (found->Maximum && components[component] > *found->Maximum))
                        throw std::invalid_argument("Material property is outside its shader-declared range: " + name);
                }
            }
        }
    }

    MaterialPropertyResolution ResolveMaterialProperties(const MaterialAuthoringDefinition& material,
                                                         const ShaderAssetDefinition& shader)
    {
        (void)MaterialAsset::EncodeAuthoringSource(material);
        MaterialAssetDefinition candidate;
        ValidateMaterialAgainstShader(candidate, shader);
        MaterialPropertyResolution result;
        result.Authoring = material;
        auto& authoring = result.Authoring;
        std::vector<MaterialPropertyOverride> upgraded;
        const auto migrate = [&](const std::string& name, const MaterialPropertyValue& value)
        {
            const auto property = std::ranges::find(shader.Properties, name, &ShaderPropertyDefinition::Name);
            if (property == shader.Properties.end() || !property->Id)
                return false;
            upgraded.push_back({property->Id, name, value});
            return true;
        };
        // Legacy archives store newest first; canonical overrides store newest last.
        for (auto property = authoring.InactiveProperties.rbegin(); property != authoring.InactiveProperties.rend();)
        {
            if (migrate(property->first, property->second))
                property = decltype(property)(authoring.InactiveProperties.erase(std::next(property).base()));
            else
                ++property;
        }
        for (auto property = authoring.Properties.begin(); property != authoring.Properties.end();)
        {
            if (migrate(property->first, property->second))
                property = authoring.Properties.erase(property);
            else
                ++property;
        }
        upgraded.insert(upgraded.end(), authoring.PropertyOverrides.begin(), authoring.PropertyOverrides.end());
        authoring.PropertyOverrides = std::move(upgraded);
        if (!authoring.PropertyOverrides.empty())
            authoring.SchemaVersion = 5;

        const auto accept = [&](const ShaderPropertyDefinition& declaration, const MaterialPropertyValue& value)
        {
            candidate.Properties = {{declaration.Name, Normalize(value, declaration.Type)}};
            try
            {
                ValidateMaterialAgainstShader(candidate, shader);
            }
            catch (const std::invalid_argument&)
            {
                return false;
            }
            result.Properties.insert_or_assign(declaration.Name, candidate.Properties.begin()->second);
            return true;
        };
        for (auto property = authoring.Properties.begin(); property != authoring.Properties.end();)
        {
            const auto declaration =
                std::ranges::find(shader.Properties, property->first, &ShaderPropertyDefinition::Name);
            if (declaration != shader.Properties.end() && accept(*declaration, property->second))
            {
                property->second = result.Properties.at(property->first);
                ++property;
                continue;
            }
            const auto [first, last] = authoring.InactiveProperties.equal_range(property->first);
            if (std::none_of(first, last, [&](const auto& saved) { return saved.second == property->second; }))
                authoring.InactiveProperties.emplace_hint(first, property->first, property->second);
            property = authoring.Properties.erase(property);
        }
        for (auto property = authoring.InactiveProperties.begin(); property != authoring.InactiveProperties.end();)
        {
            const auto declaration =
                std::ranges::find(shader.Properties, property->first, &ShaderPropertyDefinition::Name);
            if (!result.Properties.contains(property->first) && declaration != shader.Properties.end() &&
                accept(*declaration, property->second))
            {
                authoring.Properties.emplace(property->first, result.Properties.at(property->first));
                property = authoring.InactiveProperties.erase(property);
            }
            else
                ++property;
        }
        result.InactiveProperties = authoring.InactiveProperties;
        std::set<AssetId> selected;
        for (std::size_t index = authoring.PropertyOverrides.size(); index > 0; --index)
        {
            auto& property = authoring.PropertyOverrides[index - 1];
            const auto declaration =
                std::ranges::find(shader.Properties, property.Property, &ShaderPropertyDefinition::Id);
            if (declaration != shader.Properties.end())
                property.Name = declaration->Name;
            if (declaration != shader.Properties.end() && !selected.contains(property.Property) &&
                accept(*declaration, property.Value))
                selected.insert(property.Property);
            else
            {
                result.InactiveProperties.emplace(property.Name, property.Value);
                result.InactiveOverrideIndices.push_back(index - 1);
            }
        }
        (void)MaterialAsset::EncodeAuthoringSource(authoring);
        return result;
    }
    void ValidateMaterialInstance(const MaterialInstanceDefinition& definition)
    {
        if (definition.SchemaVersion != MaterialInstanceSourceSchemaVersion || !definition.Parent)
            throw std::invalid_argument("Material Instance schema or parent is invalid.");
        MaterialAuthoringDefinition source;
        source.SchemaVersion = 5;
        source.Shader.Kind = MaterialShaderSourceKind::ShaderGraph;
        source.Shader.Asset = definition.Parent;
        source.Shader.Keywords = definition.KeywordOverrides;
        source.Properties = definition.Properties;
        source.PropertyOverrides = definition.PropertyOverrides;
        source.Surface = definition.Surface.value_or(MaterialSurfaceState{});
        source.ContributeEmissionToGI = definition.ContributeEmissionToGI.value_or(true);
        source.EmissiveGIIntensity = definition.EmissiveGIIntensity.value_or(1.0F);
        (void)MaterialAsset::EncodeAuthoringSource(source);
    }
} // namespace Keire
