#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

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
                const auto s0 =
                    std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^ (words[index - 15] >> 3U);
                const auto s1 =
                    std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10U);
                words[index] = words[index - 16] + s0 + words[index - 7] + s1;
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

        [[nodiscard]] std::vector<std::byte> ReadBounded(const std::filesystem::path& path,
                                                         const std::uintmax_t maximum)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > maximum)
                throw std::runtime_error("Could not read bounded file: " + path.string());
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream ||
                (size > 0 && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))))
                throw std::runtime_error("Could not read file: " + path.string());
            return bytes;
        }
    } // namespace

    Sha256Digest Sha256(const std::span<const std::byte> bytes) noexcept
    {
        std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                           0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        std::size_t offset = 0;
        while (bytes.size() - offset >= 64U)
        {
            Transform(state, bytes.data() + offset);
            offset += 64U;
        }

        std::array<std::byte, 128> tail{};
        const std::size_t remaining = bytes.size() - offset;
        if (remaining > 0)
            std::copy_n(bytes.data() + offset, remaining, tail.data());
        tail[remaining] = std::byte{0x80};
        const std::size_t padded = remaining < 56U ? 64U : 128U;
        const auto bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
        for (std::size_t index = 0; index < 8; ++index)
            tail[padded - 1U - index] = static_cast<std::byte>(bitLength >> (index * 8U));
        Transform(state, tail.data());
        if (padded == 128U)
            Transform(state, tail.data() + 64U);

        Sha256Digest digest{};
        for (std::size_t index = 0; index < state.size(); ++index)
            StoreBigEndian(state[index], digest.data() + index * 4U);
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

    Sha256Digest ParseDigest(const std::string_view value)
    {
        if (value.size() != 64U)
            throw std::runtime_error("SHA-256 digest must contain 64 hexadecimal digits.");
        Sha256Digest result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            unsigned int byte = 0;
            const auto parsed = std::from_chars(value.data() + index * 2U, value.data() + index * 2U + 2U, byte, 16);
            if (parsed.ec != std::errc{})
                throw std::runtime_error("SHA-256 digest contains invalid hexadecimal digits.");
            result[index] = static_cast<std::byte>(byte);
        }
        return result;
    }

    CatalogData LoadCatalog(const std::filesystem::path& path)
    {
        const auto bytes = ReadBounded(path, 64U * 1024U * 1024U);
        const Json document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                          reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        const auto schemaVersion = document.value("schemaVersion", 0);
        if (!document.is_object() || (schemaVersion != 1 && schemaVersion != 2) || !document.contains("assets") ||
            !document["assets"].is_array())
            throw std::runtime_error("Asset catalog has an unsupported or malformed schema: " + path.string());

        CatalogData catalog;
        catalog.Path = std::filesystem::absolute(path).lexically_normal();
        for (const auto& item : document["assets"])
        {
            CatalogEntry entry;
            entry.Id = AssetId::Parse(item.at("id").get<std::string>());
            entry.Type = AssetTypeId::Parse(item.at("type").get<std::string>());
            const std::filesystem::path pack = item.at("pack").get<std::string>();
            if (pack.empty() || pack.is_absolute() || pack.lexically_normal().string().starts_with(".."))
                throw std::runtime_error("Asset catalog contains an unsafe pack path.");
            entry.PackPath = (catalog.Path.parent_path() / pack).lexically_normal();
            entry.Offset = item.at("offset").get<std::uint64_t>();
            entry.CompressedBytes = item.at("compressedBytes").get<std::uint64_t>();
            entry.UncompressedBytes = item.at("uncompressedBytes").get<std::uint64_t>();
            entry.Digest = ParseDigest(item.at("sha256").get<std::string>());
            if (item.contains("dependencies"))
            {
                for (const auto& dependency : item["dependencies"])
                    entry.Dependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
            }
            if (item.contains("metadata"))
            {
                const auto& metadata = item.at("metadata");
                if (metadata.contains("localBounds"))
                {
                    const auto& bounds = metadata.at("localBounds");
                    if (!bounds.is_object() || !bounds.at("minimum").is_array() || bounds.at("minimum").size() != 3 ||
                        !bounds.at("maximum").is_array() || bounds.at("maximum").size() != 3)
                        throw std::runtime_error("Asset catalog contains malformed local bounds.");
                    AssetBounds decoded;
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        decoded.Minimum[axis] = bounds.at("minimum")[axis].get<float>();
                        decoded.Maximum[axis] = bounds.at("maximum")[axis].get<float>();
                        if (!std::isfinite(decoded.Minimum[axis]) || !std::isfinite(decoded.Maximum[axis]) ||
                            decoded.Minimum[axis] > decoded.Maximum[axis])
                            throw std::runtime_error("Asset catalog contains invalid local bounds.");
                    }
                    entry.Metadata.LocalBounds = decoded;
                }
            }
            catalog.Entries.push_back(std::move(entry));
        }
        return catalog;
    }

    void WriteCatalog(const std::filesystem::path& path, const std::span<const CatalogEntry> entries)
    {
        Json assets = Json::array();
        for (const auto& entry : entries)
        {
            Json dependencies = Json::array();
            for (const auto dependency : entry.Dependencies)
                dependencies.push_back(dependency.ToString());
            Json metadata = Json::object();
            if (entry.Metadata.LocalBounds)
            {
                const auto& bounds = *entry.Metadata.LocalBounds;
                metadata["localBounds"] = {{"minimum", bounds.Minimum}, {"maximum", bounds.Maximum}};
            }
            assets.push_back({{"id", entry.Id.ToString()},
                              {"type", entry.Type.ToString()},
                              {"pack", entry.PackPath.generic_string()},
                              {"offset", entry.Offset},
                              {"compressedBytes", entry.CompressedBytes},
                              {"uncompressedBytes", entry.UncompressedBytes},
                              {"sha256", DigestToString(entry.Digest)},
                              {"dependencies", std::move(dependencies)},
                              {"metadata", std::move(metadata)}});
        }
        const Json document{{"schemaVersion", 2}, {"assets", std::move(assets)}};
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("Could not create asset catalog: " + path.string());
        stream << document.dump(2) << '\n';
        stream.close();
        if (!stream)
            throw std::runtime_error("Could not write asset catalog: " + path.string());
        AtomicReplace(temporary, path);
    }

    void WritePackHeader(std::ostream& stream)
    {
        stream.write(PackMagic.data(), static_cast<std::streamsize>(PackMagic.size()));
        std::array<std::byte, 8> values{};
        values[0] = static_cast<std::byte>(PackVersion);
        values[1] = static_cast<std::byte>(PackVersion >> 8U);
        values[2] = static_cast<std::byte>(PackVersion >> 16U);
        values[3] = static_cast<std::byte>(PackVersion >> 24U);
        stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size()));
        if (!stream)
            throw std::runtime_error("Could not write the asset pack header.");
    }

    void ValidatePackHeader(std::istream& stream, const std::filesystem::path& path)
    {
        std::array<char, 8> magic{};
        std::array<std::byte, 8> values{};
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size()));
        const auto version =
            std::to_integer<std::uint32_t>(values[0]) | (std::to_integer<std::uint32_t>(values[1]) << 8U) |
            (std::to_integer<std::uint32_t>(values[2]) << 16U) | (std::to_integer<std::uint32_t>(values[3]) << 24U);
        if (!stream || magic != PackMagic || version != PackVersion)
            throw std::runtime_error("Asset pack has an invalid header: " + path.string());
    }

    void AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& destination)
    {
        std::error_code error;
        const auto backup = destination.string() + ".bak";
        std::filesystem::remove(backup, error);
        error.clear();
        if (std::filesystem::exists(destination))
        {
            RenamePathWithRetry(destination, backup);
        }
        try
        {
            RenamePathWithRetry(temporary, destination);
        }
        catch (...)
        {
            std::error_code ignored;
            if (std::filesystem::exists(backup))
                (void)TryRenamePathWithRetry(backup, destination, ignored);
            throw;
        }
        std::filesystem::remove(backup, error);
    }
} // namespace Keire::Detail
