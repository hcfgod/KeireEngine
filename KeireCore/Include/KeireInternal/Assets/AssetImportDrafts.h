#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <map>

namespace Keire::Internal
{
    // A clean editor follows external imports; a dirty editor keeps its baseline
    // so the caller can show a conflict instead of silently replacing user edits.
    [[nodiscard]] inline bool SynchronizeImportSettingsDraft(const AssetImportSettings& imported,
                                                             AssetImportSettings& baseline, AssetImportSettings& draft)
    {
        if (imported == baseline)
            return false;
        if (draft != baseline)
            return true;
        baseline = imported;
        draft = imported;
        return false;
    }

    class AssetImportDrafts final
    {
      public:
        struct Entry final
        {
            AssetImportSettings Values;
            bool Dirty = false;
            AssetImportSettings Baseline;
        };

        [[nodiscard]] Entry& Select(const AssetId asset, const AssetImportSettings& imported)
        {
            auto& entry = m_Entries[asset];
            if (!entry.Dirty)
            {
                entry.Values = imported;
                entry.Baseline = imported;
            }
            return entry;
        }

        void Revert(const AssetId asset, const AssetImportSettings& imported)
        {
            m_Entries[asset] = {imported, false, imported};
        }

      private:
        std::map<AssetId, Entry> m_Entries;
    };
} // namespace Keire::Internal
