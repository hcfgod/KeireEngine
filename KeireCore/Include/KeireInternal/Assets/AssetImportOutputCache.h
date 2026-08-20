#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    using ImportOutputCacheJson = nlohmann::json;

    [[nodiscard]] inline ImportOutputCacheJson EncodeDerivedMetadata(const AssetDerivedMetadata& metadata)
    {
        if (!metadata.LocalBounds)
            return nullptr;
        return {{"minimum", metadata.LocalBounds->Minimum}, {"maximum", metadata.LocalBounds->Maximum}};
    }

    [[nodiscard]] inline AssetDerivedMetadata DecodeDerivedMetadata(const ImportOutputCacheJson& value)
    {
        AssetDerivedMetadata metadata;
        if (!value.is_null())
        {
            metadata.LocalBounds = AssetBounds{value.at("minimum").get<std::array<float, 3>>(),
                                               value.at("maximum").get<std::array<float, 3>>()};
        }
        return metadata;
    }

    [[nodiscard]] inline std::vector<std::uint8_t> ToUnsignedBytes(const std::span<const std::byte> bytes)
    {
        std::vector<std::uint8_t> result(bytes.size());
        std::ranges::transform(bytes, result.begin(),
                               [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
        return result;
    }

    [[nodiscard]] inline std::vector<std::byte> ToBytes(const std::vector<std::uint8_t>& bytes)
    {
        std::vector<std::byte> result(bytes.size());
        std::ranges::transform(bytes, result.begin(),
                               [](const std::uint8_t value) { return static_cast<std::byte>(value); });
        return result;
    }

    [[nodiscard]] inline ImportOutputCacheJson EncodeCachedImportOutput(const AssetImportOutput& output)
    {
        ImportOutputCacheJson sourceDependencies = ImportOutputCacheJson::array();
        for (const auto& dependency : output.SourceDependencies)
        {
            sourceDependencies.push_back(
                {{"path", PathToUtf8(dependency.RelativePath)}, {"digest", dependency.Digest}});
        }
        ImportOutputCacheJson diagnostics = ImportOutputCacheJson::array();
        for (const auto& diagnostic : output.Diagnostics)
        {
            diagnostics.push_back({{"severity", static_cast<std::uint8_t>(diagnostic.Severity)},
                                   {"path", PathToUtf8(diagnostic.RelativePath)},
                                   {"line", diagnostic.Line},
                                   {"column", diagnostic.Column},
                                   {"message", diagnostic.Message}});
        }
        ImportOutputCacheJson dependencies = ImportOutputCacheJson::array();
        for (const auto dependency : output.AssetDependencies)
            dependencies.push_back(dependency.ToString());
        ImportOutputCacheJson subAssets = ImportOutputCacheJson::array();
        for (const auto& subAsset : output.SubAssets)
        {
            ImportOutputCacheJson subAssetDependencies = ImportOutputCacheJson::array();
            for (const auto dependency : subAsset.AssetDependencies)
                subAssetDependencies.push_back(dependency.ToString());
            subAssets.push_back({{"id", subAsset.Id.ToString()},
                                 {"type", subAsset.Type.ToString()},
                                 {"key", subAsset.Key},
                                 {"name", subAsset.Name},
                                 {"bytes", ImportOutputCacheJson::binary(ToUnsignedBytes(subAsset.Bytes))},
                                 {"dependencies", std::move(subAssetDependencies)},
                                 {"metadata", EncodeDerivedMetadata(subAsset.Metadata)}});
        }
        return {{"schemaVersion", 1},
                {"sourceDependencies", std::move(sourceDependencies)},
                {"diagnostics", std::move(diagnostics)},
                {"assetDependencies", std::move(dependencies)},
                {"metadata", EncodeDerivedMetadata(output.Metadata)},
                {"subAssets", std::move(subAssets)},
                {"primaryType", output.PrimaryType ? output.PrimaryType->ToString() : std::string{}}};
    }

    [[nodiscard]] inline AssetImportOutput DecodeCachedImportOutput(const ImportOutputCacheJson& value,
                                                                    std::vector<std::byte> primaryBytes)
    {
        if (!value.is_object() || value.value("schemaVersion", 0) != 1)
            throw std::runtime_error("Asset import-output cache has an unsupported schema.");
        AssetImportOutput output;
        output.Bytes = std::move(primaryBytes);
        for (const auto& dependency : value.at("sourceDependencies"))
        {
            output.SourceDependencies.push_back(
                {PathFromUtf8(dependency.at("path").get<std::string>()), dependency.at("digest").get<std::string>()});
        }
        for (const auto& diagnostic : value.at("diagnostics"))
        {
            const auto severity = diagnostic.at("severity").get<std::uint8_t>();
            if (severity > static_cast<std::uint8_t>(AssetDiagnosticSeverity::Error))
                throw std::runtime_error("Asset import-output cache contains an invalid diagnostic severity.");
            output.Diagnostics.push_back(
                {static_cast<AssetDiagnosticSeverity>(severity), PathFromUtf8(diagnostic.at("path").get<std::string>()),
                 diagnostic.at("line").get<std::uint32_t>(), diagnostic.at("column").get<std::uint32_t>(),
                 diagnostic.at("message").get<std::string>()});
        }
        for (const auto& dependency : value.at("assetDependencies"))
            output.AssetDependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
        output.Metadata = DecodeDerivedMetadata(value.at("metadata"));
        for (const auto& encoded : value.at("subAssets"))
        {
            AssetGeneratedSubAsset subAsset;
            subAsset.Id = AssetId::Parse(encoded.at("id").get<std::string>());
            subAsset.Type = AssetTypeId::Parse(encoded.at("type").get<std::string>());
            subAsset.Key = encoded.at("key").get<std::string>();
            subAsset.Name = encoded.at("name").get<std::string>();
            subAsset.Bytes = ToBytes(encoded.at("bytes").get_binary());
            for (const auto& dependency : encoded.at("dependencies"))
                subAsset.AssetDependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
            subAsset.Metadata = DecodeDerivedMetadata(encoded.at("metadata"));
            output.SubAssets.push_back(std::move(subAsset));
        }
        if (const auto primaryType = value.value("primaryType", std::string{}); !primaryType.empty())
            output.PrimaryType = AssetTypeId::Parse(primaryType);
        return output;
    }
} // namespace Keire::Detail
