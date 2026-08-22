#include "Keire/Vfx/VfxSystem.h"

#include <cstddef>
#include <utility>
#include <variant>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::size_t VfxValueOwnedBytes(const VfxParameterValue& value) noexcept
        {
            if (const auto* curve = std::get_if<Curve1D>(&value))
                return curve->Keys().size() * sizeof(CurveKey);
            if (const auto* gradient = std::get_if<ColorGradient>(&value))
                return gradient->Keys().size() * sizeof(ColorGradientKey);
            return 0;
        }

        [[nodiscard]] std::size_t VfxPropertyOwnedBytes(const VfxGraphProperty& property) noexcept
        {
            auto result = property.Name.capacity();
            if (const auto* text = std::get_if<std::string>(&property.Value))
                result += text->capacity();
            return result;
        }

        [[nodiscard]] std::size_t VfxPinOwnedBytes(const VfxGraphPin& pin) noexcept
        {
            return pin.Name.capacity() + pin.Semantic.capacity() +
                   (pin.DefaultValue ? VfxValueOwnedBytes(*pin.DefaultValue) : 0);
        }

        [[nodiscard]] std::size_t VfxBlockOwnedBytes(const VfxGraphBlock& block) noexcept
        {
            auto result = block.TypeId.Value.capacity() + block.Type.capacity() +
                          block.Pins.capacity() * sizeof(VfxGraphPin) +
                          block.Properties.capacity() * sizeof(VfxGraphProperty);
            for (const auto& pin : block.Pins)
                result += VfxPinOwnedBytes(pin);
            for (const auto& property : block.Properties)
                result += VfxPropertyOwnedBytes(property);
            return result;
        }

        [[nodiscard]] std::size_t VfxNodeOwnedBytes(const VfxGraphNode& node) noexcept
        {
            auto result =
                node.Type.capacity() + node.CustomHlsl.capacity() + node.TypeId.Value.capacity() +
                node.Pins.capacity() * sizeof(VfxGraphPin) + node.Properties.capacity() * sizeof(VfxGraphProperty) +
                node.ResolvedSignature.capacity() * sizeof(VfxValueType) +
                node.DynamicPinOrder.capacity() * sizeof(AssetId) + node.Blocks.capacity() * sizeof(VfxGraphBlock);
            for (const auto& pin : node.Pins)
                result += VfxPinOwnedBytes(pin);
            for (const auto& property : node.Properties)
                result += VfxPropertyOwnedBytes(property);
            for (const auto& block : node.Blocks)
                result += VfxBlockOwnedBytes(block);
            return result;
        }
    } // namespace

    VfxEffectAsset::VfxEffectAsset(const VfxEffectDefinition& definition)
        : m_Definition(MigrateVfxEffectToCurrentSchema(definition))
    {
        ValidateVfxEffect(m_Definition);
    }

    std::size_t VfxEffectAsset::ResidentBytes() const noexcept
    {
        auto result = sizeof(*this) + m_Definition.Name.capacity() +
                      m_Definition.Modules.capacity() * sizeof(VfxModuleDefinition) +
                      m_Definition.Systems.capacity() * sizeof(VfxGraphSystem) +
                      m_Definition.Blackboard.capacity() * sizeof(VfxBlackboardParameter);
        for (const auto& module : m_Definition.Modules)
        {
            if (const auto* size = std::get_if<VfxSizeOverLifetimeModule>(&module.Payload))
                result += size->Size.Keys().size() * sizeof(CurveKey);
            if (const auto* color = std::get_if<VfxColorOverLifetimeModule>(&module.Payload))
                result += color->Color.Keys().size() * sizeof(ColorGradientKey);
        }
        for (const auto& system : m_Definition.Systems)
        {
            result += system.Name.capacity() + system.Nodes.capacity() * sizeof(VfxGraphNode) +
                      system.Connections.capacity() * sizeof(VfxGraphConnection);
            for (const auto& node : system.Nodes)
                result += VfxNodeOwnedBytes(node);
            for (const auto& connection : system.Connections)
                result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        }
        for (const auto& parameter : m_Definition.Blackboard)
            result += parameter.Name.capacity() + VfxValueOwnedBytes(parameter.DefaultValue);
        return result;
    }

    VfxEffectDefinition VfxEffectAsset::DefaultDefinition()
    {
        constexpr auto emitter = AssetId(0x5646584445464155ULL, 1);
        VfxEffectDefinition definition;
        definition.EmitterId = emitter;
        definition.Modules = {
            {AssetId(0x5646584445464155ULL, 2), true, VfxEmissionRateModule{}},
            {AssetId(0x5646584445464155ULL, 3), true, VfxShapeModule{}},
            {AssetId(0x5646584445464155ULL, 4), true,
             VfxInitializeModule{1.0F, 1.0F, {-0.5F, 1.0F, -0.5F}, {0.5F, 2.0F, 0.5F}}},
            {AssetId(0x5646584445464155ULL, 5), true, VfxSizeOverLifetimeModule{}},
            {AssetId(0x5646584445464155ULL, 6), true, VfxColorOverLifetimeModule{}},
            {AssetId(0x5646584445464155ULL, 7), true, VfxRendererModule{}},
        };
        definition.ExecutionSource = VfxExecutionSource::LegacyModules;
        auto result = ConvertVfxEffectToGraph(definition);
        result.CompatibilityMode = VfxCompatibilityMode::NativeSchema4;
        return result;
    }

    Ref<VfxEffectAsset> VfxEffectAsset::Default() { return CreateRef<VfxEffectAsset>(DefaultDefinition()); }

} // namespace Keire
