#include "Keire/Assets/PhysicsMaterialAsset.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 64ULL * 1024U;

        [[nodiscard]] const char* CombineModeName(const PhysicsMaterialCombineMode mode) noexcept
        {
            switch (mode)
            {
            case PhysicsMaterialCombineMode::Average:
                return "Average";
            case PhysicsMaterialCombineMode::Minimum:
                return "Minimum";
            case PhysicsMaterialCombineMode::Multiply:
                return "Multiply";
            case PhysicsMaterialCombineMode::Maximum:
                return "Maximum";
            }
            return "Average";
        }

        [[nodiscard]] PhysicsMaterialCombineMode ParseCombineMode(const std::string_view value)
        {
            if (value == "Average")
                return PhysicsMaterialCombineMode::Average;
            if (value == "Minimum")
                return PhysicsMaterialCombineMode::Minimum;
            if (value == "Multiply")
                return PhysicsMaterialCombineMode::Multiply;
            if (value == "Maximum")
                return PhysicsMaterialCombineMode::Maximum;
            throw std::runtime_error("Physics material contains an unsupported combine mode.");
        }
    } // namespace

    PhysicsMaterialAsset::PhysicsMaterialAsset(PhysicsMaterialDefinition definition) : m_Definition(definition)
    {
        Validate(m_Definition);
    }

    void PhysicsMaterialAsset::Validate(const PhysicsMaterialDefinition& definition)
    {
        if (definition.SchemaVersion != 1)
            throw std::invalid_argument("Physics material has an unsupported schema.");
        if (!std::isfinite(definition.Friction) || definition.Friction < 0.0F || definition.Friction > 100.0F)
            throw std::invalid_argument("Physics material friction must be finite and between zero and 100.");
        if (!std::isfinite(definition.Restitution) || definition.Restitution < 0.0F || definition.Restitution > 1.0F)
        {
            throw std::invalid_argument("Physics material restitution must be finite and between zero and one.");
        }
        if (definition.FrictionCombine > PhysicsMaterialCombineMode::Maximum ||
            definition.RestitutionCombine > PhysicsMaterialCombineMode::Maximum)
        {
            throw std::invalid_argument("Physics material contains an unsupported combine mode.");
        }
    }

    Ref<PhysicsMaterialAsset> PhysicsMaterialAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("Physics material asset is empty or exceeds the 64 KiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (!document.is_object() || document.value("schemaVersion", 0U) != 1U)
                throw std::runtime_error("Physics material asset has an unsupported schema.");

            PhysicsMaterialDefinition definition;
            definition.Friction = document.at("friction").get<float>();
            definition.Restitution = document.at("restitution").get<float>();
            definition.FrictionCombine = ParseCombineMode(document.value("frictionCombine", std::string("Average")));
            definition.RestitutionCombine =
                ParseCombineMode(document.value("restitutionCombine", std::string("Average")));
            return CreateRef<PhysicsMaterialAsset>(definition);
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("Physics material asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> PhysicsMaterialAsset::Encode(const PhysicsMaterialDefinition& definition)
    {
        Validate(definition);
        const Json document{{"schemaVersion", 1},
                            {"friction", definition.Friction},
                            {"restitution", definition.Restitution},
                            {"frictionCombine", CombineModeName(definition.FrictionCombine)},
                            {"restitutionCombine", CombineModeName(definition.RestitutionCombine)}};
        const auto encoded = document.dump(2);
        std::vector<std::byte> result(encoded.size());
        std::memcpy(result.data(), encoded.data(), encoded.size());
        return result;
    }

    AssetImporterRegistration CreatePhysicsMaterialAssetImporter()
    {
        return {"Keire.PhysicsMaterial",
                1,
                PhysicsMaterialAsset::StaticType(),
                {".keirephysicsmaterial"},
                [](const std::span<const std::byte> bytes)
                {
                    const auto parsed = PhysicsMaterialAsset::Decode(bytes);
                    return PhysicsMaterialAsset::Encode(parsed->Definition());
                }};
    }

    AssetDecoderRegistration CreatePhysicsMaterialAssetDecoder()
    {
        return {PhysicsMaterialAsset::StaticType(), CreateRef<PhysicsMaterialAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return PhysicsMaterialAsset::Decode(bytes); }};
    }
} // namespace Keire
