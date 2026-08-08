#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

int main(const int argc, char** argv)
{
    KeireTests::TestExecutable = std::filesystem::absolute(argv[0]);
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
