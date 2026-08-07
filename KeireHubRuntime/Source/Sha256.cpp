#include "Sha256.h"

#include "Persistence.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> RoundConstants{
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

        [[nodiscard]] std::uint32_t LoadBigEndian(const std::byte* bytes) noexcept
        {
            return (std::to_integer<std::uint32_t>(bytes[0]) << 24U) |
                   (std::to_integer<std::uint32_t>(bytes[1]) << 16U) |
                   (std::to_integer<std::uint32_t>(bytes[2]) << 8U) | std::to_integer<std::uint32_t>(bytes[3]);
        }

        void StoreBigEndian(const std::uint32_t value, std::byte* bytes) noexcept
        {
            bytes[0] = static_cast<std::byte>(value >> 24U);
            bytes[1] = static_cast<std::byte>(value >> 16U);
            bytes[2] = static_cast<std::byte>(value >> 8U);
            bytes[3] = static_cast<std::byte>(value);
        }

        void Transform(std::array<std::uint32_t, 8>& state, const std::byte* block) noexcept
        {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t index = 0; index < 16; ++index)
                words[index] = LoadBigEndian(block + index * 4U);
            for (std::size_t index = 16; index < words.size(); ++index)
            {
                const auto first =
                    std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3U);
                const auto second =
                    std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10U);
                words[index] = words[index - 16] + first + words[index - 7] + second;
            }

            auto a = state[0];
            auto b = state[1];
            auto c = state[2];
            auto d = state[3];
            auto e = state[4];
            auto f = state[5];
            auto g = state[6];
            auto h = state[7];
            for (std::size_t index = 0; index < words.size(); ++index)
            {
                const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
                const auto choice = (e & f) ^ (~e & g);
                const auto temporary1 = h + sum1 + choice + RoundConstants[index] + words[index];
                const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
                const auto majority = (a & b) ^ (a & c) ^ (b & c);
                const auto temporary2 = sum0 + majority;
                h = g;
                g = f;
                f = e;
                e = d + temporary1;
                d = c;
                c = b;
                b = a;
                a = temporary1 + temporary2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    } // namespace

    void Sha256Builder::Update(std::span<const std::byte> bytes) noexcept
    {
        m_TotalBytes += bytes.size();
        if (m_Buffered > 0)
        {
            const auto copied = std::min(bytes.size(), m_Buffer.size() - m_Buffered);
            std::copy_n(bytes.data(), copied, m_Buffer.data() + m_Buffered);
            m_Buffered += copied;
            bytes = bytes.subspan(copied);
            if (m_Buffered == m_Buffer.size())
            {
                Transform(m_State, m_Buffer.data());
                m_Buffered = 0;
            }
        }
        while (bytes.size() >= m_Buffer.size())
        {
            Transform(m_State, bytes.data());
            bytes = bytes.subspan(m_Buffer.size());
        }
        if (!bytes.empty())
        {
            std::copy(bytes.begin(), bytes.end(), m_Buffer.begin());
            m_Buffered = bytes.size();
        }
    }

    Sha256Digest Sha256Builder::Finish() noexcept
    {
        std::array<std::byte, 128> tail{};
        if (m_Buffered > 0)
            std::copy_n(m_Buffer.data(), m_Buffered, tail.data());
        tail[m_Buffered] = std::byte{0x80};
        const std::size_t padded = m_Buffered < 56U ? 64U : 128U;
        const auto bitLength = m_TotalBytes * 8U;
        for (std::size_t index = 0; index < 8; ++index)
            tail[padded - 1U - index] = static_cast<std::byte>(bitLength >> (index * 8U));
        Transform(m_State, tail.data());
        if (padded == 128U)
            Transform(m_State, tail.data() + 64U);

        Sha256Digest digest{};
        for (std::size_t index = 0; index < m_State.size(); ++index)
            StoreBigEndian(m_State[index], digest.data() + index * 4U);
        return digest;
    }

    std::string DigestToString(const Sha256Digest& digest)
    {
        constexpr char Hex[] = "0123456789abcdef";
        std::string result(digest.size() * 2U, '0');
        for (std::size_t index = 0; index < digest.size(); ++index)
        {
            const auto value = std::to_integer<unsigned char>(digest[index]);
            result[index * 2U] = Hex[value >> 4U];
            result[index * 2U + 1U] = Hex[value & 0x0fU];
        }
        return result;
    }

    HubResult<std::string> Sha256File(const std::filesystem::path& path, const std::uint64_t maximumBytes)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > maximumBytes)
        {
            return HubResult<std::string>::Failure({.Code = HubErrorCode::TemplatePayloadInvalid,
                                                    .Message = "A template payload file has an invalid size.",
                                                    .AffectedItem = PathToUtf8(path.filename()),
                                                    .TechnicalDetails = error.message()});
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return HubResult<std::string>::Failure({.Code = HubErrorCode::IoRead,
                                                    .Message = "A template payload file could not be opened.",
                                                    .AffectedItem = PathToUtf8(path.filename())});
        Sha256Builder builder;
        std::array<std::byte, 64U * 1024U> buffer{};
        while (stream)
        {
            stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto count = stream.gcount();
            if (count > 0)
                builder.Update(std::span(buffer).first(static_cast<std::size_t>(count)));
        }
        if (!stream.eof())
            return HubResult<std::string>::Failure({.Code = HubErrorCode::IoRead,
                                                    .Message = "A template payload file could not be read.",
                                                    .AffectedItem = PathToUtf8(path.filename())});
        return HubResult<std::string>::Success(DigestToString(builder.Finish()));
    }
} // namespace KeireHub::Detail
