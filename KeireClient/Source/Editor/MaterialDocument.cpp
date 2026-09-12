#include "KeireClient/Editor/MaterialDocument.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        void ValidateKeywordSelection(const Keire::ShaderGraphKeyword& keyword, const std::optional<std::string>& value)
        {
            if (!keyword.Exposed || keyword.Name.empty())
                throw std::invalid_argument("Only exposed shader keywords can be edited by a material.");
            if (value &&
                (keyword.Options.empty() ? *value != "true" && *value != "false"
                                         : std::ranges::find(keyword.Options, *value) == keyword.Options.end()))
                throw std::invalid_argument("The shader keyword does not declare that option.");
        }

        [[nodiscard]] Keire::MaterialPropertyValue NormalizePropertyValue(Keire::MaterialPropertyValue value,
                                                                          const Keire::ShaderPropertyType type)
        {
            if (const auto* color = std::get_if<Keire::Color>(&value);
                color && type == Keire::ShaderPropertyType::Vector4)
                return Keire::Vector4{color->Red, color->Green, color->Blue, color->Alpha};
            if (const auto* packed = std::get_if<Keire::Vector4>(&value))
            {
                if (type == Keire::ShaderPropertyType::Vector2)
                    return Keire::Vector2{packed->X, packed->Y};
                if (type == Keire::ShaderPropertyType::Vector3)
                    return Keire::Vector3{packed->X, packed->Y, packed->Z};
                if (type == Keire::ShaderPropertyType::Color)
                    return Keire::Color{packed->X, packed->Y, packed->Z, packed->W};
            }
            return value;
        }

        bool AcceptsPropertyValue(const Keire::ShaderPropertyDefinition& property,
                                  const Keire::MaterialPropertyValue& value, const Keire::ShaderAssetDefinition& shader)
        {
            Keire::MaterialAssetDefinition candidate;
            candidate.Properties.emplace(property.Name, NormalizePropertyValue(value, property.Type));
            try
            {
                Keire::ValidateMaterialAgainstShader(candidate, shader);
                return true;
            }
            catch (const std::invalid_argument&)
            {
                return false;
            }
        }

        void ClearCompatibleOverrides(Keire::MaterialAuthoringDefinition& authoring,
                                      const Keire::ShaderPropertyDefinition& property,
                                      const Keire::ShaderAssetDefinition& shader)
        {
            authoring.Properties.erase(property.Name);
            std::erase_if(
                authoring.InactiveProperties, [&](const auto& saved)
                { return saved.first == property.Name && AcceptsPropertyValue(property, saved.second, shader); });
            if (property.Id)
                std::erase_if(
                    authoring.PropertyOverrides, [&](const auto& saved)
                    { return saved.Property == property.Id && AcceptsPropertyValue(property, saved.Value, shader); });
        }
    } // namespace

    bool MaterialDocument::IsPropertySource(const std::span<const std::byte> source)
    {
        try
        {
            (void)Keire::MaterialAsset::DecodeAuthoringSource(source);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    std::optional<Keire::MaterialAssetDefinition> MaterialDocument::ResolveRuntimeRevision(
        const std::span<const std::byte> source,
        const std::function<Keire::AssetId(const Keire::MaterialShaderReference&)>& resolveShader)
    {
        const auto authoring = Keire::MaterialAsset::DecodeAuthoringSource(source);
        if (!authoring.PropertyOverrides.empty())
            return std::nullopt;
        const auto shader = authoring.Shader.Asset ? resolveShader(authoring.Shader) : Keire::AssetId{};
        if (authoring.Shader.Asset && !shader)
            return std::nullopt;
        Keire::MaterialAssetDefinition result;
        result.Shader = shader;
        result.Surface = authoring.Surface;
        result.ContributeEmissionToGI = authoring.ContributeEmissionToGI;
        result.EmissiveGIIntensity = authoring.EmissiveGIIntensity;
        result.Properties = authoring.Properties;
        return result;
    }

    std::optional<Keire::MaterialAssetDefinition>
    MaterialDocument::ResolveRuntimeRevision(const std::span<const std::byte> source,
                                             const ShaderReferenceResolver& resolveShader)
    {
        MaterialDocument document;
        document.Open(source, resolveShader);
        if (document.Shader() && !document.HasResolvedShader())
            return std::nullopt;
        return document.Definition();
    }

    void MaterialDocument::Open(const std::span<const std::byte> source, const ShaderResolver& resolveShader)
    {
        Open(source, AdaptShaderResolver(resolveShader));
    }

    void MaterialDocument::Open(const std::span<const std::byte> source, const ShaderReferenceResolver& resolveShader)
    {
        OpenDefinition(Keire::MaterialAsset::DecodeAuthoringSource(source), resolveShader);
    }

    void MaterialDocument::OpenDefinition(Keire::MaterialAuthoringDefinition authoring,
                                          const ShaderReferenceResolver& resolveShader)
    {
        auto resolved = authoring.Shader.Asset ? resolveShader(authoring.Shader) : std::optional<ResolvedShader>{};
        // Keep the source editable while an import is pending or its shader has been removed.
        // Resolving a replacement remains transactional in SetShaderReference.
        Keire::MaterialAssetDefinition runtime;
        runtime.SchemaVersion = 3;
        runtime.Shader = resolved ? resolved->RuntimeAsset : Keire::AssetId{};
        runtime.Surface = authoring.Surface;
        runtime.ContributeEmissionToGI = authoring.ContributeEmissionToGI;
        runtime.EmissiveGIIntensity = authoring.EmissiveGIIntensity;
        runtime.Properties = authoring.Properties;
        auto replacement = *this;
        replacement.m_AuthoringDefinition = std::move(authoring);
        replacement.m_Definition = std::move(runtime);
        replacement.m_LastChangedProperty.clear();
        replacement.SetResolvedShader(resolved ? std::optional(std::move(resolved->Definition)) : std::nullopt);
        if (replacement.m_ShaderDefinition)
            Keire::ValidateMaterialAgainstShader(replacement.m_Definition, *replacement.m_ShaderDefinition);
        *this = std::move(replacement);
    }

    void MaterialDocument::OpenAsset(const Keire::AssetId asset, std::filesystem::path sourcePath,
                                     const std::span<const std::byte> source, const ShaderResolver& resolveShader)
    {
        OpenAsset(asset, std::move(sourcePath), source, AdaptShaderResolver(resolveShader));
    }

    void MaterialDocument::OpenAsset(const Keire::AssetId asset, std::filesystem::path sourcePath,
                                     const std::span<const std::byte> source,
                                     const ShaderReferenceResolver& resolveShader)
    {
        Open(source, resolveShader);
        m_Asset = asset;
        CancelPendingKeywords();
        m_SourcePath = std::move(sourcePath);
        m_DraftSource.assign(source.begin(), source.end());
        m_BaselineSource = m_DraftSource;
        m_Dirty = false;
    }

    bool MaterialDocument::SetShader(const Keire::AssetId shader, const ShaderResolver& resolveShader)
    {
        Keire::MaterialShaderReference reference;
        reference.Kind = Keire::MaterialShaderSourceKind::ShaderAsset;
        reference.Asset = shader;
        return SetShaderReference(std::move(reference), AdaptShaderResolver(resolveShader));
    }

    bool MaterialDocument::SetShaderReference(Keire::MaterialShaderReference shader,
                                              const ShaderReferenceResolver& resolveShader)
    {
        if (m_AuthoringDefinition.Shader == shader)
            return false;
        auto resolved = shader.Asset ? resolveShader(shader) : std::optional<ResolvedShader>{};
        if (shader.Asset && !resolved)
            throw std::invalid_argument("The selected shader source could not be read.");

        auto replacement = *this;
        auto& runtime = replacement.m_Definition;
        runtime.Shader = resolved ? resolved->RuntimeAsset : Keire::AssetId{};
        replacement.m_AuthoringDefinition.Shader = std::move(shader);
        replacement.m_LastChangedProperty = "$shader";
        replacement.SetResolvedShader(resolved ? std::optional(std::move(resolved->Definition)) : std::nullopt);
        *this = std::move(replacement);
        return true;
    }

    bool MaterialDocument::SetKeyword(const Keire::ShaderGraphKeyword& keyword, std::optional<std::string> value,
                                      const ShaderReferenceResolver& resolveShader)
    {
        ValidateKeywordSelection(keyword, value);
        auto shader = ShaderReference();
        if (value)
            shader.Keywords.insert_or_assign(keyword.Name, std::move(*value));
        else
            shader.Keywords.erase(keyword.Name);
        return SetShaderReference(std::move(shader), resolveShader);
    }

    bool MaterialDocument::ResetKeywords(const ShaderReferenceResolver& resolveShader)
    {
        auto shader = ShaderReference();
        shader.Keywords.clear();
        return SetShaderReference(std::move(shader), resolveShader);
    }

    void MaterialDocument::RequestKeyword(const Keire::ShaderGraphKeyword& keyword, std::optional<std::string> value)
    {
        ValidateKeywordSelection(keyword, value);
        auto pending = m_PendingKeywords.value_or(PendingKeywords{ShaderReference(), ShaderReference()});
        if (pending.Before != ShaderReference())
            pending = {ShaderReference(), ShaderReference()};
        if (value)
            pending.After.Keywords.insert_or_assign(keyword.Name, std::move(*value));
        else
            pending.After.Keywords.erase(keyword.Name);
        m_PendingKeywords = std::move(pending);
        if (m_PendingKeywords->After == ShaderReference())
            CancelPendingKeywords();
    }

    void MaterialDocument::RequestKeywordReset()
    {
        m_PendingKeywords = PendingKeywords{ShaderReference(), ShaderReference()};
        m_PendingKeywords->After.Keywords.clear();
        if (m_PendingKeywords->After == ShaderReference())
            CancelPendingKeywords();
    }

    bool MaterialDocument::ApplyPendingKeywords(const ShaderReferenceResolver& resolveShader)
    {
        if (!m_PendingKeywords)
            return false;
        if (m_PendingKeywords->Before != ShaderReference())
        {
            CancelPendingKeywords();
            return false;
        }
        const auto resolved = resolveShader(m_PendingKeywords->After);
        if (!resolved)
            return false;
        const bool changed = SetShaderReference(m_PendingKeywords->After, [&](const auto&) { return resolved; });
        CancelPendingKeywords();
        return changed;
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
        value = NormalizePropertyValue(std::move(value), declared->Type);
        if (const auto current = m_Definition.Properties.find(property);
            current != m_Definition.Properties.end() && current->second == value)
            return false;
        auto replacement = m_Definition;
        replacement.Properties.insert_or_assign(std::string(property), value);
        Keire::ValidateMaterialAgainstShader(replacement, *m_ShaderDefinition);
        auto authoring = m_AuthoringDefinition;
        if (declared->Id)
        {
            ClearCompatibleOverrides(authoring, *declared, *m_ShaderDefinition);
            authoring.PropertyOverrides.push_back({declared->Id, std::string(property), value});
            authoring.SchemaVersion = 5;
        }
        else
            authoring.Properties.insert_or_assign(std::string(property), value);
        ApplyPropertyResolution(Keire::ResolveMaterialProperties(authoring, *m_ShaderDefinition));
        m_LastChangedProperty = property;
        return true;
    }

    bool MaterialDocument::ResetProperty(const std::string_view property)
    {
        // Validate the property even when it has no override, matching SetProperty's contract.
        (void)Property(property);
        const auto existing = m_Definition.Properties.find(property);
        if (existing == m_Definition.Properties.end())
            return false;
        auto authoring = m_AuthoringDefinition;
        const auto declared =
            std::ranges::find(m_ShaderDefinition->Properties, property, &Keire::ShaderPropertyDefinition::Name);
        ClearCompatibleOverrides(authoring, *declared, *m_ShaderDefinition);
        ApplyPropertyResolution(Keire::ResolveMaterialProperties(authoring, *m_ShaderDefinition));
        m_LastChangedProperty = property;
        return true;
    }

    bool MaterialDocument::ResetProperties()
    {
        if (!m_ShaderDefinition)
            throw std::logic_error("Resolve the material shader before resetting its properties.");
        if (m_Definition.Properties.empty())
            return false;
        auto authoring = m_AuthoringDefinition;
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            const auto declared =
                std::ranges::find(m_ShaderDefinition->Properties, name, &Keire::ShaderPropertyDefinition::Name);
            if (declared != m_ShaderDefinition->Properties.end())
                ClearCompatibleOverrides(authoring, *declared, *m_ShaderDefinition);
        }
        authoring.Properties.clear();
        ApplyPropertyResolution(Keire::ResolveMaterialProperties(authoring, *m_ShaderDefinition));
        m_LastChangedProperty = "$properties";
        return true;
    }

    std::vector<Keire::MaterialPropertyOverride> MaterialDocument::CopyProperties() const
    {
        if (!m_ShaderDefinition)
            throw std::logic_error("Resolve the material shader before copying its properties.");
        std::vector<Keire::MaterialPropertyOverride> result;
        result.reserve(Properties().size());
        for (const auto& property : Properties())
            result.push_back({property.Id, property.Name, Property(property.Name)});
        return result;
    }

    std::size_t MaterialDocument::PasteProperties(const std::span<const Keire::MaterialPropertyOverride> properties)
    {
        if (!m_ShaderDefinition)
            throw std::logic_error("Resolve the material shader before pasting its properties.");
        auto replacement = *this;
        std::size_t changed = 0;
        for (const auto& saved : properties)
        {
            const auto declared = std::ranges::find_if(
                m_ShaderDefinition->Properties, [&](const auto& property)
                { return saved.Property ? property.Id == saved.Property : property.Name == saved.Name; });
            if (declared == m_ShaderDefinition->Properties.end() ||
                !AcceptsPropertyValue(*declared, saved.Value, *m_ShaderDefinition))
                continue;
            if (replacement.SetProperty(declared->Name, saved.Value))
                ++changed;
        }
        if (changed)
        {
            replacement.m_LastChangedProperty = "$properties";
            *this = std::move(replacement);
        }
        return changed;
    }

    std::size_t
    MaterialDocument::PastePropertiesByName(const std::span<const Keire::MaterialPropertyOverride> properties)
    {
        std::vector<Keire::MaterialPropertyOverride> named(properties.begin(), properties.end());
        for (auto& property : named)
            property.Property = {};
        return PasteProperties(named);
    }

    bool MaterialDocument::RemoveInactiveProperties()
    {
        if (m_InactiveProperties.empty())
            return false;
        m_AuthoringDefinition.InactiveProperties.clear();
        std::size_t index = 0;
        std::erase_if(
            m_AuthoringDefinition.PropertyOverrides, [&](const auto&)
            { return std::ranges::find(m_InactiveOverrideIndices, index++) != m_InactiveOverrideIndices.end(); });
        m_InactiveProperties.clear();
        m_InactiveOverrideIndices.clear();
        m_LastChangedProperty = "$inactiveProperties";
        return true;
    }

    bool MaterialDocument::SetSurface(const Keire::MaterialSurfaceState surface)
    {
        if (surface.AlphaMode > Keire::MaterialAlphaMode::AlphaHoldout || !std::isfinite(surface.AlphaCutoff) ||
            surface.AlphaCutoff < 0.0F || surface.AlphaCutoff > 1.0F)
            throw std::invalid_argument("Material surface state is invalid.");
        if (m_Definition.Surface == surface)
            return false;
        m_Definition.Surface = surface;
        m_AuthoringDefinition.Surface = surface;
        m_LastChangedProperty = "$surface";
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
        return Keire::MaterialAsset::EncodeAuthoringSource(m_AuthoringDefinition);
    }

    void MaterialDocument::CaptureDraft()
    {
        m_DraftSource = SaveSource();
        m_Dirty = m_DraftSource != m_BaselineSource;
    }

    void MaterialDocument::AcceptSavedSource(const std::span<const std::byte> source)
    {
        m_DraftSource.assign(source.begin(), source.end());
        m_BaselineSource = m_DraftSource;
        m_Dirty = false;
    }

    void MaterialDocument::RequestCatalogRefresh(const Keire::AssetId asset) noexcept
    {
        if (m_RefreshRequestedGeneration <= m_RefreshQueuedGeneration)
            m_RefreshAsset = asset;
        else if (m_RefreshAsset != asset)
            m_RefreshAsset = {};
        ++m_RefreshRequestedGeneration;
        m_RefreshDelaySeconds = 0.0;
    }

    void MaterialDocument::AdvanceCatalogRefresh(const double seconds) noexcept
    {
        if (seconds > 0.0 && m_RefreshQueuedGeneration < m_RefreshRequestedGeneration)
            m_RefreshDelaySeconds += seconds;
    }

    std::optional<MaterialDocument::CatalogRefresh>
    MaterialDocument::PendingCatalogRefresh(const bool force) const noexcept
    {
        if (m_RefreshQueuedGeneration >= m_RefreshRequestedGeneration || (!force && m_RefreshDelaySeconds < 0.15))
            return std::nullopt;
        return CatalogRefresh{m_RefreshAsset, m_RefreshRequestedGeneration};
    }

    void MaterialDocument::MarkCatalogRefreshQueued(const std::uint64_t generation) noexcept
    {
        m_RefreshQueuedGeneration = std::max(m_RefreshQueuedGeneration, generation);
        if (generation >= m_RefreshRequestedGeneration)
            m_RefreshAsset = {};
        m_RefreshDelaySeconds = 0.0;
    }

    void MaterialDocument::MarkCatalogRefreshApplied(const std::uint64_t generation) noexcept
    {
        m_RefreshAppliedGeneration = std::max(m_RefreshAppliedGeneration, generation);
    }

    void MaterialDocument::ResetCatalogRefresh() noexcept
    {
        m_RefreshAppliedGeneration = m_RefreshRequestedGeneration;
        m_RefreshQueuedGeneration = m_RefreshRequestedGeneration;
        m_RefreshAsset = {};
        m_RefreshDelaySeconds = 0.0;
    }

    MaterialDocument::ShaderReferenceResolver MaterialDocument::AdaptShaderResolver(const ShaderResolver& resolveShader)
    {
        return [resolveShader](const Keire::MaterialShaderReference& reference) -> std::optional<ResolvedShader>
        {
            if (reference.Kind == Keire::MaterialShaderSourceKind::ShaderGraph)
                return std::nullopt;
            const auto definition = resolveShader(reference.Asset);
            return definition ? std::optional(ResolvedShader{reference.Asset, *definition}) : std::nullopt;
        };
    }

    void MaterialDocument::SetResolvedShader(std::optional<Keire::ShaderAssetDefinition> definition)
    {
        m_ShaderDefinition = std::move(definition);
        m_TextureProperties.clear();
        m_InactiveProperties = m_AuthoringDefinition.InactiveProperties;
        m_InactiveOverrideIndices.clear();
        if (!m_ShaderDefinition)
        {
            for (std::size_t index = 0; index < m_AuthoringDefinition.PropertyOverrides.size(); ++index)
            {
                const auto& property = m_AuthoringDefinition.PropertyOverrides[index];
                m_InactiveProperties.emplace(property.Name, property.Value);
                m_InactiveOverrideIndices.push_back(index);
            }
            return;
        }
        ApplyPropertyResolution(Keire::ResolveMaterialProperties(m_AuthoringDefinition, *m_ShaderDefinition));
        std::ranges::copy_if(m_ShaderDefinition->Properties, std::back_inserter(m_TextureProperties),
                             [](const Keire::ShaderPropertyDefinition& property)
                             { return property.Type == Keire::ShaderPropertyType::Texture2D; });
    }

    void MaterialDocument::ApplyPropertyResolution(Keire::MaterialPropertyResolution resolved)
    {
        m_AuthoringDefinition = std::move(resolved.Authoring);
        m_Definition.Properties = std::move(resolved.Properties);
        m_InactiveProperties = std::move(resolved.InactiveProperties);
        m_InactiveOverrideIndices = std::move(resolved.InactiveOverrideIndices);
    }
} // namespace KeireEditor
