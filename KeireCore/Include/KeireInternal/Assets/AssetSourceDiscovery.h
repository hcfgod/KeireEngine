#pragma once

#include "KeireInternal/FileSystem.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace Keire::Detail
{
    struct AssetFileSignature final
    {
        std::uint64_t Modified = 0;
        std::uintmax_t Size = 0;
        std::uint64_t MetadataModified = 0;
        std::uintmax_t MetadataSize = 0;

        [[nodiscard]] bool operator==(const AssetFileSignature&) const noexcept = default;
    };

    // Native save dialogs and external writers can briefly publish files that are not assets yet.
    class AssetSourceDiscovery final
    {
      public:
        using Clock = std::chrono::steady_clock;

        void BeginScan()
        {
            for (auto& [path, candidate] : m_Candidates)
                candidate.Seen = false;
        }

        [[nodiscard]] bool Ready(const std::filesystem::path& path, const AnchoredFileSignature signature,
                                 const Clock::time_point now, const Clock::duration debounce)
        {
            auto [entry, inserted] = m_Candidates.try_emplace(path, Candidate{signature, now, true});
            auto& candidate = entry->second;
            if (!inserted && candidate.Signature != signature)
                candidate = {signature, now, true};
            candidate.Seen = true;
            return now - candidate.Since >= debounce;
        }

        void EndScan()
        {
            std::erase_if(m_Candidates, [](const auto& entry) { return !entry.second.Seen; });
        }

      private:
        struct Candidate final
        {
            AnchoredFileSignature Signature;
            Clock::time_point Since;
            bool Seen = false;
        };

        std::unordered_map<std::filesystem::path, Candidate> m_Candidates;
    };
} // namespace Keire::Detail
