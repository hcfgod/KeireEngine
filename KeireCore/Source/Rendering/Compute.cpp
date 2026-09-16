#include "Keire/Rendering/Compute.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        struct CommandGuard
        {
            SDL_GPUCommandBuffer* Value = nullptr;
            ~CommandGuard()
            {
                if (Value)
                    (void)SDL_CancelGPUCommandBuffer(Value);
            }
        };

        struct TransferGuard
        {
            SDL_GPUDevice* Device;
            SDL_GPUTransferBuffer* Value;
            ~TransferGuard()
            {
                if (Value)
                    SDL_ReleaseGPUTransferBuffer(Device, Value);
            }
        };

        struct FenceGuard
        {
            SDL_GPUDevice* Device;
            SDL_GPUFence* Value;
            ~FenceGuard()
            {
                if (Value)
                    SDL_ReleaseGPUFence(Device, Value);
            }
        };

        void ValidateRange(const std::uint32_t total, const std::uint32_t offset, const std::size_t size)
        {
            if (size == 0 || offset > total || size > total - offset || offset % 4 != 0 || size % 4 != 0)
                throw std::invalid_argument(
                    "Compute buffer range must be nonempty, aligned to four bytes and in bounds.");
        }
    } // namespace

    class ComputeDevice::Impl
    {
      public:
        struct Buffer
        {
            SDL_GPUBuffer* Native = nullptr;
            std::uint32_t Size = 0;
            bool Indirect = false;
        };
        struct Pipeline
        {
            SDL_GPUComputePipeline* Native = nullptr;
            std::vector<ProgramResourceBinding> Read;
            std::vector<ProgramResourceBinding> Write;
            std::uint32_t UniformBytes = 0;
        };

        struct ReadbackRequest
        {
            SDL_GPUTransferBuffer* Transfer = nullptr;
            std::uint32_t Size = 0;
        };

        explicit Impl(const ProgramBackend backend, const bool debug) : Backend(backend), Debug(debug)
        {
            if (backend != ProgramBackend::D3D12 && backend != ProgramBackend::Vulkan &&
                backend != ProgramBackend::Metal)
                throw std::invalid_argument("Unknown compute backend.");
        }
        ~Impl() { Close(); }

        [[noreturn]] void Fail(const char* operation)
        {
            // Keep the native device alive while stack guards unwind. Shutdown owns final cleanup.
            Open = false;
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
        }

        void RequireOwner() const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error("Compute operations belong to the device construction thread.");
        }
        void RequireOpen() const
        {
            RequireOwner();
            if (!Open)
                throw std::logic_error("Compute device is shut down.");
        }
        template <typename Id> void RequireIdentity(const Id& id) const
        {
            RequireOpen();
            if (id.m_Owner != Identity || id.Value == 0)
                throw std::invalid_argument("Compute identity does not belong to this device.");
        }
        Buffer& Get(const ComputeBufferId& id)
        {
            RequireIdentity(id);
            const auto it = Buffers.find(id.Value);
            if (it == Buffers.end())
                throw std::invalid_argument("Compute buffer has been destroyed.");
            return it->second;
        }
        Pipeline& Get(const ComputePipelineId& id)
        {
            RequireIdentity(id);
            const auto it = Pipelines.find(id.Value);
            if (it == Pipelines.end())
                throw std::invalid_argument("Compute pipeline has been destroyed.");
            return it->second;
        }
        SDL_GPUFence*& Get(const ComputeSubmissionId& id)
        {
            RequireIdentity(id);
            const auto it = Submissions.find(id.Value);
            if (it == Submissions.end())
                throw std::invalid_argument("Compute submission has been released.");
            return it->second;
        }
        void EnsureDevice()
        {
            RequireOpen();
            if (Device)
                return;
            const auto format = Backend == ProgramBackend::D3D12    ? SDL_GPU_SHADERFORMAT_DXIL
                                : Backend == ProgramBackend::Vulkan ? SDL_GPU_SHADERFORMAT_SPIRV
                                                                    : SDL_GPU_SHADERFORMAT_METALLIB;
            const char* name = Backend == ProgramBackend::D3D12    ? "direct3d12"
                               : Backend == ProgramBackend::Vulkan ? "vulkan"
                                                                   : "metal";
            Device = SDL_CreateGPUDevice(format, Debug, name);
            if (!Device)
                Fail("Create compute GPU device");
        }
        void WaitFence(SDL_GPUFence* fence)
        {
            if (fence && !SDL_WaitForGPUFences(Device, true, &fence, 1))
                Fail("Wait for compute fence");
        }
        void Close() noexcept
        {
            Open = false;
            if (!Device)
                return;
            (void)SDL_WaitForGPUIdle(Device);
            for (const auto& [id, fence] : Submissions)
                if (fence)
                    SDL_ReleaseGPUFence(Device, fence);
            Submissions.clear();
            for (const auto& [id, request] : Readbacks)
                SDL_ReleaseGPUTransferBuffer(Device, request.Transfer);
            Readbacks.clear();
            for (const auto& [id, pipeline] : Pipelines)
                SDL_ReleaseGPUComputePipeline(Device, pipeline.Native);
            Pipelines.clear();
            for (const auto& [id, buffer] : Buffers)
                SDL_ReleaseGPUBuffer(Device, buffer.Native);
            Buffers.clear();
            SDL_DestroyGPUDevice(Device);
            Device = nullptr;
        }

        ProgramBackend Backend;
        bool Debug;
        std::atomic<bool> Open = true;
        const std::thread::id Owner = std::this_thread::get_id();
        const std::shared_ptr<const void> Identity = std::make_shared<int>(0);
        SDL_GPUDevice* Device = nullptr;
        std::uint64_t NextId = 1;
        std::map<std::uint64_t, Buffer> Buffers;
        std::map<std::uint64_t, Pipeline> Pipelines;
        std::map<std::uint64_t, SDL_GPUFence*> Submissions;
        std::map<std::uint64_t, ReadbackRequest> Readbacks;
    };

    ComputeDevice::ComputeDevice(const ProgramBackend backend, const bool debug)
        : m_Impl(std::make_unique<Impl>(backend, debug))
    {
    }
    ComputeDevice::~ComputeDevice() = default;

    ComputeBufferId ComputeDevice::CreateBuffer(const std::uint32_t size, const bool indirect)
    {
        auto& state = *m_Impl;
        state.RequireOpen();
        if (size == 0 || size % 4 != 0 || size > 128U * 1024U * 1024U || (indirect && size < 12))
            throw std::invalid_argument("Compute buffer size must be aligned, nonzero and at most 128 MiB.");
        if (state.Buffers.size() >= 4096)
            throw std::length_error("Compute buffer limit reached.");
        state.EnsureDevice();
        ComputeBufferId id;
        id.Value = state.NextId++;
        id.m_Owner = state.Identity;
        auto [it, inserted] = state.Buffers.emplace(id.Value, Impl::Buffer{nullptr, size, indirect});
        SDL_GPUBufferCreateInfo info{};
        info.size = size;
        info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        if (indirect)
            info.usage |= SDL_GPU_BUFFERUSAGE_INDIRECT;
        it->second.Native = SDL_CreateGPUBuffer(state.Device, &info);
        if (!it->second.Native)
        {
            state.Buffers.erase(it);
            state.Fail("Create compute buffer");
        }
        return id;
    }

    void ComputeDevice::DestroyBuffer(const ComputeBufferId buffer)
    {
        auto& value = m_Impl->Get(buffer);
        SDL_ReleaseGPUBuffer(m_Impl->Device, value.Native);
        m_Impl->Buffers.erase(buffer.Value);
    }

    ComputePipelineId ComputeDevice::CreatePipeline(const ProgramArtifact& artifact, const std::size_t variant)
    {
        auto& state = *m_Impl;
        state.RequireOpen();
        ValidateCookedProgramArtifact(artifact);
        if (artifact.Target != ProgramTarget::Compute || artifact.Stages != ProgramStage::Compute ||
            variant >= artifact.Variants.size())
            throw std::invalid_argument("Compute pipeline requires a cooked compute variant.");
        Impl::Pipeline pipeline;
        for (const auto& resource : artifact.Reflection.Resources)
        {
            if (resource.ArrayCount != 1)
                throw std::invalid_argument("Compute resource arrays are unsupported.");
            if (resource.Kind == ProgramResourceKind::Uniform)
            {
                if (pipeline.UniformBytes != 0 || resource.Space != 2 || resource.Binding != 0 ||
                    resource.StrideBytes == 0 || resource.StrideBytes > 65536 || resource.StrideBytes % 16 != 0)
                    throw std::invalid_argument("Compute requires one sized uniform block at b0, space2.");
                pipeline.UniformBytes = resource.StrideBytes;
            }
            else if (resource.Kind == ProgramResourceKind::StructuredBuffer ||
                     resource.Kind == ProgramResourceKind::ByteAddressBuffer ||
                     resource.Kind == ProgramResourceKind::StorageBuffer)
            {
                const bool writable = resource.Access != ProgramResourceAccess::ReadOnly;
                if (resource.Space != (writable ? 1U : 0U) || resource.Binding >= 16)
                    throw std::invalid_argument("Compute buffer register space or slot is unsupported.");
                (writable ? pipeline.Write : pipeline.Read).push_back(resource);
            }
            else
                throw std::invalid_argument("This compute device supports buffers and uniforms only.");
        }
        for (auto* resources : {&pipeline.Read, &pipeline.Write})
        {
            std::ranges::sort(*resources, {}, &ProgramResourceBinding::Binding);
            for (std::size_t slot = 0; slot < resources->size(); ++slot)
                if ((*resources)[slot].Binding != slot)
                    throw std::invalid_argument("Compute buffer registers must be unique and consecutive from zero.");
        }
        const auto& binaries = artifact.Variants[variant].Binaries;
        const auto binary = std::ranges::find_if(binaries,
                                                 [&state](const auto& value)
                                                 {
                                                     return value.Backend == state.Backend &&
                                                            value.Stage == ProgramStage::Compute &&
                                                            value.PassRole == "primary";
                                                 });
        if (binary == binaries.end())
            throw std::invalid_argument("Compute artifact has no binary for the requested backend.");
        if (binary->Reflection != artifact.Reflection)
            throw std::invalid_argument("Compute binary reflection differs from its artifact.");
        if (state.Pipelines.size() >= 4096)
            throw std::length_error("Compute pipeline limit reached.");
        state.EnsureDevice();
        ComputePipelineId id;
        id.Value = state.NextId++;
        id.m_Owner = state.Identity;
        auto [it, inserted] = state.Pipelines.emplace(id.Value, std::move(pipeline));
        SDL_GPUComputePipelineCreateInfo info{};
        info.code = reinterpret_cast<const Uint8*>(binary->Bytes.data());
        info.code_size = binary->Bytes.size();
        info.entrypoint = binary->EntryPoint.c_str();
        info.format = binary->Format == ProgramBinaryFormat::Dxil    ? SDL_GPU_SHADERFORMAT_DXIL
                      : binary->Format == ProgramBinaryFormat::SpirV ? SDL_GPU_SHADERFORMAT_SPIRV
                                                                     : SDL_GPU_SHADERFORMAT_METALLIB;
        info.num_readonly_storage_buffers = static_cast<Uint32>(it->second.Read.size());
        info.num_readwrite_storage_buffers = static_cast<Uint32>(it->second.Write.size());
        info.num_uniform_buffers = it->second.UniformBytes != 0 ? 1 : 0;
        info.threadcount_x = artifact.Reflection.ThreadGroupSizeX;
        info.threadcount_y = artifact.Reflection.ThreadGroupSizeY;
        info.threadcount_z = artifact.Reflection.ThreadGroupSizeZ;
        it->second.Native = SDL_CreateGPUComputePipeline(state.Device, &info);
        if (!it->second.Native)
        {
            state.Pipelines.erase(it);
            state.Fail("Create compute pipeline");
        }
        return id;
    }

    void ComputeDevice::ReloadPipeline(const ComputePipelineId pipeline, const ProgramArtifact& artifact,
                                       const std::size_t variant)
    {
        auto& current = m_Impl->Get(pipeline);
        const auto replacement = CreatePipeline(artifact, variant);
        auto& next = m_Impl->Get(replacement);
        std::swap(current, next);
        DestroyPipeline(replacement);
    }

    void ComputeDevice::DestroyPipeline(const ComputePipelineId pipeline)
    {
        auto& value = m_Impl->Get(pipeline);
        SDL_ReleaseGPUComputePipeline(m_Impl->Device, value.Native);
        m_Impl->Pipelines.erase(pipeline.Value);
    }

    void ComputeDevice::Upload(const ComputeBufferId buffer, const std::span<const std::byte> bytes,
                               const std::uint32_t offset)
    {
        auto& state = *m_Impl;
        auto& target = state.Get(buffer);
        ValidateRange(target.Size, offset, bytes.size());
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<Uint32>(bytes.size());
        TransferGuard transfer{state.Device, SDL_CreateGPUTransferBuffer(state.Device, &info)};
        if (!transfer.Value)
            state.Fail("Create compute upload transfer");
        void* mapped = SDL_MapGPUTransferBuffer(state.Device, transfer.Value, false);
        if (!mapped)
            state.Fail("Map compute upload");
        std::memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(state.Device, transfer.Value);
        CommandGuard commands{SDL_AcquireGPUCommandBuffer(state.Device)};
        if (!commands.Value)
            state.Fail("Acquire compute upload commands");
        auto* copy = SDL_BeginGPUCopyPass(commands.Value);
        if (!copy)
            state.Fail("Begin compute upload");
        const SDL_GPUTransferBufferLocation source{transfer.Value, 0};
        const SDL_GPUBufferRegion destination{target.Native, offset, info.size};
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        FenceGuard fence{state.Device,
                         SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(commands.Value, nullptr))};
        if (!fence.Value)
            state.Fail("Submit compute upload");
        state.WaitFence(fence.Value);
    }

    ComputeSubmissionId ComputeDevice::Dispatch(const ComputePipelineId pipeline,
                                                const std::span<const ComputeBufferBinding> bindings,
                                                const ComputeDispatchSize groups,
                                                const std::span<const std::byte> uniforms)
    {
        m_Impl->RequireOpen();
        if (groups.X == 0 || groups.Y == 0 || groups.Z == 0 || groups.X > 65535 || groups.Y > 65535 || groups.Z > 65535)
            throw std::invalid_argument("Compute group counts must be in 1..65535 on each axis.");
        return Submit(pipeline, bindings, groups, {}, 0, uniforms);
    }

    ComputeSubmissionId ComputeDevice::DispatchIndirect(const ComputePipelineId pipeline,
                                                        const std::span<const ComputeBufferBinding> bindings,
                                                        const ComputeBufferId arguments, const std::uint32_t offset,
                                                        const std::span<const std::byte> uniforms)
    {
        const auto& buffer = m_Impl->Get(arguments);
        if (!buffer.Indirect)
            throw std::invalid_argument("Compute argument buffer was not created for indirect dispatch.");
        ValidateRange(buffer.Size, offset, 12);
        return Submit(pipeline, bindings, {}, arguments, offset, uniforms);
    }

    ComputeSubmissionId ComputeDevice::Submit(const ComputePipelineId pipelineId,
                                              const std::span<const ComputeBufferBinding> bindings,
                                              const ComputeDispatchSize groups, const ComputeBufferId arguments,
                                              const std::uint32_t offset, const std::span<const std::byte> uniforms)
    {
        auto& state = *m_Impl;
        auto& pipeline = state.Get(pipelineId);
        if (bindings.size() != pipeline.Read.size() + pipeline.Write.size())
            throw std::invalid_argument("Compute bindings must cover every reflected buffer exactly once.");
        if (pipeline.UniformBytes != uniforms.size())
            throw std::invalid_argument("Compute uniform data must match the declared block and be 16-byte aligned.");
        std::vector<SDL_GPUBuffer*> read(pipeline.Read.size(), nullptr);
        std::vector<SDL_GPUStorageBufferReadWriteBinding> write(pipeline.Write.size());
        for (const auto& binding : bindings)
        {
            const auto& buffer = state.Get(binding.Buffer);
            const auto& reflection = binding.Writable ? pipeline.Write : pipeline.Read;
            if (binding.Slot >= reflection.size())
                throw std::invalid_argument("Compute buffer slot is outside reflection.");
            const auto stride = reflection[binding.Slot].StrideBytes;
            if (stride != 0 && buffer.Size % stride != 0)
                throw std::invalid_argument("Compute buffer size is not a multiple of its reflected stride.");
            if ((binding.Writable && write[binding.Slot].buffer) || (!binding.Writable && read[binding.Slot]))
                throw std::invalid_argument("Duplicate compute buffer slot.");
            for (const auto& other : bindings)
                if (&other != &binding && other.Buffer == binding.Buffer && (binding.Writable || other.Writable))
                    throw std::invalid_argument(
                        "Writable compute buffers cannot alias other bindings in one dispatch.");
            if (binding.Writable && arguments.Value != 0 && binding.Buffer == arguments)
                throw std::invalid_argument("Indirect arguments cannot be written by the same dispatch.");
            if (binding.Writable)
                write[binding.Slot].buffer = buffer.Native;
            else
                read[binding.Slot] = buffer.Native;
        }
        if (state.Submissions.size() >= 4096)
            throw std::length_error("Release compute submissions before submitting more work.");
        if (arguments.Value != 0)
        {
            // Validate GPU-produced arguments after preceding work completes, before issuing an indirect dispatch.
            const auto bytes = Readback(arguments, offset, 12);
            std::uint32_t dimensions[3]{};
            std::memcpy(dimensions, bytes.data(), sizeof(dimensions));
            for (const auto dimension : dimensions)
                if (dimension == 0 || dimension > 65535)
                    throw std::invalid_argument("Indirect compute group counts must be in 1..65535 on each axis.");
        }
        ComputeSubmissionId id;
        id.Value = state.NextId++;
        id.m_Owner = state.Identity;
        auto [record, inserted] = state.Submissions.emplace(id.Value, nullptr);
        try
        {
            CommandGuard commands{SDL_AcquireGPUCommandBuffer(state.Device)};
            if (!commands.Value)
                state.Fail("Acquire compute commands");
            if (!uniforms.empty())
                SDL_PushGPUComputeUniformData(commands.Value, 0, uniforms.data(), static_cast<Uint32>(uniforms.size()));
            auto* pass =
                SDL_BeginGPUComputePass(commands.Value, nullptr, 0, write.data(), static_cast<Uint32>(write.size()));
            if (!pass)
                state.Fail("Begin compute pass");
            SDL_BindGPUComputePipeline(pass, pipeline.Native);
            if (!read.empty())
                SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<Uint32>(read.size()));
            if (arguments.Value != 0)
                SDL_DispatchGPUComputeIndirect(pass, state.Get(arguments).Native, offset);
            else
                SDL_DispatchGPUCompute(pass, groups.X, groups.Y, groups.Z);
            SDL_EndGPUComputePass(pass);
            record->second = SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(commands.Value, nullptr));
            if (!record->second)
                state.Fail("Submit compute dispatch");
        }
        catch (...)
        {
            state.Submissions.erase(record);
            throw;
        }
        return id;
    }

    bool ComputeDevice::IsComplete(const ComputeSubmissionId submission)
    {
        const auto fence = m_Impl->Get(submission);
        return !fence || SDL_QueryGPUFence(m_Impl->Device, fence);
    }

    void ComputeDevice::Wait(const ComputeSubmissionId submission)
    {
        auto& fence = m_Impl->Get(submission);
        m_Impl->WaitFence(fence);
        if (fence)
        {
            SDL_ReleaseGPUFence(m_Impl->Device, fence);
            fence = nullptr;
        }
    }

    void ComputeDevice::ReleaseSubmission(const ComputeSubmissionId submission)
    {
        Wait(submission);
        if (const auto request = m_Impl->Readbacks.find(submission.Value); request != m_Impl->Readbacks.end())
        {
            SDL_ReleaseGPUTransferBuffer(m_Impl->Device, request->second.Transfer);
            m_Impl->Readbacks.erase(request);
        }
        m_Impl->Submissions.erase(submission.Value);
    }

    std::vector<std::byte> ComputeDevice::Readback(const ComputeBufferId buffer, const std::uint32_t offset,
                                                   std::uint32_t size)
    {
        auto& state = *m_Impl;
        const auto& sourceBuffer = state.Get(buffer);
        if (size == 0 && offset <= sourceBuffer.Size)
            size = sourceBuffer.Size - offset;
        ValidateRange(sourceBuffer.Size, offset, size);
        std::vector<std::byte> result(size);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = size;
        TransferGuard transfer{state.Device, SDL_CreateGPUTransferBuffer(state.Device, &info)};
        if (!transfer.Value)
            state.Fail("Create compute download transfer");
        CommandGuard commands{SDL_AcquireGPUCommandBuffer(state.Device)};
        if (!commands.Value)
            state.Fail("Acquire compute download commands");
        auto* copy = SDL_BeginGPUCopyPass(commands.Value);
        if (!copy)
            state.Fail("Begin compute download");
        const SDL_GPUBufferRegion source{sourceBuffer.Native, offset, size};
        const SDL_GPUTransferBufferLocation destination{transfer.Value, 0};
        SDL_DownloadFromGPUBuffer(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        FenceGuard fence{state.Device,
                         SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(commands.Value, nullptr))};
        if (!fence.Value)
            state.Fail("Submit compute download");
        state.WaitFence(fence.Value);
        const void* mapped = SDL_MapGPUTransferBuffer(state.Device, transfer.Value, false);
        if (!mapped)
            state.Fail("Map compute download");
        std::memcpy(result.data(), mapped, size);
        SDL_UnmapGPUTransferBuffer(state.Device, transfer.Value);
        return result;
    }

    ComputeSubmissionId ComputeDevice::RequestReadback(const ComputeBufferId buffer, const std::uint32_t offset,
                                                       std::uint32_t size)
    {
        auto& state = *m_Impl;
        const auto& sourceBuffer = state.Get(buffer);
        if (size == 0 && offset <= sourceBuffer.Size)
            size = sourceBuffer.Size - offset;
        ValidateRange(sourceBuffer.Size, offset, size);
        if (state.Submissions.size() >= 4096)
            throw std::length_error("Release compute submissions before submitting more work.");
        std::uint64_t pendingBytes = size;
        for (const auto& [key, pending] : state.Readbacks)
            pendingBytes += pending.Size;
        if (pendingBytes > 256ULL * 1024ULL * 1024ULL)
            throw std::length_error("Release compute readbacks before exceeding 256 MiB of pending snapshots.");
        ComputeSubmissionId id;
        id.Value = state.NextId++;
        id.m_Owner = state.Identity;
        auto [record, inserted] = state.Submissions.emplace(id.Value, nullptr);
        try
        {
            auto [request, added] = state.Readbacks.emplace(id.Value, Impl::ReadbackRequest{nullptr, size});
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            info.size = size;
            TransferGuard transfer{state.Device, SDL_CreateGPUTransferBuffer(state.Device, &info)};
            if (!transfer.Value)
                state.Fail("Create asynchronous compute download transfer");
            CommandGuard commands{SDL_AcquireGPUCommandBuffer(state.Device)};
            if (!commands.Value)
                state.Fail("Acquire asynchronous compute download commands");
            auto* copy = SDL_BeginGPUCopyPass(commands.Value);
            if (!copy)
                state.Fail("Begin asynchronous compute download");
            const SDL_GPUBufferRegion source{sourceBuffer.Native, offset, size};
            const SDL_GPUTransferBufferLocation destination{transfer.Value, 0};
            SDL_DownloadFromGPUBuffer(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
            record->second = SDL_SubmitGPUCommandBufferAndAcquireFence(std::exchange(commands.Value, nullptr));
            if (!record->second)
                state.Fail("Submit asynchronous compute download");
            request->second.Transfer = std::exchange(transfer.Value, nullptr);
        }
        catch (...)
        {
            state.Readbacks.erase(id.Value);
            state.Submissions.erase(record);
            throw;
        }
        return id;
    }

    std::vector<std::byte> ComputeDevice::GetReadback(const ComputeSubmissionId submission)
    {
        auto& state = *m_Impl;
        (void)state.Get(submission);
        const auto request = state.Readbacks.find(submission.Value);
        if (request == state.Readbacks.end())
            throw std::invalid_argument("Compute submission is not a readback request.");
        std::vector<std::byte> result(request->second.Size);
        Wait(submission);
        const void* mapped = SDL_MapGPUTransferBuffer(state.Device, request->second.Transfer, false);
        if (!mapped)
            state.Fail("Map asynchronous compute download");
        std::memcpy(result.data(), mapped, result.size());
        SDL_UnmapGPUTransferBuffer(state.Device, request->second.Transfer);
        return result;
    }

    void ComputeDevice::WaitIdle()
    {
        m_Impl->RequireOpen();
        if (m_Impl->Device && !SDL_WaitForGPUIdle(m_Impl->Device))
            m_Impl->Fail("Wait for compute device");
    }

    void ComputeDevice::Shutdown()
    {
        m_Impl->RequireOwner();
        m_Impl->Close();
    }

    bool ComputeDevice::IsOpen() const noexcept { return m_Impl->Open; }
} // namespace Keire
