#pragma once

#include "KeireHubRuntime/HubError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace KeireHub::Detail
{
    using Sha256Digest = std::array<std::byte, 32>;

    class Sha256Builder final
    {
      public:
        void Update(std::span<const std::byte> bytes) noexcept;
        [[nodiscard]] Sha256Digest Finish() noexcept;

      private:
        std::array<std::uint32_t, 8> m_State{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        std::array<std::byte, 64> m_Buffer{};
        std::size_t m_Buffered = 0;
        std::uint64_t m_TotalBytes = 0;
    };

    [[nodiscard]] std::string DigestToString(const Sha256Digest& digest);
    [[nodiscard]] HubResult<std::string> Sha256File(const std::filesystem::path& path, std::uint64_t maximumBytes);
} // namespace KeireHub::Detail
