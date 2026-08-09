#include "KeireInternal/Scripting/ManagedSdk.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] bool HasDotnet10Sdk(const std::filesystem::path& executable)
        {
            const auto sdk = executable.parent_path() / "sdk";
            if (!std::filesystem::is_directory(sdk))
                return false;
            return std::ranges::any_of(std::filesystem::directory_iterator(sdk),
                                       [](const std::filesystem::directory_entry& entry)
                                       {
                                           const auto name = entry.path().filename().string();
                                           return entry.is_directory() && name.starts_with("10.");
                                       });
        }
    } // namespace

    ManagedSdkConfiguration ReadManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                                        ManagedSdkConfiguration fallback)
    {
        const auto settingsPath = projectRoot / "ProjectSettings" / "Scripting.keiresettings";
        if (!std::filesystem::is_regular_file(settingsPath))
            return fallback;

        const auto document = nlohmann::json::parse(ReadTextFile(settingsPath, 1024U * 1024U));
        const auto selection = document.value("sdkSelection", std::string{"bundled"});
        if (selection == "systemPath")
            fallback.Selection = ManagedSdkSelection::SystemPath;
        else if (selection == "custom")
            fallback.Selection = ManagedSdkSelection::Custom;
        else
            fallback.Selection = ManagedSdkSelection::Bundled;
        if (const auto iterator = document.find("customSdkExecutable");
            iterator != document.end() && iterator->is_string())
        {
            fallback.CustomExecutable = PathFromUtf8(iterator->get<std::string>());
        }
        return fallback;
    }

    void WriteManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                      const ManagedSdkConfiguration& configuration)
    {
        const auto settingsPath = projectRoot / "ProjectSettings" / "Scripting.keiresettings";
        nlohmann::json document = nlohmann::json::object();
        if (std::filesystem::is_regular_file(settingsPath))
            document = nlohmann::json::parse(ReadTextFile(settingsPath, 1024U * 1024U));

        switch (configuration.Selection)
        {
        case ManagedSdkSelection::Bundled:
            document["sdkSelection"] = "bundled";
            break;
        case ManagedSdkSelection::SystemPath:
            document["sdkSelection"] = "systemPath";
            break;
        case ManagedSdkSelection::Custom:
            document["sdkSelection"] = "custom";
            break;
        }
        document["customSdkExecutable"] = PathToUtf8(configuration.CustomExecutable);
        WriteTextFileAtomically(settingsPath, document.dump(2) + '\n');
    }

    std::filesystem::path ResolveDotnet(const std::filesystem::path& configured, const ManagedSdkSelection selection,
                                        const std::filesystem::path& projectRoot,
                                        const std::filesystem::path& runtimeRoot)
    {
        constexpr std::string_view executableName =
#if defined(_WIN32)
            "dotnet.exe";
#else
            "dotnet";
#endif

        if (selection == ManagedSdkSelection::Custom)
        {
            if (configured.empty())
                throw std::runtime_error("A custom .NET SDK was selected, but no dotnet executable was configured.");
            const auto resolved = std::filesystem::absolute(configured).lexically_normal();
            if (!std::filesystem::is_regular_file(resolved) || !HasDotnet10Sdk(resolved))
                throw std::runtime_error("The configured dotnet executable does not provide the .NET 10 SDK.");
            return resolved;
        }

        if (selection == ManagedSdkSelection::Bundled)
        {
            std::vector<std::filesystem::path> roots;
            roots.reserve(16);
            const auto appendAncestors = [&roots](std::filesystem::path root)
            {
                if (root.empty())
                    return;
                root = std::filesystem::absolute(root).lexically_normal();
                for (std::size_t depth = 0; depth < 8 && !root.empty(); ++depth)
                {
                    roots.push_back(root);
                    const auto parent = root.parent_path();
                    if (parent == root)
                        break;
                    root = parent;
                }
            };
            appendAncestors(runtimeRoot);
            appendAncestors(std::filesystem::current_path());
            appendAncestors(projectRoot);

            std::vector<std::filesystem::path> candidates;
            candidates.reserve(roots.size() * 2 + 1);
            if (!runtimeRoot.empty())
                candidates.push_back(runtimeRoot / executableName);
            for (const auto& root : roots)
            {
                candidates.push_back(root / "Build" / "Dependencies" / "dotnet-sdk" / executableName);
                candidates.push_back(root / "Library" / "DotnetSdk10" / "sdk" / executableName);
            }

            for (const auto& candidate : candidates)
            {
                if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                    return std::filesystem::absolute(candidate).lexically_normal();
            }
            throw std::runtime_error(
                "The bundled .NET 10 SDK was not found. Install the engine SDK dependency or choose System PATH "
                "or Custom in Project Settings > Scripting.");
        }

#if defined(_WIN32)
        char* rawDotnetRoot = nullptr;
        std::size_t dotnetRootSize = 0;
        if (_dupenv_s(&rawDotnetRoot, &dotnetRootSize, "DOTNET_ROOT") != 0)
            rawDotnetRoot = nullptr;
        const std::unique_ptr<char, decltype(&std::free)> dotnetRoot(rawDotnetRoot, &std::free);
        const char* dotnetRootValue = dotnetRoot ? dotnetRoot.get() : nullptr;
#else
        const char* dotnetRoot = std::getenv("DOTNET_ROOT");
        const char* dotnetRootValue = dotnetRoot;
#endif
        if (dotnetRootValue)
        {
            const auto candidate = std::filesystem::path(dotnetRootValue) / executableName;
            if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                return std::filesystem::absolute(candidate).lexically_normal();
        }

#if defined(_WIN32)
        char* rawEnvironment = nullptr;
        std::size_t environmentSize = 0;
        if (_dupenv_s(&rawEnvironment, &environmentSize, "PATH") != 0)
            rawEnvironment = nullptr;
        const std::unique_ptr<char, decltype(&std::free)> environment(rawEnvironment, &std::free);
        const std::string paths = environment ? std::string(environment.get()) : std::string{};
#else
        const char* environment = std::getenv("PATH");
        const std::string paths = environment ? std::string(environment) : std::string{};
#endif
        if (paths.empty())
            throw std::runtime_error("dotnet was not found because PATH is unavailable.");
#if defined(_WIN32)
        constexpr char separator = ';';
        const std::filesystem::path executable = executableName;
#else
        constexpr char separator = ':';
        const std::filesystem::path executable = executableName;
#endif
        std::size_t begin = 0;
        while (begin <= paths.size())
        {
            const auto end = paths.find(separator, begin);
            const auto candidate = std::filesystem::path(paths.substr(begin, end - begin)) / executable;
            if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                return std::filesystem::absolute(candidate).lexically_normal();
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        throw std::runtime_error(
            "The .NET 10 SDK was not found on PATH. Choose Bundled or Custom in Project Settings > Scripting.");
    }
} // namespace Keire::Detail
