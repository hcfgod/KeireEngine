#include "Keire/Vfx/VfxVolumeAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumDocumentBytes = 64U * 1024U * 1024U;
        constexpr std::size_t MaximumVolumeCells = 1U * 1024U * 1024U;

        [[nodiscard]] std::uint32_t Hash(std::uint32_t value) noexcept
        {
            value ^= value >> 16U;
            value *= 0x7feb352dU;
            value ^= value >> 15U;
            value *= 0x846ca68bU;
            value ^= value >> 16U;
            return value;
        }

        [[nodiscard]] float Unit(const std::uint32_t value) noexcept
        {
            return static_cast<float>(value >> 8U) * (1.0F / 16'777'216.0F);
        }

        [[nodiscard]] Vector3 ParseVector3(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("VFX volume vector must contain exactly three numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }

        [[nodiscard]] Json EncodeVector3(const Vector3 value) { return Json::array({value.X, value.Y, value.Z}); }
    } // namespace

    VfxVolumeAsset::VfxVolumeAsset(VfxVolumeDefinition definition) : m_Definition(std::move(definition))
    {
        Validate(m_Definition);
        m_CumulativeWeights.reserve(m_Definition.Cells.size());
        double cumulative = 0.0;
        for (const auto& cell : m_Definition.Cells)
        {
            const auto extent = Vector3{cell.Maximum.X - cell.Minimum.X, cell.Maximum.Y - cell.Minimum.Y,
                                        cell.Maximum.Z - cell.Minimum.Z};
            cumulative += static_cast<double>(extent.X) * extent.Y * extent.Z * cell.Density;
            m_CumulativeWeights.push_back(static_cast<float>(cumulative));
        }
        m_TotalWeight = static_cast<float>(cumulative);
    }

    std::size_t VfxVolumeAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Definition.Cells.capacity() * sizeof(VfxVolumeCell) +
               m_CumulativeWeights.capacity() * sizeof(float);
    }

    Vector3 VfxVolumeAsset::Sample(const std::uint32_t randomValue) const noexcept
    {
        if (m_Definition.Cells.empty() || m_CumulativeWeights.empty() || m_TotalWeight <= 0.0F)
            return {};
        const auto selection = Unit(Hash(randomValue ^ 0x3c6ef372U)) * m_TotalWeight;
        const auto found = std::lower_bound(m_CumulativeWeights.begin(), m_CumulativeWeights.end(), selection);
        const auto index = static_cast<std::size_t>(
            std::min<std::ptrdiff_t>(std::distance(m_CumulativeWeights.begin(), found),
                                     static_cast<std::ptrdiff_t>(m_Definition.Cells.size() - 1U)));
        const auto& cell = m_Definition.Cells[index];
        const auto x = Unit(Hash(randomValue ^ 0xa54ff53aU));
        const auto y = Unit(Hash(randomValue ^ 0x510e527fU));
        const auto z = Unit(Hash(randomValue ^ 0x9b05688cU));
        return {cell.Minimum.X + (cell.Maximum.X - cell.Minimum.X) * x,
                cell.Minimum.Y + (cell.Maximum.Y - cell.Minimum.Y) * y,
                cell.Minimum.Z + (cell.Maximum.Z - cell.Minimum.Z) * z};
    }

    void VfxVolumeAsset::Validate(const VfxVolumeDefinition& definition)
    {
        if (definition.SchemaVersion != 1)
            throw std::invalid_argument("VFX volume has an unsupported schema.");
        if (definition.Cells.empty() || definition.Cells.size() > MaximumVolumeCells)
            throw std::invalid_argument("VFX volume must contain between one and 1,048,576 cells.");
        double totalWeight = 0.0;
        for (const auto& cell : definition.Cells)
        {
            if (!Math::IsFinite(cell.Minimum) || !Math::IsFinite(cell.Maximum) || cell.Minimum.X >= cell.Maximum.X ||
                cell.Minimum.Y >= cell.Maximum.Y || cell.Minimum.Z >= cell.Maximum.Z || !std::isfinite(cell.Density) ||
                cell.Density <= 0.0F || cell.Density > 1'000'000.0F)
            {
                throw std::invalid_argument("VFX volume contains an invalid density cell.");
            }
            const auto extent = Vector3{cell.Maximum.X - cell.Minimum.X, cell.Maximum.Y - cell.Minimum.Y,
                                        cell.Maximum.Z - cell.Minimum.Z};
            totalWeight += static_cast<double>(extent.X) * extent.Y * extent.Z * cell.Density;
            if (!std::isfinite(totalWeight) || totalWeight > std::numeric_limits<float>::max())
                throw std::invalid_argument("VFX volume total sampling weight exceeds the supported range.");
        }
        if (totalWeight <= 0.0)
            throw std::invalid_argument("VFX volume total sampling weight must be positive.");
    }

    Ref<VfxVolumeAsset> VfxVolumeAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("VFX volume asset is empty or exceeds the 64 MiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (!document.is_object() || document.value("schemaVersion", 0U) != 1U)
                throw std::runtime_error("VFX volume asset has an unsupported schema.");
            const auto& cells = document.at("cells");
            if (!cells.is_array() || cells.empty() || cells.size() > MaximumVolumeCells)
                throw std::runtime_error("VFX volume asset has an invalid cell table.");
            VfxVolumeDefinition definition;
            definition.Cells.clear();
            definition.Cells.reserve(cells.size());
            for (const auto& cell : cells)
            {
                definition.Cells.push_back(
                    {ParseVector3(cell.at("minimum")), ParseVector3(cell.at("maximum")), cell.value("density", 1.0F)});
            }
            return CreateRef<VfxVolumeAsset>(std::move(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("VFX volume asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> VfxVolumeAsset::Encode(const VfxVolumeDefinition& definition)
    {
        Validate(definition);
        Json cells = Json::array();
        for (const auto& cell : definition.Cells)
        {
            cells.push_back({{"minimum", EncodeVector3(cell.Minimum)},
                             {"maximum", EncodeVector3(cell.Maximum)},
                             {"density", cell.Density}});
        }
        const auto encoded = Json{{"schemaVersion", definition.SchemaVersion}, {"cells", std::move(cells)}}.dump(2);
        std::vector<std::byte> result(encoded.size());
        std::memcpy(result.data(), encoded.data(), encoded.size());
        return result;
    }

    AssetImporterRegistration CreateVfxVolumeAssetImporter()
    {
        return {"Keire.VfxVolume",
                1,
                VfxVolumeAsset::StaticType(),
                {".keirevfxvolume"},
                [](const std::span<const std::byte> bytes)
                {
                    const auto parsed = VfxVolumeAsset::Decode(bytes);
                    return VfxVolumeAsset::Encode(parsed->Definition());
                }};
    }

    AssetDecoderRegistration CreateVfxVolumeAssetDecoder()
    {
        return {VfxVolumeAsset::StaticType(), CreateRef<VfxVolumeAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return VfxVolumeAsset::Decode(bytes); }};
    }
} // namespace Keire
