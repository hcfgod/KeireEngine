#include "KeireClient/Editor/ExternalEditorProfiles.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::optional<std::string> Environment(const char* name)
        {
#if defined(_WIN32)
            char* value = nullptr;
            std::size_t size = 0;
            if (_dupenv_s(&value, &size, name) != 0 || !value)
                return std::nullopt;
            std::string result(value);
            std::free(value);
            return result;
#else
            if (const char* value = std::getenv(name))
                return std::string(value);
            return std::nullopt;
#endif
        }

        [[nodiscard]] std::optional<std::filesystem::path>
        FindExecutable(const std::span<const std::filesystem::path> names)
        {
            for (const auto& name : names)
            {
                std::error_code error;
                if (name.is_absolute() && std::filesystem::is_regular_file(name, error) && !error)
                    return std::filesystem::absolute(name).lexically_normal();
            }

            const auto pathValue = Environment("PATH");
            if (!pathValue)
                return std::nullopt;
#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            std::string_view path(*pathValue);
            while (!path.empty())
            {
                const auto end = path.find(separator);
                auto directory = std::filesystem::path(path.substr(0, end));
                if (!directory.empty())
                {
                    for (const auto& name : names)
                    {
                        std::error_code error;
                        auto candidate = directory / name;
                        if (std::filesystem::is_regular_file(candidate, error) && !error)
                            return std::filesystem::absolute(candidate).lexically_normal();
                    }
                }
                if (end == std::string_view::npos)
                    break;
                path.remove_prefix(end + 1);
            }
            return std::nullopt;
        }

#if defined(_WIN32)
        [[nodiscard]] std::optional<std::filesystem::path>
        FindNewestBelow(const std::span<const std::filesystem::path> roots,
                        const std::span<const std::filesystem::path> executableNames)
        {
            std::optional<std::filesystem::path> result;
            std::filesystem::file_time_type newest{};
            for (const auto& root : roots)
            {
                std::error_code error;
                if (!std::filesystem::is_directory(root, error) || error)
                    continue;
                std::filesystem::recursive_directory_iterator iterator(
                    root, std::filesystem::directory_options::skip_permission_denied, error);
                const std::filesystem::recursive_directory_iterator end;
                while (!error && iterator != end)
                {
                    if (iterator.depth() > 6)
                    {
                        iterator.disable_recursion_pending();
                        iterator.increment(error);
                        continue;
                    }
                    const auto& entry = *iterator;
                    if (std::ranges::find(executableNames, entry.path().filename()) != executableNames.end() &&
                        entry.is_regular_file(error) && !error)
                    {
                        const auto modified = entry.last_write_time(error);
                        if (!error && (!result || modified > newest))
                        {
                            result = std::filesystem::absolute(entry.path()).lexically_normal();
                            newest = modified;
                        }
                    }
                    iterator.increment(error);
                }
            }
            return result;
        }
#endif

        void Add(std::vector<ExternalEditorProfile>& profiles, std::string id, std::string name,
                 const std::initializer_list<std::filesystem::path> candidates)
        {
            const auto executable = FindExecutable({candidates.begin(), candidates.size()});
            profiles.push_back({.Id = std::move(id),
                                .DisplayName = std::move(name),
                                .Executable = executable.value_or(std::filesystem::path{}),
                                .Installed = executable.has_value()});
        }

#if defined(_WIN32)
        void AddDiscovered(std::vector<ExternalEditorProfile>& profiles, std::string id, std::string name,
                           const std::initializer_list<std::filesystem::path> candidates,
                           const std::initializer_list<std::filesystem::path> roots,
                           const std::initializer_list<std::filesystem::path> executableNames)
        {
            auto executable = FindExecutable({candidates.begin(), candidates.size()});
            if (!executable)
                executable =
                    FindNewestBelow({roots.begin(), roots.size()}, {executableNames.begin(), executableNames.size()});
            profiles.push_back({.Id = std::move(id),
                                .DisplayName = std::move(name),
                                .Executable = executable.value_or(std::filesystem::path{}),
                                .Installed = executable.has_value()});
        }
#endif
    } // namespace

    std::vector<ExternalEditorProfile> DiscoverExternalEditorProfiles()
    {
        std::vector<ExternalEditorProfile> profiles{
            {.Id = "system", .DisplayName = "System Default", .Installed = true, .SystemDefault = true}};
#if defined(_WIN32)
        const auto localValue = Environment("LOCALAPPDATA");
        const auto local = localValue ? std::filesystem::path(*localValue) : std::filesystem::path{};
        const auto programFilesValue = Environment("ProgramFiles");
        const auto programFiles =
            programFilesValue ? std::filesystem::path(*programFilesValue) : std::filesystem::path{};
        const auto programFilesX86Value = Environment("ProgramFiles(x86)");
        const auto programFilesX86 =
            programFilesX86Value ? std::filesystem::path(*programFilesX86Value) : std::filesystem::path{};
        Add(profiles, "vscode", "Visual Studio Code", {local / "Programs/Microsoft VS Code/Code.exe", "code.exe"});
        Add(profiles, "vscode-insiders", "Visual Studio Code Insiders",
            {local / "Programs/Microsoft VS Code Insiders/Code - Insiders.exe", "code-insiders.exe"});
        Add(profiles, "vscodium", "VSCodium",
            {local / "Programs/VSCodium/VSCodium.exe", programFiles / "VSCodium/VSCodium.exe", "codium.exe"});
        Add(profiles, "cursor", "Cursor", {local / "Programs/cursor/Cursor.exe", "cursor.exe"});
        Add(profiles, "zed", "Zed", {local / "Programs/Zed/Zed.exe", "zed.exe"});
        AddDiscovered(profiles, "rider", "JetBrains Rider", {"rider64.exe", "rider.exe"},
                      {local / "Programs", local / "JetBrains/Toolbox/apps", programFiles / "JetBrains"},
                      {"rider64.exe", "rider.exe"});
        AddDiscovered(profiles, "clion", "JetBrains CLion", {"clion64.exe", "clion.exe"},
                      {local / "Programs", local / "JetBrains/Toolbox/apps", programFiles / "JetBrains"},
                      {"clion64.exe", "clion.exe"});
        Add(profiles, "visual-studio", "Visual Studio",
            {programFiles / "Microsoft Visual Studio/2022/Community/Common7/IDE/devenv.exe",
             programFiles / "Microsoft Visual Studio/2022/Professional/Common7/IDE/devenv.exe",
             programFiles / "Microsoft Visual Studio/2022/Enterprise/Common7/IDE/devenv.exe",
             programFilesX86 / "Microsoft Visual Studio/2019/Community/Common7/IDE/devenv.exe", "devenv.exe"});
        Add(profiles, "sublime", "Sublime Text", {"sublime_text.exe"});
        Add(profiles, "neovim", "Neovim", {"nvim.exe"});
#elif defined(__APPLE__)
        Add(profiles, "vscode", "Visual Studio Code",
            {"/Applications/Visual Studio Code.app/Contents/MacOS/Electron", "code"});
        Add(profiles, "vscodium", "VSCodium", {"/Applications/VSCodium.app/Contents/MacOS/Electron", "codium"});
        Add(profiles, "cursor", "Cursor", {"/Applications/Cursor.app/Contents/MacOS/Cursor", "cursor"});
        Add(profiles, "zed", "Zed", {"/Applications/Zed.app/Contents/MacOS/zed", "zed"});
        Add(profiles, "rider", "JetBrains Rider", {"/Applications/Rider.app/Contents/MacOS/rider", "rider"});
        Add(profiles, "clion", "JetBrains CLion", {"/Applications/CLion.app/Contents/MacOS/clion", "clion"});
        Add(profiles, "xcode", "Xcode", {"/usr/bin/xed"});
        Add(profiles, "sublime", "Sublime Text",
            {"/Applications/Sublime Text.app/Contents/MacOS/sublime_text", "subl"});
        Add(profiles, "neovim", "Neovim", {"nvim"});
#else
        Add(profiles, "vscode", "Visual Studio Code", {"code"});
        Add(profiles, "vscodium", "VSCodium", {"codium"});
        Add(profiles, "cursor", "Cursor", {"cursor"});
        Add(profiles, "zed", "Zed", {"zed"});
        Add(profiles, "rider", "JetBrains Rider", {"rider"});
        Add(profiles, "clion", "JetBrains CLion", {"clion"});
        Add(profiles, "sublime", "Sublime Text", {"subl", "sublime_text"});
        Add(profiles, "neovim", "Neovim", {"nvim"});
        Add(profiles, "emacs", "Emacs", {"emacsclient", "emacs"});
#endif
        return profiles;
    }
} // namespace KeireEditor
