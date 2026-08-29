#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Coral
{
    class AssemblyLoadContext;
    class ManagedAssembly;
    enum class AssemblyLoadStatus;
} // namespace Coral

namespace Keire::Detail
{
    struct ManagedAssemblySnapshot final
    {
        std::filesystem::path Source;
        std::vector<std::byte> Bytes;
        std::string Sha256;
        std::uintmax_t Size = 0;
        std::int64_t LastWriteTimeTicks = 0;
    };

    [[nodiscard]] ManagedAssemblySnapshot CaptureManagedAssemblySnapshot(const std::filesystem::path& source);
    [[nodiscard]] Coral::ManagedAssembly& LoadManagedAssemblySnapshot(Coral::AssemblyLoadContext& context,
                                                                      const ManagedAssemblySnapshot& snapshot);
    [[nodiscard]] std::string ManagedAssemblyLoadFailure(std::string_view assemblyName,
                                                         const ManagedAssemblySnapshot& snapshot,
                                                         Coral::AssemblyLoadStatus status,
                                                         std::string_view managedException);

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    using ManagedAssemblySnapshotHook = void (*)(const ManagedAssemblySnapshot&) noexcept;
    using ManagedAssemblySnapshotReadHook = void (*)(const std::filesystem::path&, std::size_t) noexcept;
    void SetManagedAssemblySnapshotHookForTesting(ManagedAssemblySnapshotHook hook) noexcept;
    void SetManagedAssemblySnapshotReadHookForTesting(ManagedAssemblySnapshotReadHook hook) noexcept;
#endif
} // namespace Keire::Detail
