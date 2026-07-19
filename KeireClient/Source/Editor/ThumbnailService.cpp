#include "KeireClient/Editor/ThumbnailService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr std::uint32_t ThumbnailWidth = 96;
        constexpr std::uint32_t ThumbnailHeight = 96;

        void PutPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                      const std::uint32_t y, const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue,
                      const std::uint8_t alpha = 255)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[offset] = static_cast<std::byte>(red);
            pixels[offset + 1] = static_cast<std::byte>(green);
            pixels[offset + 2] = static_cast<std::byte>(blue);
            pixels[offset + 3] = static_cast<std::byte>(alpha);
        }

        [[nodiscard]] std::vector<std::byte> MakeIcon(const std::uint32_t width, const std::uint32_t height,
                                                      const std::array<std::uint8_t, 3> background,
                                                      const std::array<std::uint8_t, 3> accent, const char glyph,
                                                      const bool missing)
        {
            std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const bool border = x < 3 || y < 3 || x + 3 >= width || y + 3 >= height;
                    const auto color = border ? accent : background;
                    PutPixel(pixels, width, x, y, color[0], color[1], color[2]);
                }
            const auto centerX = width / 2;
            const auto centerY = height / 2;
            const std::uint32_t radius = std::max(4U, std::min(width, height) / 5);
            for (std::uint32_t y = centerY - radius; y <= centerY + radius; ++y)
                for (std::uint32_t x = centerX - radius; x <= centerX + radius; ++x)
                    if (x < width && y < height &&
                        (x == centerX - radius || x == centerX + radius || y == centerY - radius ||
                         y == centerY + radius ||
                         (glyph == 'X' && (x - (centerX - radius) == y - (centerY - radius) ||
                                           x - (centerX - radius) + y - (centerY - radius) == radius * 2))))
                        PutPixel(pixels, width, x, y, accent[0], accent[1], accent[2]);
            if (missing)
            {
                for (std::uint32_t index = 8; index + 8 < std::min(width, height); ++index)
                {
                    PutPixel(pixels, width, index, index, 235, 72, 82);
                    PutPixel(pixels, width, width - index - 1, index, 235, 72, 82);
                }
            }
            return pixels;
        }

        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }
    } // namespace

    std::vector<std::byte> MakeFolderThumbnail(const std::uint32_t width, const std::uint32_t height,
                                               const bool missing)
    {
        auto pixels = MakeIcon(width, height, {35, 43, 58}, {238, 181, 68}, 'F', missing);
        const auto top = height / 3;
        const auto left = width / 6;
        const auto right = width - left;
        for (std::uint32_t y = top; y < height - height / 6; ++y)
            for (std::uint32_t x = left; x < right; ++x)
                PutPixel(pixels, width, x, y, 216, 154, 48);
        for (std::uint32_t y = top - height / 12; y < top; ++y)
            for (std::uint32_t x = left; x < width / 2; ++x)
                PutPixel(pixels, width, x, y, 238, 181, 68);
        return pixels;
    }

    class ThumbnailService::Impl final
    {
      public:
        struct ProviderRecord
        {
            std::uint32_t Version = 0;
            Provider Generate;
        };

        struct Job
        {
            ThumbnailRequest Request;
            std::string Key;
            std::uint64_t Generation = 0;
        };

        Impl(std::filesystem::path cacheDirectory, const std::size_t capacity)
            : CacheDirectory(std::move(cacheDirectory)), Capacity(capacity), OwnerThread(std::this_thread::get_id()),
              Worker([this](const std::stop_token token) { Run(token); })
        {
            if (Capacity == 0 || Capacity > 4096)
                throw std::invalid_argument("Thumbnail queue capacity must be in the range 1..4096.");
            std::filesystem::create_directories(CacheDirectory);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("ThumbnailService::") + operation +
                                       " must run on the owner thread.");
        }

        [[nodiscard]] std::string KeyFor(const ThumbnailRequest& request, const ProviderRecord& provider) const
        {
            auto extension = Lower(request.RelativePath.extension().string());
            if (!extension.empty() && extension.front() == '.')
                extension.erase(extension.begin());
            std::ranges::transform(extension, extension.begin(), [](const unsigned char character)
                                   { return std::isalnum(character) ? static_cast<char>(character) : '_'; });
            return request.Digest + "-" + std::to_string(provider.Version) + "-" + extension;
        }

        [[nodiscard]] std::filesystem::path CachePath(const std::string& key) const
        {
            return CacheDirectory / (key + ".rgba");
        }

        [[nodiscard]] std::vector<std::byte> ReadCache(const std::filesystem::path& path) const
        {
            constexpr auto expected = static_cast<std::size_t>(ThumbnailWidth) * ThumbnailHeight * 4;
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != expected)
                return {};
            std::ifstream input(path, std::ios::binary);
            std::vector<std::byte> result(expected);
            if (!input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
                return {};
            return result;
        }

        void WriteCache(const std::filesystem::path& path, const std::span<const std::byte> pixels) const
        {
            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output.write(reinterpret_cast<const char*>(pixels.data()),
                                  static_cast<std::streamsize>(pixels.size())))
                    return;
            }
            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (error)
            {
                std::filesystem::remove(path, error);
                error.clear();
                std::filesystem::rename(temporary, path, error);
                if (error)
                    std::filesystem::remove(temporary, error);
            }
        }

        void Run(const std::stop_token token)
        {
            while (!token.stop_requested())
            {
                Job job;
                ProviderRecord provider;
                {
                    std::unique_lock lock(Mutex);
                    Condition.wait(lock, token, [&] { return !Jobs.empty(); });
                    if (token.stop_requested())
                        return;
                    job = std::move(Jobs.front());
                    Jobs.pop_front();
                    const auto extension = Lower(job.Request.RelativePath.extension().string());
                    provider = Providers.contains(extension) ? Providers.at(extension) : Providers.at("*");
                }
                auto pixels = ReadCache(CachePath(job.Key));
                if (pixels.empty())
                {
                    pixels = provider.Generate(ThumbnailWidth, ThumbnailHeight, job.Request.Missing);
                    if (pixels.size() == static_cast<std::size_t>(ThumbnailWidth) * ThumbnailHeight * 4)
                        WriteCache(CachePath(job.Key), pixels);
                }
                std::scoped_lock lock(Mutex);
                Pending.erase(job.Request.Asset);
                if (job.Generation == Generation && !pixels.empty())
                    Completed.push_back({job.Request.Asset, ThumbnailWidth, ThumbnailHeight, std::move(pixels)});
            }
        }

        std::filesystem::path CacheDirectory;
        std::size_t Capacity = 0;
        std::thread::id OwnerThread;
        mutable std::mutex Mutex;
        std::condition_variable_any Condition;
        std::unordered_map<std::string, ProviderRecord> Providers;
        std::deque<Job> Jobs;
        std::deque<ThumbnailResult> Completed;
        std::unordered_set<Keire::AssetId> Pending;
        std::uint64_t Generation = 1;
        std::jthread Worker;
    };

    ThumbnailService::ThumbnailService(std::filesystem::path cacheDirectory, const std::size_t queueCapacity)
        : m_Impl(std::make_unique<Impl>(std::move(cacheDirectory), queueCapacity))
    {
        RegisterProvider(".keirescene", 1, [](const auto width, const auto height, const auto missing)
                         { return MakeIcon(width, height, {28, 39, 54}, {72, 148, 245}, 'S', missing); });
        RegisterProvider(".keireinput", 1, [](const auto width, const auto height, const auto missing)
                         { return MakeIcon(width, height, {37, 34, 56}, {163, 111, 245}, 'I', missing); });
        RegisterProvider("*", 1, [](const auto width, const auto height, const auto missing)
                         { return MakeIcon(width, height, {40, 44, 52}, {130, 142, 162}, 'X', missing); });
    }

    ThumbnailService::~ThumbnailService()
    {
        m_Impl->Worker.request_stop();
        m_Impl->Condition.notify_all();
    }

    void ThumbnailService::RegisterProvider(std::string extension, const std::uint32_t version, Provider provider)
    {
        m_Impl->RequireOwner("RegisterProvider");
        extension = Lower(std::move(extension));
        if (extension.empty() || version == 0 || !provider || m_Impl->Providers.contains(extension))
            throw std::invalid_argument("Thumbnail provider registration is invalid or duplicated.");
        m_Impl->Providers.emplace(std::move(extension), Impl::ProviderRecord{version, std::move(provider)});
    }

    bool ThumbnailService::Request(ThumbnailRequest request)
    {
        m_Impl->RequireOwner("Request");
        if (!request.Asset || request.Digest.empty())
            return false;
        const auto extension = Lower(request.RelativePath.extension().string());
        const auto provider =
            m_Impl->Providers.contains(extension) ? m_Impl->Providers.at(extension) : m_Impl->Providers.at("*");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Pending.contains(request.Asset) || m_Impl->Jobs.size() >= m_Impl->Capacity)
            return false;
        m_Impl->Pending.insert(request.Asset);
        m_Impl->Jobs.push_back({std::move(request), {}, m_Impl->Generation});
        m_Impl->Jobs.back().Key = m_Impl->KeyFor(m_Impl->Jobs.back().Request, provider);
        m_Impl->Condition.notify_one();
        return true;
    }

    std::vector<ThumbnailResult> ThumbnailService::DrainCompleted(const std::size_t maximum)
    {
        m_Impl->RequireOwner("DrainCompleted");
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<ThumbnailResult> result;
        const auto count = std::min(maximum, m_Impl->Completed.size());
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            result.push_back(std::move(m_Impl->Completed.front()));
            m_Impl->Completed.pop_front();
        }
        return result;
    }

    void ThumbnailService::CancelAll() noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        ++m_Impl->Generation;
        m_Impl->Jobs.clear();
        m_Impl->Completed.clear();
        m_Impl->Pending.clear();
    }

    std::size_t ThumbnailService::PendingCount() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Pending.size();
    }
} // namespace KeireEditor
