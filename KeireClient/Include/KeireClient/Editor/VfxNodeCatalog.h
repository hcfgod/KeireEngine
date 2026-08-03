#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class VfxNodeCatalogPinDirection : std::uint8_t
    {
        Input,
        Output
    };

    enum class VfxNodeCatalogSupportLevel : std::uint8_t
    {
        Supported,
        Experimental,
        Unsupported
    };

    struct VfxNodeCatalogUsageMetadata
    {
        bool Favorite = false;
        std::uint64_t LastUsedSequence = 0;
        std::uint32_t UseCount = 0;

        bool operator==(const VfxNodeCatalogUsageMetadata&) const = default;
    };

    /// Searchable presentation metadata for one creatable VFX graph node. Id is the stable key used by favorites,
    /// recents, and the caller's creation dispatch; display strings may change without invalidating that state.
    struct VfxNodeCatalogEntry
    {
        std::string Id;
        std::string Name;
        std::string Category;
        std::string TypeName;
        std::string Description;
        std::vector<std::string> Aliases;
        std::vector<std::string> Keywords;
        std::vector<Keire::VfxValueType> InputTypes;
        std::vector<Keire::VfxValueType> OutputTypes;
        /// Empty means the entry is context-neutral.
        std::vector<Keire::VfxContextType> Contexts;
        bool CpuSupported = true;
        bool GpuSupported = true;
        VfxNodeCatalogSupportLevel Support = VfxNodeCatalogSupportLevel::Supported;
        std::string SupportQualifier;
        std::string DisabledReason;
        VfxNodeCatalogUsageMetadata Usage;
        int SortPriority = 0;

        [[nodiscard]] bool Enabled() const noexcept
        {
            return Support != VfxNodeCatalogSupportLevel::Unsupported && DisabledReason.empty();
        }

        bool operator==(const VfxNodeCatalogEntry&) const = default;
    };

    struct VfxNodeCatalogQuery
    {
        std::string_view Text;
        std::optional<Keire::VfxContextType> Context;
        std::optional<Keire::VfxValueType> PinType;
        std::optional<VfxNodeCatalogPinDirection> PinDirection;
        std::optional<Keire::VfxBackend> Backend;
        bool IncludeDisabled = true;
        bool IncludeUnsupportedBackend = false;
    };

    struct VfxNodeCatalogMatch
    {
        std::size_t EntryIndex = 0;
        std::int64_t Score = 0;
        bool BackendSupported = true;

        bool operator==(const VfxNodeCatalogMatch&) const = default;
    };

    class VfxNodeSearchIndex final
    {
      public:
        [[nodiscard]] std::size_t Add(VfxNodeCatalogEntry entry);
        void Clear() noexcept;

        [[nodiscard]] std::span<const VfxNodeCatalogEntry> Entries() const noexcept { return m_Entries; }
        [[nodiscard]] const VfxNodeCatalogEntry* Find(std::string_view id) const noexcept;
        [[nodiscard]] bool SetUsage(std::string_view id, VfxNodeCatalogUsageMetadata usage) noexcept;
        [[nodiscard]] std::vector<VfxNodeCatalogMatch> Search(const VfxNodeCatalogQuery& query = {}) const;

      private:
        struct SearchData
        {
            std::string Name;
            std::string Category;
            std::string TypeName;
            std::string Description;
            std::vector<std::string> Aliases;
            std::vector<std::string> Keywords;
            std::vector<std::string> ValueTypes;
            std::string Support;
            std::string DisabledReason;
        };

        [[nodiscard]] static SearchData BuildSearchData(const VfxNodeCatalogEntry& entry);
        [[nodiscard]] static std::optional<std::int64_t> SearchScore(const SearchData& entry,
                                                                     std::string_view normalizedQuery);

        std::vector<VfxNodeCatalogEntry> m_Entries;
        std::vector<SearchData> m_SearchData;
    };

    /// Adapts the compiler-owned descriptor without making editor search metadata authoritative for execution.
    [[nodiscard]] VfxNodeCatalogEntry BuildVfxNodeCatalogEntry(const Keire::VfxNodeDescriptor& descriptor);
    [[nodiscard]] std::string VfxNodeCatalogSupportBadge(const VfxNodeCatalogEntry& entry);
    [[nodiscard]] std::string_view VfxGraphNodeKindLabel(Keire::VfxGraphNodeKind kind) noexcept;
} // namespace KeireEditor
