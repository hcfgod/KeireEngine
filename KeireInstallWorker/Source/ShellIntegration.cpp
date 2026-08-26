#include "KeireInstallWorker/ShellIntegration.h"
#include "KeireInstallWorker/RegistryPaths.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/InstallMutationFileSystem.h>
#include <KeireHubRuntimeInternal/InstallTransactionInternal.h>
#include <KeireHubRuntimeInternal/InstallTransactionLocatorInternal.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <ShObjIdl.h>
#include <ShlObj.h>
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
        using KeireHub::InstallProduct;
        using KeireHub::InstallRegistration;
        using KeireHub::Detail::Json;

        constexpr auto PendingValueName = L"PendingShellIntegrationReceipt";
        constexpr auto CommittedValueName = L"ShellIntegrationReceipt";
        constexpr std::size_t MaximumReceiptBytes = std::size_t{1024U} * 1024U;

        struct ShellEntry final
        {
            std::string Kind;
            std::filesystem::path Path;
            std::filesystem::path Target;
            std::wstring Arguments;
            std::filesystem::path Icon;
            std::wstring Description;
            std::filesystem::path TemporaryRoot;
            std::string Sha256;
            bool Published = false;
            bool CreatedByPending = false;
        };

        struct ShellReceipt final
        {
            InstallProduct Product = InstallProduct::Editor;
            std::string ProductId;
            std::string InstallationId;
            std::string ReceiptSha256;
            std::filesystem::path Root;
            std::vector<ShellEntry> Entries;
            std::optional<Json> Previous;
        };

        [[nodiscard]] HubError ShellError(const HubErrorCode code, std::string message,
                                          const std::filesystem::path& path = {}, std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = KeireHub::Detail::PathToUtf8(path),
                    .TechnicalDetails = std::move(details)};
        }

#if defined(_WIN32)
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
            RegistryKey& operator=(RegistryKey&&) = delete;
            [[nodiscard]] HKEY Get() const noexcept { return m_Value; }

          private:
            HKEY m_Value = nullptr;
        };

        class ComApartment final
        {
          public:
            ComApartment()
            {
                const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(result) && result != RPC_E_CHANGED_MODE)
                    throw std::runtime_error("COM could not be initialized for shell integration.");
                m_Owns = result == S_OK || result == S_FALSE;
            }
            ~ComApartment()
            {
                if (m_Owns)
                    CoUninitialize();
            }

          private:
            bool m_Owns = false;
        };

#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        void InterruptAfterShellStage(const std::string_view stage, const std::string_view kind = {})
        {
            char* raw = nullptr;
            std::size_t length = 0;
            std::string configured;
            if (::_dupenv_s(&raw, &length, "KEIRE_INSTALL_WORKER_INTERRUPT_AFTER") == 0 && raw)
                configured.assign(raw);
            std::free(raw);
            const auto scoped = kind.empty() ? std::string{} : std::string(stage) + ":" + std::string(kind);
            if (configured != stage && (scoped.empty() || configured != scoped))
                return;
            (void)RegFlushKey(HKEY_CURRENT_USER);
            (void)TerminateProcess(GetCurrentProcess(), 86);
        }
#endif

        template <typename Type> class ComPointer final
        {
          public:
            ~ComPointer()
            {
                if (m_Value)
                    m_Value->Release();
            }
            ComPointer(const ComPointer&) = delete;
            ComPointer& operator=(const ComPointer&) = delete;
            ComPointer() = default;
            [[nodiscard]] Type** Put() noexcept { return &m_Value; }
            [[nodiscard]] Type* Get() const noexcept { return m_Value; }

          private:
            Type* m_Value = nullptr;
        };

        [[nodiscard]] std::wstring ProductKey(const InstallProduct product)
        {
            return Detail::ProductRegistrationKey(product);
        }

        [[nodiscard]] std::wstring ToWide(const std::string_view value)
        {
            if (value.empty())
                return {};
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0);
            if (count <= 0)
                throw std::runtime_error("A shell receipt contains invalid UTF-8.");
            std::wstring result(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), count) != count)
            {
                throw std::runtime_error("A shell receipt could not be converted to UTF-16.");
            }
            return result;
        }

        [[nodiscard]] std::string ToUtf8(const std::wstring_view value)
        {
            if (value.empty())
                return {};
            const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (count <= 0)
                throw std::runtime_error("A shell receipt contains invalid UTF-16.");
            std::string result(static_cast<std::size_t>(count), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), count, nullptr, nullptr) != count)
            {
                throw std::runtime_error("A shell receipt could not be converted to UTF-8.");
            }
            return result;
        }

        [[nodiscard]] HubResult<RegistryKey> OpenProductKey(const InstallProduct product, const bool create)
        {
            HKEY raw = nullptr;
            DWORD disposition = 0;
            const auto result = create ? RegCreateKeyExW(HKEY_CURRENT_USER, ProductKey(product).c_str(), 0, nullptr, 0,
                                                         KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw, &disposition)
                                       : RegOpenKeyExW(HKEY_CURRENT_USER, ProductKey(product).c_str(), 0,
                                                       KEY_QUERY_VALUE | KEY_SET_VALUE, &raw);
            if (result != ERROR_SUCCESS)
            {
                return HubResult<RegistryKey>::Failure(ShellError(
                    result == ERROR_FILE_NOT_FOUND ? HubErrorCode::NotFound : HubErrorCode::IoRead,
                    "The shell-integration receipt registry could not be opened.", {}, std::to_string(result)));
            }
            return HubResult<RegistryKey>::Success(RegistryKey(raw));
        }

        [[nodiscard]] HubResult<std::optional<std::string>> ReadRegistryDocument(const InstallProduct product,
                                                                                 const wchar_t* name)
        {
            auto key = OpenProductKey(product, false);
            if (!key)
            {
                return key.Error().Code == HubErrorCode::NotFound
                           ? HubResult<std::optional<std::string>>::Success(std::nullopt)
                           : HubResult<std::optional<std::string>>::Failure(key.Error());
            }
            DWORD type = 0;
            DWORD bytes = 0;
            auto result = RegQueryValueExW(key.Value().Get(), name, nullptr, &type, nullptr, &bytes);
            if (result == ERROR_FILE_NOT_FOUND)
                return HubResult<std::optional<std::string>>::Success(std::nullopt);
            if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
                bytes > MaximumReceiptBytes * sizeof(wchar_t))
            {
                return HubResult<std::optional<std::string>>::Failure(
                    ShellError(HubErrorCode::InvalidData, "The shell-integration receipt registry value is invalid.",
                               {}, std::to_string(result)));
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            result = RegQueryValueExW(key.Value().Get(), name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()),
                                      &bytes);
            if (result != ERROR_SUCCESS)
            {
                return HubResult<std::optional<std::string>>::Failure(
                    ShellError(HubErrorCode::IoRead, "The shell-integration receipt could not be read.", {},
                               std::to_string(result)));
            }
            while (!value.empty() && value.back() == L'\0')
                value.pop_back();
            return HubResult<std::optional<std::string>>::Success(ToUtf8(value));
        }

        [[nodiscard]] HubStatus WriteRegistryDocument(const InstallProduct product, const wchar_t* name,
                                                      const std::string_view document)
        {
            if (document.size() > MaximumReceiptBytes)
            {
                return HubStatus::Failure(
                    ShellError(HubErrorCode::InvalidData, "The shell-integration receipt exceeds its size limit."));
            }
            auto key = OpenProductKey(product, true);
            if (!key)
                return HubStatus::Failure(key.Error());
            const auto wide = ToWide(document);
            const auto bytes = static_cast<DWORD>((wide.size() + 1U) * sizeof(wchar_t));
            const auto result =
                RegSetValueExW(key.Value().Get(), name, 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()), bytes);
            if (result != ERROR_SUCCESS)
            {
                return HubStatus::Failure(ShellError(HubErrorCode::IoWrite,
                                                     "The shell-integration receipt could not be written.", {},
                                                     std::to_string(result)));
            }
            const auto flushed = RegFlushKey(key.Value().Get());
            return flushed == ERROR_SUCCESS
                       ? HubStatus::Success()
                       : HubStatus::Failure(ShellError(HubErrorCode::IoWrite,
                                                       "The shell-integration receipt could not be made durable.", {},
                                                       std::to_string(flushed)));
        }

        [[nodiscard]] HubStatus DeleteRegistryDocumentIfExact(const InstallProduct product, const wchar_t* name,
                                                              const std::string_view expected)
        {
            auto current = ReadRegistryDocument(product, name);
            if (!current)
                return HubStatus::Failure(current.Error());
            if (!current.Value())
                return HubStatus::Success();
            if (*current.Value() != expected)
            {
                return HubStatus::Failure(ShellError(HubErrorCode::DestinationConflict,
                                                     "The shell-integration receipt changed before removal."));
            }
            auto key = OpenProductKey(product, false);
            if (!key)
                return HubStatus::Failure(key.Error());
            const auto result = RegDeleteValueW(key.Value().Get(), name);
            if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
            {
                return HubStatus::Failure(ShellError(HubErrorCode::IoWrite,
                                                     "The shell-integration receipt could not be removed.", {},
                                                     std::to_string(result)));
            }
            const auto flushed = RegFlushKey(key.Value().Get());
            return flushed == ERROR_SUCCESS
                       ? HubStatus::Success()
                       : HubStatus::Failure(ShellError(
                             HubErrorCode::IoWrite, "The shell-integration receipt removal could not be made durable.",
                             {}, std::to_string(flushed)));
        }

        [[nodiscard]] std::filesystem::path KnownFolder(const KNOWNFOLDERID& identifier)
        {
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            wchar_t* raw = nullptr;
            std::size_t length = 0;
            if (::_wdupenv_s(&raw, &length, L"KEIRE_INSTALL_WORKER_TEST_SHELL_ROOT") == 0 && raw)
            {
                std::filesystem::path root(raw);
                std::free(raw);
                root = root.lexically_normal();
                constexpr std::wstring_view requiredPrefix = L"keire-install-worker-shell-tests-";
                if (!root.is_absolute() || root.filename().native().find(requiredPrefix) != 0U)
                    throw std::runtime_error("The test shell root is outside the installer-test namespace.");
                if (identifier == FOLDERID_Programs)
                    return root / "Programs";
                if (identifier == FOLDERID_Desktop)
                    return root / "Desktop";
                throw std::runtime_error("The requested test known folder is unsupported.");
            }
            std::free(raw);
#endif
            PWSTR value = nullptr;
            const auto result = SHGetKnownFolderPath(identifier, KF_FLAG_DEFAULT, nullptr, &value);
            if (FAILED(result) || !value)
                throw std::runtime_error("A required Windows known folder is unavailable.");
            std::filesystem::path path(value);
            CoTaskMemFree(value);
            return path;
        }

        [[nodiscard]] std::wstring ProductSuffix(const InstallProduct product)
        {
            return product == InstallProduct::Editor ? L" Editor" : L" Hub";
        }

        [[nodiscard]] std::filesystem::path ProductExecutable(const InstallProduct product,
                                                              const std::filesystem::path& root)
        {
            const auto target =
                product == InstallProduct::Editor ? KEIRE_INSTALL_EDITOR_TARGET : KEIRE_INSTALL_HUB_TARGET;
            return root / "bin" / (std::string(target) + ".exe");
        }

        [[nodiscard]] ShellEntry ExpectedEntry(const InstallProduct product, const std::filesystem::path& root,
                                               const std::string_view kind)
        {
            const auto displayName = std::wstring(L"" KEIRE_INSTALL_PRODUCT_DISPLAY_NAME) + ProductSuffix(product);
            ShellEntry entry{.Kind = std::string(kind)};
            if (kind == "startMenuLaunch" || kind == "startMenuUninstall")
            {
                const auto directory = KnownFolder(FOLDERID_Programs) / displayName;
                entry.Path = directory / (kind == "startMenuLaunch" ? displayName + L".lnk"
                                                                    : L"Uninstall " + displayName + L".lnk");
            }
            else
            {
                entry.Path = KnownFolder(FOLDERID_Desktop) / (displayName + L".lnk");
            }
            entry.Target = kind == "startMenuUninstall" ? root / "Uninstall.exe" : ProductExecutable(product, root);
            entry.Icon = kind == "startMenuUninstall" ? root / "Uninstall.exe" : ProductExecutable(product, root);
            entry.Description = (kind == "startMenuUninstall" ? L"Uninstall " : L"Open ") + displayName;
            return entry;
        }

        [[nodiscard]] Json EncodeEntry(const ShellEntry& entry)
        {
            return {{"kind", entry.Kind},
                    {"path", KeireHub::Detail::PathToUtf8(entry.Path)},
                    {"target", KeireHub::Detail::PathToUtf8(entry.Target)},
                    {"arguments", ToUtf8(entry.Arguments)},
                    {"icon", KeireHub::Detail::PathToUtf8(entry.Icon)},
                    {"description", ToUtf8(entry.Description)},
                    {"temporaryRoot", KeireHub::Detail::PathToUtf8(entry.TemporaryRoot)},
                    {"sha256", entry.Sha256},
                    {"published", entry.Published},
                    {"createdByPending", entry.CreatedByPending}};
        }

        [[nodiscard]] Json EncodeReceipt(const ShellReceipt& receipt, const bool includePrevious)
        {
            Json entries = Json::array();
            for (const auto& entry : receipt.Entries)
                entries.push_back(EncodeEntry(entry));
            Json document{{"schemaVersion", 1},
                          {"product", KeireHub::ToString(receipt.Product)},
                          {"productId", receipt.ProductId},
                          {"installationId", receipt.InstallationId},
                          {"receiptSha256", receipt.ReceiptSha256},
                          {"root", KeireHub::Detail::PathToUtf8(receipt.Root)},
                          {"entries", std::move(entries)}};
            if (includePrevious && receipt.Previous)
                document["previous"] = *receipt.Previous;
            return document;
        }

        [[nodiscard]] HubResult<ShellReceipt> DecodeReceipt(const std::string_view bytes,
                                                            const InstallProduct expectedProduct)
        {
            try
            {
                const auto document = Json::parse(bytes, nullptr, true, true);
                if (!document.is_object() || document.at("schemaVersion").get<int>() != 1 ||
                    document.at("product").get<std::string>() != KeireHub::ToString(expectedProduct) ||
                    !document.at("entries").is_array())
                {
                    throw std::runtime_error("The shell receipt identity is invalid.");
                }
                ShellReceipt receipt{.Product = expectedProduct,
                                     .ProductId = document.at("productId").get<std::string>(),
                                     .InstallationId = document.at("installationId").get<std::string>(),
                                     .ReceiptSha256 = document.at("receiptSha256").get<std::string>(),
                                     .Root = KeireHub::Detail::PathFromUtf8(document.at("root").get<std::string>())};
                receipt.Root.make_preferred();
                if (!receipt.Root.is_absolute() || !KeireHub::Detail::IsSha256(receipt.ReceiptSha256) ||
                    document.at("entries").size() > 3U)
                {
                    throw std::runtime_error("The shell receipt binding is invalid.");
                }
                std::set<std::string, std::less<>> kinds;
                std::set<std::string, std::less<>> paths;
                for (const auto& encoded : document.at("entries"))
                {
                    ShellEntry entry{.Kind = encoded.at("kind").get<std::string>(),
                                     .Path = KeireHub::Detail::PathFromUtf8(encoded.at("path").get<std::string>()),
                                     .Target = KeireHub::Detail::PathFromUtf8(encoded.at("target").get<std::string>()),
                                     .Arguments = ToWide(encoded.at("arguments").get<std::string>()),
                                     .Icon = KeireHub::Detail::PathFromUtf8(encoded.at("icon").get<std::string>()),
                                     .Description = ToWide(encoded.at("description").get<std::string>()),
                                     .TemporaryRoot =
                                         KeireHub::Detail::PathFromUtf8(encoded.at("temporaryRoot").get<std::string>()),
                                     .Sha256 = encoded.at("sha256").get<std::string>(),
                                     .Published = encoded.at("published").get<bool>(),
                                     .CreatedByPending = encoded.at("createdByPending").get<bool>()};
                    entry.Path.make_preferred();
                    entry.Target.make_preferred();
                    entry.Icon.make_preferred();
                    entry.TemporaryRoot.make_preferred();
                    auto pathKey = KeireHub::Detail::PathToUtf8(entry.Path.lexically_normal());
                    std::ranges::transform(pathKey, pathKey.begin(), [](const unsigned char value)
                                           { return static_cast<char>(std::tolower(value)); });
                    if ((entry.Kind != "startMenuLaunch" && entry.Kind != "startMenuUninstall" &&
                         entry.Kind != "desktop") ||
                        !kinds.insert(entry.Kind).second || !paths.insert(std::move(pathKey)).second)
                    {
                        throw std::runtime_error("A shell receipt kind is unknown or duplicated.");
                    }
                    const auto expected = ExpectedEntry(expectedProduct, receipt.Root, entry.Kind);
                    if (!entry.Path.is_absolute() ||
                        entry.Path.lexically_normal() != expected.Path.lexically_normal() ||
                        entry.Target.lexically_normal() != expected.Target.lexically_normal() ||
                        entry.Icon.lexically_normal() != expected.Icon.lexically_normal() ||
                        entry.Arguments != expected.Arguments || entry.Description != expected.Description ||
                        (!entry.TemporaryRoot.empty() &&
                         (entry.TemporaryRoot.parent_path().lexically_normal() !=
                              entry.Path.parent_path().lexically_normal() ||
                          entry.TemporaryRoot.filename().native().find(L".__keire-shell-") != 0U)) ||
                        (!entry.Sha256.empty() && !KeireHub::Detail::IsSha256(entry.Sha256)) ||
                        (entry.Published && entry.Sha256.empty()) ||
                        (!entry.TemporaryRoot.empty() && !entry.CreatedByPending))
                    {
                        throw std::runtime_error("A shell receipt entry is outside its product authority.");
                    }
                    receipt.Entries.push_back(std::move(entry));
                }
                if (document.contains("previous"))
                {
                    const auto& previousDocument = document.at("previous");
                    if (!previousDocument.is_object() || previousDocument.contains("previous"))
                        throw std::runtime_error("A nested previous shell receipt is invalid.");
                    auto previous = DecodeReceipt(previousDocument.dump(), expectedProduct);
                    if (!previous || previous.Value().Root.lexically_normal() != receipt.Root.lexically_normal() ||
                        previous.Value().ProductId != receipt.ProductId)
                    {
                        throw std::runtime_error("The previous shell receipt binding is invalid.");
                    }
                    receipt.Previous = EncodeReceipt(previous.Value(), false);
                }
                return HubResult<ShellReceipt>::Success(std::move(receipt));
            }
            catch (const std::exception& error)
            {
                return HubResult<ShellReceipt>::Failure(ShellError(
                    HubErrorCode::InvalidData, "The shell-integration receipt is malformed.", {}, error.what()));
            }
        }

        [[nodiscard]] bool Matches(const ShellReceipt& receipt, const InstallRegistration& registration)
        {
            return receipt.ProductId == registration.ProductId &&
                   receipt.InstallationId == registration.InstallationId &&
                   receipt.ReceiptSha256 == registration.ReceiptSha256 &&
                   receipt.Root.lexically_normal() == registration.Root.lexically_normal();
        }

        [[nodiscard]] HubStatus SaveLink(const ShellEntry& entry, const std::filesystem::path& path)
        {
            ComApartment apartment;
            ComPointer<IShellLinkW> link;
            auto result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                           reinterpret_cast<void**>(link.Put()));
            if (FAILED(result))
                return HubStatus::Failure(ShellError(HubErrorCode::IoWrite, "A shortcut could not be created."));
            if (FAILED(link.Get()->SetPath(entry.Target.c_str())) ||
                FAILED(link.Get()->SetArguments(entry.Arguments.c_str())) ||
                FAILED(link.Get()->SetDescription(entry.Description.c_str())) ||
                FAILED(link.Get()->SetIconLocation(entry.Icon.c_str(), 0)) ||
                FAILED(link.Get()->SetShowCmd(SW_SHOWNORMAL)))
            {
                return HubStatus::Failure(
                    ShellError(HubErrorCode::IoWrite, "A shortcut identity could not be configured.", entry.Path));
            }
            ComPointer<IPersistFile> persistence;
            result = link.Get()->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persistence.Put()));
            if (FAILED(result) || FAILED(persistence.Get()->Save(path.c_str(), TRUE)))
            {
                return HubStatus::Failure(
                    ShellError(HubErrorCode::IoWrite, "A staged shortcut could not be saved.", path));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool LinkIdentityMatches(const ShellEntry& entry, const std::filesystem::path& path)
        {
            try
            {
                ComApartment apartment;
                ComPointer<IShellLinkW> link;
                if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                            reinterpret_cast<void**>(link.Put()))))
                    return false;
                ComPointer<IPersistFile> persistence;
                if (FAILED(link.Get()->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persistence.Put()))) ||
                    FAILED(persistence.Get()->Load(path.c_str(), STGM_READ)))
                {
                    return false;
                }
                std::array<wchar_t, 32768> target{};
                std::array<wchar_t, 32768> arguments{};
                std::array<wchar_t, 32768> description{};
                std::array<wchar_t, 32768> icon{};
                int iconIndex = -1;
                if (FAILED(
                        link.Get()->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH)) ||
                    FAILED(link.Get()->GetArguments(arguments.data(), static_cast<int>(arguments.size()))) ||
                    FAILED(link.Get()->GetDescription(description.data(), static_cast<int>(description.size()))) ||
                    FAILED(link.Get()->GetIconLocation(icon.data(), static_cast<int>(icon.size()), &iconIndex)))
                {
                    return false;
                }
                return std::filesystem::path(target.data()).lexically_normal() == entry.Target.lexically_normal() &&
                       arguments.data() == entry.Arguments && description.data() == entry.Description &&
                       std::filesystem::path(icon.data()).lexically_normal() == entry.Icon.lexically_normal() &&
                       iconIndex == 0;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] HubResult<bool> ExactLinkExists(const ShellEntry& entry)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(entry.Path, error);
            if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
                return HubResult<bool>::Success(false);
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) ||
                !LinkIdentityMatches(entry, entry.Path))
            {
                return HubResult<bool>::Success(false);
            }
            auto digest = KeireHub::Detail::HashInstallFile(entry.Path);
            if (!digest)
                return HubResult<bool>::Failure(digest.Error());
            return HubResult<bool>::Success(!entry.Sha256.empty() && digest.Value() == entry.Sha256);
        }

        [[nodiscard]] HubStatus RemoveExactEntry(const ShellEntry& entry)
        {
            auto exact = ExactLinkExists(entry);
            if (!exact)
                return HubStatus::Failure(exact.Error());
            if (exact.Value())
            {
                KeireHub::Detail::InstallMutationFileSystem parent(entry.Path.parent_path());
                auto described = parent.Describe(entry.Path.filename());
                if (!described)
                    return HubStatus::Failure(described.Error());
                if (described.Value().Sha256 != entry.Sha256)
                {
                    return HubStatus::Failure(ShellError(HubErrorCode::DestinationConflict,
                                                         "A shortcut changed before exact removal.", entry.Path));
                }
                if (const auto removed = parent.RemoveVerified(described.Value()); !removed)
                    return removed;
            }
            if (!entry.TemporaryRoot.empty())
            {
                std::error_code error;
                if (std::filesystem::exists(entry.TemporaryRoot, error) && !error)
                {
                    KeireHub::Detail::InstallMutationFileSystem temporary(entry.TemporaryRoot);
                    auto staged = temporary.Describe(entry.Path.filename(), true);
                    if (!staged)
                        return HubStatus::Failure(staged.Error());
                    if (!staged.Value().Sha256.empty() &&
                        (entry.Sha256.empty() || staged.Value().Sha256 == entry.Sha256) &&
                        LinkIdentityMatches(entry, entry.TemporaryRoot / entry.Path.filename()))
                    {
                        if (const auto removed = temporary.RemoveVerified(staged.Value()); !removed)
                            return removed;
                    }
                    (void)temporary.RemoveRootIfEmpty();
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus WriteReceipt(const InstallProduct product, const wchar_t* name,
                                             const ShellReceipt& receipt, const bool previous)
        {
            return WriteRegistryDocument(product, name, EncodeReceipt(receipt, previous).dump());
        }

        [[nodiscard]] HubResult<std::optional<ShellReceipt>> ReadReceipt(const InstallProduct product,
                                                                         const wchar_t* name)
        {
            auto bytes = ReadRegistryDocument(product, name);
            if (!bytes)
                return HubResult<std::optional<ShellReceipt>>::Failure(bytes.Error());
            if (!bytes.Value())
                return HubResult<std::optional<ShellReceipt>>::Success(std::nullopt);
            auto receipt = DecodeReceipt(*bytes.Value(), product);
            return receipt ? HubResult<std::optional<ShellReceipt>>::Success(std::move(receipt).Value())
                           : HubResult<std::optional<ShellReceipt>>::Failure(receipt.Error());
        }

        [[nodiscard]] HubStatus CreateEntry(const InstallProduct product, ShellReceipt& receipt, ShellEntry& entry)
        {
            const auto token = KeireHub::Detail::SecureInstallRandomId();
            entry.TemporaryRoot = entry.Path.parent_path() / (L".__keire-shell-" + ToWide(token));
            entry.CreatedByPending = true;
            if (const auto written = WriteReceipt(product, PendingValueName, receipt, true); !written)
                return written;
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            InterruptAfterShellStage("shellIntent", entry.Kind);
#endif

            KeireHub::Detail::InstallMutationAuthority mutation;
            auto destination = mutation.Pin(entry.Path.parent_path(), true);
            if (!destination)
                return HubStatus::Failure(destination.Error());
            auto temporary = mutation.Pin(entry.TemporaryRoot, true, true);
            if (!temporary)
                return HubStatus::Failure(temporary.Error());
            const auto temporaryPath = entry.TemporaryRoot / entry.Path.filename();
            if (const auto saved = SaveLink(entry, temporaryPath); !saved)
                return saved;
            auto staged = temporary.Value()->Describe(entry.Path.filename());
            if (!staged)
                return HubStatus::Failure(staged.Error());
            entry.Sha256 = staged.Value().Sha256;
            if (const auto written = WriteReceipt(product, PendingValueName, receipt, true); !written)
                return written;
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            InterruptAfterShellStage("shellStaged", entry.Kind);
#endif
            if (const auto moved = temporary.Value()->RenameVerifiedTo(staged.Value(), *destination.Value(), false,
                                                                       entry.Path.filename());
                !moved)
            {
                return moved;
            }
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            InterruptAfterShellStage("shellPublished", entry.Kind);
#endif
            entry.Published = true;
            if (const auto written = WriteReceipt(product, PendingValueName, receipt, true); !written)
                return written;
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            InterruptAfterShellStage("shellRecorded", entry.Kind);
#endif
            const auto removed = temporary.Value()->RemoveRootIfEmpty();
            temporary.Value().reset();
            mutation.Unpin(entry.TemporaryRoot);
            return removed;
        }

        [[nodiscard]] HubStatus RemoveReceiptEntries(const ShellReceipt& receipt, const bool pendingOnly)
        {
            for (const auto& entry : receipt.Entries)
            {
                if (pendingOnly && !entry.CreatedByPending)
                    continue;
                if (const auto removed = RemoveExactEntry(entry); !removed)
                    return removed;
            }
            const auto startMenu = std::ranges::find_if(receipt.Entries, [](const auto& entry)
                                                        { return entry.Kind.starts_with("startMenu"); });
            if (startMenu != receipt.Entries.end())
            {
                // The retained no-follow handle can remove only this exact directory and only while it is empty.
                // A replaced, linked, or non-empty directory is deliberately preserved.
                KeireHub::Detail::InstallMutationAuthority mutation;
                if (auto directory = mutation.Pin(startMenu->Path.parent_path()); directory)
                {
                    (void)directory.Value()->RemoveRootIfEmpty();
                    directory.Value().reset();
                    mutation.Unpin(startMenu->Path.parent_path());
                }
            }
            return HubStatus::Success();
        }
#endif
    } // namespace

    HubStatus PrepareShellIntegrations(const InstallProduct product, const InstallRegistration& registration,
                                       const std::optional<InstallRegistration>& previousRegistration,
                                       const bool startMenu, const bool desktop)
    {
#if defined(_WIN32)
        try
        {
            auto prior = ReadReceipt(product, CommittedValueName);
            if (!prior)
                return HubStatus::Failure(prior.Error());
            ShellReceipt receipt{.Product = product,
                                 .ProductId = registration.ProductId,
                                 .InstallationId = registration.InstallationId,
                                 .ReceiptSha256 = registration.ReceiptSha256,
                                 .Root = registration.Root};
            if (prior.Value())
            {
                if (!previousRegistration || !Matches(*prior.Value(), *previousRegistration) ||
                    previousRegistration->ProductId != registration.ProductId ||
                    previousRegistration->Root.lexically_normal() != registration.Root.lexically_normal())
                {
                    return HubStatus::Failure(
                        ShellError(HubErrorCode::DestinationConflict,
                                   "The previous shell receipt is not part of the pending installation lineage."));
                }
                receipt.Previous = EncodeReceipt(*prior.Value(), false);
                for (auto entry : prior.Value()->Entries)
                {
                    auto exact = ExactLinkExists(entry);
                    if (!exact)
                        return HubStatus::Failure(exact.Error());
                    if (exact.Value())
                    {
                        // A committed receipt owns only the published link. Staging roots and pending ownership are
                        // transaction-local state and must never be carried into the next transaction.
                        entry.TemporaryRoot.clear();
                        entry.CreatedByPending = false;
                        receipt.Entries.push_back(std::move(entry));
                    }
                }
            }
            std::vector<std::string> requested;
            if (startMenu)
            {
                requested.emplace_back("startMenuLaunch");
                requested.emplace_back("startMenuUninstall");
            }
            if (desktop)
                requested.emplace_back("desktop");
            for (const auto& kind : requested)
            {
                if (std::ranges::any_of(receipt.Entries, [&kind](const auto& entry) { return entry.Kind == kind; }))
                {
                    continue;
                }
                auto entry = ExpectedEntry(product, registration.Root, kind);
                std::error_code error;
                if (std::filesystem::exists(entry.Path, error) || error)
                    continue;
                receipt.Entries.push_back(std::move(entry));
            }
            if (const auto written = WriteReceipt(product, PendingValueName, receipt, true); !written)
                return written;
            for (auto& entry : receipt.Entries)
            {
                if (!entry.Sha256.empty())
                    continue;
                if (const auto created = CreateEntry(product, receipt, entry); !created)
                    return created;
            }
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(
                ShellError(HubErrorCode::IoWrite, "Shell integrations could not be prepared.", {}, error.what()));
        }
#else
        (void)product;
        (void)registration;
        (void)startMenu;
        (void)desktop;
        return HubStatus::Failure(
            ShellError(HubErrorCode::InvalidArgument, "Shell integrations are supported only on Windows."));
#endif
    }

    HubStatus CommitShellIntegrations(const InstallProduct product, const InstallRegistration& registration)
    {
#if defined(_WIN32)
        auto pendingBytes = ReadRegistryDocument(product, PendingValueName);
        if (!pendingBytes)
            return HubStatus::Failure(pendingBytes.Error());
        if (!pendingBytes.Value())
            return HubStatus::Success();
        auto pending = DecodeReceipt(*pendingBytes.Value(), product);
        if (!pending)
            return HubStatus::Failure(pending.Error());
        if (!Matches(pending.Value(), registration))
        {
            return HubStatus::Failure(ShellError(HubErrorCode::InvalidTransition,
                                                 "Pending shell integrations do not match the installation."));
        }
        auto committedReceipt = pending.Value();
        for (auto& entry : committedReceipt.Entries)
        {
            entry.TemporaryRoot.clear();
            entry.CreatedByPending = false;
        }
        const auto committed = EncodeReceipt(committedReceipt, false).dump();
        if (const auto written = WriteRegistryDocument(product, CommittedValueName, committed); !written)
            return written;
#if defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        InterruptAfterShellStage("shellCommitted");
#endif
        return DeleteRegistryDocumentIfExact(product, PendingValueName, *pendingBytes.Value());
#else
        (void)product;
        (void)registration;
        return HubStatus::Success();
#endif
    }

    HubStatus ReconcileShellIntegrations(const InstallProduct product,
                                         const std::optional<InstallRegistration>& activeRegistration)
    {
#if defined(_WIN32)
        auto pendingBytes = ReadRegistryDocument(product, PendingValueName);
        if (!pendingBytes)
            return HubStatus::Failure(pendingBytes.Error());
        if (pendingBytes.Value())
        {
            auto pending = DecodeReceipt(*pendingBytes.Value(), product);
            if (!pending)
                return HubStatus::Failure(pending.Error());
            auto committed = ReadReceipt(product, CommittedValueName);
            if (!committed)
                return HubStatus::Failure(committed.Error());
            const bool completed = activeRegistration && Matches(pending.Value(), *activeRegistration) &&
                                   committed.Value() && Matches(*committed.Value(), *activeRegistration);
            if (!completed)
            {
                if (const auto removed = RemoveReceiptEntries(pending.Value(), true); !removed)
                    return removed;
                if (pending.Value().Previous)
                {
                    auto previous = DecodeReceipt(pending.Value().Previous->dump(), product);
                    if (!previous || !activeRegistration || !Matches(previous.Value(), *activeRegistration))
                    {
                        return HubStatus::Failure(
                            ShellError(HubErrorCode::DestinationConflict,
                                       "The previous shell receipt does not match the recovered installation."));
                    }
                    if (const auto restored =
                            WriteRegistryDocument(product, CommittedValueName, pending.Value().Previous->dump());
                        !restored)
                    {
                        return restored;
                    }
                }
                else if (auto committedBytes = ReadRegistryDocument(product, CommittedValueName);
                         committedBytes && committedBytes.Value())
                {
                    if (const auto deleted =
                            DeleteRegistryDocumentIfExact(product, CommittedValueName, *committedBytes.Value());
                        !deleted)
                    {
                        return deleted;
                    }
                }
            }
            if (const auto deleted = DeleteRegistryDocumentIfExact(product, PendingValueName, *pendingBytes.Value());
                !deleted)
            {
                return deleted;
            }
        }

        auto committedBytes = ReadRegistryDocument(product, CommittedValueName);
        if (!committedBytes)
            return HubStatus::Failure(committedBytes.Error());
        if (!committedBytes.Value())
            return HubStatus::Success();
        auto committed = DecodeReceipt(*committedBytes.Value(), product);
        if (!committed)
            return HubStatus::Failure(committed.Error());
        if (activeRegistration && Matches(committed.Value(), *activeRegistration))
            return HubStatus::Success();
        if (const auto removed = RemoveReceiptEntries(committed.Value(), false); !removed)
            return removed;
        return DeleteRegistryDocumentIfExact(product, CommittedValueName, *committedBytes.Value());
#else
        (void)product;
        (void)activeRegistration;
        return HubStatus::Success();
#endif
    }

    HubStatus RemoveShellIntegrations(const InstallProduct product, const InstallRegistration& registration)
    {
#if defined(_WIN32)
        auto committedBytes = ReadRegistryDocument(product, CommittedValueName);
        if (!committedBytes)
            return HubStatus::Failure(committedBytes.Error());
        if (!committedBytes.Value())
            return HubStatus::Success();
        auto receipt = DecodeReceipt(*committedBytes.Value(), product);
        if (!receipt)
            return HubStatus::Failure(receipt.Error());
        if (!Matches(receipt.Value(), registration))
        {
            return HubStatus::Failure(ShellError(HubErrorCode::DestinationConflict,
                                                 "The shell-integration receipt changed before uninstall."));
        }
        if (const auto removed = RemoveReceiptEntries(receipt.Value(), false); !removed)
            return removed;
        return DeleteRegistryDocumentIfExact(product, CommittedValueName, *committedBytes.Value());
#else
        (void)product;
        (void)registration;
        return HubStatus::Success();
#endif
    }
} // namespace KeireInstallWorker
