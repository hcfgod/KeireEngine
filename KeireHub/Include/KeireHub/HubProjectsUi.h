#pragma once

#include "KeireHub/HubDesignTokens.h"
#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include "Keire/Project/Project.h"
#include "Keire/Ui.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class HubProjectsView : std::uint8_t
    {
        List,
        Cards
    };

    enum class HubProjectsSort : std::uint8_t
    {
        LastOpened,
        Name,
        Created,
        Modified,
        Version,
        Status,
        Size
    };

    enum class HubProjectUiCommandType : std::uint8_t
    {
        None,
        NewProject,
        AddProject,
        Refresh,
        PreferencesChanged,
        Open,
        OpenWithEditor,
        FindCompatibleEditor,
        Reveal,
        CopyPath,
        SetPinned,
        Duplicate,
        Rename,
        BrowseDuplicateLocation,
        BrowseLocateProject,
        RemoveFromHub
    };

    struct HubProjectUiCommand final
    {
        HubProjectUiCommandType Type = HubProjectUiCommandType::None;
        std::string ProjectId;
        std::string EditorId;
        std::string EditorVersion;
        std::filesystem::path Path;
        std::string DisplayName;
        bool Pinned = false;

        [[nodiscard]] explicit operator bool() const noexcept { return Type != HubProjectUiCommandType::None; }
    };

    struct HubProjectUiPendingSelection final
    {
        std::string Id;
        std::filesystem::path Root;
        std::string Name;
        bool Pinned = false;
        Keire::ProjectStatus Status = Keire::ProjectStatus::Invalid;
        std::uint32_t SchemaVersion = 0;
        std::string LastSavedVersion;
        std::string MinimumVersion;
    };

    [[nodiscard]] HubProjectUiCommand TakeOpenWithEditorCommand(std::optional<HubProjectUiPendingSelection>& pending,
                                                                std::string editorId);

    class HubProjectsUi final
    {
      public:
        void SetAppearance(HubAppearance appearance, bool systemPrefersDark = true) noexcept;
        void SetPreferences(HubProjectsView view, HubProjectsSort sort) noexcept;
        void SetDuplicateParent(const std::filesystem::path& parent);
        void SetThumbnails(std::shared_ptr<const std::vector<ProjectThumbnail>> thumbnails) noexcept;

        [[nodiscard]] HubProjectsView View() const noexcept { return m_View; }
        [[nodiscard]] HubProjectsSort Sort() const noexcept { return m_Sort; }

        [[nodiscard]] HubProjectUiCommand Draw(Keire::UiFrame& ui, const std::vector<Keire::RecentProject>& entries,
                                               const std::vector<HubEditorUiRecord>& editors, std::string_view notice,
                                               bool noticeError, bool confirmRemoval, bool folderDialogActive);

      private:
        struct ThumbnailTexture final
        {
            std::string ProjectId;
            std::shared_ptr<const std::vector<std::byte>> Pixels;
            Keire::Ref<Keire::UiImage> Image;
        };

        void SelectPending(const Keire::RecentProject& project);
        void DrawActionMenu(Keire::UiFrame& ui, const Keire::RecentProject& project, bool confirmRemoval,
                            HubProjectUiCommand& command);
        void DrawList(Keire::UiFrame& ui, const std::vector<const Keire::RecentProject*>& projects, bool confirmRemoval,
                      HubProjectUiCommand& command);
        void DrawCards(Keire::UiFrame& ui, const std::vector<const Keire::RecentProject*>& projects,
                       bool confirmRemoval, HubProjectUiCommand& command);
        void DrawDialogs(Keire::UiFrame& ui, const std::vector<HubEditorUiRecord>& editors, bool folderDialogActive,
                         HubProjectUiCommand& command);
        [[nodiscard]] Keire::Ref<Keire::UiImage> ResolveThumbnail(Keire::UiFrame& ui, std::string_view projectId);

        HubProjectsView m_View = HubProjectsView::List;
        HubProjectsSort m_Sort = HubProjectsSort::LastOpened;
        HubDesignTokens m_Tokens = HubDesignTokens::For(HubAppearance::Dark);
        std::optional<HubProjectUiPendingSelection> m_Pending;
        std::string m_Search;
        std::string m_SelectedProjectId;
        std::string m_DuplicateName;
        std::string m_DuplicateParent;
        std::string m_RenameName;
        std::string m_VersionFilter;
        bool m_RequestDuplicatePopup = false;
        bool m_RequestRenamePopup = false;
        bool m_RequestRemovePopup = false;
        bool m_RequestOpenWithPopup = false;
        bool m_FavoritesOnly = false;
        std::uint8_t m_StatusFilter = 0;
        std::uint8_t m_EditorFilter = 0;
        std::shared_ptr<const std::vector<ProjectThumbnail>> m_Thumbnails;
        std::vector<ThumbnailTexture> m_ThumbnailTextures;
    };
} // namespace KeireHub
