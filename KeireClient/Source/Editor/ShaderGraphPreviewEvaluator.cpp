#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    struct ShaderGraphPreviewEvaluator::Impl final
    {
      public:
        Impl(const Keire::ShaderGraphDefinition& definition,
             const std::span<const Keire::ShaderPropertyDefinition> properties,
             const std::span<const ShaderGraphPreviewTexture> textures)
            : m_Definition(definition), m_Properties(properties), m_Textures(textures),
              m_Visiting(definition.Nodes.size(), 0)
        {
            for (std::size_t index = 0; index < definition.Nodes.size(); ++index)
            {
                m_Nodes.emplace(definition.Nodes[index].Id, index);
                for (const auto& pin : definition.Nodes[index].Pins)
                    if (pin.Direction == Keire::ShaderGraphPinDirection::Output)
                    {
                        m_CacheIndices.emplace(std::pair{definition.Nodes[index].Id, pin.Id}, m_Cache.size());
                        m_Cache.emplace_back();
                    }
            }
            for (const auto& connection : definition.Connections)
                m_Incoming.emplace(std::pair{connection.Input.Node, connection.Input.Pin}, connection.Output);
            const auto master =
                std::ranges::find(definition.Nodes, Keire::ShaderGraphNodeKind::Master, &Keire::ShaderGraphNode::Kind);
            if (master != definition.Nodes.end())
                m_Master = std::addressof(*master);
        }

        void SetContext(const Keire::Vector2 uv, const Keire::Vector3 normal, const Keire::Vector3 position)
        {
            m_Uv = uv;
            m_Normal = Normalize(normal);
            m_Position = position;
            std::ranges::fill(m_Cache, std::nullopt);
            std::ranges::fill(m_Visiting, std::uint8_t{0});
        }

        [[nodiscard]] std::optional<Keire::Vector4> MasterInput(const std::string_view name,
                                                                const Keire::ShaderGraphValueType type)
        {
            if (!m_Master)
                return std::nullopt;
            const auto pin = std::ranges::find(m_Master->Pins, name, &Keire::ShaderGraphPin::Name);
            if (pin == m_Master->Pins.end())
                return std::nullopt;
            return Coerce(Input(*m_Master, *pin), type).Data;
        }

        [[nodiscard]] std::optional<PreviewMaterialSurface> MasterAttributes()
        {
            if (!m_Master)
                return std::nullopt;
            const auto pin = std::ranges::find(m_Master->Pins, "MaterialAttributes", &Keire::ShaderGraphPin::Name);
            if (pin == m_Master->Pins.end() || !m_Incoming.contains({m_Master->Id, pin->Id}))
                return std::nullopt;
            const auto value = Coerce(Input(*m_Master, *pin), Keire::ShaderGraphValueType::MaterialAttributes);
            return value.Surface;
        }

      private:
        [[nodiscard]] PreviewGraphValue Coerce(PreviewGraphValue value, const Keire::ShaderGraphValueType type) const
        {
            if (value.Type == type ||
                ((value.Type == Keire::ShaderGraphValueType::Color && type == Keire::ShaderGraphValueType::Vector4) ||
                 (value.Type == Keire::ShaderGraphValueType::Vector4 && type == Keire::ShaderGraphValueType::Color)))
            {
                value.Type = type;
                return value;
            }
            if ((value.Type == Keire::ShaderGraphValueType::Color ||
                 value.Type == Keire::ShaderGraphValueType::Vector4) &&
                type == Keire::ShaderGraphValueType::Vector3)
            {
                value.Type = type;
                value.Data.W = 0.0F;
                return value;
            }
            if (value.Type == Keire::ShaderGraphValueType::Vector3 &&
                (type == Keire::ShaderGraphValueType::Color || type == Keire::ShaderGraphValueType::Vector4))
            {
                value.Type = type;
                value.Data.W = 1.0F;
                return value;
            }
            if (value.Type != Keire::ShaderGraphValueType::Scalar || type == Keire::ShaderGraphValueType::Texture2D ||
                type == Keire::ShaderGraphValueType::MaterialAttributes || type == Keire::ShaderGraphValueType::Bsdf)
                throw std::invalid_argument("Preview graph value cannot be converted to the destination type.");
            value.Type = type;
            value.Data = {value.Data.X, value.Data.X, value.Data.X, value.Data.X};
            return value;
        }

        [[nodiscard]] PreviewGraphValue Input(const Keire::ShaderGraphNode& node, const Keire::ShaderGraphPin& pin)
        {
            const auto incoming = m_Incoming.find({node.Id, pin.Id});
            if (incoming == m_Incoming.end())
                return GraphValue(pin.DefaultValue, pin.Type);
            return Coerce(Evaluate(incoming->second), pin.Type);
        }

        [[nodiscard]] PreviewGraphValue NamedInput(const Keire::ShaderGraphNode& node, const std::string_view name)
        {
            const auto pin = std::ranges::find_if(
                node.Pins, [&](const Keire::ShaderGraphPin& candidate)
                { return candidate.Direction == Keire::ShaderGraphPinDirection::Input && candidate.Name == name; });
            if (pin == node.Pins.end())
                throw std::invalid_argument("Preview graph node is missing a canonical input.");
            return Input(node, *pin);
        }

        template <typename Operation>
        [[nodiscard]] PreviewGraphValue Binary(const Keire::ShaderGraphNode& node, Operation operation)
        {
            const auto left = Coerce(NamedInput(node, "A"), node.ValueType);
            const auto right = Coerce(NamedInput(node, "B"), node.ValueType);
            PreviewGraphValue result{.Type = node.ValueType};
            for (std::size_t index = 0; index < ComponentCount(node.ValueType); ++index)
                SetComponent(result.Data, index, operation(Component(left.Data, index), Component(right.Data, index)));
            return result;
        }

        template <typename Operation>
        [[nodiscard]] PreviewGraphValue Unary(const Keire::ShaderGraphNode& node, Operation operation)
        {
            const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
            PreviewGraphValue result{.Type = node.ValueType};
            for (std::size_t index = 0; index < ComponentCount(node.ValueType); ++index)
                SetComponent(result.Data, index, operation(Component(value.Data, index)));
            return result;
        }

        [[nodiscard]] PreviewGraphValue TextureSample(const PreviewGraphValue texture,
                                                      const Keire::Vector2 uv) const noexcept
        {
            if (texture.Texture)
                if (const auto sample = Detail::SampleShaderGraphPreviewTexture(m_Textures, texture.Texture, uv))
                    return {.Data = *sample, .Type = Keire::ShaderGraphValueType::Color};
            const bool alternate =
                ((static_cast<int>(std::floor(uv.X * 10.0F)) + static_cast<int>(std::floor(uv.Y * 10.0F))) & 1) != 0;
            const float checker = alternate ? 0.58F : 0.88F;
            Keire::Vector4 sample{checker, checker, checker, 1.0F};
            switch (texture.TextureSemantic)
            {
            case Keire::ShaderTextureSemantic::Normal:
                sample = {0.5F, 0.5F, 1.0F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::Metallic:
                sample = {0.1F, 0.1F, 0.1F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::Roughness:
                sample = {0.55F, 0.55F, 0.55F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::MetallicRoughness:
                sample = {0.1F, 0.55F, 0.0F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::Occlusion:
                sample = {1.0F, 1.0F, 1.0F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::Emissive:
                sample = {checker * 0.25F, checker * 0.25F, checker * 0.25F, 1.0F};
                break;
            case Keire::ShaderTextureSemantic::Generic:
            case Keire::ShaderTextureSemantic::BaseColor:
                break;
            }
            return {.Data = sample, .Type = Keire::ShaderGraphValueType::Color};
        }

        void QueueConnectedInput(std::vector<EvaluationTask>& tasks, const Keire::ShaderGraphNode& node,
                                 const std::string_view name)
        {
            const auto pin = std::ranges::find_if(
                node.Pins, [&](const Keire::ShaderGraphPin& candidate)
                { return candidate.Direction == Keire::ShaderGraphPinDirection::Input && candidate.Name == name; });
            if (pin == node.Pins.end())
                throw std::invalid_argument("Preview graph node is missing a canonical input.");
            const auto incoming = m_Incoming.find({node.Id, pin->Id});
            if (incoming != m_Incoming.end())
                tasks.push_back({incoming->second, EvaluationPhase::Begin});
        }

        [[nodiscard]] PreviewGraphValue Evaluate(const Keire::ShaderGraphEndpoint endpoint)
        {
            if (!m_Nodes.contains(endpoint.Node))
                throw std::invalid_argument("Preview graph connection references a missing node.");
            const auto rootCacheIndex = m_CacheIndices.find({endpoint.Node, endpoint.Pin});
            if (rootCacheIndex == m_CacheIndices.end())
                throw std::invalid_argument("Preview graph connection references a missing output pin.");
            if (const auto& cached = m_Cache[rootCacheIndex->second])
                return *cached;

            struct TraversalReset final
            {
                std::vector<std::uint8_t>& Visiting;
                bool Complete = false;

                ~TraversalReset()
                {
                    if (!Complete)
                        std::ranges::fill(Visiting, std::uint8_t{0});
                }
            };
            TraversalReset reset{m_Visiting};
            std::vector<EvaluationTask> tasks;
            tasks.reserve(m_Definition.Nodes.size());
            tasks.push_back({endpoint, EvaluationPhase::Begin});
            while (!tasks.empty())
            {
                const EvaluationTask task = tasks.back();
                tasks.pop_back();
                const auto located = m_Nodes.find(task.Endpoint.Node);
                if (located == m_Nodes.end())
                    throw std::invalid_argument("Preview graph connection references a missing node.");
                const auto cacheIndex = m_CacheIndices.find({task.Endpoint.Node, task.Endpoint.Pin});
                if (cacheIndex == m_CacheIndices.end())
                    throw std::invalid_argument("Preview graph connection references a missing output pin.");
                const auto index = located->second;
                const auto& node = m_Definition.Nodes[index];
                const auto outputPin = std::ranges::find(node.Pins, task.Endpoint.Pin, &Keire::ShaderGraphPin::Id);
                if (outputPin == node.Pins.end() || outputPin->Direction != Keire::ShaderGraphPinDirection::Output)
                    throw std::invalid_argument("Preview graph endpoint is not an output pin.");

                if (task.Phase == EvaluationPhase::Begin)
                {
                    if (m_Cache[cacheIndex->second])
                        continue;
                    if (m_Visiting[index])
                        throw std::invalid_argument("Preview graph contains an expression cycle.");
                    m_Visiting[index] = 1;
                    tasks.push_back({task.Endpoint, EvaluationPhase::Execute});

                    if (node.Kind == Keire::ShaderGraphNodeKind::StaticSwitch)
                    {
                        tasks.push_back({task.Endpoint, EvaluationPhase::SelectBranch});
                        QueueConnectedInput(tasks, node, "Condition");
                        continue;
                    }
                    if (node.Kind == Keire::ShaderGraphNodeKind::If)
                    {
                        tasks.push_back({task.Endpoint, EvaluationPhase::SelectBranch});
                        QueueConnectedInput(tasks, node, "Threshold");
                        QueueConnectedInput(tasks, node, "B");
                        QueueConnectedInput(tasks, node, "A");
                        continue;
                    }
                    if (node.Kind == Keire::ShaderGraphNodeKind::Custom ||
                        node.Kind == Keire::ShaderGraphNodeKind::FunctionCall ||
                        node.Kind == Keire::ShaderGraphNodeKind::Keyword ||
                        node.Kind == Keire::ShaderGraphNodeKind::Master)
                    {
                        continue;
                    }
                    for (auto pin = node.Pins.rbegin(); pin != node.Pins.rend(); ++pin)
                    {
                        if (pin->Direction != Keire::ShaderGraphPinDirection::Input)
                            continue;
                        const auto incoming = m_Incoming.find({node.Id, pin->Id});
                        if (incoming != m_Incoming.end())
                            tasks.push_back({incoming->second, EvaluationPhase::Begin});
                    }
                    continue;
                }

                if (task.Phase == EvaluationPhase::SelectBranch)
                {
                    if (node.Kind == Keire::ShaderGraphNodeKind::StaticSwitch)
                    {
                        const float condition =
                            Coerce(NamedInput(node, "Condition"), Keire::ShaderGraphValueType::Scalar).Data.X;
                        QueueConnectedInput(tasks, node, condition != 0.0F ? "True" : "False");
                    }
                    else
                    {
                        const float left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X;
                        const float right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X;
                        const float threshold =
                            Coerce(NamedInput(node, "Threshold"), Keire::ShaderGraphValueType::Scalar).Data.X;
                        QueueConnectedInput(tasks, node,
                                            std::abs(left - right) <= threshold ? "Equal"
                                            : left > right                      ? "Greater"
                                                                                : "Less");
                    }
                    continue;
                }

                m_Cache[cacheIndex->second] = EvaluateNode(node, outputPin);
                m_Visiting[index] = 0;
            }

            reset.Complete = true;
            const auto& result = m_Cache[rootCacheIndex->second];
            if (!result)
                throw std::logic_error("Preview graph traversal completed without evaluating its root.");
            return *result;
        }

        [[nodiscard]] PreviewGraphValue EvaluateNode(const Keire::ShaderGraphNode& node,
                                                     const std::vector<Keire::ShaderGraphPin>::const_iterator outputPin)
        {
            PreviewGraphValue result;
            switch (node.Kind)
            {
            case Keire::ShaderGraphNodeKind::Parameter:
            {
                const auto property =
                    std::ranges::find(m_Properties, node.Symbol, &Keire::ShaderPropertyDefinition::Name);
                if (node.ValueType == Keire::ShaderGraphValueType::Texture2D)
                {
                    result = GraphValue(node.Value, node.ValueType, node.TextureSemantic);
                    if (property != m_Properties.end())
                        result.Texture = property->DefaultTexture;
                }
                else if (property != m_Properties.end())
                {
                    result = {.Data = property->DefaultValue, .Type = node.ValueType};
                }
                else
                {
                    result = GraphValue(node.Value, node.ValueType);
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::Constant:
                result = GraphValue(node.Value, node.ValueType);
                break;
            case Keire::ShaderGraphNodeKind::TextureSample:
            {
                const auto texture = NamedInput(node, "Texture");
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                result = TextureSample(texture, {uv.X, uv.Y});
                if (outputPin->Name == "RGB")
                {
                    result.Type = Keire::ShaderGraphValueType::Vector3;
                    result.Data.W = 0.0F;
                }
                else if (outputPin->Type == Keire::ShaderGraphValueType::Scalar)
                {
                    const float component = outputPin->Name == "R"   ? result.Data.X
                                            : outputPin->Name == "G" ? result.Data.Y
                                            : outputPin->Name == "B" ? result.Data.Z
                                                                     : result.Data.W;
                    result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::UV:
                result = {.Data = {m_Uv.X, m_Uv.Y, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            case Keire::ShaderGraphNodeKind::UVTransform:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto tiling = Coerce(NamedInput(node, "Tiling"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto offset = Coerce(NamedInput(node, "Offset"), Keire::ShaderGraphValueType::Vector2).Data;
                result = {.Data = {uv.X * tiling.X + offset.X, uv.Y * tiling.Y + offset.Y, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            }
            case Keire::ShaderGraphNodeKind::NormalMap:
            {
                const auto sample = Coerce(NamedInput(node, "Sample"), Keire::ShaderGraphValueType::Color).Data;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                Keire::Vector3 tangentNormal{sample.X * 2.0F - 1.0F, sample.Y * 2.0F - 1.0F, sample.Z * 2.0F - 1.0F};
                tangentNormal.X *= scale;
                tangentNormal.Y *= scale;
                tangentNormal = Normalize(tangentNormal);
                const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
                const auto bitangent = Normalize(Cross(m_Normal, tangent), {0.0F, 1.0F, 0.0F});
                const auto world = Normalize(
                    {tangent.X * tangentNormal.X + bitangent.X * tangentNormal.Y + m_Normal.X * tangentNormal.Z,
                     tangent.Y * tangentNormal.X + bitangent.Y * tangentNormal.Y + m_Normal.Y * tangentNormal.Z,
                     tangent.Z * tangentNormal.X + bitangent.Z * tangentNormal.Y + m_Normal.Z * tangentNormal.Z},
                    m_Normal);
                result = {.Data = {world.X, world.Y, world.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::DetailNormal:
            {
                const auto base = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto detail = Coerce(NamedInput(node, "Detail"), Keire::ShaderGraphValueType::Vector3).Data;
                const float strength = std::clamp(
                    Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const auto baseNormal = Normalize({base.X, base.Y, base.Z}, m_Normal);
                const auto combined = Normalize({base.X + detail.X, base.Y + detail.Y, base.Z + detail.Z}, baseNormal);
                const auto blended = Normalize({std::lerp(baseNormal.X, combined.X, strength),
                                                std::lerp(baseNormal.Y, combined.Y, strength),
                                                std::lerp(baseNormal.Z, combined.Z, strength)},
                                               baseNormal);
                result = {.Data = {blended.X, blended.Y, blended.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::Parallax:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const float height = Coerce(NamedInput(node, "Height"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
                const auto bitangent = Normalize(Cross(m_Normal, tangent), {0.0F, 1.0F, 0.0F});
                constexpr Keire::Vector3 viewDirection{0.0F, 0.0F, 1.0F};
                const Keire::Vector3 tangentView{Dot(viewDirection, tangent), Dot(viewDirection, bitangent),
                                                 Dot(viewDirection, m_Normal)};
                const float offset = (height - 0.5F) * scale / std::max(std::abs(tangentView.Z), 0.05F);
                result = {.Data = {uv.X + tangentView.X * offset, uv.Y + tangentView.Y * offset, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            }
            case Keire::ShaderGraphNodeKind::Add:
                result = Binary(node, std::plus<float>{});
                break;
            case Keire::ShaderGraphNodeKind::Subtract:
                result = Binary(node, std::minus<float>{});
                break;
            case Keire::ShaderGraphNodeKind::Multiply:
                result = Binary(node, std::multiplies<float>{});
                break;
            case Keire::ShaderGraphNodeKind::Divide:
                result = Binary(node, [](const float left, const float right) { return SafeDivide(left, right); });
                break;
            case Keire::ShaderGraphNodeKind::Power:
            {
                const auto base = Coerce(NamedInput(node, "Base"), node.ValueType);
                const auto exponent = Coerce(NamedInput(node, "Exponent"), node.ValueType);
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                {
                    SetComponent(result.Data, component,
                                 std::pow(std::max(std::abs(Component(base.Data, component)), 1.0e-6F),
                                          Component(exponent.Data, component)));
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::Minimum:
                result = Binary(node, [](const float left, const float right) { return std::min(left, right); });
                break;
            case Keire::ShaderGraphNodeKind::Maximum:
                result = Binary(node, [](const float left, const float right) { return std::max(left, right); });
                break;
            case Keire::ShaderGraphNodeKind::Lerp:
            {
                const auto left = Coerce(NamedInput(node, "A"), node.ValueType);
                const auto right = Coerce(NamedInput(node, "B"), node.ValueType);
                const float factor = Coerce(NamedInput(node, "T"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                    SetComponent(result.Data, component,
                                 std::lerp(Component(left.Data, component), Component(right.Data, component), factor));
                break;
            }
            case Keire::ShaderGraphNodeKind::OneMinus:
                result = Unary(node, [](const float value) { return 1.0F - value; });
                break;
            case Keire::ShaderGraphNodeKind::Clamp:
                result = Unary(node, [](const float value) { return std::clamp(value, 0.0F, 1.0F); });
                break;
            case Keire::ShaderGraphNodeKind::Absolute:
                result = Unary(node, [](const float value) { return std::abs(value); });
                break;
            case Keire::ShaderGraphNodeKind::Floor:
                result = Unary(node, [](const float value) { return std::floor(value); });
                break;
            case Keire::ShaderGraphNodeKind::Ceiling:
                result = Unary(node, [](const float value) { return std::ceil(value); });
                break;
            case Keire::ShaderGraphNodeKind::Fraction:
                result = Unary(node, [](const float value) { return Fraction(value); });
                break;
            case Keire::ShaderGraphNodeKind::Sine:
                result = Unary(node, [](const float value) { return std::sin(value); });
                break;
            case Keire::ShaderGraphNodeKind::Cosine:
                result = Unary(node, [](const float value) { return std::cos(value); });
                break;
            case Keire::ShaderGraphNodeKind::Normalize:
            {
                const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
                float lengthSquared = 0.0F;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                    lengthSquared += Component(value.Data, component) * Component(value.Data, component);
                result.Type = node.ValueType;
                const float inverseLength = lengthSquared > 1.0e-12F ? 1.0F / std::sqrt(lengthSquared) : 0.0F;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                    SetComponent(result.Data, component, Component(value.Data, component) * inverseLength);
                break;
            }
            case Keire::ShaderGraphNodeKind::Length:
            {
                const auto value = NamedInput(node, "Value");
                float lengthSquared = 0.0F;
                for (std::size_t component = 0; component < ComponentCount(value.Type); ++component)
                    lengthSquared += Component(value.Data, component) * Component(value.Data, component);
                result = {.Data = {std::sqrt(lengthSquared), 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Dot:
            {
                const auto left = NamedInput(node, "A");
                const auto right = Coerce(NamedInput(node, "B"), left.Type);
                float dot = 0.0F;
                for (std::size_t component = 0; component < ComponentCount(left.Type); ++component)
                    dot += Component(left.Data, component) * Component(right.Data, component);
                result = {.Data = {dot, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Remap:
            {
                const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
                const auto inputMinimum = Coerce(NamedInput(node, "In Min"), node.ValueType);
                const auto inputMaximum = Coerce(NamedInput(node, "In Max"), node.ValueType);
                const auto outputMinimum = Coerce(NamedInput(node, "Out Min"), node.ValueType);
                const auto outputMaximum = Coerce(NamedInput(node, "Out Max"), node.ValueType);
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                {
                    const float factor =
                        SafeDivide(Component(value.Data, component) - Component(inputMinimum.Data, component),
                                   Component(inputMaximum.Data, component) - Component(inputMinimum.Data, component));
                    SetComponent(result.Data, component,
                                 std::lerp(Component(outputMinimum.Data, component),
                                           Component(outputMaximum.Data, component), factor));
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::SmoothStep:
            {
                const auto edgeMinimum = Coerce(NamedInput(node, "Edge Min"), node.ValueType);
                const auto edgeMaximum = Coerce(NamedInput(node, "Edge Max"), node.ValueType);
                const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                {
                    const float factor = std::clamp(
                        SafeDivide(Component(value.Data, component) - Component(edgeMinimum.Data, component),
                                   Component(edgeMaximum.Data, component) - Component(edgeMinimum.Data, component)),
                        0.0F, 1.0F);
                    SetComponent(result.Data, component, factor * factor * (3.0F - 2.0F * factor));
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::Step:
            {
                const auto edge = Coerce(NamedInput(node, "Edge"), node.ValueType);
                const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                    SetComponent(result.Data, component,
                                 Component(value.Data, component) >= Component(edge.Data, component) ? 1.0F : 0.0F);
                break;
            }
            case Keire::ShaderGraphNodeKind::Fresnel:
            {
                const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const float power = Coerce(NamedInput(node, "Power"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float reflectance = Coerce(NamedInput(node, "F0"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float facing =
                    std::clamp(Dot(Normalize({normal.X, normal.Y, normal.Z}), {0.0F, 0.0F, 1.0F}), 0.0F, 1.0F);
                const float value = std::clamp(
                    reflectance + (1.0F - reflectance) * std::pow(1.0F - facing, std::max(std::abs(power), 1.0e-4F)),
                    0.0F, 1.0F);
                result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::VertexColor:
                result = {.Data = {1.0F, 1.0F, 1.0F, 1.0F}, .Type = Keire::ShaderGraphValueType::Color};
                break;
            case Keire::ShaderGraphNodeKind::WorldPosition:
                result = {.Data = {m_Position.X, m_Position.Y, m_Position.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            case Keire::ShaderGraphNodeKind::WorldNormal:
                result = {.Data = {m_Normal.X, m_Normal.Y, m_Normal.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            case Keire::ShaderGraphNodeKind::ViewDirection:
                result = {.Data = {0.0F, 0.0F, 1.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            case Keire::ShaderGraphNodeKind::RotateUV:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
                const float rotation = Coerce(NamedInput(node, "Rotation"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float sine = std::sin(rotation);
                const float cosine = std::cos(rotation);
                const float localX = uv.X - center.X;
                const float localY = uv.Y - center.Y;
                result = {.Data = {center.X + localX * cosine - localY * sine,
                                   center.Y + localX * sine + localY * cosine, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            }
            case Keire::ShaderGraphNodeKind::SimpleNoise:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float detail = Coerce(NamedInput(node, "Detail"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Data = {MaterialNoise({uv.X, uv.Y}, scale, detail), 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Desaturate:
            {
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                const float amount = std::clamp(
                    Coerce(NamedInput(node, "Amount"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const float luminance = color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F;
                result = {.Data = {std::lerp(color.X, luminance, amount), std::lerp(color.Y, luminance, amount),
                                   std::lerp(color.Z, luminance, amount), color.W},
                          .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::Posterize:
            {
                const auto value = Coerce(NamedInput(node, "Value"), node.ValueType);
                const float steps = std::max(
                    std::abs(Coerce(NamedInput(node, "Steps"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0F);
                result.Type = node.ValueType;
                for (std::size_t component = 0; component < ComponentCount(node.ValueType); ++component)
                    SetComponent(result.Data, component, std::floor(Component(value.Data, component) * steps) / steps);
                break;
            }
            case Keire::ShaderGraphNodeKind::Round:
                result = Unary(node, [](const float value) { return std::round(value); });
                break;
            case Keire::ShaderGraphNodeKind::Truncate:
                result = Unary(node, [](const float value) { return std::trunc(value); });
                break;
            case Keire::ShaderGraphNodeKind::Sign:
                result =
                    Unary(node, [](const float value) { return value < 0.0F   ? -1.0F
                                                               : value > 0.0F ? 1.0F
                                                                              : 0.0F; });
                break;
            case Keire::ShaderGraphNodeKind::Modulo:
                result = Binary(node, [](const float left, const float right)
                                { return std::fmod(left, std::abs(right) > 1.0e-6F ? right : 1.0e-6F); });
                break;
            case Keire::ShaderGraphNodeKind::SquareRoot:
                result = Unary(node, [](const float value) { return std::sqrt(std::max(value, 0.0F)); });
                break;
            case Keire::ShaderGraphNodeKind::ReciprocalSquareRoot:
                result = Unary(node, [](const float value) { return 1.0F / std::sqrt(std::max(value, 1.0e-8F)); });
                break;
            case Keire::ShaderGraphNodeKind::Exponential2:
                result = Unary(node, [](const float value) { return std::exp2(value); });
                break;
            case Keire::ShaderGraphNodeKind::Logarithm2:
                result = Unary(node, [](const float value) { return std::log2(std::max(std::abs(value), 1.0e-8F)); });
                break;
            case Keire::ShaderGraphNodeKind::Tangent:
                result = Unary(node, [](const float value) { return std::tan(value); });
                break;
            case Keire::ShaderGraphNodeKind::ArcSine:
                result = Unary(node, [](const float value) { return std::asin(std::clamp(value, -1.0F, 1.0F)); });
                break;
            case Keire::ShaderGraphNodeKind::ArcCosine:
                result = Unary(node, [](const float value) { return std::acos(std::clamp(value, -1.0F, 1.0F)); });
                break;
            case Keire::ShaderGraphNodeKind::ArcTangent2:
                result = Binary(node, [](const float left, const float right) { return std::atan2(left, right); });
                break;
            case Keire::ShaderGraphNodeKind::DerivativeX:
            case Keire::ShaderGraphNodeKind::DerivativeY:
            case Keire::ShaderGraphNodeKind::FilterWidth:
                result = {.Type = node.ValueType};
                break;
            case Keire::ShaderGraphNodeKind::Cross:
            {
                const auto left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto value = Cross({left.X, left.Y, left.Z}, {right.X, right.Y, right.Z});
                result = {.Data = {value.X, value.Y, value.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::Distance:
            {
                const auto left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
                const float x = left.X - right.X;
                const float y = left.Y - right.Y;
                const float z = left.Z - right.Z;
                result = {.Data = {std::sqrt(x * x + y * y + z * z), 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Reflect:
            {
                const auto incident = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normalValue = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z});
                const float projection = 2.0F * Dot({incident.X, incident.Y, incident.Z}, normal);
                result = {.Data = {incident.X - projection * normal.X, incident.Y - projection * normal.Y,
                                   incident.Z - projection * normal.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::Refract:
            {
                const auto incidentValue =
                    Coerce(NamedInput(node, "Incident"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto incident = Normalize({incidentValue.X, incidentValue.Y, incidentValue.Z});
                const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z});
                const float ior = std::max(
                    std::abs(Coerce(NamedInput(node, "IOR"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
                const float eta = 1.0F / ior;
                const float noI = Dot(normal, incident);
                const float k = 1.0F - eta * eta * (1.0F - noI * noI);
                const auto refracted = k < 0.0F
                                           ? Keire::Vector3{}
                                           : Keire::Vector3{eta * incident.X - (eta * noI + std::sqrt(k)) * normal.X,
                                                            eta * incident.Y - (eta * noI + std::sqrt(k)) * normal.Y,
                                                            eta * incident.Z - (eta * noI + std::sqrt(k)) * normal.Z};
                result = {.Data = {refracted.X, refracted.Y, refracted.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::AppendVector:
            {
                const auto xyz = Coerce(NamedInput(node, "XYZ"), Keire::ShaderGraphValueType::Vector3).Data;
                const float w = Coerce(NamedInput(node, "W"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Data = {xyz.X, xyz.Y, xyz.Z, w}, .Type = Keire::ShaderGraphValueType::Vector4};
                break;
            }
            case Keire::ShaderGraphNodeKind::ComponentMask:
            {
                const auto value = Coerce(NamedInput(node, "Value"), Keire::ShaderGraphValueType::Vector4).Data;
                if (outputPin->Name == "R" || outputPin->Name == "G" || outputPin->Name == "B" ||
                    outputPin->Name == "A")
                {
                    const float component = outputPin->Name == "R"   ? value.X
                                            : outputPin->Name == "G" ? value.Y
                                            : outputPin->Name == "B" ? value.Z
                                                                     : value.W;
                    result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                }
                else
                {
                    result = {.Data = value, .Type = outputPin->Type};
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::UV1:
            case Keire::ShaderGraphNodeKind::ScreenPosition:
                result = {.Data = {m_Uv.X, m_Uv.Y, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            case Keire::ShaderGraphNodeKind::WorldTangent:
            {
                const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
                result = {.Data = {tangent.X, tangent.Y, tangent.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::CameraPosition:
                result = {.Data = {0.0F, 0.0F, 3.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            case Keire::ShaderGraphNodeKind::ObjectPosition:
                result = {.Data = {0.0F, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            case Keire::ShaderGraphNodeKind::Time:
            case Keire::ShaderGraphNodeKind::DeltaTime:
                result = {.Type = Keire::ShaderGraphValueType::Scalar};
                break;
            case Keire::ShaderGraphNodeKind::DepthFade:
            {
                const float distance = Coerce(NamedInput(node, "Distance"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float fadeDistance = std::max(
                    std::abs(Coerce(NamedInput(node, "Fade Distance"), Keire::ShaderGraphValueType::Scalar).Data.X),
                    1.0e-4F);
                result = {.Data = {std::clamp(distance / fadeDistance, 0.0F, 1.0F), 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Luminance:
            {
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                result = {.Data = {color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F, 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::HueShift:
            {
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                const float angle =
                    Coerce(NamedInput(node, "Shift"), Keire::ShaderGraphValueType::Scalar).Data.X * 6.28318530718F;
                constexpr auto inverseRootThree = 0.57735026919F;
                const Keire::Vector3 axis{inverseRootThree, inverseRootThree, inverseRootThree};
                const Keire::Vector3 source{color.X, color.Y, color.Z};
                const auto axisCross = Cross(axis, source);
                const float axisDot = Dot(axis, source);
                const float sine = std::sin(angle);
                const float cosine = std::cos(angle);
                result = {.Data = {std::max(source.X * cosine + axisCross.X * sine + axis.X * axisDot * (1.0F - cosine),
                                            0.0F),
                                   std::max(source.Y * cosine + axisCross.Y * sine + axis.Y * axisDot * (1.0F - cosine),
                                            0.0F),
                                   std::max(source.Z * cosine + axisCross.Z * sine + axis.Z * axisDot * (1.0F - cosine),
                                            0.0F),
                                   color.W},
                          .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::Checkerboard:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto colorA = Coerce(NamedInput(node, "Color A"), Keire::ShaderGraphValueType::Color).Data;
                const auto colorB = Coerce(NamedInput(node, "Color B"), Keire::ShaderGraphValueType::Color).Data;
                const auto scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto parity =
                    static_cast<std::int64_t>(std::floor(uv.X * scale.X) + std::floor(uv.Y * scale.Y)) & 1;
                result = {.Data = parity != 0 ? colorB : colorA, .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::VoronoiNoise:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float value = MaterialValueNoise({uv.X * scale, uv.Y * scale});
                result = {.Data = {outputPin->Name == "Cell" ? Fraction(value * 17.0F) : value, 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Panner:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto speed = Coerce(NamedInput(node, "Speed"), Keire::ShaderGraphValueType::Vector2).Data;
                const float time = Coerce(NamedInput(node, "Time"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Data = {uv.X + speed.X * time, uv.Y + speed.Y * time, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            }
            case Keire::ShaderGraphNodeKind::PolarCoordinates:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
                const float radialScale =
                    Coerce(NamedInput(node, "Radial Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float lengthScale =
                    Coerce(NamedInput(node, "Length Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float x = uv.X - center.X;
                const float y = uv.Y - center.Y;
                result = {.Data = {std::sqrt(x * x + y * y) * radialScale,
                                   Fraction(std::atan2(y, x) / 6.28318530718F + 0.5F) * lengthScale, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector2};
                break;
            }
            case Keire::ShaderGraphNodeKind::SphereMask:
            {
                const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Vector3).Data;
                const float x = first.X - second.X;
                const float y = first.Y - second.Y;
                const float z = first.Z - second.Z;
                const float radius = std::max(
                    std::abs(Coerce(NamedInput(node, "Radius"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-5F);
                const float hardness = std::max(
                    std::abs(Coerce(NamedInput(node, "Hardness"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0F);
                const float mask =
                    std::clamp((1.0F - std::sqrt(x * x + y * y + z * z) / radius) * hardness, 0.0F, 1.0F);
                result = {.Data = {mask, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::RadialGradient:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
                const float radius = Coerce(NamedInput(node, "Radius"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float density = std::max(
                    std::abs(Coerce(NamedInput(node, "Density"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
                const float x = uv.X - center.X;
                const float y = uv.Y - center.Y;
                const float gradient = std::clamp((radius - std::sqrt(x * x + y * y)) * density, 0.0F, 1.0F);
                result = {.Data = {gradient, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::LinearGradient:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto direction = Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
                const float offset = Coerce(NamedInput(node, "Offset"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float inverseLength =
                    1.0F / std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 1.0e-5F);
                const float gradient =
                    std::clamp((uv.X * direction.X + uv.Y * direction.Y) * inverseLength + offset, 0.0F, 1.0F);
                result = {.Data = {gradient, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Contrast:
            {
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                const float contrast = Coerce(NamedInput(node, "Contrast"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float pivot = Coerce(NamedInput(node, "Pivot"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Data = {color.X * contrast + pivot * (1.0F - contrast),
                                   color.Y * contrast + pivot * (1.0F - contrast),
                                   color.Z * contrast + pivot * (1.0F - contrast), color.W},
                          .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::Saturation:
            {
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                const float saturation =
                    Coerce(NamedInput(node, "Saturation"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float luminance = color.X * 0.2126F + color.Y * 0.7152F + color.Z * 0.0722F;
                result = {.Data = {std::lerp(luminance, color.X, saturation), std::lerp(luminance, color.Y, saturation),
                                   std::lerp(luminance, color.Z, saturation), color.W},
                          .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::BlendOverlay:
            {
                const auto base = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Color).Data;
                const auto blend = Coerce(NamedInput(node, "Blend"), Keire::ShaderGraphValueType::Color).Data;
                const float opacity = std::clamp(
                    Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const auto overlay = [](const float first, const float second)
                { return first < 0.5F ? 2.0F * first * second : 1.0F - 2.0F * (1.0F - first) * (1.0F - second); };
                result = {.Data = {std::lerp(base.X, overlay(base.X, blend.X), opacity),
                                   std::lerp(base.Y, overlay(base.Y, blend.Y), opacity),
                                   std::lerp(base.Z, overlay(base.Z, blend.Z), opacity), base.W},
                          .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::Blackbody:
            {
                const float kelvin =
                    std::clamp(Coerce(NamedInput(node, "Temperature"), Keire::ShaderGraphValueType::Scalar).Data.X,
                               1000.0F, 40000.0F) *
                    0.01F;
                const float red = kelvin <= 66.0F
                                      ? 1.0F
                                      : std::clamp(1.29293619F * std::pow(kelvin - 60.0F, -0.133204759F), 0.0F, 1.0F);
                const float green =
                    kelvin <= 66.0F
                        ? std::clamp(0.390081579F * std::log(std::max(kelvin, 1.0F)) - 0.631841444F, 0.0F, 1.0F)
                        : std::clamp(1.12989086F * std::pow(kelvin - 60.0F, -0.0755148492F), 0.0F, 1.0F);
                const float blue =
                    kelvin >= 66.0F ? 1.0F
                    : kelvin <= 19.0F
                        ? 0.0F
                        : std::clamp(0.543206811F * std::log(std::max(kelvin - 10.0F, 1.0F)) - 1.19625409F, 0.0F, 1.0F);
                result = {.Data = {red, green, blue, 1.0F}, .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::ReflectionVector:
            {
                const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z}, m_Normal);
                constexpr Keire::Vector3 incident{0.0F, 0.0F, -1.0F};
                const float projection = 2.0F * Dot(incident, normal);
                result = {.Data = {incident.X - projection * normal.X, incident.Y - projection * normal.Y,
                                   incident.Z - projection * normal.Z, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::FacingRatio:
            {
                const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z}, m_Normal);
                const float power = std::max(
                    std::abs(Coerce(NamedInput(node, "Power"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
                const float ratio = std::pow(std::clamp(1.0F - normal.Z, 0.0F, 1.0F), power);
                result = {.Data = {ratio, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Dither:
            {
                const float alpha = std::clamp(
                    Coerce(NamedInput(node, "Alpha"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const auto screen =
                    Coerce(NamedInput(node, "Screen Position"), Keire::ShaderGraphValueType::Vector2).Data;
                constexpr std::array thresholds{0.0F,    0.5F,    0.125F,  0.625F,  0.75F,   0.25F,   0.875F,  0.375F,
                                                0.1875F, 0.6875F, 0.0625F, 0.5625F, 0.9375F, 0.4375F, 0.8125F, 0.3125F};
                const auto x = static_cast<std::size_t>(std::abs(static_cast<int>(screen.X * 256.0F))) & 3U;
                const auto y = static_cast<std::size_t>(std::abs(static_cast<int>(screen.Y * 256.0F))) & 3U;
                result = {.Data = {alpha >= thresholds[y * 4U + x] ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::GradientNoise:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Data = {MaterialNoise({uv.X, uv.Y}, scale, 0.65F), 0.0F, 0.0F, 0.0F},
                          .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::Wave:
            {
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                const auto direction = Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
                const float frequency =
                    Coerce(NamedInput(node, "Frequency"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float phase = Coerce(NamedInput(node, "Phase"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float inverseLength =
                    1.0F / std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 1.0e-5F);
                const float wave =
                    std::sin((uv.X * direction.X + uv.Y * direction.Y) * inverseLength * frequency * 6.28318530718F +
                             phase) *
                        0.5F +
                    0.5F;
                result = {.Data = {wave, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::TriplanarSample:
            {
                const auto texture = NamedInput(node, "Texture");
                const auto position = Coerce(NamedInput(node, "Position"), Keire::ShaderGraphValueType::Vector3).Data;
                const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float sharpness = std::max(
                    std::abs(Coerce(NamedInput(node, "Blend Sharpness"), Keire::ShaderGraphValueType::Scalar).Data.X),
                    1.0F);
                auto weights = Keire::Vector3{std::pow(std::abs(normalValue.X), sharpness),
                                              std::pow(std::abs(normalValue.Y), sharpness),
                                              std::pow(std::abs(normalValue.Z), sharpness)};
                const float total = std::max(weights.X + weights.Y + weights.Z, 1.0e-5F);
                weights = {weights.X / total, weights.Y / total, weights.Z / total};
                const auto xSample = TextureSample(texture, {position.Z * scale, position.Y * scale}).Data;
                const auto ySample = TextureSample(texture, {position.X * scale, position.Z * scale}).Data;
                const auto zSample = TextureSample(texture, {position.X * scale, position.Y * scale}).Data;
                const Keire::Vector4 sample{xSample.X * weights.X + ySample.X * weights.Y + zSample.X * weights.Z,
                                            xSample.Y * weights.X + ySample.Y * weights.Y + zSample.Y * weights.Z,
                                            xSample.Z * weights.X + ySample.Z * weights.Y + zSample.Z * weights.Z,
                                            xSample.W * weights.X + ySample.W * weights.Y + zSample.W * weights.Z};
                if (outputPin->Name == "RGB")
                    result = {.Data = sample, .Type = Keire::ShaderGraphValueType::Vector3};
                else if (outputPin->Type == Keire::ShaderGraphValueType::Scalar)
                {
                    const float component = outputPin->Name == "R"   ? sample.X
                                            : outputPin->Name == "G" ? sample.Y
                                            : outputPin->Name == "B" ? sample.Z
                                                                     : sample.W;
                    result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                }
                else
                    result = {.Data = sample, .Type = Keire::ShaderGraphValueType::Color};
                break;
            }
            case Keire::ShaderGraphNodeKind::TextureSampleLevel:
            {
                const auto texture = NamedInput(node, "Texture");
                const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                result = TextureSample(texture, {uv.X, uv.Y});
                if (outputPin->Name == "RGB")
                {
                    result.Type = Keire::ShaderGraphValueType::Vector3;
                    result.Data.W = 0.0F;
                }
                else if (outputPin->Type == Keire::ShaderGraphValueType::Scalar)
                {
                    const float component = outputPin->Name == "R"   ? result.Data.X
                                            : outputPin->Name == "G" ? result.Data.Y
                                            : outputPin->Name == "B" ? result.Data.Z
                                                                     : result.Data.W;
                    result = {.Data = {component, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                }
                break;
            }
            case Keire::ShaderGraphNodeKind::HeightToNormal:
            {
                const float height = Coerce(NamedInput(node, "Height"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float strength = Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto normal =
                    Normalize({-(height - 0.5F) * strength, -(height - 0.5F) * strength, 1.0F}, m_Normal);
                result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::FlattenNormal:
            {
                const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                const float strength = std::clamp(
                    Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const auto normal = Normalize(
                    {normalValue.X * strength, normalValue.Y * strength, std::lerp(1.0F, normalValue.Z, strength)},
                    m_Normal);
                result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
                break;
            }
            case Keire::ShaderGraphNodeKind::MakeMaterialAttributes:
            {
                PreviewMaterialSurface surface;
                surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.ClearCoat = Coerce(NamedInput(node, "ClearCoat"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.ClearCoatRoughness =
                    Coerce(NamedInput(node, "ClearCoatRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.SheenColor = Coerce(NamedInput(node, "SheenColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.SheenRoughness =
                    Coerce(NamedInput(node, "SheenRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                surface.Normal = {normal.X, normal.Y, normal.Z};
                surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
                surface.Occlusion = Coerce(NamedInput(node, "Occlusion"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.SubsurfaceColor =
                    Coerce(NamedInput(node, "SubsurfaceColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.Subsurface = Coerce(NamedInput(node, "Subsurface"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Anisotropy = Coerce(NamedInput(node, "Anisotropy"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto tangent = Coerce(NamedInput(node, "Tangent"), Keire::ShaderGraphValueType::Vector3).Data;
                surface.Tangent = {tangent.X, tangent.Y, tangent.Z};
                surface.Transmission =
                    Coerce(NamedInput(node, "Transmission"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.IndexOfRefraction =
                    Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::MaterialAttributes, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::BreakMaterialAttributes:
            {
                const auto attributes =
                    Coerce(NamedInput(node, "Attributes"), Keire::ShaderGraphValueType::MaterialAttributes);
                const auto surface = attributes.Surface.value_or(PreviewMaterialSurface{});
                result.Type = outputPin->Type;
                if (outputPin->Name == "BaseColor")
                    result.Data = surface.BaseColor;
                else if (outputPin->Name == "Metallic")
                    result.Data.X = surface.Metallic;
                else if (outputPin->Name == "Roughness")
                    result.Data.X = surface.Roughness;
                else if (outputPin->Name == "Specular")
                    result.Data.X = surface.Specular;
                else if (outputPin->Name == "ClearCoat")
                    result.Data.X = surface.ClearCoat;
                else if (outputPin->Name == "ClearCoatRoughness")
                    result.Data.X = surface.ClearCoatRoughness;
                else if (outputPin->Name == "SheenColor")
                    result.Data = surface.SheenColor;
                else if (outputPin->Name == "SheenRoughness")
                    result.Data.X = surface.SheenRoughness;
                else if (outputPin->Name == "Normal")
                    result.Data = {surface.Normal.X, surface.Normal.Y, surface.Normal.Z, 0.0F};
                else if (outputPin->Name == "Emission")
                    result.Data = surface.Emission;
                else if (outputPin->Name == "Occlusion")
                    result.Data.X = surface.Occlusion;
                else if (outputPin->Name == "Opacity")
                    result.Data.X = surface.Opacity;
                else if (outputPin->Name == "SubsurfaceColor")
                    result.Data = surface.SubsurfaceColor;
                else if (outputPin->Name == "Subsurface")
                    result.Data.X = surface.Subsurface;
                else if (outputPin->Name == "Anisotropy")
                    result.Data.X = surface.Anisotropy;
                else if (outputPin->Name == "Tangent")
                    result.Data = {surface.Tangent.X, surface.Tangent.Y, surface.Tangent.Z, 0.0F};
                else if (outputPin->Name == "Transmission")
                    result.Data.X = surface.Transmission;
                else if (outputPin->Name == "IndexOfRefraction")
                    result.Data.X = surface.IndexOfRefraction;
                else if (outputPin->Name == "Refraction")
                    result.Data.X = surface.Refraction;
                else if (outputPin->Name == "Thickness")
                    result.Data.X = surface.Thickness;
                break;
            }
            case Keire::ShaderGraphNodeKind::BlendMaterialAttributes:
            {
                const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::MaterialAttributes)
                                       .Surface.value_or(PreviewMaterialSurface{});
                const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::MaterialAttributes)
                                        .Surface.value_or(PreviewMaterialSurface{});
                const float alpha = std::clamp(
                    Coerce(NamedInput(node, "Alpha"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                const auto blendVector4 = [alpha](const Keire::Vector4 a, const Keire::Vector4 b)
                {
                    return Keire::Vector4{std::lerp(a.X, b.X, alpha), std::lerp(a.Y, b.Y, alpha),
                                          std::lerp(a.Z, b.Z, alpha), std::lerp(a.W, b.W, alpha)};
                };
                PreviewMaterialSurface surface;
                surface.BaseColor = blendVector4(first.BaseColor, second.BaseColor);
                surface.Metallic = std::lerp(first.Metallic, second.Metallic, alpha);
                surface.Roughness = std::lerp(first.Roughness, second.Roughness, alpha);
                surface.Specular = std::lerp(first.Specular, second.Specular, alpha);
                surface.ClearCoat = std::lerp(first.ClearCoat, second.ClearCoat, alpha);
                surface.ClearCoatRoughness = std::lerp(first.ClearCoatRoughness, second.ClearCoatRoughness, alpha);
                surface.SheenColor = blendVector4(first.SheenColor, second.SheenColor);
                surface.SheenRoughness = std::lerp(first.SheenRoughness, second.SheenRoughness, alpha);
                surface.Normal = Normalize({std::lerp(first.Normal.X, second.Normal.X, alpha),
                                            std::lerp(first.Normal.Y, second.Normal.Y, alpha),
                                            std::lerp(first.Normal.Z, second.Normal.Z, alpha)});
                surface.Emission = blendVector4(first.Emission, second.Emission);
                surface.Occlusion = std::lerp(first.Occlusion, second.Occlusion, alpha);
                surface.Opacity = std::lerp(first.Opacity, second.Opacity, alpha);
                surface.SubsurfaceColor = blendVector4(first.SubsurfaceColor, second.SubsurfaceColor);
                surface.Subsurface = std::lerp(first.Subsurface, second.Subsurface, alpha);
                surface.Anisotropy = std::lerp(first.Anisotropy, second.Anisotropy, alpha);
                surface.Tangent = Normalize({std::lerp(first.Tangent.X, second.Tangent.X, alpha),
                                             std::lerp(first.Tangent.Y, second.Tangent.Y, alpha),
                                             std::lerp(first.Tangent.Z, second.Tangent.Z, alpha)},
                                            {1.0F, 0.0F, 0.0F});
                surface.Transmission = std::lerp(first.Transmission, second.Transmission, alpha);
                surface.IndexOfRefraction = std::lerp(first.IndexOfRefraction, second.IndexOfRefraction, alpha);
                surface.Refraction = std::lerp(first.Refraction, second.Refraction, alpha);
                surface.Thickness = std::lerp(first.Thickness, second.Thickness, alpha);
                result = {.Type = Keire::ShaderGraphValueType::MaterialAttributes, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::StandardSurfaceBsdf:
            {
                PreviewMaterialSurface surface;
                surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                surface.Normal = {normal.X, normal.Y, normal.Z};
                surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
                surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf:
            {
                PreviewMaterialSurface surface;
                surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Roughness = Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Specular =
                    Coerce(NamedInput(node, "SpecularWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.ClearCoat = Coerce(NamedInput(node, "CoatWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.ClearCoatRoughness =
                    Coerce(NamedInput(node, "CoatRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto fuzz = Coerce(NamedInput(node, "FuzzColor"), Keire::ShaderGraphValueType::Color).Data;
                const float fuzzWeight =
                    Coerce(NamedInput(node, "FuzzWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.SheenColor = {fuzz.X * fuzzWeight, fuzz.Y * fuzzWeight, fuzz.Z * fuzzWeight, fuzz.W};
                surface.SheenRoughness =
                    Coerce(NamedInput(node, "FuzzRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                surface.Normal = Normalize({normal.X, normal.Y, normal.Z});
                surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
                surface.Occlusion = Coerce(NamedInput(node, "Occlusion"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.SubsurfaceColor =
                    Coerce(NamedInput(node, "SubsurfaceColor"), Keire::ShaderGraphValueType::Color).Data;
                surface.Subsurface =
                    Coerce(NamedInput(node, "SubsurfaceWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Anisotropy = Coerce(NamedInput(node, "Anisotropy"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const auto tangent = Coerce(NamedInput(node, "Tangent"), Keire::ShaderGraphValueType::Vector3).Data;
                surface.Tangent = Normalize({tangent.X, tangent.Y, tangent.Z}, {1.0F, 0.0F, 0.0F});
                surface.Transmission =
                    Coerce(NamedInput(node, "TransmissionWeight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.IndexOfRefraction =
                    Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::MixSlabs:
            case Keire::ShaderGraphNodeKind::AddSlabs:
            {
                const auto first = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Bsdf)
                                       .Surface.value_or(PreviewMaterialSurface{});
                const auto second = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Bsdf)
                                        .Surface.value_or(PreviewMaterialSurface{});
                float factor = 0.0F;
                if (node.Kind == Keire::ShaderGraphNodeKind::MixSlabs)
                    factor = Coerce(NamedInput(node, "Factor"), Keire::ShaderGraphValueType::Scalar).Data.X;
                else
                {
                    const float firstWeight =
                        std::max(Coerce(NamedInput(node, "WeightA"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F);
                    const float secondWeight =
                        std::max(Coerce(NamedInput(node, "WeightB"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F);
                    const float total = firstWeight + secondWeight;
                    factor = total > 1.0e-8F ? secondWeight / total : 0.0F;
                }
                result = {.Type = Keire::ShaderGraphValueType::Bsdf,
                          .Surface = BlendPreviewMaterialSurfaces(first, second, factor)};
                break;
            }
            case Keire::ShaderGraphNodeKind::CoatSlab:
            case Keire::ShaderGraphNodeKind::ClearCoatBsdf:
            {
                auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                                   .Surface.value_or(PreviewMaterialSurface{});
                surface.ClearCoat = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.ClearCoatRoughness =
                    Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::FuzzSlab:
            case Keire::ShaderGraphNodeKind::SheenBsdf:
            {
                auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                                   .Surface.value_or(PreviewMaterialSurface{});
                const auto color = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                const float weight = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.SheenColor = {color.X * weight, color.Y * weight, color.Z * weight, color.W};
                surface.SheenRoughness =
                    Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::SubsurfaceBsdf:
            {
                auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                                   .Surface.value_or(PreviewMaterialSurface{});
                surface.SubsurfaceColor = Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                surface.Subsurface = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::TransmissionBsdf:
            {
                auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                                   .Surface.value_or(PreviewMaterialSurface{});
                surface.Transmission = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.IndexOfRefraction =
                    Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Refraction = Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                surface.Thickness = Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                break;
            }
            case Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes:
            {
                result = Coerce(NamedInput(node, "BSDF"), Keire::ShaderGraphValueType::Bsdf);
                result.Type = Keire::ShaderGraphValueType::MaterialAttributes;
                break;
            }
            case Keire::ShaderGraphNodeKind::Keyword:
                result = {.Type = Keire::ShaderGraphValueType::Scalar};
                break;
            case Keire::ShaderGraphNodeKind::StaticSwitch:
            {
                const float condition =
                    Coerce(NamedInput(node, "Condition"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = Coerce(NamedInput(node, condition != 0.0F ? "True" : "False"), node.ValueType);
                break;
            }
            case Keire::ShaderGraphNodeKind::Custom:
                result = GraphValue(node.Value, node.ValueType);
                break;
            case Keire::ShaderGraphNodeKind::Reroute:
                result = Coerce(NamedInput(node, "Input"), node.ValueType);
                break;
            case Keire::ShaderGraphNodeKind::If:
            {
                const float left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float threshold =
                    Coerce(NamedInput(node, "Threshold"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = Coerce(NamedInput(node, std::abs(left - right) <= threshold ? "Equal"
                                                 : left > right                      ? "Greater"
                                                                                     : "Less"),
                                node.ValueType);
                break;
            }
            case Keire::ShaderGraphNodeKind::Compare:
            {
                const float left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float threshold =
                    Coerce(NamedInput(node, "Threshold"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float value = outputPin->Name == "Greater" ? (left > right ? 1.0F : 0.0F)
                                    : outputPin->Name == "Less"  ? (left < right ? 1.0F : 0.0F)
                                                                 : (std::abs(left - right) <= threshold ? 1.0F : 0.0F);
                result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::BooleanAnd:
            case Keire::ShaderGraphNodeKind::BooleanOr:
            {
                const bool left = Coerce(NamedInput(node, "A"), Keire::ShaderGraphValueType::Scalar).Data.X != 0.0F;
                const bool right = Coerce(NamedInput(node, "B"), Keire::ShaderGraphValueType::Scalar).Data.X != 0.0F;
                const float value =
                    (node.Kind == Keire::ShaderGraphNodeKind::BooleanAnd ? left && right : left || right) ? 1.0F : 0.0F;
                result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::BooleanNot:
            {
                const float value =
                    Coerce(NamedInput(node, "Input"), Keire::ShaderGraphValueType::Scalar).Data.X == 0.0F ? 1.0F : 0.0F;
                result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                break;
            }
            case Keire::ShaderGraphNodeKind::ArcTangent:
                result = Unary(node, [](const float value) { return std::atan(value); });
                break;
            case Keire::ShaderGraphNodeKind::HyperbolicSine:
                result = Unary(node, [](const float value) { return std::sinh(value); });
                break;
            case Keire::ShaderGraphNodeKind::HyperbolicCosine:
                result = Unary(node, [](const float value) { return std::cosh(value); });
                break;
            case Keire::ShaderGraphNodeKind::HyperbolicTangent:
                result = Unary(node, [](const float value) { return std::tanh(value); });
                break;
            case Keire::ShaderGraphNodeKind::DegreesToRadians:
                result = Unary(node, [](const float value) { return value * 0.017453292519943295F; });
                break;
            case Keire::ShaderGraphNodeKind::RadiansToDegrees:
                result = Unary(node, [](const float value) { return value * 57.29577951308232F; });
                break;
            case Keire::ShaderGraphNodeKind::Negate:
                result = Unary(node, [](const float value) { return -value; });
                break;
            case Keire::ShaderGraphNodeKind::Exponential:
                result = Unary(node, [](const float value) { return std::exp(value); });
                break;
            case Keire::ShaderGraphNodeKind::Logarithm:
                result = Unary(node, [](const float value) { return std::log(std::max(value, 1.0e-8F)); });
                break;
            case Keire::ShaderGraphNodeKind::ScaleAndBias:
            {
                const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                const float bias = Coerce(NamedInput(node, "Bias"), Keire::ShaderGraphValueType::Scalar).Data.X;
                result = Unary(node, [scale, bias](const float value) { return value * scale + bias; });
                break;
            }
            case Keire::ShaderGraphNodeKind::FunctionCall:
                throw std::invalid_argument("Function Call preview requires an expanded reusable graph.");
            case Keire::ShaderGraphNodeKind::Master:
                throw std::invalid_argument("The Shader Output node cannot be used as an expression.");
            }
            return result;
        }

        const Keire::ShaderGraphDefinition& m_Definition;
        std::span<const Keire::ShaderPropertyDefinition> m_Properties;
        std::span<const ShaderGraphPreviewTexture> m_Textures;
        std::map<Keire::AssetId, std::size_t> m_Nodes;
        std::map<std::pair<Keire::AssetId, Keire::AssetId>, Keire::ShaderGraphEndpoint> m_Incoming;
        std::map<std::pair<Keire::AssetId, Keire::AssetId>, std::size_t> m_CacheIndices;
        std::vector<std::optional<PreviewGraphValue>> m_Cache;
        std::vector<std::uint8_t> m_Visiting;
        const Keire::ShaderGraphNode* m_Master = nullptr;
        Keire::Vector2 m_Uv;
        Keire::Vector3 m_Normal{0.0F, 0.0F, 1.0F};
        Keire::Vector3 m_Position;
    };

    ShaderGraphPreviewEvaluator::ShaderGraphPreviewEvaluator(const ShaderGraphPreviewRequest& request)
        : m_Request(request)
    {
        if (request.Definition)
        {
            m_Impl = std::make_unique<Impl>(*request.Definition, request.Properties, request.Textures);
        }
    }

    ShaderGraphPreviewEvaluator::~ShaderGraphPreviewEvaluator() = default;

    void ShaderGraphPreviewEvaluator::SetContext(const Keire::Vector2 uv, const Keire::Vector3 normal,
                                                 const Keire::Vector3 position)
    {
        if (m_Impl)
            m_Impl->SetContext(uv, normal, position);
    }

    std::optional<Keire::Vector4> ShaderGraphPreviewEvaluator::MasterInput(const std::string_view name,
                                                                           const Keire::ShaderGraphValueType type)
    {
        return m_Impl ? m_Impl->MasterInput(name, type) : std::nullopt;
    }

    std::optional<PreviewMaterialSurface> ShaderGraphPreviewEvaluator::MasterAttributes()
    {
        return m_Impl ? m_Impl->MasterAttributes() : std::nullopt;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
