#include "KeireHub/HubProjectCreationUi.h"

#include "KeireHub/HubProjectUiSupport.h"

#include "KeireHubRuntime/TemplateManager.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <ranges>
#include <system_error>

namespace KeireHub
{
    HubCreateProjectRequest DrawHubCreateProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                                       std::string& templateId, std::string& editorId,
                                                       std::string& projectName, std::string& projectLocation,
                                                       bool& openAfterCreation, const bool folderDialogPending)
    {
        HubCreateProjectRequest request;
        ui.SetNextWindowSize({800.0F, 500.0F}, false);
        if (auto dialog = ui.BeginPopupModal("Create Project"); !dialog)
            return request;

        const auto editorAvailable = [](const HubEditorUiRecord& editor)
        { return editor.Healthy && !editor.Entrypoint.empty() && !editor.AssetToolEntrypoint.empty(); };
        const auto compatibilityInput = [](const HubEditorUiRecord& editor)
        {
            return HubTemplateEditorCompatibilityInput{.Version = editor.Version,
                                                       .MinimumProjectSchema = editor.MinimumProjectSchema,
                                                       .MaximumProjectSchema = editor.MaximumProjectSchema,
                                                       .Healthy = editor.Healthy,
                                                       .HasEntrypoint = !editor.Entrypoint.empty(),
                                                       .HasAssetToolEntrypoint = !editor.AssetToolEntrypoint.empty()};
        };
        auto selectedTemplate = std::ranges::find(snapshot.Templates, templateId, &HubTemplateUiRecord::Id);
        if (selectedTemplate == snapshot.Templates.end() && !snapshot.Templates.empty())
        {
            selectedTemplate = snapshot.Templates.begin();
            templateId = selectedTemplate->Id;
        }
        const auto compatibleEditor = [&](const HubTemplateUiRecord& item)
        {
            return std::ranges::find_if(
                snapshot.Editors, [&](const HubEditorUiRecord& editor)
                { return EvaluateTemplateCompatibility(item, compatibilityInput(editor)).Compatible(); });
        };
        auto selectedEditor = std::ranges::find(snapshot.Editors, editorId, &HubEditorUiRecord::Id);
        const bool selectedPairCompatible =
            selectedTemplate != snapshot.Templates.end() && selectedEditor != snapshot.Editors.end() &&
            EvaluateTemplateCompatibility(*selectedTemplate, compatibilityInput(*selectedEditor)).Compatible();
        if (!selectedPairCompatible)
        {
            selectedEditor = selectedTemplate == snapshot.Templates.end()
                                 ? std::ranges::find_if(snapshot.Editors, editorAvailable)
                                 : compatibleEditor(*selectedTemplate);
            editorId = selectedEditor == snapshot.Editors.end() ? std::string{} : selectedEditor->Id;
        }

        Keire::UiTableOptions layoutOptions;
        layoutOptions.Borders = false;
        layoutOptions.Resizable = false;
        if (auto layout = ui.BeginTable("CreateProjectLayout", 2, layoutOptions); layout)
        {
            ui.TableNextRow();
            (void)ui.TableNextColumn();
            ui.TextColored({0.58F, 0.68F, 0.88F, 1.0F}, "TEMPLATES");
            for (const auto& item : snapshot.Templates)
            {
                auto id = ui.PushId(item.Id);
                const auto firstCompatibleEditor = compatibleEditor(item);
                const bool available = firstCompatibleEditor != snapshot.Editors.end();
                if (auto disabled = ui.BeginDisabled(!available); disabled)
                {
                    if (ui.Selectable(item.Name + "\n" + item.Category +
                                          (available ? "  ·  Compatible" : "  ·  No compatible editor"),
                                      templateId == item.Id))
                    {
                        templateId = item.Id;
                        selectedTemplate = std::ranges::find(snapshot.Templates, templateId, &HubTemplateUiRecord::Id);
                        if (selectedEditor == snapshot.Editors.end() ||
                            !EvaluateTemplateCompatibility(item, compatibilityInput(*selectedEditor)).Compatible())
                        {
                            selectedEditor = firstCompatibleEditor;
                            editorId = selectedEditor->Id;
                        }
                    }
                }
            }

            (void)ui.TableNextColumn();
            ui.TextColored({0.58F, 0.68F, 0.88F, 1.0F}, "PROJECT DETAILS");
            const std::string editorPreview = selectedEditor == snapshot.Editors.end()
                                                  ? "No compatible editor installed"
                                                  : selectedEditor->Version + "  ·  " + selectedEditor->Channel;
            if (auto combo = ui.BeginCombo("Editor version", editorPreview); combo)
            {
                for (const auto& editor : snapshot.Editors)
                {
                    if (!editorAvailable(editor) || selectedTemplate == snapshot.Templates.end() ||
                        !EvaluateTemplateCompatibility(*selectedTemplate, compatibilityInput(editor)).Compatible())
                        continue;
                    if (ui.Selectable(editor.Version + "  ·  " + editor.Channel, editor.Id == editorId))
                    {
                        editorId = editor.Id;
                        selectedEditor = std::ranges::find(snapshot.Editors, editorId, &HubEditorUiRecord::Id);
                    }
                }
            }
            if (selectedTemplate != snapshot.Templates.end())
            {
                ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "Template v" + selectedTemplate->Version + "  ·  Editors " +
                                                                selectedTemplate->CompatibleEditors + "  ·  Schema " +
                                                                std::to_string(selectedTemplate->ProjectSchema));
            }
            (void)ui.InputTextWithHint("Name", "My Project", projectName);
            (void)ui.InputTextWithHint("Location", "Parent folder", projectLocation);
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(folderDialogPending || snapshot.ProjectCreationBusy); disabled)
                if (ui.Button("Browse..."))
                    request.Action = HubCreateProjectAction::Browse;

            const auto parentDirectory = Keire::Detail::PathFromUtf8(projectLocation);
            const auto destination = parentDirectory / Keire::Detail::PathFromUtf8(projectName);
            ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "Destination: " + Utf8Path(destination));
            (void)ui.Checkbox("Open in the selected editor after creation", openAfterCreation);
            const bool validName = IsValidProjectName(projectName);
            std::error_code error;
            const bool parentAvailable =
                !projectLocation.empty() && std::filesystem::is_directory(parentDirectory, error);
            const bool conflict = validName && parentAvailable && std::filesystem::exists(destination, error);
            const bool templateAvailable =
                selectedTemplate != snapshot.Templates.end() && selectedEditor != snapshot.Editors.end() &&
                EvaluateTemplateCompatibility(*selectedTemplate, compatibilityInput(*selectedEditor)).Compatible();
            if (!validName)
                ui.TextColored({0.96F, 0.38F, 0.42F, 1.0F},
                               "Use 1-128 bytes with no reserved characters or surrounding whitespace.");
            else if (error)
                ui.TextColored({0.96F, 0.38F, 0.42F, 1.0F}, "The destination could not be inspected.");
            else if (!parentAvailable)
                ui.TextColored({0.96F, 0.38F, 0.42F, 1.0F}, "Choose an existing project location folder.");
            else if (conflict)
                ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, "The destination already exists.");
            else if (!templateAvailable)
                ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, "Locate or install a compatible editor first.");
            else if (snapshot.ProjectCreationBusy)
                ui.TextColored({0.38F, 0.64F, 0.96F, 1.0F}, snapshot.ProjectCreationMessage);
            else
                ui.TextColored({0.32F, 0.84F, 0.58F, 1.0F}, "Ready to create and validate.");

            const bool canCreate = validName && !error && parentAvailable && !conflict && templateAvailable &&
                                   !snapshot.ProjectCreationBusy;
            if (auto disabled = ui.BeginDisabled(!canCreate); disabled)
                if (ui.Button("Create Project", {132.0F, 34.0F}))
                {
                    request = {.Action = HubCreateProjectAction::Create,
                               .TemplateId = templateId,
                               .EditorId = editorId,
                               .Name = projectName,
                               .ParentDirectory = parentDirectory,
                               .OpenAfterCreation = openAfterCreation};
                    ui.CloseCurrentPopup();
                }
        }
        ui.SameLine();
        if (ui.Button("Cancel"))
            ui.CloseCurrentPopup();
        return request;
    }
} // namespace KeireHub
