#include "Keire/Rendering/ComputeCompilation.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireInternal/Rendering/ComputeSpirvReflection.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire
{
    namespace
    {
        class ComputeCompilerDirectory final
        {
          public:
            ComputeCompilerDirectory()
            {
                m_Path = std::filesystem::temp_directory_path() / ("KeireCompute-" + AssetId::Generate().ToString());
                if (!std::filesystem::create_directory(m_Path))
                    throw std::runtime_error("Could not create a compute compiler workspace.");
            }

            ~ComputeCompilerDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(m_Path, ignored);
            }

            ComputeCompilerDirectory(const ComputeCompilerDirectory&) = delete;
            ComputeCompilerDirectory& operator=(const ComputeCompilerDirectory&) = delete;
            [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

          private:
            std::filesystem::path m_Path;
        };

        void RunComputeCompiler(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                                const std::filesystem::path& directory, const std::chrono::milliseconds timeout)
        {
            const auto result = Detail::RunProcess(executable, arguments, directory, timeout);
            if (result.TimedOut)
                throw std::runtime_error("Compute compiler timed out.\n" + result.Output);
            if (result.ExitCode != 0)
                throw std::runtime_error("Compute compiler failed.\n" + result.Output);
        }

        [[nodiscard]] std::vector<std::byte> ReadComputeBinary(const std::filesystem::path& directory,
                                                               const std::filesystem::path& relative,
                                                               const std::size_t maximumBytes)
        {
            auto bytes = Detail::AnchoredFileSystem(directory).Read(relative, maximumBytes);
            if (bytes.empty())
                throw std::runtime_error("Compute compiler produced an empty output.");
            return bytes;
        }

        void VerifyComputeReflection(const nlohmann::json& actual, const ProgramReflection& expected)
        {
            std::uint32_t readonlyBuffers = 0;
            std::uint32_t writableBuffers = 0;
            std::uint32_t uniforms = 0;
            std::set<std::uint32_t> readonlySlots;
            std::set<std::uint32_t> writableSlots;
            for (const auto& resource : expected.Resources)
            {
                if (resource.ArrayCount != 1)
                    throw std::invalid_argument("Compute compilation does not support resource arrays.");
                if (resource.Kind == ProgramResourceKind::Uniform && resource.Space == 2 && resource.Binding == 0 &&
                    resource.Access == ProgramResourceAccess::ReadOnly)
                {
                    ++uniforms;
                }
                else if ((resource.Kind == ProgramResourceKind::StructuredBuffer ||
                          resource.Kind == ProgramResourceKind::ByteAddressBuffer) &&
                         resource.Space == 0 && resource.Access == ProgramResourceAccess::ReadOnly)
                {
                    ++readonlyBuffers;
                    readonlySlots.insert(resource.Binding);
                }
                else if (resource.Kind == ProgramResourceKind::StorageBuffer && resource.Space == 1 &&
                         resource.Access != ProgramResourceAccess::ReadOnly)
                {
                    ++writableBuffers;
                    writableSlots.insert(resource.Binding);
                }
                else
                    throw std::invalid_argument("Compute compilation requires portable buffer and uniform bindings.");
            }
            if (uniforms > 1 || (!readonlySlots.empty() && *readonlySlots.rbegin() != readonlyBuffers - 1) ||
                (!writableSlots.empty() && *writableSlots.rbegin() != writableBuffers - 1))
                throw std::invalid_argument("Compute buffer registers must be contiguous from zero.");

            const std::array<std::pair<const char*, std::uint32_t>, 9> counts{{
                {"samplers", 0},
                {"readonly_storage_textures", 0},
                {"readonly_storage_buffers", readonlyBuffers},
                {"readwrite_storage_textures", 0},
                {"readwrite_storage_buffers", writableBuffers},
                {"uniform_buffers", uniforms},
                {"threadcount_x", expected.ThreadGroupSizeX},
                {"threadcount_y", expected.ThreadGroupSizeY},
                {"threadcount_z", expected.ThreadGroupSizeZ},
            }};
            for (const auto& [name, count] : counts)
                if (!actual.contains(name) || !actual.at(name).is_number_unsigned() ||
                    actual.at(name).get<std::uint64_t>() != count)
                    throw std::invalid_argument("Compiled compute reflection disagrees with '" + std::string(name) +
                                                "'.");
        }
    } // namespace

    ProgramArtifact CompileComputeProgramBinaries(const ProgramArtifact& artifact,
                                                  const ComputeCompilationOptions& options)
    {
        ValidateProgramArtifact(artifact);
        if (!artifact.Succeeded() || artifact.Target != ProgramTarget::Compute || options.Compiler.empty() ||
            !std::filesystem::is_regular_file(options.Compiler) || options.Timeout.count() <= 0 ||
            options.MaximumOutputBytes == 0 || options.MaximumOutputBytes > ProgramBinaryMaximumBytes ||
            options.Backends.empty() || options.Backends.size() > 3)
            throw std::invalid_argument("Compute compilation requires a valid compute artifact, compiler and limits.");
        std::set<ProgramBackend> backends;
        for (const auto backend : options.Backends)
        {
            if (backend > ProgramBackend::Metal || !backends.insert(backend).second)
                throw std::invalid_argument("Compute compilation backends must be valid and unique.");
#if !defined(__APPLE__)
            if (backend == ProgramBackend::Metal)
                throw std::invalid_argument("Metallib compilation requires the macOS Xcode toolchain.");
#endif
        }
        const auto compiler = std::filesystem::absolute(options.Compiler);
        ComputeCompilerDirectory temporary;
        const auto& directory = temporary.Path();
        auto result = artifact;
        for (auto& variant : result.Variants)
        {
            variant.Binaries.clear();
            // Each variant replaces these private files. No source or binary is published until the complete
            // returned artifact passes validation, so a late compiler failure cannot partially update the caller.
            Detail::WriteTextFileAtomically(directory / "kernel.hlsl", variant.Hlsl);
            const auto& entry = artifact.Reflection.EntryPoints.front().Name;
            const auto compile = [&](const std::string& format, const std::string& output)
            {
                RunComputeCompiler(
                    compiler, {"kernel.hlsl", "-s", "HLSL", "-d", format, "-t", "compute", "-e", entry, "-o", output},
                    directory, options.Timeout);
            };
            compile("SPIRV", "kernel.spv");
            Detail::ValidateComputeSpirvReflection(
                ReadComputeBinary(directory, "kernel.spv", options.MaximumOutputBytes), artifact.Reflection);
            RunComputeCompiler(
                compiler,
                {"kernel.spv", "-s", "SPIRV", "-d", "JSON", "-t", "compute", "-e", entry, "-o", "reflection.json"},
                directory, options.Timeout);
            VerifyComputeReflection(nlohmann::json::parse(Detail::ReadTextFile(directory / "reflection.json", 65536)),
                                    artifact.Reflection);
            for (const auto backend : options.Backends)
            {
                ProgramStageBinary binary;
                binary.Backend = backend;
                binary.Stage = ProgramStage::Compute;
                binary.EntryPoint = entry;
                binary.Reflection = artifact.Reflection;
                std::filesystem::path output = "kernel.spv";
                binary.Format = ProgramBinaryFormat::SpirV;
                if (backend == ProgramBackend::D3D12)
                {
                    compile("DXIL", "kernel.dxil");
                    output = "kernel.dxil";
                    binary.Format = ProgramBinaryFormat::Dxil;
                }
#if defined(__APPLE__)
                else if (backend == ProgramBackend::Metal)
                {
                    compile("MSL", "kernel.metal");
                    RunComputeCompiler("/usr/bin/xcrun",
                                       {"-sdk", "macosx", "metal", "-c", "kernel.metal", "-o", "kernel.air"}, directory,
                                       options.Timeout);
                    RunComputeCompiler("/usr/bin/xcrun",
                                       {"-sdk", "macosx", "metallib", "kernel.air", "-o", "kernel.metallib"}, directory,
                                       options.Timeout);
                    output = "kernel.metallib";
                    binary.Format = ProgramBinaryFormat::Metallib;
                }
#endif
                binary.Bytes = ReadComputeBinary(directory, output, options.MaximumOutputBytes);
                binary.Sha256 = Detail::DigestToString(Detail::Sha256(binary.Bytes));
                variant.Binaries.push_back(std::move(binary));
            }
        }
        ValidateCookedProgramArtifact(result);
        return result;
    }
} // namespace Keire
