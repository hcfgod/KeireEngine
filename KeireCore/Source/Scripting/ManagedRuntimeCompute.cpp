#include "KeireInternal/Scripting/ManagedRuntimeCompute.h"

#include "Keire/Rendering/Compute.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace Keire::Detail
{
    namespace
    {
        thread_local ManagedComputeStore* ActiveStore = nullptr;
        thread_local bool AllowWork = false;
        // Only identity sequencing is shared. No program, resource, or runtime ownership is process-global.
        std::atomic<std::uint64_t> NextIdentity{1};

        std::uint8_t CommandBridge(const std::uint64_t device, const std::uint8_t command, const std::uint64_t a,
                                   const std::uint64_t b, std::byte* bytes, const std::uint32_t size,
                                   std::uint64_t* result) noexcept
        {
            if (!ActiveStore || !result || (!bytes && size != 0))
                return 0;
            if (!AllowWork && command != 1 && command != 3 && command != 5 && command != 8 && command != 9 &&
                command != 10)
                return 0;
            try
            {
                *result = ActiveStore->Command(device, command, a, b, {bytes, size});
                return 1;
            }
            catch (...)
            {
                *result = 0;
                return 0;
            }
        }

        std::uint8_t DispatchBridge(const std::uint64_t device, const std::uint64_t pipeline,
                                    const ManagedComputeBinding* bindings, const std::uint32_t count,
                                    const std::uint32_t x, const std::uint32_t y, const std::uint32_t z,
                                    const std::uint64_t arguments, const std::uint32_t offset,
                                    const std::byte* uniforms, const std::uint32_t size, std::uint64_t* result) noexcept
        {
            if (!ActiveStore || !AllowWork || !result || (!bindings && count != 0) || (!uniforms && size != 0) ||
                count > ProgramResourceHardLimit)
                return 0;
            try
            {
                *result = ActiveStore->Dispatch(device, pipeline, {bindings, count}, x, y, z, arguments, offset,
                                                {uniforms, size});
                return 1;
            }
            catch (...)
            {
                *result = 0;
                return 0;
            }
        }
    } // namespace

    class ManagedComputeStore::Impl final
    {
      public:
        struct Device
        {
            std::unique_ptr<ComputeDevice> Native;
            std::map<std::uint64_t, ComputeBufferId> Buffers;
            std::map<std::uint64_t, ComputePipelineId> Pipelines;
            std::map<std::uint64_t, ComputeSubmissionId> Submissions;
        };

        void RequireOwner() const
        {
            if (Owner != std::this_thread::get_id())
                throw std::logic_error("Managed compute must run on the scripting owner thread.");
        }
        std::uint64_t Allocate()
        {
            auto next = NextIdentity.load(std::memory_order_relaxed);
            for (;;)
            {
                if (next == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Managed compute identities exhausted.");
                if (NextIdentity.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
                    return next;
            }
        }
        std::thread::id Owner = std::this_thread::get_id();
        std::map<std::uint64_t, ProgramArtifact> Programs;
        std::map<std::uint64_t, std::size_t> ProgramSizes;
        std::size_t ProgramBytes = 0;
        std::map<std::uint64_t, Device> Devices;
    };

    ManagedComputeStore::ManagedComputeStore() : m_Impl(std::make_unique<Impl>()) {}
    ManagedComputeStore::~ManagedComputeStore() = default;

    std::uint64_t ManagedComputeStore::RegisterProgram(const ProgramArtifact& artifact)
    {
        m_Impl->RequireOwner();
        ValidateCookedProgramArtifact(artifact);
        if (artifact.Target != ProgramTarget::Compute || artifact.Stages != ProgramStage::Compute)
            throw std::invalid_argument("Only a cooked compute program may be registered for managed dispatch.");
        if (m_Impl->Programs.size() >= 1024)
            throw std::length_error("Managed compute program registration limit reached.");
        constexpr std::size_t MaximumRegisteredBytes = 256ULL * 1024ULL * 1024ULL;
        std::size_t bytes = 0;
        const auto account = [&](const std::size_t count)
        {
            if (count > MaximumRegisteredBytes - m_Impl->ProgramBytes - bytes)
                throw std::length_error("Managed compute program storage limit reached.");
            bytes += count;
        };
        for (const auto& variant : artifact.Variants)
        {
            account(variant.Hlsl.size());
            account(variant.Manifest.size());
            for (const auto& binary : variant.Binaries)
                account(binary.Bytes.size());
        }
        const auto id = m_Impl->Allocate();
        auto [size, inserted] = m_Impl->ProgramSizes.emplace(id, bytes);
        (void)inserted;
        try
        {
            m_Impl->Programs.emplace(id, artifact);
        }
        catch (...)
        {
            m_Impl->ProgramSizes.erase(size);
            throw;
        }
        m_Impl->ProgramBytes += bytes;
        return id;
    }

    void ManagedComputeStore::UnregisterProgram(const std::uint64_t program)
    {
        m_Impl->RequireOwner();
        const auto found = m_Impl->ProgramSizes.find(program);
        if (found == m_Impl->ProgramSizes.end())
            return;
        m_Impl->ProgramBytes -= found->second;
        m_Impl->ProgramSizes.erase(found);
        m_Impl->Programs.erase(program);
    }

    void ManagedComputeStore::Clear() noexcept
    {
        ClearResources();
        m_Impl->Programs.clear();
        m_Impl->ProgramSizes.clear();
        m_Impl->ProgramBytes = 0;
    }

    void ManagedComputeStore::ClearResources() noexcept { m_Impl->Devices.clear(); }

    std::uint64_t ManagedComputeStore::Command(const std::uint64_t device, const std::uint8_t command,
                                               const std::uint64_t a, const std::uint64_t b,
                                               const std::span<std::byte> bytes)
    {
        m_Impl->RequireOwner();
        if (command == 0)
        {
            if (device != 0 || a > static_cast<std::uint64_t>(ProgramBackend::Metal))
                throw std::invalid_argument("Invalid managed compute backend.");
            if (m_Impl->Devices.size() >= 64)
                throw std::length_error("Managed compute device limit reached.");
            auto native = std::make_unique<ComputeDevice>(static_cast<ProgramBackend>(a));
            const auto id = m_Impl->Allocate();
            m_Impl->Devices.emplace(id, Impl::Device{std::move(native), {}, {}, {}});
            return id;
        }
        const bool releasing = command == 1 || command == 3 || command == 5 || command == 10;
        const auto found = m_Impl->Devices.find(device);
        // Disposed managed wrappers can outlive committed reload. Globally unique keys prevent this no-op from
        // releasing a resource owned by another device or runtime.
        if (releasing && found == m_Impl->Devices.end())
            return 0;
        auto& entry = m_Impl->Devices.at(device);
        switch (command)
        {
        case 1:
            entry.Native->Shutdown();
            m_Impl->Devices.erase(device);
            return 0;
        case 2:
        {
            if (a == 0 || a > std::numeric_limits<std::int32_t>::max() || b > 1)
                throw std::invalid_argument("Invalid managed compute buffer size or usage.");
            const auto id = m_Impl->Allocate();
            auto [slot, inserted] = entry.Buffers.emplace(id, ComputeBufferId{});
            (void)inserted;
            try
            {
                slot->second = entry.Native->CreateBuffer(static_cast<std::uint32_t>(a), b != 0);
            }
            catch (...)
            {
                entry.Buffers.erase(slot);
                throw;
            }
            return id;
        }
        case 3:
            if (!entry.Buffers.contains(a))
                return 0;
            entry.Native->DestroyBuffer(entry.Buffers.at(a));
            entry.Buffers.erase(a);
            return 0;
        case 4:
        {
            const auto& program = m_Impl->Programs.at(a);
            const auto id = m_Impl->Allocate();
            auto [slot, inserted] = entry.Pipelines.emplace(id, ComputePipelineId{});
            (void)inserted;
            try
            {
                slot->second = entry.Native->CreatePipeline(program, static_cast<std::size_t>(b));
            }
            catch (...)
            {
                entry.Pipelines.erase(slot);
                throw;
            }
            return id;
        }
        case 5:
            if (!entry.Pipelines.contains(a))
                return 0;
            entry.Native->DestroyPipeline(entry.Pipelines.at(a));
            entry.Pipelines.erase(a);
            return 0;
        case 6:
        case 7:
        {
            if (b > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Invalid managed compute byte offset.");
            const auto buffer = entry.Buffers.at(a);
            if (command == 6)
                entry.Native->Upload(buffer, bytes, static_cast<std::uint32_t>(b));
            else if (!bytes.empty())
            {
                const auto data = entry.Native->Readback(buffer, static_cast<std::uint32_t>(b),
                                                         static_cast<std::uint32_t>(bytes.size()));
                if (data.size() != bytes.size())
                    throw std::runtime_error("Compute readback returned an unexpected byte count.");
                std::ranges::copy(data, bytes.begin());
            }
            return 0;
        }
        case 8:
            return entry.Native->IsComplete(entry.Submissions.at(a)) ? 1 : 0;
        case 9:
            entry.Native->Wait(entry.Submissions.at(a));
            return 0;
        case 10:
            if (!entry.Submissions.contains(a))
                return 0;
            entry.Native->ReleaseSubmission(entry.Submissions.at(a));
            entry.Submissions.erase(a);
            return 0;
        case 11:
        {
            if (bytes.size() != sizeof(std::uint32_t))
                throw std::invalid_argument("Compute reload requires a variant index.");
            std::uint32_t variant = 0;
            std::memcpy(&variant, bytes.data(), sizeof(variant));
            entry.Native->ReloadPipeline(entry.Pipelines.at(a), m_Impl->Programs.at(b), variant);
            return 0;
        }
        case 12:
        {
            if (bytes.size() != sizeof(std::uint32_t) || b > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Compute readback requires a size and valid offset.");
            std::uint32_t size = 0;
            std::memcpy(&size, bytes.data(), sizeof(size));
            const auto id = m_Impl->Allocate();
            auto [slot, inserted] = entry.Submissions.emplace(id, ComputeSubmissionId{});
            try
            {
                slot->second = entry.Native->RequestReadback(entry.Buffers.at(a), static_cast<std::uint32_t>(b), size);
            }
            catch (...)
            {
                entry.Submissions.erase(slot);
                throw;
            }
            return id;
        }
        case 13:
        {
            const auto data = entry.Native->GetReadback(entry.Submissions.at(a));
            if (data.size() != bytes.size())
                throw std::invalid_argument("Compute readback destination has an incorrect size.");
            std::ranges::copy(data, bytes.begin());
            return 0;
        }
        default:
            throw std::invalid_argument("Unknown managed compute command.");
        }
    }

    std::uint64_t ManagedComputeStore::Dispatch(const std::uint64_t device, const std::uint64_t pipeline,
                                                const std::span<const ManagedComputeBinding> bindings,
                                                const std::uint32_t x, const std::uint32_t y, const std::uint32_t z,
                                                const std::uint64_t arguments, const std::uint32_t offset,
                                                const std::span<const std::byte> uniforms)
    {
        m_Impl->RequireOwner();
        if (bindings.size() > ProgramResourceHardLimit)
            throw std::invalid_argument("Too many managed compute bindings.");
        auto& entry = m_Impl->Devices.at(device);
        std::vector<ComputeBufferBinding> native;
        native.reserve(bindings.size());
        for (const auto& binding : bindings)
        {
            if (binding.Writable > 1)
                throw std::invalid_argument("Invalid managed compute access.");
            native.push_back({binding.Slot, entry.Buffers.at(binding.Buffer), binding.Writable != 0});
        }
        const auto nativePipeline = entry.Pipelines.at(pipeline);
        const auto nativeArguments = arguments == 0 ? ComputeBufferId{} : entry.Buffers.at(arguments);
        const auto id = m_Impl->Allocate();
        auto [slot, inserted] = entry.Submissions.emplace(id, ComputeSubmissionId{});
        (void)inserted;
        try
        {
            slot->second = arguments == 0 ? entry.Native->Dispatch(nativePipeline, native, {x, y, z}, uniforms)
                                          : entry.Native->DispatchIndirect(nativePipeline, native, nativeArguments,
                                                                           offset, uniforms);
        }
        catch (...)
        {
            entry.Submissions.erase(slot);
            throw;
        }
        return id;
    }

    ManagedRuntimeComputeScope::ManagedRuntimeComputeScope(ManagedComputeStore* store, const bool allowWork) noexcept
        : m_Previous(ActiveStore), m_PreviousAllowWork(AllowWork)
    {
        ActiveStore = store;
        AllowWork = allowWork;
    }
    ManagedRuntimeComputeScope::~ManagedRuntimeComputeScope()
    {
        ActiveStore = m_Previous;
        AllowWork = m_PreviousAllowWork;
    }

    void RegisterManagedRuntimeCompute(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeCompute", "CommandIcall", reinterpret_cast<void*>(&CommandBridge));
        assembly.AddInternalCall("Keire.NativeCompute", "DispatchIcall", reinterpret_cast<void*>(&DispatchBridge));
    }

    std::uint8_t InvokeManagedComputeCommand(const std::uint64_t device, const std::uint8_t command,
                                             const std::uint64_t a, const std::uint64_t b, std::byte* bytes,
                                             const std::uint32_t size, std::uint64_t* result) noexcept
    {
        return CommandBridge(device, command, a, b, bytes, size, result);
    }
} // namespace Keire::Detail
