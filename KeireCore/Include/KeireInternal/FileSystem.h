#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Keire::Detail
{
    using AnchoredFileChunkReader = std::function<void(std::span<std::byte>)>;
    using AnchoredFileChunkVisitor = std::function<void(std::span<const std::byte>)>;

    struct AnchoredFileSignature final
    {
        std::uint64_t Modified = 0;
        std::uintmax_t Size = 0;

        [[nodiscard]] bool operator==(const AnchoredFileSignature&) const noexcept = default;
    };

    struct AnchoredFileMetadata final
    {
        std::uintmax_t Size = 0;
        std::filesystem::perms Permissions = std::filesystem::perms::unknown;
    };

    enum class AnchoredRootPolicy : std::uint8_t
    {
        ResolveCanonical,
        RejectLink
    };

    // Retains an operating-system handle to a trusted directory and resolves every operation relative to that
    // handle. Relative components are opened without following links/reparse points, so a concurrent namespace swap
    // cannot redirect an operation outside the root.
    class AnchoredFileSystem final
    {
      public:
        explicit AnchoredFileSystem(const std::filesystem::path& root,
                                    AnchoredRootPolicy rootPolicy = AnchoredRootPolicy::ResolveCanonical);
        ~AnchoredFileSystem();

        AnchoredFileSystem(const AnchoredFileSystem&) = delete;
        AnchoredFileSystem& operator=(const AnchoredFileSystem&) = delete;
        AnchoredFileSystem(AnchoredFileSystem&&) noexcept;
        AnchoredFileSystem& operator=(AnchoredFileSystem&&) noexcept;

        [[nodiscard]] const std::filesystem::path& Root() const noexcept;
        [[nodiscard]] std::vector<std::byte> Read(const std::filesystem::path& relative,
                                                  std::size_t maximumBytes) const;
        [[nodiscard]] std::vector<std::byte> ReadTail(const std::filesystem::path& relative,
                                                      std::size_t maximumBytes) const;
        [[nodiscard]] AnchoredFileMetadata ReadChunks(const std::filesystem::path& relative,
                                                      std::uintmax_t maximumBytes,
                                                      const AnchoredFileChunkVisitor& visitor) const;
        [[nodiscard]] AnchoredFileSignature Signature(const std::filesystem::path& relative) const;
        [[nodiscard]] bool Exists(const std::filesystem::path& relative) const;
        [[nodiscard]] bool IsRegularFile(const std::filesystem::path& relative) const;
        void CreateDirectories(const std::filesystem::path& relative) const;
        void WriteFileAtomically(const std::filesystem::path& relative, std::span<const std::byte> contents,
                                 bool replaceExisting = true) const;
        void WriteFileAtomically(const std::filesystem::path& relative, std::uint64_t size,
                                 const AnchoredFileChunkReader& reader,
                                 std::filesystem::perms permissions = std::filesystem::perms::owner_read |
                                                                      std::filesystem::perms::owner_write |
                                                                      std::filesystem::perms::group_read |
                                                                      std::filesystem::perms::others_read,
                                 bool replaceExisting = true) const;
        void Remove(const std::filesystem::path& relative) const;
        void Rename(const std::filesystem::path& source, const std::filesystem::path& destination,
                    bool replaceExisting = false) const;
        void RenameTo(const std::filesystem::path& source, const AnchoredFileSystem& destinationFileSystem,
                      const std::filesystem::path& destination, bool replaceExisting = false) const;
        void Copy(const std::filesystem::path& source, const std::filesystem::path& destination,
                  bool replaceExisting = false) const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    using AnchoredFileSystemOperationHook = std::function<void(std::string_view, const std::filesystem::path&)>;
    void SetAnchoredFileSystemOperationHookForTesting(AnchoredFileSystemOperationHook hook);

    class InterprocessMutex final
    {
      public:
        explicit InterprocessMutex(const std::filesystem::path& path);
        ~InterprocessMutex();

        InterprocessMutex(const InterprocessMutex&) = delete;
        InterprocessMutex& operator=(const InterprocessMutex&) = delete;

        void lock();
        void unlock() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view value);
    [[nodiscard]] std::filesystem::path PathWithSuffix(const std::filesystem::path& path, std::string_view suffix);
    [[nodiscard]] bool IsTransientFile(const std::filesystem::path& path);
    [[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path, std::size_t maximumBytes);
    void PublishFileAtomically(const std::filesystem::path& temporary, const std::filesystem::path& destination);
    void WriteFileAtomically(const std::filesystem::path& path, std::span<const std::byte> contents);
    void WriteTextFileAtomically(const std::filesystem::path& path, std::string_view contents);
    [[nodiscard]] bool WriteFileAtomicallyIfChanged(const std::filesystem::path& path,
                                                    std::span<const std::byte> contents);
    [[nodiscard]] bool WriteTextFileAtomicallyIfChanged(const std::filesystem::path& path, std::string_view contents);
    using RenamePathOperation =
        std::function<void(const std::filesystem::path&, const std::filesystem::path&, std::error_code&)>;
    using RenamePathDelay = std::function<void(std::size_t attempt, std::chrono::milliseconds delay)>;
    [[nodiscard]] bool TryRenamePathWithRetry(const std::filesystem::path& source,
                                              const std::filesystem::path& destination, std::error_code& error,
                                              const RenamePathOperation& operation = {},
                                              const RenamePathDelay& delay = {});
    void RenamePathWithRetry(const std::filesystem::path& source, const std::filesystem::path& destination,
                             const RenamePathOperation& operation = {}, const RenamePathDelay& delay = {});
    [[nodiscard]] std::filesystem::path CanonicalExistingPath(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path ResolveConfinedPath(const std::filesystem::path& root,
                                                            const std::filesystem::path& relative);
} // namespace Keire::Detail
