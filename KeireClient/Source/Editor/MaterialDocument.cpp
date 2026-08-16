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
        [[nodiscard]] bool IsTextureProperty(const Keire::MaterialPropertyValue& value)
        {
            return std::holds_alternative<Keire::AssetId>(value);
        }
    } // namespace

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
        if (authoring.Shader.Asset && !resolved)
            throw std::invalid_argument("The selected material shader source could not be resolved.");
        Keire::MaterialAssetDefinition runtime;
        runtime.SchemaVersion = 3;
        runtime.Shader = resolved ? resolved->RuntimeAsset : Keire::AssetId{};
        runtime.Surface = authoring.Surface;
        runtime.ContributeEmissionToGI = authoring.ContributeEmissionToGI;
        runtime.EmissiveGIIntensity = authoring.EmissiveGIIntensity;
        runtime.Properties = authoring.Properties;
        m_AuthoringDefinition = std::move(authoring);
        m_Definition = std::move(runtime);
        m_LastChangedProperty.clear();
        SetResolvedShader(resolved ? std::optional(std::move(resolved->Definition)) : std::nullopt);
        if (m_ShaderDefinition)
            Keire::ValidateMaterialAgainstShader(m_Definition, *m_ShaderDefinition);
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

        if (resolved)
        {
            std::erase_if(m_AuthoringDefinition.Properties,
                          [&](const auto& entry)
                          {
                              const auto property = std::ranges::find(resolved->Definition.Properties, entry.first,
                                                                      &Keire::ShaderPropertyDefinition::Name);
                              if (property == resolved->Definition.Properties.end())
                                  return true;
                              return (property->Type == Keire::ShaderPropertyType::Texture2D) !=
                                     IsTextureProperty(entry.second);
                          });
        }
        else
            m_AuthoringDefinition.Properties.clear();

        m_AuthoringDefinition.Shader = std::move(shader);
        m_Definition.Shader = resolved ? resolved->RuntimeAsset : Keire::AssetId{};
        m_Definition.Properties = m_AuthoringDefinition.Properties;
        m_LastChangedProperty = "$shader";
        SetResolvedShader(resolved ? std::optional(std::move(resolved->Definition)) : std::nullopt);
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
        replacement.Properties.insert_or_assign(std::string(property), value);
        Keire::ValidateMaterialAgainstShader(replacement, *m_ShaderDefinition);
        m_Definition = std::move(replacement);
        m_AuthoringDefinition.Properties.insert_or_assign(std::string(property), std::move(value));
        m_LastChangedProperty = property;
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
        m_AuthoringDefinition.Properties = m_Definition.Properties;
    }
} // namespace KeireEditor
