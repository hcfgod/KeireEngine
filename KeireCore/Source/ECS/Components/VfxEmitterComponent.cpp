#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Vfx/VfxSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        template <typename T>
        [[nodiscard]] T ReadVfxProperty(const ComponentPropertyBag& values, const std::string_view key,
                                        const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("VFX Emitter property has an incompatible type.");
        }

        [[nodiscard]] bool FiniteOverride(const VfxParameterValue& value) noexcept
        {
            return std::visit(
                [](const auto& item)
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, float>)
                        return std::isfinite(item);
                    else if constexpr (std::same_as<T, Vector2> || std::same_as<T, Vector3> || std::same_as<T, Color>)
                        return Math::IsFinite(item);
                    else
                        return true;
                },
                value);
        }

        [[nodiscard]] Json EncodeOverrideValue(const VfxParameterValue& value)
        {
            return std::visit(
                [](const auto& item) -> Json
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, bool>)
                        return Json{{"kind", "boolean"}, {"value", item}};
                    else if constexpr (std::same_as<T, std::int64_t>)
                        return Json{{"kind", "integer"}, {"value", item}};
                    else if constexpr (std::same_as<T, float>)
                        return Json{{"kind", "scalar"}, {"value", item}};
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json{{"kind", "vector2"}, {"value", Json::array({item.X, item.Y})}};
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json{{"kind", "vector3"}, {"value", Json::array({item.X, item.Y, item.Z})}};
                    else if constexpr (std::same_as<T, Color>)
                    {
                        return Json{{"kind", "color"},
                                    {"value", Json::array({item.Red, item.Green, item.Blue, item.Alpha})}};
                    }
                    else
                        return Json{{"kind", "asset"}, {"value", item ? item.ToString() : std::string{}}};
                },
                value);
        }

        [[nodiscard]] VfxParameterValue DecodeOverrideValue(const Json& encoded)
        {
            const auto kind = encoded.at("kind").get<std::string>();
            const auto& value = encoded.at("value");
            if (kind == "boolean")
                return value.get<bool>();
            if (kind == "integer")
                return value.get<std::int64_t>();
            if (kind == "scalar")
                return value.get<float>();
            if (kind == "vector2" && value.is_array() && value.size() == 2)
                return Vector2{value.at(0).get<float>(), value.at(1).get<float>()};
            if (kind == "vector3" && value.is_array() && value.size() == 3)
                return Vector3{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            if (kind == "color" && value.is_array() && value.size() == 4)
            {
                return Color{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                             value.at(3).get<float>()};
            }
            if (kind == "asset")
            {
                const auto text = value.get<std::string>();
                return text.empty() ? AssetId{} : AssetId::Parse(text);
            }
            throw std::invalid_argument("VFX Emitter parameter override value is malformed.");
        }

        [[nodiscard]] std::string EncodeOverrides(const std::span<const VfxParameterOverride> overrides)
        {
            auto result = Json::array();
            for (const auto& overrideValue : overrides)
            {
                auto encoded = EncodeOverrideValue(overrideValue.Value);
                encoded["parameter"] = overrideValue.Parameter.ToString();
                result.push_back(std::move(encoded));
            }
            return result.dump();
        }

        [[nodiscard]] std::vector<VfxParameterOverride> DecodeOverrides(const std::string& source)
        {
            std::vector<VfxParameterOverride> result;
            std::set<AssetId> unique;
            try
            {
                const auto document = Json::parse(source.empty() ? "[]" : source);
                if (!document.is_array() || document.size() > 1024)
                    throw std::invalid_argument("VFX Emitter parameter overrides must be a bounded array.");
                result.reserve(document.size());
                for (const auto& encoded : document)
                {
                    const auto parameter = AssetId::Parse(encoded.at("parameter").get<std::string>());
                    auto value = DecodeOverrideValue(encoded);
                    if (!parameter || !unique.insert(parameter).second || !FiniteOverride(value))
                        throw std::invalid_argument("VFX Emitter parameter override is invalid.");
                    result.push_back({parameter, std::move(value)});
                }
                std::ranges::sort(result, {}, &VfxParameterOverride::Parameter);
            }
            catch (const std::invalid_argument&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                throw std::invalid_argument(std::string("VFX Emitter parameter overrides are malformed: ") +
                                            error.what());
            }
            return result;
        }
    } // namespace

    VfxEmitterComponent::VfxEmitterComponent() : Component(StaticType()) {}

    void VfxEmitterComponent::SetEffect(const AssetId effect)
    {
        m_Effect = effect;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetPlayOnAwake(const bool value)
    {
        m_PlayOnAwake = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetAutoDestroy(const bool value)
    {
        m_AutoDestroy = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetSimulationSpeed(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 8.0F)
            throw std::invalid_argument("VFX Emitter simulation speed must be finite and in the range 0..8.");
        m_SimulationSpeed = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetSeedOffset(const std::uint32_t value)
    {
        m_SeedOffset = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetQuality(const VfxQualityTier value)
    {
        if (value > VfxQualityTier::Cinematic)
            throw std::invalid_argument("VFX Emitter quality tier is invalid.");
        m_Quality = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetCulling(const VfxCullingMode value)
    {
        if (value > VfxCullingMode::AlwaysSimulate)
            throw std::invalid_argument("VFX Emitter culling mode is invalid.");
        m_Culling = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetBounds(const Vector3 center, const Vector3 extent)
    {
        if (!Math::IsFinite(center) || !Math::IsFinite(extent) || extent.X <= 0.0F || extent.Y <= 0.0F ||
            extent.Z <= 0.0F)
            throw std::invalid_argument("VFX Emitter bounds must be finite and have positive extents.");
        m_BoundsCenter = center;
        m_BoundsExtent = extent;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetEditModePreview(const bool value)
    {
        m_EditModePreview = value;
        NotifyChanged();
    }

    void VfxEmitterComponent::SetParameterOverride(VfxParameterOverride value)
    {
        if (!value.Parameter || !FiniteOverride(value.Value))
            throw std::invalid_argument("VFX Emitter parameter override is invalid.");
        const auto existing =
            std::ranges::lower_bound(m_ParameterOverrides, value.Parameter, {}, &VfxParameterOverride::Parameter);
        if (existing == m_ParameterOverrides.end() || existing->Parameter != value.Parameter)
        {
            if (m_ParameterOverrides.size() >= 1024)
                throw std::invalid_argument("VFX Emitter parameter override limit exceeded.");
            m_ParameterOverrides.insert(existing, std::move(value));
        }
        else
            *existing = std::move(value);
        NotifyChanged();
    }

    bool VfxEmitterComponent::RemoveParameterOverride(const AssetId parameter)
    {
        const auto erased = std::erase_if(m_ParameterOverrides, [parameter](const VfxParameterOverride& value)
                                          { return value.Parameter == parameter; });
        if (erased != 0)
            NotifyChanged();
        return erased != 0;
    }

    void VfxEmitterComponent::ClearParameterOverrides()
    {
        if (m_ParameterOverrides.empty())
            return;
        m_ParameterOverrides.clear();
        NotifyChanged();
    }

    ComponentRegistration CreateVfxEmitterComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = VfxEmitterComponent::StaticType();
        result.Name = "VFX Emitter";
        result.Category = "Effects";
        result.SchemaVersion = 2;
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"effect", "Effect", "VFX", ComponentPropertyKind::Asset, false, {}, {}, 0.1, VfxEffectAsset::StaticType()},
            {"playOnAwake", "Play On Awake", "Playback", ComponentPropertyKind::Boolean},
            {"autoDestroy", "Auto Destroy", "Playback", ComponentPropertyKind::Boolean},
            {"simulationSpeed", "Simulation Speed", "Playback", ComponentPropertyKind::Scalar, false, 0.0, 8.0, 0.05},
            {"seedOffset", "Seed Offset", "Determinism", ComponentPropertyKind::Integer, false, 0.0,
             static_cast<double>(std::numeric_limits<std::uint32_t>::max()), 1.0},
            {"quality", "Quality Tier", "Rendering", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"culling", "Culling Mode", "Rendering", ComponentPropertyKind::Integer, false, 0.0, 2.0, 1.0},
            {"boundsCenter", "Bounds Center", "Culling", ComponentPropertyKind::Vector3},
            {"boundsExtent", "Bounds Extent", "Culling", ComponentPropertyKind::Vector3},
            {"editModePreview", "Preview In Edit Mode", "Authoring", ComponentPropertyKind::Boolean},
            {"parameterOverrides",
             "Parameter Overrides",
             "Blackboard",
             ComponentPropertyKind::Text,
             true,
             {},
             {},
             0.1,
             {},
             "Canonical stable-ID overrides. Use the VFX override inspector or runtime API to edit this value."},
        };
        result.Factory = [] { return Ref<Component>(CreateRef<VfxEmitterComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& emitter = dynamic_cast<const VfxEmitterComponent&>(component);
            return ComponentPropertyBag{
                {"effect", emitter.m_Effect},
                {"playOnAwake", emitter.m_PlayOnAwake},
                {"autoDestroy", emitter.m_AutoDestroy},
                {"simulationSpeed", static_cast<double>(emitter.m_SimulationSpeed)},
                {"seedOffset", static_cast<std::int64_t>(emitter.m_SeedOffset)},
                {"quality", static_cast<std::int64_t>(emitter.m_Quality)},
                {"culling", static_cast<std::int64_t>(emitter.m_Culling)},
                {"boundsCenter", emitter.m_BoundsCenter},
                {"boundsExtent", emitter.m_BoundsExtent},
                {"editModePreview", emitter.m_EditModePreview},
                {"parameterOverrides", EncodeOverrides(emitter.m_ParameterOverrides)},
            };
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 2)
                throw std::invalid_argument("Unsupported VFX Emitter component schema version.");
            auto& emitter = dynamic_cast<VfxEmitterComponent&>(component);
            const auto speed = static_cast<float>(ReadVfxProperty(values, "simulationSpeed", 1.0));
            const auto seedOffset = ReadVfxProperty(values, "seedOffset", std::int64_t{0});
            const auto quality = ReadVfxProperty(values, "quality", std::int64_t{2});
            const auto culling = ReadVfxProperty(values, "culling", std::int64_t{0});
            if (seedOffset < 0 || seedOffset > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("VFX Emitter seed offset is outside the supported range.");
            if (quality < 0 || quality > static_cast<std::int64_t>(VfxQualityTier::Cinematic) || culling < 0 ||
                culling > static_cast<std::int64_t>(VfxCullingMode::AlwaysSimulate))
                throw std::invalid_argument("VFX Emitter quality or culling value is invalid.");
            emitter.SetEffect(ReadVfxProperty(values, "effect", AssetId{}));
            emitter.SetPlayOnAwake(ReadVfxProperty(values, "playOnAwake", true));
            emitter.SetAutoDestroy(ReadVfxProperty(values, "autoDestroy", false));
            emitter.SetSimulationSpeed(speed);
            emitter.SetSeedOffset(static_cast<std::uint32_t>(seedOffset));
            emitter.SetQuality(static_cast<VfxQualityTier>(quality));
            emitter.SetCulling(static_cast<VfxCullingMode>(culling));
            emitter.SetBounds(ReadVfxProperty(values, "boundsCenter", Vector3{}),
                              ReadVfxProperty(values, "boundsExtent", Vector3{5.0F, 5.0F, 5.0F}));
            emitter.SetEditModePreview(ReadVfxProperty(values, "editModePreview", false));
            emitter.m_ParameterOverrides =
                DecodeOverrides(ReadVfxProperty(values, "parameterOverrides", std::string{"[]"}));
        };
        result.Migrate = [](ComponentPropertyBag values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported VFX Emitter component schema migration.");
            values.insert_or_assign("parameterOverrides", std::string{"[]"});
            return values;
        };
        return result;
    }
} // namespace Keire
