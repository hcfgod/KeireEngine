#include "Keire/Rendering/ShaderGraphResources.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumResourceTextBytes = 128;
        constexpr std::size_t MaximumResourceContractBytes = std::size_t{4} * 1024U * 1024U;
        constexpr std::uint32_t PortableStructuredBufferStrideBytes = 16;

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > MaximumResourceTextBytes ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] bool TextureResource(const ShaderGraphResourceKind kind) noexcept
        {
            return kind == ShaderGraphResourceKind::Texture2DArray || kind == ShaderGraphResourceKind::TextureCube ||
                   kind == ShaderGraphResourceKind::Texture3D;
        }

        [[nodiscard]] bool BufferResource(const ShaderGraphResourceKind kind) noexcept
        {
            return kind == ShaderGraphResourceKind::StructuredBuffer ||
                   kind == ShaderGraphResourceKind::ByteAddressBuffer;
        }

        void ValidateSampler(const SamplerDescription& sampler)
        {
            const auto validFilter = [](const TextureFilter filter)
            { return filter == TextureFilter::Nearest || filter == TextureFilter::Linear; };
            const auto validAddress = [](const TextureAddressMode address)
            {
                return address == TextureAddressMode::Repeat || address == TextureAddressMode::Clamp ||
                       address == TextureAddressMode::Mirror;
            };
            if (!validFilter(sampler.Minimum) || !validFilter(sampler.Magnification) || !validFilter(sampler.Mip) ||
                !validAddress(sampler.AddressU) || !validAddress(sampler.AddressV) || !validAddress(sampler.AddressW) ||
                sampler.Anisotropy == 0 || sampler.Anisotropy > 16)
                throw std::invalid_argument("Shader Graph sampler contains a non-portable value.");
        }

        void ValidateBuffer(const ShaderGraphBufferView& view, const ShaderGraphResourceKind kind)
        {
            const auto end = static_cast<std::uint64_t>(view.OffsetBytes) + view.SizeBytes;
            if (!view.Asset || view.SizeBytes == 0 || view.SizeBytes > MaximumShaderGraphBufferViewBytes ||
                (view.OffsetBytes % 4U) != 0U || (view.SizeBytes % 4U) != 0U ||
                end > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Shader Graph buffer view has invalid asset, alignment, size, or range.");
            if (kind == ShaderGraphResourceKind::StructuredBuffer)
            {
                if (view.StrideBytes != PortableStructuredBufferStrideBytes ||
                    (view.OffsetBytes % view.StrideBytes) != 0U || (view.SizeBytes % view.StrideBytes) != 0U)
                    throw std::invalid_argument("Shader Graph structured-buffer stride or range is invalid.");
            }
            else if (view.StrideBytes != 0)
                throw std::invalid_argument("Shader Graph byte-address buffers require a zero stride.");
        }

        [[nodiscard]] ShaderGraphResourceStatistics
        ResourceStatistics(const std::span<const ShaderGraphResourceDefinition> resources)
        {
            ShaderGraphResourceStatistics result;
            result.ResourceCount = resources.size();
            for (const auto& resource : resources)
            {
                if (resource.Kind == ShaderGraphResourceKind::Sampler)
                    ++result.SamplerCount;
                else if (TextureResource(resource.Kind))
                    ++result.TextureCount;
                else if (resource.Kind == ShaderGraphResourceKind::StructuredBuffer)
                {
                    ++result.ReadOnlyBufferCount;
                    ++result.StructuredBufferCount;
                    result.BufferViewBytes += std::get<ShaderGraphBufferView>(resource.Value).SizeBytes;
                }
                else if (resource.Kind == ShaderGraphResourceKind::ByteAddressBuffer)
                {
                    ++result.ReadOnlyBufferCount;
                    ++result.ByteAddressBufferCount;
                    result.BufferViewBytes += std::get<ShaderGraphBufferView>(resource.Value).SizeBytes;
                }
            }
            return result;
        }

        [[nodiscard]] const char* FilterName(const TextureFilter filter)
        {
            return filter == TextureFilter::Nearest ? "Nearest" : "Linear";
        }

        [[nodiscard]] const char* AddressName(const TextureAddressMode address)
        {
            switch (address)
            {
            case TextureAddressMode::Repeat:
                return "Repeat";
            case TextureAddressMode::Clamp:
                return "Clamp";
            case TextureAddressMode::Mirror:
                return "Mirror";
            }
            throw std::invalid_argument("Shader Graph sampler address mode is invalid.");
        }

        [[nodiscard]] TextureFilter DecodeFilter(const Json& source)
        {
            const auto value = source.get<std::string>();
            if (value == "Nearest")
                return TextureFilter::Nearest;
            if (value == "Linear")
                return TextureFilter::Linear;
            throw std::invalid_argument("Shader Graph sampler filter is invalid.");
        }

        [[nodiscard]] TextureAddressMode DecodeAddress(const Json& source)
        {
            const auto value = source.get<std::string>();
            if (value == "Repeat")
                return TextureAddressMode::Repeat;
            if (value == "Clamp")
                return TextureAddressMode::Clamp;
            if (value == "Mirror")
                return TextureAddressMode::Mirror;
            throw std::invalid_argument("Shader Graph sampler address mode is invalid.");
        }

        [[nodiscard]] Json EncodeSampler(const SamplerDescription& sampler)
        {
            return {{"minimum", FilterName(sampler.Minimum)},
                    {"magnification", FilterName(sampler.Magnification)},
                    {"mip", FilterName(sampler.Mip)},
                    {"addressU", AddressName(sampler.AddressU)},
                    {"addressV", AddressName(sampler.AddressV)},
                    {"addressW", AddressName(sampler.AddressW)},
                    {"anisotropy", sampler.Anisotropy}};
        }

        [[nodiscard]] SamplerDescription DecodeSampler(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Shader Graph sampler value must be an object.");
            SamplerDescription result;
            result.Minimum = DecodeFilter(source.at("minimum"));
            result.Magnification = DecodeFilter(source.at("magnification"));
            result.Mip = DecodeFilter(source.at("mip"));
            result.AddressU = DecodeAddress(source.at("addressU"));
            result.AddressV = DecodeAddress(source.at("addressV"));
            result.AddressW = DecodeAddress(source.at("addressW"));
            const auto anisotropy = source.at("anisotropy").get<std::uint32_t>();
            if (anisotropy > std::numeric_limits<std::uint8_t>::max())
                throw std::invalid_argument("Shader Graph sampler anisotropy is invalid.");
            result.Anisotropy = static_cast<std::uint8_t>(anisotropy);
            ValidateSampler(result);
            return result;
        }

        [[nodiscard]] Json EncodeResource(const ShaderGraphResourceDefinition& resource)
        {
            Json value;
            if (resource.Kind == ShaderGraphResourceKind::Sampler)
                value = EncodeSampler(std::get<SamplerDescription>(resource.Value));
            else if (TextureResource(resource.Kind))
            {
                const auto asset = std::get<AssetId>(resource.Value);
                value = asset ? Json(asset.ToString()) : Json(nullptr);
            }
            else
            {
                const auto& view = std::get<ShaderGraphBufferView>(resource.Value);
                value = {{"asset", view.Asset.ToString()},
                         {"offsetBytes", view.OffsetBytes},
                         {"sizeBytes", view.SizeBytes},
                         {"strideBytes", view.StrideBytes}};
            }
            return {{"id", resource.Id.ToString()},
                    {"name", resource.Name},
                    {"symbol", resource.Symbol},
                    {"kind", static_cast<std::uint8_t>(resource.Kind)},
                    {"value", std::move(value)}};
        }

        [[nodiscard]] ShaderGraphResourceDefinition DecodeResource(const Json& source)
        {
            ShaderGraphResourceDefinition result;
            result.Id = AssetId::Parse(source.at("id").get<std::string>());
            result.Name = source.at("name").get<std::string>();
            result.Symbol = source.at("symbol").get<std::string>();
            result.Kind = static_cast<ShaderGraphResourceKind>(source.at("kind").get<std::uint8_t>());
            const auto& value = source.at("value");
            if (result.Kind == ShaderGraphResourceKind::Sampler)
                result.Value = DecodeSampler(value);
            else if (TextureResource(result.Kind))
                result.Value = value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            else if (BufferResource(result.Kind))
                result.Value = ShaderGraphBufferView{
                    AssetId::Parse(value.at("asset").get<std::string>()), value.at("offsetBytes").get<std::uint32_t>(),
                    value.at("sizeBytes").get<std::uint32_t>(), value.at("strideBytes").get<std::uint32_t>()};
            else
                throw std::invalid_argument("Shader Graph resource kind is invalid.");
            return result;
        }
    } // namespace

    void ValidateShaderGraphResources(const std::span<const ShaderGraphResourceDefinition> resources)
    {
        if (resources.size() > MaximumShaderGraphResourceDefinitions)
            throw std::invalid_argument("Shader Graph resource definitions exceed their bound.");
        std::set<AssetId> identities;
        std::set<std::string, std::less<>> symbols;
        std::size_t bufferCount = 0;
        for (const auto& resource : resources)
        {
            if (!resource.Id || !identities.insert(resource.Id).second || resource.Name.empty() ||
                resource.Name.size() > MaximumResourceTextBytes || !ValidIdentifier(resource.Symbol) ||
                !symbols.insert(resource.Symbol).second || resource.Kind > ShaderGraphResourceKind::ByteAddressBuffer)
                throw std::invalid_argument("Shader Graph resource identity, name, symbol, or kind is invalid.");
            if (resource.Kind == ShaderGraphResourceKind::Sampler)
            {
                if (!std::holds_alternative<SamplerDescription>(resource.Value))
                    throw std::invalid_argument("Shader Graph sampler resource has the wrong value type.");
                ValidateSampler(std::get<SamplerDescription>(resource.Value));
            }
            else if (TextureResource(resource.Kind))
            {
                if (!std::holds_alternative<AssetId>(resource.Value))
                    throw std::invalid_argument("Shader Graph texture resource has the wrong value type.");
            }
            else
            {
                if (!BufferResource(resource.Kind) || !std::holds_alternative<ShaderGraphBufferView>(resource.Value))
                    throw std::invalid_argument("Shader Graph buffer resource has the wrong value type.");
                ValidateBuffer(std::get<ShaderGraphBufferView>(resource.Value), resource.Kind);
                if (++bufferCount > MaximumShaderGraphReadOnlyBuffers)
                    throw std::invalid_argument("Shader Graph read-only buffers exceed their bound.");
            }
        }
        const auto statistics = ResourceStatistics(resources);
        if (statistics.TextureCount > MaximumShaderGraphSampledTextures ||
            statistics.SamplerCount > MaximumShaderGraphSamplers)
            throw std::invalid_argument("Shader Graph sampled textures or samplers exceed their portable bound.");
    }

    ShaderGraphResourceAnalysis
    AnalyzeShaderGraphResources(const std::span<const ShaderGraphResourceDefinition> resources) noexcept
    {
        ShaderGraphResourceAnalysis result;
        try
        {
            ValidateShaderGraphResources(resources);
            result.Statistics = ResourceStatistics(resources);
        }
        catch (const std::exception& error)
        {
            result.Diagnostics.push_back({"SGR0001", error.what(), {}});
        }
        return result;
    }

    std::vector<std::byte> EncodeShaderGraphResources(const std::span<const ShaderGraphResourceDefinition> resources)
    {
        ValidateShaderGraphResources(resources);
        Json encoded = Json::array();
        for (const auto& resource : resources)
            encoded.push_back(EncodeResource(resource));
        const auto text =
            Json{{"schemaVersion", ShaderGraphResourceContractSchemaVersion}, {"resources", std::move(encoded)}}.dump(
                2);
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    std::vector<ShaderGraphResourceDefinition> DecodeShaderGraphResources(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumResourceContractBytes)
            throw std::invalid_argument("Shader Graph resource contract exceeds its byte bound.");
        const auto source = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                        reinterpret_cast<const char*>(bytes.data()) + bytes.size());
        if (!source.is_object())
            throw std::invalid_argument("Shader Graph resource contract must be an object.");
        const auto schemaVersion = source.value("schemaVersion", 0U);
        if (schemaVersion == 0)
            throw std::invalid_argument("Shader Graph resource contract schema is missing or unsupported.");
        if (schemaVersion > ShaderGraphResourceContractSchemaVersion)
            throw std::invalid_argument("Shader Graph resource contract schema version " +
                                        std::to_string(schemaVersion) + " is newer than the supported version " +
                                        std::to_string(ShaderGraphResourceContractSchemaVersion) + '.');
        const auto& encoded = source.at("resources");
        if (!encoded.is_array() || encoded.size() > MaximumShaderGraphResourceDefinitions)
            throw std::invalid_argument("Shader Graph resource definitions exceed their bound.");
        std::vector<ShaderGraphResourceDefinition> result;
        result.reserve(encoded.size());
        for (const auto& resource : encoded)
            result.push_back(DecodeResource(resource));
        ValidateShaderGraphResources(result);
        return result;
    }

    ShaderGraphResourceDeclarations GenerateShaderGraphResourceDeclarations(
        const std::span<const ShaderGraphResourceDefinition> resources, const std::uint32_t firstTextureRegister,
        const std::uint32_t firstSamplerRegister, const std::uint32_t firstBufferRegister)
    {
        ValidateShaderGraphResources(resources);
        ShaderGraphResourceDeclarations result;
        result.Statistics = ResourceStatistics(resources);
        result.NextTextureRegister = firstTextureRegister;
        result.NextSamplerRegister = firstSamplerRegister;
        result.NextBufferRegister = firstBufferRegister;
        std::ostringstream hlsl;
        for (const auto& resource : resources)
        {
            switch (resource.Kind)
            {
            case ShaderGraphResourceKind::Sampler:
                hlsl << "SamplerState " << resource.Symbol << " : register(s" << result.NextSamplerRegister++
                     << ", space2);\n";
                break;
            case ShaderGraphResourceKind::Texture2DArray:
                hlsl << "Texture2DArray " << resource.Symbol << " : register(t" << result.NextTextureRegister++
                     << ", space2);\n";
                break;
            case ShaderGraphResourceKind::TextureCube:
                hlsl << "TextureCube " << resource.Symbol << " : register(t" << result.NextTextureRegister++
                     << ", space2);\n";
                break;
            case ShaderGraphResourceKind::Texture3D:
                hlsl << "Texture3D " << resource.Symbol << " : register(t" << result.NextTextureRegister++
                     << ", space2);\n";
                break;
            case ShaderGraphResourceKind::StructuredBuffer:
                hlsl << "StructuredBuffer<uint4> " << resource.Symbol << " : register(t" << result.NextBufferRegister++
                     << ", space5);\n";
                break;
            case ShaderGraphResourceKind::ByteAddressBuffer:
                hlsl << "ByteAddressBuffer " << resource.Symbol << " : register(t" << result.NextBufferRegister++
                     << ", space5);\n";
                break;
            }
        }
        result.Hlsl = std::move(hlsl).str();
        return result;
    }

    std::vector<AssetId> ShaderGraphResourceDependencies(const std::span<const ShaderGraphResourceDefinition> resources)
    {
        ValidateShaderGraphResources(resources);
        std::vector<AssetId> result;
        for (const auto& resource : resources)
        {
            AssetId asset;
            if (TextureResource(resource.Kind))
                asset = std::get<AssetId>(resource.Value);
            else if (BufferResource(resource.Kind))
                asset = std::get<ShaderGraphBufferView>(resource.Value).Asset;
            if (asset)
                result.push_back(asset);
        }
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    ShaderGraphResourceBindings
    ResolveShaderGraphResourceBindings(const std::span<const ShaderGraphResourceDefinition> resources,
                                       const ShaderGraphResourceBindings& overrides)
    {
        ValidateShaderGraphResources(resources);
        ShaderGraphResourceBindings result;
        for (const auto& resource : resources)
            result.emplace(resource.Symbol, resource.Value);
        for (const auto& [symbol, value] : overrides)
        {
            const auto definition = std::ranges::find(resources, symbol, &ShaderGraphResourceDefinition::Symbol);
            if (definition == resources.end() ||
                (definition->Kind == ShaderGraphResourceKind::Sampler &&
                 !std::holds_alternative<SamplerDescription>(value)) ||
                (TextureResource(definition->Kind) && !std::holds_alternative<AssetId>(value)) ||
                (BufferResource(definition->Kind) && !std::holds_alternative<ShaderGraphBufferView>(value)))
                throw std::invalid_argument("Shader Graph material resource binding is unknown or type-incompatible.");
            auto candidate = *definition;
            candidate.Value = value;
            ValidateShaderGraphResources(std::span(&candidate, 1));
            result.insert_or_assign(symbol, value);
        }
        return result;
    }

    std::vector<AssetId>
    ShaderGraphResourceBindingDependencies(const std::span<const ShaderGraphResourceDefinition> resources,
                                           const ShaderGraphResourceBindings& bindings)
    {
        const auto resolved = ResolveShaderGraphResourceBindings(resources, bindings);
        auto definitions = std::vector<ShaderGraphResourceDefinition>(resources.begin(), resources.end());
        for (auto& definition : definitions)
            definition.Value = resolved.at(definition.Symbol);
        return ShaderGraphResourceDependencies(definitions);
    }
} // namespace Keire
