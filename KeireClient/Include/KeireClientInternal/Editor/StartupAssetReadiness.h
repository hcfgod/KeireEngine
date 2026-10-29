#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <algorithm>
#include <span>
#include <vector>

namespace KeireEditor
{
    // Importer versions describe artifact compatibility, not just source freshness. Never expose a stale
    // executable artifact while its replacement is compiling; compatible unrelated assets remain available.
    [[nodiscard]] inline std::vector<Keire::AssetId>
    IncompatibleStartupAssets(const Keire::AssetDatabase& database,
                              const std::span<const Keire::AssetSourceRecord> records)
    {
        std::vector<Keire::AssetId> excluded;
        for (const auto& record : records)
        {
            if (record.ParentSource)
            {
                continue;
            }
            const auto importer = database.FindImporterForPath(record.RelativePath);
            const auto status = database.ImportStatus(record.Id).State;
            if (!importer || importer->Name != record.Importer || importer->Version != record.ImporterVersion ||
                (status != Keire::AssetImportState::Imported && status != Keire::AssetImportState::CacheHit))
            {
                excluded.push_back(record.Id);
                excluded.insert(excluded.end(), record.SubAssets.begin(), record.SubAssets.end());
            }
        }
        return excluded;
    }

    [[nodiscard]] inline Keire::AssetMountSpecification
    CompatibleStartupMount(const std::filesystem::path& catalog, const Keire::AssetDatabase& database,
                           const std::span<const Keire::AssetSourceRecord> records,
                           const Keire::AssetId pendingScene = {})
    {
        Keire::AssetMountSpecification result{catalog, 0, true};
        result.ExcludedAssets = IncompatibleStartupAssets(database, records);
        if (pendingScene)
        {
            result.ExcludedAssets.push_back(pendingScene);
        }
        result.AllowedAssets.emplace();
        for (const auto& record : records)
        {
            result.AllowedAssets->push_back(record.Id);
            result.AllowedAssets->insert(result.AllowedAssets->end(), record.SubAssets.begin(), record.SubAssets.end());
        }
        return result;
    }
} // namespace KeireEditor
