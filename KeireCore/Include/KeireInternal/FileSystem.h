#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace Keire::Detail
{
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
} // namespace Keire::Detail
