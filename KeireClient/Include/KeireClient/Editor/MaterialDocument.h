#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireEditor
{
    class MaterialDocument final
    {
      public:
        using ShaderResolver = std::function<std::optional<Keire::ShaderAssetDefinition>(Keire::AssetId)>;

        struct ResolvedShader final
        {
            Keire::AssetId RuntimeAsset;
            Keire::ShaderAssetDefinition Definition;
        };

        using ShaderReferenceResolver =
            std::function<std::optional<ResolvedShader>(const Keire::MaterialShaderReference&)>;

        [[nodiscard]] static bool IsPropertySource(std::span<const std::byte> source);

        /// Resolves an authored shader reference before publishing a saved or undo revision.
        /// An unavailable referenced shader leaves the last-good runtime revision active.
        [[nodiscard]] static std::optional<Keire::MaterialAssetDefinition> ResolveRuntimeRevision(
            std::span<const std::byte> source,
            const std::function<Keire::AssetId(const Keire::MaterialShaderReference&)>& resolveShader);
        [[nodiscard]] static std::optional<Keire::MaterialAssetDefinition>
        ResolveRuntimeRevision(std::span<const std::byte> source, const ShaderReferenceResolver& resolveShader);

        struct CatalogRefresh final
        {
            Keire::AssetId Asset;
            std::uint64_t Generation = 0;
        };

        void Open(std::span<const std::byte> source, const ShaderResolver& resolveShader);
        void Open(std::span<const std::byte> source, const ShaderReferenceResolver& resolveShader);
        void OpenAsset(Keire::AssetId asset, std::filesystem::path sourcePath, std::span<const std::byte> source,
                       const ShaderResolver& resolveShader);
        void OpenAsset(Keire::AssetId asset, std::filesystem::path sourcePath, std::span<const std::byte> source,
                       const ShaderReferenceResolver& resolveShader);
        [[nodiscard]] bool SetShader(Keire::AssetId shader, const ShaderResolver& resolveShader);
        [[nodiscard]] bool SetShaderReference(Keire::MaterialShaderReference shader,
                                              const ShaderReferenceResolver& resolveShader);
        [[nodiscard]] bool SetKeyword(const Keire::ShaderGraphKeyword& keyword, std::optional<std::string> value,
                                      const ShaderReferenceResolver& resolveShader);
        [[nodiscard]] bool ResetKeywords(const ShaderReferenceResolver& resolveShader);
        void RequestKeyword(const Keire::ShaderGraphKeyword& keyword, std::optional<std::string> value);
        void RequestKeywordReset();
        [[nodiscard]] bool ApplyPendingKeywords(const ShaderReferenceResolver& resolveShader);
        void CancelPendingKeywords() noexcept { m_PendingKeywords.reset(); }
        [[nodiscard]] bool HasPendingKeywords() const noexcept { return m_PendingKeywords.has_value(); }
        [[nodiscard]] const Keire::MaterialShaderReference& RequestedShaderReference() const noexcept
        {
            return m_PendingKeywords ? m_PendingKeywords->After : ShaderReference();
        }
        [[nodiscard]] bool SetTexture(std::string_view property, Keire::AssetId texture);
        [[nodiscard]] bool SetProperty(std::string_view property, Keire::MaterialPropertyValue value);
        [[nodiscard]] bool ResetProperty(std::string_view property);
        [[nodiscard]] bool ResetProperties();
        [[nodiscard]] std::vector<Keire::MaterialPropertyOverride> CopyProperties() const;
        /// Pastes matching stable identities (or legacy names), skipping incompatible values.
        [[nodiscard]] std::size_t PasteProperties(std::span<const Keire::MaterialPropertyOverride> properties);
        /// Explicit cross-shader paste using current code symbols, retaining destination identities.
        [[nodiscard]] std::size_t PastePropertiesByName(std::span<const Keire::MaterialPropertyOverride> properties);
        [[nodiscard]] bool RemoveInactiveProperties();
        [[nodiscard]] const std::multimap<std::string, Keire::MaterialPropertyValue, std::less<>>&
        InactiveProperties() const noexcept
        {
            return m_InactiveProperties;
        }
        [[nodiscard]] bool SetSurface(Keire::MaterialSurfaceState surface);

        [[nodiscard]] Keire::AssetId Shader() const noexcept { return m_AuthoringDefinition.Shader.Asset; }
        [[nodiscard]] bool HasResolvedShader() const noexcept { return m_ShaderDefinition.has_value(); }
        [[nodiscard]] const Keire::MaterialShaderReference& ShaderReference() const noexcept
        {
            return m_AuthoringDefinition.Shader;
        }
        [[nodiscard]] const Keire::MaterialSurfaceState& Surface() const noexcept { return m_Definition.Surface; }
        [[nodiscard]] Keire::AssetId Texture(std::string_view property) const;
        [[nodiscard]] Keire::MaterialPropertyValue Property(std::string_view property) const;
        [[nodiscard]] std::span<const Keire::ShaderPropertyDefinition> Properties() const noexcept;
        [[nodiscard]] std::span<const Keire::ShaderPropertyDefinition> TextureProperties() const noexcept
        {
            return m_TextureProperties;
        }
        [[nodiscard]] const Keire::MaterialAssetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] std::string_view LastChangedProperty() const noexcept { return m_LastChangedProperty; }
        [[nodiscard]] std::vector<std::byte> SaveSource() const;
        void CaptureDraft();
        void AcceptSavedSource(std::span<const std::byte> source);
        void RequestCatalogRefresh(Keire::AssetId asset) noexcept;
        void AdvanceCatalogRefresh(double seconds) noexcept;
        [[nodiscard]] std::optional<CatalogRefresh> PendingCatalogRefresh(bool force = false) const noexcept;
        void MarkCatalogRefreshQueued(std::uint64_t generation) noexcept;
        void MarkCatalogRefreshApplied(std::uint64_t generation) noexcept;
        void ResetCatalogRefresh() noexcept;

        [[nodiscard]] bool IsOpen(Keire::AssetId asset) const noexcept { return m_Asset == asset; }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return m_SourcePath; }
        [[nodiscard]] std::span<const std::byte> DraftSource() const noexcept { return m_DraftSource; }
        [[nodiscard]] std::span<const std::byte> BaselineSource() const noexcept { return m_BaselineSource; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }

      private:
        struct PendingKeywords
        {
            Keire::MaterialShaderReference Before;
            Keire::MaterialShaderReference After;
        };
        [[nodiscard]] static ShaderReferenceResolver AdaptShaderResolver(const ShaderResolver& resolveShader);
        void OpenDefinition(Keire::MaterialAuthoringDefinition definition,
                            const ShaderReferenceResolver& resolveShader);
        void SetResolvedShader(std::optional<Keire::ShaderAssetDefinition> definition);
        void ApplyPropertyResolution(Keire::MaterialPropertyResolution resolution);

        Keire::MaterialAuthoringDefinition m_AuthoringDefinition;
        std::optional<PendingKeywords> m_PendingKeywords;
        Keire::MaterialAssetDefinition m_Definition;
        std::optional<Keire::ShaderAssetDefinition> m_ShaderDefinition;
        std::vector<Keire::ShaderPropertyDefinition> m_TextureProperties;
        std::multimap<std::string, Keire::MaterialPropertyValue, std::less<>> m_InactiveProperties;
        std::vector<std::size_t> m_InactiveOverrideIndices;
        std::string m_LastChangedProperty;
        Keire::AssetId m_Asset;
        std::filesystem::path m_SourcePath;
        std::vector<std::byte> m_DraftSource;
        std::vector<std::byte> m_BaselineSource;
        std::uint64_t m_RefreshRequestedGeneration = 0;
        std::uint64_t m_RefreshQueuedGeneration = 0;
        std::uint64_t m_RefreshAppliedGeneration = 0;
        Keire::AssetId m_RefreshAsset;
        double m_RefreshDelaySeconds = 0.0;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
