#include "KeireInternal/Scripting/ManagedAssemblySnapshot.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Scripting/ManagedReflection.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumManagedAssemblyBytes = std::size_t{64} << 20U;
        constexpr std::size_t SnapshotAttempts = 3;

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        std::atomic<ManagedAssemblySnapshotHook> SnapshotHook;
        std::atomic<ManagedAssemblySnapshotReadHook> SnapshotReadHook;
#endif

        [[nodiscard]] std::string_view AssemblyLoadStatusName(const Coral::AssemblyLoadStatus status) noexcept
        {
            switch (status)
            {
            case Coral::AssemblyLoadStatus::Success:
                return "Success";
            case Coral::AssemblyLoadStatus::FileNotFound:
                return "FileNotFound";
            case Coral::AssemblyLoadStatus::FileLoadFailure:
                return "FileLoadFailure";
            case Coral::AssemblyLoadStatus::InvalidFilePath:
                return "InvalidFilePath";
            case Coral::AssemblyLoadStatus::InvalidAssembly:
                return "InvalidAssembly";
            case Coral::AssemblyLoadStatus::UnknownError:
                return "UnknownError";
            }
            return "Unrecognized";
        }
    } // namespace

    ManagedAssemblySnapshot CaptureManagedAssemblySnapshot(const std::filesystem::path& source)
    {
        const auto resolved = std::filesystem::absolute(source).lexically_normal();
        std::string lastFailure;
        for (std::size_t attempt = 0; attempt < SnapshotAttempts; ++attempt)
        {
            try
            {
                std::error_code error;
                const auto sizeBefore = std::filesystem::file_size(resolved, error);
                if (error)
                    throw std::runtime_error("cannot inspect size: " + error.message());
                if (sizeBefore == 0)
                    throw std::runtime_error("assembly is empty");
                if (sizeBefore > MaximumManagedAssemblyBytes)
                    throw std::runtime_error("assembly exceeds the 64 MiB safety limit");
                const auto writeTimeBefore = std::filesystem::last_write_time(resolved, error);
                if (error)
                    throw std::runtime_error("cannot inspect modification time: " + error.message());

                const auto contents = ReadTextFile(resolved, MaximumManagedAssemblyBytes);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (const auto hook = SnapshotReadHook.load(std::memory_order_acquire))
                    hook(resolved, attempt);
#endif
                const auto sizeAfter = std::filesystem::file_size(resolved, error);
                if (error)
                    throw std::runtime_error("cannot recheck size: " + error.message());
                const auto writeTimeAfter = std::filesystem::last_write_time(resolved, error);
                if (error)
                    throw std::runtime_error("cannot recheck modification time: " + error.message());
                if (sizeBefore != sizeAfter || sizeAfter != contents.size() || writeTimeBefore != writeTimeAfter)
                    throw std::runtime_error("assembly changed while its immutable snapshot was captured");

                ManagedAssemblySnapshot snapshot;
                snapshot.Source = resolved;
                snapshot.Size = sizeAfter;
                snapshot.LastWriteTimeTicks = static_cast<std::int64_t>(writeTimeAfter.time_since_epoch().count());
                snapshot.Bytes.resize(contents.size());
                std::memcpy(snapshot.Bytes.data(), contents.data(), contents.size());
                snapshot.Sha256 = DigestToString(Sha256(snapshot.Bytes));
                return snapshot;
            }
            catch (const std::exception& error)
            {
                lastFailure = error.what();
                if (attempt + 1U < SnapshotAttempts)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        throw std::runtime_error("Managed reload could not capture a stable assembly snapshot for '" +
                                 PathText(resolved) + "': " + lastFailure +
                                 ". The last-good generation remains active.");
    }

    Coral::ManagedAssembly& LoadManagedAssemblySnapshot(Coral::AssemblyLoadContext& context,
                                                        const ManagedAssemblySnapshot& snapshot)
    {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (const auto hook = SnapshotHook.load(std::memory_order_acquire))
            hook(snapshot);
#endif
        if (snapshot.Bytes.empty() ||
            snapshot.Bytes.size() > static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)()))
            throw std::invalid_argument("Managed assembly snapshot is invalid.");
        return context.LoadAssemblyFromMemory(snapshot.Bytes.data(), static_cast<std::int64_t>(snapshot.Bytes.size()));
    }

    std::string ManagedAssemblyLoadFailure(const std::string_view assemblyName, const ManagedAssemblySnapshot& snapshot,
                                           const Coral::AssemblyLoadStatus status,
                                           const std::string_view managedException)
    {
        auto diagnostic = "Managed reload rejected " + std::string(assemblyName) + ": Coral status " +
                          std::string(AssemblyLoadStatusName(status)) + " (" +
                          std::to_string(static_cast<int>(status)) + "); snapshot path='" + PathText(snapshot.Source) +
                          "', size=" + std::to_string(snapshot.Size) + " bytes, sha256=" + snapshot.Sha256 +
                          ", lastWriteTimeTicks=" + std::to_string(snapshot.LastWriteTimeTicks) + ".";
        if (!managedException.empty())
            diagnostic += " Managed exception: " + std::string(managedException);
        diagnostic += " The last-good generation remains active.";
        return diagnostic;
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void SetManagedAssemblySnapshotHookForTesting(const ManagedAssemblySnapshotHook hook) noexcept
    {
        SnapshotHook.store(hook, std::memory_order_release);
    }

    void SetManagedAssemblySnapshotReadHookForTesting(const ManagedAssemblySnapshotReadHook hook) noexcept
    {
        SnapshotReadHook.store(hook, std::memory_order_release);
    }
#endif
} // namespace Keire::Detail
