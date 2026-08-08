#include <KeireHubRuntimeInternal/SodiumVerifier.h>

#include <KeireHubRuntimeInternal/Persistence.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t Ed25519PublicKeyBytes = 32;
        constexpr std::size_t Ed25519SignatureBytes = 64;
        constexpr std::string_view PinnedSodiumVersion = "1.0.22";

        [[nodiscard]] HubError LoadError(std::string details)
        {
            return {.Code = HubErrorCode::DistributionConfigurationInvalid,
                    .Message = "The distribution signature verifier is unavailable.",
                    .AffectedItem = "libsodium",
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubResult<std::filesystem::path> ExecutableDirectory()
        {
#if defined(_WIN32)
            std::vector<wchar_t> buffer(1024);
            while (buffer.size() <= 32768U)
            {
                const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0U)
                    return HubResult<std::filesystem::path>::Failure(LoadError("GetModuleFileNameW failed."));
                if (length + 1U < buffer.size())
                    return HubResult<std::filesystem::path>::Success(
                        std::filesystem::path(std::wstring_view(buffer.data(), length)).parent_path());
                buffer.resize(buffer.size() * 2U);
            }
            return HubResult<std::filesystem::path>::Failure(
                LoadError("The executable path exceeds supported limits."));
#elif defined(__APPLE__)
            std::uint32_t required = 0;
            _NSGetExecutablePath(nullptr, &required);
            if (required == 0U || required > std::size_t{1024U} * 1024U)
                return HubResult<std::filesystem::path>::Failure(LoadError("The executable path is unavailable."));
            std::vector<char> buffer(required);
            if (_NSGetExecutablePath(buffer.data(), &required) != 0)
                return HubResult<std::filesystem::path>::Failure(LoadError("The executable path could not be read."));
            std::error_code error;
            const auto path = std::filesystem::weakly_canonical(buffer.data(), error);
            if (error)
                return HubResult<std::filesystem::path>::Failure(LoadError(error.message()));
            return HubResult<std::filesystem::path>::Success(path.parent_path());
#else
            std::vector<char> buffer(1024);
            while (buffer.size() <= std::size_t{1024U} * 1024U)
            {
                const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
                if (length < 0)
                    return HubResult<std::filesystem::path>::Failure(LoadError("/proc/self/exe could not be read."));
                if (static_cast<std::size_t>(length) < buffer.size())
                    return HubResult<std::filesystem::path>::Success(
                        std::filesystem::path(std::string_view(buffer.data(), static_cast<std::size_t>(length)))
                            .parent_path());
                buffer.resize(buffer.size() * 2U);
            }
            return HubResult<std::filesystem::path>::Failure(
                LoadError("The executable path exceeds supported limits."));
#endif
        }

        [[nodiscard]] HubResult<std::filesystem::path> ResolveLibraryPath(const std::filesystem::path& explicitPath)
        {
            std::vector<std::filesystem::path> candidates;
            if (!explicitPath.empty())
            {
                candidates.push_back(explicitPath);
            }
            else
            {
                auto executable = ExecutableDirectory();
                if (!executable)
                    return HubResult<std::filesystem::path>::Failure(executable.Error());
#if defined(_WIN32)
                candidates.push_back(executable.Value() / "libsodium.dll");
#elif defined(__APPLE__)
                candidates.push_back(executable.Value() / "libsodium.dylib");
                candidates.push_back(executable.Value() / "libsodium.26.dylib");
#else
                candidates.push_back(executable.Value() / "libsodium.so");
                candidates.push_back(executable.Value() / "libsodium.so.26");
#endif
            }
            for (const auto& candidate : candidates)
            {
                std::error_code error;
                const auto canonical = std::filesystem::weakly_canonical(candidate, error);
                if (error)
                    continue;
                const auto status = std::filesystem::status(canonical, error);
                if (!error && status.type() == std::filesystem::file_type::regular)
                    return HubResult<std::filesystem::path>::Success(canonical);
            }
            return HubResult<std::filesystem::path>::Failure(
                LoadError("No regular libsodium runtime was found at the configured location."));
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

        template <typename Function> [[nodiscard]] Function LoadSymbol(void* library, const char* name) noexcept
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

        template <typename Function> [[nodiscard]] Function LoadSymbol(void* library, const char* name) noexcept
        {
            Function function = nullptr;
            static_assert(sizeof(function) == sizeof(void*));
            const auto symbol = dlsym(library, name);
            std::memcpy(&function, &symbol, sizeof(function));
            return function;
        }
#endif
    } // namespace

    HubResult<std::shared_ptr<const SodiumVerifier>> SodiumVerifier::Load(const std::filesystem::path& explicitPath)
    {
        auto path = ResolveLibraryPath(explicitPath);
        if (!path)
            return HubResult<std::shared_ptr<const SodiumVerifier>>::Failure(path.Error());
        auto* library = OpenLibrary(path.Value());
        if (!library)
            return HubResult<std::shared_ptr<const SodiumVerifier>>::Failure(
                LoadError("The pinned libsodium runtime could not be loaded: " + PathToUtf8(path.Value())));
        const auto initialize = LoadSymbol<InitializeFunction>(library, "sodium_init");
        const auto verify = LoadSymbol<VerifyFunction>(library, "crypto_sign_verify_detached");
        const auto signatureBytes = LoadSymbol<SizeFunction>(library, "crypto_sign_bytes");
        const auto publicKeyBytes = LoadSymbol<SizeFunction>(library, "crypto_sign_publickeybytes");
        const auto version = LoadSymbol<VersionFunction>(library, "sodium_version_string");
        if (!initialize || !verify || !signatureBytes || !publicKeyBytes || !version ||
            signatureBytes() != Ed25519SignatureBytes || publicKeyBytes() != Ed25519PublicKeyBytes || !version() ||
            std::string_view(version()) != PinnedSodiumVersion || initialize() < 0)
        {
            CloseLibrary(library);
            return HubResult<std::shared_ptr<const SodiumVerifier>>::Failure(
                LoadError("The loaded libsodium runtime does not provide the required Ed25519 ABI."));
        }
        return HubResult<std::shared_ptr<const SodiumVerifier>>::Success(
            std::shared_ptr<const SodiumVerifier>(new SodiumVerifier(library, verify)));
    }

    SodiumVerifier::~SodiumVerifier() { CloseLibrary(m_Library); }

    bool SodiumVerifier::Verify(const std::span<const std::byte> signature, const std::span<const std::byte> message,
                                const std::span<const std::byte> publicKey) const noexcept
    {
        if (signature.size() != Ed25519SignatureBytes || publicKey.size() != Ed25519PublicKeyBytes || !m_Verify)
            return false;
        return m_Verify(reinterpret_cast<const unsigned char*>(signature.data()),
                        reinterpret_cast<const unsigned char*>(message.data()),
                        static_cast<unsigned long long>(message.size()),
                        reinterpret_cast<const unsigned char*>(publicKey.data())) == 0;
    }

    SodiumVerifier::SodiumVerifier(void* library, const VerifyFunction verify) noexcept
        : m_Library(library), m_Verify(verify)
    {
    }
} // namespace KeireHub::Detail
