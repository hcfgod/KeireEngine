#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <memory>
#include <string>

class EditorWorkspaceLayer;

namespace KeireEditor
{
    class AssetBrowserPanel final
    {
      public:
        AssetBrowserPanel();
        ~AssetBrowserPanel();

        AssetBrowserPanel(const AssetBrowserPanel&) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

        void SetProjectRoot(const std::filesystem::path& root);
        void SetUndoContext(Keire::Ref<Keire::UndoContext> context);
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const;
        [[nodiscard]] bool Focused() const noexcept;
        [[nodiscard]] std::filesystem::path CurrentFolder() const;
        void RevealAsset(Keire::AssetId asset, EditorWorkspaceLayer& editor);
        void RecordCreatedAsset(const Keire::Ref<Keire::AssetDatabase>& database, Keire::AssetId asset,
                                std::string name);
        void Draw(Keire::UiFrame& ui, EditorWorkspaceLayer& editor);
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor
