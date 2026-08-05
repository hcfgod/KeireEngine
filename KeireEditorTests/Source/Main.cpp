#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "EditorTestSupport.h"
#include "KeireHub/HubInstance.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <thread>

namespace
{
    [[nodiscard]] bool TestWorkerMode(const char* expected)
    {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "KEIRE_EDITOR_TEST_WORKER_MODE") != 0)
            return false;
        const bool matches = value && std::strcmp(value, expected) == 0;
        std::free(value);
        return matches;
#else
        const auto* value = std::getenv("KEIRE_EDITOR_TEST_WORKER_MODE");
        return value && std::strcmp(value, expected) == 0;
#endif
    }
} // namespace

int main(const int argc, char** argv)
{
    KeireEditorTests::ExecutablePath = std::filesystem::absolute(argv[0]);
    if (argc >= 4 && std::strcmp(argv[1], "--hub-instance-secondary") == 0)
    {
        KeireHub::HubActivationRequest activation;
        if (std::strcmp(argv[3], "build-support") == 0)
        {
            if (argc != 6)
                return 30;
            activation.Platform = argv[4];
            activation.Architecture = argv[5];
        }
        else if (std::strcmp(argv[3], "show") != 0)
            return 31;

        try
        {
            const KeireHub::HubInstanceCoordinator instance(argv[2], activation, true);
            return instance.IsPrimary() ? 32 : 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 33;
        }
    }
    if (argc > 1 && std::strcmp(argv[1], "--request") == 0)
    {
        if (TestWorkerMode("hang"))
        {
            std::cout << "test worker deliberately hung\n" << std::flush;
            std::this_thread::sleep_for(std::chrono::minutes(5));
            return 0;
        }
        if (TestWorkerMode("malformed"))
        {
            std::cout << "test worker deliberately omitted its result\n";
            return 0;
        }
    }
    doctest::Context context(argc, argv);
    return context.run();
}
