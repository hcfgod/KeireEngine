#include "Keire/Vfx/VfxSystem.h"

#include "VfxExecutionInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <queue>
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

        [[nodiscard]] std::string_view ExecutionSourceName(const VfxExecutionSource value)
        {
            switch (value)
            {
            case VfxExecutionSource::LegacyModules:
                return "legacyModules";
            case VfxExecutionSource::Graph:
                return "graph";
            }
            throw std::invalid_argument("VFX execution source is unsupported.");
        }

        [[nodiscard]] VfxExecutionSource ParseExecutionSource(const std::string_view value)
        {
            if (value == "legacyModules")
                return VfxExecutionSource::LegacyModules;
            if (value == "graph")
                return VfxExecutionSource::Graph;
            throw std::runtime_error("VFX execution source is unsupported.");
        }

        [[nodiscard]] std::string_view NodeKindName(const VfxGraphNodeKind value)
        {
            switch (value)
            {
            case VfxGraphNodeKind::Context:
                return "context";
            case VfxGraphNodeKind::Module:
                return "module";
            case VfxGraphNodeKind::Parameter:
                return "parameter";
            case VfxGraphNodeKind::CustomHlsl:
                return "customHlsl";
            }
            throw std::invalid_argument("VFX graph node kind is unsupported.");
        }

        [[nodiscard]] VfxGraphNodeKind ParseNodeKind(const std::string_view value)
        {
            if (value == "context")
                return VfxGraphNodeKind::Context;
            if (value == "module")
                return VfxGraphNodeKind::Module;
            if (value == "parameter")
                return VfxGraphNodeKind::Parameter;
            if (value == "customHlsl")
                return VfxGraphNodeKind::CustomHlsl;
            throw std::runtime_error("VFX graph node kind is unsupported.");
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
            case VfxValueType::ParticleStream:
                return "particleStream";
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
            if (value == "particleStream")
                return VfxValueType::ParticleStream;
            throw std::runtime_error("VFX value type is unsupported.");
        }

        [[nodiscard]] Json EncodeTypedValue(const VfxValueType type, const VfxParameterValue& value)
        {
            switch (type)
            {
            case VfxValueType::Boolean:
                return std::get<bool>(value);
            case VfxValueType::Integer:
                return std::get<std::int64_t>(value);
            case VfxValueType::Scalar:
                return std::get<float>(value);
            case VfxValueType::Vector2:
            {
                const auto vector = std::get<Vector2>(value);
                return Json::array({vector.X, vector.Y});
            }
            case VfxValueType::Vector3:
                return EncodeVector(std::get<Vector3>(value));
            case VfxValueType::Color:
                return EncodeColor(std::get<Color>(value));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
                return IdText(std::get<AssetId>(value));
            case VfxValueType::ParticleStream:
                break;
            }
            throw std::invalid_argument("VFX typed value is unsupported.");
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
            case VfxValueType::ParticleStream:
                break;
            }
            throw std::runtime_error("VFX typed default value is malformed.");
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
                    {
                        Json encodedPin{
                            {"id", IdText(pin.Id)}, {"name", pin.Name},         {"type", ValueTypeName(pin.Type)},
                            {"input", pin.Input},   {"semantic", pin.Semantic}, {"default", nullptr}};
                        if (pin.DefaultValue)
                            encodedPin["default"] = EncodeTypedValue(pin.Type, *pin.DefaultValue);
                        pins.push_back(std::move(encodedPin));
                    }
                    nodes.push_back({{"id", IdText(node.Id)},
                                     {"type", node.Type},
                                     {"context", ContextName(node.Context)},
                                     {"position", Json::array({node.EditorPosition.X, node.EditorPosition.Y})},
                                     {"pins", std::move(pins)},
                                     {"customHlsl", node.CustomHlsl},
                                     {"kind", NodeKindName(node.Kind)},
                                     {"reference", IdText(node.Reference)}});
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

        [[nodiscard]] std::vector<VfxGraphSystem> DecodeSystems(const Json& value, const std::uint32_t schemaVersion)
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
                    if (schemaVersion >= 3)
                    {
                        node.Kind = ParseNodeKind(encodedNode.value("kind", std::string("context")));
                        node.Reference = ParseId(encodedNode, "reference");
                    }
                    for (const auto& encodedPin : encodedNode.at("pins"))
                    {
                        VfxGraphPin pin{ParseId(encodedPin, "id"), encodedPin.at("name").get<std::string>(),
                                        ParseValueType(encodedPin.at("type").get<std::string>()),
                                        encodedPin.value("input", true)};
                        if (schemaVersion >= 3)
                        {
                            pin.Semantic = encodedPin.value("semantic", std::string{});
                            if (encodedPin.contains("default") && !encodedPin.at("default").is_null())
                                pin.DefaultValue = DecodeParameterValue(pin.Type, encodedPin.at("default"));
                        }
                        node.Pins.push_back(std::move(pin));
                    }
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
                                  {"default", EncodeTypedValue(parameter.Type, parameter.DefaultValue)},
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

        [[nodiscard]] bool ValueMatchesType(const VfxValueType type, const VfxParameterValue& value) noexcept
        {
            switch (type)
            {
            case VfxValueType::Boolean:
                return std::holds_alternative<bool>(value);
            case VfxValueType::Integer:
                return std::holds_alternative<std::int64_t>(value);
            case VfxValueType::Scalar:
                return std::holds_alternative<float>(value) && std::isfinite(std::get<float>(value));
            case VfxValueType::Vector2:
                return std::holds_alternative<Vector2>(value) && Math::IsFinite(std::get<Vector2>(value));
            case VfxValueType::Vector3:
                return std::holds_alternative<Vector3>(value) && Math::IsFinite(std::get<Vector3>(value));
            case VfxValueType::Color:
                return std::holds_alternative<Color>(value) && Math::IsFinite(std::get<Color>(value));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
                return std::holds_alternative<AssetId>(value);
            case VfxValueType::ParticleStream:
                return false;
            }
            return false;
        }

        [[nodiscard]] VfxContextType ModuleContext(const VfxModulePayload& payload) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule&) { return VfxContextType::Spawn; },
                    [](const VfxBurstModule&) { return VfxContextType::Spawn; },
                    [](const VfxShapeModule&) { return VfxContextType::Initialize; },
                    [](const VfxInitializeModule&) { return VfxContextType::Initialize; },
                    [](const VfxForceModule&) { return VfxContextType::Update; },
                    [](const VfxSizeOverLifetimeModule&) { return VfxContextType::Update; },
                    [](const VfxColorOverLifetimeModule&) { return VfxContextType::Update; },
                    [](const VfxCollisionModule&) { return VfxContextType::Update; },
                    [](const VfxRendererModule&) { return VfxContextType::Output; },
                },
                payload);
        }

        struct ModulePinSpecification
        {
            std::string_view Name;
            std::string_view Semantic;
            VfxValueType Type = VfxValueType::Scalar;
            VfxModuleProperty Property = VfxModuleProperty::None;
            VfxParameterValue DefaultValue = 0.0F;
        };

        [[nodiscard]] std::vector<ModulePinSpecification> ModulePinSpecifications(const VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Particles Per Second", "particlesPerSecond", VfxValueType::Scalar,
                             VfxModuleProperty::EmissionParticlesPerSecond, value.ParticlesPerSecond}};
                    },
                    [](const VfxBurstModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Time", "time", VfxValueType::Scalar, VfxModuleProperty::BurstTime, value.Time},
                            {"Count", "count", VfxValueType::Integer, VfxModuleProperty::BurstCount,
                             static_cast<std::int64_t>(value.Count)},
                            {"Cycles", "cycles", VfxValueType::Integer, VfxModuleProperty::BurstCycles,
                             static_cast<std::int64_t>(value.Cycles)},
                            {"Interval", "interval", VfxValueType::Scalar, VfxModuleProperty::BurstInterval,
                             value.Interval}};
                    },
                    [](const VfxShapeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Box Half Extent", "boxHalfExtent", VfxValueType::Vector3,
                             VfxModuleProperty::ShapeBoxHalfExtent, value.BoxHalfExtent},
                            {"Radius", "radius", VfxValueType::Scalar, VfxModuleProperty::ShapeRadius, value.Radius},
                            {"Cone Angle", "coneAngleDegrees", VfxValueType::Scalar,
                             VfxModuleProperty::ShapeConeAngleDegrees, value.ConeAngleDegrees},
                            {"Cone Length", "coneLength", VfxValueType::Scalar, VfxModuleProperty::ShapeConeLength,
                             value.ConeLength},
                            {"Mesh", "mesh", VfxValueType::Mesh, VfxModuleProperty::ShapeMesh, value.Mesh},
                            {"Volume", "volume", VfxValueType::Asset, VfxModuleProperty::ShapeVolume, value.Volume}};
                    },
                    [](const VfxInitializeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Lifetime Minimum", "lifetimeMinimum", VfxValueType::Scalar,
                             VfxModuleProperty::InitializeLifetimeMinimum, value.LifetimeMinimum},
                            {"Lifetime Maximum", "lifetimeMaximum", VfxValueType::Scalar,
                             VfxModuleProperty::InitializeLifetimeMaximum, value.LifetimeMaximum},
                            {"Velocity Minimum", "velocityMinimum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeVelocityMinimum, value.VelocityMinimum},
                            {"Velocity Maximum", "velocityMaximum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeVelocityMaximum, value.VelocityMaximum},
                            {"Rotation Minimum", "rotationMinimum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeRotationMinimum, value.RotationMinimum},
                            {"Rotation Maximum", "rotationMaximum", VfxValueType::Vector3,
                             VfxModuleProperty::InitializeRotationMaximum, value.RotationMaximum}};
                    },
                    [](const VfxForceModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Force", "force", VfxValueType::Vector3, VfxModuleProperty::ForceVector, value.Force},
                            {"Gravity Multiplier", "gravityMultiplier", VfxValueType::Scalar,
                             VfxModuleProperty::ForceGravityMultiplier, value.GravityMultiplier}};
                    },
                    [](const VfxSizeOverLifetimeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{{"Size", "size", VfxValueType::Scalar,
                                                                    VfxModuleProperty::SizeConstant,
                                                                    value.Size.Evaluate(0.0F)}};
                    },
                    [](const VfxColorOverLifetimeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{{"Color", "color", VfxValueType::Color,
                                                                    VfxModuleProperty::ColorConstant,
                                                                    value.Color.Evaluate(0.0F)}};
                    },
                    [](const VfxCollisionModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Restitution", "restitution", VfxValueType::Scalar,
                             VfxModuleProperty::CollisionRestitution, value.Restitution},
                            {"Kill On Collision", "killOnCollision", VfxValueType::Boolean,
                             VfxModuleProperty::CollisionKillOnCollision, value.KillOnCollision}};
                    },
                    [](const VfxRendererModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Sprite", "sprite", VfxValueType::Texture, VfxModuleProperty::RendererSprite,
                             value.Sprite},
                            {"Mesh", "mesh", VfxValueType::Mesh, VfxModuleProperty::RendererMesh, value.Mesh}};
                    },
                },
                payload);
        }

        [[nodiscard]] std::uint32_t ContextOrder(const VfxContextType context)
        {
            switch (context)
            {
            case VfxContextType::Spawn:
                return 0;
            case VfxContextType::Initialize:
                return 1;
            case VfxContextType::Update:
                return 2;
            case VfxContextType::Output:
                return 3;
            case VfxContextType::Event:
                break;
            }
            throw std::invalid_argument("VFX Event contexts are not executable.");
        }

        [[nodiscard]] const VfxGraphPin* FindPin(const VfxGraphNode& node, const bool input, const VfxValueType type,
                                                 const std::string_view semantic) noexcept
        {
            const auto found =
                std::ranges::find_if(node.Pins, [input, type, semantic](const VfxGraphPin& pin)
                                     { return pin.Input == input && pin.Type == type && pin.Semantic == semantic; });
            return found == node.Pins.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] std::uint64_t HashBytes(const std::span<const std::byte> bytes) noexcept
        {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto value : bytes)
            {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        [[nodiscard]] std::vector<std::byte> JsonBytes(const Json& value)
        {
            const auto encoded = value.dump();
            std::vector<std::byte> result(encoded.size());
            std::memcpy(result.data(), encoded.data(), encoded.size());
            return result;
        }

        [[nodiscard]] AssetId AllocateDerivedId(const AssetId source, std::uint64_t salt, std::set<AssetId>& used)
        {
            for (;; ++salt)
            {
                const auto candidate = DerivedGraphId(source, salt);
                if (candidate && used.insert(candidate).second)
                    return candidate;
            }
        }

        [[nodiscard]] std::string_view Trim(std::string_view value) noexcept
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
                value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] bool IsIdentifier(const std::string_view value) noexcept
        {
            if (value.empty() || (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_'))
                return false;
            return std::ranges::all_of(
                value.substr(1), [](const char character)
                { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; });
        }

        [[nodiscard]] float ParsePortableFloat(std::string_view value)
        {
            value = Trim(value);
            if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
                value.remove_suffix(1);
            float result = 0.0F;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                !std::isfinite(result) || std::abs(result) > MaximumAuthoredScalar)
            {
                throw std::invalid_argument("Portable Custom HLSL contains an invalid numeric literal.");
            }
            return result;
        }

        struct PortableLiteral
        {
            VfxValueType Type = VfxValueType::Scalar;
            VfxParameterValue Value = 0.0F;
        };

        [[nodiscard]] PortableLiteral ParsePortableLiteral(std::string_view value)
        {
            value = Trim(value);
            std::size_t componentCount = 0;
            if (value.starts_with("float2(") && value.ends_with(')'))
                componentCount = 2;
            else if (value.starts_with("float3(") && value.ends_with(')'))
                componentCount = 3;
            else if (value.starts_with("float4(") && value.ends_with(')'))
                componentCount = 4;
            else
                return {VfxValueType::Scalar, ParsePortableFloat(value)};

            value.remove_prefix(7);
            value.remove_suffix(1);
            std::array<float, 4> components{};
            for (std::size_t index = 0; index < componentCount; ++index)
            {
                const auto comma = value.find(',');
                if ((index + 1 < componentCount && comma == std::string_view::npos) ||
                    (index + 1 == componentCount && comma != std::string_view::npos))
                {
                    throw std::invalid_argument("Portable Custom HLSL vector literals have invalid arity.");
                }
                const auto component = index + 1 < componentCount ? value.substr(0, comma) : value;
                components[index] = ParsePortableFloat(component);
                if (index + 1 < componentCount)
                    value.remove_prefix(comma + 1);
            }
            if (componentCount == 2)
                return {VfxValueType::Vector2, Vector2{components[0], components[1]}};
            if (componentCount == 3)
                return {VfxValueType::Vector3, Vector3{components[0], components[1], components[2]}};
            return {VfxValueType::Color, Color{components[0], components[1], components[2], components[3]}};
        }

        struct PortableTarget
        {
            VfxCustomTarget Target = VfxCustomTarget::Velocity;
            VfxValueType Type = VfxValueType::Vector3;
        };

        [[nodiscard]] PortableTarget ParsePortableTarget(const std::string_view value)
        {
            if (value == "Position")
                return {VfxCustomTarget::Position, VfxValueType::Vector3};
            if (value == "Velocity")
                return {VfxCustomTarget::Velocity, VfxValueType::Vector3};
            if (value == "Rotation")
                return {VfxCustomTarget::Rotation, VfxValueType::Scalar};
            if (value == "Tint")
                return {VfxCustomTarget::Tint, VfxValueType::Color};
            if (value == "Size")
                return {VfxCustomTarget::Size, VfxValueType::Scalar};
            throw std::invalid_argument("Portable Custom HLSL targets Position, Velocity, Rotation, Tint, or Size.");
        }

        struct PortableInput
        {
            VfxValueType Type = VfxValueType::Scalar;
            std::uint32_t ParameterSlot = ~std::uint32_t{0};
            std::optional<VfxParameterValue> DefaultValue;
        };

        [[nodiscard]] bool PortableOperandMatches(const VfxValueType target, const VfxValueType operand) noexcept
        {
            return operand == VfxValueType::Scalar || operand == target;
        }

        [[nodiscard]] std::vector<VfxCompiledCustomInstruction>
        CompilePortableCustomHlsl(const VfxGraphNode& node, const std::map<std::string, PortableInput>& inputs)
        {
            std::vector<VfxCompiledCustomInstruction> result;
            std::size_t start = 0;
            while (start <= node.CustomHlsl.size())
            {
                const auto separator = node.CustomHlsl.find_first_of(";\r\n", start);
                auto statement =
                    Trim(std::string_view(node.CustomHlsl)
                             .substr(start, separator == std::string::npos ? std::string::npos : separator - start));
                start = separator == std::string::npos ? node.CustomHlsl.size() + 1 : separator + 1;
                if (statement.empty())
                    continue;
                if (result.size() >= 8)
                    throw std::invalid_argument("Portable Custom HLSL exceeds the eight-instruction limit.");

                std::size_t operationPosition = statement.find("+=");
                auto operation = VfxCustomOperation::Add;
                std::size_t operationLength = 2;
                if (operationPosition == std::string_view::npos)
                {
                    operationPosition = statement.find("*=");
                    operation = VfxCustomOperation::Multiply;
                }
                if (operationPosition == std::string_view::npos)
                {
                    operationPosition = statement.find('=');
                    operation = VfxCustomOperation::Assign;
                    operationLength = 1;
                }
                if (operationPosition == std::string_view::npos)
                    throw std::invalid_argument("Portable Custom HLSL statement has no supported assignment.");

                const auto target = ParsePortableTarget(Trim(statement.substr(0, operationPosition)));
                auto operandText = Trim(statement.substr(operationPosition + operationLength));
                bool scaleByDeltaTime = false;
                constexpr std::string_view deltaTime = "DeltaTime";
                if (operandText.ends_with(deltaTime))
                {
                    auto prefix = Trim(operandText.substr(0, operandText.size() - deltaTime.size()));
                    if (prefix.empty() || prefix.back() != '*')
                        throw std::invalid_argument(
                            "Portable Custom HLSL uses DeltaTime in an unsupported expression.");
                    prefix.remove_suffix(1);
                    operandText = Trim(prefix);
                    scaleByDeltaTime = true;
                }
                if (operandText.empty())
                    throw std::invalid_argument("Portable Custom HLSL statement has an empty operand.");

                VfxCompiledCustomInstruction instruction;
                instruction.Node = node.Id;
                instruction.Context = node.Context;
                instruction.Target = target.Target;
                instruction.Operation = operation;
                instruction.ScaleByDeltaTime = scaleByDeltaTime;
                if (IsIdentifier(operandText))
                {
                    const auto input = inputs.find(std::string(operandText));
                    if (input == inputs.end())
                        throw std::invalid_argument("Portable Custom HLSL references an unknown input semantic.");
                    instruction.OperandType = input->second.Type;
                    instruction.ParameterSlot = input->second.ParameterSlot;
                    if (instruction.ParameterSlot == ~std::uint32_t{0})
                    {
                        if (!input->second.DefaultValue)
                            throw std::invalid_argument(
                                "Portable Custom HLSL input must be connected or have a typed default.");
                        instruction.Literal = *input->second.DefaultValue;
                    }
                }
                else
                {
                    const auto literal = ParsePortableLiteral(operandText);
                    instruction.OperandType = literal.Type;
                    instruction.Literal = literal.Value;
                }
                if (!PortableOperandMatches(target.Type, instruction.OperandType))
                    throw std::invalid_argument("Portable Custom HLSL operand type does not match its target.");
                result.push_back(std::move(instruction));
            }
            if (result.empty())
                throw std::invalid_argument("Portable Custom HLSL nodes require at least one instruction.");
            return result;
        }

        struct LoweredPlan
        {
            std::vector<VfxCompiledParameter> Parameters;
            std::vector<VfxCompiledModule> Modules;
            std::vector<VfxCompiledBinding> Bindings;
            std::vector<VfxCompiledCustomInstruction> CustomInstructions;
            std::vector<VfxCompiledOperation> Operations;
        };

        [[nodiscard]] std::vector<VfxCompiledParameter>
        CompileParameters(const std::span<const VfxBlackboardParameter> blackboard)
        {
            std::vector<const VfxBlackboardParameter*> sorted;
            sorted.reserve(blackboard.size());
            for (const auto& parameter : blackboard)
                sorted.push_back(std::addressof(parameter));
            std::ranges::sort(sorted, {}, [](const VfxBlackboardParameter* parameter) { return parameter->Id; });

            std::vector<VfxCompiledParameter> result;
            result.reserve(sorted.size());
            for (std::uint32_t slot = 0; slot < sorted.size(); ++slot)
            {
                const auto& parameter = *sorted[slot];
                result.push_back({parameter.Id, parameter.Type, parameter.DefaultValue, slot, parameter.Exposed});
            }
            return result;
        }

        [[nodiscard]] LoweredPlan LowerLegacyModules(const VfxEffectDefinition& definition)
        {
            LoweredPlan result;
            result.Parameters = CompileParameters(definition.Blackboard);
            for (std::uint32_t index = 0; index < definition.Modules.size(); ++index)
            {
                const auto& module = definition.Modules[index];
                if (module.Enabled)
                {
                    const auto context = ModuleContext(module.Payload);
                    const auto operationIndex = static_cast<std::uint32_t>(result.Modules.size());
                    result.Modules.push_back({module.Id, module.Id, context, index});
                    result.Operations.push_back({module.Id, context, VfxCompiledOperationKind::Module, operationIndex});
                }
            }
            return result;
        }

        struct LocatedPin
        {
            const VfxGraphNode* Node = nullptr;
            const VfxGraphPin* Pin = nullptr;
        };

        [[nodiscard]] std::size_t CountFlowPins(const VfxGraphNode& node, const bool input) noexcept
        {
            return std::ranges::count_if(node.Pins, [input](const VfxGraphPin& pin)
                                         { return pin.Input == input && pin.Type == VfxValueType::ParticleStream; });
        }

        void ValidateContextNode(const VfxGraphNode& node)
        {
            const auto expectedInputs = node.Context == VfxContextType::Spawn ? 0U : 1U;
            const auto expectedOutputs = node.Context == VfxContextType::Output ? 0U : 1U;
            if (node.Reference || !node.CustomHlsl.empty() || node.Pins.size() != expectedInputs + expectedOutputs ||
                CountFlowPins(node, true) != expectedInputs || CountFlowPins(node, false) != expectedOutputs)
            {
                throw std::invalid_argument("VFX context nodes require canonical particle-stream pins.");
            }
            for (const auto& pin : node.Pins)
                if (pin.Semantic != "particles")
                    throw std::invalid_argument("VFX context nodes require the particles pin semantic.");
        }

        void ValidateModuleNode(const VfxGraphNode& node, const VfxModuleDefinition& module)
        {
            const auto specifications = ModulePinSpecifications(module.Payload);
            if (!node.Reference || node.Context != ModuleContext(module.Payload) || !node.CustomHlsl.empty() ||
                CountFlowPins(node, true) != 1 || CountFlowPins(node, false) != 1 ||
                node.Pins.size() != specifications.size() + 2)
            {
                throw std::invalid_argument("VFX module node does not match its referenced Runtime Module.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (!semantics.insert(pin.Semantic).second && pin.Type != VfxValueType::ParticleStream)
                    throw std::invalid_argument("VFX module node contains a duplicate input semantic.");
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    if (pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("VFX module flow pins are malformed.");
                    continue;
                }
                if (!pin.Input)
                    throw std::invalid_argument("VFX module property pins must be inputs.");
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (specification == specifications.end() || specification->Type != pin.Type || !pin.DefaultValue ||
                    !ValueMatchesType(pin.Type, *pin.DefaultValue) || *pin.DefaultValue != specification->DefaultValue)
                {
                    throw std::invalid_argument(
                        "VFX module node contains an unknown, stale, or type-mismatched property pin.");
                }
            }
            for (const auto& specification : specifications)
            {
                if (!FindPin(node, true, specification.Type, specification.Semantic))
                    throw std::invalid_argument("VFX module node is missing a canonical property pin.");
            }
        }

        void ValidateParameterNode(const VfxGraphNode& node, const VfxBlackboardParameter& parameter)
        {
            if (!node.Reference || !node.CustomHlsl.empty() || node.Pins.size() != 1)
                throw std::invalid_argument("VFX parameter nodes require one canonical output pin.");
            const auto& pin = node.Pins.front();
            if (pin.Input || pin.Type != parameter.Type || pin.Type == VfxValueType::ParticleStream ||
                pin.Semantic != "value" || pin.DefaultValue)
            {
                throw std::invalid_argument("VFX parameter node output does not match its Blackboard parameter.");
            }
        }

        void ValidateCustomNode(const VfxGraphNode& node)
        {
            if (node.Reference || node.CustomHlsl.empty() || CountFlowPins(node, true) != 1 ||
                CountFlowPins(node, false) != 1)
            {
                throw std::invalid_argument("Portable Custom HLSL nodes require particle-stream input and output.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    if (pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("Portable Custom HLSL flow pins are malformed.");
                    continue;
                }
                if (!pin.Input || !IsIdentifier(pin.Semantic) || !semantics.insert(pin.Semantic).second ||
                    pin.Type == VfxValueType::Boolean || pin.Type == VfxValueType::Integer ||
                    pin.Type >= VfxValueType::Texture ||
                    (pin.DefaultValue && !ValueMatchesType(pin.Type, *pin.DefaultValue)))
                {
                    throw std::invalid_argument("Portable Custom HLSL contains an invalid typed input pin.");
                }
            }
        }

        [[nodiscard]] LoweredPlan LowerGraph(const VfxEffectDefinition& definition)
        {
            if (definition.Systems.size() != 1)
                throw std::invalid_argument("Executable VFX graphs require exactly one particle system.");
            const auto& system = definition.Systems.front();

            std::map<AssetId, std::pair<const VfxModuleDefinition*, std::uint32_t>> modules;
            for (std::uint32_t index = 0; index < definition.Modules.size(); ++index)
                modules.emplace(definition.Modules[index].Id,
                                std::pair{std::addressof(definition.Modules[index]), index});
            std::map<AssetId, const VfxBlackboardParameter*> parameters;
            for (const auto& parameter : definition.Blackboard)
                parameters.emplace(parameter.Id, std::addressof(parameter));

            LoweredPlan result;
            result.Parameters = CompileParameters(definition.Blackboard);
            std::map<AssetId, std::uint32_t> parameterSlots;
            for (const auto& parameter : result.Parameters)
                parameterSlots.emplace(parameter.Parameter, parameter.Slot);

            std::map<AssetId, const VfxGraphNode*> nodes;
            std::map<AssetId, LocatedPin> pins;
            std::array<const VfxGraphNode*, 4> contexts{};
            std::set<AssetId> referencedModules;
            for (const auto& node : system.Nodes)
            {
                nodes.emplace(node.Id, std::addressof(node));
                if (node.Context == VfxContextType::Event)
                    throw std::invalid_argument("VFX Event contexts are not supported by the particle compiler.");
                for (const auto& pin : node.Pins)
                    pins.emplace(pin.Id, LocatedPin{std::addressof(node), std::addressof(pin)});
                switch (node.Kind)
                {
                case VfxGraphNodeKind::Context:
                {
                    ValidateContextNode(node);
                    const auto index = ContextOrder(node.Context);
                    if (contexts[index])
                        throw std::invalid_argument("Executable VFX graphs require one context of each stage.");
                    contexts[index] = std::addressof(node);
                    break;
                }
                case VfxGraphNodeKind::Module:
                {
                    const auto module = modules.find(node.Reference);
                    if (module == modules.end() || !referencedModules.insert(node.Reference).second)
                        throw std::invalid_argument(
                            "VFX module node has an unknown or duplicate Runtime Module reference.");
                    ValidateModuleNode(node, *module->second.first);
                    break;
                }
                case VfxGraphNodeKind::Parameter:
                {
                    const auto parameter = parameters.find(node.Reference);
                    if (parameter == parameters.end())
                        throw std::invalid_argument("VFX parameter node references an unknown Blackboard parameter.");
                    ValidateParameterNode(node, *parameter->second);
                    break;
                }
                case VfxGraphNodeKind::CustomHlsl:
                    ValidateCustomNode(node);
                    break;
                }
            }
            if (std::ranges::any_of(contexts, [](const VfxGraphNode* context) { return context == nullptr; }))
                throw std::invalid_argument(
                    "Executable VFX graphs require Spawn, Initialize, Update, and Output contexts.");

            std::map<AssetId, const VfxGraphConnection*> inputDrivers;
            std::map<AssetId, std::size_t> indegree;
            std::map<AssetId, std::vector<AssetId>> adjacency;
            std::map<AssetId, std::vector<AssetId>> flowAdjacency;
            std::map<AssetId, std::vector<AssetId>> reverseFlowAdjacency;
            for (const auto& node : system.Nodes)
                indegree.emplace(node.Id, 0);
            for (const auto& connection : system.Connections)
            {
                const auto output = pins.find(connection.OutputPin);
                const auto input = pins.find(connection.InputPin);
                if (output == pins.end() || input == pins.end() || output->second.Node->Id != connection.OutputNode ||
                    input->second.Node->Id != connection.InputNode)
                {
                    throw std::invalid_argument("VFX graph connection references a mismatched node or pin.");
                }
                if (!inputDrivers.emplace(connection.InputPin, std::addressof(connection)).second)
                    throw std::invalid_argument("VFX graph input pins may have at most one cable.");
                ++indegree.at(connection.InputNode);
                adjacency[connection.OutputNode].push_back(connection.InputNode);
                if (output->second.Pin->Type == VfxValueType::ParticleStream)
                {
                    if (ContextOrder(output->second.Node->Context) > ContextOrder(input->second.Node->Context))
                        throw std::invalid_argument(
                            "VFX particle-stream cables cannot travel backwards across contexts.");
                    flowAdjacency[connection.OutputNode].push_back(connection.InputNode);
                    reverseFlowAdjacency[connection.InputNode].push_back(connection.OutputNode);
                }
            }

            std::set<AssetId> ready;
            for (const auto& [node, count] : indegree)
                if (count == 0)
                    ready.insert(node);
            std::vector<AssetId> topologicalOrder;
            topologicalOrder.reserve(nodes.size());
            while (!ready.empty())
            {
                const auto node = *ready.begin();
                ready.erase(ready.begin());
                topologicalOrder.push_back(node);
                for (const auto destination : adjacency[node])
                {
                    auto& count = indegree.at(destination);
                    if (--count == 0)
                        ready.insert(destination);
                }
            }
            if (topologicalOrder.size() != nodes.size())
                throw std::invalid_argument("VFX graph must be a directed acyclic graph.");

            const auto visitFlow = [](const AssetId start, const std::map<AssetId, std::vector<AssetId>>& edges)
            {
                std::set<AssetId> visited;
                std::queue<AssetId> pending;
                visited.insert(start);
                pending.push(start);
                while (!pending.empty())
                {
                    const auto node = pending.front();
                    pending.pop();
                    const auto destinations = edges.find(node);
                    if (destinations == edges.end())
                        continue;
                    for (const auto destination : destinations->second)
                    {
                        if (visited.insert(destination).second)
                            pending.push(destination);
                    }
                }
                return visited;
            };
            const auto fromSpawn = visitFlow(contexts[0]->Id, flowAdjacency);
            const auto toOutput = visitFlow(contexts[3]->Id, reverseFlowAdjacency);
            for (const auto* context : contexts)
                if (!fromSpawn.contains(context->Id) || !toOutput.contains(context->Id))
                    throw std::invalid_argument("VFX contexts must share one connected particle-stream path.");

            bool hasEmission = false;
            bool hasRenderer = false;
            for (const auto nodeId : topologicalOrder)
            {
                const auto& node = *nodes.at(nodeId);
                if (node.Kind != VfxGraphNodeKind::Module && node.Kind != VfxGraphNodeKind::CustomHlsl)
                    continue;
                if (!fromSpawn.contains(node.Id) || !toOutput.contains(node.Id))
                    throw std::invalid_argument("Executable VFX nodes must be connected to the main particle stream.");
                if (node.Kind == VfxGraphNodeKind::Module)
                {
                    const auto [module, index] = modules.at(node.Reference);
                    if (!module->Enabled)
                        continue;
                    const auto operationIndex = static_cast<std::uint32_t>(result.Modules.size());
                    result.Modules.push_back({node.Id, module->Id, node.Context, index});
                    result.Operations.push_back(
                        {node.Id, node.Context, VfxCompiledOperationKind::Module, operationIndex});
                    hasEmission |= std::holds_alternative<VfxEmissionRateModule>(module->Payload) ||
                                   std::holds_alternative<VfxBurstModule>(module->Payload);
                    hasRenderer |= std::holds_alternative<VfxRendererModule>(module->Payload);

                    const auto specifications = ModulePinSpecifications(module->Payload);
                    for (const auto& specification : specifications)
                    {
                        const auto* input = FindPin(node, true, specification.Type, specification.Semantic);
                        const auto driver = inputDrivers.find(input->Id);
                        if (driver == inputDrivers.end())
                            continue;
                        const auto& source = *nodes.at(driver->second->OutputNode);
                        if (source.Kind != VfxGraphNodeKind::Parameter)
                            throw std::invalid_argument("VFX module properties may only bind Blackboard parameters.");
                        const auto& output = *pins.at(driver->second->OutputPin).Pin;
                        if (output.Type != specification.Type)
                            throw std::invalid_argument("VFX module binding type does not match its property.");
                        result.Bindings.push_back(
                            {node.Id, module->Id, specification.Property, parameterSlots.at(source.Reference)});
                    }
                }
                else
                {
                    std::map<std::string, PortableInput> inputs;
                    for (const auto& input : node.Pins)
                    {
                        if (!input.Input || input.Type == VfxValueType::ParticleStream)
                            continue;
                        PortableInput source;
                        source.Type = input.Type;
                        source.DefaultValue = input.DefaultValue;
                        const auto driver = inputDrivers.find(input.Id);
                        if (driver != inputDrivers.end())
                        {
                            const auto& outputNode = *nodes.at(driver->second->OutputNode);
                            if (outputNode.Kind != VfxGraphNodeKind::Parameter)
                                throw std::invalid_argument(
                                    "Portable Custom HLSL inputs may only bind Blackboard parameters.");
                            source.ParameterSlot = parameterSlots.at(outputNode.Reference);
                        }
                        inputs.emplace(input.Semantic, std::move(source));
                    }
                    auto instructions = CompilePortableCustomHlsl(node, inputs);
                    if (result.CustomInstructions.size() + instructions.size() > 8)
                        throw std::invalid_argument("VFX graph exceeds the eight-instruction Custom HLSL budget.");
                    for (auto& instruction : instructions)
                    {
                        const auto operationIndex = static_cast<std::uint32_t>(result.CustomInstructions.size());
                        result.CustomInstructions.push_back(std::move(instruction));
                        result.Operations.push_back(
                            {node.Id, node.Context, VfxCompiledOperationKind::CustomHlsl, operationIndex});
                    }
                }
            }
            if (!hasEmission || !hasRenderer)
                throw std::invalid_argument("Executable VFX graphs require connected emission and renderer modules.");
            return result;
        }

        [[nodiscard]] LoweredPlan LowerEffect(const VfxEffectDefinition& definition)
        {
            if (definition.ExecutionSource == VfxExecutionSource::Graph)
                return LowerGraph(definition);
            return LowerLegacyModules(definition);
        }

        [[nodiscard]] Json EncodeCompiledParameters(const std::span<const VfxCompiledParameter> parameters,
                                                    const bool includeDefaults)
        {
            auto result = Json::array();
            for (const auto& parameter : parameters)
            {
                Json encoded{{"id", IdText(parameter.Parameter)},
                             {"type", ValueTypeName(parameter.Type)},
                             {"slot", parameter.Slot},
                             {"exposed", parameter.Exposed}};
                if (includeDefaults)
                    encoded["default"] = EncodeTypedValue(parameter.Type, parameter.DefaultValue);
                result.push_back(std::move(encoded));
            }
            return result;
        }

        [[nodiscard]] Json EncodeCompiledModules(const VfxEffectDefinition& definition,
                                                 const std::span<const VfxCompiledModule> modules,
                                                 const bool includePayload)
        {
            auto result = Json::array();
            for (const auto& module : modules)
            {
                Json encoded{{"node", IdText(module.Node)},
                             {"module", IdText(module.Module)},
                             {"context", ContextName(module.Context)}};
                if (includePayload)
                {
                    if (module.ModuleIndex >= definition.Modules.size() ||
                        definition.Modules[module.ModuleIndex].Id != module.Module)
                    {
                        throw std::invalid_argument("VFX compiled module index is invalid.");
                    }
                    encoded["payload"] = EncodeModule(definition.Modules[module.ModuleIndex]);
                }
                else
                    encoded["type"] = ModuleTypeName(definition.Modules[module.ModuleIndex].Payload);
                result.push_back(std::move(encoded));
            }
            return result;
        }

        [[nodiscard]] Json EncodeCompiledBindings(const std::span<const VfxCompiledBinding> bindings)
        {
            auto result = Json::array();
            for (const auto& binding : bindings)
                result.push_back({{"node", IdText(binding.Node)},
                                  {"module", IdText(binding.Module)},
                                  {"property", static_cast<std::uint32_t>(binding.Property)},
                                  {"slot", binding.ParameterSlot}});
            return result;
        }

        [[nodiscard]] Json
        EncodeCompiledCustomInstructions(const std::span<const VfxCompiledCustomInstruction> instructions,
                                         const bool includeOperands)
        {
            auto result = Json::array();
            for (const auto& instruction : instructions)
            {
                Json encoded{{"node", IdText(instruction.Node)},
                             {"context", ContextName(instruction.Context)},
                             {"target", static_cast<std::uint32_t>(instruction.Target)},
                             {"operation", static_cast<std::uint32_t>(instruction.Operation)},
                             {"operandType", ValueTypeName(instruction.OperandType)},
                             {"deltaTime", instruction.ScaleByDeltaTime}};
                if (includeOperands)
                {
                    encoded["slot"] = instruction.ParameterSlot;
                    if (instruction.ParameterSlot == ~std::uint32_t{0})
                        encoded["literal"] = EncodeTypedValue(instruction.OperandType, instruction.Literal);
                }
                result.push_back(std::move(encoded));
            }
            return result;
        }

        [[nodiscard]] Json EncodeCompiledOperations(const std::span<const VfxCompiledOperation> operations)
        {
            auto result = Json::array();
            for (const auto& operation : operations)
            {
                result.push_back({{"node", IdText(operation.Node)},
                                  {"context", ContextName(operation.Context)},
                                  {"kind", static_cast<std::uint32_t>(operation.Kind)},
                                  {"index", operation.Index}});
            }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> BuildCanonicalIr(const VfxEffectDefinition& definition,
                                                              const LoweredPlan& plan)
        {
            return JsonBytes({{"emitterId", IdText(definition.EmitterId)},
                              {"loop", definition.Loop},
                              {"duration", definition.Duration},
                              {"space", SpaceName(definition.Space)},
                              {"seed", definition.Seed},
                              {"capacity", definition.Capacity},
                              {"executionSource", ExecutionSourceName(definition.ExecutionSource)},
                              {"parameters", EncodeCompiledParameters(plan.Parameters, true)},
                              {"modules", EncodeCompiledModules(definition, plan.Modules, true)},
                              {"bindings", EncodeCompiledBindings(plan.Bindings)},
                              {"customInstructions", EncodeCompiledCustomInstructions(plan.CustomInstructions, true)},
                              {"operations", EncodeCompiledOperations(plan.Operations)}});
        }

        [[nodiscard]] std::uint64_t BuildStateLayoutHash(const VfxEffectDefinition& definition, const LoweredPlan& plan)
        {
            const auto renderer = std::ranges::find_if(plan.Modules,
                                                       [&definition](const VfxCompiledModule& compiled)
                                                       {
                                                           return compiled.ModuleIndex < definition.Modules.size() &&
                                                                  std::holds_alternative<VfxRendererModule>(
                                                                      definition.Modules[compiled.ModuleIndex].Payload);
                                                       });
            if (renderer == plan.Modules.end())
                throw std::invalid_argument("VFX compiled program has no renderer representation.");
            const auto rendererType =
                std::get<VfxRendererModule>(definition.Modules[renderer->ModuleIndex].Payload).Type;
            const auto bytes =
                JsonBytes({{"emitterId", IdText(definition.EmitterId)},
                           {"space", SpaceName(definition.Space)},
                           {"seed", definition.Seed},
                           {"rendererType", static_cast<std::uint32_t>(rendererType)},
                           {"parameters", EncodeCompiledParameters(plan.Parameters, false)},
                           {"modules", EncodeCompiledModules(definition, plan.Modules, false)},
                           {"bindings", EncodeCompiledBindings(plan.Bindings)},
                           {"customInstructions", EncodeCompiledCustomInstructions(plan.CustomInstructions, false)},
                           {"operations", EncodeCompiledOperations(plan.Operations)}});
            return HashBytes(bytes);
        }
    } // namespace

    namespace Internal
    {
        VfxEffectDefinition ResolveVfxExecutableDefinition(const VfxEffectDefinition& source,
                                                           const VfxCompiledProgram& program,
                                                           const std::span<const VfxParameterValue> parameters)
        {
            auto result = source;
            result.ExecutionSource = VfxExecutionSource::LegacyModules;
            result.Systems.clear();
            result.Blackboard.clear();
            result.Modules.clear();
            result.Modules.reserve(program.Modules.size());
            for (const auto& compiled : program.Modules)
            {
                if (compiled.ModuleIndex >= source.Modules.size())
                    throw std::invalid_argument("VFX compiled module index is invalid.");
                const auto& module = source.Modules[compiled.ModuleIndex];
                if (module.Id != compiled.Module)
                    throw std::invalid_argument("VFX compiled module identity is invalid.");
                result.Modules.push_back(module);
            }

            for (const auto& binding : program.Bindings)
            {
                const auto module = std::ranges::find(result.Modules, binding.Module, &VfxModuleDefinition::Id);
                if (module == result.Modules.end() || binding.ParameterSlot >= parameters.size())
                    throw std::invalid_argument("VFX compiled binding is invalid.");
                const auto& value = parameters[binding.ParameterSlot];
                switch (binding.Property)
                {
                case VfxModuleProperty::EmissionParticlesPerSecond:
                    std::get<VfxEmissionRateModule>(module->Payload).ParticlesPerSecond = std::get<float>(value);
                    break;
                case VfxModuleProperty::BurstTime:
                    std::get<VfxBurstModule>(module->Payload).Time = std::get<float>(value);
                    break;
                case VfxModuleProperty::BurstCount:
                {
                    const auto integer = std::get<std::int64_t>(value);
                    if (integer < 0 || integer > std::numeric_limits<std::uint32_t>::max())
                        throw std::invalid_argument("VFX burst count override is outside the supported range.");
                    std::get<VfxBurstModule>(module->Payload).Count = static_cast<std::uint32_t>(integer);
                    break;
                }
                case VfxModuleProperty::BurstCycles:
                {
                    const auto integer = std::get<std::int64_t>(value);
                    if (integer < 0 || integer > std::numeric_limits<std::uint32_t>::max())
                        throw std::invalid_argument("VFX burst cycle override is outside the supported range.");
                    std::get<VfxBurstModule>(module->Payload).Cycles = static_cast<std::uint32_t>(integer);
                    break;
                }
                case VfxModuleProperty::BurstInterval:
                    std::get<VfxBurstModule>(module->Payload).Interval = std::get<float>(value);
                    break;
                case VfxModuleProperty::ShapeBoxHalfExtent:
                    std::get<VfxShapeModule>(module->Payload).BoxHalfExtent = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::ShapeRadius:
                    std::get<VfxShapeModule>(module->Payload).Radius = std::get<float>(value);
                    break;
                case VfxModuleProperty::ShapeConeAngleDegrees:
                    std::get<VfxShapeModule>(module->Payload).ConeAngleDegrees = std::get<float>(value);
                    break;
                case VfxModuleProperty::ShapeConeLength:
                    std::get<VfxShapeModule>(module->Payload).ConeLength = std::get<float>(value);
                    break;
                case VfxModuleProperty::ShapeMesh:
                    std::get<VfxShapeModule>(module->Payload).Mesh = std::get<AssetId>(value);
                    break;
                case VfxModuleProperty::ShapeVolume:
                    std::get<VfxShapeModule>(module->Payload).Volume = std::get<AssetId>(value);
                    break;
                case VfxModuleProperty::InitializeLifetimeMinimum:
                    std::get<VfxInitializeModule>(module->Payload).LifetimeMinimum = std::get<float>(value);
                    break;
                case VfxModuleProperty::InitializeLifetimeMaximum:
                    std::get<VfxInitializeModule>(module->Payload).LifetimeMaximum = std::get<float>(value);
                    break;
                case VfxModuleProperty::InitializeVelocityMinimum:
                    std::get<VfxInitializeModule>(module->Payload).VelocityMinimum = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::InitializeVelocityMaximum:
                    std::get<VfxInitializeModule>(module->Payload).VelocityMaximum = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::InitializeRotationMinimum:
                    std::get<VfxInitializeModule>(module->Payload).RotationMinimum = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::InitializeRotationMaximum:
                    std::get<VfxInitializeModule>(module->Payload).RotationMaximum = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::ForceVector:
                    std::get<VfxForceModule>(module->Payload).Force = std::get<Vector3>(value);
                    break;
                case VfxModuleProperty::ForceGravityMultiplier:
                    std::get<VfxForceModule>(module->Payload).GravityMultiplier = std::get<float>(value);
                    break;
                case VfxModuleProperty::SizeConstant:
                    std::get<VfxSizeOverLifetimeModule>(module->Payload).Size =
                        Curve1D::Constant(std::get<float>(value));
                    break;
                case VfxModuleProperty::ColorConstant:
                    std::get<VfxColorOverLifetimeModule>(module->Payload).Color =
                        ColorGradient::Constant(std::get<Color>(value));
                    break;
                case VfxModuleProperty::CollisionRestitution:
                    std::get<VfxCollisionModule>(module->Payload).Restitution = std::get<float>(value);
                    break;
                case VfxModuleProperty::CollisionKillOnCollision:
                    std::get<VfxCollisionModule>(module->Payload).KillOnCollision = std::get<bool>(value);
                    break;
                case VfxModuleProperty::RendererSprite:
                    std::get<VfxRendererModule>(module->Payload).Sprite = std::get<AssetId>(value);
                    break;
                case VfxModuleProperty::RendererMesh:
                    std::get<VfxRendererModule>(module->Payload).Mesh = std::get<AssetId>(value);
                    break;
                case VfxModuleProperty::None:
                    throw std::invalid_argument("VFX graph binding has no executable module property.");
                }
            }

            ValidateVfxEffect(result);
            return result;
        }
    } // namespace Internal

    void ValidateVfxEffect(const VfxEffectDefinition& definition)
    {
        if ((definition.SchemaVersion < 1 || definition.SchemaVersion > 3) || !definition.EmitterId ||
            definition.Name.empty() || definition.Name.size() > MaximumNameBytes ||
            !std::isfinite(definition.Duration) || definition.Duration < 0.001F || definition.Duration > 3600.0F ||
            definition.Capacity == 0 || definition.Capacity > 1'000'000 || definition.Modules.empty() ||
            definition.Modules.size() > MaximumModules || definition.ExecutionSource > VfxExecutionSource::Graph ||
            (definition.SchemaVersion < 3 && definition.ExecutionSource != VfxExecutionSource::LegacyModules))
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

        if (!definition.Systems.empty() || !definition.Blackboard.empty() ||
            definition.ExecutionSource == VfxExecutionSource::Graph)
        {
            if (definition.Systems.size() > MaximumSystems ||
                definition.Blackboard.size() > MaximumBlackboardParameters)
                throw std::invalid_argument("VFX graph system or blackboard count is invalid.");
            std::size_t nodeCount = 0;
            std::size_t connectionCount = 0;
            std::set<std::string> parameterNames;
            for (const auto& parameter : definition.Blackboard)
            {
                if (!parameter.Id || !stableIds.insert(parameter.Id).second || parameter.Name.empty() ||
                    parameter.Name.size() > MaximumNameBytes || !parameterNames.insert(parameter.Name).second ||
                    parameter.Type >= VfxValueType::ParticleStream ||
                    !ValueMatchesType(parameter.Type, parameter.DefaultValue))
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
                        !Math::IsFinite(node.EditorPosition) || node.CustomHlsl.size() > MaximumDocumentBytes ||
                        node.Context > VfxContextType::Event || node.Kind > VfxGraphNodeKind::CustomHlsl)
                        throw std::invalid_argument("VFX graph contains an invalid node.");
                    for (const auto& pin : node.Pins)
                    {
                        if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                            pin.Name.empty() || pin.Name.size() > MaximumNameBytes ||
                            pin.Semantic.size() > MaximumNameBytes || pin.Type > VfxValueType::ParticleStream ||
                            (pin.DefaultValue && (pin.Type == VfxValueType::ParticleStream ||
                                                  !ValueMatchesType(pin.Type, *pin.DefaultValue))))
                            throw std::invalid_argument("VFX graph contains an invalid pin.");
                    }
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
        if (definition.ExecutionSource == VfxExecutionSource::Graph)
            (void)LowerGraph(definition);
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
        for (const auto& system : definition.Systems)
            for (const auto& node : system.Nodes)
                for (const auto& pin : node.Pins)
                    if (pin.DefaultValue)
                        if (const auto* asset = std::get_if<AssetId>(&*pin.DefaultValue); asset && *asset)
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
            auto plan = LowerEffect(definition);
            result.CanonicalIr = BuildCanonicalIr(definition, plan);
            result.Hash = HashBytes(result.CanonicalIr);
            result.StateLayoutHash = BuildStateLayoutHash(definition, plan);
            result.Parameters = std::move(plan.Parameters);
            result.Modules = std::move(plan.Modules);
            result.Bindings = std::move(plan.Bindings);
            result.CustomInstructions = std::move(plan.CustomInstructions);
            result.Operations = std::move(plan.Operations);
            std::vector<VfxParameterValue> defaultParameters(result.Parameters.size());
            std::vector<bool> assignedDefaults(result.Parameters.size());
            for (const auto& parameter : result.Parameters)
            {
                if (parameter.Slot >= defaultParameters.size() || assignedDefaults[parameter.Slot])
                    throw std::invalid_argument("VFX compiled parameter layout is invalid.");
                defaultParameters[parameter.Slot] = parameter.DefaultValue;
                assignedDefaults[parameter.Slot] = true;
            }
            (void)Internal::ResolveVfxExecutableDefinition(definition, result, defaultParameters);
            if (backend == VfxBackend::Cpu)
            {
                for (const auto& compiled : result.Modules)
                {
                    const auto& module = definition.Modules.at(compiled.ModuleIndex);
                    if (const auto* collision = std::get_if<VfxCollisionModule>(&module.Payload);
                        collision && collision->Mode == VfxCollisionMode::GpuDepth)
                        result.Diagnostics.push_back(
                            {VfxCompileDiagnosticSeverity::Warning, compiled.Node,
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

    VfxGraphNode CreateVfxGraphModuleNode(const VfxModuleDefinition& module, const Vector2 editorPosition)
    {
        if (!module.Id)
            throw std::invalid_argument("VFX module nodes require a valid Runtime Module reference.");
        VfxGraphNode result;
        result.Id = AssetId::Generate();
        result.Type = std::string(ModuleTypeName(module.Payload));
        result.Context = ModuleContext(module.Payload);
        result.EditorPosition = editorPosition;
        result.Kind = VfxGraphNodeKind::Module;
        result.Reference = module.Id;
        result.Pins.push_back(
            {AssetId::Generate(), "Particles", VfxValueType::ParticleStream, true, "particles", std::nullopt});
        for (const auto& specification : ModulePinSpecifications(module.Payload))
        {
            result.Pins.push_back({AssetId::Generate(), std::string(specification.Name), specification.Type, true,
                                   std::string(specification.Semantic), specification.DefaultValue});
        }
        result.Pins.push_back(
            {AssetId::Generate(), "Particles", VfxValueType::ParticleStream, false, "particles", std::nullopt});
        return result;
    }

    VfxEffectDefinition ConvertVfxEffectToGraph(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffect(definition);
        if (definition.ExecutionSource == VfxExecutionSource::Graph)
            return definition;
        auto result = definition;
        result.SchemaVersion = 3;
        result.ExecutionSource = VfxExecutionSource::Graph;
        result.Systems.clear();

        std::set<AssetId> used{result.EmitterId};
        for (const auto& module : result.Modules)
            used.insert(module.Id);
        for (const auto& parameter : result.Blackboard)
            used.insert(parameter.Id);

        VfxGraphSystem system;
        system.Id = AllocateDerivedId(result.EmitterId, 0x1000, used);
        system.Name = "Particle System";
        float cursorX = 0.0F;
        std::uint64_t connectionSalt = 0x8000;
        AssetId previousNode;
        AssetId previousOutput;

        const auto connect = [&](const AssetId inputNode, const AssetId inputPin)
        {
            if (!previousNode || !previousOutput)
                throw std::logic_error("VFX graph conversion has no particle-stream source.");
            system.Connections.push_back({AllocateDerivedId(result.EmitterId, connectionSalt++, used), previousNode,
                                          previousOutput, inputNode, inputPin});
        };

        const auto appendContext = [&](const VfxContextType context)
        {
            VfxGraphNode node;
            node.Id = AllocateDerivedId(result.EmitterId, 0x2000 + ContextOrder(context) * 0x100, used);
            node.Type = std::string(ContextName(context)) + " Context";
            node.Context = context;
            node.EditorPosition = {cursorX, 0.0F};
            node.Kind = VfxGraphNodeKind::Context;
            cursorX += 280.0F;
            if (context != VfxContextType::Spawn)
            {
                node.Pins.push_back({AllocateDerivedId(node.Id, 1, used), "Particles", VfxValueType::ParticleStream,
                                     true, "particles", std::nullopt});
                connect(node.Id, node.Pins.back().Id);
            }
            if (context != VfxContextType::Output)
            {
                node.Pins.push_back({AllocateDerivedId(node.Id, 2, used), "Particles", VfxValueType::ParticleStream,
                                     false, "particles", std::nullopt});
                previousNode = node.Id;
                previousOutput = node.Pins.back().Id;
            }
            system.Nodes.push_back(std::move(node));
        };

        const auto appendModules = [&](const VfxContextType context)
        {
            for (const auto& module : result.Modules)
            {
                if (ModuleContext(module.Payload) != context)
                    continue;
                auto node = CreateVfxGraphModuleNode(module, {cursorX, 0.0F});
                node.Id = AllocateDerivedId(module.Id, 0x3000, used);
                for (std::size_t pinIndex = 0; pinIndex < node.Pins.size(); ++pinIndex)
                    node.Pins[pinIndex].Id = AllocateDerivedId(module.Id, 0x3100 + pinIndex, used);
                cursorX += 280.0F;
                const auto* input = FindPin(node, true, VfxValueType::ParticleStream, "particles");
                const auto* output = FindPin(node, false, VfxValueType::ParticleStream, "particles");
                connect(node.Id, input->Id);
                previousNode = node.Id;
                previousOutput = output->Id;
                system.Nodes.push_back(std::move(node));
            }
        };

        appendContext(VfxContextType::Spawn);
        appendModules(VfxContextType::Spawn);
        appendContext(VfxContextType::Initialize);
        appendModules(VfxContextType::Initialize);
        appendContext(VfxContextType::Update);
        appendModules(VfxContextType::Update);
        appendModules(VfxContextType::Output);
        appendContext(VfxContextType::Output);

        std::vector<const VfxBlackboardParameter*> sortedParameters;
        sortedParameters.reserve(result.Blackboard.size());
        for (const auto& parameter : result.Blackboard)
            sortedParameters.push_back(std::addressof(parameter));
        std::ranges::sort(sortedParameters, {}, [](const VfxBlackboardParameter* parameter) { return parameter->Id; });
        float parameterY = 0.0F;
        for (const auto* parameter : sortedParameters)
        {
            VfxGraphNode node;
            node.Id = AllocateDerivedId(parameter->Id, 0x4000, used);
            node.Type = parameter->Name;
            node.Context = VfxContextType::Update;
            node.EditorPosition = {-360.0F, parameterY};
            node.Kind = VfxGraphNodeKind::Parameter;
            node.Reference = parameter->Id;
            node.Pins.push_back({AllocateDerivedId(parameter->Id, 0x4100, used), parameter->Name, parameter->Type,
                                 false, "value", std::nullopt});
            system.Nodes.push_back(std::move(node));
            parameterY += 150.0F;
        }
        result.Systems.push_back(std::move(system));
        ValidateVfxEffect(result);
        return result;
    }

    VfxEffectAsset::VfxEffectAsset(VfxEffectDefinition definition) : m_Definition(std::move(definition))
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
            {
                result +=
                    node.Type.capacity() + node.CustomHlsl.capacity() + node.Pins.capacity() * sizeof(VfxGraphPin);
                for (const auto& pin : node.Pins)
                    result += pin.Name.capacity() + pin.Semantic.capacity();
            }
        }
        for (const auto& parameter : m_Definition.Blackboard)
            result += parameter.Name.capacity();
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
        return ConvertVfxEffectToGraph(definition);
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
            if (!document.is_object() || schemaVersion < 1U || schemaVersion > 3U)
                throw std::runtime_error("VFX effect asset has an unsupported schema.");

            VfxEffectDefinition definition;
            definition.SchemaVersion = schemaVersion;
            definition.ExecutionSource = schemaVersion < 3
                                             ? VfxExecutionSource::LegacyModules
                                             : ParseExecutionSource(document.at("executionSource").get<std::string>());
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
            if (schemaVersion >= 2)
            {
                definition.Systems = DecodeSystems(document.at("systems"), schemaVersion);
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
        if (published.SchemaVersion < 3)
            published.ExecutionSource = VfxExecutionSource::LegacyModules;
        published.SchemaVersion = 3;
        ValidateVfxEffect(published);
        auto modules = Json::array();
        for (const auto& module : published.Modules)
            modules.push_back(EncodeModule(module));
        const Json document{{"schemaVersion", 3},
                            {"emitterId", IdText(published.EmitterId)},
                            {"name", published.Name},
                            {"loop", published.Loop},
                            {"duration", published.Duration},
                            {"space", SpaceName(published.Space)},
                            {"seed", published.Seed},
                            {"capacity", published.Capacity},
                            {"executionSource", ExecutionSourceName(published.ExecutionSource)},
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
        result.Version = 3;
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
