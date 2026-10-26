#pragma once

#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    // The caller owns graph/property/texture storage; synchronous previews borrow it without resizing it.
    // Traversal owns cache/visiting mutations; node families read that context through Input/NamedInput/Coerce.
    struct ShaderGraphPreviewEvaluator::Impl final
    {
      public:
        Impl(const Keire::ShaderGraphDefinition& definition,
             std::span<const Keire::ShaderPropertyDefinition> properties,
             std::span<const ShaderGraphPreviewTexture> textures);

        void SetContext(Keire::Vector2 uv, Keire::Vector3 normal, Keire::Vector3 position);

        [[nodiscard]] std::optional<Keire::Vector4> MasterInput(std::string_view name,
                                                                Keire::ShaderGraphValueType type);

        [[nodiscard]] std::optional<PreviewMaterialSurface> MasterAttributes();

      private:
        [[nodiscard]] PreviewGraphValue Coerce(PreviewGraphValue value, Keire::ShaderGraphValueType type) const;

        [[nodiscard]] PreviewGraphValue Input(const Keire::ShaderGraphNode& node, const Keire::ShaderGraphPin& pin);

        [[nodiscard]] PreviewGraphValue NamedInput(const Keire::ShaderGraphNode& node, std::string_view name);

        template <typename Operation>
        [[nodiscard]] PreviewGraphValue Binary(const Keire::ShaderGraphNode& node, Operation operation)
        {
            const auto left = Coerce(NamedInput(node, "A"), node.ValueType);
            const auto right = Coerce(NamedInput(node, "B"), node.ValueType);
            PreviewGraphValue result{.Type = node.ValueType};
            for (std::size_t index = 0; index < ComponentCount(node.ValueType); ++index)
            {
                SetComponent(result.Data, index, operation(Component(left.Data, index), Component(right.Data, index)));
            }
            return result;
        }

        template <typename Operation>
        [[nodiscard]] PreviewGraphValue Unary(const Keire::ShaderGraphNode& node, Operation operation,
                                              const std::string_view inputName = "Value")
        {
            const auto value = Coerce(NamedInput(node, inputName), node.ValueType);
            PreviewGraphValue result{.Type = node.ValueType};
            for (std::size_t index = 0; index < ComponentCount(node.ValueType); ++index)
            {
                SetComponent(result.Data, index, operation(Component(value.Data, index)));
            }
            return result;
        }

        [[nodiscard]] PreviewGraphValue TextureSample(PreviewGraphValue texture, Keire::Vector2 uv) const noexcept;

        void QueueConnectedInput(std::vector<EvaluationTask>& tasks, const Keire::ShaderGraphNode& node,
                                 std::string_view name);

        [[nodiscard]] PreviewGraphValue Evaluate(Keire::ShaderGraphEndpoint endpoint);

        [[nodiscard]] PreviewGraphValue EvaluateNode(const Keire::ShaderGraphNode& node,
                                                     const Keire::ShaderGraphPin& outputPin);

        [[nodiscard]] PreviewGraphValue EvaluateSamplingNode(const Keire::ShaderGraphNode& node,
                                                             const Keire::ShaderGraphPin& outputPin);

        [[nodiscard]] PreviewGraphValue EvaluateGeometryNode(const Keire::ShaderGraphNode& node);

        [[nodiscard]] PreviewGraphValue EvaluateProceduralNode(const Keire::ShaderGraphNode& node,
                                                               const Keire::ShaderGraphPin& outputPin);

        [[nodiscard]] PreviewGraphValue EvaluateSurfaceNode(const Keire::ShaderGraphNode& node,
                                                            const Keire::ShaderGraphPin& outputPin);

        [[nodiscard]] PreviewGraphValue EvaluateControlNode(const Keire::ShaderGraphNode& node,
                                                            const Keire::ShaderGraphPin& outputPin);

        [[nodiscard]] PreviewGraphValue EvaluateMathNode(const Keire::ShaderGraphNode& node,
                                                         const Keire::ShaderGraphPin& outputPin);

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
} // namespace KeireEditor::ShaderGraphPreviewInternal
