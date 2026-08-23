#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class IAssetBrowserController;

    enum class AssetBrowserClipboardMode : std::uint8_t
    {
        Empty,
        Copy,
        Cut
    };

    struct AssetBrowserClipboardEntry final
    {
        Keire::AssetId Asset;
        std::filesystem::path Folder;
    };

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

    struct ManagedScriptAssemblyCandidate final
    {
        Keire::AssetId Asset;
        Keire::ManagedAssemblyDefinition Definition;
    };

    struct ManagedScriptPlacement final
    {
        Keire::AssetId Assembly;
        std::string RootNamespace;
        std::filesystem::path SourceRootToAdd;
    };

    struct AssetBrowserHierarchyEntry final
    {
        const Keire::AssetSourceRecord* Record = nullptr;
        std::size_t Depth = 0;
        bool HasChildren = false;
    };

    enum class ManagedScriptTemplateKind : std::uint8_t
    {
        Behaviour,
        ScriptableObject
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
    [[nodiscard]] std::string ElideAssetDisplayName(std::string_view name, float maximumWidth,
                                                    const std::function<float(std::string_view)>& measureText);
    [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate);
    [[nodiscard]] AssetBrowserPreferences LoadAssetBrowserPreferences(const std::filesystem::path& path) noexcept;
    void SaveAssetBrowserPreferences(const std::filesystem::path& path,
                                     const AssetBrowserPreferences& preferences) noexcept;
    [[nodiscard]] std::vector<std::filesystem::path>
    DirectChildAssetFolders(std::span<const std::filesystem::path> folders, const std::filesystem::path& parent);
    [[nodiscard]] std::vector<AssetBrowserHierarchyEntry>
    BuildAssetBrowserHierarchy(std::span<const Keire::AssetSourceRecord* const> records,
                               std::span<const Keire::AssetId> expandedParents, bool expandAll = false);
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
    [[nodiscard]] Keire::AssetId DecodeSingleAssetPayload(std::span<const std::byte> bytes);
    [[nodiscard]] std::string EncodeAssetPayload(std::span<const Keire::AssetId> assets);
    [[nodiscard]] ManagedScriptPlacement
    ResolveManagedScriptPlacement(std::span<const ManagedScriptAssemblyCandidate> assemblies,
                                  const std::filesystem::path& selectedAssetFolder);
    [[nodiscard]] std::string BuildManagedScriptSource(ManagedScriptTemplateKind kind, std::string_view rootNamespace,
                                                       std::string_view typeName, Keire::AssetId stableTypeId);
    [[nodiscard]] bool ExtendManagedAssemblySourceRoots(Keire::ManagedAssemblyDefinition& assembly,
                                                        const std::filesystem::path& sourceRoot);
    [[nodiscard]] std::vector<std::filesystem::path>
    BuildFolderRangeSelection(std::span<const std::filesystem::path> order, const std::filesystem::path& anchor,
                              const std::filesystem::path& target, std::span<const std::filesystem::path> existing = {},
                              bool additive = false);
    void SelectAssetBrowserAsset(std::vector<Keire::AssetId>& selection,
                                 std::vector<std::filesystem::path>& folderSelection, Keire::AssetId& anchor,
                                 Keire::AssetId asset, bool additive, IAssetBrowserController& controller);
    [[nodiscard]] std::vector<std::filesystem::path> DecodeFolderPayload(std::span<const std::byte> bytes);
    [[nodiscard]] std::string EncodeFolderPayload(std::span<const std::filesystem::path> folders);
    void DuplicateAssetBrowserFolders(std::span<const std::filesystem::path> folders,
                                      const std::filesystem::path& assetRoot, IAssetBrowserController& controller);
    void MoveAssetBrowserAssets(std::span<const Keire::AssetId> assets, const std::filesystem::path& folder,
                                IAssetBrowserController& controller);
    void SetAssetBrowserClipboard(std::span<const Keire::AssetId> assets,
                                  std::vector<AssetBrowserClipboardEntry>& clipboard);
    void SetAssetBrowserFolderClipboard(std::span<const std::filesystem::path> folders,
                                        std::vector<AssetBrowserClipboardEntry>& clipboard);
    void PasteAssetBrowserClipboard(AssetBrowserClipboardMode& mode, std::vector<AssetBrowserClipboardEntry>& clipboard,
                                    const std::filesystem::path& assetRoot, const std::filesystem::path& folder,
                                    IAssetBrowserController& controller);
} // namespace KeireEditor
