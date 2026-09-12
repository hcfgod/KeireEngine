#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"
#include "Keire/Rendering/ProgramArtifact.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Keire
{
    class ComputeDevice;

    /// An opaque device-scoped identity. Copies do not extend native resource lifetime.
    struct ComputeBufferId
    {
        std::uint64_t Value = 0;
        bool operator==(const ComputeBufferId&) const = default;

      private:
        friend class ComputeDevice;
        std::shared_ptr<const void> m_Owner;
    };

    struct ComputePipelineId
    {
        std::uint64_t Value = 0;
        bool operator==(const ComputePipelineId&) const = default;

      private:
        friend class ComputeDevice;
        std::shared_ptr<const void> m_Owner;
    };

    struct ComputeSubmissionId
    {
        std::uint64_t Value = 0;
        bool operator==(const ComputeSubmissionId&) const = default;

      private:
        friend class ComputeDevice;
        std::shared_ptr<const void> m_Owner;
    };

    struct ComputeBufferBinding
    {
        std::uint32_t Slot = 0;
        ComputeBufferId Buffer;
        bool Writable = false;
    };

    struct ComputeDispatchSize
    {
        std::uint32_t X = 1;
        std::uint32_t Y = 1;
        std::uint32_t Z = 1;
    };

    /// Independent GPU compute queue. All calls and destruction belong to the construction thread.
    /// Buffers are device-owned; Shutdown invalidates every identity and releases pending work safely.
    /// There is no sharing with RenderSystem resources. Backend initialization is lazy.
    /// Native failures make the device inert; call Shutdown and create a fresh device to recover.
    class KEIRE_API ComputeDevice final : public RefCounted
    {
      public:
        explicit ComputeDevice(ProgramBackend backend, bool debug = false);
        ~ComputeDevice() override;
        ComputeDevice(const ComputeDevice&) = delete;
        ComputeDevice& operator=(const ComputeDevice&) = delete;

        [[nodiscard]] ComputeBufferId CreateBuffer(std::uint32_t size, bool indirect = false);
        void DestroyBuffer(ComputeBufferId buffer);
        [[nodiscard]] ComputePipelineId CreatePipeline(const ProgramArtifact& artifact, std::size_t variant = 0);
        void DestroyPipeline(ComputePipelineId pipeline);
        void Upload(ComputeBufferId buffer, std::span<const std::byte> bytes, std::uint32_t offset = 0);
        [[nodiscard]] ComputeSubmissionId Dispatch(ComputePipelineId pipeline,
                                                   std::span<const ComputeBufferBinding> bindings,
                                                   ComputeDispatchSize groups,
                                                   std::span<const std::byte> uniforms = {});
        [[nodiscard]] ComputeSubmissionId DispatchIndirect(ComputePipelineId pipeline,
                                                           std::span<const ComputeBufferBinding> bindings,
                                                           ComputeBufferId arguments, std::uint32_t offset = 0,
                                                           std::span<const std::byte> uniforms = {});
        [[nodiscard]] bool IsComplete(ComputeSubmissionId submission);
        void Wait(ComputeSubmissionId submission);
        void ReleaseSubmission(ComputeSubmissionId submission);
        [[nodiscard]] std::vector<std::byte> Readback(ComputeBufferId buffer, std::uint32_t offset = 0,
                                                      std::uint32_t size = 0);
        void WaitIdle();
        void Shutdown();
        [[nodiscard]] bool IsOpen() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
        [[nodiscard]] ComputeSubmissionId Submit(ComputePipelineId pipeline,
                                                 std::span<const ComputeBufferBinding> bindings,
                                                 ComputeDispatchSize groups, ComputeBufferId arguments,
                                                 std::uint32_t offset, std::span<const std::byte> uniforms);
    };
} // namespace Keire
