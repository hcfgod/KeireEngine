#pragma once

#include "Keire/Core.h"

#include <atomic>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace KeireEditor
{
    struct ExternalAssetImportCompletion
    {
        Keire::ExternalAssetImportResult Result;
        bool Viewport = false;
        Keire::EntityId ViewportTarget;
    };

    class ExternalAssetImportController final
    {
      public:
        ~ExternalAssetImportController();

        void Queue(std::span<const std::filesystem::path> paths, const std::filesystem::path& destinationFolder,
                   bool viewport, Keire::EntityId viewportTarget, const Keire::Ref<Keire::AssetDatabase>& database);
        void Draw(Keire::UiFrame& ui, const Keire::Ref<Keire::AssetDatabase>& database);
        [[nodiscard]] std::optional<ExternalAssetImportCompletion> TakeCompletion();
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }
        [[nodiscard]] bool Pending() const noexcept { return !m_Items.empty() || m_Worker.joinable() || m_Failed; }

      private:
        void Execute(const Keire::Ref<Keire::AssetDatabase>& database);

        std::vector<Keire::ExternalAssetImportItem> m_Items;
        std::vector<bool> m_Included;
        std::vector<std::optional<Keire::AssetImporterRegistration>> m_Importers;
        std::optional<ExternalAssetImportCompletion> m_Completion;
        std::string m_Diagnostic;
        bool m_Viewport = false;
        Keire::EntityId m_ViewportTarget;
        bool m_OpenRequested = false;
        bool m_Failed = false;
        std::jthread m_Worker;
        std::optional<Keire::ExternalAssetImportResult> m_WorkerResult;
        std::exception_ptr m_WorkerError;
        std::atomic_bool m_WorkerFinished = false;
    };
} // namespace KeireEditor
