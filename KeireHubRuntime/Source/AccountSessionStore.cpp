#include "KeireHubRuntime/AccountSessionStore.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::array<std::byte, 4> Header{std::byte{'K'}, std::byte{'H'}, std::byte{'S'}, std::byte{'1'}};
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
    } // namespace

    AccountSessionStore::AccountSessionStore(std::filesystem::path path) : m_Path(std::move(path)) {}

    bool AccountSessionStore::PersistentStorageAvailable() const noexcept
    {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }

    HubResult<std::optional<std::string>> AccountSessionStore::LoadRefreshToken() const
    {
        if (!PersistentStorageAvailable())
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
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
        if (stored.Value().size() <= Header.size() ||
            std::memcmp(stored.Value().data(), Header.data(), Header.size()) != 0)
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid session header."));
        }
#if defined(_WIN32)
        if (stored.Value().size() - Header.size() > std::numeric_limits<DWORD>::max())
        {
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Session is too large."));
        }
        DATA_BLOB input{.cbData = static_cast<DWORD>(stored.Value().size() - Header.size()),
                        .pbData = reinterpret_cast<BYTE*>(stored.Value().data() + Header.size())};
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
        if (token.empty() || token.size() > 4096U)
        {
            SecureZeroMemory(token.data(), token.size());
            return HubResult<std::optional<std::string>>::Failure(StorageError(
                HubErrorCode::InvalidData, m_Path, "The saved account session is invalid.", "Invalid token length."));
        }
        return HubResult<std::optional<std::string>>::Success(std::move(token));
#else
        return HubResult<std::optional<std::string>>::Success(std::nullopt);
#endif
    }

    HubStatus AccountSessionStore::SaveRefreshToken(const std::string_view refreshToken) const
    {
        if (!PersistentStorageAvailable())
            return HubStatus::Success();
        if (refreshToken.empty() || refreshToken.size() > 4096U)
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
        std::string stored(Header.size() + output.cbData, '\0');
        std::memcpy(stored.data(), Header.data(), Header.size());
        std::memcpy(stored.data() + Header.size(), output.pbData, output.cbData);
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
#endif
        return HubStatus::Success();
    }

    HubStatus AccountSessionStore::Clear() const
    {
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
