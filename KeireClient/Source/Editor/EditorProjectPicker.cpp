#include "KeireClient/Editor/EditorProjectPicker.h"

#include "KeireInternal/Process.h"

#include <array>
#include <exception>
#include <stdexcept>

namespace KeireEditor
{
    void OpenEditorProjectPicker(const std::filesystem::path& editorExecutable, const ProjectPickerLauncher& launcher)
    {
        std::filesystem::path hub;
        try
        {
            hub = Keire::Detail::ResolveCompanionExecutable(editorExecutable, "KeireHub");
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(std::string("Project selection needs Kéire Hub. Repair the Editor installation "
                                                 "or open a project from your installed Hub. ") +
                                     error.what());
        }
        const std::array<std::string, 2> arguments{"--navigate", "projects"};
        std::string diagnostic;
        // The setup program's temporary working directory disappears when it exits.
        const auto workingDirectory = hub.parent_path();
        const bool launched = launcher ? launcher(hub, arguments, workingDirectory, diagnostic)
                                       : Keire::Detail::LaunchDetachedProcessAtDesktopUserIntegrity(
                                             hub, arguments, workingDirectory, diagnostic);
        if (!launched)
            throw std::runtime_error("Could not open the project picker. Open Kéire Hub and select your project. " +
                                     diagnostic);
    }
} // namespace KeireEditor
