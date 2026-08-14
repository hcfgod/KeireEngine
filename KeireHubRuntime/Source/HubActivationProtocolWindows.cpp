#include "KeireHubRuntime/HubActivationProtocol.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        constexpr wchar_t ProtocolKey[] = L"Software\\Classes\\keirehub";

        struct RegistryValueBackup final
        {
            std::wstring Key;
            std::wstring Name;
            bool KeyExisted = false;
            bool ValueExisted = false;
            DWORD Type = REG_NONE;
            std::vector<std::byte> Data;
        };

        [[nodiscard]] HubError RegistryError(const LSTATUS status)
        {
            return {.Code = HubErrorCode::IoWrite,
                    .Message = "Kéire Hub could not register its browser sign-in callback.",
                    .Retryable = true,
                    .AffectedItem = "keirehub-protocol",
                    .TechnicalDetails = "Windows registry status " + std::to_string(status) + '.'};
        }

        [[nodiscard]] RegistryValueBackup CaptureValue(std::wstring key, std::wstring name, LSTATUS& failure) noexcept
        {
            failure = ERROR_SUCCESS;
            RegistryValueBackup result{.Key = std::move(key), .Name = std::move(name)};
            HKEY opened = nullptr;
            const auto openedStatus = RegOpenKeyExW(HKEY_CURRENT_USER, result.Key.c_str(), 0, KEY_QUERY_VALUE, &opened);
            if (openedStatus == ERROR_FILE_NOT_FOUND)
                return result;
            if (openedStatus != ERROR_SUCCESS)
            {
                failure = openedStatus;
                return result;
            }
            result.KeyExisted = true;

            DWORD size = 0;
            const auto* valueName = result.Name.empty() ? nullptr : result.Name.c_str();
            const auto queried = RegQueryValueExW(opened, valueName, nullptr, &result.Type, nullptr, &size);
            if (queried == ERROR_SUCCESS)
            {
                result.ValueExisted = true;
                result.Data.resize(size);
                if (size != 0U)
                {
                    auto readableSize = size;
                    const auto read = RegQueryValueExW(opened, valueName, nullptr, &result.Type,
                                                       reinterpret_cast<BYTE*>(result.Data.data()), &readableSize);
                    if (read != ERROR_SUCCESS)
                    {
                        RegCloseKey(opened);
                        failure = read;
                        return result;
                    }
                }
            }
            else if (queried != ERROR_FILE_NOT_FOUND)
            {
                RegCloseKey(opened);
                failure = queried;
                return result;
            }
            RegCloseKey(opened);
            return result;
        }

        [[nodiscard]] LSTATUS SetString(const std::wstring& key, const wchar_t* name, const std::wstring& value)
        {
            HKEY opened = nullptr;
            const auto created = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                                 KEY_SET_VALUE, nullptr, &opened, nullptr);
            if (created != ERROR_SUCCESS)
                return created;
            const auto bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
            const auto written =
                RegSetValueExW(opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
            RegCloseKey(opened);
            return written;
        }

        [[nodiscard]] bool KeyExists(const std::wstring& key, LSTATUS& failure) noexcept
        {
            HKEY opened = nullptr;
            const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE, &opened);
            if (status == ERROR_FILE_NOT_FOUND)
                return false;
            if (status != ERROR_SUCCESS)
            {
                failure = status;
                return false;
            }
            RegCloseKey(opened);
            return true;
        }

        void RestoreValue(const RegistryValueBackup& backup) noexcept
        {
            if (!backup.KeyExisted)
            {
                RegDeleteTreeW(HKEY_CURRENT_USER, backup.Key.c_str());
                return;
            }
            HKEY opened = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, backup.Key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                KEY_SET_VALUE, nullptr, &opened, nullptr) != ERROR_SUCCESS)
            {
                return;
            }
            const auto* valueName = backup.Name.empty() ? nullptr : backup.Name.c_str();
            if (backup.ValueExisted)
            {
                static_cast<void>(RegSetValueExW(opened, valueName, 0, backup.Type,
                                                 reinterpret_cast<const BYTE*>(backup.Data.data()),
                                                 static_cast<DWORD>(backup.Data.size())));
            }
            else
                static_cast<void>(RegDeleteValueW(opened, valueName));
            RegCloseKey(opened);
        }

        void RollBack(const std::array<RegistryValueBackup, 4>& backups, const bool shellExisted,
                      const bool openExisted) noexcept
        {
            for (auto iterator = backups.rbegin(); iterator != backups.rend(); ++iterator)
                RestoreValue(*iterator);
            if (!openExisted)
                static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\keirehub\\shell\\open"));
            if (!shellExisted)
                static_cast<void>(RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\keirehub\\shell"));
        }
    } // namespace

    HubStatus EnsureHubActivationProtocolRegistration(const std::filesystem::path& executable)
    {
        auto registration = PlanHubActivationProtocolRegistration(executable);
        if (!registration)
            return HubStatus::Failure(registration.Error());
        std::error_code error;
        if (!std::filesystem::is_regular_file(registration.Value().Executable, error) || error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The running Kéire Hub executable could not be registered.",
                                       .AffectedItem = "keirehub-protocol"});
        }

        LSTATUS captureFailure = ERROR_SUCCESS;
        const bool shellExisted = KeyExists(std::wstring(ProtocolKey) + L"\\shell", captureFailure);
        const bool openExisted = captureFailure == ERROR_SUCCESS
                                     ? KeyExists(std::wstring(ProtocolKey) + L"\\shell\\open", captureFailure)
                                     : false;
        if (captureFailure != ERROR_SUCCESS)
            return HubStatus::Failure(RegistryError(captureFailure));
        std::array<RegistryValueBackup, 4> backups;
        backups[0] = CaptureValue(ProtocolKey, L"", captureFailure);
        if (captureFailure == ERROR_SUCCESS)
            backups[1] = CaptureValue(ProtocolKey, L"URL Protocol", captureFailure);
        if (captureFailure == ERROR_SUCCESS)
            backups[2] = CaptureValue(std::wstring(ProtocolKey) + L"\\DefaultIcon", L"", captureFailure);
        if (captureFailure == ERROR_SUCCESS)
            backups[3] = CaptureValue(std::wstring(ProtocolKey) + L"\\shell\\open\\command", L"", captureFailure);
        if (captureFailure != ERROR_SUCCESS)
            return HubStatus::Failure(RegistryError(captureFailure));
        const auto executableWide = registration.Value().Executable.wstring();
        const std::array writes{
            SetString(ProtocolKey, nullptr, L"URL:Kéire Hub Protocol"),
            SetString(ProtocolKey, L"URL Protocol", L""),
            SetString(std::wstring(ProtocolKey) + L"\\DefaultIcon", nullptr, L"\"" + executableWide + L"\",0"),
            SetString(std::wstring(ProtocolKey) + L"\\shell\\open\\command", nullptr,
                      L"\"" + executableWide + L"\" \"%1\""),
        };
        for (const auto status : writes)
        {
            if (status == ERROR_SUCCESS)
                continue;
            RollBack(backups, shellExisted, openExisted);
            return HubStatus::Failure(RegistryError(status));
        }
        return HubStatus::Success();
    }
} // namespace KeireHub

#endif
