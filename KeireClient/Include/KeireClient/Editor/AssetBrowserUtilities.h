#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct AssetBrowserPreferences final
    {
        bool GridView = true;
        float ThumbnailSize = 88.0F;
    };

    struct AssetBrowserDropTarget final
    {
        Keire::UiItemRect Rect;
        std::filesystem::path Folder;
    };

    class AssetBrowserRecordViewCache final
    {
      public:
        [[nodiscard]] bool Refresh(std::span<const Keire::AssetSourceRecord> records, std::uint64_t revision,
                                   const std::filesystem::path& folder, std::string_view search);
        [[nodiscard]] std::span<const Keire::AssetSourceRecord* const> Records() const noexcept { return m_Records; }
        void Clear() noexcept;

      private:
        std::uint64_t m_Revision = 0;
        std::filesystem::path m_Folder;
        std::string m_Search;
        std::vector<const Keire::AssetSourceRecord*> m_Records;
        bool m_Initialized = false;
    };

    enum class AssetBrowserOpenAction : std::uint8_t
    {
        External,
        InputActions,
        AnimationGraph,
        AudioMixer,
        VfxEffect,
        Material,
        MaterialGraph,
        MaterialInstance,
        ShaderGraph,
        MaterialParameterCollection,
        Scene,
        Prefab
    };

    [[nodiscard]] std::string DisplayName(const std::filesystem::path& path);
    [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate);
    [[nodiscard]] AssetBrowserPreferences LoadAssetBrowserPreferences(const std::filesystem::path& path) noexcept;
    void SaveAssetBrowserPreferences(const std::filesystem::path& path,
                                     const AssetBrowserPreferences& preferences) noexcept;
    [[nodiscard]] std::vector<std::filesystem::path>
    DirectChildAssetFolders(std::span<const std::filesystem::path> folders, const std::filesystem::path& parent);
    [[nodiscard]] std::filesystem::path UniqueAssetBrowserFolder(const std::filesystem::path& assetRoot,
                                                                 std::filesystem::path desired);
    [[nodiscard]] std::filesystem::path UniqueAssetBrowserPath(const Keire::AssetSourceRecord& source,
                                                               const std::filesystem::path& folder,
                                                               const Keire::AssetDatabase& database);
    [[nodiscard]] std::filesystem::path ResolveAssetBrowserDropFolder(std::span<const AssetBrowserDropTarget> targets,
                                                                      Keire::UiPosition position,
                                                                      const std::filesystem::path& fallback);
    [[nodiscard]] std::string AssetTypeName(const Keire::AssetSourceRecord& record);
    [[nodiscard]] AssetBrowserOpenAction ResolveAssetBrowserOpenAction(const std::filesystem::path& path) noexcept;
    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(std::span<const std::byte> bytes);
    [[nodiscard]] std::string EncodeAssetPayload(std::span<const Keire::AssetId> assets);
} // namespace KeireEditor
