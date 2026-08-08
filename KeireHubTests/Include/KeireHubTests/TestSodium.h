#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHubTests
{
    class TestSodiumSigner final
    {
      public:
        explicit TestSodiumSigner(unsigned char seedOffset = 0);
        ~TestSodiumSigner();

        TestSodiumSigner(const TestSodiumSigner&) = delete;
        TestSodiumSigner& operator=(const TestSodiumSigner&) = delete;

        [[nodiscard]] const std::filesystem::path& LibraryPath() const noexcept;
        [[nodiscard]] const std::string& KeyId() const noexcept;
        [[nodiscard]] const std::string& PublicKeyDocument() const noexcept;
        [[nodiscard]] std::string SignBase64(std::span<const std::byte> message) const;
        [[nodiscard]] std::string Sha256Hex(std::span<const std::byte> message) const;

      private:
        using SeedKeyPairFunction = int (*)(unsigned char*, unsigned char*, const unsigned char*);
        using SignFunction = int (*)(unsigned char*, unsigned long long*, const unsigned char*, unsigned long long,
                                     const unsigned char*);
        using HashFunction = int (*)(unsigned char*, const unsigned char*, unsigned long long);
        using MemzeroFunction = void (*)(void*, std::size_t);

        void* m_Library = nullptr;
        std::filesystem::path m_LibraryPath;
        SignFunction m_Sign = nullptr;
        HashFunction m_Hash = nullptr;
        MemzeroFunction m_Memzero = nullptr;
        std::array<unsigned char, 32> m_PublicKey{};
        std::array<unsigned char, 64> m_SecretKey{};
        std::string m_KeyId;
        std::string m_PublicKeyDocument;
    };

    [[nodiscard]] std::vector<std::byte> Bytes(std::string_view value);
    [[nodiscard]] std::string Text(std::span<const std::byte> value);
} // namespace KeireHubTests
