#pragma once

#include "Keire/Api.h"
#include "Keire/Vfx/VfxSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t VfxSubgraphSchemaVersion = 1;
    inline constexpr std::size_t MaximumVfxSubgraphExpansionDepth = 16;

    enum class VfxSubgraphPurpose : std::uint8_t
    {
        Operator,
        Block,
        System
    };

    /// Stable typed boundary exported by a VFX subgraph. Node, Block, and Pin identify the internal endpoint while
    /// Input describes the callable subgraph boundary direction.
    struct VfxSubgraphPort
    {
        AssetId Id;
        std::string Name;
        VfxValueType Type = VfxValueType::Scalar;
        bool Input = true;
        AssetId Node;
        AssetId Block;
        AssetId Pin;

        [[nodiscard]] bool operator==(const VfxSubgraphPort&) const = default;
    };

    /// Schema-1 source definition for .keirevfxsubgraph assets. Operator graphs expose typed values, Block graphs
    /// declare compatible Contexts, and System graphs contain one complete executable particle or strip system.
    struct VfxSubgraphDefinition
    {
        std::uint32_t SchemaVersion = VfxSubgraphSchemaVersion;
        AssetId Id;
        std::string Name = "VFX Subgraph";
        VfxSubgraphPurpose Purpose = VfxSubgraphPurpose::Operator;
        VfxGraphSystem Graph;
        std::vector<VfxModuleDefinition> Modules;
        std::vector<VfxBlackboardParameter> Parameters;
        std::vector<VfxSubgraphPort> Ports;
        std::vector<VfxContextType> ValidContexts;

        [[nodiscard]] bool operator==(const VfxSubgraphDefinition&) const = default;
    };

    KEIRE_API void ValidateVfxSubgraph(const VfxSubgraphDefinition& definition);
    [[nodiscard]] KEIRE_API std::vector<AssetId> VfxSubgraphDependencies(const VfxSubgraphDefinition& definition);
    /// Creates a canonical free-standing call node for Operator or System Subgraphs.
    [[nodiscard]] KEIRE_API VfxGraphNode CreateVfxSubgraphNode(const VfxSubgraphDefinition& definition,
                                                               Vector2 editorPosition = {});
    /// Creates a canonical ordered call Block for a Block Subgraph.
    [[nodiscard]] KEIRE_API VfxGraphBlock CreateVfxSubgraphBlock(const VfxSubgraphDefinition& definition);
    [[nodiscard]] KEIRE_API bool HasVfxSubgraphCalls(const VfxEffectDefinition& definition) noexcept;
    /// Resolves and deterministically expands all Subgraph calls. Indirect cycles, missing assets, purpose mismatches,
    /// and excessive nesting throw before a partially expanded definition can escape.
    [[nodiscard]] KEIRE_API VfxEffectDefinition
    ExpandVfxSubgraphs(const VfxEffectDefinition& definition, const VfxSubgraphResolver& resolver,
                       std::size_t maximumDepth = MaximumVfxSubgraphExpansionDepth);

    class KEIRE_API VfxSubgraphAsset final : public Asset
    {
      public:
        explicit VfxSubgraphAsset(VfxSubgraphDefinition definition);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245564658ULL, 0x5355424752410001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const VfxSubgraphDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static VfxSubgraphDefinition DefaultDefinition();
        [[nodiscard]] static Ref<VfxSubgraphAsset> Default();
        [[nodiscard]] static Ref<VfxSubgraphAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const VfxSubgraphDefinition& definition);

      private:
        VfxSubgraphDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateVfxSubgraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateVfxSubgraphAssetDecoder();
} // namespace Keire
