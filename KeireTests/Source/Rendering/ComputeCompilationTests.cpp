#include "Keire/Rendering/ComputeCompilation.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>

namespace
{
    Keire::ComputeCompilationOptions ComputeCompilerOptions()
    {
        Keire::ComputeCompilationOptions options;
#if defined(_WIN32)
        options.Compiler = std::filesystem::current_path() / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
        options.Compiler = std::filesystem::current_path() / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
        return options;
    }
} // namespace

TEST_CASE("compute binary compilation verifies production compiler outputs and preserves source artifact")
{
    const auto source =
        Keire::CompileShaderGraphProgram(Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute));
    REQUIRE(source.Succeeded());
    auto options = ComputeCompilerOptions();
    REQUIRE(std::filesystem::is_regular_file(options.Compiler));
    const auto compiled = Keire::CompileComputeProgramBinaries(source, options);
    CHECK_NOTHROW(Keire::ValidateCookedProgramArtifact(compiled));
    CHECK(source.Variants.front().Binaries.empty());
    REQUIRE(compiled.Variants.front().Binaries.size() == 2);
    CHECK(compiled.Variants.front().Binaries.front().Format == Keire::ProgramBinaryFormat::Dxil);
    CHECK(compiled.Variants.front().Binaries.back().Format == Keire::ProgramBinaryFormat::SpirV);
    CHECK(compiled.Variants.front().Binaries.front().Reflection == source.Reflection);

    auto mismatched = source;
    mismatched.Reflection.ThreadGroupSizeX = 32;
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(mismatched, options), std::invalid_argument);
    CHECK(mismatched.Variants.front().Binaries.empty());
    mismatched = source;
    REQUIRE_FALSE(mismatched.Reflection.Resources.empty());
    mismatched.Reflection.Resources.front().Symbol = "WrongBufferName";
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(mismatched, options), std::invalid_argument);
    CHECK(mismatched.Variants.front().Binaries.empty());
    mismatched = source;
    mismatched.Reflection.Resources.front().StrideBytes = 32;
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(mismatched, options), std::invalid_argument);
    CHECK(mismatched.Variants.front().Binaries.empty());
    mismatched = source;
    mismatched.Reflection.Resources.clear();
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(mismatched, options), std::invalid_argument);

    auto broken = source;
    broken.Variants.front().Hlsl = "invalid HLSL source";
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(broken, options), std::runtime_error);
    CHECK(broken.Variants.front().Binaries.empty());

    options.MaximumOutputBytes = 1;
    CHECK_THROWS(Keire::CompileComputeProgramBinaries(source, options));
    CHECK(source.Variants.front().Binaries.empty());
}

TEST_CASE("compute binary compilation rejects unsupported requests before producing artifacts")
{
    const auto source =
        Keire::CompileShaderGraphProgram(Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute));
    REQUIRE(source.Succeeded());
    auto options = ComputeCompilerOptions();
    options.Backends = {Keire::ProgramBackend::Vulkan, Keire::ProgramBackend::Vulkan};
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(source, options), std::invalid_argument);
    options.Backends.clear();
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(source, options), std::invalid_argument);
    options = ComputeCompilerOptions();
    options.Timeout = std::chrono::milliseconds::zero();
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(source, options), std::invalid_argument);
    options = ComputeCompilerOptions();
    options.Compiler.clear();
    CHECK_THROWS_AS(Keire::CompileComputeProgramBinaries(source, options), std::invalid_argument);
#if !defined(__APPLE__)
    options = ComputeCompilerOptions();
    options.Backends = {Keire::ProgramBackend::Metal};
    CHECK_THROWS_WITH_AS(Keire::CompileComputeProgramBinaries(source, options),
                         "Metallib compilation requires the macOS Xcode toolchain.", std::invalid_argument);
#endif
}
