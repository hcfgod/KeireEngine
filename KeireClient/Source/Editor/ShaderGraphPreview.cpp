#include "KeireClient/Editor/ShaderGraphPreview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::size_t MaximumPreviewTriangles = 500'000;

        void CheckPreviewCancellation(const ShaderGraphPreviewRequest& request)
        {
            if (request.CancellationRequested && request.CancellationRequested())
                throw std::runtime_error("Shader Graph preview rendering was superseded.");
        }

        struct PreviewMaterial
        {
            Keire::Vector4 BaseColor{0.72F, 0.72F, 0.74F, 1.0F};
            Keire::Vector3 Emission;
            float Metallic = 0.0F;
            float Roughness = 0.45F;
            float Specular = 0.5F;
            float ClearCoat = 0.0F;
            float ClearCoatRoughness = 0.1F;
            Keire::Vector3 SheenColor;
            float SheenRoughness = 0.5F;
            float Opacity = 1.0F;
            float Occlusion = 1.0F;
            Keire::Vector3 Normal;
            bool HasBaseTexture = false;
            bool HasNormal = false;
            bool Unlit = false;
        };

        struct PreviewVertex
        {
            Keire::Vector3 Position;
            Keire::Vector3 Normal;
            Keire::Vector2 UV;
        };

        struct ProjectedVertex
        {
            float X = 0.0F;
            float Y = 0.0F;
            float Depth = 0.0F;
            Keire::Vector3 Position;
            Keire::Vector3 Normal;
            Keire::Vector2 UV;
        };

        struct PreviewGeometry
        {
            std::vector<PreviewVertex> Vertices;
            std::vector<std::uint32_t> Indices;
        };

        [[nodiscard]] std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] float Clamp01(const float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

        [[nodiscard]] Keire::Vector4 ValueVector(const Keire::ShaderGraphValue& value) noexcept
        {
            if (const auto* scalar = std::get_if<float>(&value))
                return {*scalar, *scalar, *scalar, *scalar};
            if (const auto* vector = std::get_if<Keire::Vector2>(&value))
                return {vector->X, vector->Y, 0.0F, 0.0F};
            if (const auto* vector = std::get_if<Keire::Vector3>(&value))
                return {vector->X, vector->Y, vector->Z, 0.0F};
            if (const auto* vector = std::get_if<Keire::Vector4>(&value))
                return *vector;
            if (const auto* color = std::get_if<Keire::Color>(&value))
                return {color->Red, color->Green, color->Blue, color->Alpha};
            return {};
        }

        [[nodiscard]] Keire::Vector3 Normalize(const Keire::Vector3 value,
                                               const Keire::Vector3 fallback = {0.0F, 0.0F, 1.0F}) noexcept
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        [[nodiscard]] float Dot(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
        {
            return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
        }

        [[nodiscard]] Keire::Vector3 Cross(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
        {
            return {first.Y * second.Z - first.Z * second.Y, first.Z * second.X - first.X * second.Z,
                    first.X * second.Y - first.Y * second.X};
        }

        struct PreviewMaterialSurface
        {
            Keire::Vector4 BaseColor{1.0F, 1.0F, 1.0F, 1.0F};
            float Metallic = 0.0F;
            float Roughness = 0.5F;
            float Specular = 0.5F;
            float ClearCoat = 0.0F;
            float ClearCoatRoughness = 0.25F;
            Keire::Vector4 SheenColor{0.0F, 0.0F, 0.0F, 1.0F};
            float SheenRoughness = 0.5F;
            Keire::Vector3 Normal{0.0F, 0.0F, 1.0F};
            Keire::Vector4 Emission{0.0F, 0.0F, 0.0F, 1.0F};
            float Occlusion = 1.0F;
            float Opacity = 1.0F;
            Keire::Vector4 SubsurfaceColor{1.0F, 0.35F, 0.25F, 1.0F};
            float Subsurface = 0.0F;
            float Anisotropy = 0.0F;
            Keire::Vector3 Tangent{1.0F, 0.0F, 0.0F};
            float Transmission = 0.0F;
            float IndexOfRefraction = 1.5F;
            float Refraction = 0.0F;
            float Thickness = 1.0F;
        };

        struct PreviewGraphValue
        {
            Keire::Vector4 Data;
            Keire::ShaderGraphValueType Type = Keire::ShaderGraphValueType::Scalar;
            Keire::AssetId Texture;
            Keire::ShaderTextureSemantic TextureSemantic = Keire::ShaderTextureSemantic::Generic;
            std::optional<PreviewMaterialSurface> Surface;
        };

        [[nodiscard]] std::size_t ComponentCount(const Keire::ShaderGraphValueType type) noexcept
        {
            switch (type)
            {
            case Keire::ShaderGraphValueType::Scalar:
                return 1;
            case Keire::ShaderGraphValueType::Vector2:
                return 2;
            case Keire::ShaderGraphValueType::Vector3:
                return 3;
            case Keire::ShaderGraphValueType::Vector4:
            case Keire::ShaderGraphValueType::Color:
                return 4;
            case Keire::ShaderGraphValueType::Texture2D:
            case Keire::ShaderGraphValueType::MaterialAttributes:
            case Keire::ShaderGraphValueType::Bsdf:
                return 0;
            }
            return 0;
        }

        [[nodiscard]] float Component(const Keire::Vector4 value, const std::size_t index) noexcept
        {
            switch (index)
            {
            case 0:
                return value.X;
            case 1:
                return value.Y;
            case 2:
                return value.Z;
            default:
                return value.W;
            }
        }

        void SetComponent(Keire::Vector4& value, const std::size_t index, const float component) noexcept
        {
            switch (index)
            {
            case 0:
                value.X = component;
                break;
            case 1:
                value.Y = component;
                break;
            case 2:
                value.Z = component;
                break;
            default:
                value.W = component;
                break;
            }
        }

        [[nodiscard]] PreviewGraphValue
        GraphValue(const Keire::ShaderGraphValue& value, const Keire::ShaderGraphValueType type,
                   const Keire::ShaderTextureSemantic semantic = Keire::ShaderTextureSemantic::Generic) noexcept
        {
            PreviewGraphValue result{.Data = ValueVector(value), .Type = type, .TextureSemantic = semantic};
            if (const auto* texture = std::get_if<Keire::AssetId>(&value))
                result.Texture = *texture;
            if (type == Keire::ShaderGraphValueType::MaterialAttributes || type == Keire::ShaderGraphValueType::Bsdf)
                result.Surface.emplace();
            return result;
        }

        [[nodiscard]] float SafeDivide(const float first, const float second) noexcept
        {
            const float divisor = std::abs(second) >= 1.0e-5F ? second : (second < 0.0F ? -1.0e-5F : 1.0e-5F);
            return first / divisor;
        }

        [[nodiscard]] float Fraction(const float value) noexcept { return value - std::floor(value); }

        [[nodiscard]] float MaterialHash(const Keire::Vector2 value) noexcept
        {
            const Keire::Vector2 wrapped{Fraction(value.X * 0.1031F), Fraction(value.Y * 0.103F)};
            const float mixed = wrapped.X * (wrapped.Y + 33.33F) + wrapped.Y * (wrapped.X + 33.33F);
            return Fraction((wrapped.X + wrapped.Y) * mixed);
        }

        [[nodiscard]] float MaterialValueNoise(const Keire::Vector2 position) noexcept
        {
            const Keire::Vector2 cell{std::floor(position.X), std::floor(position.Y)};
            const Keire::Vector2 local{Fraction(position.X), Fraction(position.Y)};
            const Keire::Vector2 blend{local.X * local.X * (3.0F - 2.0F * local.X),
                                       local.Y * local.Y * (3.0F - 2.0F * local.Y)};
            const float bottom = std::lerp(MaterialHash(cell), MaterialHash({cell.X + 1.0F, cell.Y}), blend.X);
            const float top =
                std::lerp(MaterialHash({cell.X, cell.Y + 1.0F}), MaterialHash({cell.X + 1.0F, cell.Y + 1.0F}), blend.X);
            return std::lerp(bottom, top, blend.Y);
        }

        [[nodiscard]] float MaterialNoise(const Keire::Vector2 uv, const float scale, const float detail) noexcept
        {
            Keire::Vector2 position{uv.X * std::max(std::abs(scale), 1.0e-4F),
                                    uv.Y * std::max(std::abs(scale), 1.0e-4F)};
            const float persistence = std::clamp(detail, 0.0F, 1.0F) * 0.5F;
            float amplitude = 0.5F;
            float value = MaterialValueNoise(position) * amplitude;
            float normalization = amplitude;
            for (std::size_t octave = 1; octave < 4; ++octave)
            {
                position = {position.X * 2.0F + 17.0F, position.Y * 2.0F + 29.0F};
                amplitude *= persistence;
                value += MaterialValueNoise(position) * amplitude;
                normalization += amplitude;
            }
            return value / std::max(normalization, 1.0e-5F);
        }

        class GraphPreviewEvaluator final
        {
          public:
            GraphPreviewEvaluator(const Keire::ShaderGraphDefinition& definition,
                                  const std::span<const Keire::ShaderPropertyDefinition> properties)
                : m_Definition(definition), m_Properties(properties), m_Visiting(definition.Nodes.size(), 0)
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
                const auto master = std::ranges::find(definition.Nodes, Keire::ShaderGraphNodeKind::Master,
                                                      &Keire::ShaderGraphNode::Kind);
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
                try
                {
                    return Coerce(Input(*m_Master, *pin), type).Data;
                }
                catch (const std::exception&)
                {
                    return std::nullopt;
                }
            }

            [[nodiscard]] std::optional<PreviewMaterialSurface> MasterAttributes()
            {
                if (!m_Master)
                    return std::nullopt;
                const auto pin = std::ranges::find(m_Master->Pins, "MaterialAttributes", &Keire::ShaderGraphPin::Name);
                if (pin == m_Master->Pins.end() || !m_Incoming.contains({m_Master->Id, pin->Id}))
                    return std::nullopt;
                try
                {
                    const auto value = Coerce(Input(*m_Master, *pin), Keire::ShaderGraphValueType::MaterialAttributes);
                    return value.Surface;
                }
                catch (const std::exception&)
                {
                    return std::nullopt;
                }
            }

          private:
            [[nodiscard]] PreviewGraphValue Coerce(PreviewGraphValue value,
                                                   const Keire::ShaderGraphValueType type) const
            {
                if (value.Type == type || ((value.Type == Keire::ShaderGraphValueType::Color &&
                                            type == Keire::ShaderGraphValueType::Vector4) ||
                                           (value.Type == Keire::ShaderGraphValueType::Vector4 &&
                                            type == Keire::ShaderGraphValueType::Color)))
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
                if (value.Type != Keire::ShaderGraphValueType::Scalar ||
                    type == Keire::ShaderGraphValueType::Texture2D ||
                    type == Keire::ShaderGraphValueType::MaterialAttributes ||
                    type == Keire::ShaderGraphValueType::Bsdf)
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
                    SetComponent(result.Data, index,
                                 operation(Component(left.Data, index), Component(right.Data, index)));
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
                const bool alternate =
                    ((static_cast<int>(std::floor(uv.X * 10.0F)) + static_cast<int>(std::floor(uv.Y * 10.0F))) & 1) !=
                    0;
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

            [[nodiscard]] PreviewGraphValue Evaluate(const Keire::ShaderGraphEndpoint endpoint)
            {
                const auto located = m_Nodes.find(endpoint.Node);
                if (located == m_Nodes.end())
                    throw std::invalid_argument("Preview graph connection references a missing node.");
                const auto index = located->second;
                const auto cacheIndex = m_CacheIndices.find({endpoint.Node, endpoint.Pin});
                if (cacheIndex == m_CacheIndices.end())
                    throw std::invalid_argument("Preview graph connection references a missing output pin.");
                if (const auto& cached = m_Cache[cacheIndex->second])
                    return *cached;
                if (m_Visiting[index])
                    throw std::invalid_argument("Preview graph contains an expression cycle.");
                m_Visiting[index] = true;
                struct VisitingReset final
                {
                    std::uint8_t& Value;

                    ~VisitingReset() { Value = 0; }
                };
                const VisitingReset visitingReset{m_Visiting[index]};
                const auto& node = m_Definition.Nodes[index];
                const auto outputPin = std::ranges::find(node.Pins, endpoint.Pin, &Keire::ShaderGraphPin::Id);
                if (outputPin == node.Pins.end() || outputPin->Direction != Keire::ShaderGraphPinDirection::Output)
                    throw std::invalid_argument("Preview graph endpoint is not an output pin.");
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
                    Keire::Vector3 tangentNormal{sample.X * 2.0F - 1.0F, sample.Y * 2.0F - 1.0F,
                                                 sample.Z * 2.0F - 1.0F};
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
                    const auto combined =
                        Normalize({base.X + detail.X, base.Y + detail.Y, base.Z + detail.Z}, baseNormal);
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
                        SetComponent(
                            result.Data, component,
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
                        const float factor = SafeDivide(
                            Component(value.Data, component) - Component(inputMinimum.Data, component),
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
                    const float reflectance =
                        Coerce(NamedInput(node, "F0"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const float facing =
                        std::clamp(Dot(Normalize({normal.X, normal.Y, normal.Z}), {0.0F, 0.0F, 1.0F}), 0.0F, 1.0F);
                    const float value =
                        std::clamp(reflectance + (1.0F - reflectance) *
                                                     std::pow(1.0F - facing, std::max(std::abs(power), 1.0e-4F)),
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
                    const float rotation =
                        Coerce(NamedInput(node, "Rotation"), Keire::ShaderGraphValueType::Scalar).Data.X;
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
                        SetComponent(result.Data, component,
                                     std::floor(Component(value.Data, component) * steps) / steps);
                    break;
                }
                case Keire::ShaderGraphNodeKind::Round:
                    result = Unary(node, [](const float value) { return std::round(value); });
                    break;
                case Keire::ShaderGraphNodeKind::Truncate:
                    result = Unary(node, [](const float value) { return std::trunc(value); });
                    break;
                case Keire::ShaderGraphNodeKind::Sign:
                    result = Unary(node,
                                   [](const float value) { return value < 0.0F   ? -1.0F
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
                    result =
                        Unary(node, [](const float value) { return std::log2(std::max(std::abs(value), 1.0e-8F)); });
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
                    const auto normalValue =
                        Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    const auto incident = Normalize({incidentValue.X, incidentValue.Y, incidentValue.Z});
                    const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z});
                    const float ior = std::max(
                        std::abs(Coerce(NamedInput(node, "IOR"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
                    const float eta = 1.0F / ior;
                    const float noI = Dot(normal, incident);
                    const float k = 1.0F - eta * eta * (1.0F - noI * noI);
                    const auto refracted =
                        k < 0.0F ? Keire::Vector3{}
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
                    const float distance =
                        Coerce(NamedInput(node, "Distance"), Keire::ShaderGraphValueType::Scalar).Data.X;
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
                    result = {
                        .Data = {std::max(source.X * cosine + axisCross.X * sine + axis.X * axisDot * (1.0F - cosine),
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
                        std::abs(Coerce(NamedInput(node, "Radius"), Keire::ShaderGraphValueType::Scalar).Data.X),
                        1.0e-5F);
                    const float hardness = std::max(
                        std::abs(Coerce(NamedInput(node, "Hardness"), Keire::ShaderGraphValueType::Scalar).Data.X),
                        1.0F);
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
                        std::abs(Coerce(NamedInput(node, "Density"), Keire::ShaderGraphValueType::Scalar).Data.X),
                        1.0e-4F);
                    const float x = uv.X - center.X;
                    const float y = uv.Y - center.Y;
                    const float gradient = std::clamp((radius - std::sqrt(x * x + y * y)) * density, 0.0F, 1.0F);
                    result = {.Data = {gradient, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                    break;
                }
                case Keire::ShaderGraphNodeKind::LinearGradient:
                {
                    const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
                    const auto direction =
                        Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
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
                    const float contrast =
                        Coerce(NamedInput(node, "Contrast"), Keire::ShaderGraphValueType::Scalar).Data.X;
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
                    result = {.Data = {std::lerp(luminance, color.X, saturation),
                                       std::lerp(luminance, color.Y, saturation),
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
                    const float red =
                        kelvin <= 66.0F ? 1.0F
                                        : std::clamp(1.29293619F * std::pow(kelvin - 60.0F, -0.133204759F), 0.0F, 1.0F);
                    const float green =
                        kelvin <= 66.0F
                            ? std::clamp(0.390081579F * std::log(std::max(kelvin, 1.0F)) - 0.631841444F, 0.0F, 1.0F)
                            : std::clamp(1.12989086F * std::pow(kelvin - 60.0F, -0.0755148492F), 0.0F, 1.0F);
                    const float blue =
                        kelvin >= 66.0F ? 1.0F
                        : kelvin <= 19.0F
                            ? 0.0F
                            : std::clamp(0.543206811F * std::log(std::max(kelvin - 10.0F, 1.0F)) - 1.19625409F, 0.0F,
                                         1.0F);
                    result = {.Data = {red, green, blue, 1.0F}, .Type = Keire::ShaderGraphValueType::Color};
                    break;
                }
                case Keire::ShaderGraphNodeKind::ReflectionVector:
                {
                    const auto normalValue =
                        Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
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
                    const auto normalValue =
                        Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z}, m_Normal);
                    const float power = std::max(
                        std::abs(Coerce(NamedInput(node, "Power"), Keire::ShaderGraphValueType::Scalar).Data.X),
                        1.0e-4F);
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
                    constexpr std::array thresholds{0.0F,    0.5F,    0.125F,  0.625F,  0.75F,   0.25F,
                                                    0.875F,  0.375F,  0.1875F, 0.6875F, 0.0625F, 0.5625F,
                                                    0.9375F, 0.4375F, 0.8125F, 0.3125F};
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
                    const auto direction =
                        Coerce(NamedInput(node, "Direction"), Keire::ShaderGraphValueType::Vector2).Data;
                    const float frequency =
                        Coerce(NamedInput(node, "Frequency"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const float phase = Coerce(NamedInput(node, "Phase"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const float inverseLength =
                        1.0F / std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 1.0e-5F);
                    const float wave = std::sin((uv.X * direction.X + uv.Y * direction.Y) * inverseLength * frequency *
                                                    6.28318530718F +
                                                phase) *
                                           0.5F +
                                       0.5F;
                    result = {.Data = {wave, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
                    break;
                }
                case Keire::ShaderGraphNodeKind::TriplanarSample:
                {
                    const auto texture = NamedInput(node, "Texture");
                    const auto position =
                        Coerce(NamedInput(node, "Position"), Keire::ShaderGraphValueType::Vector3).Data;
                    const auto normalValue =
                        Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const float sharpness = std::max(
                        std::abs(
                            Coerce(NamedInput(node, "Blend Sharpness"), Keire::ShaderGraphValueType::Scalar).Data.X),
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
                    const float strength =
                        Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const auto normal =
                        Normalize({-(height - 0.5F) * strength, -(height - 0.5F) * strength, 1.0F}, m_Normal);
                    result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F},
                              .Type = Keire::ShaderGraphValueType::Vector3};
                    break;
                }
                case Keire::ShaderGraphNodeKind::FlattenNormal:
                {
                    const auto normalValue =
                        Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    const float strength = std::clamp(
                        Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
                    const auto normal = Normalize(
                        {normalValue.X * strength, normalValue.Y * strength, std::lerp(1.0F, normalValue.Z, strength)},
                        m_Normal);
                    result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F},
                              .Type = Keire::ShaderGraphValueType::Vector3};
                    break;
                }
                case Keire::ShaderGraphNodeKind::MakeMaterialAttributes:
                {
                    PreviewMaterialSurface surface;
                    surface.BaseColor = Coerce(NamedInput(node, "BaseColor"), Keire::ShaderGraphValueType::Color).Data;
                    surface.Metallic = Coerce(NamedInput(node, "Metallic"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Roughness =
                        Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.ClearCoat =
                        Coerce(NamedInput(node, "ClearCoat"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.ClearCoatRoughness =
                        Coerce(NamedInput(node, "ClearCoatRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.SheenColor =
                        Coerce(NamedInput(node, "SheenColor"), Keire::ShaderGraphValueType::Color).Data;
                    surface.SheenRoughness =
                        Coerce(NamedInput(node, "SheenRoughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    surface.Normal = {normal.X, normal.Y, normal.Z};
                    surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
                    surface.Occlusion =
                        Coerce(NamedInput(node, "Occlusion"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.SubsurfaceColor =
                        Coerce(NamedInput(node, "SubsurfaceColor"), Keire::ShaderGraphValueType::Color).Data;
                    surface.Subsurface =
                        Coerce(NamedInput(node, "Subsurface"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Anisotropy =
                        Coerce(NamedInput(node, "Anisotropy"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const auto tangent = Coerce(NamedInput(node, "Tangent"), Keire::ShaderGraphValueType::Vector3).Data;
                    surface.Tangent = {tangent.X, tangent.Y, tangent.Z};
                    surface.Transmission =
                        Coerce(NamedInput(node, "Transmission"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.IndexOfRefraction =
                        Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Refraction =
                        Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Thickness =
                        Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
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
                    surface.Roughness =
                        Coerce(NamedInput(node, "Roughness"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Specular = Coerce(NamedInput(node, "Specular"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
                    surface.Normal = {normal.X, normal.Y, normal.Z};
                    surface.Emission = Coerce(NamedInput(node, "Emission"), Keire::ShaderGraphValueType::Color).Data;
                    surface.Opacity = Coerce(NamedInput(node, "Opacity"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                    break;
                }
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
                    surface.SubsurfaceColor =
                        Coerce(NamedInput(node, "Color"), Keire::ShaderGraphValueType::Color).Data;
                    surface.Subsurface = Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    result = {.Type = Keire::ShaderGraphValueType::Bsdf, .Surface = surface};
                    break;
                }
                case Keire::ShaderGraphNodeKind::TransmissionBsdf:
                {
                    auto surface = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Bsdf)
                                       .Surface.value_or(PreviewMaterialSurface{});
                    surface.Transmission =
                        Coerce(NamedInput(node, "Weight"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.IndexOfRefraction =
                        Coerce(NamedInput(node, "IndexOfRefraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Refraction =
                        Coerce(NamedInput(node, "Refraction"), Keire::ShaderGraphValueType::Scalar).Data.X;
                    surface.Thickness =
                        Coerce(NamedInput(node, "Thickness"), Keire::ShaderGraphValueType::Scalar).Data.X;
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
                case Keire::ShaderGraphNodeKind::Master:
                    throw std::invalid_argument("The Shader Output node cannot be used as an expression.");
                }
                m_Cache[cacheIndex->second] = result;
                return result;
            }

            const Keire::ShaderGraphDefinition& m_Definition;
            std::span<const Keire::ShaderPropertyDefinition> m_Properties;
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
        [[nodiscard]] PreviewMaterial ResolveMaterial(const ShaderGraphPreviewRequest& request,
                                                      GraphPreviewEvaluator* evaluator = nullptr,
                                                      const Keire::Vector2 uv = {},
                                                      const Keire::Vector3 normal = {0.0F, 0.0F, 1.0F},
                                                      const Keire::Vector3 position = {})
        {
            PreviewMaterial result;
            result.Unlit = request.Output == Keire::ShaderGraphOutput::Unlit ||
                           request.Output == Keire::ShaderGraphOutput::Fullscreen;
            bool foundColor = false;
            for (const auto& property : request.Properties)
            {
                const auto name = Lower(property.Name);
                if (property.Type == Keire::ShaderPropertyType::Texture2D)
                {
                    result.HasBaseTexture |= property.TextureSemantic == Keire::ShaderTextureSemantic::BaseColor;
                    continue;
                }
                if ((name == "basecolor" || name == "color" || name == "tint" ||
                     (!foundColor && property.Type == Keire::ShaderPropertyType::Color)))
                {
                    result.BaseColor = property.DefaultValue;
                    foundColor = true;
                }
                else if (name == "metallic")
                    result.Metallic = property.DefaultValue.X;
                else if (name == "roughness")
                    result.Roughness = property.DefaultValue.X;
                else if (name == "specular")
                    result.Specular = property.DefaultValue.X;
                else if (name == "clearcoat")
                    result.ClearCoat = property.DefaultValue.X;
                else if (name == "clearcoatroughness")
                    result.ClearCoatRoughness = property.DefaultValue.X;
                else if (name == "sheencolor")
                    result.SheenColor = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
                else if (name == "sheenroughness")
                    result.SheenRoughness = property.DefaultValue.X;
                else if (name == "opacity")
                    result.Opacity = property.DefaultValue.X;
                else if (name == "emission" || name == "emissive" || name.find("emission") != std::string::npos ||
                         name.find("emissive") != std::string::npos)
                    result.Emission = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
            }
            if (evaluator)
            {
                result.HasBaseTexture = false;
                evaluator->SetContext(uv, normal, position);
                if (const auto attributes = evaluator->MasterAttributes())
                {
                    result.BaseColor = attributes->BaseColor;
                    result.Emission = {attributes->Emission.X, attributes->Emission.Y, attributes->Emission.Z};
                    result.Metallic = attributes->Metallic;
                    result.Roughness = attributes->Roughness;
                    result.Specular = attributes->Specular;
                    result.ClearCoat = attributes->ClearCoat;
                    result.ClearCoatRoughness = attributes->ClearCoatRoughness;
                    result.SheenColor = {attributes->SheenColor.X, attributes->SheenColor.Y, attributes->SheenColor.Z};
                    result.SheenRoughness = attributes->SheenRoughness;
                    result.Opacity = attributes->Opacity;
                    result.Occlusion = attributes->Occlusion;
                    result.Normal = attributes->Normal;
                    result.HasNormal = true;
                }
                else
                {
                    const auto colorName = result.Unlit ? "Color" : "BaseColor";
                    if (const auto value = evaluator->MasterInput(colorName, Keire::ShaderGraphValueType::Color))
                        result.BaseColor = *value;
                    if (const auto value = evaluator->MasterInput("Emission", Keire::ShaderGraphValueType::Color))
                        result.Emission = {value->X, value->Y, value->Z};
                    if (const auto value = evaluator->MasterInput("Metallic", Keire::ShaderGraphValueType::Scalar))
                        result.Metallic = value->X;
                    if (const auto value = evaluator->MasterInput("Roughness", Keire::ShaderGraphValueType::Scalar))
                        result.Roughness = value->X;
                    if (const auto value = evaluator->MasterInput("Specular", Keire::ShaderGraphValueType::Scalar))
                        result.Specular = value->X;
                    if (const auto value = evaluator->MasterInput("ClearCoat", Keire::ShaderGraphValueType::Scalar))
                        result.ClearCoat = value->X;
                    if (const auto value =
                            evaluator->MasterInput("ClearCoatRoughness", Keire::ShaderGraphValueType::Scalar))
                        result.ClearCoatRoughness = value->X;
                    if (const auto value = evaluator->MasterInput("SheenColor", Keire::ShaderGraphValueType::Color))
                        result.SheenColor = {value->X, value->Y, value->Z};
                    if (const auto value =
                            evaluator->MasterInput("SheenRoughness", Keire::ShaderGraphValueType::Scalar))
                        result.SheenRoughness = value->X;
                    if (const auto value = evaluator->MasterInput("Opacity", Keire::ShaderGraphValueType::Scalar))
                        result.Opacity = value->X;
                    if (const auto value = evaluator->MasterInput("Occlusion", Keire::ShaderGraphValueType::Scalar))
                        result.Occlusion = value->X;
                    if (const auto value = evaluator->MasterInput("Normal", Keire::ShaderGraphValueType::Vector3))
                    {
                        result.Normal = {value->X, value->Y, value->Z};
                        result.HasNormal = true;
                    }
                }
            }
            result.BaseColor.X = Clamp01(result.BaseColor.X);
            result.BaseColor.Y = Clamp01(result.BaseColor.Y);
            result.BaseColor.Z = Clamp01(result.BaseColor.Z);
            result.BaseColor.W = Clamp01(result.BaseColor.W);
            result.Metallic = Clamp01(result.Metallic);
            result.Roughness = std::clamp(result.Roughness, 0.04F, 1.0F);
            result.Specular = Clamp01(result.Specular);
            result.ClearCoat = Clamp01(result.ClearCoat);
            result.ClearCoatRoughness = std::clamp(result.ClearCoatRoughness, 0.04F, 1.0F);
            result.SheenColor.X = Clamp01(result.SheenColor.X);
            result.SheenColor.Y = Clamp01(result.SheenColor.Y);
            result.SheenColor.Z = Clamp01(result.SheenColor.Z);
            result.SheenRoughness = std::clamp(result.SheenRoughness, 0.04F, 1.0F);
            result.Opacity = Clamp01(result.Opacity);
            result.Occlusion = Clamp01(result.Occlusion);
            return result;
        }

        [[nodiscard]] PreviewGeometry SphereGeometry()
        {
            constexpr std::uint32_t rings = 24;
            constexpr std::uint32_t segments = 36;
            constexpr float pi = 3.14159265358979323846F;
            PreviewGeometry result;
            result.Vertices.reserve(static_cast<std::size_t>(rings + 1U) * (segments + 1U));
            result.Indices.reserve(static_cast<std::size_t>(rings) * segments * 6U);
            for (std::uint32_t ring = 0; ring <= rings; ++ring)
            {
                const float v = static_cast<float>(ring) / rings;
                const float latitude = (0.5F - v) * pi;
                const float radius = std::cos(latitude);
                const float y = std::sin(latitude);
                for (std::uint32_t segment = 0; segment <= segments; ++segment)
                {
                    const float u = static_cast<float>(segment) / segments;
                    const float longitude = u * pi * 2.0F;
                    const Keire::Vector3 normal{radius * std::sin(longitude), y, radius * std::cos(longitude)};
                    result.Vertices.push_back({normal, normal, {u, v}});
                }
            }
            for (std::uint32_t ring = 0; ring < rings; ++ring)
                for (std::uint32_t segment = 0; segment < segments; ++segment)
                {
                    const auto first = ring * (segments + 1U) + segment;
                    const auto second = first + segments + 1U;
                    result.Indices.insert(result.Indices.end(),
                                          {first, second, first + 1U, first + 1U, second, second + 1U});
                }
            return result;
        }

        [[nodiscard]] PreviewGeometry PlaneGeometry()
        {
            return {{{{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
                     {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
                     {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
                     {{-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}}},
                    {0, 1, 2, 0, 2, 3}};
        }

        [[nodiscard]] PreviewGeometry FromMesh(const Keire::MeshAsset& mesh)
        {
            PreviewGeometry result;
            const auto vertices = mesh.Vertices();
            const auto indices = mesh.Indices();
            if (vertices.empty() || indices.size() < 3 || indices.size() / 3 > MaximumPreviewTriangles)
                throw std::invalid_argument("Shader Graph preview mesh has unsupported geometry.");
            result.Vertices.reserve(vertices.size());
            for (const auto& vertex : vertices)
                result.Vertices.push_back({vertex.Position, vertex.Normal, vertex.UV0});
            result.Indices.assign(indices.begin(), indices.end());
            return result;
        }

        void PutPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                      const std::uint32_t y, const std::array<float, 4> color)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            for (std::size_t channel = 0; channel < 4; ++channel)
                pixels[offset + channel] = static_cast<std::byte>(std::lround(Clamp01(color[channel]) * 255.0F));
        }

        [[nodiscard]] std::array<float, 3> Background(const std::uint32_t x, const std::uint32_t y) noexcept
        {
            const float checker = ((x / 14U + y / 14U) & 1U) == 0 ? 0.115F : 0.145F;
            const float vignette = 1.0F - std::min(0.28F, std::abs(static_cast<float>(x % 256U) - 128.0F) / 900.0F);
            return {checker * vignette, (checker + 0.012F) * vignette, (checker + 0.025F) * vignette};
        }

        [[nodiscard]] std::array<float, 4> Shade(const PreviewMaterial& material, const Keire::Vector3 normal,
                                                 const Keire::Vector2 uv, const float exposure,
                                                 const float environmentIntensity)
        {
            const auto n = Normalize(material.HasNormal ? material.Normal : normal, Normalize(normal));
            const auto light = Normalize(Keire::Vector3{-0.45F, 0.62F, 0.68F});
            const auto view = Keire::Vector3{0.0F, 0.0F, 1.0F};
            const auto halfVector = Normalize({light.X + view.X, light.Y + view.Y, light.Z + view.Z});
            const float noL = Clamp01(Dot(n, light));
            const float noH = Clamp01(Dot(n, halfVector));
            const float noV = Clamp01(Dot(n, view));
            const float texture =
                material.HasBaseTexture
                    ? (((static_cast<int>(std::floor(uv.X * 10.0F)) + static_cast<int>(std::floor(uv.Y * 10.0F))) &
                        1) == 0
                           ? 0.88F
                           : 0.58F)
                    : 1.0F;
            const float glossExponent = 4.0F + (1.0F - material.Roughness) * 124.0F;
            const float dielectric = 0.08F * material.Specular;
            const float specular =
                std::pow(noH, glossExponent) * (dielectric + material.Metallic * (1.0F - dielectric));
            const float clearCoatExponent = 4.0F + (1.0F - material.ClearCoatRoughness) * 252.0F;
            const float clearCoat = std::pow(noH, clearCoatExponent) * material.ClearCoat * 0.32F;
            const float sheen = std::pow(1.0F - noV, 2.0F + material.SheenRoughness * 4.0F);
            const float diffuse =
                material.Unlit ? 1.0F : 0.08F * environmentIntensity * material.Occlusion + noL * 0.82F;
            const float diffuseWeight = material.Unlit ? 1.0F : 1.0F - material.Metallic * 0.72F;
            const auto channel = [&](const float base, const float emission, const float sheenChannel)
            {
                const float linear =
                    base * texture * diffuse * diffuseWeight + specular + clearCoat + sheenChannel * sheen + emission;
                return 1.0F - std::exp(-std::max(linear, 0.0F) * exposure);
            };
            return {channel(material.BaseColor.X, material.Emission.X, material.SheenColor.X),
                    channel(material.BaseColor.Y, material.Emission.Y, material.SheenColor.Y),
                    channel(material.BaseColor.Z, material.Emission.Z, material.SheenColor.Z),
                    material.BaseColor.W * material.Opacity};
        }

        [[nodiscard]] Keire::Vector3 Rotate(const Keire::Vector3 value, const float rotationDegrees) noexcept
        {
            constexpr float radiansPerDegree = 0.01745329251994329577F;
            const float yaw = rotationDegrees * radiansPerDegree;
            const float cosineYaw = std::cos(yaw);
            const float sineYaw = std::sin(yaw);
            constexpr float cosinePitch = 0.97133797F;
            constexpr float sinePitch = -0.23770263F;
            const float x = cosineYaw * value.X + sineYaw * value.Z;
            const float z = -sineYaw * value.X + cosineYaw * value.Z;
            return {x, cosinePitch * value.Y - sinePitch * z, sinePitch * value.Y + cosinePitch * z};
        }

        void Rasterize(const PreviewGeometry& geometry, const ShaderGraphPreviewRequest& request,
                       GraphPreviewEvaluator* evaluator, const std::uint32_t width, const std::uint32_t height,
                       const float exposure, const float environmentIntensity, const float rotationDegrees,
                       std::vector<std::byte>& pixels)
        {
            Keire::Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max()};
            Keire::Vector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                   std::numeric_limits<float>::lowest()};
            for (const auto& vertex : geometry.Vertices)
            {
                minimum.X = std::min(minimum.X, vertex.Position.X);
                minimum.Y = std::min(minimum.Y, vertex.Position.Y);
                minimum.Z = std::min(minimum.Z, vertex.Position.Z);
                maximum.X = std::max(maximum.X, vertex.Position.X);
                maximum.Y = std::max(maximum.Y, vertex.Position.Y);
                maximum.Z = std::max(maximum.Z, vertex.Position.Z);
            }
            const Keire::Vector3 center{(minimum.X + maximum.X) * 0.5F, (minimum.Y + maximum.Y) * 0.5F,
                                        (minimum.Z + maximum.Z) * 0.5F};
            const float extent =
                std::max({maximum.X - minimum.X, maximum.Y - minimum.Y, maximum.Z - minimum.Z, 1.0e-4F});
            const float scale = static_cast<float>(std::min(width, height)) * 0.78F / extent;
            std::vector<ProjectedVertex> projected;
            projected.reserve(geometry.Vertices.size());
            for (const auto& vertex : geometry.Vertices)
            {
                const auto position =
                    Rotate({vertex.Position.X - center.X, vertex.Position.Y - center.Y, vertex.Position.Z - center.Z},
                           rotationDegrees);
                projected.push_back({static_cast<float>(width) * 0.5F + position.X * scale,
                                     static_cast<float>(height) * 0.51F - position.Y * scale, -position.Z, position,
                                     Normalize(Rotate(vertex.Normal, rotationDegrees)), vertex.UV});
            }

            std::vector<float> depth(static_cast<std::size_t>(width) * height, std::numeric_limits<float>::infinity());
            const auto edge =
                [](const ProjectedVertex& first, const ProjectedVertex& second, const float x, const float y)
            { return (x - first.X) * (second.Y - first.Y) - (y - first.Y) * (second.X - first.X); };
            for (std::size_t index = 0; index + 2 < geometry.Indices.size(); index += 3)
            {
                CheckPreviewCancellation(request);
                const auto i0 = geometry.Indices[index];
                const auto i1 = geometry.Indices[index + 1];
                const auto i2 = geometry.Indices[index + 2];
                if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size())
                    continue;
                const auto& first = projected[i0];
                const auto& second = projected[i1];
                const auto& third = projected[i2];
                const float area = edge(first, second, third.X, third.Y);
                if (!std::isfinite(area) || std::abs(area) <= 1.0e-5F)
                    continue;
                const int minimumX = std::max(0, static_cast<int>(std::floor(std::min({first.X, second.X, third.X}))));
                const int maximumX = std::min(static_cast<int>(width) - 1,
                                              static_cast<int>(std::ceil(std::max({first.X, second.X, third.X}))));
                const int minimumY = std::max(0, static_cast<int>(std::floor(std::min({first.Y, second.Y, third.Y}))));
                const int maximumY = std::min(static_cast<int>(height) - 1,
                                              static_cast<int>(std::ceil(std::max({first.Y, second.Y, third.Y}))));
                for (int y = minimumY; y <= maximumY; ++y)
                {
                    CheckPreviewCancellation(request);
                    for (int x = minimumX; x <= maximumX; ++x)
                    {
                        const float sampleX = static_cast<float>(x) + 0.5F;
                        const float sampleY = static_cast<float>(y) + 0.5F;
                        const float w0 = edge(second, third, sampleX, sampleY) / area;
                        const float w1 = edge(third, first, sampleX, sampleY) / area;
                        const float w2 = 1.0F - w0 - w1;
                        if (w0 < -1.0e-5F || w1 < -1.0e-5F || w2 < -1.0e-5F)
                            continue;
                        const float candidateDepth = first.Depth * w0 + second.Depth * w1 + third.Depth * w2;
                        const auto pixel = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                        if (candidateDepth >= depth[pixel])
                            continue;
                        depth[pixel] = candidateDepth;
                        const Keire::Vector3 normal{first.Normal.X * w0 + second.Normal.X * w1 + third.Normal.X * w2,
                                                    first.Normal.Y * w0 + second.Normal.Y * w1 + third.Normal.Y * w2,
                                                    first.Normal.Z * w0 + second.Normal.Z * w1 + third.Normal.Z * w2};
                        const Keire::Vector3 position{
                            first.Position.X * w0 + second.Position.X * w1 + third.Position.X * w2,
                            first.Position.Y * w0 + second.Position.Y * w1 + third.Position.Y * w2,
                            first.Position.Z * w0 + second.Position.Z * w1 + third.Position.Z * w2};
                        const Keire::Vector2 uv{first.UV.X * w0 + second.UV.X * w1 + third.UV.X * w2,
                                                first.UV.Y * w0 + second.UV.Y * w1 + third.UV.Y * w2};
                        const auto material = ResolveMaterial(request, evaluator, uv, normal, position);
                        const auto shaded = Shade(material, normal, uv, exposure, environmentIntensity);
                        const auto background =
                            Background(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                        const float alpha = Clamp01(shaded[3]);
                        PutPixel(pixels, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                 {shaded[0] * alpha + background[0] * (1.0F - alpha),
                                  shaded[1] * alpha + background[1] * (1.0F - alpha),
                                  shaded[2] * alpha + background[2] * (1.0F - alpha), 1.0F});
                    }
                }
            }
        }
    } // namespace

    std::vector<std::byte> RenderShaderGraphPreview(const ShaderGraphPreviewRequest& request)
    {
        if (request.Width < 32 || request.Height < 32 || request.Width > 2048 || request.Height > 2048)
            throw std::invalid_argument("Shader Graph preview dimensions must be in the range 32..2048.");
        if (!std::isfinite(request.Exposure) || request.Exposure < 0.1F || request.Exposure > 8.0F ||
            !std::isfinite(request.EnvironmentIntensity) || request.EnvironmentIntensity < 0.0F ||
            request.EnvironmentIntensity > 8.0F || !std::isfinite(request.RotationDegrees) ||
            request.RotationDegrees < -180.0F || request.RotationDegrees > 180.0F)
            throw std::invalid_argument("Shader Graph preview lighting controls are outside their supported range.");
        std::vector<std::byte> pixels(static_cast<std::size_t>(request.Width) * request.Height * 4U);
        for (std::uint32_t y = 0; y < request.Height; ++y)
        {
            CheckPreviewCancellation(request);
            for (std::uint32_t x = 0; x < request.Width; ++x)
            {
                const auto background = Background(x, y);
                PutPixel(pixels, request.Width, x, y, {background[0], background[1], background[2], 1.0F});
            }
        }

        PreviewGeometry geometry;
        switch (request.Mesh)
        {
        case Keire::ShaderGraphPreviewMesh::Sphere:
            geometry = SphereGeometry();
            break;
        case Keire::ShaderGraphPreviewMesh::Plane:
            geometry = PlaneGeometry();
            break;
        case Keire::ShaderGraphPreviewMesh::Cube:
            geometry = FromMesh(*Keire::MeshAsset::Cube());
            break;
        case Keire::ShaderGraphPreviewMesh::Custom:
            if (!request.CustomMesh)
                throw std::invalid_argument("Custom Shader Graph preview mesh is unavailable.");
            geometry = FromMesh(*request.CustomMesh);
            break;
        default:
            throw std::invalid_argument("Shader Graph preview mesh is invalid.");
        }
        std::unique_ptr<GraphPreviewEvaluator> evaluator;
        if (request.Definition)
            evaluator = std::make_unique<GraphPreviewEvaluator>(*request.Definition, request.Properties);
        Rasterize(geometry, request, evaluator.get(), request.Width, request.Height, request.Exposure,
                  request.EnvironmentIntensity, request.RotationDegrees, pixels);
        return pixels;
    }
} // namespace KeireEditor
