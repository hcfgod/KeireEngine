#include "Keire/Vfx/VfxSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumDocumentBytes = 4U * 1024U * 1024U;
        constexpr std::size_t MaximumModules = 128;
        constexpr std::size_t MaximumSystems = 64;
        constexpr std::size_t MaximumGraphNodes = 4096;
        constexpr std::size_t MaximumGraphConnections = 16'384;
        constexpr std::size_t MaximumBlackboardParameters = 1024;
        constexpr std::size_t MaximumBursts = 32;
        constexpr std::size_t MaximumBurstCycles = 1024;
        constexpr std::size_t MaximumNameBytes = 128;
        constexpr float MaximumAuthoredScalar = 1'000'000.0F;

        template <typename... Ts> struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

        [[nodiscard]] std::string IdText(const AssetId id) { return id ? id.ToString() : std::string{}; }

        [[nodiscard]] AssetId ParseId(const Json& object, const char* key)
        {
            const auto text = object.value(key, std::string{});
            return text.empty() ? AssetId{} : AssetId::Parse(text);
        }

        [[nodiscard]] AssetId DerivedGraphId(const AssetId source, const std::uint64_t salt) noexcept
        {
            auto high = source.High() ^ 0x4752415048564658ULL;
            auto low = source.Low() ^ salt;
            high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
            low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
            return AssetId(high, low);
        }

        [[nodiscard]] Json EncodeVector(const Vector3 value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Vector3 DecodeVector(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("VFX vector values must contain exactly three scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
        }

        [[nodiscard]] Json EncodeColor(const Color value)
        {
            return Json::array({value.Red, value.Green, value.Blue, value.Alpha});
        }

        [[nodiscard]] Color DecodeColor(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("VFX color values must contain exactly four scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                    value.at(3).get<float>()};
        }

        [[nodiscard]] std::string_view CurveInterpolationName(const CurveInterpolation interpolation)
        {
            switch (interpolation)
            {
            case CurveInterpolation::Constant:
                return "constant";
            case CurveInterpolation::Linear:
                return "linear";
            case CurveInterpolation::Cubic:
                return "cubic";
            }
            throw std::invalid_argument("VFX curve interpolation is unsupported.");
        }

        [[nodiscard]] CurveInterpolation ParseCurveInterpolation(const std::string_view value)
        {
            if (value == "constant")
                return CurveInterpolation::Constant;
            if (value == "linear")
                return CurveInterpolation::Linear;
            if (value == "cubic")
                return CurveInterpolation::Cubic;
            throw std::runtime_error("VFX curve interpolation is unsupported.");
        }

        [[nodiscard]] Json EncodeCurve(const Curve1D& curve)
        {
            auto result = Json::array();
            for (const auto& key : curve.Keys())
            {
                result.push_back({{"time", key.Time},
                                  {"value", key.Value},
                                  {"inTangent", key.InTangent},
                                  {"outTangent", key.OutTangent},
                                  {"interpolation", CurveInterpolationName(key.Interpolation)}});
            }
            return result;
        }

        [[nodiscard]] Curve1D DecodeCurve(const Json& value)
        {
            if (!value.is_array() || value.size() > Curve1D::MaximumKeys)
                throw std::runtime_error("VFX curve is not an array or exceeds its key limit.");
            std::vector<CurveKey> keys;
            keys.reserve(value.size());
            for (const auto& key : value)
            {
                keys.push_back({key.at("time").get<float>(), key.at("value").get<float>(), key.value("inTangent", 0.0F),
                                key.value("outTangent", 0.0F),
                                ParseCurveInterpolation(key.value("interpolation", std::string("linear")))});
            }
            return Curve1D(std::move(keys));
        }

        [[nodiscard]] std::string_view GradientInterpolationName(const GradientInterpolation interpolation)
        {
            switch (interpolation)
            {
            case GradientInterpolation::Constant:
                return "constant";
            case GradientInterpolation::Linear:
                return "linear";
            }
            throw std::invalid_argument("VFX gradient interpolation is unsupported.");
        }

        [[nodiscard]] GradientInterpolation ParseGradientInterpolation(const std::string_view value)
        {
            if (value == "constant")
                return GradientInterpolation::Constant;
            if (value == "linear")
                return GradientInterpolation::Linear;
            throw std::runtime_error("VFX gradient interpolation is unsupported.");
        }

        [[nodiscard]] Json EncodeGradient(const ColorGradient& gradient)
        {
            auto keys = Json::array();
            for (const auto& key : gradient.Keys())
                keys.push_back({{"time", key.Time}, {"color", EncodeColor(key.Value)}});
            return {{"interpolation", GradientInterpolationName(gradient.Interpolation())}, {"keys", std::move(keys)}};
        }

        [[nodiscard]] ColorGradient DecodeGradient(const Json& value)
        {
            if (!value.is_object() || !value.contains("keys") || !value.at("keys").is_array() ||
                value.at("keys").size() > ColorGradient::MaximumKeys)
            {
                throw std::runtime_error("VFX gradient is malformed or exceeds its key limit.");
            }
            std::vector<ColorGradientKey> keys;
            keys.reserve(value.at("keys").size());
            for (const auto& key : value.at("keys"))
                keys.push_back({key.at("time").get<float>(), DecodeColor(key.at("color"))});
            return ColorGradient(std::move(keys),
                                 ParseGradientInterpolation(value.value("interpolation", std::string("linear"))));
        }

        [[nodiscard]] std::string_view SpaceName(const VfxSimulationSpace value)
        {
            switch (value)
            {
            case VfxSimulationSpace::Local:
                return "local";
            case VfxSimulationSpace::World:
                return "world";
            }
            throw std::invalid_argument("VFX simulation space is unsupported.");
        }

        [[nodiscard]] VfxSimulationSpace ParseSpace(const std::string_view value)
        {
            if (value == "local")
                return VfxSimulationSpace::Local;
            if (value == "world")
                return VfxSimulationSpace::World;
            throw std::runtime_error("VFX simulation space is unsupported.");
        }

        [[nodiscard]] std::string_view ShapeName(const VfxShape value)
        {
            switch (value)
            {
            case VfxShape::Point:
                return "point";
            case VfxShape::Box:
                return "box";
            case VfxShape::Sphere:
                return "sphere";
            case VfxShape::Cone:
                return "cone";
            case VfxShape::Mesh:
                return "mesh";
            case VfxShape::Volume:
                return "volume";
            }
            throw std::invalid_argument("VFX shape is unsupported.");
        }

        [[nodiscard]] VfxShape ParseShape(const std::string_view value)
        {
            if (value == "point")
                return VfxShape::Point;
            if (value == "box")
                return VfxShape::Box;
            if (value == "sphere")
                return VfxShape::Sphere;
            if (value == "cone")
                return VfxShape::Cone;
            if (value == "mesh")
                return VfxShape::Mesh;
            if (value == "volume")
                return VfxShape::Volume;
            throw std::runtime_error("VFX shape is unsupported.");
        }

        [[nodiscard]] std::string_view CollisionModeName(const VfxCollisionMode value)
        {
            switch (value)
            {
            case VfxCollisionMode::None:
                return "none";
            case VfxCollisionMode::Cpu:
                return "cpu";
            case VfxCollisionMode::GpuDepth:
                return "gpuDepth";
            case VfxCollisionMode::ScenePhysics:
                return "scenePhysics";
            }
            throw std::invalid_argument("VFX collision mode is unsupported.");
        }

        [[nodiscard]] VfxCollisionMode ParseCollisionMode(const std::string_view value)
        {
            if (value == "none")
                return VfxCollisionMode::None;
            if (value == "cpu")
                return VfxCollisionMode::Cpu;
            if (value == "gpuDepth")
                return VfxCollisionMode::GpuDepth;
            if (value == "scenePhysics")
                return VfxCollisionMode::ScenePhysics;
            throw std::runtime_error("VFX collision mode is unsupported.");
        }

        [[nodiscard]] std::string_view RendererTypeName(const VfxRendererType value)
        {
            switch (value)
            {
            case VfxRendererType::Sprite:
                return "sprite";
            case VfxRendererType::Mesh:
                return "mesh";
            }
            throw std::invalid_argument("VFX renderer type is unsupported.");
        }

        [[nodiscard]] VfxRendererType ParseRendererType(const std::string_view value)
        {
            if (value == "sprite")
                return VfxRendererType::Sprite;
            if (value == "mesh")
                return VfxRendererType::Mesh;
            throw std::runtime_error("VFX renderer type is unsupported.");
        }

        [[nodiscard]] std::string_view ModuleTypeName(const VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule&) -> std::string_view { return "emissionRate"; },
                    [](const VfxBurstModule&) -> std::string_view { return "burst"; },
                    [](const VfxShapeModule&) -> std::string_view { return "shape"; },
                    [](const VfxInitializeModule&) -> std::string_view { return "initialize"; },
                    [](const VfxForceModule&) -> std::string_view { return "force"; },
                    [](const VfxSizeOverLifetimeModule&) -> std::string_view { return "sizeOverLifetime"; },
                    [](const VfxColorOverLifetimeModule&) -> std::string_view { return "colorOverLifetime"; },
                    [](const VfxCollisionModule&) -> std::string_view { return "collision"; },
                    [](const VfxRendererModule&) -> std::string_view { return "renderer"; },
                },
                payload);
        }

        [[nodiscard]] Json EncodeModule(const VfxModuleDefinition& module)
        {
            Json result{
                {"id", IdText(module.Id)}, {"enabled", module.Enabled}, {"type", ModuleTypeName(module.Payload)}};
            std::visit(
                Overloaded{
                    [&result](const VfxEmissionRateModule& value)
                    { result["particlesPerSecond"] = value.ParticlesPerSecond; },
                    [&result](const VfxBurstModule& value)
                    {
                        result["time"] = value.Time;
                        result["count"] = value.Count;
                        result["cycles"] = value.Cycles;
                        result["interval"] = value.Interval;
                    },
                    [&result](const VfxShapeModule& value)
                    {
                        result["shape"] = ShapeName(value.Shape);
                        result["boxHalfExtent"] = EncodeVector(value.BoxHalfExtent);
                        result["radius"] = value.Radius;
                        result["coneAngleDegrees"] = value.ConeAngleDegrees;
                        result["coneLength"] = value.ConeLength;
                        result["mesh"] = IdText(value.Mesh);
                        result["volume"] = IdText(value.Volume);
                    },
                    [&result](const VfxInitializeModule& value)
                    {
                        result["lifetimeMinimum"] = value.LifetimeMinimum;
                        result["lifetimeMaximum"] = value.LifetimeMaximum;
                        result["velocityMinimum"] = EncodeVector(value.VelocityMinimum);
                        result["velocityMaximum"] = EncodeVector(value.VelocityMaximum);
                        result["rotationMinimum"] = EncodeVector(value.RotationMinimum);
                        result["rotationMaximum"] = EncodeVector(value.RotationMaximum);
                    },
                    [&result](const VfxForceModule& value)
                    {
                        result["force"] = EncodeVector(value.Force);
                        result["gravityMultiplier"] = value.GravityMultiplier;
                    },
                    [&result](const VfxSizeOverLifetimeModule& value) { result["curve"] = EncodeCurve(value.Size); },
                    [&result](const VfxColorOverLifetimeModule& value)
                    { result["gradient"] = EncodeGradient(value.Color); },
                    [&result](const VfxCollisionModule& value)
                    {
                        result["mode"] = CollisionModeName(value.Mode);
                        result["restitution"] = value.Restitution;
                        result["killOnCollision"] = value.KillOnCollision;
                    },
                    [&result](const VfxRendererModule& value)
                    {
                        result["renderer"] = RendererTypeName(value.Type);
                        result["sprite"] = IdText(value.Sprite);
                        result["mesh"] = IdText(value.Mesh);
                    },
                },
                module.Payload);
            return result;
        }

        [[nodiscard]] VfxModuleDefinition DecodeModule(const Json& value)
        {
            VfxModuleDefinition result;
            result.Id = ParseId(value, "id");
            result.Enabled = value.value("enabled", true);
            const auto type = value.at("type").get<std::string>();
            if (type == "emissionRate")
                result.Payload = VfxEmissionRateModule{value.at("particlesPerSecond").get<float>()};
            else if (type == "burst")
                result.Payload = VfxBurstModule{value.at("time").get<float>(), value.at("count").get<std::uint32_t>(),
                                                value.value("cycles", 1U), value.value("interval", 0.1F)};
            else if (type == "shape")
                result.Payload =
                    VfxShapeModule{ParseShape(value.value("shape", std::string("point"))),
                                   DecodeVector(value.value("boxHalfExtent", Json::array({0.5F, 0.5F, 0.5F}))),
                                   value.value("radius", 0.5F),
                                   value.value("coneAngleDegrees", 25.0F),
                                   value.value("coneLength", 1.0F),
                                   ParseId(value, "mesh"),
                                   ParseId(value, "volume")};
            else if (type == "initialize")
                result.Payload =
                    VfxInitializeModule{value.value("lifetimeMinimum", 1.0F),
                                        value.value("lifetimeMaximum", 1.0F),
                                        DecodeVector(value.value("velocityMinimum", Json::array({0.0F, 0.0F, 0.0F}))),
                                        DecodeVector(value.value("velocityMaximum", Json::array({0.0F, 0.0F, 0.0F}))),
                                        DecodeVector(value.value("rotationMinimum", Json::array({0.0F, 0.0F, 0.0F}))),
                                        DecodeVector(value.value("rotationMaximum", Json::array({0.0F, 0.0F, 0.0F})))};
            else if (type == "force")
                result.Payload = VfxForceModule{DecodeVector(value.value("force", Json::array({0.0F, 0.0F, 0.0F}))),
                                                value.value("gravityMultiplier", 0.0F)};
            else if (type == "sizeOverLifetime")
                result.Payload = VfxSizeOverLifetimeModule{DecodeCurve(value.at("curve"))};
            else if (type == "colorOverLifetime")
                result.Payload = VfxColorOverLifetimeModule{DecodeGradient(value.at("gradient"))};
            else if (type == "collision")
                result.Payload =
                    VfxCollisionModule{ParseCollisionMode(value.value("mode", std::string("none"))),
                                       value.value("restitution", 0.5F), value.value("killOnCollision", false)};
            else if (type == "renderer")
                result.Payload = VfxRendererModule{ParseRendererType(value.value("renderer", std::string("sprite"))),
                                                   ParseId(value, "sprite"), ParseId(value, "mesh")};
            else
                throw std::runtime_error("VFX module type is unsupported.");
            return result;
        }

        [[nodiscard]] std::string_view ContextName(const VfxContextType value)
        {
            switch (value)
            {
            case VfxContextType::Spawn:
                return "spawn";
            case VfxContextType::Initialize:
                return "initialize";
            case VfxContextType::Update:
                return "update";
            case VfxContextType::Output:
                return "output";
            case VfxContextType::Event:
                return "event";
            }
            throw std::invalid_argument("VFX context type is unsupported.");
        }

        [[nodiscard]] VfxContextType ParseContext(const std::string_view value)
        {
            if (value == "spawn")
                return VfxContextType::Spawn;
            if (value == "initialize")
                return VfxContextType::Initialize;
            if (value == "update")
                return VfxContextType::Update;
            if (value == "output")
                return VfxContextType::Output;
            if (value == "event")
                return VfxContextType::Event;
            throw std::runtime_error("VFX context type is unsupported.");
        }

        [[nodiscard]] std::string_view ValueTypeName(const VfxValueType value)
        {
            switch (value)
            {
            case VfxValueType::Boolean:
                return "boolean";
            case VfxValueType::Integer:
                return "integer";
            case VfxValueType::Scalar:
                return "scalar";
            case VfxValueType::Vector2:
                return "vector2";
            case VfxValueType::Vector3:
                return "vector3";
            case VfxValueType::Color:
                return "color";
            case VfxValueType::Texture:
                return "texture";
            case VfxValueType::Mesh:
                return "mesh";
            case VfxValueType::Asset:
                return "asset";
            }
            throw std::invalid_argument("VFX value type is unsupported.");
        }

        [[nodiscard]] VfxValueType ParseValueType(const std::string_view value)
        {
            if (value == "boolean")
                return VfxValueType::Boolean;
            if (value == "integer")
                return VfxValueType::Integer;
            if (value == "scalar")
                return VfxValueType::Scalar;
            if (value == "vector2")
                return VfxValueType::Vector2;
            if (value == "vector3")
                return VfxValueType::Vector3;
            if (value == "color")
                return VfxValueType::Color;
            if (value == "texture")
                return VfxValueType::Texture;
            if (value == "mesh")
                return VfxValueType::Mesh;
            if (value == "asset")
                return VfxValueType::Asset;
            throw std::runtime_error("VFX value type is unsupported.");
        }

        [[nodiscard]] Json EncodeParameterValue(const VfxBlackboardParameter& parameter)
        {
            switch (parameter.Type)
            {
            case VfxValueType::Boolean:
                return std::get<bool>(parameter.DefaultValue);
            case VfxValueType::Integer:
                return std::get<std::int64_t>(parameter.DefaultValue);
            case VfxValueType::Scalar:
                return std::get<float>(parameter.DefaultValue);
            case VfxValueType::Vector2:
            {
                const auto value = std::get<Vector2>(parameter.DefaultValue);
                return Json::array({value.X, value.Y});
            }
            case VfxValueType::Vector3:
                return EncodeVector(std::get<Vector3>(parameter.DefaultValue));
            case VfxValueType::Color:
                return EncodeColor(std::get<Color>(parameter.DefaultValue));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
                return IdText(std::get<AssetId>(parameter.DefaultValue));
            }
            throw std::invalid_argument("VFX blackboard value type is unsupported.");
        }

        [[nodiscard]] VfxParameterValue DecodeParameterValue(const VfxValueType type, const Json& value)
        {
            switch (type)
            {
            case VfxValueType::Boolean:
                return value.get<bool>();
            case VfxValueType::Integer:
                return value.get<std::int64_t>();
            case VfxValueType::Scalar:
                return value.get<float>();
            case VfxValueType::Vector2:
                if (value.is_array() && value.size() == 2)
                    return Vector2{value.at(0).get<float>(), value.at(1).get<float>()};
                break;
            case VfxValueType::Vector3:
                return DecodeVector(value);
            case VfxValueType::Color:
                return DecodeColor(value);
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            {
                const auto text = value.get<std::string>();
                return text.empty() ? AssetId{} : AssetId::Parse(text);
            }
            }
            throw std::runtime_error("VFX blackboard default value is malformed.");
        }

        [[nodiscard]] Json EncodeSystems(const std::span<const VfxGraphSystem> systems)
        {
            auto encodedSystems = Json::array();
            for (const auto& system : systems)
            {
                auto nodes = Json::array();
                for (const auto& node : system.Nodes)
                {
                    auto pins = Json::array();
                    for (const auto& pin : node.Pins)
                        pins.push_back({{"id", IdText(pin.Id)},
                                        {"name", pin.Name},
                                        {"type", ValueTypeName(pin.Type)},
                                        {"input", pin.Input}});
                    nodes.push_back({{"id", IdText(node.Id)},
                                     {"type", node.Type},
                                     {"context", ContextName(node.Context)},
                                     {"position", Json::array({node.EditorPosition.X, node.EditorPosition.Y})},
                                     {"pins", std::move(pins)},
                                     {"customHlsl", node.CustomHlsl}});
                }
                auto connections = Json::array();
                for (const auto& connection : system.Connections)
                    connections.push_back({{"id", IdText(connection.Id)},
                                           {"outputNode", IdText(connection.OutputNode)},
                                           {"outputPin", IdText(connection.OutputPin)},
                                           {"inputNode", IdText(connection.InputNode)},
                                           {"inputPin", IdText(connection.InputPin)}});
                encodedSystems.push_back({{"id", IdText(system.Id)},
                                          {"name", system.Name},
                                          {"nodes", std::move(nodes)},
                                          {"connections", std::move(connections)}});
            }
            return encodedSystems;
        }

        [[nodiscard]] std::vector<VfxGraphSystem> DecodeSystems(const Json& value)
        {
            if (!value.is_array())
                throw std::runtime_error("VFX graph systems must be an array.");
            std::vector<VfxGraphSystem> systems;
            for (const auto& encodedSystem : value)
            {
                VfxGraphSystem system;
                system.Id = ParseId(encodedSystem, "id");
                system.Name = encodedSystem.at("name").get<std::string>();
                for (const auto& encodedNode : encodedSystem.at("nodes"))
                {
                    VfxGraphNode node;
                    node.Id = ParseId(encodedNode, "id");
                    node.Type = encodedNode.at("type").get<std::string>();
                    node.Context = ParseContext(encodedNode.at("context").get<std::string>());
                    const auto& position = encodedNode.at("position");
                    node.EditorPosition = {position.at(0).get<float>(), position.at(1).get<float>()};
                    node.CustomHlsl = encodedNode.value("customHlsl", std::string{});
                    for (const auto& encodedPin : encodedNode.at("pins"))
                        node.Pins.push_back({ParseId(encodedPin, "id"), encodedPin.at("name").get<std::string>(),
                                             ParseValueType(encodedPin.at("type").get<std::string>()),
                                             encodedPin.value("input", true)});
                    system.Nodes.push_back(std::move(node));
                }
                for (const auto& encodedConnection : encodedSystem.at("connections"))
                    system.Connections.push_back(
                        {ParseId(encodedConnection, "id"), ParseId(encodedConnection, "outputNode"),
                         ParseId(encodedConnection, "outputPin"), ParseId(encodedConnection, "inputNode"),
                         ParseId(encodedConnection, "inputPin")});
                systems.push_back(std::move(system));
            }
            return systems;
        }

        [[nodiscard]] std::vector<VfxBlackboardParameter> DecodeBlackboard(const Json& value)
        {
            if (!value.is_array())
                throw std::runtime_error("VFX blackboard must be an array.");
            std::vector<VfxBlackboardParameter> result;
            for (const auto& encoded : value)
            {
                const auto type = ParseValueType(encoded.at("type").get<std::string>());
                result.push_back({ParseId(encoded, "id"), encoded.at("name").get<std::string>(), type,
                                  DecodeParameterValue(type, encoded.at("default")), encoded.value("exposed", true)});
            }
            return result;
        }

        [[nodiscard]] Json EncodeBlackboard(const std::span<const VfxBlackboardParameter> parameters)
        {
            auto result = Json::array();
            for (const auto& parameter : parameters)
                result.push_back({{"id", IdText(parameter.Id)},
                                  {"name", parameter.Name},
                                  {"type", ValueTypeName(parameter.Type)},
                                  {"default", EncodeParameterValue(parameter)},
                                  {"exposed", parameter.Exposed}});
            return result;
        }

        template <typename T> [[nodiscard]] bool FiniteRange(const T minimum, const T maximum) noexcept
        {
            return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
        }

        [[nodiscard]] bool OrderedRange(const Vector3 minimum, const Vector3 maximum) noexcept
        {
            return Math::IsFinite(minimum) && Math::IsFinite(maximum) && minimum.X <= maximum.X &&
                   minimum.Y <= maximum.Y && minimum.Z <= maximum.Z;
        }

        [[nodiscard]] bool BoundedVector(const Vector3 value) noexcept
        {
            return Math::IsFinite(value) && std::abs(value.X) <= MaximumAuthoredScalar &&
                   std::abs(value.Y) <= MaximumAuthoredScalar && std::abs(value.Z) <= MaximumAuthoredScalar;
        }

        [[nodiscard]] bool ValidSizeCurve(const Curve1D& curve) noexcept
        {
            if (curve.Keys().empty())
                return false;
            return std::ranges::all_of(curve.Keys(),
                                       [](const CurveKey& key)
                                       {
                                           return key.Time >= 0.0F && key.Time <= 1.0F && key.Value >= 0.0F &&
                                                  key.Value <= MaximumAuthoredScalar &&
                                                  std::abs(key.InTangent) <= MaximumAuthoredScalar &&
                                                  std::abs(key.OutTangent) <= MaximumAuthoredScalar;
                                       });
        }

        [[nodiscard]] bool ValidColorGradient(const ColorGradient& gradient) noexcept
        {
            if (gradient.Keys().empty())
                return false;
            return std::ranges::all_of(gradient.Keys(),
                                       [](const ColorGradientKey& key)
                                       {
                                           return key.Time >= 0.0F && key.Time <= 1.0F && key.Value.Red >= 0.0F &&
                                                  key.Value.Red <= MaximumAuthoredScalar && key.Value.Green >= 0.0F &&
                                                  key.Value.Green <= MaximumAuthoredScalar && key.Value.Blue >= 0.0F &&
                                                  key.Value.Blue <= MaximumAuthoredScalar && key.Value.Alpha >= 0.0F &&
                                                  key.Value.Alpha <= MaximumAuthoredScalar;
                                       });
        }
    } // namespace

    void ValidateVfxEffect(const VfxEffectDefinition& definition)
    {
        if ((definition.SchemaVersion != 1 && definition.SchemaVersion != 2) || !definition.EmitterId ||
            definition.Name.empty() || definition.Name.size() > MaximumNameBytes ||
            !std::isfinite(definition.Duration) || definition.Duration < 0.001F || definition.Duration > 3600.0F ||
            definition.Capacity == 0 || definition.Capacity > 1'000'000 || definition.Modules.empty() ||
            definition.Modules.size() > MaximumModules)
        {
            throw std::invalid_argument("VFX effect header is invalid.");
        }
        if (definition.Space > VfxSimulationSpace::World)
            throw std::invalid_argument("VFX effect simulation space is invalid.");

        std::set<AssetId> stableIds{definition.EmitterId};
        std::size_t bursts = 0;
        std::size_t emissions = 0;
        std::size_t shapes = 0;
        std::size_t initializers = 0;
        std::size_t forces = 0;
        std::size_t sizes = 0;
        std::size_t colors = 0;
        std::size_t collisions = 0;
        std::size_t renderers = 0;

        for (const auto& module : definition.Modules)
        {
            if (!module.Id || !stableIds.insert(module.Id).second)
                throw std::invalid_argument("VFX effect contains an empty or duplicate stable ID.");
            std::visit(
                Overloaded{
                    [&](const VfxEmissionRateModule& value)
                    {
                        ++emissions;
                        if (!std::isfinite(value.ParticlesPerSecond) || value.ParticlesPerSecond < 0.0F ||
                            value.ParticlesPerSecond > 1'000'000.0F)
                        {
                            throw std::invalid_argument("VFX emission rate is invalid.");
                        }
                    },
                    [&](const VfxBurstModule& value)
                    {
                        ++bursts;
                        if (!std::isfinite(value.Time) || value.Time < 0.0F || value.Time >= definition.Duration ||
                            value.Count == 0 || value.Count > 1'000'000 || value.Cycles == 0 ||
                            value.Cycles > MaximumBurstCycles || !std::isfinite(value.Interval) ||
                            value.Interval < 0.0F || (value.Cycles > 1 && value.Interval <= 0.0F) ||
                            value.Time + static_cast<float>(value.Cycles - 1) * value.Interval >= definition.Duration)
                        {
                            throw std::invalid_argument("VFX burst is invalid.");
                        }
                    },
                    [&](const VfxShapeModule& value)
                    {
                        ++shapes;
                        if (value.Shape > VfxShape::Volume || !Math::IsFinite(value.BoxHalfExtent) ||
                            value.BoxHalfExtent.X <= 0.0F || value.BoxHalfExtent.Y <= 0.0F ||
                            value.BoxHalfExtent.Z <= 0.0F || value.BoxHalfExtent.X > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Y > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Z > MaximumAuthoredScalar || !std::isfinite(value.Radius) ||
                            value.Radius <= 0.0F || value.Radius > MaximumAuthoredScalar ||
                            !std::isfinite(value.ConeAngleDegrees) || value.ConeAngleDegrees <= 0.0F ||
                            value.ConeAngleDegrees >= 90.0F || !std::isfinite(value.ConeLength) ||
                            value.ConeLength <= 0.0F || value.ConeLength > MaximumAuthoredScalar ||
                            (value.Shape == VfxShape::Mesh && !value.Mesh) ||
                            (value.Shape == VfxShape::Volume && !value.Volume))
                        {
                            throw std::invalid_argument("VFX shape module is invalid.");
                        }
                    },
                    [&](const VfxInitializeModule& value)
                    {
                        ++initializers;
                        if (!FiniteRange(value.LifetimeMinimum, value.LifetimeMaximum) ||
                            value.LifetimeMinimum <= 0.0F || value.LifetimeMaximum > 86'400.0F ||
                            !OrderedRange(value.VelocityMinimum, value.VelocityMaximum) ||
                            !OrderedRange(value.RotationMinimum, value.RotationMaximum) ||
                            !BoundedVector(value.VelocityMinimum) || !BoundedVector(value.VelocityMaximum) ||
                            !BoundedVector(value.RotationMinimum) || !BoundedVector(value.RotationMaximum))
                        {
                            throw std::invalid_argument("VFX initialize module is invalid.");
                        }
                    },
                    [&](const VfxForceModule& value)
                    {
                        ++forces;
                        if (!BoundedVector(value.Force) || !std::isfinite(value.GravityMultiplier) ||
                            std::abs(value.GravityMultiplier) > 1000.0F)
                        {
                            throw std::invalid_argument("VFX force module is invalid.");
                        }
                    },
                    [&](const VfxSizeOverLifetimeModule& value)
                    {
                        ++sizes;
                        if (!ValidSizeCurve(value.Size))
                            throw std::invalid_argument("VFX size curve is invalid.");
                    },
                    [&](const VfxColorOverLifetimeModule& value)
                    {
                        ++colors;
                        if (!ValidColorGradient(value.Color))
                            throw std::invalid_argument("VFX color gradient is invalid.");
                    },
                    [&](const VfxCollisionModule& value)
                    {
                        ++collisions;
                        if (value.Mode > VfxCollisionMode::ScenePhysics || !std::isfinite(value.Restitution) ||
                            value.Restitution < 0.0F || value.Restitution > 1.0F)
                        {
                            throw std::invalid_argument("VFX collision module is invalid.");
                        }
                    },
                    [&](const VfxRendererModule& value)
                    {
                        ++renderers;
                        if (value.Type > VfxRendererType::Mesh || (value.Type == VfxRendererType::Mesh && !value.Mesh))
                        {
                            throw std::invalid_argument("VFX renderer module is invalid.");
                        }
                    },
                },
                module.Payload);
        }

        if (bursts > MaximumBursts || emissions > 1 || shapes > 1 || initializers > 1 || forces > 1 || sizes > 1 ||
            colors > 1 || collisions > 1 || renderers != 1)
        {
            throw std::invalid_argument("VFX effect contains an invalid module multiplicity.");
        }

        if (definition.SchemaVersion == 2)
        {
            if (definition.Systems.size() > MaximumSystems ||
                definition.Blackboard.size() > MaximumBlackboardParameters)
                throw std::invalid_argument("VFX graph system or blackboard count is invalid.");
            std::size_t nodeCount = 0;
            std::size_t connectionCount = 0;
            std::set<std::string> parameterNames;
            for (const auto& parameter : definition.Blackboard)
            {
                const bool valueMatches =
                    (parameter.Type == VfxValueType::Boolean && std::holds_alternative<bool>(parameter.DefaultValue)) ||
                    (parameter.Type == VfxValueType::Integer &&
                     std::holds_alternative<std::int64_t>(parameter.DefaultValue)) ||
                    (parameter.Type == VfxValueType::Scalar && std::holds_alternative<float>(parameter.DefaultValue)) ||
                    (parameter.Type == VfxValueType::Vector2 &&
                     std::holds_alternative<Vector2>(parameter.DefaultValue)) ||
                    (parameter.Type == VfxValueType::Vector3 &&
                     std::holds_alternative<Vector3>(parameter.DefaultValue)) ||
                    (parameter.Type == VfxValueType::Color && std::holds_alternative<Color>(parameter.DefaultValue)) ||
                    (parameter.Type >= VfxValueType::Texture &&
                     std::holds_alternative<AssetId>(parameter.DefaultValue));
                if (!parameter.Id || !stableIds.insert(parameter.Id).second || parameter.Name.empty() ||
                    parameter.Name.size() > MaximumNameBytes || !parameterNames.insert(parameter.Name).second ||
                    !valueMatches)
                    throw std::invalid_argument("VFX blackboard contains an invalid parameter.");
            }
            for (const auto& system : definition.Systems)
            {
                if (!system.Id || !stableIds.insert(system.Id).second || system.Name.empty() ||
                    system.Name.size() > MaximumNameBytes)
                    throw std::invalid_argument("VFX graph contains an invalid system.");
                nodeCount += system.Nodes.size();
                connectionCount += system.Connections.size();
                std::set<AssetId> nodeIds;
                std::set<AssetId> pinIds;
                for (const auto& node : system.Nodes)
                {
                    if (!node.Id || !stableIds.insert(node.Id).second || !nodeIds.insert(node.Id).second ||
                        node.Type.empty() || node.Type.size() > MaximumNameBytes ||
                        !Math::IsFinite(node.EditorPosition) || node.CustomHlsl.size() > MaximumDocumentBytes)
                        throw std::invalid_argument("VFX graph contains an invalid node.");
                    for (const auto& pin : node.Pins)
                        if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                            pin.Name.empty() || pin.Name.size() > MaximumNameBytes || pin.Type > VfxValueType::Asset)
                            throw std::invalid_argument("VFX graph contains an invalid pin.");
                }
                const auto findPin = [&system](const AssetId nodeId, const AssetId pinId) -> const VfxGraphPin*
                {
                    const auto node = std::ranges::find(system.Nodes, nodeId, &VfxGraphNode::Id);
                    if (node == system.Nodes.end())
                        return nullptr;
                    const auto pin = std::ranges::find(node->Pins, pinId, &VfxGraphPin::Id);
                    return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
                };
                for (const auto& connection : system.Connections)
                {
                    const auto* output = findPin(connection.OutputNode, connection.OutputPin);
                    const auto* input = findPin(connection.InputNode, connection.InputPin);
                    if (!connection.Id || !stableIds.insert(connection.Id).second || !output || !input ||
                        output->Input || !input->Input || output->Type != input->Type)
                        throw std::invalid_argument("VFX graph contains an invalid connection.");
                }
            }
            if (nodeCount > MaximumGraphNodes || connectionCount > MaximumGraphConnections)
                throw std::invalid_argument("VFX graph exceeds its bounded complexity limits.");
        }

        const auto hasEnabledEmission = std::ranges::any_of(
            definition.Modules,
            [](const VfxModuleDefinition& module)
            {
                return module.Enabled && (std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                                          std::holds_alternative<VfxBurstModule>(module.Payload));
            });
        const auto hasEnabledRenderer = std::ranges::any_of(
            definition.Modules, [](const VfxModuleDefinition& module)
            { return module.Enabled && std::holds_alternative<VfxRendererModule>(module.Payload); });
        if (!hasEnabledEmission || !hasEnabledRenderer)
            throw std::invalid_argument("VFX effect requires enabled emission and renderer modules.");
    }

    std::vector<AssetId> VfxEffectDependencies(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffect(definition);
        std::vector<AssetId> result;
        for (const auto& module : definition.Modules)
        {
            std::visit(
                Overloaded{
                    [&result](const VfxShapeModule& value)
                    {
                        if (value.Shape == VfxShape::Mesh && value.Mesh)
                            result.push_back(value.Mesh);
                        if (value.Shape == VfxShape::Volume && value.Volume)
                            result.push_back(value.Volume);
                    },
                    [&result](const VfxRendererModule& value)
                    {
                        if (value.Type == VfxRendererType::Sprite && value.Sprite)
                            result.push_back(value.Sprite);
                        if (value.Type == VfxRendererType::Mesh && value.Mesh)
                            result.push_back(value.Mesh);
                    },
                    [](const auto&) {},
                },
                module.Payload);
        }
        for (const auto& parameter : definition.Blackboard)
            if (const auto* asset = std::get_if<AssetId>(&parameter.DefaultValue); asset && *asset)
                result.push_back(*asset);
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    VfxCompiledProgram CompileVfxEffect(const VfxEffectDefinition& definition, const VfxBackend backend)
    {
        VfxCompiledProgram result;
        result.Backend = backend;
        try
        {
            ValidateVfxEffect(definition);
            result.CanonicalIr = VfxEffectAsset::Encode(definition);
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto value : result.CanonicalIr)
            {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= 1099511628211ULL;
            }
            result.Hash = hash;
            if (backend == VfxBackend::Cpu)
            {
                for (const auto& system : definition.Systems)
                    for (const auto& node : system.Nodes)
                        if (!node.CustomHlsl.empty())
                            result.Diagnostics.push_back(
                                {VfxCompileDiagnosticSeverity::Warning, node.Id,
                                 "Custom HLSL is unavailable in the deterministic CPU compatibility backend."});
                for (const auto& module : definition.Modules)
                {
                    if (const auto* collision = std::get_if<VfxCollisionModule>(&module.Payload);
                        collision && collision->Mode == VfxCollisionMode::GpuDepth)
                        result.Diagnostics.push_back(
                            {VfxCompileDiagnosticSeverity::Warning, module.Id,
                             "GPU depth collision degrades to the configured CPU collision query."});
                }
            }
            result.Valid = true;
        }
        catch (const std::exception& error)
        {
            result.Diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, {}, error.what()});
        }
        return result;
    }

    VfxEffectAsset::VfxEffectAsset(VfxEffectDefinition definition) : m_Definition(std::move(definition))
    {
        ValidateVfxEffect(m_Definition);
    }

    std::size_t VfxEffectAsset::ResidentBytes() const noexcept
    {
        auto result = sizeof(*this) + m_Definition.Name.capacity() +
                      m_Definition.Modules.capacity() * sizeof(VfxModuleDefinition);
        for (const auto& module : m_Definition.Modules)
        {
            if (const auto* size = std::get_if<VfxSizeOverLifetimeModule>(&module.Payload))
                result += size->Size.Keys().size() * sizeof(CurveKey);
            if (const auto* color = std::get_if<VfxColorOverLifetimeModule>(&module.Payload))
                result += color->Color.Keys().size() * sizeof(ColorGradientKey);
        }
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
        definition.Systems = {
            {AssetId(0x5646584445464155ULL, 8),
             "Particle System",
             {{AssetId(0x5646584445464155ULL, 9), "Spawn Context", VfxContextType::Spawn, {0.0F, 0.0F}},
              {AssetId(0x5646584445464155ULL, 10), "Initialize Context", VfxContextType::Initialize, {280.0F, 0.0F}},
              {AssetId(0x5646584445464155ULL, 11), "Update Context", VfxContextType::Update, {560.0F, 0.0F}},
              {AssetId(0x5646584445464155ULL, 12), "Output Context", VfxContextType::Output, {840.0F, 0.0F}}},
             {}},
        };
        return definition;
    }

    Ref<VfxEffectAsset> VfxEffectAsset::Default() { return CreateRef<VfxEffectAsset>(DefaultDefinition()); }

    Ref<VfxEffectAsset> VfxEffectAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("VFX effect asset is empty or exceeds the 4 MiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            const auto schemaVersion = document.value("schemaVersion", 0U);
            if (!document.is_object() || (schemaVersion != 1U && schemaVersion != 2U))
                throw std::runtime_error("VFX effect asset has an unsupported schema.");

            VfxEffectDefinition definition;
            definition.SchemaVersion = schemaVersion;
            definition.EmitterId = ParseId(document, "emitterId");
            definition.Name = document.value("name", std::string("VFX Effect"));
            definition.Loop = document.value("loop", false);
            definition.Duration = document.value("duration", 1.0F);
            definition.Space = ParseSpace(document.value("space", std::string("world")));
            definition.Seed = document.value("seed", 1U);
            definition.Capacity = document.value("capacity", 1024U);
            const auto& modules = document.at("modules");
            if (!modules.is_array() || modules.empty() || modules.size() > MaximumModules)
                throw std::runtime_error("VFX effect module stack is malformed or exceeds its limit.");
            definition.Modules.reserve(modules.size());
            for (const auto& module : modules)
                definition.Modules.push_back(DecodeModule(module));
            if (schemaVersion == 2)
            {
                definition.Systems = DecodeSystems(document.at("systems"));
                definition.Blackboard = DecodeBlackboard(document.value("blackboard", Json::array()));
            }
            return CreateRef<VfxEffectAsset>(std::move(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("VFX effect asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> VfxEffectAsset::Encode(const VfxEffectDefinition& definition)
    {
        auto published = definition;
        published.SchemaVersion = 2;
        if (published.Systems.empty())
        {
            const auto systemId = DerivedGraphId(published.EmitterId, 1);
            published.Systems = {
                {systemId,
                 "Migrated Particle System",
                 {{DerivedGraphId(published.EmitterId, 2), "Spawn Context", VfxContextType::Spawn, {0.0F, 0.0F}},
                  {DerivedGraphId(published.EmitterId, 3),
                   "Initialize Context",
                   VfxContextType::Initialize,
                   {280.0F, 0.0F}},
                  {DerivedGraphId(published.EmitterId, 4), "Update Context", VfxContextType::Update, {560.0F, 0.0F}},
                  {DerivedGraphId(published.EmitterId, 5), "Output Context", VfxContextType::Output, {840.0F, 0.0F}}},
                 {}},
            };
        }
        ValidateVfxEffect(published);
        auto modules = Json::array();
        for (const auto& module : published.Modules)
            modules.push_back(EncodeModule(module));
        const Json document{{"schemaVersion", 2},
                            {"emitterId", IdText(published.EmitterId)},
                            {"name", published.Name},
                            {"loop", published.Loop},
                            {"duration", published.Duration},
                            {"space", SpaceName(published.Space)},
                            {"seed", published.Seed},
                            {"capacity", published.Capacity},
                            {"modules", std::move(modules)},
                            {"systems", EncodeSystems(published.Systems)},
                            {"blackboard", EncodeBlackboard(published.Blackboard)}};
        const auto encoded = document.dump(2);
        std::vector<std::byte> result(encoded.size());
        std::memcpy(result.data(), encoded.data(), encoded.size());
        return result;
    }

    AssetImporterRegistration CreateVfxEffectAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.VfxEffect";
        result.Version = 2;
        result.Type = VfxEffectAsset::StaticType();
        result.Extensions = {".keirevfx"};
        result.Import = [](const std::span<const std::byte> bytes)
        {
            const auto asset = VfxEffectAsset::Decode(bytes);
            return VfxEffectAsset::Encode(asset->Definition());
        };
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto asset = VfxEffectAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = VfxEffectAsset::Encode(asset->Definition());
            output.AssetDependencies = VfxEffectDependencies(asset->Definition());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateVfxEffectAssetDecoder()
    {
        return {VfxEffectAsset::StaticType(), VfxEffectAsset::Default(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return VfxEffectAsset::Decode(bytes); }};
    }
} // namespace Keire
