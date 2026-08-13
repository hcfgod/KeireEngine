#include "KeireHubRuntime/AccountSessionStore.h"

#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <dpapi.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::array<std::byte, 4> ProtectedFileHeader{std::byte{'K'}, std::byte{'H'}, std::byte{'S'},
                                                               std::byte{'1'}};
        constexpr std::array<char, 4> SessionPayloadHeader{'K', 'H', 'S', '2'};
        constexpr std::size_t MaximumRefreshTokenBytes = 4096U;
        constexpr std::size_t MaximumSessionPayloadBytes = MaximumRefreshTokenBytes + SessionPayloadHeader.size() + 1U;
        constexpr std::size_t MaximumStoredBytes = std::size_t{64U} * 1024U;

        [[nodiscard]] HubError StorageError(const HubErrorCode code, const std::filesystem::path& path,
                                            const std::string_view message, const std::string_view details = {})
        {
            return {.Code = code,
                    .Message = std::string(message),
                    .Retryable = true,
                    .AffectedItem = Detail::PathToUtf8(path.filename()),
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] HubStatus ValidatePath(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (status.type() == std::filesystem::file_type::not_found ||
                error == std::make_error_code(std::errc::no_such_file_or_directory))
            {
                return HubStatus::Success();
            }
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
            {
                return HubStatus::Failure(
                    StorageError(HubErrorCode::IoRead, path, "The saved account session is unsafe.", error.message()));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] std::string EncodeSession(const AccountSessionKind kind, const std::string_view refreshToken)
        {
            std::string encoded;
            encoded.reserve(SessionPayloadHeader.size() + 1U + refreshToken.size());
            encoded.append(SessionPayloadHeader.data(), SessionPayloadHeader.size());
            encoded.push_back(kind == AccountSessionKind::DesktopOAuth ? '\x02' : '\x01');
            encoded.append(refreshToken);
            return encoded;
        }

        [[nodiscard]] HubResult<StoredAccountSession> DecodeSession(std::string encoded,
                                                                    const std::filesystem::path& path)
        {
            if (encoded.size() >= SessionPayloadHeader.size() &&
                std::memcmp(encoded.data(), SessionPayloadHeader.data(), SessionPayloadHeader.size()) == 0)
            {
                if (encoded.size() <= SessionPayloadHeader.size() + 1U || encoded.size() > MaximumSessionPayloadBytes)
                {
                    return HubResult<StoredAccountSession>::Failure(
                        StorageError(HubErrorCode::InvalidData, path, "The saved account session is invalid.",
                                     "Invalid versioned session length."));
                }
                const auto kindByte = static_cast<unsigned char>(encoded[SessionPayloadHeader.size()]);
                std::optional<AccountSessionKind> kind;
                if (kindByte == 1U)
                    kind = AccountSessionKind::SupabaseAuth;
                else if (kindByte == 2U)
                    kind = AccountSessionKind::DesktopOAuth;
                else
                {
                    return HubResult<StoredAccountSession>::Failure(
                        StorageError(HubErrorCode::InvalidData, path, "The saved account session is invalid.",
                                     "Unknown account-session kind."));
                }
                auto refreshToken = encoded.substr(SessionPayloadHeader.size() + 1U);
                std::fill(encoded.begin(), encoded.end(), '\0');
                return HubResult<StoredAccountSession>::Success(
                    {.RefreshToken = std::move(refreshToken), .Kind = kind});
            }
            if (encoded.empty() || encoded.size() > MaximumRefreshTokenBytes)
            {
                return HubResult<StoredAccountSession>::Failure(StorageError(
                    HubErrorCode::InvalidData, path, "The saved account session is invalid.", "Invalid token length."));
            }
            return HubResult<StoredAccountSession>::Success({.RefreshToken = std::move(encoded), .Kind = std::nullopt});
        }

        [[nodiscard]] std::string SecretAccount(const std::filesystem::path& path)
        {
            const auto identity = Detail::PathToUtf8(std::filesystem::absolute(path).lexically_normal());
            Detail::Sha256Builder builder;
            builder.Update(std::as_bytes(std::span(identity.data(), identity.size())));
            return Detail::DigestToString(builder.Finish());
        }

#if defined(__linux__)
        struct SecretToolResult final
        {
            int ExitCode = -1;
            std::string Output;
        };

        [[nodiscard]] std::optional<std::filesystem::path> FindSecretTool() noexcept
        {
            const auto* pathValue = std::getenv("PATH");
            if (!pathValue)
                return std::nullopt;
            std::string_view paths(pathValue);
            while (!paths.empty())
            {
                const auto separator = paths.find(':');
                const auto candidate = std::filesystem::path(std::string(paths.substr(0U, separator))) / "secret-tool";
                if (!candidate.empty() && ::access(candidate.c_str(), X_OK) == 0)
                    return candidate;
                if (separator == paths.npos)
                    break;
                paths.remove_prefix(separator + 1U);
            }
            return std::nullopt;
        }

        void CloseDescriptor(const int descriptor) noexcept
        {
            if (descriptor >= 0)
                while (::close(descriptor) == -1 && errno == EINTR)
                {
                }
        }

        [[nodiscard]] HubResult<SecretToolResult> RunSecretTool(const std::filesystem::path& executable,
                                                                const std::vector<std::string>& arguments,
                                                                const std::optional<std::string_view> input,
                                                                const std::filesystem::path& affectedPath)
        {
            int inputPipe[2]{-1, -1};
            int outputPipe[2]{-1, -1};
            if ((input && ::pipe(inputPipe) != 0) || ::pipe(outputPipe) != 0)
            {
                CloseDescriptor(inputPipe[0]);
                CloseDescriptor(inputPipe[1]);
                CloseDescriptor(outputPipe[0]);
                CloseDescriptor(outputPipe[1]);
                return HubResult<SecretToolResult>::Failure(
                    StorageError(HubErrorCode::IoWrite, affectedPath, "The Secret Service helper could not be started.",
                                 std::strerror(errno)));
            }
            const auto process = ::fork();
            if (process == -1)
            {
                CloseDescriptor(inputPipe[0]);
                CloseDescriptor(inputPipe[1]);
                CloseDescriptor(outputPipe[0]);
                CloseDescriptor(outputPipe[1]);
                return HubResult<SecretToolResult>::Failure(
                    StorageError(HubErrorCode::IoWrite, affectedPath, "The Secret Service helper could not be started.",
                                 std::strerror(errno)));
            }
            if (process == 0)
            {
                if (input)
                    static_cast<void>(::dup2(inputPipe[0], STDIN_FILENO));
                else
                {
                    const auto nullInput = ::open("/dev/null", O_RDONLY);
                    if (nullInput >= 0)
                    {
                        static_cast<void>(::dup2(nullInput, STDIN_FILENO));
                        CloseDescriptor(nullInput);
                    }
                }
                static_cast<void>(::dup2(outputPipe[1], STDOUT_FILENO));
                const auto nullError = ::open("/dev/null", O_WRONLY);
                if (nullError >= 0)
                {
                    static_cast<void>(::dup2(nullError, STDERR_FILENO));
                    CloseDescriptor(nullError);
                }
                CloseDescriptor(inputPipe[0]);
                CloseDescriptor(inputPipe[1]);
                CloseDescriptor(outputPipe[0]);
                CloseDescriptor(outputPipe[1]);
                std::vector<char*> argv;
                argv.reserve(arguments.size() + 2U);
                auto executableText = executable.string();
                argv.push_back(executableText.data());
                for (const auto& argument : arguments)
                    argv.push_back(const_cast<char*>(argument.c_str()));
                argv.push_back(nullptr);
                ::execv(executable.c_str(), argv.data());
                ::_exit(127);
            }

            CloseDescriptor(inputPipe[0]);
            CloseDescriptor(outputPipe[1]);
            if (input)
            {
                std::size_t written = 0;
                while (written < input->size())
                {
                    const auto count = ::write(inputPipe[1], input->data() + written, input->size() - written);
                    if (count > 0)
                        written += static_cast<std::size_t>(count);
                    else if (count == -1 && errno == EINTR)
                        continue;
                    else
                        break;
                }
                static_cast<void>(::write(inputPipe[1], "\n", 1));
                CloseDescriptor(inputPipe[1]);
            }

            SecretToolResult result;
            std::array<char, 4096> buffer{};
            while (result.Output.size() <= MaximumStoredBytes)
            {
                const auto count = ::read(outputPipe[0], buffer.data(), buffer.size());
                if (count > 0)
                    result.Output.append(buffer.data(), static_cast<std::size_t>(count));
                else if (count == -1 && errno == EINTR)
                    continue;
                else
                    break;
            }
            CloseDescriptor(outputPipe[0]);
            int status = 0;
            while (::waitpid(process, &status, 0) == -1 && errno == EINTR)
            {
            }
            result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (result.Output.size() > MaximumStoredBytes)
            {
                return HubResult<SecretToolResult>::Failure(StorageError(HubErrorCode::InvalidData, affectedPath,
                                                                         "The Secret Service response was oversized."));
            }
            return HubResult<SecretToolResult>::Success(std::move(result));
        }
#elif defined(__APPLE__)
        class CoreFoundationValue final
        {
          public:
            explicit CoreFoundationValue(CFTypeRef value = nullptr) noexcept : m_Value(value) {}
            ~CoreFoundationValue()
            {
                if (m_Value)
                    CFRelease(m_Value);
            }

            CoreFoundationValue(const CoreFoundationValue&) = delete;
            CoreFoundationValue& operator=(const CoreFoundationValue&) = delete;

            [[nodiscard]] CFTypeRef Get() const noexcept { return m_Value; }

          private:
            CFTypeRef m_Value = nullptr;
        };

        [[nodiscard]] CoreFoundationValue KeychainAccount(const std::filesystem::path& path)
        {
            const auto account = SecretAccount(path);
            return CoreFoundationValue(
                CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(account.data()),
                                        static_cast<CFIndex>(account.size()), kCFStringEncodingUTF8, false));
        }

        [[nodiscard]] CoreFoundationValue KeychainQuery(const CFStringRef account, const bool returnData)
        {
            if (returnData)
            {
                const void* keys[]{kSecClass, kSecAttrService, kSecAttrAccount, kSecMatchLimit, kSecReturnData};
                const void* values[]{kSecClassGenericPassword, CFSTR("com.keire.hub"), account, kSecMatchLimitOne,
                                     kCFBooleanTrue};
                return CoreFoundationValue(CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                                                              &kCFTypeDictionaryKeyCallBacks,
                                                              &kCFTypeDictionaryValueCallBacks));
            }
            const void* keys[]{kSecClass, kSecAttrService, kSecAttrAccount};
            const void* values[]{kSecClassGenericPassword, CFSTR("com.keire.hub"), account};
            return CoreFoundationValue(CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                                                          &kCFTypeDictionaryKeyCallBacks,
                                                          &kCFTypeDictionaryValueCallBacks));
        }

        [[nodiscard]] HubError KeychainError(const HubErrorCode code, const std::filesystem::path& path,
                                             const std::string_view message, const OSStatus status)
        {
            return StorageError(code, path, message, "Keychain status " + std::to_string(status) + '.');
        }
#endif
    } // namespace

    AccountSessionStore::AccountSessionStore(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool AccountSessionStore::PersistentStorageAvailable() const noexcept
    {
#if defined(_WIN32)
        return true;
#elif defined(__linux__)
        return FindSecretTool().has_value();
#elif defined(__APPLE__)
        return true;
#else
        return false;
#endif
    }

    HubResult<std::optional<StoredAccountSession>> AccountSessionStore::LoadSession() const
    {
        auto loaded = LoadStoredPayload();
        if (!loaded)
            return HubResult<std::optional<StoredAccountSession>>::Failure(loaded.Error());
        if (!loaded.Value())
            return HubResult<std::optional<StoredAccountSession>>::Success(std::nullopt);
        auto decoded = DecodeSession(std::move(*loaded.Value()), m_Path);
        if (!decoded)
            return HubResult<std::optional<StoredAccountSession>>::Failure(decoded.Error());
        return HubResult<std::optional<StoredAccountSession>>::Success(std::move(decoded).Value());
    }

    HubStatus AccountSessionStore::SaveSession(const AccountSessionKind kind, const std::string_view refreshToken) const
    {
        if (refreshToken.empty() || refreshToken.size() > MaximumRefreshTokenBytes)
        {
            return HubStatus::Failure(StorageError(HubErrorCode::InvalidArgument, m_Path,
                                                   "The account session cannot be saved.",
                                                   "Invalid refresh-token length."));
        }
        auto encoded = EncodeSession(kind, refreshToken);
        auto status = SaveStoredPayload(encoded);
        std::fill(encoded.begin(), encoded.end(), '\0');
        return status;
    }

    HubResult<std::optional<std::string>> AccountSessionStore::LoadRefreshToken() const
    {
        auto loaded = LoadSession();
        if (!loaded)
            return HubResult<std::optional<std::string>>::Failure(loaded.Error());
        if (!loaded.Value())
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        return HubResult<std::optional<std::string>>::Success(std::move(loaded.Value()->RefreshToken));
    }

    HubResult<std::optional<std::string>> AccountSessionStore::LoadStoredPayload() const
    {
        if (!PersistentStorageAvailable())
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
#if defined(__linux__)
        const auto executable = FindSecretTool();
        if (!executable)
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        const auto account = SecretAccount(m_Path);
        auto result = RunSecretTool(*executable, {"lookup", "service", "com.keire.hub", "account", account},
                                    std::nullopt, m_Path);
        if (!result)
            return HubResult<std::optional<std::string>>::Failure(result.Error());
        if (result.Value().ExitCode == 1)
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        if (result.Value().ExitCode != 0)
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::IoRead, m_Path, "The saved account session could not be unlocked.",
                "Secret Service lookup failed with exit code " + std::to_string(result.Value().ExitCode) + '.'));
        }
        while (!result.Value().Output.empty() &&
               (result.Value().Output.back() == '\n' || result.Value().Output.back() == '\r'))
            result.Value().Output.pop_back();
        if (result.Value().Output.empty() || result.Value().Output.size() > MaximumSessionPayloadBytes)
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid token length."));
        }
        return HubResult<std::optional<std::string>>::Success(std::move(result.Value().Output));
#elif defined(__APPLE__)
        const auto account = KeychainAccount(m_Path);
        if (!account.Get())
            return HubResult<std::optional<std::string>>::Failure(
                StorageError(HubErrorCode::IoRead, m_Path, "The saved account session could not be identified."));
        const auto query = KeychainQuery(static_cast<CFStringRef>(account.Get()), true);
        if (!query.Get())
            return HubResult<std::optional<std::string>>::Failure(
                StorageError(HubErrorCode::IoRead, m_Path, "The saved account session could not be queried."));
        CFTypeRef value = nullptr;
        const auto status = SecItemCopyMatching(static_cast<CFDictionaryRef>(query.Get()), &value);
        CoreFoundationValue protectedValue(value);
        if (status == errSecItemNotFound)
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        if (status != errSecSuccess || !value || CFGetTypeID(value) != CFDataGetTypeID())
        {
            return HubResult<std::optional<std::string>>::Failure(KeychainError(
                HubErrorCode::IoRead, m_Path, "The saved account session could not be unlocked.", status));
        }
        const auto data = static_cast<CFDataRef>(value);
        const auto length = CFDataGetLength(data);
        if (length <= 0 || static_cast<std::size_t>(length) > MaximumSessionPayloadBytes)
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid token length."));
        }
        return HubResult<std::optional<std::string>>::Success(
            std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<std::size_t>(length)));
#else
        if (auto status = ValidatePath(m_Path); !status)
            return HubResult<std::optional<std::string>>::Failure(status.Error());
        std::error_code error;
        if (!std::filesystem::exists(m_Path, error))
        {
            if (error)
            {
                return HubResult<std::optional<std::string>>::Failure(StorageError(
                    HubErrorCode::IoRead, m_Path, "The saved account session could not be checked.", error.message()));
            }
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        }
        auto stored = Detail::ReadTextFile(m_Path, MaximumStoredBytes);
        if (!stored)
            return HubResult<std::optional<std::string>>::Failure(stored.Error());
        if (stored.Value().size() <= ProtectedFileHeader.size() ||
            std::memcmp(stored.Value().data(), ProtectedFileHeader.data(), ProtectedFileHeader.size()) != 0)
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid session header."));
        }
#if defined(_WIN32)
        if (stored.Value().size() - ProtectedFileHeader.size() > std::numeric_limits<DWORD>::max())
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Session is too large."));
        }
        DATA_BLOB input{.cbData = static_cast<DWORD>(stored.Value().size() - ProtectedFileHeader.size()),
                        .pbData = reinterpret_cast<BYTE*>(stored.Value().data() + ProtectedFileHeader.size())};
        DATA_BLOB output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            const std::error_code failure(static_cast<int>(GetLastError()), std::system_category());
            return HubResult<std::optional<std::string>>::Failure(
                StorageError(HubErrorCode::InvalidData, m_Path, "The saved account session could not be unlocked.",
                             failure.message()));
        }
        std::string token(reinterpret_cast<const char*>(output.pbData), output.cbData);
        if (output.pbData)
        {
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
        }
        if (token.empty() || token.size() > MaximumSessionPayloadBytes)
        {
            SecureZeroMemory(token.data(), token.size());
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid token length."));
        }
        return HubResult<std::optional<std::string>>::Success(std::move(token));
#else
        return HubResult<std::optional<std::string>>::Success(std::nullopt);
#endif
#endif
    }

    HubStatus AccountSessionStore::SaveRefreshToken(const std::string_view refreshToken) const
    {
        if (refreshToken.empty() || refreshToken.size() > MaximumRefreshTokenBytes)
        {
            return HubStatus::Failure(StorageError(HubErrorCode::InvalidArgument, m_Path,
                                                   "The account session cannot be saved.",
                                                   "Invalid refresh-token length."));
        }
        return SaveStoredPayload(refreshToken);
    }

    HubStatus AccountSessionStore::SaveStoredPayload(const std::string_view refreshToken) const
    {
        if (!PersistentStorageAvailable())
            return HubStatus::Success();
        if (refreshToken.empty() || refreshToken.size() > MaximumSessionPayloadBytes)
        {
            return HubStatus::Failure(StorageError(HubErrorCode::InvalidArgument, m_Path,
                                                   "The account session cannot be saved.",
                                                   "Invalid refresh-token length."));
        }
#if defined(_WIN32)
        DATA_BLOB input{.cbData = static_cast<DWORD>(refreshToken.size()),
                        .pbData = reinterpret_cast<BYTE*>(const_cast<char*>(refreshToken.data()))};
        DATA_BLOB output{};
        if (!CryptProtectData(&input, L"Kéire Hub account session", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            const std::error_code failure(static_cast<int>(GetLastError()), std::system_category());
            return HubStatus::Failure(StorageError(HubErrorCode::IoWrite, m_Path,
                                                   "The account session could not be secured.", failure.message()));
        }
        std::string stored(ProtectedFileHeader.size() + output.cbData, '\0');
        std::memcpy(stored.data(), ProtectedFileHeader.data(), ProtectedFileHeader.size());
        std::memcpy(stored.data() + ProtectedFileHeader.size(), output.pbData, output.cbData);
        if (output.pbData)
        {
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
        }
        auto status = Detail::WriteTextFileAtomically(m_Path, stored);
        SecureZeroMemory(stored.data(), stored.size());
        if (!status)
        {
            auto error = status.Error();
            error.Message = "The account session could not be saved.";
            return HubStatus::Failure(std::move(error));
        }
#elif defined(__linux__)
        const auto executable = FindSecretTool();
        if (!executable)
            return HubStatus::Success();
        const auto account = SecretAccount(m_Path);
        const auto result =
            RunSecretTool(*executable, {"store", "--label=Kéire Hub", "service", "com.keire.hub", "account", account},
                          refreshToken, m_Path);
        if (!result)
            return HubStatus::Failure(result.Error());
        if (result.Value().ExitCode != 0)
        {
            return HubStatus::Failure(StorageError(
                HubErrorCode::IoWrite, m_Path, "The account session could not be secured.",
                "Secret Service store failed with exit code " + std::to_string(result.Value().ExitCode) + '.'));
        }
#elif defined(__APPLE__)
        const auto account = KeychainAccount(m_Path);
        if (!account.Get())
            return HubStatus::Failure(
                StorageError(HubErrorCode::IoWrite, m_Path, "The account session could not be identified."));
        const auto query = KeychainQuery(static_cast<CFStringRef>(account.Get()), false);
        const auto data =
            CoreFoundationValue(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(refreshToken.data()),
                                             static_cast<CFIndex>(refreshToken.size())));
        if (!query.Get() || !data.Get())
            return HubStatus::Failure(
                StorageError(HubErrorCode::IoWrite, m_Path, "The account session could not be secured."));
        const void* updateKeys[]{kSecValueData};
        const void* updateValues[]{data.Get()};
        const CoreFoundationValue update(CFDictionaryCreate(kCFAllocatorDefault, updateKeys, updateValues, 1,
                                                            &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks));
        auto status =
            SecItemUpdate(static_cast<CFDictionaryRef>(query.Get()), static_cast<CFDictionaryRef>(update.Get()));
        if (status == errSecItemNotFound)
        {
            const void* addKeys[]{kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
            const void* addValues[]{kSecClassGenericPassword, CFSTR("com.keire.hub"), account.Get(), data.Get()};
            const CoreFoundationValue add(CFDictionaryCreate(kCFAllocatorDefault, addKeys, addValues,
                                                             std::size(addKeys), &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks));
            status = SecItemAdd(static_cast<CFDictionaryRef>(add.Get()), nullptr);
        }
        if (status != errSecSuccess)
        {
            return HubStatus::Failure(
                KeychainError(HubErrorCode::IoWrite, m_Path, "The account session could not be secured.", status));
        }
#endif
        return HubStatus::Success();
    }

    HubStatus AccountSessionStore::Clear() const
    {
#if defined(__linux__)
        if (const auto executable = FindSecretTool())
        {
            const auto account = SecretAccount(m_Path);
            const auto result = RunSecretTool(*executable, {"clear", "service", "com.keire.hub", "account", account},
                                              std::nullopt, m_Path);
            if (!result)
                return HubStatus::Failure(result.Error());
            if (result.Value().ExitCode != 0 && result.Value().ExitCode != 1)
            {
                return HubStatus::Failure(StorageError(
                    HubErrorCode::IoWrite, m_Path, "The saved account session could not be removed.",
                    "Secret Service clear failed with exit code " + std::to_string(result.Value().ExitCode) + '.'));
            }
        }
#elif defined(__APPLE__)
        const auto account = KeychainAccount(m_Path);
        if (!account.Get())
            return HubStatus::Failure(
                StorageError(HubErrorCode::IoWrite, m_Path, "The saved account session could not be identified."));
        const auto query = KeychainQuery(static_cast<CFStringRef>(account.Get()), false);
        if (!query.Get())
            return HubStatus::Failure(
                StorageError(HubErrorCode::IoWrite, m_Path, "The saved account session could not be queried."));
        const auto status = SecItemDelete(static_cast<CFDictionaryRef>(query.Get()));
        if (status != errSecSuccess && status != errSecItemNotFound)
        {
            return HubStatus::Failure(KeychainError(HubErrorCode::IoWrite, m_Path,
                                                    "The saved account session could not be removed.", status));
        }
#endif
        if (auto status = ValidatePath(m_Path); !status)
            return status;
        std::error_code error;
        const auto removed = std::filesystem::remove(m_Path, error);
        if (error)
        {
            return HubStatus::Failure(StorageError(HubErrorCode::IoWrite, m_Path,
                                                   "The saved account session could not be removed.", error.message()));
        }
        (void)removed;
        return HubStatus::Success();
    }

    const std::filesystem::path& AccountSessionStore::Path() const noexcept { return m_Path; }
} // namespace KeireHub
