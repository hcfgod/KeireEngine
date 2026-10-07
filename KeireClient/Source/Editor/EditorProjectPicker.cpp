#include "KeireClient/Editor/EditorProjectPicker.h"

#include "KeireInternal/Process.h"

#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#endif

namespace KeireEditor
{
    namespace
    {
        std::filesystem::path RegisteredHubExecutable()
        {
#if defined(_WIN32)
            constexpr auto key = L"Software\\Classes\\keirehub\\shell\\open\\command";
            DWORD bytes = 0;
            if (::RegGetValueW(HKEY_CURRENT_USER, key, nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) !=
                    ERROR_SUCCESS ||
                bytes < sizeof(wchar_t) || bytes > 64U * 1024U || bytes % sizeof(wchar_t) != 0)
                return {};
            std::vector<wchar_t> command(bytes / sizeof(wchar_t) + 1, L'\0');
            if (::RegGetValueW(HKEY_CURRENT_USER, key, nullptr, RRF_RT_REG_SZ, nullptr, command.data(), &bytes) !=
                    ERROR_SUCCESS ||
                command.front() == L'\0')
                return {};
            int count = 0;
            const auto release = [](wchar_t** value) { ::LocalFree(value); };
            std::unique_ptr<wchar_t*, decltype(release)> arguments(::CommandLineToArgvW(command.data(), &count),
                                                                   release);
            // Resolve the executable only: never evaluate a registered command through a shell or inherit its flags.
            if (arguments && count > 0)
            {
                const std::filesystem::path executable(arguments.get()[0]);
                if (::CompareStringOrdinal(executable.filename().c_str(), -1, L"KeireHub.exe", -1, TRUE) == CSTR_EQUAL)
                    return executable;
            }
#endif
            return {};
        }
    } // namespace

    void OpenEditorProjectPicker(const std::filesystem::path& editorExecutable, const ProjectPickerLauncher& launcher,
                                 const ProjectPickerRegistration& registration)
    {
        std::filesystem::path hub;
        try
        {
            hub = Keire::Detail::ResolveCompanionExecutable(editorExecutable, "KeireHub");
        }
        catch (const std::exception& error)
        {
            hub = registration ? registration() : RegisteredHubExecutable();
            std::error_code status;
            if (!hub.is_absolute() || !std::filesystem::is_regular_file(hub, status))
                throw std::runtime_error(std::string("Project selection needs Kéire Hub. Install or repair Kéire Hub, "
                                                     "or open the Editor with --project <path>. ") +
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
