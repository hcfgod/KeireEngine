#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <filesystem>
#include <functional>
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

        void Open(std::span<const std::byte> source, const ShaderResolver& resolveShader);
        void OpenAsset(Keire::AssetId asset, std::filesystem::path sourcePath, std::span<const std::byte> source,
                       const ShaderResolver& resolveShader);
        [[nodiscard]] bool SetShader(Keire::AssetId shader, const ShaderResolver& resolveShader);
        [[nodiscard]] bool SetTexture(std::string_view property, Keire::AssetId texture);
        [[nodiscard]] bool SetProperty(std::string_view property, Keire::MaterialPropertyValue value);

        [[nodiscard]] Keire::AssetId Shader() const noexcept { return m_Definition.Shader; }
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

        [[nodiscard]] bool IsOpen(Keire::AssetId asset) const noexcept { return m_Asset == asset; }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept { return m_SourcePath; }
        [[nodiscard]] std::span<const std::byte> DraftSource() const noexcept { return m_DraftSource; }
        [[nodiscard]] std::span<const std::byte> BaselineSource() const noexcept { return m_BaselineSource; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }

      private:
        void SetResolvedShader(std::optional<Keire::ShaderAssetDefinition> definition);

        Keire::MaterialAssetDefinition m_Definition;
        std::optional<Keire::ShaderAssetDefinition> m_ShaderDefinition;
        std::vector<Keire::ShaderPropertyDefinition> m_TextureProperties;
        std::string m_LastChangedProperty;
        Keire::AssetId m_Asset;
        std::filesystem::path m_SourcePath;
        std::vector<std::byte> m_DraftSource;
        std::vector<std::byte> m_BaselineSource;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
