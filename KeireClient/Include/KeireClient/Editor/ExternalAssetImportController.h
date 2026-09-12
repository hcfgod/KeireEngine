#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetOperationService.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KeireEditor
{
    struct ExternalAssetImportCompletion
    {
        Keire::ExternalAssetImportResult Result;
        bool Viewport = false;
        Keire::EntityId ViewportTarget;
        Keire::Vector3 ViewportPosition;
        Keire::WeakRef<Keire::Scene> ViewportScene;
    };

    class ExternalAssetImportController final
    {
      public:
        void Queue(std::span<const std::filesystem::path> paths, const std::filesystem::path& destinationFolder,
                   bool viewport, Keire::EntityId viewportTarget, Keire::Vector3 viewportPosition,
                   const Keire::Ref<Keire::AssetDatabase>& database, AssetOperationService& operations,
                   const Keire::Ref<Keire::Scene>& viewportScene = {});
        void Draw(Keire::UiFrame& ui, const Keire::Ref<Keire::AssetDatabase>& database,
                  AssetOperationService& operations);
        void Complete(AssetOperationCompletion completion);
        [[nodiscard]] std::optional<ExternalAssetImportCompletion> TakeCompletion();
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }
        [[nodiscard]] bool Pending() const noexcept { return !m_Items.empty() || m_OperationPending || m_Failed; }

      private:
        void Execute(AssetOperationService& operations);

        std::vector<Keire::ExternalAssetImportItem> m_Items;
        std::vector<bool> m_Included;
        std::vector<std::optional<Keire::AssetImporterRegistration>> m_Importers;
        std::optional<ExternalAssetImportCompletion> m_Completion;
        std::string m_Diagnostic;
        bool m_Viewport = false;
        Keire::EntityId m_ViewportTarget;
        Keire::Vector3 m_ViewportPosition;
        Keire::WeakRef<Keire::Scene> m_ViewportScene;
        bool m_OpenRequested = false;
        bool m_Failed = false;
        bool m_OperationPending = false;
    };
} // namespace KeireEditor
