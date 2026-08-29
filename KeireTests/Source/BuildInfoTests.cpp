#include "doctest/doctest.h"

#include "Keire/Core.h"

#include <string>

TEST_CASE("Build information is populated")
{
    const auto& info = Keire::GetBuildInfo();
    CHECK(info.ProjectName == "Kéire");
    CHECK(info.Version == "0.4.4");
    CHECK(info.RepositorySlug == "hcfgod/KeireEngine");
    CHECK_FALSE(info.GitCommit.empty());
    CHECK_FALSE(info.Configuration.empty());
    CHECK_FALSE(info.Compiler.empty());
    CHECK_FALSE(info.Platform.empty());
    CHECK_FALSE(info.Architecture.empty());
    CHECK_FALSE(Keire::GetVersionString().empty());
}

TEST_CASE("Assertions evaluate successful conditions once when enabled")
{
    int evaluations = 0;
    KEIRE_ASSERT(++evaluations == 1);
#if defined(KEIRE_ASSERTIONS_ENABLED)
    CHECK(evaluations == 1);
#else
    CHECK(evaluations == 0);
#endif
}

TEST_CASE("Core name is stable and does not initialize logging")
{
    Keire::Log::Shutdown();
    CHECK(std::string(Keire::GetName()) == "KeireCore");
    CHECK_NOTHROW(Keire::Log::Shutdown());
}
