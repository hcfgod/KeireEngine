#include "KeireInstallWorker/RegistryPaths.h"

#include <cstdlib>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#ifndef KEIRE_INSTALL_PRODUCT_IDENTIFIER
#define KEIRE_INSTALL_PRODUCT_IDENTIFIER "Keire"
#endif

namespace KeireInstallWorker::Detail
{
    namespace
    {
#if defined(_WIN32) && defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        [[nodiscard]] std::wstring TestRegistryRoot()
        {
            wchar_t* raw = nullptr;
            std::size_t length = 0;
            if (::_wdupenv_s(&raw, &length, L"KEIRE_INSTALL_WORKER_TEST_REGISTRY_ROOT") != 0 || !raw)
            {
                std::free(raw);
                return {};
            }
            std::wstring root(raw);
            std::free(raw);
            constexpr std::wstring_view requiredPrefix = L"Software\\KeireInstallerTests\\";
            if (!root.starts_with(requiredPrefix) || root.size() == requiredPrefix.size())
                throw std::runtime_error("The test registry root is outside the installer-test namespace.");
            for (const auto character : std::wstring_view(root).substr(requiredPrefix.size()))
            {
                if ((character < L'0' || character > L'9') && (character < L'A' || character > L'Z') &&
                    (character < L'a' || character > L'z') && character != L'-' && character != L'_')
                {
                    throw std::runtime_error("The test registry root contains an unsafe character.");
                }
            }
            return root;
        }
#endif

        [[nodiscard]] std::wstring ProductRoot()
        {
#if defined(_WIN32) && defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
            if (const auto testRoot = TestRegistryRoot(); !testRoot.empty())
                return testRoot + L"\\Product";
#endif
            return L"Software\\" + std::wstring(L"" KEIRE_INSTALL_PRODUCT_IDENTIFIER);
        }
    } // namespace

    std::wstring ProductRegistrationKey(const KeireHub::InstallProduct product)
    {
        return ProductRoot() + (product == KeireHub::InstallProduct::Editor ? L"\\Editor" : L"\\HubInstaller");
    }

    std::wstring UninstallRegistrationKey(const KeireHub::InstallProduct product)
    {
#if defined(_WIN32) && defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        if (const auto testRoot = TestRegistryRoot(); !testRoot.empty())
            return testRoot + (product == KeireHub::InstallProduct::Editor ? L"\\Uninstall\\Editor"
                                                                           : L"\\Uninstall\\Hub");
#endif
        return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
               std::wstring(L"" KEIRE_INSTALL_PRODUCT_IDENTIFIER) +
               (product == KeireHub::InstallProduct::Editor ? L"Editor" : L"Hub");
    }

    std::wstring HubProtocolRegistrationKey()
    {
#if defined(_WIN32) && defined(KEIRE_INSTALL_WORKER_FAULT_INJECTION)
        if (const auto testRoot = TestRegistryRoot(); !testRoot.empty())
            return testRoot + L"\\Classes\\keirehub";
#endif
        return L"Software\\Classes\\keirehub";
    }
} // namespace KeireInstallWorker::Detail
