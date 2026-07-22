#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

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
    doctest::Context context(argc, argv);
    return context.run();
}
