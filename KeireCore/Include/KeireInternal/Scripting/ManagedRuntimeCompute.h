#pragma once

#include "Keire/Rendering/ProgramArtifact.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Coral
{
    class ManagedAssembly;
}

namespace Keire::Detail
{
    struct ManagedComputeBinding
    {
        std::uint64_t Buffer = 0;
        std::uint32_t Slot = 0;
        std::uint32_t Writable = 0;
    };

    /// Owned by one ScriptSystem. Resources expire at committed reload; programs survive until shutdown.
    class ManagedComputeStore final
    {
      public:
        ManagedComputeStore();
        ~ManagedComputeStore();
        ManagedComputeStore(const ManagedComputeStore&) = delete;
        ManagedComputeStore& operator=(const ManagedComputeStore&) = delete;
        [[nodiscard]] std::uint64_t RegisterProgram(const ProgramArtifact& artifact);
        void UnregisterProgram(std::uint64_t program);
        void Clear() noexcept;
        void ClearResources() noexcept;
        [[nodiscard]] std::uint64_t Command(std::uint64_t device, std::uint8_t command, std::uint64_t a,
                                            std::uint64_t b, std::span<std::byte> bytes);
        [[nodiscard]] std::uint64_t Dispatch(std::uint64_t device, std::uint64_t pipeline,
                                             std::span<const ManagedComputeBinding> bindings, std::uint32_t x,
                                             std::uint32_t y, std::uint32_t z, std::uint64_t arguments,
                                             std::uint32_t offset, std::span<const std::byte> uniforms);

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ManagedRuntimeComputeScope final
    {
      public:
        explicit ManagedRuntimeComputeScope(ManagedComputeStore* store, bool allowWork = true) noexcept;
        ~ManagedRuntimeComputeScope();
        ManagedRuntimeComputeScope(const ManagedRuntimeComputeScope&) = delete;
        ManagedRuntimeComputeScope& operator=(const ManagedRuntimeComputeScope&) = delete;

      private:
        ManagedComputeStore* m_Previous;
        bool m_PreviousAllowWork;
    };

    void RegisterManagedRuntimeCompute(Coral::ManagedAssembly& assembly);
    [[nodiscard]] std::uint8_t InvokeManagedComputeCommand(std::uint64_t device, std::uint8_t command, std::uint64_t a,
                                                           std::uint64_t b, std::byte* bytes, std::uint32_t size,
                                                           std::uint64_t* result) noexcept;
} // namespace Keire::Detail
