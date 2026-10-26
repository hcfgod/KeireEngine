#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    ShaderGraphPreviewEvaluator::Impl::Impl(const Keire::ShaderGraphDefinition& definition,
                                            const std::span<const Keire::ShaderPropertyDefinition> properties,
                                            const std::span<const ShaderGraphPreviewTexture> textures)
        : m_Definition(definition), m_Properties(properties), m_Textures(textures),
          m_Visiting(definition.Nodes.size(), 0)
    {
        for (std::size_t index = 0; index < definition.Nodes.size(); ++index)
        {
            m_Nodes.emplace(definition.Nodes[index].Id, index);
            for (const auto& pin : definition.Nodes[index].Pins)
            {
                if (pin.Direction == Keire::ShaderGraphPinDirection::Output)
                {
                    m_CacheIndices.emplace(std::pair{definition.Nodes[index].Id, pin.Id}, m_Cache.size());
                    m_Cache.emplace_back();
                }
            }
        }
        for (const auto& connection : definition.Connections)
        {
            m_Incoming.emplace(std::pair{connection.Input.Node, connection.Input.Pin}, connection.Output);
        }
        const auto master =
            std::ranges::find(definition.Nodes, Keire::ShaderGraphNodeKind::Master, &Keire::ShaderGraphNode::Kind);
        if (master != definition.Nodes.end())
        {
            m_Master = std::addressof(*master);
        }
    }

    void ShaderGraphPreviewEvaluator::Impl::SetContext(const Keire::Vector2 uv, const Keire::Vector3 normal,
                                                       const Keire::Vector3 position)
    {
        m_Uv = uv;
        m_Normal = Normalize(normal);
        m_Position = position;
        std::ranges::fill(m_Cache, std::nullopt);
        std::ranges::fill(m_Visiting, std::uint8_t{0});
    }

    [[nodiscard]] std::optional<Keire::Vector4>
    ShaderGraphPreviewEvaluator::Impl::MasterInput(const std::string_view name, const Keire::ShaderGraphValueType type)
    {
        if (!m_Master)
        {
            return std::nullopt;
        }
        const auto pin = std::ranges::find(m_Master->Pins, name, &Keire::ShaderGraphPin::Name);
        if (pin == m_Master->Pins.end())
        {
            return std::nullopt;
        }
        return Coerce(Input(*m_Master, *pin), type).Data;
    }

    [[nodiscard]] std::optional<PreviewMaterialSurface> ShaderGraphPreviewEvaluator::Impl::MasterAttributes()
    {
        if (!m_Master)
        {
            return std::nullopt;
        }
        const auto pin = std::ranges::find(m_Master->Pins, "MaterialAttributes", &Keire::ShaderGraphPin::Name);
        if (pin == m_Master->Pins.end() || !m_Incoming.contains({m_Master->Id, pin->Id}))
        {
            return std::nullopt;
        }
        const auto value = Coerce(Input(*m_Master, *pin), Keire::ShaderGraphValueType::MaterialAttributes);
        return value.Surface;
    }

    [[nodiscard]] PreviewGraphValue
    ShaderGraphPreviewEvaluator::Impl::Coerce(PreviewGraphValue value, const Keire::ShaderGraphValueType type) const
    {
        if (value.Type == type ||
            ((value.Type == Keire::ShaderGraphValueType::Color && type == Keire::ShaderGraphValueType::Vector4) ||
             (value.Type == Keire::ShaderGraphValueType::Vector4 && type == Keire::ShaderGraphValueType::Color)))
        {
            value.Type = type;
            return value;
        }
        if ((value.Type == Keire::ShaderGraphValueType::Color || value.Type == Keire::ShaderGraphValueType::Vector4) &&
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
        {
            throw std::invalid_argument("Preview graph value cannot be converted to the destination type.");
        }
        value.Type = type;
        value.Data = {value.Data.X, value.Data.X, value.Data.X, value.Data.X};
        return value;
    }

    [[nodiscard]] PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::Input(const Keire::ShaderGraphNode& node,
                                                                             const Keire::ShaderGraphPin& pin)
    {
        const auto incoming = m_Incoming.find({node.Id, pin.Id});
        if (incoming == m_Incoming.end())
        {
            return GraphValue(pin.DefaultValue, pin.Type);
        }
        return Coerce(Evaluate(incoming->second), pin.Type);
    }

    [[nodiscard]] PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::NamedInput(const Keire::ShaderGraphNode& node,
                                                                                  const std::string_view name)
    {
        const auto pin = std::ranges::find_if(
            node.Pins, [&](const Keire::ShaderGraphPin& candidate)
            { return candidate.Direction == Keire::ShaderGraphPinDirection::Input && candidate.Name == name; });
        if (pin == node.Pins.end())
        {
            throw std::invalid_argument("Preview graph node is missing a canonical input.");
        }
        return Input(node, *pin);
    }

    void ShaderGraphPreviewEvaluator::Impl::QueueConnectedInput(std::vector<EvaluationTask>& tasks,
                                                                const Keire::ShaderGraphNode& node,
                                                                const std::string_view name)
    {
        const auto pin = std::ranges::find_if(
            node.Pins, [&](const Keire::ShaderGraphPin& candidate)
            { return candidate.Direction == Keire::ShaderGraphPinDirection::Input && candidate.Name == name; });
        if (pin == node.Pins.end())
        {
            throw std::invalid_argument("Preview graph node is missing a canonical input.");
        }
        const auto incoming = m_Incoming.find({node.Id, pin->Id});
        if (incoming != m_Incoming.end())
        {
            tasks.push_back({incoming->second, EvaluationPhase::Begin});
        }
    }

    [[nodiscard]] PreviewGraphValue
    ShaderGraphPreviewEvaluator::Impl::Evaluate(const Keire::ShaderGraphEndpoint endpoint)
    {
        if (!m_Nodes.contains(endpoint.Node))
        {
            throw std::invalid_argument("Preview graph connection references a missing node.");
        }
        const auto rootCacheIndex = m_CacheIndices.find({endpoint.Node, endpoint.Pin});
        if (rootCacheIndex == m_CacheIndices.end())
        {
            throw std::invalid_argument("Preview graph connection references a missing output pin.");
        }
        if (const auto& cached = m_Cache[rootCacheIndex->second])
        {
            return *cached;
        }

        struct TraversalReset final
        {
            std::vector<std::uint8_t>& Visiting;
            bool Complete = false;

            ~TraversalReset()
            {
                if (!Complete)
                {
                    std::ranges::fill(Visiting, std::uint8_t{0});
                }
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
            {
                throw std::invalid_argument("Preview graph connection references a missing node.");
            }
            const auto cacheIndex = m_CacheIndices.find({task.Endpoint.Node, task.Endpoint.Pin});
            if (cacheIndex == m_CacheIndices.end())
            {
                throw std::invalid_argument("Preview graph connection references a missing output pin.");
            }
            const auto index = located->second;
            const auto& node = m_Definition.Nodes[index];
            const auto outputPin = std::ranges::find(node.Pins, task.Endpoint.Pin, &Keire::ShaderGraphPin::Id);
            if (outputPin == node.Pins.end() || outputPin->Direction != Keire::ShaderGraphPinDirection::Output)
            {
                throw std::invalid_argument("Preview graph endpoint is not an output pin.");
            }

            if (task.Phase == EvaluationPhase::Begin)
            {
                if (m_Cache[cacheIndex->second])
                {
                    continue;
                }
                if (m_Visiting[index])
                {
                    throw std::invalid_argument("Preview graph contains an expression cycle.");
                }
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
                    node.Kind == Keire::ShaderGraphNodeKind::Keyword || node.Kind == Keire::ShaderGraphNodeKind::Master)
                {
                    continue;
                }
                for (auto pin = node.Pins.rbegin(); pin != node.Pins.rend(); ++pin)
                {
                    if (pin->Direction != Keire::ShaderGraphPinDirection::Input)
                    {
                        continue;
                    }
                    const auto incoming = m_Incoming.find({node.Id, pin->Id});
                    if (incoming != m_Incoming.end())
                    {
                        tasks.push_back({incoming->second, EvaluationPhase::Begin});
                    }
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

            m_Cache[cacheIndex->second] = EvaluateNode(node, *outputPin);
            m_Visiting[index] = 0;
        }

        reset.Complete = true;
        const auto& result = m_Cache[rootCacheIndex->second];
        if (!result)
        {
            throw std::logic_error("Preview graph traversal completed without evaluating its root.");
        }
        return *result;
    }

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
        {
            m_Impl->SetContext(uv, normal, position);
        }
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
