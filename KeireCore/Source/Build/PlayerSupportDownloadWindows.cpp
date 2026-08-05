#include "KeireInternal/Build/PlayerSupportCatalog.h"

#if defined(_WIN32)
#include "KeireInternal/FileSystem.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    namespace
    {
        struct HttpHandleCloser final
        {
            void operator()(void* handle) const noexcept
            {
                if (handle)
                    WinHttpCloseHandle(handle);
            }
        };

        using HttpHandle = std::unique_ptr<void, HttpHandleCloser>;

        void ThrowIfCancelled(const PlayerSupportInstallCallbacks& callbacks)
        {
            if (callbacks.Cancelled && callbacks.Cancelled())
                throw std::runtime_error("Build Support download cancelled.");
        }
    } // namespace

    void DownloadHttpsFileNative(const std::string_view url, const std::filesystem::path& destination,
                                 const std::uint64_t maximumBytes, const PlayerSupportInstallCallbacks& callbacks)
    {
        const auto wideUrl = PathFromUtf8(url).wstring();
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS)
            throw std::runtime_error("Build Support download URL is not valid HTTPS.");
        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring target(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength != 0)
            target.append(components.lpszExtraInfo, components.dwExtraInfoLength);

        HttpHandle session(WinHttpOpen(L"KeireBuildSupport/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session)
            throw std::runtime_error("Could not initialize the Windows HTTPS client.");
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
            throw std::runtime_error("Could not configure the Windows HTTPS redirect policy.");
        HttpHandle connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
        HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!connection || !request ||
            !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
            throw std::runtime_error("Build Support HTTPS request failed.");
        DWORD status = 0;
        DWORD statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX) ||
            status != 200)
            throw std::runtime_error("Build Support server returned HTTP " + std::to_string(status) + ".");

        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not create the Build Support download file.");
        std::uint64_t received = 0;
        for (;;)
        {
            ThrowIfCancelled(callbacks);
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available))
                throw std::runtime_error("Build Support HTTPS download failed.");
            if (available == 0)
                break;
            if (received > maximumBytes - available)
                throw std::runtime_error("Build Support download exceeds its size limit.");
            std::vector<std::byte> buffer(std::min<DWORD>(available, 256U * 1024U));
            while (available != 0)
            {
                ThrowIfCancelled(callbacks);
                const auto requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
                DWORD count = 0;
                if (!WinHttpReadData(request.get(), buffer.data(), requested, &count) || count == 0)
                    throw std::runtime_error("Build Support HTTPS response ended unexpectedly.");
                output.write(reinterpret_cast<const char*>(buffer.data()), count);
                if (!output)
                    throw std::runtime_error("Could not write the Build Support download file.");
                received += count;
                available -= count;
                if (callbacks.Progress)
                    callbacks.Progress(0.8F, "Downloading Build Support");
            }
        }
    }
} // namespace Keire::Detail
#endif
