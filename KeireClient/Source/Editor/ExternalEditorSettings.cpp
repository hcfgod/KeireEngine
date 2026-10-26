#include "KeireClientInternal/Editor/ExternalEditorSettings.h"

#include "Keire/Ui.h"
#include "KeireInternal/FileSystem.h"

#include <exception>
#include <system_error>

namespace KeireEditor::Detail
{
    bool DrawCustomExternalEditor(Keire::UiFrame& ui, std::string& draft, std::filesystem::path& executable,
                                  std::string& diagnostic)
    {
        ui.Text("Editor Executable");
        ui.SetNextItemWidth(-1.0F);
        (void)ui.InputText("##Editor Executable", draft);
        // Item state belongs to the input, not the explanatory text or button drawn afterwards.
        const bool finishedEditing = ui.LastItemState().DeactivatedAfterEdit;
        const bool apply = ui.Button("Use Custom Editor");
        ui.TextWrapped("The path is validated and saved when you leave the field or choose Use Custom Editor.");
        if (!finishedEditing && !apply)
        {
            return false;
        }
        try
        {
            std::error_code error;
            const auto candidate = Keire::Detail::PathFromUtf8(draft);
            if (!std::filesystem::is_regular_file(candidate, error) || error)
            {
                diagnostic = "The custom external editor executable does not exist.";
                return false;
            }
            executable = std::filesystem::absolute(candidate).lexically_normal();
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }
} // namespace KeireEditor::Detail
