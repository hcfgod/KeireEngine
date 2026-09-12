#pragma once

#include "Keire/Rendering/ProgramArtifact.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace Keire
{
    struct ComputeCompilationOptions
    {
        /// The pinned KeireShaderCompiler executable supplied by the engine toolchain.
        std::filesystem::path Compiler;
        std::vector<ProgramBackend> Backends{ProgramBackend::D3D12, ProgramBackend::Vulkan};
        std::chrono::milliseconds Timeout{30000};
        std::size_t MaximumOutputBytes = ProgramBinaryMaximumBytes;
    };

    /// Compiles self-contained HLSL kernels and verifies their resource counts and thread groups using SPIR-V.
    /// Returns a new artifact only after all requested variants/backends succeed. Existing binaries are replaced.
    /// Metal requires execution on macOS with the Xcode command-line toolchain installed.
    [[nodiscard]] KEIRE_API ProgramArtifact CompileComputeProgramBinaries(const ProgramArtifact& artifact,
                                                                          const ComputeCompilationOptions& options);
} // namespace Keire
