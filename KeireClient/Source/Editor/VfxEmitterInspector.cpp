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
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return std::holds_alternative<bool>(value);
            case Keire::VfxValueType::Integer:
                return std::holds_alternative<std::int64_t>(value);
            case Keire::VfxValueType::Scalar:
                return std::holds_alternative<float>(value);
            case Keire::VfxValueType::Vector2:
                return std::holds_alternative<Keire::Vector2>(value);
            case Keire::VfxValueType::Vector3:
                return std::holds_alternative<Keire::Vector3>(value);
            case Keire::VfxValueType::Color:
                return std::holds_alternative<Keire::Color>(value);
            case Keire::VfxValueType::Texture:
            case Keire::VfxValueType::Mesh:
            case Keire::VfxValueType::Asset:
                return std::holds_alternative<Keire::AssetId>(value);
            case Keire::VfxValueType::ParticleStream:
                return false;
            }
            return false;
        }

        [[nodiscard]] std::string TypeName(const Keire::VfxValueType type)
        {
            switch (type)
            {
            case Keire::VfxValueType::Boolean:
                return "Boolean";
            case Keire::VfxValueType::Integer:
                return "Integer";
            case Keire::VfxValueType::Scalar:
                return "Scalar";
            case Keire::VfxValueType::Vector2:
                return "Vector2";
            case Keire::VfxValueType::Vector3:
                return "Vector3";
            case Keire::VfxValueType::Color:
                return "Color";
            case Keire::VfxValueType::Texture:
                return "Texture";
            case Keire::VfxValueType::Mesh:
                return "Mesh";
            case Keire::VfxValueType::Asset:
                return "Asset";
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
                        else if constexpr (std::same_as<T, Keire::Color>)
                            result << '(' << item.Red << ", " << item.Green << ", " << item.Blue << ", " << item.Alpha
                                   << ')';
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
            case Keire::VfxValueType::Color:
                return editor.EditColor(label, std::get<Keire::Color>(value));
            case Keire::VfxValueType::Texture:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), Keire::Texture2DAsset::StaticType());
            case Keire::VfxValueType::Mesh:
                return editor.EditAsset(label, std::get<Keire::AssetId>(value), Keire::MeshAsset::StaticType());
            case Keire::VfxValueType::Asset:
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
