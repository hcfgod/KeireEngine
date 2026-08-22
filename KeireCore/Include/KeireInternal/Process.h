#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    class Utf8CommandLine final
    {
      public:
        Utf8CommandLine(int count, char* const* values);

        Utf8CommandLine(const Utf8CommandLine&) = delete;
        Utf8CommandLine& operator=(const Utf8CommandLine&) = delete;
        Utf8CommandLine(Utf8CommandLine&&) = delete;
        Utf8CommandLine& operator=(Utf8CommandLine&&) = delete;

        [[nodiscard]] int Count() const noexcept { return static_cast<int>(m_Arguments.size()); }
        [[nodiscard]] char* const* Values() noexcept { return m_Pointers.data(); }

      private:
        std::vector<std::string> m_Arguments;
        std::vector<char*> m_Pointers;
    };

    class ChildProcess final
    {
      public:
        class Impl;

        ChildProcess(const ChildProcess&) = delete;
        ChildProcess& operator=(const ChildProcess&) = delete;
        ChildProcess(ChildProcess&&) noexcept;
        ChildProcess& operator=(ChildProcess&&) noexcept;
        ~ChildProcess();

        [[nodiscard]] static ChildProcess Start(const std::filesystem::path& executable,
                                                std::span<const std::string> arguments,
                                                const std::filesystem::path& workingDirectory);
        [[nodiscard]] bool Poll();
        [[nodiscard]] bool Running() const noexcept;
        [[nodiscard]] std::uint64_t ProcessId() const noexcept;
        [[nodiscard]] std::optional<int> ExitCode() const noexcept;
        [[nodiscard]] std::string TakeOutput();
        [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout);
        void Terminate() noexcept;

      private:
        explicit ChildProcess(std::unique_ptr<Impl> implementation) noexcept;
        std::unique_ptr<Impl> m_Impl;
    };

    struct ProcessResult
    {
        int ExitCode = -1;
        bool TimedOut = false;
        std::string Output;
    };

    [[nodiscard]] ProcessResult RunProcess(const std::filesystem::path& executable,
                                           std::span<const std::string> arguments,
                                           const std::filesystem::path& workingDirectory,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] bool LaunchDetachedProcess(const std::filesystem::path& executable,
                                             std::span<const std::string> arguments,
                                             const std::filesystem::path& workingDirectory, std::string& diagnostic,
                                             std::uint64_t* processId = nullptr) noexcept;
    [[nodiscard]] bool LaunchDetachedProcessAtDesktopUserIntegrity(const std::filesystem::path& executable,
                                                                   std::span<const std::string> arguments,
                                                                   const std::filesystem::path& workingDirectory,
                                                                   std::string& diagnostic,
                                                                   std::uint64_t* processId = nullptr) noexcept;
    [[nodiscard]] std::uint64_t CurrentProcessId() noexcept;
    [[nodiscard]] bool IsCurrentProcessElevated() noexcept;
    [[nodiscard]] bool IsProcessAlive(std::uint64_t processId) noexcept;
    [[nodiscard]] std::filesystem::path ResolveCompanionExecutable(const std::filesystem::path& executable,
                                                                   std::string_view companionTarget);
    [[nodiscard]] std::filesystem::path
    ResolveManagedSolutionForExternalEditor(const std::filesystem::path& path,
                                            const std::filesystem::path& workingDirectory, bool reuseManagedSession);
    [[nodiscard]] bool OpenInExternalEditor(const std::filesystem::path& path,
                                            const std::filesystem::path& preferredEditor,
                                            const std::filesystem::path& workingDirectory, std::string& diagnostic,
                                            bool reuseManagedSession = false) noexcept;
    [[nodiscard]] bool RevealInFileManager(const std::filesystem::path& path, std::string& diagnostic) noexcept;
} // namespace Keire::Detail
#include <chrono>
