#include "KeireClient/Editor/VfxEmitterInspector.h"

#include "Keire/Assets/RenderingAssets.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool ValueMatches(const Keire::VfxValueType type, const Keire::VfxParameterValue& value) noexcept
        {
            return Keire::VfxValueMatchesType(type, value) && Keire::IsFiniteVfxValue(value);
        }

        [[nodiscard]] std::string TypeName(const Keire::VfxValueType type)
        {
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return "Boolean";
            case Keire::VfxValueType::Integer:
                return "Integer";
            case Keire::VfxValueType::UnsignedInteger:
                return "Unsigned Integer";
            case Keire::VfxValueType::Scalar:
                return "Scalar";
            case Keire::VfxValueType::Vector2:
                return "Vector2";
            case Keire::VfxValueType::Vector3:
                return "Vector3";
            case Keire::VfxValueType::Vector4:
                return "Vector4";
            case Keire::VfxValueType::Quaternion:
                return "Quaternion";
            case Keire::VfxValueType::Color:
                return "Color";
            case Keire::VfxValueType::Texture:
                return "Texture";
            case Keire::VfxValueType::Mesh:
                return "Mesh";
            case Keire::VfxValueType::Asset:
                return "Asset";
            case Keire::VfxValueType::Matrix:
                return "Matrix4";
            case Keire::VfxValueType::Curve:
                return "Curve";
            case Keire::VfxValueType::Gradient:
                return "Gradient";
            case Keire::VfxValueType::ScalarRange:
                return "Scalar Range";
            case Keire::VfxValueType::IntegerRange:
                return "Integer Range";
            case Keire::VfxValueType::UnsignedIntegerRange:
                return "Unsigned Integer Range";
            case Keire::VfxValueType::Vector2Range:
                return "Vector2 Range";
            case Keire::VfxValueType::Vector3Range:
                return "Vector3 Range";
            case Keire::VfxValueType::Vector4Range:
                return "Vector4 Range";
            case Keire::VfxValueType::ColorRange:
                return "Color Range";
            case Keire::VfxValueType::Texture2DArray:
                return "Texture2D Array";
            case Keire::VfxValueType::Texture3D:
                return "Texture3D";
            case Keire::VfxValueType::TextureCube:
                return "Texture Cube";
            case Keire::VfxValueType::Buffer:
                return "Buffer";
            case Keire::VfxValueType::PointCache:
                return "Point Cache";
            case Keire::VfxValueType::SignedDistanceField:
                return "Signed Distance Field";
            case Keire::VfxValueType::ParticleStream:
                return "Particle Stream";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string FormatValue(const Keire::VfxParameterValue& value)
        {
            return std::visit(
                [](const auto& item)
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, bool>)
                        return std::string(item ? "On" : "Off");
                    else if constexpr (std::same_as<T, std::int64_t>)
                        return std::to_string(item);
                    else if constexpr (std::same_as<T, std::uint64_t>)
                        return std::to_string(item);
                    else if constexpr (std::same_as<T, Keire::AssetId>)
                        return item ? item.ToString() : std::string("None");
                    else
                    {
                        std::ostringstream result;
                        if constexpr (std::same_as<T, float>)
                            result << item;
                        else if constexpr (std::same_as<T, Keire::Vector2>)
                            result << '(' << item.X << ", " << item.Y << ')';
                        else if constexpr (std::same_as<T, Keire::Vector3>)
                            result << '(' << item.X << ", " << item.Y << ", " << item.Z << ')';
                        else if constexpr (std::same_as<T, Keire::Vector4> || std::same_as<T, Keire::Quaternion>)
                            result << '(' << item.X << ", " << item.Y << ", " << item.Z << ", " << item.W << ')';
                        else if constexpr (std::same_as<T, Keire::Color>)
                            result << '(' << item.Red << ", " << item.Green << ", " << item.Blue << ", " << item.Alpha
                                   << ')';
                        else if constexpr (std::same_as<T, Keire::Matrix4>)
                            result << "Matrix4";
                        else if constexpr (std::same_as<T, Keire::Curve1D>)
                            result << item.Keys().size() << " curve keys";
                        else if constexpr (std::same_as<T, Keire::ColorGradient>)
                            result << item.Keys().size() << " gradient keys";
                        else if constexpr (requires {
                                               item.Minimum;
                                               item.Maximum;
                                           })
                            result << "Range";
                        return result.str();
                    }
                },
                value);
        }

        [[nodiscard]] std::string ParameterLabel(const Keire::VfxBlackboardParameter& parameter)
        {
            const auto displayName = parameter.Name.empty() ? std::string("Unnamed Parameter") : parameter.Name;
            return displayName + "###VfxBlackboard-" + parameter.Id.ToString();
        }

        [[nodiscard]] bool DrawValue(IPropertyEditor& editor, const Keire::VfxBlackboardParameter& parameter,
                                     Keire::VfxParameterValue& value)
        {
            if (!ValueMatches(parameter.Type, value))
                throw std::invalid_argument("VFX Blackboard parameter '" + parameter.Name +
                                            "' has an incompatible default value.");
            const auto label = ParameterLabel(parameter);
            switch (parameter.Type)
            {
            case Keire::VfxValueType::Boolean:
                return editor.EditBoolean(label, std::get<bool>(value));
            case Keire::VfxValueType::Integer:
                return editor.EditInteger(label, std::get<std::int64_t>(value), 1.0, {}, {});
            case Keire::VfxValueType::UnsignedInteger:
            {
                double scalar = static_cast<double>(std::get<std::uint64_t>(value));
                if (!editor.EditScalar(label, scalar, 1.0, 0.0, {}))
                    return false;
                value = static_cast<std::uint64_t>(scalar);
                return true;
            }
            case Keire::VfxValueType::Scalar:
            {
                double scalar = std::get<float>(value);
                if (!editor.EditScalar(label, scalar, 0.05, {}, {}))
                    return false;
                value = static_cast<float>(scalar);
                return true;
            }
            case Keire::VfxValueType::Vector2:
                return editor.EditVector2(label, std::get<Keire::Vector2>(value), 0.05);
            case Keire::VfxValueType::Vector3:
                return editor.EditVector3(label, std::get<Keire::Vector3>(value), 0.05);
            case Keire::VfxValueType::Vector4:
                return editor.EditVector4(label, std::get<Keire::Vector4>(value), 0.05);
            case Keire::VfxValueType::Quaternion:
                return editor.EditQuaternion(label, std::get<Keire::Quaternion>(value), 0.05);
            case Keire::VfxValueType::Color:
                return editor.EditColor(label, std::get<Keire::Color>(value));
            case Keire::VfxValueType::Texture:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), Keire::Texture2DAsset::StaticType());
            case Keire::VfxValueType::Mesh:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), Keire::MeshAsset::StaticType());
            case Keire::VfxValueType::Asset:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), {});
            case Keire::VfxValueType::Matrix:
            {
                auto& matrix = std::get<Keire::Matrix4>(value);
                bool changed = false;
                for (std::size_t row = 0; row < 4; ++row)
                {
                    Keire::Vector4 values{matrix.Elements[row * 4], matrix.Elements[row * 4 + 1],
                                          matrix.Elements[row * 4 + 2], matrix.Elements[row * 4 + 3]};
                    if (editor.EditVector4(label + " Row " + std::to_string(row + 1), values, 0.05))
                    {
                        matrix.Elements[row * 4] = values.X;
                        matrix.Elements[row * 4 + 1] = values.Y;
                        matrix.Elements[row * 4 + 2] = values.Z;
                        matrix.Elements[row * 4 + 3] = values.W;
                        changed = true;
                    }
                }
                return changed;
            }
            case Keire::VfxValueType::Curve:
                return editor.EditCurve(label, std::get<Keire::Curve1D>(value));
            case Keire::VfxValueType::Gradient:
                return editor.EditGradient(label, std::get<Keire::ColorGradient>(value));
            case Keire::VfxValueType::ScalarRange:
            {
                auto& range = std::get<Keire::VfxScalarRange>(value);
                double minimum = range.Minimum;
                double maximum = range.Maximum;
                const bool changed = editor.EditScalar(label + " Min", minimum, 0.05, {}, {}) |
                                     editor.EditScalar(label + " Max", maximum, 0.05, {}, {});
                range = {static_cast<float>(minimum), static_cast<float>(maximum)};
                return changed;
            }
            case Keire::VfxValueType::IntegerRange:
            {
                auto& range = std::get<Keire::VfxIntegerRange>(value);
                return editor.EditInteger(label + " Min", range.Minimum, 1.0, {}, {}) |
                       editor.EditInteger(label + " Max", range.Maximum, 1.0, {}, {});
            }
            case Keire::VfxValueType::UnsignedIntegerRange:
            {
                auto& range = std::get<Keire::VfxUnsignedIntegerRange>(value);
                double minimum = static_cast<double>(range.Minimum);
                double maximum = static_cast<double>(range.Maximum);
                const bool changed = editor.EditScalar(label + " Min", minimum, 1.0, 0.0, {}) |
                                     editor.EditScalar(label + " Max", maximum, 1.0, 0.0, {});
                range = {static_cast<std::uint64_t>(minimum), static_cast<std::uint64_t>(maximum)};
                return changed;
            }
            case Keire::VfxValueType::Vector2Range:
            {
                auto& range = std::get<Keire::VfxVector2Range>(value);
                return editor.EditVector2(label + " Min", range.Minimum, 0.05) |
                       editor.EditVector2(label + " Max", range.Maximum, 0.05);
            }
            case Keire::VfxValueType::Vector3Range:
            {
                auto& range = std::get<Keire::VfxVector3Range>(value);
                return editor.EditVector3(label + " Min", range.Minimum, 0.05) |
                       editor.EditVector3(label + " Max", range.Maximum, 0.05);
            }
            case Keire::VfxValueType::Vector4Range:
            {
                auto& range = std::get<Keire::VfxVector4Range>(value);
                return editor.EditVector4(label + " Min", range.Minimum, 0.05) |
                       editor.EditVector4(label + " Max", range.Maximum, 0.05);
            }
            case Keire::VfxValueType::ColorRange:
            {
                auto& range = std::get<Keire::VfxColorRange>(value);
                return editor.EditColor(label + " Min", range.Minimum) |
                       editor.EditColor(label + " Max", range.Maximum);
            }
            case Keire::VfxValueType::Texture2DArray:
            case Keire::VfxValueType::Texture3D:
            case Keire::VfxValueType::TextureCube:
            case Keire::VfxValueType::Buffer:
            case Keire::VfxValueType::PointCache:
            case Keire::VfxValueType::SignedDistanceField:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), {});
            case Keire::VfxValueType::ParticleStream:
                throw std::invalid_argument("Particle streams cannot be exposed as VFX Blackboard overrides.");
            }
            return false;
        }

        void Canonicalize(std::vector<Keire::VfxParameterOverride>& overrides)
        {
            std::ranges::sort(overrides, {}, &Keire::VfxParameterOverride::Parameter);
            if (std::ranges::adjacent_find(overrides, {}, &Keire::VfxParameterOverride::Parameter) != overrides.end())
                throw std::invalid_argument("VFX Blackboard overrides contain a duplicate stable ID.");
        }

        void SetOverride(std::vector<Keire::VfxParameterOverride>& overrides, const Keire::AssetId parameter,
                         Keire::VfxParameterValue value)
        {
            const auto found =
                std::ranges::lower_bound(overrides, parameter, {}, &Keire::VfxParameterOverride::Parameter);
            if (found == overrides.end() || found->Parameter != parameter)
                overrides.insert(found, {parameter, std::move(value)});
            else
                found->Value = std::move(value);
        }

        [[nodiscard]] bool RemoveOverride(std::vector<Keire::VfxParameterOverride>& overrides,
                                          const Keire::AssetId parameter)
        {
            return std::erase_if(overrides, [parameter](const Keire::VfxParameterOverride& value)
                                 { return value.Parameter == parameter; }) != 0;
        }
    } // namespace

    std::size_t
    VfxEmitterInspector::VisibleEntryCount(const Keire::VfxEffectDefinition& effect,
                                           const std::span<const Keire::VfxParameterOverride> overrides) noexcept
    {
        const auto exposed = static_cast<std::size_t>(
            std::ranges::count(effect.Blackboard, true, &Keire::VfxBlackboardParameter::Exposed));
        const auto stale = static_cast<std::size_t>(std::ranges::count_if(
            overrides,
            [&effect](const Keire::VfxParameterOverride& overrideValue)
            {
                const auto parameter =
                    std::ranges::find(effect.Blackboard, overrideValue.Parameter, &Keire::VfxBlackboardParameter::Id);
                return parameter == effect.Blackboard.end() || !parameter->Exposed;
            }));
        return exposed + stale;
    }

    bool VfxEmitterInspector::Draw(IPropertyEditor& editor, const Keire::VfxEffectDefinition& effect,
                                   std::vector<Keire::VfxParameterOverride>& overrides,
                                   const VfxEmitterInspectorCallbacks& callbacks) const
    {
        Canonicalize(overrides);
        bool changed = false;
        for (const auto& parameter : effect.Blackboard)
        {
            if (!parameter.Exposed)
                continue;
            auto existing =
                std::ranges::lower_bound(overrides, parameter.Id, {}, &Keire::VfxParameterOverride::Parameter);
            const bool hasOverride = existing != overrides.end() && existing->Parameter == parameter.Id;
            const bool compatible = hasOverride && ValueMatches(parameter.Type, existing->Value);
            auto value = compatible ? existing->Value : parameter.DefaultValue;
            if (DrawValue(editor, parameter, value))
            {
                SetOverride(overrides, parameter.Id, std::move(value));
                changed = true;
            }

            existing = std::ranges::lower_bound(overrides, parameter.Id, {}, &Keire::VfxParameterOverride::Parameter);
            const bool overridden = existing != overrides.end() && existing->Parameter == parameter.Id &&
                                    ValueMatches(parameter.Type, existing->Value);
            if (overridden)
            {
                if (callbacks.Status)
                {
                    const auto status = "Override | Default: " + FormatValue(parameter.DefaultValue);
                    callbacks.Status(parameter.Id, status, false);
                }
                if (callbacks.Reset && callbacks.Reset(parameter.Id))
                    changed = RemoveOverride(overrides, parameter.Id) || changed;
            }
            else if (hasOverride)
            {
                if (callbacks.Status)
                {
                    const auto status = "Stale override: expected " + TypeName(parameter.Type) + '.';
                    callbacks.Status(parameter.Id, status, true);
                }
                if (callbacks.RemoveStale && callbacks.RemoveStale(parameter.Id))
                    changed = RemoveOverride(overrides, parameter.Id) || changed;
            }
            else if (callbacks.Status)
            {
                const auto status = "Using default: " + FormatValue(parameter.DefaultValue);
                callbacks.Status(parameter.Id, status, false);
            }
        }

        std::vector<Keire::AssetId> stale;
        for (const auto& overrideValue : overrides)
        {
            const auto parameter =
                std::ranges::find(effect.Blackboard, overrideValue.Parameter, &Keire::VfxBlackboardParameter::Id);
            if (parameter == effect.Blackboard.end() || !parameter->Exposed)
                stale.push_back(overrideValue.Parameter);
        }
        for (const auto parameterId : stale)
        {
            const auto parameter =
                std::ranges::find(effect.Blackboard, parameterId, &Keire::VfxBlackboardParameter::Id);
            if (callbacks.Status)
            {
                const auto status = parameter == effect.Blackboard.end()
                                        ? "Stale override " + parameterId.ToString() + ": parameter no longer exists."
                                        : "Stale override '" + parameter->Name + "': parameter is no longer exposed.";
                callbacks.Status(parameterId, status, true);
            }
            if (callbacks.RemoveStale && callbacks.RemoveStale(parameterId))
                changed = RemoveOverride(overrides, parameterId) || changed;
        }
        return changed;
    }

    std::string VfxEmitterInspector::SerializeOverrides(const Keire::ComponentRegistration& registration,
                                                        const Keire::ComponentPropertyBag& componentValues,
                                                        const std::span<const Keire::VfxParameterOverride> overrides)
    {
        if (registration.Type != Keire::VfxEmitterComponent::StaticType() || !registration.Factory ||
            !registration.Serialize || !registration.Deserialize)
        {
            throw std::invalid_argument("A complete VFX Emitter registration is required to serialize overrides.");
        }
        auto candidate = registration.Factory();
        if (!candidate)
            throw std::runtime_error("The VFX Emitter component factory returned null.");
        registration.Deserialize(*candidate, componentValues, registration.SchemaVersion);
        auto* emitter = dynamic_cast<Keire::VfxEmitterComponent*>(candidate.Get());
        if (!emitter)
            throw std::logic_error("The VFX Emitter registration created an incompatible component.");
        emitter->ClearParameterOverrides();
        for (const auto& overrideValue : overrides)
            emitter->SetParameterOverride(overrideValue);
        const auto serialized = registration.Serialize(*candidate);
        const auto found = serialized.find("parameterOverrides");
        if (found == serialized.end())
            throw std::logic_error("The VFX Emitter registration omitted parameter overrides.");
        const auto* text = std::get_if<std::string>(&found->second);
        if (!text)
            throw std::logic_error("The VFX Emitter registration serialized parameter overrides incorrectly.");
        return *text;
    }
} // namespace KeireEditor
