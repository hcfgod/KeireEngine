#include "KeireClient/Editor/ExternalAssetImportController.h"

#include <stdexcept>

namespace KeireEditor
{
    ExternalAssetImportController::~ExternalAssetImportController()
    {
        if (m_Worker.joinable())
        {
            m_Worker.request_stop();
            m_Worker.join();
        }
    }

    void ExternalAssetImportController::Queue(const std::span<const std::filesystem::path> paths,
                                              const std::filesystem::path& destinationFolder, const bool viewport,
                                              const Keire::EntityId viewportTarget,
                                              const Keire::Ref<Keire::AssetDatabase>& database)
    {
        if (!database || paths.empty())
            return;
        if (Pending())
            throw std::logic_error("Finish the current external import before starting another.");
        m_Viewport = viewport;
        m_ViewportTarget = viewportTarget;
        m_Diagnostic.clear();
        m_Failed = false;
        bool requiresDialog = paths.size() > 1;
        for (const auto& source : paths)
        {
            const auto importer = database->FindImporterForPath(source);
            const bool directory = std::filesystem::is_directory(source);
            if (!directory && !importer)
            {
                m_Diagnostic += "Unsupported asset: " + source.filename().string() + "\n";
                continue;
            }
            Keire::ExternalAssetImportItem item;
            item.SourcePath = source;
            item.RelativeDestination = destinationFolder / source.filename();
            item.Conflict = Keire::ExternalAssetConflictPolicy::UniqueName;
            if (importer)
            {
                for (const auto& option : importer->ImportOptions)
                    item.Settings.emplace(option.Key, option.DefaultValue);
                if (importer->SuggestImportSettings)
                    item.Settings = importer->SuggestImportSettings(source, item.Settings);
                requiresDialog |= !importer->ImportOptions.empty();
            }
            requiresDialog |= directory || static_cast<bool>(database->Find(item.RelativeDestination));
            m_Items.push_back(std::move(item));
            m_Included.push_back(true);
            m_Importers.push_back(importer);
        }
        if (m_Items.empty())
            return;
        if (requiresDialog)
            m_OpenRequested = true;
        else
            Execute(database);
    }

    void ExternalAssetImportController::Execute(const Keire::Ref<Keire::AssetDatabase>& database)
    {
        std::vector<Keire::ExternalAssetImportItem> items;
        for (std::size_t index = 0; index < m_Items.size(); ++index)
            if (m_Included[index])
                items.push_back(std::move(m_Items[index]));
        m_Items.clear();
        m_Included.clear();
        m_Importers.clear();
        if (items.empty())
        {
            m_Diagnostic = "No assets were selected for import.";
            return;
        }
        m_Diagnostic = "Importing assets...";
        m_WorkerResult.reset();
        m_WorkerError = {};
        m_WorkerFinished.store(false, std::memory_order_relaxed);
        m_Worker = std::jthread(
            [this, database, items = std::move(items)](const std::stop_token cancellation)
            {
                try
                {
                    m_WorkerResult = database->ImportExternal(items, cancellation);
                }
                catch (...)
                {
                    m_WorkerError = std::current_exception();
                }
                m_WorkerFinished.store(true, std::memory_order_release);
            });
        m_OpenRequested = true;
    }

    void ExternalAssetImportController::Draw(Keire::UiFrame& ui, const Keire::Ref<Keire::AssetDatabase>& database)
    {
        if (m_Worker.joinable() && m_WorkerFinished.load(std::memory_order_acquire))
        {
            m_Worker.join();
            try
            {
                if (m_WorkerError)
                    std::rethrow_exception(m_WorkerError);
                ExternalAssetImportCompletion completion;
                completion.Result = std::move(*m_WorkerResult);
                completion.Viewport = m_Viewport;
                completion.ViewportTarget = m_ViewportTarget;
                m_Completion = std::move(completion);
                m_Diagnostic = "Imported " + std::to_string(m_Completion->Result.Entries.size()) + " asset(s).";
            }
            catch (const std::exception& error)
            {
                m_Diagnostic = error.what();
                m_Failed = true;
            }
        }
        if (m_OpenRequested)
        {
            ui.OpenPopup("Import Assets");
            m_OpenRequested = false;
        }
        auto popup = ui.BeginPopupModal("Import Assets");
        if (!popup)
            return;
        if (m_Worker.joinable())
        {
            ui.Text("Importing and validating assets...");
            ui.TextColored({0.62F, 0.65F, 0.72F, 1.0F}, m_Diagnostic);
            if (ui.Button("Cancel"))
            {
                m_Worker.request_stop();
                m_Diagnostic = "Cancelling import...";
            }
            return;
        }
        if (m_Failed)
        {
            ui.TextColored({1.0F, 0.35F, 0.35F, 1.0F}, "Import failed; no project files were changed.");
            ui.Text(m_Diagnostic);
            if (ui.Button("Close"))
            {
                m_Failed = false;
                ui.CloseCurrentPopup();
            }
            return;
        }
        if (m_Items.empty())
        {
            ui.CloseCurrentPopup();
            return;
        }
        ui.Text("Import files into the project");
        ui.Separator();
        for (std::size_t index = 0; index < m_Items.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            auto& item = m_Items[index];
            bool included = m_Included[index];
            if (ui.Checkbox("Include", included))
                m_Included[index] = included;
            ui.Text(item.SourcePath.filename().string());
            ui.TextColored({0.62F, 0.65F, 0.72F, 1.0F}, item.RelativeDestination.generic_string());
            const auto& importer = m_Importers[index];
            if (!importer)
                continue;
            ui.TextColored({0.62F, 0.65F, 0.72F, 1.0F},
                           "Importer: " + importer->Name + " / " + importer->Type.ToString());
            std::string activeGroup;
            for (const auto& option : importer->ImportOptions)
            {
                if (option.Group != activeGroup)
                {
                    activeGroup = option.Group;
                    ui.TextColored({0.35F, 0.75F, 1.0F, 1.0F}, activeGroup);
                }
                auto& value = item.Settings.at(option.Key);
                if (auto* boolean = std::get_if<bool>(&value))
                    (void)ui.Checkbox(option.DisplayName, *boolean);
                else if (auto* integer = std::get_if<std::int64_t>(&value))
                    (void)ui.DragInteger(
                        option.DisplayName, *integer, option.Step,
                        option.Minimum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*option.Minimum))
                                       : std::nullopt,
                        option.Maximum ? std::optional<std::int64_t>(static_cast<std::int64_t>(*option.Maximum))
                                       : std::nullopt);
                else if (auto* scalar = std::get_if<double>(&value))
                    (void)ui.DragScalar(option.DisplayName, *scalar, option.Step, option.Minimum, option.Maximum);
                else if (auto* choice = std::get_if<std::string>(&value))
                {
                    if (auto combo = ui.BeginCombo(option.DisplayName, *choice); combo)
                        for (const auto& candidate : option.Choices)
                            if (ui.Selectable(candidate, candidate == *choice))
                                *choice = candidate;
                }
            }
            const auto existing = database->Find(item.RelativeDestination);
            if (existing)
            {
                if (auto combo = ui.BeginCombo(
                        "Conflict", item.Conflict == Keire::ExternalAssetConflictPolicy::Replace ? "Replace"
                                    : item.Conflict == Keire::ExternalAssetConflictPolicy::Skip  ? "Skip"
                                                                                                 : "Unique Name");
                    combo)
                {
                    if (ui.Selectable("Unique Name", item.Conflict == Keire::ExternalAssetConflictPolicy::UniqueName))
                        item.Conflict = Keire::ExternalAssetConflictPolicy::UniqueName;
                    const bool compatible = existing->Importer == importer->Name && existing->Type == importer->Type;
                    if (auto disabled = ui.BeginDisabled(!compatible); disabled)
                        if (ui.Selectable("Replace", item.Conflict == Keire::ExternalAssetConflictPolicy::Replace))
                            item.Conflict = Keire::ExternalAssetConflictPolicy::Replace;
                    if (ui.Selectable("Skip", item.Conflict == Keire::ExternalAssetConflictPolicy::Skip))
                        item.Conflict = Keire::ExternalAssetConflictPolicy::Skip;
                }
            }
            ui.Separator();
        }
        if (!m_Diagnostic.empty())
            ui.TextColored({1.0F, 0.62F, 0.25F, 1.0F}, m_Diagnostic);
        if (ui.Button("Import"))
            Execute(database);
        ui.SameLine();
        if (ui.Button("Cancel"))
        {
            m_Items.clear();
            m_Included.clear();
            m_Importers.clear();
            ui.CloseCurrentPopup();
        }
    }

    std::optional<ExternalAssetImportCompletion> ExternalAssetImportController::TakeCompletion()
    {
        auto result = std::move(m_Completion);
        m_Completion.reset();
        return result;
    }
} // namespace KeireEditor
