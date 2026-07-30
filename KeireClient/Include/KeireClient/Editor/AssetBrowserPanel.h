#pragma once

#include "Keire/Core.h"

#include "KeireInternal/Assets/AssetWorkerProtocol.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class IAssetBrowserController
    {
      public:
        virtual ~IAssetBrowserController() = default;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& AssetBrowserTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> AssetBrowserDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> AssetBrowserAssets() const noexcept = 0;
        [[nodiscard]] virtual std::span<const Keire::AssetSourceRecord> AssetBrowserRecords() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t AssetBrowserRecordRevision() const noexcept = 0;
        [[nodiscard]] virtual std::string_view AssetBrowserStatus() const noexcept = 0;
        [[nodiscard]] virtual Keire::AssetId AssetBrowserSceneAsset() const noexcept = 0;
        [[nodiscard]] virtual bool AssetBrowserSceneDirty() const noexcept = 0;
        [[nodiscard]] virtual std::vector<Keire::ManagedAssetTypeDescriptor> AssetBrowserManagedAssetTypes() const = 0;
        virtual void SetAssetBrowserSelected(Keire::AssetId asset) noexcept = 0;
        virtual void ClearAssetBrowserSceneSelection() noexcept = 0;
        virtual void SetAssetBrowserStatus(std::string status) noexcept = 0;
        virtual void ReportAssetBrowserError(std::string message) noexcept = 0;
        virtual void ImportAssetBrowserAssets() = 0;
        virtual void RequestAssetBrowserCreateScene() = 0;
        virtual bool CreateAssetBrowserMaterial(std::string_view name) = 0;
        virtual bool CreateAssetBrowserAnimationGraph(std::string_view name) = 0;
        virtual bool CreateAssetBrowserScript(std::string_view name) = 0;
        virtual bool CreateAssetBrowserManagedAssembly(std::string_view name) = 0;
        virtual bool CreateAssetBrowserManagedData(Keire::ManagedTypeId type, std::string_view name) = 0;
        virtual bool CreateAssetBrowserAudioMixer(std::string_view name) = 0;
        virtual bool CreateAssetBrowserPhysicsMaterial(std::string_view name) = 0;
        virtual bool CreateAssetBrowserVfxEffect(std::string_view name) = 0;
        virtual bool CreateAssetBrowserPrefab(std::string_view name) = 0;
        virtual bool CreateAssetBrowserPrefabVariant(Keire::AssetId basePrefab, std::string_view name) = 0;
        virtual void CreateAssetBrowserPrefabFromObject(Keire::AssetId object, const std::filesystem::path& folder) = 0;
        virtual void CreateAssetBrowserShader() = 0;
        virtual void CreateAssetBrowserInputActions(Keire::InputActionAssetDefinition definition,
                                                    std::string_view baseName) = 0;
        virtual void ExtractAssetBrowserMaterials(Keire::AssetId model) = 0;
        virtual void MutateAssetBrowser(Keire::Detail::AssetWorkerMutation mutation,
                                        Keire::Detail::AssetWorkerMutation reverse, std::string name,
                                        bool revealResult = false) = 0;
        virtual void OpenAssetBrowserInputActions(Keire::AssetId asset) = 0;
        virtual void OpenAssetBrowserAnimationGraph(Keire::AssetId asset) = 0;
        virtual void OpenAssetBrowserAudioMixer(Keire::AssetId asset) = 0;
        virtual void OpenAssetBrowserVfxEffect(Keire::AssetId asset) = 0;
        virtual void OpenAssetBrowserPrefab(Keire::AssetId asset) = 0;
        virtual void OpenAssetBrowserScene(Keire::AssetId asset) = 0;
        virtual void PrepareAssetBrowserExternalOpen(Keire::AssetId asset) = 0;
        virtual void CopyAssetBrowserText(std::string_view value) = 0;
    };

    class AssetBrowserPanel final
    {
      public:
        explicit AssetBrowserPanel(IAssetBrowserController& controller);
        ~AssetBrowserPanel();

        AssetBrowserPanel(const AssetBrowserPanel&) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

        void SetProjectRoot(const std::filesystem::path& root);
        void SetUndoContext(Keire::Ref<Keire::UndoContext> context);
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const;
        [[nodiscard]] bool Focused() const noexcept;
        [[nodiscard]] std::filesystem::path CurrentFolder() const;
        [[nodiscard]] std::filesystem::path ResolveExternalDropFolder(Keire::UiPosition position) const;
        [[nodiscard]] static std::vector<Keire::AssetId> DecodeDragPayload(std::span<const std::byte> bytes);
        void InvalidateThumbnail(Keire::AssetId asset);
        void Attach(Keire::UiWorkspace& workspace);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept;
        void RevealAsset(Keire::AssetId asset);
        void OpenAsset(Keire::AssetId asset);
        void RequestCreateMaterial();
        void Draw(Keire::UiFrame& ui);
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor
