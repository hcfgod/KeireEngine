#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace KeireHub::Detail
{
    // Pins an ordinary directory and resolves all descendants relative to its operating-system handle. Windows
    // mutations therefore remain confined to the object that was validated even if its pathname is concurrently
    // renamed or replaced.
    class InstallMutationFileSystem final
    {
      public:
        explicit InstallMutationFileSystem(const std::filesystem::path& root, bool createIfMissing = false,
                                           bool requireNew = false);
        ~InstallMutationFileSystem();

        InstallMutationFileSystem(const InstallMutationFileSystem&) = delete;
        InstallMutationFileSystem& operator=(const InstallMutationFileSystem&) = delete;
        InstallMutationFileSystem(InstallMutationFileSystem&&) noexcept;
        InstallMutationFileSystem& operator=(InstallMutationFileSystem&&) noexcept;

        [[nodiscard]] const std::filesystem::path& Root() const noexcept;
        [[nodiscard]] HubResult<InstallOwnedFile> Describe(const std::filesystem::path& relative,
                                                           bool allowMissing = false) const;
        [[nodiscard]] HubResult<std::string> ReadText(const std::filesystem::path& relative,
                                                      std::size_t maximumBytes) const;
        [[nodiscard]] HubStatus WriteTextAtomically(const std::filesystem::path& relative, std::string_view text,
                                                    bool replaceExisting) const;
        [[nodiscard]] HubStatus CreateDirectories(const std::filesystem::path& relative) const;
        [[nodiscard]] HubStatus CopyVerifiedTo(const InstallOwnedFile& file,
                                               const InstallMutationFileSystem& destination) const;
        [[nodiscard]] HubStatus RenameVerifiedTo(const InstallOwnedFile& file,
                                                 const InstallMutationFileSystem& destination,
                                                 bool allowMissing = false,
                                                 const std::filesystem::path& destinationPath = {}) const;
        [[nodiscard]] HubStatus RemoveVerified(const InstallOwnedFile& file) const;
        [[nodiscard]] HubStatus RemoveEmptyDirectory(const std::filesystem::path& relative) const;
        [[nodiscard]] HubStatus RemoveRootIfEmpty() const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class InstallMutationAuthority final
    {
      public:
        [[nodiscard]] HubResult<std::shared_ptr<InstallMutationFileSystem>>
        Pin(const std::filesystem::path& root, bool createIfMissing = false, bool requireNew = false);
        void Unpin(const std::filesystem::path& root) noexcept;

      private:
        std::map<std::string, std::shared_ptr<InstallMutationFileSystem>, std::less<>> m_Roots;
    };

#if defined(KEIRE_INSTALL_TRANSACTION_TESTING)
    using InstallMutationHook = void (*)(std::string_view operation, const std::filesystem::path& relative);
    void SetInstallMutationHookForTesting(InstallMutationHook hook) noexcept;
#if defined(_WIN32)
    void SetInstallMutationTransientRenameFailuresForTesting(std::size_t failureCount) noexcept;
    void SetInstallMutationTransientDeleteFailuresForTesting(std::size_t failureCount) noexcept;
    void SetInstallMutationTransientDirectoryNotEmptyFailuresForTesting(std::size_t failureCount) noexcept;
#endif
#endif
} // namespace KeireHub::Detail
