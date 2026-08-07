#include "TestSodium.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace KeireHubTests
{
    namespace
    {
        constexpr std::string_view Base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        [[nodiscard]] std::string Base64(const std::span<const unsigned char> bytes)
        {
            std::string result;
            result.reserve((bytes.size() + 2U) / 3U * 4U);
            for (std::size_t offset = 0; offset < bytes.size(); offset += 3U)
            {
                const auto first = static_cast<unsigned int>(bytes[offset]);
                const auto second = offset + 1U < bytes.size() ? static_cast<unsigned int>(bytes[offset + 1U]) : 0U;
                const auto third = offset + 2U < bytes.size() ? static_cast<unsigned int>(bytes[offset + 2U]) : 0U;
                const auto packed = (first << 16U) | (second << 8U) | third;
                result.push_back(Base64Alphabet[(packed >> 18U) & 0x3fU]);
                result.push_back(Base64Alphabet[(packed >> 12U) & 0x3fU]);
                result.push_back(offset + 1U < bytes.size() ? Base64Alphabet[(packed >> 6U) & 0x3fU] : '=');
                result.push_back(offset + 2U < bytes.size() ? Base64Alphabet[packed & 0x3fU] : '=');
            }
            return result;
        }

        [[nodiscard]] std::filesystem::path FindLibrary()
        {
#if defined(_WIN32)
            char* configuredValue = nullptr;
            std::size_t configuredSize = 0;
            if (_dupenv_s(&configuredValue, &configuredSize, "KEIRE_TEST_SODIUM_LIBRARY") != 0)
                throw std::runtime_error("The libsodium test runtime override could not be read.");
            const std::unique_ptr<char, decltype(&std::free)> configured(configuredValue, &std::free);
            if (configured && configuredSize > 1U)
                return std::filesystem::weakly_canonical(configured.get());
#else
            if (const auto* configured = std::getenv("KEIRE_TEST_SODIUM_LIBRARY"); configured && *configured)
                return std::filesystem::weakly_canonical(configured);
#endif
            const auto dependencies = std::filesystem::current_path() / "Build" / "Dependencies";
            std::error_code error;
            if (std::filesystem::is_directory(dependencies, error))
            {
                for (std::filesystem::directory_iterator
                         iterator(dependencies, std::filesystem::directory_options::skip_permission_denied, error),
                     end;
                     !error && iterator != end; ++iterator)
                {
#if defined(_WIN32)
                    const std::array candidates{iterator->path() / "Debug/install/bin/libsodium.dll",
                                                iterator->path() / "Release/install/bin/libsodium.dll",
                                                iterator->path() / "libsodium/out/libsodium.dll"};
#elif defined(__APPLE__)
                    const std::array candidates{iterator->path() / "Debug/install/lib/libsodium.dylib",
                                                iterator->path() / "Release/install/lib/libsodium.dylib"};
#else
                    const std::array candidates{iterator->path() / "Debug/install/lib/libsodium.so",
                                                iterator->path() / "Release/install/lib/libsodium.so"};
#endif
                    for (const auto& candidate : candidates)
                    {
                        std::error_code candidateError;
                        if (std::filesystem::is_regular_file(candidate, candidateError))
                            return std::filesystem::weakly_canonical(candidate);
                    }
                }
                error.clear();
                std::size_t inspected = 0;
                for (std::filesystem::recursive_directory_iterator iterator(dependencies, error), end;
                     !error && iterator != end && inspected < 4096U; ++iterator, ++inspected)
                {
                    std::error_code entryError;
                    if (!iterator->is_regular_file(entryError))
                        continue;
#if defined(_WIN32)
                    if (iterator->path().filename() == "libsodium.dll")
#elif defined(__APPLE__)
                    if (iterator->path().filename() == "libsodium.dylib" ||
                        iterator->path().filename() == "libsodium.26.dylib")
#else
                    if (iterator->path().filename() == "libsodium.so" ||
                        iterator->path().filename() == "libsodium.so.26")
#endif
                    {
                        return std::filesystem::weakly_canonical(iterator->path());
                    }
                }
            }
            throw std::runtime_error("The pinned libsodium test runtime is unavailable.");
        }

#if defined(_WIN32)
        [[nodiscard]] void* OpenLibrary(const std::filesystem::path& path)
        {
            return LoadLibraryExW(path.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        void CloseLibrary(void* library) noexcept
        {
            if (library)
                FreeLibrary(static_cast<HMODULE>(library));
        }

        template <typename Function> [[nodiscard]] Function Symbol(void* library, const char* name)
        {
            return reinterpret_cast<Function>(GetProcAddress(static_cast<HMODULE>(library), name));
        }
#else
        [[nodiscard]] void* OpenLibrary(const std::filesystem::path& path)
        {
            return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        }

        void CloseLibrary(void* library) noexcept
        {
            if (library)
                dlclose(library);
        }

        template <typename Function> [[nodiscard]] Function Symbol(void* library, const char* name)
        {
            Function function = nullptr;
            const auto symbol = dlsym(library, name);
            static_assert(sizeof(function) == sizeof(symbol));
            std::memcpy(&function, &symbol, sizeof(function));
            return function;
        }
#endif

        [[nodiscard]] std::string Hex(const std::span<const unsigned char> bytes)
        {
            constexpr char values[] = "0123456789abcdef";
            std::string result(bytes.size() * 2U, '0');
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                result[index * 2U] = values[bytes[index] >> 4U];
                result[index * 2U + 1U] = values[bytes[index] & 0x0fU];
            }
            return result;
        }
    } // namespace

    TestSodiumSigner::TestSodiumSigner(const unsigned char seedOffset) : m_LibraryPath(FindLibrary())
    {
        auto* library = OpenLibrary(m_LibraryPath);
        if (!library)
            throw std::runtime_error("The pinned libsodium test runtime could not be loaded.");
        MemzeroFunction memzero = nullptr;
        try
        {
            const auto initialize = Symbol<int (*)()>(library, "sodium_init");
            const auto version = Symbol<const char* (*)()>(library, "sodium_version_string");
            const auto seedBytes = Symbol<std::size_t (*)()>(library, "crypto_sign_seedbytes");
            const auto publicKeyBytes = Symbol<std::size_t (*)()>(library, "crypto_sign_publickeybytes");
            const auto secretKeyBytes = Symbol<std::size_t (*)()>(library, "crypto_sign_secretkeybytes");
            const auto signatureBytes = Symbol<std::size_t (*)()>(library, "crypto_sign_bytes");
            const auto seedKeyPair = Symbol<SeedKeyPairFunction>(library, "crypto_sign_seed_keypair");
            const auto sign = Symbol<SignFunction>(library, "crypto_sign_detached");
            const auto hash = Symbol<HashFunction>(library, "crypto_hash_sha256");
            memzero = Symbol<MemzeroFunction>(library, "sodium_memzero");
            if (!initialize || !version || !seedBytes || !publicKeyBytes || !secretKeyBytes || !signatureBytes ||
                !seedKeyPair || !sign || !hash || !memzero || !version() || std::string_view(version()) != "1.0.22" ||
                seedBytes() != 32U || publicKeyBytes() != 32U || secretKeyBytes() != 64U || signatureBytes() != 64U ||
                initialize() < 0)
            {
                throw std::runtime_error("The pinned libsodium test runtime has an incompatible ABI.");
            }
            std::array<unsigned char, 32> seed{};
            for (std::size_t index = 0; index < seed.size(); ++index)
                seed[index] = static_cast<unsigned char>(index + seedOffset);
            if (seedKeyPair(m_PublicKey.data(), m_SecretKey.data(), seed.data()) != 0)
                throw std::runtime_error("The deterministic Ed25519 test key could not be generated.");
            memzero(seed.data(), seed.size());
            std::array<unsigned char, 32> digest{};
            if (hash(digest.data(), m_PublicKey.data(), m_PublicKey.size()) != 0)
                throw std::runtime_error("The Ed25519 test key fingerprint could not be generated.");
            const auto digestText = Hex(digest);
            m_KeyId = "ed25519-" + digestText.substr(0, 32);
            m_PublicKeyDocument = "{\"schemaVersion\":1,\"algorithm\":\"Ed25519\",\"keyId\":\"" + m_KeyId +
                                  "\",\"publicKey\":\"" + Base64(m_PublicKey) +
                                  "\",\"fingerprint\":\"sha256:" + digestText + "\"}";
            m_Library = library;
            m_Sign = sign;
            m_Hash = hash;
            m_Memzero = memzero;
        }
        catch (...)
        {
            if (memzero)
                memzero(m_SecretKey.data(), m_SecretKey.size());
            CloseLibrary(library);
            throw;
        }
    }

    TestSodiumSigner::~TestSodiumSigner()
    {
        if (m_Memzero)
            m_Memzero(m_SecretKey.data(), m_SecretKey.size());
        CloseLibrary(m_Library);
    }

    const std::filesystem::path& TestSodiumSigner::LibraryPath() const noexcept { return m_LibraryPath; }

    const std::string& TestSodiumSigner::KeyId() const noexcept { return m_KeyId; }

    const std::string& TestSodiumSigner::PublicKeyDocument() const noexcept { return m_PublicKeyDocument; }

    std::string TestSodiumSigner::SignBase64(const std::span<const std::byte> message) const
    {
        std::array<unsigned char, 64> signature{};
        unsigned long long size = 0;
        if (m_Sign(signature.data(), &size, reinterpret_cast<const unsigned char*>(message.data()),
                   static_cast<unsigned long long>(message.size()), m_SecretKey.data()) != 0 ||
            size != signature.size())
        {
            throw std::runtime_error("The exact-byte Ed25519 test signature could not be generated.");
        }
        return Base64(signature);
    }

    std::string TestSodiumSigner::Sha256Hex(const std::span<const std::byte> message) const
    {
        std::array<unsigned char, 32> digest{};
        if (m_Hash(digest.data(), reinterpret_cast<const unsigned char*>(message.data()),
                   static_cast<unsigned long long>(message.size())) != 0)
        {
            throw std::runtime_error("The test fixture digest could not be generated.");
        }
        return Hex(digest);
    }

    std::vector<std::byte> Bytes(const std::string_view value)
    {
        std::vector<std::byte> result(value.size());
        std::memcpy(result.data(), value.data(), value.size());
        return result;
    }

    std::string Text(const std::span<const std::byte> value)
    {
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }
} // namespace KeireHubTests
