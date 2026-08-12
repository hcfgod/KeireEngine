#include "Keire/EntryPoint.h"

#include "Keire/BuildInfo.h"
#include "Keire/Log.h"

#include "KeireInternal/Process.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#if defined(_MSC_VER) && !defined(__clang__) && !defined(NDEBUG)
#include <rtcapi.h>
#endif
#endif

namespace
{
#if defined(_WIN32) && defined(_MSC_VER) && !defined(__clang__) && !defined(NDEBUG)
    int RuntimeCheckFailure(const int error, const char* file, const int line, const char* module, const char* format,
                            ...)
    {
        std::fprintf(stderr, "MSVC runtime check %d at %s:%d in %s: ", error, file ? file : "<unknown>", line,
                     module ? module : "<unknown>");
        std::va_list arguments;
        va_start(arguments, format);
        std::vfprintf(stderr, format ? format : "<no diagnostic>", arguments);
        va_end(arguments);
        std::fputc('\n', stderr);

        void* frames[64]{};
        const auto count = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
        for (USHORT index = 0; index < count; ++index)
        {
            HMODULE image = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(frames[index]), &image))
            {
                wchar_t path[MAX_PATH]{};
                const auto length = GetModuleFileNameW(image, path, static_cast<DWORD>(std::size(path)));
                const auto offset =
                    reinterpret_cast<std::uintptr_t>(frames[index]) - reinterpret_cast<std::uintptr_t>(image);
                char utf8Path[MAX_PATH * 4]{};
                const auto utf8Length =
                    WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length), utf8Path,
                                        static_cast<int>(std::size(utf8Path) - 1), nullptr, nullptr);
                if (utf8Length > 0)
                    std::fprintf(stderr, "  #%hu %.*s+0x%zx\n", index, utf8Length, utf8Path, offset);
                else
                    std::fprintf(stderr, "  #%hu %p+0x%zx\n", index, static_cast<void*>(image), offset);
            }
            else
                std::fprintf(stderr, "  #%hu %p\n", index, frames[index]);
        }
        std::fflush(stderr);
        std::_Exit(3);
    }

    void ConfigureRuntimeCheckDiagnostics() noexcept { (void)_RTC_SetErrorFunc(RuntimeCheckFailure); }
#else
    void ConfigureRuntimeCheckDiagnostics() noexcept {}
#endif

    void ConfigureConsoleEncoding(const Keire::ApplicationCommandLineArguments& arguments) noexcept
    {
#if defined(_WIN32)
        const bool informational = arguments.Size() == 2 && (arguments[1] == "--help" || arguments[1] == "-h" ||
                                                             arguments[1] == "--version" || arguments[1] == "-v");
        const auto validHandle = [](const DWORD kind)
        {
            const auto handle = GetStdHandle(kind);
            return handle != nullptr && handle != INVALID_HANDLE_VALUE;
        };
        if (GetConsoleCP() == 0 && informational && !validHandle(STD_OUTPUT_HANDLE) &&
            AttachConsole(ATTACH_PARENT_PROCESS))
        {
            FILE* stream = nullptr;
            (void)freopen_s(&stream, "CONOUT$", "w", stdout);
            (void)freopen_s(&stream, "CONOUT$", "w", stderr);
        }
        if (GetConsoleCP() != 0)
        {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
        }
#else
        (void)arguments;
#endif
    }

    void ReportClientFailure(const std::string_view message) noexcept
    {
        std::fprintf(stderr, "Client failed: %.*s\n", static_cast<int>(message.size()), message.data());
        try
        {
            KEIRE_CLIENT_CRITICAL("Client failed: {}", message);
            Keire::Log::Flush();
        }
        catch (...)
        {
        }
    }

    bool IsHelpArgument(const std::string_view argument) noexcept { return argument == "--help" || argument == "-h"; }

    bool IsVersionArgument(const std::string_view argument) noexcept
    {
        return argument == "--version" || argument == "-v";
    }

    void PrintOption(const std::string_view syntax, const std::string_view description)
    {
        std::printf("  %-22.*s %.*s\n", static_cast<int>(syntax.size()), syntax.data(),
                    static_cast<int>(description.size()), description.data());
    }

    void PrintHelp(const std::string_view executable)
    {
        const auto client = Keire::GetApplicationCommandLineDescription();
        std::printf("Usage: %.*s", static_cast<int>(executable.size()), executable.data());
        if (!client.UsageSuffix.empty())
        {
            std::printf(" %.*s", static_cast<int>(client.UsageSuffix.size()), client.UsageSuffix.data());
        }
        std::fputs("\n\nOptions:\n", stdout);
        PrintOption("-h, --help", "Show this help without initializing engine services.");
        PrintOption("-v, --version", "Show build information without initializing engine services.");
        for (const auto& option : client.Options)
        {
            PrintOption(option.Syntax, option.Description);
        }
    }

    void PrintVersion()
    {
        const auto& info = Keire::GetBuildInfo();
        std::printf("%.*s %s\n", static_cast<int>(info.ProjectName.size()), info.ProjectName.data(),
                    Keire::GetVersionString().c_str());
    }

    std::optional<int> HandleInformationalCommand(const Keire::ApplicationCommandLineArguments& arguments)
    {
        if (arguments.Size() == 2 && IsHelpArgument(arguments[1]))
        {
            PrintHelp(arguments.Executable());
            return 0;
        }
        if (arguments.Size() == 2 && IsVersionArgument(arguments[1]))
        {
            PrintVersion();
            return 0;
        }

        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            if (IsHelpArgument(arguments[index]) || IsVersionArgument(arguments[index]))
            {
                throw Keire::CommandLineError("--help and --version must be used alone.");
            }
        }
        return std::nullopt;
    }

    int RunClient(const int argc, char* const* argv)
    {
        const Keire::ApplicationCommandLineArguments arguments(argc, argv);
        ConfigureConsoleEncoding(arguments);

        try
        {
            if (const auto result = HandleInformationalCommand(arguments))
            {
                return *result;
            }

            auto application = Keire::CreateApplication(arguments);
            if (!application)
            {
                throw std::runtime_error("CreateApplication returned a null application.");
            }
            return application->Run();
        }
        catch (const Keire::CommandLineError& exception)
        {
            std::fprintf(stderr, "%s\n", exception.what());
            return 2;
        }
        catch (const std::exception& exception)
        {
            ReportClientFailure(exception.what());
        }
        catch (...)
        {
            ReportClientFailure("unknown exception");
        }
        return 1;
    }
} // namespace

namespace Keire
{
    ApplicationCommandLineArguments::ApplicationCommandLineArguments(const int count, char* const* values) noexcept
        : m_Count(count > 0 && values ? static_cast<std::size_t>(count) : 0), m_Values(values)
    {
    }

    std::string_view ApplicationCommandLineArguments::operator[](const std::size_t index) const noexcept
    {
        if (index >= m_Count || !m_Values[index])
        {
            return {};
        }
        return m_Values[index];
    }

    std::string_view ApplicationCommandLineArguments::Executable() const noexcept
    {
        const auto executable = (*this)[0];
        return executable.empty() ? std::string_view{"KeireClient"} : executable;
    }
} // namespace Keire

int main(const int argc, char* argv[])
{
    ConfigureRuntimeCheckDiagnostics();
    try
    {
        Keire::Detail::Utf8CommandLine commandLine(argc, argv);
        return RunClient(commandLine.Count(), commandLine.Values());
    }
    catch (const std::exception& exception)
    {
        ReportClientFailure(exception.what());
    }
    catch (...)
    {
        ReportClientFailure("unknown exception");
    }
    return 1;
}
