#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace KeireHubTests
{
    inline std::filesystem::path ExecutablePath;

    [[nodiscard]] inline bool RunningInCi()
    {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        const int result = ::_dupenv_s(&value, &length, "CI");
        std::free(value);
        return result == 0 && length != 0;
#else
        return std::getenv("CI") != nullptr;
#endif
    }

    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
        {
            static std::atomic_uint64_t counter = 0;
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            m_Path = std::filesystem::temp_directory_path() /
                     ("keire-hub-runtime-tests-" + std::to_string(nonce) + '-' +
                      std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
            std::filesystem::create_directories(m_Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_Path, ignored);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

      private:
        std::filesystem::path m_Path;
    };

    inline void WriteText(const std::filesystem::path& path, const std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream)
            throw std::runtime_error("Could not write test fixture.");
    }

    [[nodiscard]] inline std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] inline std::string Digest(const char value = 'a') { return std::string(64, value); }
} // namespace KeireHubTests
