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
                                        const T& fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("VFX Emitter property has an incompatible type.");
        }

        [[nodiscard]] bool HasCanonicalRangeEndpoints(const VfxParameterValue& value) noexcept
        {
            return std::visit(
                [](const auto& item) noexcept
                {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::same_as<T, VfxScalarRange> || std::same_as<T, VfxIntegerRange> ||
                                  std::same_as<T, VfxUnsignedIntegerRange>)
                    {
                        return item.Minimum <= item.Maximum;
                    }
                    else if constexpr (std::same_as<T, VfxVector2Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y;
                    }
                    else if constexpr (std::same_as<T, VfxVector3Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                               item.Minimum.Z <= item.Maximum.Z;
                    }
                    else if constexpr (std::same_as<T, VfxVector4Range>)
                    {
                        return item.Minimum.X <= item.Maximum.X && item.Minimum.Y <= item.Maximum.Y &&
                               item.Minimum.Z <= item.Maximum.Z && item.Minimum.W <= item.Maximum.W;
                    }
                    else if constexpr (std::same_as<T, VfxColorRange>)
                    {
                        return item.Minimum.Red <= item.Maximum.Red && item.Minimum.Green <= item.Maximum.Green &&
                               item.Minimum.Blue <= item.Maximum.Blue && item.Minimum.Alpha <= item.Maximum.Alpha;
                    }
                    else
                    {
                        return true;
                    }
                },
                value);
        }

        [[nodiscard]] bool ValidOverride(const VfxParameterValue& value) noexcept
        {
            return IsFiniteVfxValue(value) && HasCanonicalRangeEndpoints(value);
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
                    else if constexpr (std::same_as<T, std::uint64_t>)
                        return Json{{"kind", "unsignedInteger"}, {"value", item}};
                    else if constexpr (std::same_as<T, float>)
                        return Json{{"kind", "scalar"}, {"value", item}};
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json{{"kind", "vector2"}, {"value", Json::array({item.X, item.Y})}};
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json{{"kind", "vector3"}, {"value", Json::array({item.X, item.Y, item.Z})}};
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json{{"kind", "vector4"}, {"value", Json::array({item.X, item.Y, item.Z, item.W})}};
                    else if constexpr (std::same_as<T, Quaternion>)
                        return Json{{"kind", "quaternion"}, {"value", Json::array({item.X, item.Y, item.Z, item.W})}};
                    else if constexpr (std::same_as<T, Color>)
                    {
                        return Json{{"kind", "color"},
                                    {"value", Json::array({item.Red, item.Green, item.Blue, item.Alpha})}};
                    }
                    else if constexpr (std::same_as<T, Matrix4>)
                        return Json{{"kind", "matrix"}, {"value", item.Elements}};
                    else if constexpr (std::same_as<T, Curve1D>)
                    {
                        auto keys = Json::array();
                        for (const auto& key : item.Keys())
                        {
                            keys.push_back({{"time", key.Time},
                                            {"value", key.Value},
                                            {"inTangent", key.InTangent},
                                            {"outTangent", key.OutTangent},
                                            {"interpolation", static_cast<std::uint32_t>(key.Interpolation)}});
                        }
                        return Json{{"kind", "curve"}, {"value", std::move(keys)}};
                    }
                    else if constexpr (std::same_as<T, ColorGradient>)
                    {
                        auto keys = Json::array();
                        for (const auto& key : item.Keys())
                        {
                            keys.push_back({{"time", key.Time},
                                            {"value", Json::array({key.Value.Red, key.Value.Green, key.Value.Blue,
                                                                   key.Value.Alpha})}});
                        }
                        return Json{{"kind", "gradient"},
                                    {"interpolation", static_cast<std::uint32_t>(item.Interpolation())},
                                    {"value", std::move(keys)}};
                    }
                    else if constexpr (std::same_as<T, VfxScalarRange>)
                        return Json{{"kind", "scalarRange"}, {"value", Json::array({item.Minimum, item.Maximum})}};
                    else if constexpr (std::same_as<T, VfxIntegerRange>)
                        return Json{{"kind", "integerRange"}, {"value", Json::array({item.Minimum, item.Maximum})}};
                    else if constexpr (std::same_as<T, VfxUnsignedIntegerRange>)
                    {
                        return Json{{"kind", "unsignedIntegerRange"},
                                    {"value", Json::array({item.Minimum, item.Maximum})}};
                    }
                    else if constexpr (std::same_as<T, VfxVector2Range>)
                    {
                        return Json{{"kind", "vector2Range"},
                                    {"value", Json::array({Json::array({item.Minimum.X, item.Minimum.Y}),
                                                           Json::array({item.Maximum.X, item.Maximum.Y})})}};
                    }
                    else if constexpr (std::same_as<T, VfxVector3Range>)
                    {
                        return Json{
                            {"kind", "vector3Range"},
                            {"value", Json::array({Json::array({item.Minimum.X, item.Minimum.Y, item.Minimum.Z}),
                                                   Json::array({item.Maximum.X, item.Maximum.Y, item.Maximum.Z})})}};
                    }
                    else if constexpr (std::same_as<T, VfxVector4Range>)
                    {
                        return Json{
                            {"kind", "vector4Range"},
                            {"value",
                             Json::array(
                                 {Json::array({item.Minimum.X, item.Minimum.Y, item.Minimum.Z, item.Minimum.W}),
                                  Json::array({item.Maximum.X, item.Maximum.Y, item.Maximum.Z, item.Maximum.W})})}};
                    }
                    else if constexpr (std::same_as<T, VfxColorRange>)
                    {
                        return Json{{"kind", "colorRange"},
                                    {"value", Json::array({Json::array({item.Minimum.Red, item.Minimum.Green,
                                                                        item.Minimum.Blue, item.Minimum.Alpha}),
                                                           Json::array({item.Maximum.Red, item.Maximum.Green,
                                                                        item.Maximum.Blue, item.Maximum.Alpha})})}};
                    }
                    else if constexpr (std::same_as<T, AssetId>)
                        return Json{{"kind", "asset"}, {"value", item ? item.ToString() : std::string{}}};
                    else
                        static_assert(!sizeof(T), "Unhandled VFX parameter value type.");
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
            if (kind == "unsignedInteger")
                return value.get<std::uint64_t>();
            if (kind == "scalar")
                return value.get<float>();
            if (kind == "vector2" && value.is_array() && value.size() == 2)
                return Vector2{value.at(0).get<float>(), value.at(1).get<float>()};
            if (kind == "vector3" && value.is_array() && value.size() == 3)
                return Vector3{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            if (kind == "vector4" && value.is_array() && value.size() == 4)
            {
                return Vector4{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                               value.at(3).get<float>()};
            }
            if (kind == "quaternion" && value.is_array() && value.size() == 4)
            {
                return Quaternion{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                                  value.at(3).get<float>()};
            }
            if (kind == "color" && value.is_array() && value.size() == 4)
            {
                return Color{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                             value.at(3).get<float>()};
            }
            if (kind == "matrix" && value.is_array() && value.size() == 16)
            {
                Matrix4 result;
                for (std::size_t index = 0; index < result.Elements.size(); ++index)
                    result.Elements[index] = value.at(index).get<float>();
                return result;
            }
            if (kind == "curve" && value.is_array())
            {
                std::vector<CurveKey> keys;
                keys.reserve(value.size());
                for (const auto& key : value)
                {
                    keys.push_back({key.at("time").get<float>(), key.at("value").get<float>(),
                                    key.at("inTangent").get<float>(), key.at("outTangent").get<float>(),
                                    static_cast<CurveInterpolation>(key.at("interpolation").get<std::uint32_t>())});
                }
                return Curve1D(std::move(keys));
            }
            if (kind == "gradient" && value.is_array())
            {
                std::vector<ColorGradientKey> keys;
                keys.reserve(value.size());
                for (const auto& key : value)
                {
                    const auto& color = key.at("value");
                    if (!color.is_array() || color.size() != 4)
                        throw std::invalid_argument("VFX Emitter gradient color is malformed.");
                    keys.push_back({key.at("time").get<float>(),
                                    {color.at(0).get<float>(), color.at(1).get<float>(), color.at(2).get<float>(),
                                     color.at(3).get<float>()}});
                }
                return ColorGradient(std::move(keys), static_cast<GradientInterpolation>(
                                                          encoded.at("interpolation").get<std::uint32_t>()));
            }
            if (kind == "scalarRange" && value.is_array() && value.size() == 2)
                return VfxScalarRange{value.at(0).get<float>(), value.at(1).get<float>()};
            if (kind == "integerRange" && value.is_array() && value.size() == 2)
                return VfxIntegerRange{value.at(0).get<std::int64_t>(), value.at(1).get<std::int64_t>()};
            if (kind == "unsignedIntegerRange" && value.is_array() && value.size() == 2)
                return VfxUnsignedIntegerRange{value.at(0).get<std::uint64_t>(), value.at(1).get<std::uint64_t>()};
            if (kind == "vector2Range" && value.is_array() && value.size() == 2 && value.at(0).is_array() &&
                value.at(0).size() == 2 && value.at(1).is_array() && value.at(1).size() == 2)
            {
                return VfxVector2Range{{value.at(0).at(0).get<float>(), value.at(0).at(1).get<float>()},
                                       {value.at(1).at(0).get<float>(), value.at(1).at(1).get<float>()}};
            }
            if (kind == "vector3Range" && value.is_array() && value.size() == 2 && value.at(0).is_array() &&
                value.at(0).size() == 3 && value.at(1).is_array() && value.at(1).size() == 3)
            {
                return VfxVector3Range{
                    {value.at(0).at(0).get<float>(), value.at(0).at(1).get<float>(), value.at(0).at(2).get<float>()},
                    {value.at(1).at(0).get<float>(), value.at(1).at(1).get<float>(), value.at(1).at(2).get<float>()}};
            }
            if (kind == "vector4Range" && value.is_array() && value.size() == 2 && value.at(0).is_array() &&
                value.at(0).size() == 4 && value.at(1).is_array() && value.at(1).size() == 4)
            {
                return VfxVector4Range{{value.at(0).at(0).get<float>(), value.at(0).at(1).get<float>(),
                                        value.at(0).at(2).get<float>(), value.at(0).at(3).get<float>()},
                                       {value.at(1).at(0).get<float>(), value.at(1).at(1).get<float>(),
                                        value.at(1).at(2).get<float>(), value.at(1).at(3).get<float>()}};
            }
            if (kind == "colorRange" && value.is_array() && value.size() == 2 && value.at(0).is_array() &&
                value.at(0).size() == 4 && value.at(1).is_array() && value.at(1).size() == 4)
            {
                return VfxColorRange{{value.at(0).at(0).get<float>(), value.at(0).at(1).get<float>(),
                                      value.at(0).at(2).get<float>(), value.at(0).at(3).get<float>()},
                                     {value.at(1).at(0).get<float>(), value.at(1).at(1).get<float>(),
                                      value.at(1).at(2).get<float>(), value.at(1).at(3).get<float>()}};
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
                    if (!parameter || !unique.insert(parameter).second || !ValidOverride(value))
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
        if (!value.Parameter || !ValidOverride(value.Value))
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

    void VfxEmitterComponent::CommitRuntimeParameterOverrides(std::vector<VfxParameterOverride> values) noexcept
    {
        m_ParameterOverrides.swap(values);
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
