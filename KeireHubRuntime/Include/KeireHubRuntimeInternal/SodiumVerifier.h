#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace KeireHub::Detail
{
    class SodiumVerifier final
    {
      public:
        [[nodiscard]] static HubResult<std::shared_ptr<const SodiumVerifier>>
        Load(const std::filesystem::path& explicitPath);

        ~SodiumVerifier();

        SodiumVerifier(const SodiumVerifier&) = delete;
        SodiumVerifier& operator=(const SodiumVerifier&) = delete;

        [[nodiscard]] bool Verify(std::span<const std::byte> signature, std::span<const std::byte> message,
                                  std::span<const std::byte> publicKey) const noexcept;

        [[nodiscard]] bool Sha512(std::span<const std::byte> message, std::span<std::byte> digest) const noexcept;

      private:
        using InitializeFunction = int (*)();
        using VerifyFunction = int (*)(const unsigned char*, const unsigned char*, unsigned long long,
                                       const unsigned char*);
        using SizeFunction = std::size_t (*)();
        using VersionFunction = const char* (*)();
        using HashFunction = int (*)(unsigned char*, const unsigned char*, unsigned long long);

        SodiumVerifier(void* library, VerifyFunction verify, HashFunction hash) noexcept;

        void* m_Library = nullptr;
        VerifyFunction m_Verify = nullptr;
        HashFunction m_Hash = nullptr;
    };
} // namespace KeireHub::Detail
