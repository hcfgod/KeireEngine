#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::size_t MaximumDocumentBytes = std::size_t{4} * 1024U * 1024U;
    inline constexpr std::size_t MaximumModules = 128;
    inline constexpr std::size_t MaximumSystems = 64;
    inline constexpr std::size_t MaximumGraphNodes = 4096;
    inline constexpr std::size_t MaximumGraphConnections = 16'384;
    inline constexpr std::size_t MaximumGraphRoutingPointsPerConnection = 64;
    inline constexpr std::size_t MaximumBlackboardParameters = 1024;
    inline constexpr std::size_t MaximumPortableCustomInstructions = 4096;
    inline constexpr std::size_t MaximumBursts = 32;
    inline constexpr std::size_t MaximumBurstCycles = 1024;
    inline constexpr std::size_t MaximumNameBytes = 128;
    inline constexpr std::uint32_t VfxEffectImporterVersion = 5;
    inline constexpr float MaximumAuthoredScalar = 1'000'000.0F;

    template <typename... Ts> struct Overloaded : Ts...
    {
        using Ts::operator()...;
    };
    template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    class VfxNodeCompileError final : public std::invalid_argument
    {
      public:
        VfxNodeCompileError(const AssetId node, const std::string& message)
            : std::invalid_argument(message), m_Node(node)
        {
        }

        [[nodiscard]] AssetId Node() const noexcept { return m_Node; }

      private:
        AssetId m_Node;
    };

    struct ModulePinSpecification
    {
        std::string_view Name;
        std::string_view Semantic;
        VfxValueType Type = VfxValueType::Scalar;
        VfxModuleProperty Property = VfxModuleProperty::None;
        VfxParameterValue DefaultValue = 0.0F;
    };

    struct LoweredPlan
    {
        AssetId System;
        VfxParticleDataType DataType = VfxParticleDataType::Particle;
        std::uint32_t ParticlesPerStrip = 1;
        std::string EventName;
        std::vector<VfxCompiledParameter> Parameters;
        std::vector<VfxCompiledModule> Modules;
        std::vector<VfxCompiledBinding> Bindings;
        std::vector<VfxCompiledValueInstruction> ValueInstructions;
        std::uint32_t ValueRegisterCount = 0;
        std::vector<VfxCompiledCustomInstruction> CustomInstructions;
        std::vector<VfxCompiledOperation> Operations;
    };

    [[nodiscard]] std::string_view ModuleTypeName(const VfxModulePayload& payload);
    [[nodiscard]] std::string_view ContextName(VfxContextType value);
    [[nodiscard]] std::string_view ValueTypeName(VfxValueType value);
    [[nodiscard]] bool UsesStrictSchemaFourCapabilities(const VfxEffectDefinition& definition) noexcept;

    [[nodiscard]] bool ValueMatchesType(VfxValueType type, const VfxParameterValue& value) noexcept;
    [[nodiscard]] bool IsPersistableValueType(VfxValueType type) noexcept;
    [[nodiscard]] bool IsPortableCustomValueType(VfxValueType type) noexcept;
    [[nodiscard]] bool ValidGraphProperties(std::span<const VfxGraphProperty> properties,
                                            std::size_t maximumStringBytes);
    [[nodiscard]] bool ValidTypeId(const VfxNodeTypeId& typeId) noexcept;

    [[nodiscard]] VfxContextType ModuleContext(const VfxModulePayload& payload) noexcept;
    [[nodiscard]] std::string_view ContextTypeId(VfxContextType context);
    [[nodiscard]] std::string_view ModuleTypeId(const VfxModulePayload& payload);
    [[nodiscard]] std::vector<ModulePinSpecification> ModulePinSpecifications(const VfxModulePayload& payload);
    [[nodiscard]] std::uint32_t ModuleDefinitionVersion(const VfxModulePayload& payload) noexcept;
    [[nodiscard]] std::uint32_t ContextOrder(VfxContextType context);
    [[nodiscard]] const VfxGraphPin* FindPin(const VfxGraphNode& node, bool input, VfxValueType type,
                                             std::string_view semantic) noexcept;
    [[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] AssetId AllocateDerivedId(AssetId source, std::uint64_t salt, std::set<AssetId>& used);

    [[nodiscard]] LoweredPlan LowerGraph(const VfxEffectDefinition& definition, const VfxGraphSystem& system,
                                         bool requirePublishable);
    [[nodiscard]] LoweredPlan LowerEffect(const VfxEffectDefinition& definition, const VfxGraphSystem* system);
    [[nodiscard]] std::vector<std::byte> BuildCanonicalIr(const VfxEffectDefinition& definition,
                                                          const LoweredPlan& plan);
    [[nodiscard]] std::uint64_t BuildStateLayoutHash(const VfxEffectDefinition& definition, const LoweredPlan& plan);
} // namespace Keire::Detail
