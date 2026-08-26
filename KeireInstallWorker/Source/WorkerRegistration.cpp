#include "KeireInstallWorker/WorkerRegistration.h"
#include "KeireInstallWorker/RegistryPaths.h"

#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>

#include <array>
#include <cwchar>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#ifndef KEIRE_INSTALL_PRODUCT_IDENTIFIER
#define KEIRE_INSTALL_PRODUCT_IDENTIFIER "Keire"
#endif
#ifndef KEIRE_INSTALL_PRODUCT_DISPLAY_NAME
#define KEIRE_INSTALL_PRODUCT_DISPLAY_NAME "Keire"
#endif
#ifndef KEIRE_INSTALL_EDITOR_TARGET
#define KEIRE_INSTALL_EDITOR_TARGET "KeireClient"
#endif
#ifndef KEIRE_INSTALL_HUB_TARGET
#define KEIRE_INSTALL_HUB_TARGET "KeireHub"
#endif

namespace KeireInstallWorker
{
    namespace
    {
        using KeireHub::HubError;
        using KeireHub::HubErrorCode;
        using KeireHub::HubResult;
        using KeireHub::HubStatus;
        using KeireHub::InstallLegacyCandidate;
        using KeireHub::InstallProduct;
        using KeireHub::InstallRegistration;

        [[nodiscard]] HubError RegistrationError(std::string message, std::string details = {})
        {
            return {.Code = HubErrorCode::IoWrite,
                    .Message = std::move(message),
                    .AffectedItem = "Windows installation registration",
                    .TechnicalDetails = std::move(details)};
        }

#if defined(_WIN32)
        [[nodiscard]] std::wstring ProductKey(const InstallProduct product)
        {
            return Detail::ProductRegistrationKey(product);
        }

        [[nodiscard]] std::wstring UninstallKey(const InstallProduct product)
        {
            return Detail::UninstallRegistrationKey(product);
        }

        [[nodiscard]] std::wstring HubProtocolKey() { return Detail::HubProtocolRegistrationKey(); }

        [[nodiscard]] std::wstring ToWide(const std::string& value)
        {
            if (value.empty())
                return {};
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0);
            if (count <= 0)
                throw std::runtime_error("Invalid UTF-8 registration value.");
            std::wstring result(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), count) != count)
                throw std::runtime_error("Could not convert a registration value.");
            return result;
        }

        [[nodiscard]] std::string ToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};
            const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (count <= 0)
                throw std::runtime_error("Invalid UTF-16 registration value.");
            std::string result(static_cast<std::size_t>(count), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), count, nullptr, nullptr) != count)
                throw std::runtime_error("Could not convert a registration value.");
            return result;
        }

        [[nodiscard]] HubError RegistrationDriftError(std::string message, std::wstring valueName,
                                                      const std::wstring_view expected = {},
                                                      const std::wstring_view actual = {})
        {
            auto details = ToUtf8(valueName);
            if (!expected.empty() || !actual.empty())
                details +=
                    " expected='" + ToUtf8(std::wstring(expected)) + "' actual='" + ToUtf8(std::wstring(actual)) + "'";
            return {.Code = HubErrorCode::DestinationConflict,
                    .Message = std::move(message),
                    .AffectedItem = "Windows installation registration",
                    .TechnicalDetails = std::move(details)};
        }

        class RegistryKey final
        {
          public:
            explicit RegistryKey(HKEY value = nullptr) noexcept : m_Value(value) {}
            ~RegistryKey()
            {
                if (m_Value)
                    RegCloseKey(m_Value);
            }

            RegistryKey(const RegistryKey&) = delete;
            RegistryKey& operator=(const RegistryKey&) = delete;
            RegistryKey(RegistryKey&& other) noexcept : m_Value(std::exchange(other.m_Value, nullptr)) {}

            [[nodiscard]] HKEY Get() const noexcept { return m_Value; }

          private:
            HKEY m_Value = nullptr;
        };

        [[nodiscard]] HubResult<std::optional<RegistryKey>> OpenKey(const std::wstring& path)
        {
            HKEY raw = nullptr;
            const auto result =
                RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &raw);
            if (result == ERROR_FILE_NOT_FOUND)
                return HubResult<std::optional<RegistryKey>>::Success(std::nullopt);
            if (result != ERROR_SUCCESS)
            {
                return HubResult<std::optional<RegistryKey>>::Failure(
                    RegistrationError("The product registration could not be opened.", std::to_string(result)));
            }
            return HubResult<std::optional<RegistryKey>>::Success(RegistryKey(raw));
        }

        [[nodiscard]] HubResult<std::wstring> ReadString(HKEY key, const wchar_t* name)
        {
            DWORD type = 0;
            DWORD bytes = 0;
            auto result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
            if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 64U * 1024U)
            {
                return HubResult<std::wstring>::Failure(
                    RegistrationError("The product registration is incomplete.",
                                      "value='" + ToUtf8(name ? std::wstring(name) : std::wstring{}) +
                                          "' error=" + std::to_string(result)));
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
            if (result != ERROR_SUCCESS)
            {
                return HubResult<std::wstring>::Failure(
                    RegistrationError("The product registration could not be read.", std::to_string(result)));
            }
            while (!value.empty() && value.back() == L'\0')
                value.pop_back();
            return HubResult<std::wstring>::Success(std::move(value));
        }

        [[nodiscard]] HubResult<std::optional<std::wstring>> ReadOptionalString(HKEY key, const wchar_t* name)
        {
            DWORD type = 0;
            DWORD bytes = 0;
            const auto result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
            if (result == ERROR_FILE_NOT_FOUND)
                return HubResult<std::optional<std::wstring>>::Success(std::nullopt);
            if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 64U * 1024U)
            {
                return HubResult<std::optional<std::wstring>>::Failure(
                    RegistrationError("The product registration value is invalid.", std::to_string(result)));
            }
            auto value = ReadString(key, name);
            return value ? HubResult<std::optional<std::wstring>>::Success(std::move(value).Value())
                         : HubResult<std::optional<std::wstring>>::Failure(value.Error());
        }

        [[nodiscard]] HubResult<bool>
        RegistryKeyIsEmptyWithKnownSubkeys(const std::wstring& path,
                                           const std::span<const std::wstring_view> allowedSubkeys)
        {
            auto key = OpenKey(path);
            if (!key)
                return HubResult<bool>::Failure(key.Error());
            if (!key.Value())
                return HubResult<bool>::Success(true);
            DWORD subkeyCount = 0;
            DWORD maximumSubkeyLength = 0;
            DWORD valueCount = 0;
            const auto queried =
                RegQueryInfoKeyW(key.Value()->Get(), nullptr, nullptr, nullptr, &subkeyCount, &maximumSubkeyLength,
                                 nullptr, &valueCount, nullptr, nullptr, nullptr, nullptr);
            if (queried != ERROR_SUCCESS)
            {
                return HubResult<bool>::Failure(
                    RegistrationError("The protocol registration could not be inspected.", std::to_string(queried)));
            }
            if (valueCount != 0)
                return HubResult<bool>::Success(false);
            std::vector<wchar_t> name(static_cast<std::size_t>(maximumSubkeyLength) + 1U, L'\0');
            for (DWORD index = 0; index < subkeyCount; ++index)
            {
                DWORD length = static_cast<DWORD>(name.size());
                const auto enumerated =
                    RegEnumKeyExW(key.Value()->Get(), index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
                if (enumerated != ERROR_SUCCESS)
                {
                    return HubResult<bool>::Failure(RegistrationError(
                        "The protocol registration could not be enumerated.", std::to_string(enumerated)));
                }
                const std::wstring_view actual(name.data(), length);
                if (!std::ranges::any_of(allowedSubkeys,
                                         [&actual](const auto expected)
                                         {
                                             return actual.size() == expected.size() &&
                                                    _wcsnicmp(actual.data(), expected.data(), actual.size()) == 0;
                                         }))
                {
                    return HubResult<bool>::Success(false);
                }
            }
            return HubResult<bool>::Success(true);
        }

        [[nodiscard]] HubResult<bool> IsEmptyHubProtocolSkeleton()
        {
            const std::array rootSubkeys{std::wstring_view(L"DefaultIcon"), std::wstring_view(L"shell")};
            const std::array shellSubkeys{std::wstring_view(L"open")};
            const std::array openSubkeys{std::wstring_view(L"command")};
            const std::array<std::wstring_view, 0> noSubkeys{};
            const auto root = HubProtocolKey();
            const std::array checks{
                std::pair{root, std::span<const std::wstring_view>(rootSubkeys)},
                std::pair{root + L"\\DefaultIcon", std::span<const std::wstring_view>(noSubkeys)},
                std::pair{root + L"\\shell", std::span<const std::wstring_view>(shellSubkeys)},
                std::pair{root + L"\\shell\\open", std::span<const std::wstring_view>(openSubkeys)},
                std::pair{root + L"\\shell\\open\\command", std::span<const std::wstring_view>(noSubkeys)}};
            for (const auto& [path, subkeys] : checks)
            {
                auto empty = RegistryKeyIsEmptyWithKnownSubkeys(path, subkeys);
                if (!empty || !empty.Value())
                    return empty;
            }
            return HubResult<bool>::Success(true);
        }

        [[nodiscard]] HubStatus WriteString(HKEY key, const wchar_t* name, const std::wstring& value)
        {
            const auto bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
            const auto result =
                RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
            return result == ERROR_SUCCESS
                       ? HubStatus::Success()
                       : HubStatus::Failure(RegistrationError("The product registration could not be written.",
                                                              std::to_string(result)));
        }

        [[nodiscard]] HubStatus WriteDword(HKEY key, const wchar_t* name, const DWORD value)
        {
            const auto result =
                RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            return result == ERROR_SUCCESS
                       ? HubStatus::Success()
                       : HubStatus::Failure(RegistrationError("The product registration could not be written.",
                                                              std::to_string(result)));
        }

        using RegistryStringValues = std::vector<std::pair<std::wstring, std::wstring>>;

        [[nodiscard]] RegistryStringValues ProductRegistrationValues(const InstallRegistration& registration)
        {
            return {{L"ProductId", ToWide(registration.ProductId)},
                    {L"InstallationId", ToWide(registration.InstallationId)},
                    {L"InstallDirectory", registration.Root.native()},
                    {L"DisplayVersion", ToWide(registration.Version)},
                    {L"ManifestFingerprint", ToWide(registration.ManifestFingerprint)},
                    {L"ReceiptSha256", ToWide(registration.ReceiptSha256)}};
        }

        [[nodiscard]] RegistryStringValues UninstallRegistrationValues(const InstallRegistration& registration,
                                                                       const InstallProduct product)
        {
            const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) +
                                     (product == InstallProduct::Editor ? L" Editor" : L" Hub");
            const auto executable = registration.Root / "bin" /
                                    (std::string(product == InstallProduct::Editor ? KEIRE_INSTALL_EDITOR_TARGET
                                                                                   : KEIRE_INSTALL_HUB_TARGET) +
                                     ".exe");
            const auto uninstaller = registration.Root / "Uninstall.exe";
            return {{L"ProductId", ToWide(registration.ProductId)},
                    {L"InstallationId", ToWide(registration.InstallationId)},
                    {L"ManifestFingerprint", ToWide(registration.ManifestFingerprint)},
                    {L"ReceiptSha256", ToWide(registration.ReceiptSha256)},
                    {L"DisplayName", productName},
                    {L"DisplayVersion", ToWide(registration.Version)},
                    {L"DisplayIcon", executable.native()},
                    {L"Publisher", std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME)},
                    {L"InstallLocation", registration.Root.native()},
                    {L"UninstallString", L"\"" + uninstaller.native() + L"\""},
                    {L"QuietUninstallString", L"\"" + uninstaller.native() + L"\" /S"}};
        }

        [[nodiscard]] HubResult<std::optional<DWORD>> ReadOptionalDword(HKEY key, const wchar_t* name)
        {
            DWORD type = 0;
            DWORD bytes = sizeof(DWORD);
            DWORD value = 0;
            const auto result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes);
            if (result == ERROR_FILE_NOT_FOUND)
                return HubResult<std::optional<DWORD>>::Success(std::nullopt);
            if (result != ERROR_SUCCESS || type != REG_DWORD || bytes != sizeof(DWORD))
            {
                return HubResult<std::optional<DWORD>>::Failure(
                    RegistrationError("The product registration value is invalid.", std::to_string(result)));
            }
            return HubResult<std::optional<DWORD>>::Success(value);
        }

        [[nodiscard]] HubStatus RemoveExactValues(const std::wstring& path, const RegistryStringValues& strings,
                                                  const std::span<const std::pair<std::wstring, DWORD>> dwords,
                                                  const bool allowMissing)
        {
            HKEY raw = nullptr;
            const auto opened =
                RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &raw);
            if (opened == ERROR_FILE_NOT_FOUND && allowMissing)
                return HubStatus::Success();
            if (opened != ERROR_SUCCESS)
            {
                return HubStatus::Failure(
                    RegistrationError("The product registration could not be opened.", std::to_string(opened)));
            }
            RegistryKey key(raw);
            for (const auto& [name, expected] : strings)
            {
                auto actual = ReadOptionalString(key.Get(), name.c_str());
                if (!actual)
                    return HubStatus::Failure(actual.Error());
                if (!actual.Value())
                {
                    if (allowMissing)
                        continue;
                    return HubStatus::Failure(RegistrationDriftError("An owned registration value is missing.", name));
                }
                if (*actual.Value() != expected)
                {
                    return HubStatus::Failure(RegistrationDriftError(
                        "An owned registration value changed before removal.", name, expected, *actual.Value()));
                }
            }
            for (const auto& [name, expected] : dwords)
            {
                auto actual = ReadOptionalDword(key.Get(), name.c_str());
                if (!actual)
                    return HubStatus::Failure(actual.Error());
                if (!actual.Value())
                {
                    if (allowMissing)
                        continue;
                    return HubStatus::Failure(RegistrationDriftError("An owned registration value is missing.", name));
                }
                if (*actual.Value() != expected)
                {
                    return HubStatus::Failure(
                        RegistrationDriftError("An owned registration value changed before removal.", name));
                }
            }
            for (const auto& [name, expected] : strings)
            {
                (void)expected;
                const auto result = RegDeleteValueW(key.Get(), name.c_str());
                if (result != ERROR_SUCCESS && !(allowMissing && result == ERROR_FILE_NOT_FOUND))
                {
                    return HubStatus::Failure(
                        RegistrationError("An owned registration value could not be removed.", std::to_string(result)));
                }
            }
            for (const auto& [name, expected] : dwords)
            {
                (void)expected;
                const auto result = RegDeleteValueW(key.Get(), name.c_str());
                if (result != ERROR_SUCCESS && !(allowMissing && result == ERROR_FILE_NOT_FOUND))
                {
                    return HubStatus::Failure(
                        RegistrationError("An owned registration value could not be removed.", std::to_string(result)));
                }
            }
            // Keys are deliberately retained. Deleting a registry tree or even an apparently empty key can race a
            // concurrent writer and would make unknown values or subkeys part of the installer's deletion authority.
            return HubStatus::Success();
        }

        struct LegacyRegistrationSnapshot final
        {
            InstallLegacyCandidate Candidate;
            std::wstring OwnershipMarker;
            bool HasOwnershipMarker = false;
        };

        [[nodiscard]] std::wstring ExpectedHubCommand(const std::filesystem::path& root);

        [[nodiscard]] RegistryStringValues LegacyUninstallValues(const InstallLegacyCandidate& candidate)
        {
            const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) +
                                     (candidate.Product == InstallProduct::Editor ? L" Editor" : L" Hub");
            const auto target =
                candidate.Product == InstallProduct::Editor ? KEIRE_INSTALL_EDITOR_TARGET : KEIRE_INSTALL_HUB_TARGET;
            const auto executable = candidate.Root / "bin" / (std::string(target) + ".exe");
            const auto uninstaller = candidate.Root / "Uninstall.exe";
            return {{L"DisplayName", productName},
                    {L"DisplayVersion", ToWide(candidate.Version)},
                    {L"DisplayIcon", executable.native()},
                    {L"Publisher", std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME)},
                    {L"InstallLocation", candidate.Root.native()},
                    {L"UninstallString", L"\"" + uninstaller.native() + L"\""},
                    {L"QuietUninstallString", L"\"" + uninstaller.native() + L"\" /S"}};
        }

        [[nodiscard]] HubStatus ValidateExactStringValues(const std::wstring& path,
                                                          const RegistryStringValues& expected)
        {
            auto key = OpenKey(path);
            if (!key)
                return HubStatus::Failure(key.Error());
            if (!key.Value())
            {
                return HubStatus::Failure(
                    RegistrationError("A required legacy registration key is missing.", ToUtf8(path)));
            }
            for (const auto& [name, value] : expected)
            {
                auto actual = ReadOptionalString(key.Value()->Get(), name.c_str());
                if (!actual)
                    return HubStatus::Failure(actual.Error());
                if (!actual.Value() || *actual.Value() != value)
                {
                    return HubStatus::Failure(
                        RegistrationDriftError("A legacy registration value does not match the owned installation.",
                                               name, value, actual.Value().value_or(L"<missing>")));
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateLegacyUninstallDwords(const std::wstring& path)
        {
            auto key = OpenKey(path);
            if (!key)
                return HubStatus::Failure(key.Error());
            if (!key.Value())
                return HubStatus::Failure(RegistrationError("The legacy uninstall registration is missing."));
            for (const auto* name : {L"NoModify", L"NoRepair"})
            {
                auto value = ReadOptionalDword(key.Value()->Get(), name);
                if (!value)
                    return HubStatus::Failure(value.Error());
                if (!value.Value() || *value.Value() != 1)
                {
                    return HubStatus::Failure(
                        RegistrationDriftError("A legacy uninstall policy value is invalid.", name));
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateLegacyHubProtocol(const InstallLegacyCandidate& candidate)
        {
            const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) + L" Hub";
            const auto executable = candidate.Root / "bin" / (std::string(KEIRE_INSTALL_HUB_TARGET) + ".exe");
            const std::array values{
                std::pair{HubProtocolKey(),
                          RegistryStringValues{{L"", L"URL:" + productName + L" Protocol"}, {L"URL Protocol", L""}}},
                std::pair{HubProtocolKey() + L"\\DefaultIcon",
                          RegistryStringValues{{L"", executable.native() + L",0"}}},
                std::pair{HubProtocolKey() + L"\\shell\\open\\command",
                          RegistryStringValues{{L"", ExpectedHubCommand(candidate.Root)}}}};
            for (const auto& [path, expected] : values)
            {
                if (const auto status = ValidateExactStringValues(path, expected); !status)
                    return status;
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateLegacyOwnershipMarkers(const LegacyRegistrationSnapshot& snapshot)
        {
            for (const auto& path : {ProductKey(snapshot.Candidate.Product), UninstallKey(snapshot.Candidate.Product)})
            {
                auto key = OpenKey(path);
                if (!key)
                    return HubStatus::Failure(key.Error());
                if (!key.Value())
                    return HubStatus::Failure(RegistrationError("A legacy registration key is missing."));
                auto marker = ReadOptionalString(key.Value()->Get(), L"OwnershipMarker");
                if (!marker)
                    return HubStatus::Failure(marker.Error());
                if ((snapshot.HasOwnershipMarker && (!marker.Value() || *marker.Value() != snapshot.OwnershipMarker)) ||
                    (!snapshot.HasOwnershipMarker && marker.Value()))
                {
                    return HubStatus::Failure(RegistrationDriftError(
                        "A legacy ownership value changed during migration.", L"OwnershipMarker"));
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] std::wstring ExpectedHubCommand(const std::filesystem::path& root)
        {
            return L"\"" + (root / "bin" / (std::string(KEIRE_INSTALL_HUB_TARGET) + ".exe")).native() + L"\" \"%1\"";
        }

        [[nodiscard]] std::wstring ExpectedHubCommand(const InstallRegistration& registration)
        {
            return ExpectedHubCommand(registration.Root);
        }

        [[nodiscard]] std::wstring LegacyMarkerGuid(const InstallProduct product)
        {
            return product == InstallProduct::Editor ? L"{1D37B84D-13B7-4C73-96BD-6D23AD40757A}"
                                                     : L"{B2499023-1E3C-4F87-A8D5-E8DFA0470B97}";
        }

        [[nodiscard]] HubResult<LegacyRegistrationSnapshot>
        ValidateLegacyRegistration(const InstallLegacyCandidate& candidate)
        {
            try
            {
                const auto legacyMarkerPath = std::filesystem::path(
                    candidate.Product == InstallProduct::Editor ? ".keire-editor-install" : ".keire-hub-install");
                KeireHub::Detail::InstallMutationFileSystem files(candidate.Root);
                auto marker = files.ReadText(legacyMarkerPath, 1024U);
                if (!marker)
                    return HubResult<LegacyRegistrationSnapshot>::Failure(marker.Error());
                const auto guid = LegacyMarkerGuid(candidate.Product);
                const auto fullMarker = guid + L"|" + std::wstring(L"" KEIRE_INSTALL_PRODUCT_IDENTIFIER);
                const auto markerText = ToWide(marker.Value());
                const bool bare = markerText == guid + L"\r\n";
                const bool bound = markerText == fullMarker + L"\r\n";
                if (!bare && !bound)
                {
                    return HubResult<LegacyRegistrationSnapshot>::Failure(
                        RegistrationError("The legacy ownership marker is not exact."));
                }

                auto productKey = OpenKey(ProductKey(candidate.Product));
                auto uninstallKey = OpenKey(UninstallKey(candidate.Product));
                if (!productKey || !uninstallKey)
                    return HubResult<LegacyRegistrationSnapshot>::Failure(!productKey ? productKey.Error()
                                                                                      : uninstallKey.Error());
                if (!productKey.Value() || !uninstallKey.Value())
                {
                    return HubResult<LegacyRegistrationSnapshot>::Failure(
                        RegistrationError("The legacy product registration is missing."));
                }
                auto productRoot = ReadString(productKey.Value()->Get(), L"InstallDirectory");
                auto productOwnership = ReadOptionalString(productKey.Value()->Get(), L"OwnershipMarker");
                auto uninstallOwnership = ReadOptionalString(uninstallKey.Value()->Get(), L"OwnershipMarker");
                if (!productRoot || !productOwnership || !uninstallOwnership)
                {
                    return HubResult<LegacyRegistrationSnapshot>::Failure(
                        RegistrationError("The legacy registration is incomplete."));
                }
                if (std::filesystem::path(productRoot.Value()).lexically_normal() !=
                        candidate.Root.lexically_normal() ||
                    (bound && (!productOwnership.Value() || *productOwnership.Value() != fullMarker ||
                               !uninstallOwnership.Value() || *uninstallOwnership.Value() != fullMarker)) ||
                    (productOwnership.Value() && *productOwnership.Value() != fullMarker) ||
                    (uninstallOwnership.Value() && *uninstallOwnership.Value() != fullMarker))
                {
                    return HubResult<LegacyRegistrationSnapshot>::Failure(
                        RegistrationError("The legacy marker and registration roots do not agree."));
                }
                if (const auto exact =
                        ValidateExactStringValues(UninstallKey(candidate.Product), LegacyUninstallValues(candidate));
                    !exact)
                {
                    return HubResult<LegacyRegistrationSnapshot>::Failure(exact.Error());
                }
                if (const auto dwords = ValidateLegacyUninstallDwords(UninstallKey(candidate.Product)); !dwords)
                    return HubResult<LegacyRegistrationSnapshot>::Failure(dwords.Error());
                if (candidate.Product == InstallProduct::Hub)
                {
                    if (const auto protocol = ValidateLegacyHubProtocol(candidate); !protocol)
                        return HubResult<LegacyRegistrationSnapshot>::Failure(protocol.Error());
                }
                return HubResult<LegacyRegistrationSnapshot>::Success(
                    {.Candidate = candidate, .OwnershipMarker = fullMarker, .HasOwnershipMarker = bound});
            }
            catch (const std::exception& error)
            {
                return HubResult<LegacyRegistrationSnapshot>::Failure(
                    RegistrationError("The legacy registration is invalid.", error.what()));
            }
        }

        [[nodiscard]] HubStatus DeleteValue(HKEY key, const wchar_t* name)
        {
            const auto result = RegDeleteValueW(key, name);
            return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND
                       ? HubStatus::Success()
                       : HubStatus::Failure(RegistrationError("A modern registration value could not be rolled back.",
                                                              std::to_string(result)));
        }

        [[nodiscard]] HubStatus RestoreLegacyRegistration(const LegacyRegistrationSnapshot& snapshot)
        {
            if (const auto exact = ValidateExactStringValues(
                    ProductKey(snapshot.Candidate.Product),
                    RegistryStringValues{{L"InstallDirectory", snapshot.Candidate.Root.native()}});
                !exact)
            {
                return exact;
            }
            if (const auto exact = ValidateExactStringValues(UninstallKey(snapshot.Candidate.Product),
                                                             LegacyUninstallValues(snapshot.Candidate));
                !exact)
            {
                return exact;
            }
            if (const auto dwords = ValidateLegacyUninstallDwords(UninstallKey(snapshot.Candidate.Product)); !dwords)
                return dwords;
            if (const auto ownership = ValidateLegacyOwnershipMarkers(snapshot); !ownership)
                return ownership;
            if (snapshot.Candidate.Product == InstallProduct::Hub)
            {
                if (const auto protocol = ValidateLegacyHubProtocol(snapshot.Candidate); !protocol)
                    return protocol;
            }
            HKEY raw = nullptr;
            DWORD disposition = 0;
            auto result = RegCreateKeyExW(HKEY_CURRENT_USER, ProductKey(snapshot.Candidate.Product).c_str(), 0, nullptr,
                                          0, KEY_SET_VALUE, nullptr, &raw, &disposition);
            if (result != ERROR_SUCCESS)
                return HubStatus::Failure(RegistrationError("The legacy product registration could not be restored.",
                                                            std::to_string(result)));
            RegistryKey productKey(raw);
            for (const auto* name :
                 {L"ProductId", L"InstallationId", L"DisplayVersion", L"ManifestFingerprint", L"ReceiptSha256"})
            {
                if (const auto removed = DeleteValue(productKey.Get(), name); !removed)
                    return removed;
            }
            if (const auto written =
                    WriteString(productKey.Get(), L"InstallDirectory", snapshot.Candidate.Root.native());
                !written)
                return written;
            if (snapshot.HasOwnershipMarker)
            {
                if (const auto written = WriteString(productKey.Get(), L"OwnershipMarker", snapshot.OwnershipMarker);
                    !written)
                    return written;
            }
            else if (const auto removed = DeleteValue(productKey.Get(), L"OwnershipMarker"); !removed)
                return removed;

            raw = nullptr;
            result = RegCreateKeyExW(HKEY_CURRENT_USER, UninstallKey(snapshot.Candidate.Product).c_str(), 0, nullptr, 0,
                                     KEY_SET_VALUE, nullptr, &raw, &disposition);
            if (result != ERROR_SUCCESS)
                return HubStatus::Failure(RegistrationError("The legacy uninstall registration could not be restored.",
                                                            std::to_string(result)));
            RegistryKey uninstallKey(raw);
            for (const auto* name : {L"ProductId", L"InstallationId", L"ManifestFingerprint", L"ReceiptSha256"})
            {
                if (const auto removed = DeleteValue(uninstallKey.Get(), name); !removed)
                    return removed;
            }
            for (const auto& [name, value] : LegacyUninstallValues(snapshot.Candidate))
            {
                if (const auto written = WriteString(uninstallKey.Get(), name.c_str(), value); !written)
                    return written;
            }
            if (const auto written = WriteDword(uninstallKey.Get(), L"NoModify", 1); !written)
                return written;
            if (const auto written = WriteDword(uninstallKey.Get(), L"NoRepair", 1); !written)
                return written;
            if (snapshot.HasOwnershipMarker)
            {
                if (const auto written = WriteString(uninstallKey.Get(), L"OwnershipMarker", snapshot.OwnershipMarker);
                    !written)
                    return written;
            }
            else if (const auto removed = DeleteValue(uninstallKey.Get(), L"OwnershipMarker"); !removed)
                return removed;
            if (snapshot.Candidate.Product != InstallProduct::Hub)
                return HubStatus::Success();
            const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) + L" Hub";
            const auto executable = snapshot.Candidate.Root / "bin" / (std::string(KEIRE_INSTALL_HUB_TARGET) + ".exe");
            const std::array protocolValues{
                std::pair{HubProtocolKey(),
                          RegistryStringValues{{L"", L"URL:" + productName + L" Protocol"}, {L"URL Protocol", L""}}},
                std::pair{HubProtocolKey() + L"\\DefaultIcon",
                          RegistryStringValues{{L"", executable.native() + L",0"}}},
                std::pair{HubProtocolKey() + L"\\shell\\open\\command",
                          RegistryStringValues{{L"", ExpectedHubCommand(snapshot.Candidate.Root)}}}};
            for (const auto& [path, values] : protocolValues)
            {
                raw = nullptr;
                result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &raw,
                                         &disposition);
                if (result != ERROR_SUCCESS)
                {
                    return HubStatus::Failure(RegistrationError(
                        "The legacy Hub protocol registration could not be restored.", std::to_string(result)));
                }
                RegistryKey protocolKey(raw);
                for (const auto& [name, value] : values)
                {
                    if (const auto written = WriteString(protocolKey.Get(), name.c_str(), value); !written)
                        return written;
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateHubProtocolMutation(const InstallRegistration& attempted,
                                                            const std::optional<InstallRegistration>& baseline)
        {
            auto root = OpenKey(HubProtocolKey());
            if (!root)
                return HubStatus::Failure(root.Error());
            if (!root.Value())
                return HubStatus::Success();
            auto commandKey = OpenKey(HubProtocolKey() + L"\\shell\\open\\command");
            if (!commandKey)
                return HubStatus::Failure(commandKey.Error());
            if (!commandKey.Value())
            {
                auto empty = IsEmptyHubProtocolSkeleton();
                return empty && empty.Value()
                           ? HubStatus::Success()
                           : HubStatus::Failure(
                                 empty
                                     ? HubError{.Code = HubErrorCode::DestinationConflict,
                                                .Message = "An existing Hub protocol registration is not worker-owned.",
                                                .AffectedItem = "HKCU\\Software\\Classes\\keirehub"}
                                     : empty.Error());
            }
            auto command = ReadOptionalString(commandKey.Value()->Get(), L"");
            if (!command)
                return HubStatus::Failure(command.Error());
            if (!command.Value())
            {
                auto empty = IsEmptyHubProtocolSkeleton();
                return empty && empty.Value()
                           ? HubStatus::Success()
                           : HubStatus::Failure(
                                 empty
                                     ? HubError{.Code = HubErrorCode::DestinationConflict,
                                                .Message = "An existing Hub protocol registration is not worker-owned.",
                                                .AffectedItem = "HKCU\\Software\\Classes\\keirehub"}
                                     : empty.Error());
            }
            if (*command.Value() == ExpectedHubCommand(attempted) ||
                (baseline && *command.Value() == ExpectedHubCommand(*baseline)))
            {
                return HubStatus::Success();
            }
            return HubStatus::Failure({.Code = HubErrorCode::DestinationConflict,
                                       .Message = "An existing Hub protocol registration belongs to another command.",
                                       .AffectedItem = "HKCU\\Software\\Classes\\keirehub\\shell\\open\\command"});
        }

        [[nodiscard]] HubStatus WriteProductRegistration(const InstallRegistration& registration,
                                                         const InstallProduct product)
        {
            try
            {
                HKEY raw = nullptr;
                DWORD disposition = 0;
                auto result = RegCreateKeyExW(HKEY_CURRENT_USER, ProductKey(product).c_str(), 0, nullptr, 0,
                                              KEY_SET_VALUE, nullptr, &raw, &disposition);
                if (result != ERROR_SUCCESS)
                    return HubStatus::Failure(
                        RegistrationError("The product registration could not be created.", std::to_string(result)));
                RegistryKey productKey(raw);
                const std::array values{std::pair{L"ProductId", ToWide(registration.ProductId)},
                                        std::pair{L"InstallationId", ToWide(registration.InstallationId)},
                                        std::pair{L"InstallDirectory", registration.Root.native()},
                                        std::pair{L"DisplayVersion", ToWide(registration.Version)},
                                        std::pair{L"ManifestFingerprint", ToWide(registration.ManifestFingerprint)},
                                        std::pair{L"ReceiptSha256", ToWide(registration.ReceiptSha256)}};
                for (const auto& [name, value] : values)
                {
                    if (const auto status = WriteString(productKey.Get(), name, value); !status)
                        return status;
                }

                raw = nullptr;
                result = RegCreateKeyExW(HKEY_CURRENT_USER, UninstallKey(product).c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                                         nullptr, &raw, &disposition);
                if (result != ERROR_SUCCESS)
                    return HubStatus::Failure(
                        RegistrationError("The uninstall registration could not be created.", std::to_string(result)));
                RegistryKey uninstallKey(raw);
                const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) +
                                         (product == InstallProduct::Editor ? L" Editor" : L" Hub");
                const auto executable = registration.Root / "bin" /
                                        (std::string(product == InstallProduct::Editor ? KEIRE_INSTALL_EDITOR_TARGET
                                                                                       : KEIRE_INSTALL_HUB_TARGET) +
                                         ".exe");
                const auto uninstaller = registration.Root / "Uninstall.exe";
                const std::array uninstallValues{
                    std::pair{L"ProductId", ToWide(registration.ProductId)},
                    std::pair{L"InstallationId", ToWide(registration.InstallationId)},
                    std::pair{L"ManifestFingerprint", ToWide(registration.ManifestFingerprint)},
                    std::pair{L"ReceiptSha256", ToWide(registration.ReceiptSha256)},
                    std::pair{L"DisplayName", productName},
                    std::pair{L"DisplayVersion", ToWide(registration.Version)},
                    std::pair{L"DisplayIcon", executable.native()},
                    std::pair{L"Publisher", std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME)},
                    std::pair{L"InstallLocation", registration.Root.native()},
                    std::pair{L"UninstallString", L"\"" + uninstaller.native() + L"\""},
                    std::pair{L"QuietUninstallString", L"\"" + uninstaller.native() + L"\" /S"}};
                for (const auto& [name, value] : uninstallValues)
                {
                    if (const auto status = WriteString(uninstallKey.Get(), name, value); !status)
                        return status;
                }
                if (const auto status = WriteDword(uninstallKey.Get(), L"NoModify", 1); !status)
                    return status;
                if (const auto status = WriteDword(uninstallKey.Get(), L"NoRepair", 1); !status)
                    return status;
                if (product != InstallProduct::Hub)
                    return HubStatus::Success();

                const auto hubExecutable = registration.Root / "bin" / (std::string(KEIRE_INSTALL_HUB_TARGET) + ".exe");
                const std::array protocolValues{
                    std::pair{HubProtocolKey(), std::wstring(L"URL:") + productName + L" Protocol"},
                    std::pair{HubProtocolKey() + L"\\DefaultIcon", hubExecutable.native() + L",0"},
                    std::pair{HubProtocolKey() + L"\\shell\\open\\command",
                              L"\"" + hubExecutable.native() + L"\" \"%1\""}};
                for (const auto& [path, value] : protocolValues)
                {
                    raw = nullptr;
                    result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                             &raw, &disposition);
                    if (result != ERROR_SUCCESS)
                        return HubStatus::Failure(
                            RegistrationError("The Hub protocol could not be registered.", std::to_string(result)));
                    RegistryKey protocolKey(raw);
                    if (const auto status = WriteString(protocolKey.Get(), L"", value); !status)
                        return status;
                    if (path == HubProtocolKey())
                    {
                        if (const auto status = WriteString(protocolKey.Get(), L"URL Protocol", L""); !status)
                            return status;
                    }
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(RegistrationError("The product registration is invalid.", error.what()));
            }
        }

        [[nodiscard]] HubResult<std::optional<InstallRegistration>>
        ReadProductRegistration(const InstallProduct product)
        {
            auto key = OpenKey(ProductKey(product));
            if (!key || !key.Value())
                return key ? HubResult<std::optional<InstallRegistration>>::Success(std::nullopt)
                           : HubResult<std::optional<InstallRegistration>>::Failure(key.Error());
            try
            {
                auto productId = ReadOptionalString(key.Value()->Get(), L"ProductId");
                if (!productId)
                    return HubResult<std::optional<InstallRegistration>>::Failure(productId.Error());
                if (!productId.Value())
                    return HubResult<std::optional<InstallRegistration>>::Success(std::nullopt);
                auto installationId = ReadString(key.Value()->Get(), L"InstallationId");
                auto root = ReadString(key.Value()->Get(), L"InstallDirectory");
                auto version = ReadString(key.Value()->Get(), L"DisplayVersion");
                auto fingerprint = ReadString(key.Value()->Get(), L"ManifestFingerprint");
                auto receipt = ReadString(key.Value()->Get(), L"ReceiptSha256");
                if (!installationId || !root || !version || !fingerprint || !receipt)
                {
                    const auto& error = !installationId ? installationId.Error()
                                        : !root         ? root.Error()
                                        : !version      ? version.Error()
                                        : !fingerprint  ? fingerprint.Error()
                                                        : receipt.Error();
                    return HubResult<std::optional<InstallRegistration>>::Failure(error);
                }
                return HubResult<std::optional<InstallRegistration>>::Success(
                    InstallRegistration{.ProductId = ToUtf8(*productId.Value()),
                                        .InstallationId = ToUtf8(installationId.Value()),
                                        .Root = root.Value(),
                                        .Version = ToUtf8(version.Value()),
                                        .ManifestFingerprint = ToUtf8(fingerprint.Value()),
                                        .ReceiptSha256 = ToUtf8(receipt.Value())});
            }
            catch (const std::exception& error)
            {
                return HubResult<std::optional<InstallRegistration>>::Failure(
                    RegistrationError("The product registration is invalid.", error.what()));
            }
        }

        [[nodiscard]] HubResult<bool> ExactStringValuesMatch(const std::wstring& path,
                                                             const RegistryStringValues& expected)
        {
            auto key = OpenKey(path);
            if (!key)
                return HubResult<bool>::Failure(key.Error());
            if (!key.Value())
                return HubResult<bool>::Success(false);
            for (const auto& [name, value] : expected)
            {
                auto actual = ReadOptionalString(key.Value()->Get(), name.c_str());
                if (!actual)
                    return HubResult<bool>::Failure(actual.Error());
                if (!actual.Value() || *actual.Value() != value)
                    return HubResult<bool>::Success(false);
            }
            return HubResult<bool>::Success(true);
        }

        [[nodiscard]] HubStatus RemoveHubProtocolIfOwned(const InstallRegistration& registration)
        {
            const auto productName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) + L" Hub";
            const auto executable = registration.Root / "bin" / (std::string(KEIRE_INSTALL_HUB_TARGET) + ".exe");
            const RegistryStringValues rootValues{{L"", L"URL:" + productName + L" Protocol"}, {L"URL Protocol", L""}};
            const RegistryStringValues iconValues{{L"", executable.native() + L",0"}};
            const RegistryStringValues commandValues{{L"", ExpectedHubCommand(registration)}};
            const std::array paths{std::pair{HubProtocolKey(), &rootValues},
                                   std::pair{HubProtocolKey() + L"\\DefaultIcon", &iconValues},
                                   std::pair{HubProtocolKey() + L"\\shell\\open\\command", &commandValues}};
            for (const auto& [path, values] : paths)
            {
                auto matches = ExactStringValuesMatch(path, *values);
                if (!matches)
                    return HubStatus::Failure(matches.Error());
                if (!matches.Value())
                {
                    // A missing or changed integration is no longer ours to mutate. Preserve it and continue the
                    // payload uninstall rather than deleting an external protocol handler.
                    return HubStatus::Success();
                }
            }
            for (const auto& [path, values] : std::views::reverse(paths))
            {
                if (const auto removed = RemoveExactValues(path, *values, {}, false); !removed)
                    return removed;
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemoveProductRegistration(const InstallRegistration& expected,
                                                          const InstallProduct product)
        {
            auto current = ReadProductRegistration(product);
            if (!current)
                return HubStatus::Failure(current.Error());
            if (!current.Value())
                return HubStatus::Success();
            if (current.Value()->ProductId != expected.ProductId ||
                current.Value()->InstallationId != expected.InstallationId ||
                current.Value()->Root.lexically_normal() != expected.Root.lexically_normal() ||
                current.Value()->Version != expected.Version ||
                current.Value()->ManifestFingerprint != expected.ManifestFingerprint ||
                current.Value()->ReceiptSha256 != expected.ReceiptSha256)
            {
                return HubStatus::Failure(RegistrationError(
                    "The product registration changed while the installation transaction was running."));
            }
            const std::array dwords{std::pair{std::wstring(L"NoModify"), DWORD{1}},
                                    std::pair{std::wstring(L"NoRepair"), DWORD{1}}};
            if (const auto removed = RemoveExactValues(UninstallKey(product),
                                                       UninstallRegistrationValues(expected, product), dwords, false);
                !removed)
            {
                return removed;
            }
            if (const auto removed =
                    RemoveExactValues(ProductKey(product), ProductRegistrationValues(expected), {}, false);
                !removed)
            {
                return removed;
            }
            return product == InstallProduct::Hub ? RemoveHubProtocolIfOwned(expected) : HubStatus::Success();
        }

        [[nodiscard]] HubStatus RemovePartialProductRegistration(const InstallRegistration& attempted,
                                                                 const InstallProduct product)
        {
            const std::array dwords{std::pair{std::wstring(L"NoModify"), DWORD{1}},
                                    std::pair{std::wstring(L"NoRepair"), DWORD{1}}};
            if (const auto removed = RemoveExactValues(UninstallKey(product),
                                                       UninstallRegistrationValues(attempted, product), dwords, true);
                !removed)
            {
                return removed;
            }
            if (const auto removed =
                    RemoveExactValues(ProductKey(product), ProductRegistrationValues(attempted), {}, true);
                !removed)
            {
                return removed;
            }
            return product == InstallProduct::Hub ? RemoveHubProtocolIfOwned(attempted) : HubStatus::Success();
        }
#endif
    } // namespace

    KeireHub::InstallRegistrationStore CreateProductRegistrationStore(const KeireHub::InstallProduct product)
    {
#if defined(_WIN32)
        struct RegistrationStoreState final
        {
            KeireHub::InstallProduct Product = KeireHub::InstallProduct::Editor;
            bool BaselineCaptured = false;
            std::optional<KeireHub::InstallRegistration> Baseline;
            std::optional<LegacyRegistrationSnapshot> Legacy;
        };
        auto state = std::make_shared<RegistrationStoreState>();
        state->Product = product;
        return {.Read =
                    [state](const KeireHub::InstallProduct value)
                {
                    if (value != state->Product)
                    {
                        return KeireHub::HubResult<std::optional<KeireHub::InstallRegistration>>::Failure(
                            RegistrationError("The registration store was used for the wrong product."));
                    }
                    auto registration = ReadProductRegistration(value);
                    if (registration && !state->BaselineCaptured)
                    {
                        state->Baseline = registration.Value();
                        state->BaselineCaptured = true;
                    }
                    return registration;
                },
                .Write =
                    [state](const KeireHub::InstallRegistration& registration)
                {
                    if (state->Product == KeireHub::InstallProduct::Hub)
                    {
                        if (const auto protocol = ValidateHubProtocolMutation(registration, state->Baseline); !protocol)
                            return protocol;
                    }
                    const auto written = WriteProductRegistration(registration, state->Product);
                    if (written)
                        return written;
                    const auto restored = state->Baseline ? WriteProductRegistration(*state->Baseline, state->Product)
                                          : state->Legacy
                                              ? RestoreLegacyRegistration(*state->Legacy)
                                              : RemovePartialProductRegistration(registration, state->Product);
                    if (restored)
                        return written;
                    auto error = written.Error();
                    error.TechnicalDetails += error.TechnicalDetails.empty() ? "" : " | ";
                    error.TechnicalDetails += "Registration rollback also failed: " + restored.Error().Message + " " +
                                              restored.Error().TechnicalDetails;
                    return KeireHub::HubStatus::Failure(std::move(error));
                },
                .Remove =
                    [state](const KeireHub::InstallRegistration& registration)
                {
                    if (state->Legacy)
                    {
                        const auto current = ReadProductRegistration(state->Product);
                        if (!current)
                            return KeireHub::HubStatus::Failure(current.Error());
                        if (current.Value() && current.Value()->InstallationId == registration.InstallationId)
                            return RestoreLegacyRegistration(*state->Legacy);
                    }
                    return RemoveProductRegistration(registration, state->Product);
                },
                .ValidateLegacy =
                    [state](const KeireHub::InstallLegacyCandidate& candidate)
                {
                    if (candidate.Product != state->Product)
                    {
                        return KeireHub::HubStatus::Failure(
                            RegistrationError("The legacy validator was used for the wrong product."));
                    }
                    auto validated = ValidateLegacyRegistration(candidate);
                    if (!validated)
                        return KeireHub::HubStatus::Failure(validated.Error());
                    state->Legacy = std::move(validated).Value();
                    return KeireHub::HubStatus::Success();
                }};
#else
        (void)product;
        return {.Read =
                    [](const KeireHub::InstallProduct)
                {
                    return KeireHub::HubResult<std::optional<KeireHub::InstallRegistration>>::Failure(
                        RegistrationError("KeireInstallWorker is supported only on Windows."));
                },
                .Write = [](const KeireHub::InstallRegistration&)
                { return KeireHub::HubStatus::Failure(RegistrationError("Unsupported platform.")); },
                .Remove = [](const KeireHub::InstallRegistration&)
                { return KeireHub::HubStatus::Failure(RegistrationError("Unsupported platform.")); },
                .ValidateLegacy = [](const KeireHub::InstallLegacyCandidate&)
                { return KeireHub::HubStatus::Failure(RegistrationError("Unsupported platform.")); }};
#endif
    }
} // namespace KeireInstallWorker
