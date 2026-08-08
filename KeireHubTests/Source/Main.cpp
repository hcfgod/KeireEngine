#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <KeireHubTests/TestSupport.h>

#include "KeireHub/HubInstance.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

int main(const int argc, char** argv)
{
    KeireHubTests::ExecutablePath = std::filesystem::absolute(argv[0]);
    if (argc > 1 && std::string_view(argv[1]) == "--hub-instance-secondary")
    {
        if (argc < 3)
            return 30;
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 3));
        for (int index = 3; index < argc; ++index)
            arguments.emplace_back(argv[index]);

        auto activation = KeireHub::ParseHubActivationArguments(arguments);
        if (!activation)
            return 30;
        try
        {
            const KeireHub::HubInstanceCoordinator instance(argv[2], activation.Value(), true);
            return instance.IsPrimary() ? 31 : 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 32;
        }
    }

    doctest::Context context(argc, argv);
    return context.run();
}
