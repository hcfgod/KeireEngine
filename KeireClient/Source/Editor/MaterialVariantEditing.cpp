#include "KeireClient/Editor/MaterialVariantEditing.h"

#include "Keire/Rendering/MaterialGraph.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    Keire::MaterialPropertyResolution ResolveMaterialVariantOverrides(const Keire::MaterialInstanceDefinition& instance,
                                                                      const Keire::ShaderAssetDefinition& shader)
    {
        Keire::MaterialAuthoringDefinition authored;
        authored.SchemaVersion = 5;
        authored.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
        authored.Shader.Asset = instance.Parent;
        authored.Properties = instance.Properties;
        authored.PropertyOverrides = instance.PropertyOverrides;
        return Keire::ResolveMaterialProperties(authored, shader);
    }

    bool RemoveInactiveMaterialVariantProperties(Keire::MaterialInstanceDefinition& instance,
                                                 const Keire::ShaderAssetDefinition& shader)
    {
        const auto resolved = ResolveMaterialVariantOverrides(instance, shader);
        if (resolved.InactiveProperties.empty())
            return false;
        auto replacement = instance;
        replacement.Properties = resolved.Authoring.Properties;
        replacement.PropertyOverrides.clear();
        for (std::size_t index = 0; index < resolved.Authoring.PropertyOverrides.size(); ++index)
            if (std::ranges::find(resolved.InactiveOverrideIndices, index) == resolved.InactiveOverrideIndices.end())
                replacement.PropertyOverrides.push_back(resolved.Authoring.PropertyOverrides[index]);
        instance = std::move(replacement);
        return true;
    }

    namespace
    {
        bool AcceptsPropertyValue(const Keire::ShaderPropertyDefinition& property,
                                  const Keire::MaterialPropertyValue& value)
        {
            Keire::ShaderAssetDefinition reflection;
            reflection.Source = "MaterialVariantProperty.hlsl";
            reflection.Properties.push_back(property);
            Keire::MaterialAssetDefinition material;
            material.Properties.emplace(property.Name, value);
            // Match the historical packed-vector conversion before validating component ranges.
            if (const auto* packed = std::get_if<Keire::Vector4>(&value))
            {
                if (property.Type == Keire::ShaderPropertyType::Vector2)
                    material.Properties[property.Name] = Keire::Vector2{packed->X, packed->Y};
                else if (property.Type == Keire::ShaderPropertyType::Vector3)
                    material.Properties[property.Name] = Keire::Vector3{packed->X, packed->Y, packed->Z};
            }
            try
            {
                Keire::ValidateMaterialAgainstShader(material, reflection);
                return true;
            }
            catch (const std::invalid_argument&)
            {
                return false;
            }
        }
    } // namespace

    bool HasMaterialVariantProperty(const Keire::MaterialInstanceDefinition& instance,
                                    const Keire::ShaderPropertyDefinition& property)
    {
        const auto legacy = instance.Properties.find(property.Name);
        if (legacy != instance.Properties.end() && AcceptsPropertyValue(property, legacy->second))
            return true;
        return property.Id &&
               std::ranges::any_of(
                   instance.PropertyOverrides, [&](const auto& saved)
                   { return saved.Property == property.Id && AcceptsPropertyValue(property, saved.Value); });
    }

    bool SetMaterialVariantProperty(Keire::MaterialInstanceDefinition& instance,
                                    const Keire::ShaderPropertyDefinition& property,
                                    std::optional<Keire::MaterialPropertyValue> value)
    {
        if (value && !AcceptsPropertyValue(property, *value))
            throw std::invalid_argument("The property override does not match the shader declaration.");
        auto replacement = instance;
        if (const auto legacy = replacement.Properties.find(property.Name);
            legacy != replacement.Properties.end() && AcceptsPropertyValue(property, legacy->second))
            replacement.Properties.erase(legacy);
        if (property.Id)
        {
            std::erase_if(replacement.PropertyOverrides, [&](const auto& saved)
                          { return saved.Property == property.Id && AcceptsPropertyValue(property, saved.Value); });
            if (value)
                replacement.PropertyOverrides.push_back({property.Id, property.Name, *value});
        }
        else if (value)
            replacement.Properties.insert_or_assign(property.Name, *value);
        if (replacement.Properties == instance.Properties &&
            replacement.PropertyOverrides == instance.PropertyOverrides)
            return false;
        instance = std::move(replacement);
        return true;
    }

    std::size_t PasteMaterialVariantProperties(Keire::MaterialInstanceDefinition& instance,
                                               const Keire::ShaderAssetDefinition& shader,
                                               const std::span<const Keire::MaterialPropertyOverride> properties)
    {
        auto replacement = instance;
        std::size_t changed = 0;
        for (const auto& saved : properties)
        {
            const auto property = std::ranges::find_if(
                shader.Properties, [&](const auto& declared)
                { return saved.Property ? declared.Id == saved.Property : declared.Name == saved.Name; });
            if (property == shader.Properties.end() || !AcceptsPropertyValue(*property, saved.Value))
                continue;
            if (SetMaterialVariantProperty(replacement, *property, saved.Value))
                ++changed;
        }
        if (replacement.Properties == instance.Properties &&
            replacement.PropertyOverrides == instance.PropertyOverrides)
            return 0;
        instance = std::move(replacement);
        return changed;
    }

    std::size_t PasteMaterialVariantPropertiesByName(Keire::MaterialInstanceDefinition& instance,
                                                     const Keire::ShaderAssetDefinition& shader,
                                                     const std::span<const Keire::MaterialPropertyOverride> properties)
    {
        std::vector<Keire::MaterialPropertyOverride> named(properties.begin(), properties.end());
        for (auto& property : named)
            property.Property = {};
        return PasteMaterialVariantProperties(instance, shader, named);
    }

    void ValidateMaterialVariantParent(const Keire::AssetId variant, Keire::AssetId parent,
                                       const MaterialVariantParentResolver& resolve)
    {
        if (!variant || !parent || !resolve)
            throw std::invalid_argument("Choose an available parent material.");
        std::set<Keire::AssetId> visited{variant};
        for (std::size_t depth = 0; depth < 16; ++depth)
        {
            if (!visited.insert(parent).second)
                throw std::invalid_argument("This parent would create a material inheritance cycle.");
            const auto ancestor = resolve(parent);
            if (!ancestor)
                throw std::invalid_argument("The selected parent's inheritance chain contains a missing material.");
            if (ancestor->Type == Keire::MaterialAsset::StaticType() ||
                ancestor->Type == Keire::MaterialGraphAsset::StaticType())
                return;
            if (ancestor->Type != Keire::MaterialInstanceAsset::StaticType() || !ancestor->Parent)
                throw std::invalid_argument("The selected parent does not inherit from a material.");
            parent = ancestor->Parent;
        }
        throw std::invalid_argument("Material inheritance exceeds the supported depth of 16.");
    }

    namespace
    {
        class MaterialVariantEdit final : public Keire::UndoCommand
        {
          public:
            MaterialVariantEdit(Keire::AssetId asset, std::vector<std::byte> before, std::vector<std::byte> after,
                                std::uint64_t serial, std::function<void(std::span<const std::byte>)> apply,
                                Keire::UndoAvailability available)
                : m_Asset(asset), m_Before(std::move(before)), m_After(std::move(after)), m_Serial(serial),
                  m_Apply(std::move(apply)), m_Available(std::move(available))
            {
                if (!m_Asset || !m_Apply)
                    throw std::invalid_argument("Material edits require an asset and source writer.");
            }

            std::string_view Name() const noexcept override { return "Edit Material Variant"; }
            std::size_t EstimatedBytes() const noexcept override { return m_Before.size() + m_After.size(); }
            bool Available() const noexcept override
            {
                try
                {
                    return !m_Available || m_Available();
                }
                catch (...)
                {
                    return false;
                }
            }
            void Redo() override { m_Apply(m_After); }
            void Undo() override { m_Apply(m_Before); }
            bool TryMerge(const Keire::UndoCommand& other) override
            {
                const auto* edit = dynamic_cast<const MaterialVariantEdit*>(&other);
                if (!edit || edit->m_Asset != m_Asset || edit->m_Serial != m_Serial || edit->m_Before != m_After)
                    return false;
                m_After = edit->m_After;
                return true;
            }

          private:
            Keire::AssetId m_Asset;
            std::vector<std::byte> m_Before;
            std::vector<std::byte> m_After;
            std::uint64_t m_Serial;
            std::function<void(std::span<const std::byte>)> m_Apply;
            Keire::UndoAvailability m_Available;
        };
    } // namespace

    std::unique_ptr<Keire::UndoCommand> CreateMaterialVariantEdit(Keire::AssetId asset, std::vector<std::byte> before,
                                                                  std::vector<std::byte> after,
                                                                  std::uint64_t editSerial,
                                                                  std::function<void(std::span<const std::byte>)> apply,
                                                                  Keire::UndoAvailability available)
    {
        return std::make_unique<MaterialVariantEdit>(asset, std::move(before), std::move(after), editSerial,
                                                     std::move(apply), std::move(available));
    }
} // namespace KeireEditor
