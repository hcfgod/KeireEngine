#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "Keire/Core.h"

#include <cstring>

int main(const int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--core-assert-probe") == 0)
    {
        KEIRE_ASSERT(false, "assertion probe");
        return 2;
    }
    doctest::Context context(argc, argv);
    return context.run();
}
