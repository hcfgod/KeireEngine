#include "KeireInternal/Build/PlayerSupportCatalog.h"

#if defined(__linux__)
#include <curl/curl.h>

#include <fstream>
#include <mutex>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        struct DownloadContext final
        {
            std::ofstream Output;
            const PlayerSupportInstallCallbacks* Callbacks = nullptr;
            std::uint64_t MaximumBytes = 0;
            std::uint64_t Received = 0;
            bool Failed = false;
        };

        std::size_t WriteDownload(void* data, const std::size_t size, const std::size_t count, void* user)
        {
            auto& context = *static_cast<DownloadContext*>(user);
            const auto bytes = size * count;
            if (bytes > context.MaximumBytes - context.Received)
            {
                context.Failed = true;
                return 0;
            }
            context.Output.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
            if (!context.Output)
            {
                context.Failed = true;
                return 0;
            }
            context.Received += bytes;
            return bytes;
        }

        int TransferProgress(void* user, const curl_off_t total, const curl_off_t current, curl_off_t, curl_off_t)
        {
            auto& context = *static_cast<DownloadContext*>(user);
            if (context.Callbacks->Cancelled && context.Callbacks->Cancelled())
                return 1;
            if (context.Callbacks->Progress)
                context.Callbacks->Progress(total > 0 ? 0.8F * static_cast<float>(current) / static_cast<float>(total)
                                                      : 0.0F,
                                            "Downloading Build Support");
            return 0;
        }
    } // namespace

    void DownloadHttpsFileNative(const std::string_view url, const std::filesystem::path& destination,
                                 const std::uint64_t maximumBytes, const PlayerSupportInstallCallbacks& callbacks)
    {
        static std::once_flag initialized;
        std::call_once(initialized,
                       []
                       {
                           if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                               throw std::runtime_error("Could not initialize libcurl.");
                       });
        DownloadContext context{.Output = std::ofstream(destination, std::ios::binary | std::ios::trunc),
                                .Callbacks = &callbacks,
                                .MaximumBytes = maximumBytes};
        if (!context.Output)
            throw std::runtime_error("Could not create the Build Support download file.");
        auto* curl = curl_easy_init();
        if (!curl)
            throw std::runtime_error("Could not initialize the Build Support HTTPS request.");
        const auto encodedUrl = std::string(url);
        curl_easy_setopt(curl, CURLOPT_URL, encodedUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "KeireBuildSupport/1");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDownload);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, TransferProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
        const auto result = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (result != CURLE_OK || context.Failed)
            throw std::runtime_error("Build Support HTTPS download failed: " + std::string(curl_easy_strerror(result)));
    }
} // namespace Keire::Detail
#endif
