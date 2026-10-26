#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "Keire/Core.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireTests/TestSupport.h"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

int main(const int argc, char** argv)
{
    KeireTests::TestExecutable = std::filesystem::absolute(argv[0]);
    if (argc > 4 && std::strcmp(argv[2], "-s") == 0 && std::strcmp(argv[3], "HLSL") == 0 &&
        std::filesystem::path(argv[1]).filename() == "cancellation-probe.hlsl")
    {
        const auto source = KeireTests::ReadFile(argv[1]);
        constexpr std::string_view markerPrefix = "// cancellation-marker-root=";
        if (!source.starts_with(markerPrefix))
        {
            return 2;
        }
        const auto markerRoot =
            Keire::Detail::PathFromUtf8(source.substr(markerPrefix.size(), source.find('\n') - markerPrefix.size()));
        for (int index = 4; index + 1 < argc; ++index)
        {
            if (std::strcmp(argv[index], "-t") == 0)
            {
                const std::string stage = argv[index + 1];
                if (stage != "vertex" && stage != "fragment")
                {
                    return 2;
                }
                Keire::Detail::WriteTextFileAtomically(markerRoot / (stage + ".txt"),
                                                       std::to_string(Keire::Detail::CurrentProcessId()));
                std::this_thread::sleep_for(std::chrono::minutes(5));
                return 0;
            }
        }
        return 2;
    }
    if (argc == 2 && std::strcmp(argv[1], "--core-assert-probe") == 0)
    {
        KEIRE_ASSERT(false, "assertion probe");
        return 2;
    }
    if (argc == 2 && std::strcmp(argv[1], "--child-process-probe") == 0)
    {
        std::cout << "child-process-output\n";
        return 23;
    }
    if (argc == 2 && std::strcmp(argv[1], "--child-process-success") == 0)
    {
        return 0;
    }
    if (argc == 2 && (std::strcmp(argv[1], "--child-process-large-output") == 0 ||
                      std::strcmp(argv[1], "--child-process-capped-output") == 0))
    {
        const std::size_t outputBytes = std::strcmp(argv[1], "--child-process-large-output") == 0
                                            ? std::size_t{384} * 1024U
                                            : std::size_t{5} * 1024U * 1024U;
        const std::string block(4096, 'x');
        for (std::size_t written = 0; written < outputBytes; written += block.size())
        {
            std::cout << block;
        }
        std::cout << std::flush;
        std::cerr << "diagnostic-tail\n";
        return 37;
    }
    if (argc == 3 && (std::strcmp(argv[1], "--child-process-record-hang") == 0 ||
                      std::strcmp(argv[1], "--child-process-record-flood") == 0))
    {
        Keire::Detail::WriteTextFileAtomically(argv[2], std::to_string(Keire::Detail::CurrentProcessId()));
        if (std::strcmp(argv[1], "--child-process-record-flood") == 0)
        {
            const std::string output(4096, 'x');
            for (;;)
            {
                std::cout << output << std::flush;
            }
        }
        std::this_thread::sleep_for(std::chrono::minutes(5));
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--child-process-hang") == 0)
    {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        return 0;
    }
#if defined(_WIN32)
    if (argc == 3 && std::strcmp(argv[1], "--child-inherited-event-probe") == 0)
    {
        const auto value = std::strtoull(argv[2], nullptr, 10);
        const auto event = std::bit_cast<HANDLE>(static_cast<std::uintptr_t>(value));
        return SetEvent(event) ? 91 : 0;
    }
#endif
    doctest::Context context(argc, argv);
    return context.run();
}
