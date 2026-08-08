#include "KeireClient/Editor/VfxNodeCatalog.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool IsSearchCharacter(const unsigned char character) noexcept
        {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character >= 0x80;
        }

        [[nodiscard]] std::string NormalizeSearchText(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            bool separated = true;
            for (const char input : value)
            {
                const auto character = static_cast<unsigned char>(input);
                if (IsSearchCharacter(character))
                {
                    result.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                                          : static_cast<char>(character));
                    separated = false;
                }
                else if (!separated)
                {
                    result.push_back(' ');
                    separated = true;
                }
            }
            if (!result.empty() && result.back() == ' ')
                result.pop_back();
            return result;
        }

        [[nodiscard]] std::vector<std::string_view> SearchTokens(const std::string_view normalized)
        {
            std::vector<std::string_view> result;
            std::size_t offset = 0;
            while (offset < normalized.size())
            {
                const auto separator = normalized.find(' ', offset);
                const auto end = separator == std::string_view::npos ? normalized.size() : separator;
                result.push_back(normalized.substr(offset, end - offset));
                offset = end + 1;
            }
            return result;
        }

        [[nodiscard]] bool HasWordPrefix(const std::string_view field, const std::string_view token) noexcept
        {
            std::size_t offset = 0;
            while (offset < field.size())
            {
                if (field.substr(offset).starts_with(token))
                    return true;
                const auto separator = field.find(' ', offset);
                if (separator == std::string_view::npos)
                    break;
                offset = separator + 1;
            }
            return false;
        }

        [[nodiscard]] std::int64_t FieldScore(const std::string_view field, const std::string_view token,
                                              const std::int64_t exact, const std::int64_t prefix,
                                              const std::int64_t wordPrefix, const std::int64_t contains) noexcept
        {
            if (field == token)
                return exact;
            if (field.starts_with(token))
                return prefix;
            if (HasWordPrefix(field, token))
                return wordPrefix;
            if (field.find(token) != std::string_view::npos)
                return contains;
            return 0;
        }

        template <typename Range>
        [[nodiscard]] std::int64_t RangeScore(const Range& values, const std::string_view token,
                                              const std::int64_t exact, const std::int64_t prefix,
                                              const std::int64_t wordPrefix, const std::int64_t contains) noexcept
        {
            std::int64_t result = 0;
            for (const auto& value : values)
                result = std::max(result, FieldScore(value, token, exact, prefix, wordPrefix, contains));
            return result;
        }

        [[nodiscard]] std::string ValueTypeSearchText(const Keire::VfxValueType type)
        {
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return "boolean bool";
            case Keire::VfxValueType::Integer:
                return "integer int";
            case Keire::VfxValueType::Scalar:
                return "scalar float number";
            case Keire::VfxValueType::Vector2:
                return "vector2 vector 2 float2 vec2";
            case Keire::VfxValueType::Vector3:
                return "vector3 vector 3 float3 vec3";
            case Keire::VfxValueType::Color:
                return "color colour float4 rgba";
            case Keire::VfxValueType::Texture:
                return "texture image sprite";
            case Keire::VfxValueType::Mesh:
                return "mesh geometry";
            case Keire::VfxValueType::Asset:
                return "asset resource";
            case Keire::VfxValueType::ParticleStream:
                return "particle stream flow execution";
            case Keire::VfxValueType::UnsignedInteger:
                return "unsigned integer uint";
            case Keire::VfxValueType::Vector4:
                return "vector4 vector 4 float4 vec4";
            case Keire::VfxValueType::Quaternion:
                return "quaternion rotation";
            case Keire::VfxValueType::Matrix:
                return "matrix transform float4x4";
            case Keire::VfxValueType::Curve:
                return "curve animation curve";
            case Keire::VfxValueType::Gradient:
                return "gradient color ramp";
            case Keire::VfxValueType::ScalarRange:
                return "scalar range float range";
            case Keire::VfxValueType::IntegerRange:
                return "integer range int range";
            case Keire::VfxValueType::UnsignedIntegerRange:
                return "unsigned integer range uint range";
            case Keire::VfxValueType::Vector2Range:
                return "vector2 range float2 range";
            case Keire::VfxValueType::Vector3Range:
                return "vector3 range float3 range";
            case Keire::VfxValueType::Vector4Range:
                return "vector4 range float4 range";
            case Keire::VfxValueType::ColorRange:
                return "color range gradient rgba range";
            case Keire::VfxValueType::Texture2DArray:
                return "texture2d array texture array";
            case Keire::VfxValueType::Texture3D:
                return "texture3d volume texture";
            case Keire::VfxValueType::TextureCube:
                return "texture cube cubemap";
            case Keire::VfxValueType::Buffer:
                return "buffer data";
            case Keire::VfxValueType::PointCache:
                return "point cache";
            case Keire::VfxValueType::SignedDistanceField:
                return "signed distance field sdf";
            }
            throw std::invalid_argument("VFX node catalog value type is unsupported.");
        }

        [[nodiscard]] bool SupportsBackend(const VfxNodeCatalogEntry& entry, const Keire::VfxBackend backend) noexcept
        {
            switch (backend)
            {
            case Keire::VfxBackend::Cpu:
                return entry.CpuSupported;
            case Keire::VfxBackend::Gpu:
                return entry.GpuSupported;
            }
            return false;
        }

        [[nodiscard]] bool MatchesContext(const VfxNodeCatalogEntry& entry,
                                          const std::optional<Keire::VfxContextType> context) noexcept
        {
            return !context || entry.Contexts.empty() ||
                   std::ranges::find(entry.Contexts, *context) != entry.Contexts.end();
        }

        [[nodiscard]] bool MatchesPin(const VfxNodeCatalogEntry& entry, const VfxNodeCatalogQuery& query) noexcept
        {
            if (!query.PinType)
                return true;
            const auto contains = [&](const std::vector<Keire::VfxValueType>& types)
            { return std::ranges::find(types, *query.PinType) != types.end(); };
            if (!query.PinDirection)
                return contains(entry.InputTypes) || contains(entry.OutputTypes);
            return *query.PinDirection == VfxNodeCatalogPinDirection::Input ? contains(entry.InputTypes)
                                                                            : contains(entry.OutputTypes);
        }

        void ValidateEntry(const VfxNodeCatalogEntry& entry)
        {
            if (entry.Id.empty() || entry.Name.empty() || entry.Category.empty() || entry.TypeName.empty())
                throw std::invalid_argument("VFX node catalog entries require an ID, name, category, and type name.");
            if ((!entry.CpuSupported && !entry.GpuSupported) &&
                entry.Support != VfxNodeCatalogSupportLevel::Unsupported)
            {
                throw std::invalid_argument("A supported VFX node catalog entry requires at least one backend.");
            }
            if (entry.Support == VfxNodeCatalogSupportLevel::Unsupported && entry.DisabledReason.empty())
                throw std::invalid_argument("An unsupported VFX node catalog entry requires a disabled reason.");
            if (std::ranges::any_of(entry.Aliases, [](const std::string& value) { return value.empty(); }) ||
                std::ranges::any_of(entry.Keywords, [](const std::string& value) { return value.empty(); }))
            {
                throw std::invalid_argument("VFX node catalog aliases and keywords cannot be empty.");
            }
            if (std::ranges::any_of(entry.InputTypes, [](const Keire::VfxValueType type)
                                    { return type > Keire::VfxValueType::SignedDistanceField; }) ||
                std::ranges::any_of(entry.OutputTypes, [](const Keire::VfxValueType type)
                                    { return type > Keire::VfxValueType::SignedDistanceField; }) ||
                std::ranges::any_of(entry.Contexts, [](const Keire::VfxContextType context)
                                    { return context > Keire::VfxContextType::Event; }))
            {
                throw std::invalid_argument("VFX node catalog compatibility metadata is invalid.");
            }
        }

    } // namespace

    VfxNodeSearchIndex::SearchData VfxNodeSearchIndex::BuildSearchData(const VfxNodeCatalogEntry& entry)
    {
        SearchData result;
        result.Name = NormalizeSearchText(entry.Name);
        result.Category = NormalizeSearchText(entry.Category);
        result.TypeName = NormalizeSearchText(entry.TypeName);
        result.Description = NormalizeSearchText(entry.Description);
        result.Aliases.reserve(entry.Aliases.size());
        for (const auto& alias : entry.Aliases)
            result.Aliases.push_back(NormalizeSearchText(alias));
        result.Keywords.reserve(entry.Keywords.size());
        for (const auto& keyword : entry.Keywords)
            result.Keywords.push_back(NormalizeSearchText(keyword));

        std::vector<Keire::VfxValueType> types = entry.InputTypes;
        types.insert(types.end(), entry.OutputTypes.begin(), entry.OutputTypes.end());
        std::ranges::sort(types);
        const auto unique = std::ranges::unique(types);
        types.erase(unique.begin(), unique.end());
        result.ValueTypes.reserve(types.size());
        for (const auto type : types)
            result.ValueTypes.push_back(ValueTypeSearchText(type));
        result.Support = NormalizeSearchText(VfxNodeCatalogSupportBadge(entry));
        result.DisabledReason = NormalizeSearchText(entry.DisabledReason);
        return result;
    }

    std::optional<std::int64_t> VfxNodeSearchIndex::SearchScore(const SearchData& entry,
                                                                const std::string_view normalizedQuery)
    {
        if (normalizedQuery.empty())
            return 0;

        std::int64_t result = 0;
        for (const auto token : SearchTokens(normalizedQuery))
        {
            std::int64_t tokenScore = 0;
            tokenScore = std::max(tokenScore, FieldScore(entry.Name, token, 12'000, 11'000, 10'500, 6'000));
            tokenScore = std::max(tokenScore, RangeScore(entry.Aliases, token, 10'000, 9'500, 9'000, 5'500));
            tokenScore = std::max(tokenScore, FieldScore(entry.TypeName, token, 8'500, 8'000, 7'800, 4'500));
            tokenScore = std::max(tokenScore, RangeScore(entry.ValueTypes, token, 8'000, 7'600, 7'400, 4'200));
            tokenScore = std::max(tokenScore, FieldScore(entry.Category, token, 7'500, 7'000, 6'800, 4'000));
            tokenScore = std::max(tokenScore, RangeScore(entry.Keywords, token, 6'500, 6'000, 5'800, 3'500));
            tokenScore = std::max(tokenScore, FieldScore(entry.Support, token, 5'000, 4'500, 4'300, 2'500));
            tokenScore = std::max(tokenScore, FieldScore(entry.DisabledReason, token, 2'000, 1'800, 1'700, 1'200));
            tokenScore = std::max(tokenScore, FieldScore(entry.Description, token, 1'000, 900, 850, 600));
            if (tokenScore == 0)
                return std::nullopt;
            result += tokenScore;
        }

        if (entry.Name == normalizedQuery)
            result += 50'000;
        else if (entry.Name.starts_with(normalizedQuery))
            result += 25'000;
        else if (std::ranges::find(entry.Aliases, normalizedQuery) != entry.Aliases.end())
            result += 20'000;
        return result;
    }

    std::size_t VfxNodeSearchIndex::Add(VfxNodeCatalogEntry entry)
    {
        ValidateEntry(entry);
        if (Find(entry.Id))
            throw std::invalid_argument("A VFX node catalog entry with this stable ID is already registered.");
        auto search = BuildSearchData(entry);
        m_Entries.push_back(std::move(entry));
        try
        {
            m_SearchData.push_back(std::move(search));
        }
        catch (...)
        {
            m_Entries.pop_back();
            throw;
        }
        return m_Entries.size() - 1;
    }

    void VfxNodeSearchIndex::Clear() noexcept
    {
        m_Entries.clear();
        m_SearchData.clear();
    }

    const VfxNodeCatalogEntry* VfxNodeSearchIndex::Find(const std::string_view id) const noexcept
    {
        const auto found = std::ranges::find(m_Entries, id, &VfxNodeCatalogEntry::Id);
        return found == m_Entries.end() ? nullptr : std::addressof(*found);
    }

    bool VfxNodeSearchIndex::SetUsage(const std::string_view id, const VfxNodeCatalogUsageMetadata usage) noexcept
    {
        const auto found = std::ranges::find(m_Entries, id, &VfxNodeCatalogEntry::Id);
        if (found == m_Entries.end())
            return false;
        found->Usage = usage;
        return true;
    }

    std::vector<VfxNodeCatalogMatch> VfxNodeSearchIndex::Search(const VfxNodeCatalogQuery& query) const
    {
        if (query.PinDirection && !query.PinType)
            throw std::invalid_argument("A VFX node catalog pin direction filter requires a pin type.");

        const auto normalizedQuery = NormalizeSearchText(query.Text);
        std::vector<VfxNodeCatalogMatch> result;
        result.reserve(m_Entries.size());
        for (std::size_t index = 0; index < m_Entries.size(); ++index)
        {
            const auto& entry = m_Entries[index];
            if ((!query.IncludeDisabled && !entry.Enabled()) || !MatchesContext(entry, query.Context) ||
                !MatchesPin(entry, query))
            {
                continue;
            }
            const bool backendSupported = !query.Backend || SupportsBackend(entry, *query.Backend);
            if (!backendSupported && !query.IncludeUnsupportedBackend)
                continue;
            const auto score = SearchScore(m_SearchData[index], normalizedQuery);
            if (score)
                result.push_back({index, *score, backendSupported});
        }

        std::ranges::sort(result,
                          [&](const VfxNodeCatalogMatch& left, const VfxNodeCatalogMatch& right)
                          {
                              const auto& leftEntry = m_Entries[left.EntryIndex];
                              const auto& rightEntry = m_Entries[right.EntryIndex];
                              if (left.Score != right.Score)
                                  return left.Score > right.Score;
                              if (left.BackendSupported != right.BackendSupported)
                                  return left.BackendSupported;
                              if (leftEntry.Usage.Favorite != rightEntry.Usage.Favorite)
                                  return leftEntry.Usage.Favorite;
                              if (leftEntry.Usage.LastUsedSequence != rightEntry.Usage.LastUsedSequence)
                                  return leftEntry.Usage.LastUsedSequence > rightEntry.Usage.LastUsedSequence;
                              if (leftEntry.Usage.UseCount != rightEntry.Usage.UseCount)
                                  return leftEntry.Usage.UseCount > rightEntry.Usage.UseCount;
                              if (leftEntry.SortPriority != rightEntry.SortPriority)
                                  return leftEntry.SortPriority > rightEntry.SortPriority;
                              if (m_SearchData[left.EntryIndex].Name != m_SearchData[right.EntryIndex].Name)
                                  return m_SearchData[left.EntryIndex].Name < m_SearchData[right.EntryIndex].Name;
                              return leftEntry.Id < rightEntry.Id;
                          });
        return result;
    }

    VfxNodeCatalogEntry BuildVfxNodeCatalogEntry(const Keire::VfxNodeDescriptor& descriptor)
    {
        VfxNodeCatalogEntry result;
        result.Id = descriptor.TypeId.Value;
        result.Name = descriptor.Label;
        result.Category = descriptor.Category;
        result.Aliases = descriptor.Synonyms;
        result.Contexts = descriptor.ValidContexts;
        switch (descriptor.Class)
        {
        case Keire::VfxNodeClass::Operator:
            result.TypeName = "Operator";
            break;
        case Keire::VfxNodeClass::Parameter:
            result.TypeName = "Parameter";
            break;
        case Keire::VfxNodeClass::Constant:
            result.TypeName = "Constant";
            break;
        case Keire::VfxNodeClass::Attribute:
            result.TypeName = "Attribute";
            break;
        case Keire::VfxNodeClass::Subgraph:
            result.TypeName = "Subgraph";
            break;
        case Keire::VfxNodeClass::Block:
            result.TypeName = "Block";
            break;
        case Keire::VfxNodeClass::Context:
            result.TypeName = "Context";
            break;
        case Keire::VfxNodeClass::Output:
            result.TypeName = "Output";
            break;
        }

        const auto addType = [](std::vector<Keire::VfxValueType>& types, const Keire::VfxValueType type)
        {
            if (std::ranges::find(types, type) == types.end())
                types.push_back(type);
        };
        for (const auto& pin : descriptor.Pins)
        {
            auto& types = pin.Input ? result.InputTypes : result.OutputTypes;
            addType(types, pin.Type);
            for (const auto accepted : pin.AcceptedTypes)
                addType(types, accepted);
        }

        switch (descriptor.BackendTier)
        {
        case Keire::VfxNodeBackendTier::CpuOnly:
            result.GpuSupported = false;
            break;
        case Keire::VfxNodeBackendTier::CpuAndGpu:
            break;
        case Keire::VfxNodeBackendTier::GpuRequired:
            result.CpuSupported = false;
            break;
        }

        switch (descriptor.SupportTier)
        {
        case Keire::VfxNodeSupportTier::Supported:
            break;
        case Keire::VfxNodeSupportTier::GpuRequired:
            result.CpuSupported = false;
            result.GpuSupported = true;
            result.SupportQualifier = "GPU Required";
            break;
        case Keire::VfxNodeSupportTier::KeireEquivalent:
            result.SupportQualifier = "Kéire Equivalent";
            break;
        case Keire::VfxNodeSupportTier::Disabled:
            result.CpuSupported = false;
            result.GpuSupported = false;
            result.Support = VfxNodeCatalogSupportLevel::Unsupported;
            result.DisabledReason = descriptor.DisabledReason;
            break;
        }
        return result;
    }

    std::string VfxNodeCatalogSupportBadge(const VfxNodeCatalogEntry& entry)
    {
        if (entry.Support == VfxNodeCatalogSupportLevel::Unsupported || (!entry.CpuSupported && !entry.GpuSupported))
        {
            return "Unsupported";
        }

        std::string result;
        if (entry.CpuSupported && entry.GpuSupported)
            result = "CPU + GPU";
        else if (entry.CpuSupported)
            result = "CPU";
        else
            result = "GPU";
        if (!entry.SupportQualifier.empty())
            result += " / " + entry.SupportQualifier;
        else if (entry.Support == VfxNodeCatalogSupportLevel::Experimental)
            result += " / Experimental";
        return result;
    }

    std::string_view VfxGraphNodeKindLabel(const Keire::VfxGraphNodeKind kind) noexcept
    {
        switch (kind)
        {
        case Keire::VfxGraphNodeKind::Context:
            return "Context";
        case Keire::VfxGraphNodeKind::Module:
            return "Runtime Module";
        case Keire::VfxGraphNodeKind::Parameter:
            return "Blackboard Parameter";
        case Keire::VfxGraphNodeKind::CustomHlsl:
            return "Custom HLSL";
        case Keire::VfxGraphNodeKind::Operator:
            return "Operator";
        case Keire::VfxGraphNodeKind::Attribute:
            return "Attribute";
        case Keire::VfxGraphNodeKind::Subgraph:
            return "Subgraph";
        }
        return "Unknown";
    }
} // namespace KeireEditor
