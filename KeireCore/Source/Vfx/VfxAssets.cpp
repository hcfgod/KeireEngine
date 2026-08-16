#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Vfx/VfxAssetValueCodec.h"
#include "KeireInternal/Vfx/VfxExecutionInternal.h"
#include "KeireInternal/Vfx/VfxExpressionInternal.h"

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

        const auto IdText = Detail::VfxAssetIdText;
        const auto ParseId = Detail::ParseVfxAssetId;
        const auto DerivedGraphId = Detail::DerivedVfxGraphId;
        const auto EncodeVector2 = Detail::EncodeVfxVector2;
        const auto EncodeVector = Detail::EncodeVfxVector3;
        const auto EncodeVector4 = Detail::EncodeVfxVector4;
        const auto EncodeQuaternion = Detail::EncodeVfxQuaternion;
        const auto EncodeMatrix = Detail::EncodeVfxMatrix;
        const auto DecodeVector2 = Detail::DecodeVfxVector2;
        const auto DecodeVector = Detail::DecodeVfxVector3;
        const auto DecodeVector4 = Detail::DecodeVfxVector4;
        const auto DecodeQuaternion = Detail::DecodeVfxQuaternion;
        const auto DecodeMatrix = Detail::DecodeVfxMatrix;
        const auto EncodeColor = Detail::EncodeVfxColor;
        const auto DecodeColor = Detail::DecodeVfxColor;
        const auto CurveInterpolationName = Detail::VfxCurveInterpolationName;
        const auto ParseCurveInterpolation = Detail::ParseVfxCurveInterpolation;
        const auto EncodeCurve = Detail::EncodeVfxCurve;
        const auto DecodeCurve = Detail::DecodeVfxCurve;
        const auto GradientInterpolationName = Detail::VfxGradientInterpolationName;
        const auto ParseGradientInterpolation = Detail::ParseVfxGradientInterpolation;
        const auto EncodeGradient = Detail::EncodeVfxGradient;
        const auto DecodeGradient = Detail::DecodeVfxGradient;

        constexpr std::size_t MaximumDocumentBytes = std::size_t{4} * 1024U * 1024U;
        constexpr std::size_t MaximumModules = 128;
        constexpr std::size_t MaximumSystems = 64;
        constexpr std::size_t MaximumGraphNodes = 4096;
        constexpr std::size_t MaximumGraphConnections = 16'384;
        constexpr std::size_t MaximumBlackboardParameters = 1024;
        constexpr std::size_t MaximumPortableCustomInstructions = 4096;
        constexpr std::size_t MaximumBursts = 32;
        constexpr std::size_t MaximumBurstCycles = 1024;
        constexpr std::size_t MaximumNameBytes = 128;
        constexpr std::uint32_t VfxEffectImporterVersion = 5;
        constexpr float MaximumAuthoredScalar = 1'000'000.0F;

        template <typename... Ts> struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

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

        [[nodiscard]] std::string_view KillShapeModeName(const VfxKillShapeMode value)
        {
            switch (value)
            {
            case VfxKillShapeMode::Solid:
                return "solid";
            case VfxKillShapeMode::Inverted:
                return "inverted";
            }
            throw std::invalid_argument("VFX kill-shape mode is unsupported.");
        }

        [[nodiscard]] VfxKillShapeMode ParseKillShapeMode(const std::string_view value)
        {
            if (value == "solid")
                return VfxKillShapeMode::Solid;
            if (value == "inverted")
                return VfxKillShapeMode::Inverted;
            throw std::runtime_error("VFX kill-shape mode is unsupported.");
        }

        [[nodiscard]] std::string_view RendererTypeName(const VfxRendererType value)
        {
            switch (value)
            {
            case VfxRendererType::Sprite:
                return "sprite";
            case VfxRendererType::Mesh:
                return "mesh";
            case VfxRendererType::Ribbon:
                return "ribbon";
            case VfxRendererType::Volumetric:
                return "volumetric";
            }
            throw std::invalid_argument("VFX renderer type is unsupported.");
        }

        [[nodiscard]] VfxRendererType ParseRendererType(const std::string_view value)
        {
            if (value == "sprite")
                return VfxRendererType::Sprite;
            if (value == "mesh")
                return VfxRendererType::Mesh;
            if (value == "ribbon")
                return VfxRendererType::Ribbon;
            if (value == "volumetric")
                return VfxRendererType::Volumetric;
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
                    [](const VfxKillShapeModule&) -> std::string_view { return "killShape"; },
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
                        result["material"] = IdText(value.Material);
                    },
                    [&result](const VfxKillShapeModule& value)
                    {
                        result["shape"] = ShapeName(value.Shape);
                        result["center"] = EncodeVector(value.Center);
                        result["boxHalfExtent"] = EncodeVector(value.BoxHalfExtent);
                        result["radius"] = value.Radius;
                        result["mode"] = KillShapeModeName(value.Mode);
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
                                                   ParseId(value, "sprite"), ParseId(value, "mesh"),
                                                   value.contains("material") ? ParseId(value, "material") : AssetId{}};
            else if (type == "killShape")
                result.Payload = VfxKillShapeModule{
                    ParseShape(value.value("shape", std::string("sphere"))),
                    DecodeVector(value.value("center", Json::array({0.0F, 0.0F, 0.0F}))),
                    DecodeVector(value.value("boxHalfExtent", Json::array({0.5F, 0.5F, 0.5F}))),
                    value.value("radius", 0.5F), ParseKillShapeMode(value.value("mode", std::string("solid")))};
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

        [[nodiscard]] std::string_view CompatibilityModeName(const VfxCompatibilityMode value)
        {
            switch (value)
            {
            case VfxCompatibilityMode::NativeSchema4:
                return "nativeSchema4";
            case VfxCompatibilityMode::MigratedLegacyModules:
                return "migratedLegacyModules";
            }
            throw std::invalid_argument("VFX compatibility mode is unsupported.");
        }

        [[nodiscard]] VfxCompatibilityMode ParseCompatibilityMode(const std::string_view value)
        {
            if (value == "nativeSchema4")
                return VfxCompatibilityMode::NativeSchema4;
            if (value == "migratedLegacyModules")
                return VfxCompatibilityMode::MigratedLegacyModules;
            throw std::runtime_error("VFX compatibility mode is unsupported.");
        }

        [[nodiscard]] bool UsesStrictSchemaFourCapabilities(const VfxEffectDefinition& definition) noexcept
        {
            return definition.SchemaVersion >= CurrentVfxSchemaVersion &&
                   definition.ExecutionSource == VfxExecutionSource::Graph &&
                   definition.CompatibilityMode == VfxCompatibilityMode::NativeSchema4;
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
            case VfxGraphNodeKind::Operator:
                return "operator";
            case VfxGraphNodeKind::Attribute:
                return "attribute";
            case VfxGraphNodeKind::Subgraph:
                return "subgraph";
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
            if (value == "operator")
                return VfxGraphNodeKind::Operator;
            if (value == "attribute")
                return VfxGraphNodeKind::Attribute;
            if (value == "subgraph")
                return VfxGraphNodeKind::Subgraph;
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
            case VfxValueType::UnsignedInteger:
                return "unsignedInteger";
            case VfxValueType::Vector4:
                return "vector4";
            case VfxValueType::Quaternion:
                return "quaternion";
            case VfxValueType::Matrix:
                return "matrix";
            case VfxValueType::Curve:
                return "curve";
            case VfxValueType::Gradient:
                return "gradient";
            case VfxValueType::ScalarRange:
                return "scalarRange";
            case VfxValueType::IntegerRange:
                return "integerRange";
            case VfxValueType::UnsignedIntegerRange:
                return "unsignedIntegerRange";
            case VfxValueType::Vector2Range:
                return "vector2Range";
            case VfxValueType::Vector3Range:
                return "vector3Range";
            case VfxValueType::Vector4Range:
                return "vector4Range";
            case VfxValueType::ColorRange:
                return "colorRange";
            case VfxValueType::Texture2DArray:
                return "texture2DArray";
            case VfxValueType::Texture3D:
                return "texture3D";
            case VfxValueType::TextureCube:
                return "textureCube";
            case VfxValueType::Buffer:
                return "buffer";
            case VfxValueType::PointCache:
                return "pointCache";
            case VfxValueType::SignedDistanceField:
                return "signedDistanceField";
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
            if (value == "unsignedInteger")
                return VfxValueType::UnsignedInteger;
            if (value == "vector4")
                return VfxValueType::Vector4;
            if (value == "quaternion")
                return VfxValueType::Quaternion;
            if (value == "matrix")
                return VfxValueType::Matrix;
            if (value == "curve")
                return VfxValueType::Curve;
            if (value == "gradient")
                return VfxValueType::Gradient;
            if (value == "scalarRange")
                return VfxValueType::ScalarRange;
            if (value == "integerRange")
                return VfxValueType::IntegerRange;
            if (value == "unsignedIntegerRange")
                return VfxValueType::UnsignedIntegerRange;
            if (value == "vector2Range")
                return VfxValueType::Vector2Range;
            if (value == "vector3Range")
                return VfxValueType::Vector3Range;
            if (value == "vector4Range")
                return VfxValueType::Vector4Range;
            if (value == "colorRange")
                return VfxValueType::ColorRange;
            if (value == "texture2DArray")
                return VfxValueType::Texture2DArray;
            if (value == "texture3D")
                return VfxValueType::Texture3D;
            if (value == "textureCube")
                return VfxValueType::TextureCube;
            if (value == "buffer")
                return VfxValueType::Buffer;
            if (value == "pointCache")
                return VfxValueType::PointCache;
            if (value == "signedDistanceField")
                return VfxValueType::SignedDistanceField;
            throw std::runtime_error("VFX value type is unsupported.");
        }

        template <typename T, typename Encoder>
        [[nodiscard]] Json EncodeRange(const VfxRange<T>& value, Encoder&& encode)
        {
            return {{"minimum", encode(value.Minimum)}, {"maximum", encode(value.Maximum)}};
        }

        template <typename T, typename Decoder>
        [[nodiscard]] VfxRange<T> DecodeRange(const Json& value, Decoder&& decode)
        {
            if (!value.is_object())
                throw std::runtime_error("VFX range values must be objects.");
            return {decode(value.at("minimum")), decode(value.at("maximum"))};
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
                return EncodeVector2(std::get<Vector2>(value));
            case VfxValueType::Vector3:
                return EncodeVector(std::get<Vector3>(value));
            case VfxValueType::Color:
                return EncodeColor(std::get<Color>(value));
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            case VfxValueType::Texture2DArray:
            case VfxValueType::Texture3D:
            case VfxValueType::TextureCube:
            case VfxValueType::Buffer:
            case VfxValueType::PointCache:
            case VfxValueType::SignedDistanceField:
                return IdText(std::get<AssetId>(value));
            case VfxValueType::ParticleStream:
                break;
            case VfxValueType::UnsignedInteger:
                return std::get<std::uint64_t>(value);
            case VfxValueType::Vector4:
                return EncodeVector4(std::get<Vector4>(value));
            case VfxValueType::Quaternion:
                return EncodeQuaternion(std::get<Quaternion>(value));
            case VfxValueType::Matrix:
                return EncodeMatrix(std::get<Matrix4>(value));
            case VfxValueType::Curve:
                return EncodeCurve(std::get<Curve1D>(value));
            case VfxValueType::Gradient:
                return EncodeGradient(std::get<ColorGradient>(value));
            case VfxValueType::ScalarRange:
                return EncodeRange(std::get<VfxScalarRange>(value), [](const float scalar) { return Json(scalar); });
            case VfxValueType::IntegerRange:
                return EncodeRange(std::get<VfxIntegerRange>(value),
                                   [](const std::int64_t integer) { return Json(integer); });
            case VfxValueType::UnsignedIntegerRange:
                return EncodeRange(std::get<VfxUnsignedIntegerRange>(value),
                                   [](const std::uint64_t integer) { return Json(integer); });
            case VfxValueType::Vector2Range:
                return EncodeRange(std::get<VfxVector2Range>(value), EncodeVector2);
            case VfxValueType::Vector3Range:
                return EncodeRange(std::get<VfxVector3Range>(value), EncodeVector);
            case VfxValueType::Vector4Range:
                return EncodeRange(std::get<VfxVector4Range>(value), EncodeVector4);
            case VfxValueType::ColorRange:
                return EncodeRange(std::get<VfxColorRange>(value), EncodeColor);
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
                return DecodeVector2(value);
            case VfxValueType::Vector3:
                return DecodeVector(value);
            case VfxValueType::Color:
                return DecodeColor(value);
            case VfxValueType::Texture:
            case VfxValueType::Mesh:
            case VfxValueType::Asset:
            case VfxValueType::Texture2DArray:
            case VfxValueType::Texture3D:
            case VfxValueType::TextureCube:
            case VfxValueType::Buffer:
            case VfxValueType::PointCache:
            case VfxValueType::SignedDistanceField:
            {
                const auto text = value.get<std::string>();
                return text.empty() ? AssetId{} : AssetId::Parse(text);
            }
            case VfxValueType::ParticleStream:
                break;
            case VfxValueType::UnsignedInteger:
                return value.get<std::uint64_t>();
            case VfxValueType::Vector4:
                return DecodeVector4(value);
            case VfxValueType::Quaternion:
                return DecodeQuaternion(value);
            case VfxValueType::Matrix:
                return DecodeMatrix(value);
            case VfxValueType::Curve:
                return DecodeCurve(value);
            case VfxValueType::Gradient:
                return DecodeGradient(value);
            case VfxValueType::ScalarRange:
                return DecodeRange<float>(value, [](const Json& scalar) { return scalar.get<float>(); });
            case VfxValueType::IntegerRange:
                return DecodeRange<std::int64_t>(value,
                                                 [](const Json& integer) { return integer.get<std::int64_t>(); });
            case VfxValueType::UnsignedIntegerRange:
                return DecodeRange<std::uint64_t>(value,
                                                  [](const Json& integer) { return integer.get<std::uint64_t>(); });
            case VfxValueType::Vector2Range:
                return DecodeRange<Vector2>(value, DecodeVector2);
            case VfxValueType::Vector3Range:
                return DecodeRange<Vector3>(value, DecodeVector);
            case VfxValueType::Vector4Range:
                return DecodeRange<Vector4>(value, DecodeVector4);
            case VfxValueType::ColorRange:
                return DecodeRange<Color>(value, DecodeColor);
            }
            throw std::runtime_error("VFX typed default value is malformed.");
        }

        [[nodiscard]] Json EncodeGraphProperty(const VfxGraphProperty& property)
        {
            Json result{{"name", property.Name}};
            std::visit(
                Overloaded{
                    [&result](const bool value)
                    {
                        result["type"] = "boolean";
                        result["value"] = value;
                    },
                    [&result](const std::int64_t value)
                    {
                        result["type"] = "integer";
                        result["value"] = value;
                    },
                    [&result](const std::uint64_t value)
                    {
                        result["type"] = "unsignedInteger";
                        result["value"] = value;
                    },
                    [&result](const float value)
                    {
                        result["type"] = "scalar";
                        result["value"] = value;
                    },
                    [&result](const std::string& value)
                    {
                        result["type"] = "string";
                        result["value"] = value;
                    },
                    [&result](const Vector2 value)
                    {
                        result["type"] = "vector2";
                        result["value"] = EncodeVector2(value);
                    },
                    [&result](const Vector3 value)
                    {
                        result["type"] = "vector3";
                        result["value"] = EncodeVector(value);
                    },
                    [&result](const Vector4 value)
                    {
                        result["type"] = "vector4";
                        result["value"] = EncodeVector4(value);
                    },
                    [&result](const Quaternion value)
                    {
                        result["type"] = "quaternion";
                        result["value"] = EncodeQuaternion(value);
                    },
                    [&result](const Color value)
                    {
                        result["type"] = "color";
                        result["value"] = EncodeColor(value);
                    },
                    [&result](const Matrix4& value)
                    {
                        result["type"] = "matrix";
                        result["value"] = EncodeMatrix(value);
                    },
                    [&result](const AssetId value)
                    {
                        result["type"] = "asset";
                        result["value"] = IdText(value);
                    },
                },
                property.Value);
            return result;
        }

        [[nodiscard]] VfxGraphProperty DecodeGraphProperty(const Json& value)
        {
            VfxGraphProperty result;
            result.Name = value.at("name").get<std::string>();
            const auto type = value.at("type").get<std::string>();
            const auto& encoded = value.at("value");
            if (type == "boolean")
                result.Value = encoded.get<bool>();
            else if (type == "integer")
                result.Value = encoded.get<std::int64_t>();
            else if (type == "unsignedInteger")
                result.Value = encoded.get<std::uint64_t>();
            else if (type == "scalar")
                result.Value = encoded.get<float>();
            else if (type == "string")
                result.Value = encoded.get<std::string>();
            else if (type == "vector2")
                result.Value = DecodeVector2(encoded);
            else if (type == "vector3")
                result.Value = DecodeVector(encoded);
            else if (type == "vector4")
                result.Value = DecodeVector4(encoded);
            else if (type == "quaternion")
                result.Value = DecodeQuaternion(encoded);
            else if (type == "color")
                result.Value = DecodeColor(encoded);
            else if (type == "matrix")
                result.Value = DecodeMatrix(encoded);
            else if (type == "asset")
            {
                const auto text = encoded.get<std::string>();
                result.Value = text.empty() ? AssetId{} : AssetId::Parse(text);
            }
            else
                throw std::runtime_error("VFX graph property type is unsupported.");
            return result;
        }

        [[nodiscard]] Json EncodeGraphProperties(const std::span<const VfxGraphProperty> properties)
        {
            auto result = Json::array();
            for (const auto& property : properties)
                result.push_back(EncodeGraphProperty(property));
            return result;
        }

        [[nodiscard]] std::vector<VfxGraphProperty> DecodeGraphProperties(const Json& value)
        {
            if (!value.is_array())
                throw std::runtime_error("VFX graph properties must be an array.");
            std::vector<VfxGraphProperty> result;
            result.reserve(value.size());
            for (const auto& property : value)
                result.push_back(DecodeGraphProperty(property));
            return result;
        }

        [[nodiscard]] Json EncodePins(const std::span<const VfxGraphPin> pins)
        {
            auto result = Json::array();
            for (const auto& pin : pins)
            {
                Json encoded{{"id", IdText(pin.Id)}, {"name", pin.Name},         {"type", ValueTypeName(pin.Type)},
                             {"input", pin.Input},   {"semantic", pin.Semantic}, {"default", nullptr}};
                if (pin.DefaultValue)
                    encoded["default"] = EncodeTypedValue(pin.Type, *pin.DefaultValue);
                result.push_back(std::move(encoded));
            }
            return result;
        }

        [[nodiscard]] std::vector<VfxGraphPin> DecodePins(const Json& value, const std::uint32_t schemaVersion)
        {
            if (!value.is_array())
                throw std::runtime_error("VFX graph pins must be an array.");
            std::vector<VfxGraphPin> result;
            result.reserve(value.size());
            for (const auto& encoded : value)
            {
                VfxGraphPin pin{ParseId(encoded, "id"), encoded.at("name").get<std::string>(),
                                ParseValueType(encoded.at("type").get<std::string>()), encoded.value("input", true)};
                if (schemaVersion >= 3)
                {
                    pin.Semantic = encoded.value("semantic", std::string{});
                    if (encoded.contains("default") && !encoded.at("default").is_null())
                        pin.DefaultValue = DecodeParameterValue(pin.Type, encoded.at("default"));
                }
                result.push_back(std::move(pin));
            }
            return result;
        }

        [[nodiscard]] Json EncodeBlock(const VfxGraphBlock& block)
        {
            return {{"id", IdText(block.Id)},
                    {"typeId", block.TypeId.Value},
                    {"type", block.Type},
                    {"enabled", block.Enabled},
                    {"pins", EncodePins(block.Pins)},
                    {"properties", EncodeGraphProperties(block.Properties)},
                    {"definitionVersion", block.DefinitionVersion},
                    {"reference", IdText(block.Reference)}};
        }

        [[nodiscard]] VfxGraphBlock DecodeBlock(const Json& value, const std::uint32_t schemaVersion)
        {
            VfxGraphBlock result;
            result.Id = ParseId(value, "id");
            result.TypeId.Value = value.at("typeId").get<std::string>();
            result.Type = value.at("type").get<std::string>();
            result.Enabled = value.value("enabled", true);
            result.Pins = DecodePins(value.at("pins"), schemaVersion);
            result.Properties = DecodeGraphProperties(value.at("properties"));
            result.DefinitionVersion = value.at("definitionVersion").get<std::uint32_t>();
            result.Reference = ParseId(value, "reference");
            return result;
        }

        [[nodiscard]] Json EncodeSystems(const std::span<const VfxGraphSystem> systems)
        {
            auto encodedSystems = Json::array();
            for (const auto& system : systems)
            {
                auto nodes = Json::array();
                for (const auto& node : system.Nodes)
                {
                    auto signature = Json::array();
                    for (const auto type : node.ResolvedSignature)
                        signature.push_back(ValueTypeName(type));
                    auto dynamicPinOrder = Json::array();
                    for (const auto pin : node.DynamicPinOrder)
                        dynamicPinOrder.push_back(IdText(pin));
                    auto blocks = Json::array();
                    for (const auto& block : node.Blocks)
                        blocks.push_back(EncodeBlock(block));
                    nodes.push_back({{"id", IdText(node.Id)},
                                     {"typeId", node.TypeId.Value},
                                     {"type", node.Type},
                                     {"context", ContextName(node.Context)},
                                     {"position", Json::array({node.EditorPosition.X, node.EditorPosition.Y})},
                                     {"pins", EncodePins(node.Pins)},
                                     {"customHlsl", node.CustomHlsl},
                                     {"kind", NodeKindName(node.Kind)},
                                     {"reference", IdText(node.Reference)},
                                     {"definitionVersion", node.DefinitionVersion},
                                     {"properties", EncodeGraphProperties(node.Properties)},
                                     {"resolvedSignature", std::move(signature)},
                                     {"dynamicPinOrder", std::move(dynamicPinOrder)},
                                     {"blocks", std::move(blocks)}});
                }
                auto connections = Json::array();
                for (const auto& connection : system.Connections)
                    connections.push_back({{"id", IdText(connection.Id)},
                                           {"outputNode", IdText(connection.OutputNode)},
                                           {"outputBlock", IdText(connection.OutputBlock)},
                                           {"outputPin", IdText(connection.OutputPin)},
                                           {"inputNode", IdText(connection.InputNode)},
                                           {"inputBlock", IdText(connection.InputBlock)},
                                           {"inputPin", IdText(connection.InputPin)}});
                encodedSystems.push_back(
                    {{"id", IdText(system.Id)},
                     {"name", system.Name},
                     {"dataType",
                      system.DataType == VfxParticleDataType::ParticleStrip ? "particle-strip" : "particle"},
                     {"particlesPerStrip", system.ParticlesPerStrip},
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
                const auto dataType = encodedSystem.value("dataType", std::string("particle"));
                if (dataType == "particle")
                    system.DataType = VfxParticleDataType::Particle;
                else if (dataType == "particle-strip")
                    system.DataType = VfxParticleDataType::ParticleStrip;
                else
                    throw std::runtime_error("VFX graph system data type is unsupported.");
                system.ParticlesPerStrip = encodedSystem.value("particlesPerStrip", 32U);
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
                    node.Pins = DecodePins(encodedNode.at("pins"), schemaVersion);
                    if (schemaVersion >= 4)
                    {
                        node.TypeId.Value = encodedNode.at("typeId").get<std::string>();
                        node.DefinitionVersion = encodedNode.at("definitionVersion").get<std::uint32_t>();
                        node.Properties = DecodeGraphProperties(encodedNode.at("properties"));
                        const auto& signature = encodedNode.at("resolvedSignature");
                        if (!signature.is_array())
                            throw std::runtime_error("VFX resolved signatures must be arrays.");
                        for (const auto& type : signature)
                            node.ResolvedSignature.push_back(ParseValueType(type.get<std::string>()));
                        const auto& dynamicPinOrder = encodedNode.at("dynamicPinOrder");
                        if (!dynamicPinOrder.is_array())
                            throw std::runtime_error("VFX dynamic pin order must be an array.");
                        for (const auto& pin : dynamicPinOrder)
                        {
                            const auto text = pin.get<std::string>();
                            node.DynamicPinOrder.push_back(text.empty() ? AssetId{} : AssetId::Parse(text));
                        }
                        const auto& blocks = encodedNode.at("blocks");
                        if (!blocks.is_array())
                            throw std::runtime_error("VFX context blocks must be an array.");
                        for (const auto& block : blocks)
                            node.Blocks.push_back(DecodeBlock(block, schemaVersion));
                    }
                    system.Nodes.push_back(std::move(node));
                }
                for (const auto& encodedConnection : encodedSystem.at("connections"))
                {
                    VfxGraphConnection connection{
                        ParseId(encodedConnection, "id"), ParseId(encodedConnection, "outputNode"),
                        ParseId(encodedConnection, "outputPin"), ParseId(encodedConnection, "inputNode"),
                        ParseId(encodedConnection, "inputPin")};
                    if (schemaVersion >= 4)
                    {
                        if (!encodedConnection.contains("outputBlock") || !encodedConnection.contains("inputBlock"))
                            throw std::runtime_error("VFX schema-four connection block endpoints are required.");
                        connection.OutputBlock = ParseId(encodedConnection, "outputBlock");
                        connection.InputBlock = ParseId(encodedConnection, "inputBlock");
                    }
                    system.Connections.push_back(connection);
                }
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
            case VfxValueType::Texture2DArray:
            case VfxValueType::Texture3D:
            case VfxValueType::TextureCube:
            case VfxValueType::Buffer:
            case VfxValueType::PointCache:
            case VfxValueType::SignedDistanceField:
                return std::holds_alternative<AssetId>(value);
            case VfxValueType::ParticleStream:
                return false;
            case VfxValueType::UnsignedInteger:
                return std::holds_alternative<std::uint64_t>(value);
            case VfxValueType::Vector4:
                return std::holds_alternative<Vector4>(value) && Math::IsFinite(std::get<Vector4>(value));
            case VfxValueType::Quaternion:
                return std::holds_alternative<Quaternion>(value) && Math::IsFinite(std::get<Quaternion>(value));
            case VfxValueType::Matrix:
                return std::holds_alternative<Matrix4>(value) && Math::IsFinite(std::get<Matrix4>(value));
            case VfxValueType::Curve:
                return std::holds_alternative<Curve1D>(value);
            case VfxValueType::Gradient:
                return std::holds_alternative<ColorGradient>(value);
            case VfxValueType::ScalarRange:
                if (const auto* range = std::get_if<VfxScalarRange>(&value))
                    return FiniteRange(range->Minimum, range->Maximum);
                return false;
            case VfxValueType::IntegerRange:
                if (const auto* range = std::get_if<VfxIntegerRange>(&value))
                    return range->Minimum <= range->Maximum;
                return false;
            case VfxValueType::UnsignedIntegerRange:
                if (const auto* range = std::get_if<VfxUnsignedIntegerRange>(&value))
                    return range->Minimum <= range->Maximum;
                return false;
            case VfxValueType::Vector2Range:
                if (const auto* range = std::get_if<VfxVector2Range>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.X <= range->Maximum.X && range->Minimum.Y <= range->Maximum.Y;
                }
                return false;
            case VfxValueType::Vector3Range:
                if (const auto* range = std::get_if<VfxVector3Range>(&value))
                    return OrderedRange(range->Minimum, range->Maximum);
                return false;
            case VfxValueType::Vector4Range:
                if (const auto* range = std::get_if<VfxVector4Range>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.X <= range->Maximum.X && range->Minimum.Y <= range->Maximum.Y &&
                           range->Minimum.Z <= range->Maximum.Z && range->Minimum.W <= range->Maximum.W;
                }
                return false;
            case VfxValueType::ColorRange:
                if (const auto* range = std::get_if<VfxColorRange>(&value))
                {
                    return Math::IsFinite(range->Minimum) && Math::IsFinite(range->Maximum) &&
                           range->Minimum.Red <= range->Maximum.Red && range->Minimum.Green <= range->Maximum.Green &&
                           range->Minimum.Blue <= range->Maximum.Blue && range->Minimum.Alpha <= range->Maximum.Alpha;
                }
                return false;
            }
            return false;
        }

        [[nodiscard]] bool IsPersistableValueType(const VfxValueType type) noexcept
        {
            return type <= VfxValueType::SignedDistanceField && type != VfxValueType::ParticleStream;
        }

        [[nodiscard]] bool IsPortableCustomValueType(const VfxValueType type) noexcept
        {
            return type == VfxValueType::Scalar || type == VfxValueType::Vector2 || type == VfxValueType::Vector3 ||
                   type == VfxValueType::Vector4 || type == VfxValueType::Color;
        }

        [[nodiscard]] bool ValidGraphPropertyValue(const VfxGraphPropertyValue& value,
                                                   const std::size_t maximumStringBytes) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const bool) { return true; },
                    [](const std::int64_t) { return true; },
                    [](const std::uint64_t) { return true; },
                    [](const float scalar) { return std::isfinite(scalar); },
                    [maximumStringBytes](const std::string& text) { return text.size() <= maximumStringBytes; },
                    [](const Vector2 vector) { return Math::IsFinite(vector); },
                    [](const Vector3 vector) { return Math::IsFinite(vector); },
                    [](const Vector4 vector) { return Math::IsFinite(vector); },
                    [](const Quaternion quaternion) { return Math::IsFinite(quaternion); },
                    [](const Color color) { return Math::IsFinite(color); },
                    [](const Matrix4& matrix) { return Math::IsFinite(matrix); },
                    [](const AssetId) { return true; },
                },
                value);
        }

        [[nodiscard]] bool ValidGraphProperties(const std::span<const VfxGraphProperty> properties,
                                                const std::size_t maximumStringBytes = MaximumNameBytes)
        {
            std::set<std::string> names;
            return std::ranges::all_of(properties,
                                       [&names, maximumStringBytes](const VfxGraphProperty& property)
                                       {
                                           return !property.Name.empty() && property.Name.size() <= MaximumNameBytes &&
                                                  names.insert(property.Name).second &&
                                                  ValidGraphPropertyValue(property.Value, maximumStringBytes);
                                       });
        }

        [[nodiscard]] bool ValidTypeId(const VfxNodeTypeId& typeId) noexcept
        {
            if (typeId.Empty() || typeId.Value.size() > MaximumNameBytes || typeId.Value.front() == '.' ||
                typeId.Value.back() == '.')
            {
                return false;
            }
            return std::ranges::all_of(typeId.Value,
                                       [](const char character)
                                       {
                                           return (character >= 'a' && character <= 'z') ||
                                                  (character >= '0' && character <= '9') || character == '.' ||
                                                  character == '-';
                                       });
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
                    [](const VfxKillShapeModule&) { return VfxContextType::Update; },
                    [](const VfxRendererModule&) { return VfxContextType::Output; },
                },
                payload);
        }

        [[nodiscard]] std::string_view ContextTypeId(const VfxContextType context)
        {
            switch (context)
            {
            case VfxContextType::Spawn:
                return "keire.context.spawn";
            case VfxContextType::Initialize:
                return "keire.context.initialize";
            case VfxContextType::Update:
                return "keire.context.update";
            case VfxContextType::Output:
                return "keire.context.output";
            case VfxContextType::Event:
                return "keire.context.event";
            }
            throw std::invalid_argument("VFX context type is unsupported.");
        }

        [[nodiscard]] std::string_view ModuleTypeId(const VfxModulePayload& payload)
        {
            return std::visit(
                Overloaded{
                    [](const VfxEmissionRateModule&) -> std::string_view { return "keire.block.emission-rate"; },
                    [](const VfxBurstModule&) -> std::string_view { return "keire.block.burst"; },
                    [](const VfxShapeModule&) -> std::string_view { return "keire.block.shape"; },
                    [](const VfxInitializeModule&) -> std::string_view { return "keire.block.initialize"; },
                    [](const VfxForceModule&) -> std::string_view { return "keire.block.force"; },
                    [](const VfxSizeOverLifetimeModule&) -> std::string_view
                    { return "keire.block.size-over-lifetime"; },
                    [](const VfxColorOverLifetimeModule&) -> std::string_view
                    { return "keire.block.color-over-lifetime"; },
                    [](const VfxCollisionModule&) -> std::string_view { return "keire.block.collision"; },
                    [](const VfxKillShapeModule&) -> std::string_view { return "keire.block.kill-shape"; },
                    [](const VfxRendererModule&) -> std::string_view { return "keire.output.renderer"; },
                },
                payload);
        }

        [[nodiscard]] std::string TypeSlug(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            bool separator = false;
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0)
                {
                    if (separator && !result.empty())
                        result.push_back('-');
                    result.push_back(static_cast<char>(std::tolower(byte)));
                    separator = false;
                }
                else
                    separator = true;
            }
            return result;
        }

        [[nodiscard]] VfxNodeTypeId MigratedNodeTypeId(const VfxEffectDefinition& definition, const VfxGraphNode& node)
        {
            switch (node.Kind)
            {
            case VfxGraphNodeKind::Context:
                return {std::string(ContextTypeId(node.Context))};
            case VfxGraphNodeKind::Module:
            {
                const auto module = std::ranges::find(definition.Modules, node.Reference, &VfxModuleDefinition::Id);
                if (module != definition.Modules.end())
                    return {std::string(ModuleTypeId(module->Payload))};
                const auto slug = TypeSlug(node.Type);
                return {slug == "renderer" ? "keire.output.renderer" : "keire.block." + slug};
            }
            case VfxGraphNodeKind::Parameter:
                return {"keire.parameter"};
            case VfxGraphNodeKind::CustomHlsl:
                return {"keire.operator.portable-hlsl"};
            case VfxGraphNodeKind::Operator:
                return {"keire.operator." + TypeSlug(node.Type)};
            case VfxGraphNodeKind::Attribute:
                return {"keire.attribute." + TypeSlug(node.Type)};
            case VfxGraphNodeKind::Subgraph:
                return {"keire.subgraph." + TypeSlug(node.Type)};
            }
            throw std::invalid_argument("VFX graph node kind is unsupported.");
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
                    [](const VfxKillShapeModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Center", "center", VfxValueType::Vector3, VfxModuleProperty::KillShapeCenter,
                             value.Center},
                            {"Box Half Extent", "boxHalfExtent", VfxValueType::Vector3,
                             VfxModuleProperty::KillShapeBoxHalfExtent, value.BoxHalfExtent},
                            {"Radius", "radius", VfxValueType::Scalar, VfxModuleProperty::KillShapeRadius,
                             value.Radius},
                            {"Inverted", "inverted", VfxValueType::Boolean, VfxModuleProperty::KillShapeInverted,
                             value.Mode == VfxKillShapeMode::Inverted}};
                    },
                    [](const VfxRendererModule& value)
                    {
                        return std::vector<ModulePinSpecification>{
                            {"Sprite", "sprite", VfxValueType::Texture, VfxModuleProperty::RendererSprite,
                             value.Sprite},
                            {"Mesh", "mesh", VfxValueType::Mesh, VfxModuleProperty::RendererMesh, value.Mesh},
                            {"Material", "material", VfxValueType::Asset, VfxModuleProperty::RendererMaterial,
                             value.Material}};
                    },
                },
                payload);
        }

        [[nodiscard]] std::uint32_t ModuleDefinitionVersion(const VfxModulePayload& payload) noexcept
        {
            return std::visit(
                Overloaded{
                    [](const VfxShapeModule&) { return 2U; },
                    [](const VfxRendererModule&) { return 2U; },
                    [](const auto&) { return 1U; },
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
                return 0;
            }
            throw std::invalid_argument("VFX context is invalid.");
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
            std::uint32_t ValueRegister = ~std::uint32_t{0};
            std::optional<VfxParameterValue> DefaultValue;
        };

        class VfxNodeCompileError final : public std::invalid_argument
        {
          public:
            VfxNodeCompileError(const AssetId node, const std::string& message)
                : std::invalid_argument(message), m_Node(node)
            {
            }

            [[nodiscard]] AssetId Node() const noexcept { return m_Node; }

          private:
            AssetId m_Node;
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
                if (result.size() >= MaximumPortableCustomInstructions)
                {
                    throw VfxNodeCompileError(
                        node.Id, "Portable Custom HLSL exceeds the 4096-instruction compiler safety limit.");
                }

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
                    instruction.ValueRegister = input->second.ValueRegister;
                    if (instruction.ParameterSlot != ~std::uint32_t{0} &&
                        instruction.ValueRegister != ~std::uint32_t{0})
                    {
                        throw std::logic_error("Portable Custom HLSL input has multiple compiled value sources.");
                    }
                    if (instruction.ParameterSlot == ~std::uint32_t{0} &&
                        instruction.ValueRegister == ~std::uint32_t{0})
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
            AssetId System;
            VfxParticleDataType DataType = VfxParticleDataType::Particle;
            std::uint32_t ParticlesPerStrip = 1;
            std::string EventName;
            std::vector<VfxCompiledParameter> Parameters;
            std::vector<VfxCompiledModule> Modules;
            std::vector<VfxCompiledBinding> Bindings;
            std::vector<VfxCompiledValueInstruction> ValueInstructions;
            std::uint32_t ValueRegisterCount = 0;
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
            const VfxGraphBlock* Block = nullptr;
            const VfxGraphPin* Pin = nullptr;
        };

        [[nodiscard]] std::size_t CountFlowPins(const VfxGraphNode& node, const bool input) noexcept
        {
            return static_cast<std::size_t>(
                std::ranges::count_if(node.Pins, [input](const VfxGraphPin& pin)
                                      { return pin.Input == input && pin.Type == VfxValueType::ParticleStream; }));
        }
        void ValidateContextNode(const VfxGraphNode& node)
        {
            const auto expectedInputs =
                node.Context == VfxContextType::Spawn || node.Context == VfxContextType::Event ? 0U : 1U;
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
                node.Pins.size() != specifications.size() + 2 ||
                node.DefinitionVersion != ModuleDefinitionVersion(module.Payload))
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

        void ValidateBlock(const VfxGraphNode& context, const VfxGraphBlock& block, const VfxModuleDefinition& module)
        {
            const auto specifications = ModulePinSpecifications(module.Payload);
            if (context.Kind != VfxGraphNodeKind::Context || !block.Reference ||
                context.Context != ModuleContext(module.Payload) ||
                block.TypeId.View() != ModuleTypeId(module.Payload) ||
                block.DefinitionVersion != ModuleDefinitionVersion(module.Payload) ||
                block.Type != ModuleTypeName(module.Payload) || block.Pins.size() != specifications.size())
            {
                throw std::invalid_argument("VFX Block does not match its referenced Runtime Module payload.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                if (!pin.Input || pin.Type == VfxValueType::ParticleStream || pin.DefaultValue == std::nullopt ||
                    !semantics.insert(pin.Semantic).second)
                {
                    throw std::invalid_argument("VFX Block contains a malformed data-input pin.");
                }
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (specification == specifications.end() || specification->Type != pin.Type ||
                    !ValueMatchesType(pin.Type, *pin.DefaultValue))
                {
                    throw std::invalid_argument("VFX Block contains an unknown or type-mismatched input pin.");
                }
            }
            for (const auto& specification : specifications)
            {
                const auto pin = std::ranges::find(block.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin == block.Pins.end() || pin->Type != specification.Type)
                    throw std::invalid_argument("VFX Block is missing a canonical property input.");
            }
        }

        [[nodiscard]] const std::string& PortableBlockSource(const VfxGraphBlock& block)
        {
            if (block.Properties.size() != 1 || block.Properties.front().Name != "Source" ||
                !std::holds_alternative<std::string>(block.Properties.front().Value))
            {
                throw std::invalid_argument("Portable Custom HLSL Block source is missing or malformed.");
            }
            return std::get<std::string>(block.Properties.front().Value);
        }

        void ValidatePortableBlock(const VfxGraphNode& context, const VfxGraphBlock& block)
        {
            const auto& source = PortableBlockSource(block);
            if (context.Kind != VfxGraphNodeKind::Context || block.Reference ||
                block.TypeId.View() != "keire.block.portable-hlsl" || block.Type != "Portable Custom HLSL" ||
                block.DefinitionVersion != 1 || source.empty() || source.size() > MaximumDocumentBytes)
            {
                throw std::invalid_argument("Portable Custom HLSL Block is not canonical.");
            }
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                if (!pin.Input || pin.Type == VfxValueType::ParticleStream || !IsIdentifier(pin.Semantic) ||
                    !semantics.insert(pin.Semantic).second || !IsPortableCustomValueType(pin.Type) ||
                    (pin.DefaultValue && !ValueMatchesType(pin.Type, *pin.DefaultValue)))
                {
                    throw std::invalid_argument("Portable Custom HLSL Block contains an invalid typed input pin.");
                }
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

        void ValidateOperatorNode(const VfxGraphNode& node, const VfxNodeDescriptor& descriptor,
                                  const VfxParticleDataType dataType)
        {
            if (node.Reference || !node.CustomHlsl.empty() || node.Type != descriptor.Label ||
                node.DefinitionVersion != descriptor.DefinitionVersion || node.Pins.size() != descriptor.Pins.size() ||
                node.Properties.size() != descriptor.Settings.size() ||
                std::ranges::find(descriptor.ValidContexts, node.Context) == descriptor.ValidContexts.end())
            {
                throw std::invalid_argument("VFX Operator node does not match its catalog descriptor.");
            }

            std::vector<VfxValueType> resolvedSignature;
            std::vector<AssetId> dynamicPinOrder;
            resolvedSignature.reserve(node.Pins.size());
            for (std::size_t index = 0; index < node.Pins.size(); ++index)
            {
                const auto& pin = node.Pins[index];
                const auto& expected = descriptor.Pins[index];
                if (pin.Name != expected.Name || pin.Semantic != expected.Semantic || pin.Type != expected.Type ||
                    pin.Input != expected.Input ||
                    (pin.Input && (!pin.DefaultValue || !ValueMatchesType(pin.Type, *pin.DefaultValue))) ||
                    (!pin.Input && pin.DefaultValue))
                {
                    throw std::invalid_argument("VFX Operator node pin signature is not canonical.");
                }
                resolvedSignature.push_back(pin.Type);
                if (descriptor.TypeBehavior == VfxNodeTypeBehavior::Cascaded && pin.Input)
                    dynamicPinOrder.push_back(pin.Id);
            }
            if (node.ResolvedSignature != resolvedSignature || node.DynamicPinOrder != dynamicPinOrder)
                throw std::invalid_argument("VFX Operator resolved signature is not canonical.");

            for (std::size_t index = 0; index < node.Properties.size(); ++index)
            {
                const auto& property = node.Properties[index];
                const auto& expected = descriptor.Settings[index];
                if (property.Name != expected.Name || property.Value.index() != expected.DefaultValue.index())
                    throw std::invalid_argument("VFX Operator settings do not match the catalog descriptor.");
                if (property.Name == "Scope")
                {
                    const auto scope = std::get<std::uint64_t>(property.Value);
                    if (scope > static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip))
                        throw std::invalid_argument("VFX Random scope is unsupported.");
                    if (scope == static_cast<std::uint64_t>(VfxRandomScope::PerParticleStrip) &&
                        dataType != VfxParticleDataType::ParticleStrip)
                    {
                        throw std::invalid_argument(
                            "VFX Random Per Particle Strip scope requires a Particle Strip system.");
                    }
                }
                if (property.Name == "Condition")
                {
                    static constexpr std::array<std::string_view, 6> conditions{
                        "Less", "Less Or Equal", "Equal", "Not Equal", "Greater Or Equal", "Greater"};
                    const auto& condition = std::get<std::string>(property.Value);
                    if (std::ranges::find(conditions, condition) == conditions.end())
                        throw std::invalid_argument("VFX Compare operator condition is unsupported.");
                }
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
                    !IsPortableCustomValueType(pin.Type) ||
                    (pin.DefaultValue && !ValueMatchesType(pin.Type, *pin.DefaultValue)))
                {
                    throw std::invalid_argument("Portable Custom HLSL contains an invalid typed input pin.");
                }
            }
        }

        [[nodiscard]] LoweredPlan LowerGraph(const VfxEffectDefinition& definition, const VfxGraphSystem& system,
                                             const bool requirePublishable)
        {
            std::map<AssetId, std::pair<const VfxModuleDefinition*, std::uint32_t>> modules;
            for (std::uint32_t index = 0; index < definition.Modules.size(); ++index)
                modules.emplace(definition.Modules[index].Id,
                                std::pair{std::addressof(definition.Modules[index]), index});
            std::map<AssetId, const VfxBlackboardParameter*> parameters;
            for (const auto& parameter : definition.Blackboard)
                parameters.emplace(parameter.Id, std::addressof(parameter));

            LoweredPlan result;
            result.System = system.Id;
            result.DataType = system.DataType;
            result.ParticlesPerStrip =
                system.DataType == VfxParticleDataType::ParticleStrip ? system.ParticlesPerStrip : 1U;
            result.Parameters = CompileParameters(definition.Blackboard);
            std::map<AssetId, std::uint32_t> parameterSlots;
            for (const auto& parameter : result.Parameters)
                parameterSlots.emplace(parameter.Parameter, parameter.Slot);

            std::map<AssetId, const VfxGraphNode*> nodes;
            std::map<AssetId, LocatedPin> pins;
            std::array<const VfxGraphNode*, 4> contexts{};
            bool eventDriven = false;
            for (const auto& node : system.Nodes)
            {
                nodes.emplace(node.Id, std::addressof(node));
                for (const auto& pin : node.Pins)
                    pins.emplace(pin.Id, LocatedPin{std::addressof(node), nullptr, std::addressof(pin)});
                switch (node.Kind)
                {
                case VfxGraphNodeKind::Context:
                {
                    ValidateContextNode(node);
                    const auto index = ContextOrder(node.Context);
                    if (contexts[index])
                        throw std::invalid_argument(
                            "Executable VFX graphs require one Spawn or Event source and one context per later stage.");
                    contexts[index] = std::addressof(node);
                    if (node.Context == VfxContextType::Event)
                    {
                        if (!node.Blocks.empty())
                            throw std::invalid_argument("VFX Event contexts cannot contain particle Blocks.");
                        eventDriven = true;
                        result.EventName = node.Type;
                        if (result.EventName.empty())
                            throw std::invalid_argument("VFX Event contexts require a non-empty event name.");
                    }
                    for (const auto& block : node.Blocks)
                    {
                        if (block.TypeId.View() == "keire.block.portable-hlsl")
                        {
                            ValidatePortableBlock(node, block);
                        }
                        else
                        {
                            const auto module = modules.find(block.Reference);
                            if (module == modules.end())
                                throw std::invalid_argument("VFX Block has an unknown Runtime Module reference.");
                            ValidateBlock(node, block, *module->second.first);
                        }
                        for (const auto& pin : block.Pins)
                            pins.emplace(pin.Id,
                                         LocatedPin{std::addressof(node), std::addressof(block), std::addressof(pin)});
                    }
                    break;
                }
                case VfxGraphNodeKind::Module:
                {
                    const auto module = modules.find(node.Reference);
                    if (module == modules.end())
                        throw std::invalid_argument("VFX module node has an unknown Runtime Module reference.");
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
                case VfxGraphNodeKind::Operator:
                {
                    const auto* descriptor = FindVfxNodeDescriptor(node.TypeId.View());
                    if (!descriptor || descriptor->Class != VfxNodeClass::Operator || !descriptor->Lowering)
                        throw std::invalid_argument("VFX graph contains an unknown executable Operator type ID.");
                    if (descriptor->SupportTier == VfxNodeSupportTier::Disabled)
                        throw std::invalid_argument("VFX Operator is disabled: " + descriptor->DisabledReason);
                    ValidateOperatorNode(node, *descriptor, system.DataType);
                    break;
                }
                case VfxGraphNodeKind::Attribute:
                    throw std::invalid_argument("VFX Attribute nodes are not executable in this production tier.");
                case VfxGraphNodeKind::Subgraph:
                    throw std::invalid_argument("VFX Subgraph nodes are not executable in this production tier.");
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
                    input->second.Node->Id != connection.InputNode ||
                    (output->second.Block ? output->second.Block->Id : AssetId{}) != connection.OutputBlock ||
                    (input->second.Block ? input->second.Block->Id : AssetId{}) != connection.InputBlock)
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
                else if ((output->second.Node->Kind == VfxGraphNodeKind::Operator ||
                          input->second.Node->Kind == VfxGraphNodeKind::Operator) &&
                         output->second.Node->Kind != VfxGraphNodeKind::Parameter &&
                         output->second.Node->Context != input->second.Node->Context)
                {
                    throw std::invalid_argument(
                        "VFX Operator cables must remain in one evaluation context in this production tier.");
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
            if (requirePublishable)
            {
                for (const auto* context : contexts)
                {
                    if (!fromSpawn.contains(context->Id) || !toOutput.contains(context->Id))
                    {
                        throw std::invalid_argument("VFX contexts must share one connected particle-stream path.");
                    }
                }
            }

            std::vector<AssetId> requiredExpressionOutputs;
            for (const auto& connection : system.Connections)
            {
                const auto& outputNode = *nodes.at(connection.OutputNode);
                const auto& inputNode = *nodes.at(connection.InputNode);
                const auto& input = pins.at(connection.InputPin);
                const bool connected = fromSpawn.contains(inputNode.Id) && toOutput.contains(inputNode.Id);
                bool executableConsumer = false;
                if (input.Block)
                    executableConsumer = connected && input.Block->Enabled;
                else if (inputNode.Kind == VfxGraphNodeKind::Module)
                    executableConsumer = connected && modules.at(inputNode.Reference).first->Enabled;
                if (outputNode.Kind == VfxGraphNodeKind::Operator && executableConsumer)
                    requiredExpressionOutputs.push_back(connection.OutputPin);
            }
            std::ranges::sort(requiredExpressionOutputs);
            requiredExpressionOutputs.erase(
                std::unique(requiredExpressionOutputs.begin(), requiredExpressionOutputs.end()),
                requiredExpressionOutputs.end());
            auto expressions = Internal::CompileVfxExpressions(system, parameterSlots, requiredExpressionOutputs);
            result.ValueInstructions = std::move(expressions.Instructions);
            result.ValueRegisterCount = expressions.RegisterCount;

            bool hasEmission = false;
            bool hasRenderer = false;
            const auto lowerModule = [&](const VfxModuleDefinition& module, const std::uint32_t moduleIndex,
                                         const AssetId executionId, const VfxContextType context,
                                         const std::span<const VfxGraphPin> propertyPins, const bool connected,
                                         const bool enabled)
            {
                if (!enabled || !connected)
                    return;
                const auto operationIndex = static_cast<std::uint32_t>(result.Modules.size());
                result.Modules.push_back({executionId, module.Id, context, moduleIndex});
                result.Operations.push_back({executionId, context, VfxCompiledOperationKind::Module, operationIndex});
                hasEmission |= std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                               std::holds_alternative<VfxBurstModule>(module.Payload);
                hasRenderer |= std::holds_alternative<VfxRendererModule>(module.Payload);

                const auto specifications = ModulePinSpecifications(module.Payload);
                for (const auto& specification : specifications)
                {
                    const auto input = std::ranges::find(propertyPins, specification.Semantic, &VfxGraphPin::Semantic);
                    if (input == propertyPins.end())
                        throw std::logic_error("Canonical VFX module property input is unavailable.");
                    const auto driver = inputDrivers.find(input->Id);
                    VfxCompiledBinding binding;
                    binding.Node = executionId;
                    binding.Module = module.Id;
                    binding.Property = specification.Property;
                    binding.Type = specification.Type;
                    if (driver == inputDrivers.end())
                    {
                        if (!input->DefaultValue)
                            throw std::logic_error("Canonical VFX module property input has no inline value.");
                        if (*input->DefaultValue == specification.DefaultValue)
                            continue;
                        binding.LiteralValue = *input->DefaultValue;
                    }
                    else
                    {
                        const auto& source = *nodes.at(driver->second->OutputNode);
                        const auto& output = *pins.at(driver->second->OutputPin).Pin;
                        if (output.Type != specification.Type)
                            throw std::invalid_argument("VFX module binding type does not match its property.");
                        if (source.Kind == VfxGraphNodeKind::Parameter)
                            binding.ParameterSlot = parameterSlots.at(source.Reference);
                        else if (source.Kind == VfxGraphNodeKind::Operator)
                        {
                            const auto expression = expressions.SourcesByOutputPin.find(driver->second->OutputPin);
                            if (expression == expressions.SourcesByOutputPin.end())
                                throw std::logic_error("VFX Operator binding was not lowered.");
                            if (expression->second.Kind == VfxCompiledValueSourceKind::Parameter)
                                binding.ParameterSlot = expression->second.Index;
                            else if (expression->second.Kind == VfxCompiledValueSourceKind::Register)
                                binding.ValueRegister = expression->second.Index;
                            else
                                binding.LiteralValue = expression->second.Literal;
                        }
                        else
                            throw std::invalid_argument(
                                "VFX module properties require a Blackboard or executable Operator source.");
                    }
                    result.Bindings.push_back(std::move(binding));
                }
            };
            const auto lowerPortable = [&](const AssetId executionId, const VfxContextType context,
                                           const std::span<const VfxGraphPin> inputPins, const std::string_view source,
                                           const bool connected, const bool enabled)
            {
                if (!enabled || !connected)
                    return;
                std::map<std::string, PortableInput> inputs;
                for (const auto& input : inputPins)
                {
                    if (!input.Input || input.Type == VfxValueType::ParticleStream)
                        continue;
                    PortableInput value;
                    value.Type = input.Type;
                    value.DefaultValue = input.DefaultValue;
                    const auto driver = inputDrivers.find(input.Id);
                    if (driver != inputDrivers.end())
                    {
                        const auto& outputNode = *nodes.at(driver->second->OutputNode);
                        if (outputNode.Kind == VfxGraphNodeKind::Parameter)
                        {
                            value.ParameterSlot = parameterSlots.at(outputNode.Reference);
                        }
                        else if (outputNode.Kind == VfxGraphNodeKind::Operator)
                        {
                            const auto expression = expressions.SourcesByOutputPin.find(driver->second->OutputPin);
                            if (expression == expressions.SourcesByOutputPin.end())
                                throw std::logic_error("Portable Custom HLSL Operator input was not lowered.");
                            switch (expression->second.Kind)
                            {
                            case VfxCompiledValueSourceKind::Literal:
                                value.DefaultValue = expression->second.Literal;
                                break;
                            case VfxCompiledValueSourceKind::Parameter:
                                value.ParameterSlot = expression->second.Index;
                                break;
                            case VfxCompiledValueSourceKind::Register:
                                value.ValueRegister = expression->second.Index;
                                break;
                            }
                        }
                        else
                        {
                            throw std::invalid_argument(
                                "Portable Custom HLSL inputs require a Blackboard or executable Operator source.");
                        }
                    }
                    inputs.emplace(input.Semantic, std::move(value));
                }
                VfxGraphNode portable;
                portable.Id = executionId;
                portable.Context = context;
                portable.CustomHlsl = source;
                std::vector<VfxCompiledCustomInstruction> instructions;
                try
                {
                    instructions = CompilePortableCustomHlsl(portable, inputs);
                }
                catch (const VfxNodeCompileError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    throw VfxNodeCompileError(executionId, error.what());
                }
                if (instructions.size() > MaximumPortableCustomInstructions - result.CustomInstructions.size())
                {
                    throw VfxNodeCompileError(executionId,
                                              "VFX graph exceeds the 4096-instruction Portable Custom HLSL compiler "
                                              "safety limit.");
                }
                for (auto& instruction : instructions)
                {
                    const auto operationIndex = static_cast<std::uint32_t>(result.CustomInstructions.size());
                    result.CustomInstructions.push_back(std::move(instruction));
                    result.Operations.push_back(
                        {executionId, context, VfxCompiledOperationKind::CustomHlsl, operationIndex});
                }
            };
            for (const auto nodeId : topologicalOrder)
            {
                const auto& node = *nodes.at(nodeId);
                if (node.Kind == VfxGraphNodeKind::Context)
                {
                    const bool connected = fromSpawn.contains(node.Id) && toOutput.contains(node.Id);
                    for (const auto& block : node.Blocks)
                    {
                        if (block.TypeId.View() == "keire.block.portable-hlsl")
                            lowerPortable(block.Id, node.Context, block.Pins, PortableBlockSource(block), connected,
                                          block.Enabled);
                        else
                        {
                            const auto [module, index] = modules.at(block.Reference);
                            lowerModule(*module, index, block.Id, node.Context, block.Pins, connected, block.Enabled);
                        }
                    }
                    continue;
                }
                if (node.Kind != VfxGraphNodeKind::Module && node.Kind != VfxGraphNodeKind::CustomHlsl)
                    continue;
                const bool connected = fromSpawn.contains(node.Id) && toOutput.contains(node.Id);
                if (requirePublishable && !connected)
                    throw std::invalid_argument("Executable VFX nodes must be connected to the main particle stream.");
                if (node.Kind == VfxGraphNodeKind::Module)
                {
                    const auto [module, index] = modules.at(node.Reference);
                    lowerModule(*module, index, node.Id, node.Context, node.Pins, connected, module->Enabled);
                }
                else
                {
                    lowerPortable(node.Id, node.Context, node.Pins, node.CustomHlsl, connected, true);
                }
            }
            if (requirePublishable && ((!eventDriven && !hasEmission) || !hasRenderer))
            {
                throw std::invalid_argument(
                    "Executable VFX graphs require connected emission (unless Event-driven) and renderer modules.");
            }
            return result;
        }

        [[nodiscard]] LoweredPlan LowerEffect(const VfxEffectDefinition& definition, const VfxGraphSystem* system)
        {
            if (definition.ExecutionSource == VfxExecutionSource::Graph)
            {
                if (!system)
                    throw std::invalid_argument("VFX graph compilation requires a particle system.");
                return LowerGraph(definition, *system, true);
            }
            return LowerLegacyModules(definition);
        }

        void MigrateLegacyExecutableNodesToBlocks(VfxEffectDefinition& definition)
        {
            if (definition.ExecutionSource != VfxExecutionSource::Graph)
                return;

            for (auto& system : definition.Systems)
            {
                if (std::ranges::none_of(
                        system.Nodes, [](const VfxGraphNode& node)
                        { return node.Kind == VfxGraphNodeKind::Module || node.Kind == VfxGraphNodeKind::CustomHlsl; }))
                {
                    continue;
                }

                std::map<AssetId, VfxGraphNode*> nodes;
                std::array<VfxGraphNode*, 4> contexts{};
                for (auto& node : system.Nodes)
                {
                    nodes.emplace(node.Id, std::addressof(node));
                    if (node.Kind == VfxGraphNodeKind::Context && node.Context != VfxContextType::Event)
                    {
                        const auto index = ContextOrder(node.Context);
                        if (contexts[index])
                            throw std::invalid_argument(
                                "Historical VFX graph migration found duplicate executable contexts.");
                        contexts[index] = std::addressof(node);
                    }
                }
                if (std::ranges::any_of(contexts, [](const VfxGraphNode* context) { return context == nullptr; }))
                {
                    throw std::invalid_argument(
                        "Historical VFX graph migration requires Spawn, Initialize, Update, and Output contexts.");
                }

                const auto graphPin = [&nodes](const AssetId nodeId, const AssetId pinId) -> const VfxGraphPin*
                {
                    const auto node = nodes.find(nodeId);
                    if (node == nodes.end())
                        return nullptr;
                    const auto pin = std::ranges::find(node->second->Pins, pinId, &VfxGraphPin::Id);
                    return pin == node->second->Pins.end() ? nullptr : std::addressof(*pin);
                };

                std::map<AssetId, std::size_t> flowIndegree;
                std::map<AssetId, std::vector<AssetId>> flowAdjacency;
                std::map<AssetId, std::vector<AssetId>> reverseFlowAdjacency;
                for (const auto& node : system.Nodes)
                    flowIndegree.emplace(node.Id, 0);
                for (const auto& connection : system.Connections)
                {
                    const auto* output = graphPin(connection.OutputNode, connection.OutputPin);
                    const auto* input = graphPin(connection.InputNode, connection.InputPin);
                    if (!output || !input || output->Type != VfxValueType::ParticleStream ||
                        input->Type != VfxValueType::ParticleStream)
                    {
                        continue;
                    }
                    flowAdjacency[connection.OutputNode].push_back(connection.InputNode);
                    reverseFlowAdjacency[connection.InputNode].push_back(connection.OutputNode);
                    ++flowIndegree.at(connection.InputNode);
                }

                const auto visitFlow = [](const AssetId start, const std::map<AssetId, std::vector<AssetId>>& adjacency)
                {
                    std::set<AssetId> visited{start};
                    std::vector<AssetId> pending{start};
                    while (!pending.empty())
                    {
                        const auto current = pending.back();
                        pending.pop_back();
                        if (const auto found = adjacency.find(current); found != adjacency.end())
                            for (const auto destination : found->second)
                                if (visited.insert(destination).second)
                                    pending.push_back(destination);
                    }
                    return visited;
                };
                const auto fromSpawn = visitFlow(contexts.front()->Id, flowAdjacency);
                const auto toOutput = visitFlow(contexts.back()->Id, reverseFlowAdjacency);

                std::set<AssetId> ready;
                for (const auto& [node, count] : flowIndegree)
                    if (count == 0)
                        ready.insert(node);
                std::vector<AssetId> flowOrder;
                flowOrder.reserve(system.Nodes.size());
                while (!ready.empty())
                {
                    const auto node = *ready.begin();
                    ready.erase(ready.begin());
                    flowOrder.push_back(node);
                    for (const auto destination : flowAdjacency[node])
                        if (--flowIndegree.at(destination) == 0)
                            ready.insert(destination);
                }
                if (flowOrder.size() != system.Nodes.size())
                    throw std::invalid_argument("Historical VFX graph migration found a particle-stream cycle.");

                std::map<AssetId, AssetId> migratedContexts;
                for (const auto nodeId : flowOrder)
                {
                    auto& node = *nodes.at(nodeId);
                    if (node.Kind != VfxGraphNodeKind::Module && node.Kind != VfxGraphNodeKind::CustomHlsl)
                        continue;
                    if (!fromSpawn.contains(node.Id) || !toOutput.contains(node.Id))
                        continue;
                    auto& context = *contexts[ContextOrder(node.Context)];
                    VfxGraphBlock block;
                    if (node.Kind == VfxGraphNodeKind::Module)
                    {
                        const auto module =
                            std::ranges::find(definition.Modules, node.Reference, &VfxModuleDefinition::Id);
                        if (module == definition.Modules.end())
                            throw std::invalid_argument(
                                "Historical VFX Module node references an unknown compatibility payload.");
                        block = CreateVfxGraphBlock(*module);
                    }
                    else
                    {
                        block = CreateVfxGraphPortableHlslBlock(node.CustomHlsl);
                    }
                    block.Id = node.Id;
                    block.DefinitionVersion = node.DefinitionVersion;
                    block.Pins.clear();
                    for (const auto& pin : node.Pins)
                        if (pin.Input && pin.Type != VfxValueType::ParticleStream)
                            block.Pins.push_back(pin);
                    context.Blocks.push_back(std::move(block));
                    migratedContexts.emplace(node.Id, context.Id);
                }

                std::vector<VfxGraphConnection> migratedConnections;
                migratedConnections.reserve(system.Connections.size());
                for (auto connection : system.Connections)
                {
                    const auto* output = graphPin(connection.OutputNode, connection.OutputPin);
                    const auto* input = graphPin(connection.InputNode, connection.InputPin);
                    if (!output || !input)
                        throw std::invalid_argument("Historical VFX graph migration found an invalid connection.");
                    const bool stream =
                        output->Type == VfxValueType::ParticleStream || input->Type == VfxValueType::ParticleStream;
                    const auto outputContext = migratedContexts.find(connection.OutputNode);
                    const auto inputContext = migratedContexts.find(connection.InputNode);
                    if (!stream)
                    {
                        if (outputContext != migratedContexts.end())
                            throw std::invalid_argument(
                                "Historical executable VFX nodes may not expose data-output pins.");
                        if (inputContext != migratedContexts.end())
                        {
                            connection.InputNode = inputContext->second;
                            connection.InputBlock = inputContext->first;
                        }
                        migratedConnections.push_back(connection);
                        continue;
                    }

                    if (outputContext == migratedContexts.end() && inputContext == migratedContexts.end())
                    {
                        migratedConnections.push_back(connection);
                        continue;
                    }
                    if (outputContext != migratedContexts.end() && inputContext == migratedContexts.end() &&
                        nodes.at(connection.InputNode)->Kind == VfxGraphNodeKind::Context)
                    {
                        const auto inputStage = ContextOrder(nodes.at(connection.InputNode)->Context);
                        if (inputStage == 0)
                            throw std::invalid_argument(
                                "Historical VFX graph migration found a stream entering the Spawn context.");
                        auto& context = *contexts[inputStage - 1];
                        const auto* contextOutput = FindPin(context, false, VfxValueType::ParticleStream, "particles");
                        if (!contextOutput)
                            throw std::invalid_argument(
                                "Historical VFX graph migration found a context without a stream output.");
                        connection.OutputNode = context.Id;
                        connection.OutputBlock = {};
                        connection.OutputPin = contextOutput->Id;
                        migratedConnections.push_back(connection);
                    }
                }
                system.Connections = std::move(migratedConnections);
                std::erase_if(system.Nodes, [&migratedContexts](const VfxGraphNode& node)
                              { return migratedContexts.contains(node.Id); });
            }
        }

        void CollectVfxStableIds(const VfxEffectDefinition& definition, std::set<AssetId>& used)
        {
            used.insert(definition.EmitterId);
            for (const auto& module : definition.Modules)
                used.insert(module.Id);
            for (const auto& parameter : definition.Blackboard)
                used.insert(parameter.Id);
            for (const auto& system : definition.Systems)
            {
                used.insert(system.Id);
                for (const auto& node : system.Nodes)
                {
                    used.insert(node.Id);
                    for (const auto& pin : node.Pins)
                        used.insert(pin.Id);
                    for (const auto& block : node.Blocks)
                    {
                        used.insert(block.Id);
                        for (const auto& pin : block.Pins)
                            used.insert(pin.Id);
                    }
                }
                for (const auto& connection : system.Connections)
                    used.insert(connection.Id);
            }
        }

        [[nodiscard]] VfxGraphPin UpgradeModulePin(const VfxGraphPin& pin, const ModulePinSpecification& specification)
        {
            if (!pin.Input || pin.Type != specification.Type || !pin.DefaultValue ||
                !ValueMatchesType(pin.Type, *pin.DefaultValue))
            {
                throw std::invalid_argument("Historical VFX module input cannot be upgraded safely.");
            }
            auto result = pin;
            result.Name = specification.Name;
            result.Semantic = specification.Semantic;
            return result;
        }

        void UpgradeModuleBlockLayout(VfxGraphBlock& block, const VfxModuleDefinition& module, std::set<AssetId>& used)
        {
            const auto targetVersion = ModuleDefinitionVersion(module.Payload);
            if (block.DefinitionVersion >= targetVersion)
                return;
            if (block.DefinitionVersion != 1 || targetVersion != 2 ||
                block.TypeId.View() != ModuleTypeId(module.Payload) || block.Type != ModuleTypeName(module.Payload))
            {
                return;
            }

            const auto specifications = ModulePinSpecifications(module.Payload);
            std::set<std::string> semantics;
            for (const auto& pin : block.Pins)
            {
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (!semantics.insert(pin.Semantic).second || specification == specifications.end())
                    throw std::invalid_argument("Historical VFX Block contains an input that cannot be upgraded.");
                (void)UpgradeModulePin(pin, *specification);
            }

            std::vector<VfxGraphPin> upgraded;
            upgraded.reserve(specifications.size());
            for (std::size_t index = 0; index < specifications.size(); ++index)
            {
                const auto& specification = specifications[index];
                const auto pin = std::ranges::find(block.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin != block.Pins.end())
                    upgraded.push_back(UpgradeModulePin(*pin, specification));
                else
                {
                    upgraded.push_back({AllocateDerivedId(block.Id, 0x50494e0000000000ULL + index, used),
                                        std::string(specification.Name), specification.Type, true,
                                        std::string(specification.Semantic), specification.DefaultValue});
                }
            }
            block.Pins = std::move(upgraded);
            block.DefinitionVersion = targetVersion;
        }

        void UpgradeModuleNodeLayout(VfxGraphNode& node, const VfxModuleDefinition& module, std::set<AssetId>& used)
        {
            const auto targetVersion = ModuleDefinitionVersion(module.Payload);
            if (node.DefinitionVersion >= targetVersion)
                return;
            if (node.DefinitionVersion != 1 || targetVersion != 2 || node.TypeId.View() != ModuleTypeId(module.Payload))
                return;

            const auto specifications = ModulePinSpecifications(module.Payload);
            const VfxGraphPin* inputFlow = nullptr;
            const VfxGraphPin* outputFlow = nullptr;
            std::set<std::string> semantics;
            for (const auto& pin : node.Pins)
            {
                if (pin.Type == VfxValueType::ParticleStream)
                {
                    auto*& flow = pin.Input ? inputFlow : outputFlow;
                    if (flow || pin.Semantic != "particles" || pin.DefaultValue)
                        throw std::invalid_argument("Historical VFX module flow cannot be upgraded safely.");
                    flow = std::addressof(pin);
                    continue;
                }
                const auto specification =
                    std::ranges::find(specifications, pin.Semantic, &ModulePinSpecification::Semantic);
                if (!semantics.insert(pin.Semantic).second || specification == specifications.end())
                    throw std::invalid_argument(
                        "Historical VFX module node contains an input that cannot be upgraded.");
                const auto upgraded = UpgradeModulePin(pin, *specification);
                if (*upgraded.DefaultValue != specification->DefaultValue)
                    throw std::invalid_argument("Historical VFX module node default cannot be upgraded safely.");
            }
            if (!inputFlow || !outputFlow)
                throw std::invalid_argument("Historical VFX module flow cannot be upgraded safely.");

            std::vector<VfxGraphPin> upgraded;
            upgraded.reserve(specifications.size() + 2);
            upgraded.push_back(*inputFlow);
            for (std::size_t index = 0; index < specifications.size(); ++index)
            {
                const auto& specification = specifications[index];
                const auto pin = std::ranges::find(node.Pins, specification.Semantic, &VfxGraphPin::Semantic);
                if (pin != node.Pins.end())
                    upgraded.push_back(UpgradeModulePin(*pin, specification));
                else
                {
                    upgraded.push_back({AllocateDerivedId(node.Id, 0x50494e0000000000ULL + index, used),
                                        std::string(specification.Name), specification.Type, true,
                                        std::string(specification.Semantic), specification.DefaultValue});
                }
            }
            upgraded.push_back(*outputFlow);
            node.Pins = std::move(upgraded);
            node.DefinitionVersion = targetVersion;
        }

        void UpgradeModuleGraphLayouts(VfxEffectDefinition& definition)
        {
            std::map<AssetId, const VfxModuleDefinition*> modules;
            for (const auto& module : definition.Modules)
                modules.emplace(module.Id, std::addressof(module));
            std::set<AssetId> used;
            CollectVfxStableIds(definition, used);

            for (auto& system : definition.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    if (node.Kind == VfxGraphNodeKind::Module)
                    {
                        if (const auto module = modules.find(node.Reference); module != modules.end())
                            UpgradeModuleNodeLayout(node, *module->second, used);
                    }
                    if (node.Kind != VfxGraphNodeKind::Context)
                        continue;
                    for (auto& block : node.Blocks)
                    {
                        if (const auto module = modules.find(block.Reference); module != modules.end())
                            UpgradeModuleBlockLayout(block, *module->second, used);
                    }
                }
            }
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

        [[nodiscard]] Json EncodeCompiledValueSource(const VfxCompiledValueSource& source)
        {
            Json result{{"kind", static_cast<std::uint32_t>(source.Kind)},
                        {"type", ValueTypeName(source.Type)},
                        {"index", source.Index}};
            if (source.Kind == VfxCompiledValueSourceKind::Literal)
                result["literal"] = EncodeTypedValue(source.Type, source.Literal);
            return result;
        }

        [[nodiscard]] Json EncodeCompiledBindings(const std::span<const VfxCompiledBinding> bindings,
                                                  const bool includeValues)
        {
            auto result = Json::array();
            for (const auto& binding : bindings)
            {
                Json encoded{{"node", IdText(binding.Node)},
                             {"module", IdText(binding.Module)},
                             {"property", static_cast<std::uint32_t>(binding.Property)},
                             {"type", ValueTypeName(binding.Type)}};
                if (binding.LiteralValue)
                {
                    encoded["source"] = "literal";
                    if (includeValues)
                        encoded["literal"] = EncodeTypedValue(binding.Type, *binding.LiteralValue);
                }
                else if (binding.ValueRegister != ~std::uint32_t{0})
                {
                    encoded["source"] = "register";
                    encoded["index"] = binding.ValueRegister;
                }
                else
                {
                    encoded["source"] = "parameter";
                    encoded["index"] = binding.ParameterSlot;
                }
                result.push_back(std::move(encoded));
            }
            return result;
        }

        [[nodiscard]] Json
        EncodeCompiledValueInstructions(const std::span<const VfxCompiledValueInstruction> instructions)
        {
            auto result = Json::array();
            for (const auto& instruction : instructions)
            {
                auto inputs = Json::array();
                for (const auto& input : instruction.Inputs)
                    inputs.push_back(EncodeCompiledValueSource(input));
                result.push_back({{"node", IdText(instruction.Node)},
                                  {"opcode", static_cast<std::uint32_t>(instruction.Opcode)},
                                  {"type", ValueTypeName(instruction.Type)},
                                  {"context", ContextName(instruction.Context)},
                                  {"domain", static_cast<std::uint32_t>(instruction.Domain)},
                                  {"output", instruction.OutputRegister},
                                  {"outputIndex", instruction.OutputIndex},
                                  {"inputs", std::move(inputs)},
                                  {"channelSalt", instruction.ChannelSalt},
                                  {"randomScope", static_cast<std::uint32_t>(instruction.RandomScope)},
                                  {"constantRandom", instruction.ConstantRandom},
                                  {"independentRandomChannels", instruction.IndependentRandomChannels},
                                  {"inclusiveMaximum", instruction.InclusiveMaximum},
                                  {"clampRemap", instruction.ClampRemap},
                                  {"comparison", static_cast<std::uint32_t>(instruction.Comparison)}});
            }
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
                    if (instruction.ValueRegister != ~std::uint32_t{0})
                    {
                        encoded["source"] = "register";
                        encoded["index"] = instruction.ValueRegister;
                    }
                    else if (instruction.ParameterSlot != ~std::uint32_t{0})
                    {
                        encoded["source"] = "parameter";
                        encoded["index"] = instruction.ParameterSlot;
                    }
                    else
                    {
                        encoded["source"] = "literal";
                        encoded["literal"] = EncodeTypedValue(instruction.OperandType, instruction.Literal);
                    }
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
            return JsonBytes(
                {{"emitterId", IdText(definition.EmitterId)},
                 {"systemId", IdText(plan.System)},
                 {"dataType", static_cast<std::uint32_t>(plan.DataType)},
                 {"particlesPerStrip", plan.ParticlesPerStrip},
                 {"eventName", plan.EventName},
                 {"loop", definition.Loop},
                 {"duration", definition.Duration},
                 {"space", SpaceName(definition.Space)},
                 {"seed", definition.Seed},
                 {"capacity", definition.Capacity},
                 {"executionSource", ExecutionSourceName(definition.ExecutionSource)},
                 {"compatibilityMode", CompatibilityModeName(definition.SchemaVersion < CurrentVfxSchemaVersion
                                                                 ? VfxCompatibilityMode::MigratedLegacyModules
                                                                 : definition.CompatibilityMode)},
                 {"parameters", EncodeCompiledParameters(plan.Parameters, true)},
                 {"modules", EncodeCompiledModules(definition, plan.Modules, true)},
                 {"bindings", EncodeCompiledBindings(plan.Bindings, true)},
                 {"valueRegisterCount", plan.ValueRegisterCount},
                 {"valueInstructions", EncodeCompiledValueInstructions(plan.ValueInstructions)},
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
                           {"systemId", IdText(plan.System)},
                           {"dataType", static_cast<std::uint32_t>(plan.DataType)},
                           {"particlesPerStrip", plan.ParticlesPerStrip},
                           {"space", SpaceName(definition.Space)},
                           {"seed", definition.Seed},
                           {"rendererType", static_cast<std::uint32_t>(rendererType)},
                           {"parameters", EncodeCompiledParameters(plan.Parameters, false)},
                           {"modules", EncodeCompiledModules(definition, plan.Modules, false)},
                           {"bindings", EncodeCompiledBindings(plan.Bindings, false)},
                           {"customInstructions", EncodeCompiledCustomInstructions(plan.CustomInstructions, false)},
                           {"operations", EncodeCompiledOperations(plan.Operations)}});
            return HashBytes(bytes);
        }

        [[nodiscard]] bool IsGpuValueType(const VfxValueType type) noexcept
        {
            return IsVfxGpuExpressionValueType(type);
        }

        [[nodiscard]] std::array<std::uint32_t, 4> GpuIdentity(const AssetId id) noexcept
        {
            return {static_cast<std::uint32_t>(id.High()), static_cast<std::uint32_t>(id.High() >> 32U),
                    static_cast<std::uint32_t>(id.Low()), static_cast<std::uint32_t>(id.Low() >> 32U)};
        }

        [[nodiscard]] VfxCompiledGpuValueProgram
        BuildGpuValueProgram(const std::span<const VfxCompiledValueInstruction> instructions,
                             const std::uint32_t registerCount, const std::uint32_t parameterCount,
                             const AssetId system)
        {
            VfxCompiledGpuValueProgram result;
            result.SystemIdentity = GpuIdentity(system);
            result.RegisterCount = registerCount;
            if (registerCount > VfxCompiledGpuValueProgram::MaximumRegisters)
            {
                const auto instruction = std::ranges::find_if(
                    instructions, [](const VfxCompiledValueInstruction& candidate)
                    { return candidate.OutputRegister >= VfxCompiledGpuValueProgram::MaximumRegisters; });
                throw VfxNodeCompileError(instruction == instructions.end() ? AssetId{} : instruction->Node,
                                          "VFX GPU expression program exceeds the 64-register shader limit.");
            }
            result.Instructions.reserve(instructions.size());
            result.Sources.reserve(
                std::min<std::size_t>(instructions.size() * 2, VfxCompiledGpuValueProgram::MaximumSources));

            for (const auto& instruction : instructions)
            {
                if (result.Instructions.size() >= VfxCompiledGpuValueProgram::MaximumInstructions)
                {
                    throw VfxNodeCompileError(instruction.Node,
                                              "VFX GPU expression program exceeds the 64-instruction shader limit.");
                }
                if (!IsGpuValueType(instruction.Type))
                {
                    throw VfxNodeCompileError(instruction.Node,
                                              "This VFX value type has no packed GPU register representation.");
                }
                if (instruction.OutputRegister >= registerCount || instruction.Inputs.size() > 8)
                    throw VfxNodeCompileError(instruction.Node, "VFX GPU expression instruction layout is invalid.");

                VfxGpuValueInstruction packed;
                packed.Header = {
                    static_cast<std::uint32_t>(instruction.Opcode), static_cast<std::uint32_t>(instruction.Type),
                    static_cast<std::uint32_t>(instruction.Context), static_cast<std::uint32_t>(instruction.Domain)};
                packed.Output = {instruction.OutputRegister, instruction.OutputIndex,
                                 static_cast<std::uint32_t>(result.Sources.size()),
                                 static_cast<std::uint32_t>(instruction.Inputs.size())};
                std::uint32_t flags = 0;
                if (instruction.ConstantRandom)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::ConstantRandom);
                if (instruction.IndependentRandomChannels)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::IndependentRandomChannels);
                if (instruction.InclusiveMaximum)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::InclusiveMaximum);
                if (instruction.ClampRemap)
                    flags |= static_cast<std::uint32_t>(VfxGpuValueInstructionFlag::ClampRemap);
                packed.Settings = {instruction.ChannelSalt, static_cast<std::uint32_t>(instruction.RandomScope), flags,
                                   static_cast<std::uint32_t>(instruction.Comparison)};
                packed.NodeIdentity = GpuIdentity(instruction.Node);

                for (const auto& source : instruction.Inputs)
                {
                    if (result.Sources.size() >= VfxCompiledGpuValueProgram::MaximumSources)
                    {
                        throw VfxNodeCompileError(instruction.Node,
                                                  "VFX GPU expression program exceeds the 256-source shader limit.");
                    }
                    if (!IsGpuValueType(source.Type))
                    {
                        throw VfxNodeCompileError(instruction.Node,
                                                  "This VFX input type has no packed GPU value representation.");
                    }

                    VfxGpuValueSource packedSource;
                    packedSource.Type = static_cast<std::uint32_t>(source.Type);
                    switch (source.Kind)
                    {
                    case VfxCompiledValueSourceKind::Literal:
                    {
                        VfxGpuValue value;
                        if (!Internal::PackVfxGpuValue(source.Type, source.Literal, value))
                        {
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX literal cannot be represented by the GPU value ABI.");
                        }
                        const auto found = std::ranges::find(result.Constants, value);
                        if (found == result.Constants.end())
                        {
                            if (result.Constants.size() >= VfxCompiledGpuValueProgram::MaximumConstants)
                            {
                                throw VfxNodeCompileError(
                                    instruction.Node,
                                    "VFX GPU expression program exceeds the 256-constant shader limit.");
                            }
                            packedSource.Index = static_cast<std::uint32_t>(result.Constants.size());
                            result.Constants.push_back(value);
                        }
                        else
                        {
                            packedSource.Index =
                                static_cast<std::uint32_t>(std::distance(result.Constants.begin(), found));
                        }
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Literal);
                        break;
                    }
                    case VfxCompiledValueSourceKind::Parameter:
                        if (source.Index >= parameterCount)
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX GPU expression parameter source is invalid.");
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Parameter);
                        packedSource.Index = source.Index;
                        break;
                    case VfxCompiledValueSourceKind::Register:
                        if (source.Index >= registerCount)
                            throw VfxNodeCompileError(instruction.Node,
                                                      "VFX GPU expression register source is invalid.");
                        packedSource.Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Register);
                        packedSource.Index = source.Index;
                        break;
                    }
                    result.Sources.push_back(packedSource);
                }
                result.Instructions.push_back(packed);
            }
            return result;
        }

        void AppendGpuCapabilityDiagnostics(const VfxEffectDefinition& executable, const VfxCompiledProgram& program,
                                            const bool strictSchemaFour, std::vector<VfxCompileDiagnostic>& diagnostics)
        {
            const auto error = [&diagnostics, strictSchemaFour](const AssetId node, std::string message)
            {
                diagnostics.push_back(
                    {strictSchemaFour ? VfxCompileDiagnosticSeverity::Error : VfxCompileDiagnosticSeverity::Warning,
                     node, std::move(message)});
            };
            const auto hardError = [&diagnostics](const AssetId node, std::string message)
            { diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, node, std::move(message)}); };
            std::size_t rendererCount = 0;
            const auto resolveModule = [&executable](const VfxCompiledModule& compiled) -> const VfxModuleDefinition&
            {
                const auto module = std::ranges::find(executable.Modules, compiled.Node, &VfxModuleDefinition::Id);
                if (module == executable.Modules.end())
                    throw std::logic_error("VFX resolved GPU module layout is invalid.");
                return *module;
            };
            const VfxRendererModule* renderer = nullptr;
            for (const auto& compiledModule : program.Modules)
            {
                const auto& executableModule = resolveModule(compiledModule);
                const auto node = compiledModule.Node;
                std::visit(
                    Overloaded{
                        [](const VfxShapeModule&) {},
                        [](const VfxInitializeModule&) {},
                        [](const VfxSizeOverLifetimeModule&) {},
                        [](const VfxColorOverLifetimeModule&) {},
                        [](const VfxForceModule&) {},
                        [&](const VfxCollisionModule& value)
                        {
                            if (value.Mode == VfxCollisionMode::Cpu || value.Mode == VfxCollisionMode::ScenePhysics)
                            {
                                error(node, "GPU VFX supports None or GPU Depth collision; CPU and Scene Physics "
                                            "queries require the CPU backend.");
                            }
                        },
                        [](const VfxKillShapeModule&) {},
                        [&](const VfxRendererModule& value)
                        {
                            if (!renderer)
                                renderer = std::addressof(value);
                            if (++rendererCount > 1)
                            {
                                hardError(compiledModule.Node,
                                          "GPU VFX currently supports one Renderer Block per particle system.");
                            }
                            if (value.Type == VfxRendererType::Ribbon &&
                                program.DataType != VfxParticleDataType::ParticleStrip)
                            {
                                hardError(compiledModule.Node, "Ribbon output requires a Particle Strip system.");
                            }
                            (void)node;
                        },
                        [](const auto&) {},
                    },
                    executableModule.Payload);
            }

            const auto spriteOutput = renderer && renderer->Type == VfxRendererType::Sprite;
            if (spriteOutput)
            {
                for (const auto& binding : program.Bindings)
                {
                    const auto rotation = binding.Property == VfxModuleProperty::InitializeRotationMinimum ||
                                          binding.Property == VfxModuleProperty::InitializeRotationMaximum;
                    if (rotation && binding.ValueRegister != ~std::uint32_t{0})
                    {
                        error(binding.Node,
                              "GPU VFX cannot prove that a dynamic Sprite rotation contains only a Z component; "
                              "use literal or Blackboard Z-only values.");
                    }
                }
                for (const auto& compiledModule : program.Modules)
                {
                    const auto& executableModule = resolveModule(compiledModule);
                    const auto* initialize = std::get_if<VfxInitializeModule>(&executableModule.Payload);
                    if (!initialize)
                        continue;
                    if (initialize->RotationMinimum.X != 0.0F || initialize->RotationMinimum.Y != 0.0F ||
                        initialize->RotationMaximum.X != 0.0F || initialize->RotationMaximum.Y != 0.0F)
                    {
                        error(compiledModule.Node,
                              "GPU VFX Sprite output supports Z-axis initialization rotation only; X/Y rotation "
                              "requires Mesh output.");
                    }
                }
            }
        }

        void AppendCpuCapabilityDiagnostics(const VfxEffectDefinition& executable, const VfxCompiledProgram& program,
                                            const bool strictSchemaFour, std::vector<VfxCompileDiagnostic>& diagnostics)
        {
            const auto unsupported = [&diagnostics, strictSchemaFour](const AssetId node, std::string message)
            {
                diagnostics.push_back(
                    {strictSchemaFour ? VfxCompileDiagnosticSeverity::Error : VfxCompileDiagnosticSeverity::Warning,
                     node, std::move(message)});
            };
            const auto hardError = [&diagnostics](const AssetId node, std::string message)
            { diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, node, std::move(message)}); };

            const VfxRendererModule* renderer = nullptr;
            std::size_t rendererCount = 0;
            if (executable.Modules.size() != program.Modules.size())
                throw std::logic_error("VFX resolved CPU module layout is invalid.");
            for (std::size_t index = 0; index < program.Modules.size(); ++index)
            {
                const auto& compiledModule = program.Modules[index];
                if (executable.Modules[index].Id != compiledModule.Node)
                    throw std::logic_error("VFX resolved CPU module layout is invalid.");
                const auto* candidate = std::get_if<VfxRendererModule>(&executable.Modules[index].Payload);
                if (!candidate)
                    continue;
                ++rendererCount;
                if (!renderer)
                    renderer = candidate;
                if (rendererCount > 1)
                {
                    hardError(compiledModule.Node,
                              "CPU VFX currently supports one Renderer Block per particle system.");
                }
                if (candidate->Type == VfxRendererType::Ribbon &&
                    program.DataType != VfxParticleDataType::ParticleStrip)
                    hardError(compiledModule.Node, "Ribbon output requires a Particle Strip system.");
            }

            if (!renderer || renderer->Type != VfxRendererType::Sprite)
                return;
            for (const auto& binding : program.Bindings)
            {
                const auto rotation = binding.Property == VfxModuleProperty::InitializeRotationMinimum ||
                                      binding.Property == VfxModuleProperty::InitializeRotationMaximum;
                if (rotation && binding.ValueRegister != ~std::uint32_t{0})
                {
                    unsupported(binding.Node,
                                "CPU VFX cannot prove that a dynamic Sprite rotation contains only a Z component; "
                                "use literal or Blackboard Z-only values.");
                }
            }
            for (std::size_t index = 0; index < program.Modules.size(); ++index)
            {
                const auto& compiledModule = program.Modules[index];
                const auto* initialize = std::get_if<VfxInitializeModule>(&executable.Modules[index].Payload);
                if (!initialize)
                    continue;
                if (initialize->RotationMinimum.X != 0.0F || initialize->RotationMinimum.Y != 0.0F ||
                    initialize->RotationMaximum.X != 0.0F || initialize->RotationMaximum.Y != 0.0F)
                {
                    unsupported(compiledModule.Node,
                                "CPU VFX Sprite output supports Z-axis initialization rotation only; X/Y rotation "
                                "requires Mesh output.");
                }
            }
        }
    } // namespace

    namespace Internal
    {
        void ValidateVfxResolvedBackendCapabilities(const VfxEffectDefinition& definition,
                                                    const VfxCompiledProgram& program, const VfxBackend backend,
                                                    const bool strictSchemaFour)
        {
            std::vector<VfxCompileDiagnostic> diagnostics;
            if (backend == VfxBackend::Gpu)
            {
                AppendGpuCapabilityDiagnostics(definition, program, strictSchemaFour, diagnostics);
            }
            else
            {
                AppendCpuCapabilityDiagnostics(definition, program, strictSchemaFour, diagnostics);
            }
            const auto failure =
                std::ranges::find(diagnostics, VfxCompileDiagnosticSeverity::Error, &VfxCompileDiagnostic::Severity);
            if (failure != diagnostics.end())
                throw std::invalid_argument(failure->Message);
        }

        void ApplyVfxModuleProperty(VfxModuleDefinition& module, const VfxModuleProperty property,
                                    const VfxParameterValue& value)
        {
            switch (property)
            {
            case VfxModuleProperty::EmissionParticlesPerSecond:
                std::get<VfxEmissionRateModule>(module.Payload).ParticlesPerSecond = std::get<float>(value);
                break;
            case VfxModuleProperty::BurstTime:
                std::get<VfxBurstModule>(module.Payload).Time = std::get<float>(value);
                break;
            case VfxModuleProperty::BurstCount:
            {
                const auto integer = std::get<std::int64_t>(value);
                if (integer < 1 || integer > 1'000'000)
                    throw std::invalid_argument("VFX burst is invalid.");
                std::get<VfxBurstModule>(module.Payload).Count = static_cast<std::uint32_t>(integer);
                break;
            }
            case VfxModuleProperty::BurstCycles:
            {
                const auto integer = std::get<std::int64_t>(value);
                if (integer < 1 || integer > static_cast<std::int64_t>(MaximumBurstCycles))
                    throw std::invalid_argument("VFX burst is invalid.");
                std::get<VfxBurstModule>(module.Payload).Cycles = static_cast<std::uint32_t>(integer);
                break;
            }
            case VfxModuleProperty::BurstInterval:
                std::get<VfxBurstModule>(module.Payload).Interval = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeBoxHalfExtent:
                std::get<VfxShapeModule>(module.Payload).BoxHalfExtent = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ShapeRadius:
                std::get<VfxShapeModule>(module.Payload).Radius = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeConeAngleDegrees:
                std::get<VfxShapeModule>(module.Payload).ConeAngleDegrees = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeConeLength:
                std::get<VfxShapeModule>(module.Payload).ConeLength = std::get<float>(value);
                break;
            case VfxModuleProperty::ShapeMesh:
                std::get<VfxShapeModule>(module.Payload).Mesh = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::ShapeVolume:
                std::get<VfxShapeModule>(module.Payload).Volume = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::InitializeLifetimeMinimum:
                std::get<VfxInitializeModule>(module.Payload).LifetimeMinimum = std::get<float>(value);
                break;
            case VfxModuleProperty::InitializeLifetimeMaximum:
                std::get<VfxInitializeModule>(module.Payload).LifetimeMaximum = std::get<float>(value);
                break;
            case VfxModuleProperty::InitializeVelocityMinimum:
                std::get<VfxInitializeModule>(module.Payload).VelocityMinimum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeVelocityMaximum:
                std::get<VfxInitializeModule>(module.Payload).VelocityMaximum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeRotationMinimum:
                std::get<VfxInitializeModule>(module.Payload).RotationMinimum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::InitializeRotationMaximum:
                std::get<VfxInitializeModule>(module.Payload).RotationMaximum = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ForceVector:
                std::get<VfxForceModule>(module.Payload).Force = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::ForceGravityMultiplier:
                std::get<VfxForceModule>(module.Payload).GravityMultiplier = std::get<float>(value);
                break;
            case VfxModuleProperty::SizeConstant:
                std::get<VfxSizeOverLifetimeModule>(module.Payload).Size = Curve1D::Constant(std::get<float>(value));
                break;
            case VfxModuleProperty::ColorConstant:
                std::get<VfxColorOverLifetimeModule>(module.Payload).Color =
                    ColorGradient::Constant(std::get<Color>(value));
                break;
            case VfxModuleProperty::CollisionRestitution:
                std::get<VfxCollisionModule>(module.Payload).Restitution = std::get<float>(value);
                break;
            case VfxModuleProperty::CollisionKillOnCollision:
                std::get<VfxCollisionModule>(module.Payload).KillOnCollision = std::get<bool>(value);
                break;
            case VfxModuleProperty::KillShapeCenter:
                std::get<VfxKillShapeModule>(module.Payload).Center = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::KillShapeBoxHalfExtent:
                std::get<VfxKillShapeModule>(module.Payload).BoxHalfExtent = std::get<Vector3>(value);
                break;
            case VfxModuleProperty::KillShapeRadius:
                std::get<VfxKillShapeModule>(module.Payload).Radius = std::get<float>(value);
                break;
            case VfxModuleProperty::KillShapeInverted:
                std::get<VfxKillShapeModule>(module.Payload).Mode =
                    std::get<bool>(value) ? VfxKillShapeMode::Inverted : VfxKillShapeMode::Solid;
                break;
            case VfxModuleProperty::RendererSprite:
                std::get<VfxRendererModule>(module.Payload).Sprite = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::RendererMesh:
                std::get<VfxRendererModule>(module.Payload).Mesh = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::RendererMaterial:
                std::get<VfxRendererModule>(module.Payload).Material = std::get<AssetId>(value);
                break;
            case VfxModuleProperty::None:
                throw std::invalid_argument("VFX graph binding has no executable module property.");
            }
        }

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
                auto executableModule = module;
                // Runtime copies are keyed by the stable execution node, not their shared authoring payload. This
                // gives duplicate Blocks independent literals, expression inputs, ordering, and hot-reload identity.
                executableModule.Id = compiled.Node;
                // A compiled operation is already filtered by its graph Block or legacy-node enabled state. Runtime
                // execution must therefore follow the compiled graph, not the compatibility payload's authoring flag.
                executableModule.Enabled = true;
                result.Modules.push_back(std::move(executableModule));
            }

            for (const auto& binding : program.Bindings)
            {
                const auto module = std::ranges::find(result.Modules, binding.Node, &VfxModuleDefinition::Id);
                if (module == result.Modules.end())
                    throw std::invalid_argument("VFX compiled binding is invalid.");
                if (binding.ValueRegister != ~std::uint32_t{0})
                    continue;
                const auto* valuePointer = binding.LiteralValue ? std::addressof(*binding.LiteralValue)
                                           : binding.ParameterSlot < parameters.size()
                                               ? std::addressof(parameters[binding.ParameterSlot])
                                               : nullptr;
                if (!valuePointer || !VfxValueMatchesType(binding.Type, *valuePointer))
                    throw std::invalid_argument("VFX compiled binding value is invalid.");
                const auto& value = *valuePointer;
                ApplyVfxModuleProperty(*module, binding.Property, value);
            }

            const auto hasEmission =
                std::ranges::any_of(result.Modules,
                                    [](const VfxModuleDefinition& module)
                                    {
                                        return std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                                               std::holds_alternative<VfxBurstModule>(module.Payload);
                                    });
            if (!hasEmission && !program.EventName.empty())
            {
                auto validationId = AssetId(program.System.High() ^ 0x4556454e54535041ULL,
                                            program.System.Low() ^ 0x574e000000000001ULL);
                while (!validationId || std::ranges::find(result.Modules, validationId, &VfxModuleDefinition::Id) !=
                                            result.Modules.end())
                {
                    validationId = AssetId(validationId.High(), validationId.Low() + 1U);
                }
                result.Modules.push_back({validationId, true, VfxEmissionRateModule{0.0F}});
                ValidateVfxEffect(result);
                result.Modules.pop_back();
            }
            else
            {
                ValidateVfxEffect(result);
            }
            return result;
        }
    } // namespace Internal

    void ValidateVfxEffectAuthoring(const VfxEffectDefinition& definition)
    {
        if ((definition.SchemaVersion < 1 || definition.SchemaVersion > CurrentVfxSchemaVersion) ||
            !definition.EmitterId || definition.Name.empty() || definition.Name.size() > MaximumNameBytes ||
            !std::isfinite(definition.Duration) || definition.Duration < 0.001F || definition.Duration > 3600.0F ||
            definition.Capacity == 0 || definition.Capacity > 1'000'000 || definition.Modules.empty() ||
            definition.Modules.size() > MaximumModules || definition.ExecutionSource > VfxExecutionSource::Graph ||
            definition.CompatibilityMode > VfxCompatibilityMode::MigratedLegacyModules ||
            (definition.SchemaVersion < 3 && definition.ExecutionSource != VfxExecutionSource::LegacyModules))
        {
            throw std::invalid_argument("VFX effect header is invalid.");
        }
        if (definition.Space > VfxSimulationSpace::World)
            throw std::invalid_argument("VFX effect simulation space is invalid.");

        std::set<AssetId> stableIds{definition.EmitterId};
        std::size_t bursts = 0;

        for (const auto& module : definition.Modules)
        {
            if (!module.Id || !stableIds.insert(module.Id).second)
                throw std::invalid_argument("VFX effect contains an empty or duplicate stable ID.");
            std::visit(
                Overloaded{
                    [&](const VfxEmissionRateModule& value)
                    {
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
                        if (!BoundedVector(value.Force) || !std::isfinite(value.GravityMultiplier) ||
                            std::abs(value.GravityMultiplier) > 1000.0F)
                        {
                            throw std::invalid_argument("VFX force module is invalid.");
                        }
                    },
                    [&](const VfxSizeOverLifetimeModule& value)
                    {
                        if (!ValidSizeCurve(value.Size))
                            throw std::invalid_argument("VFX size curve is invalid.");
                    },
                    [&](const VfxColorOverLifetimeModule& value)
                    {
                        if (!ValidColorGradient(value.Color))
                            throw std::invalid_argument("VFX color gradient is invalid.");
                    },
                    [&](const VfxCollisionModule& value)
                    {
                        if (value.Mode > VfxCollisionMode::ScenePhysics || !std::isfinite(value.Restitution) ||
                            value.Restitution < 0.0F || value.Restitution > 1.0F)
                        {
                            throw std::invalid_argument("VFX collision module is invalid.");
                        }
                    },
                    [&](const VfxKillShapeModule& value)
                    {
                        if ((value.Shape != VfxShape::Box && value.Shape != VfxShape::Sphere) ||
                            value.Mode > VfxKillShapeMode::Inverted || !BoundedVector(value.Center) ||
                            !Math::IsFinite(value.BoxHalfExtent) || value.BoxHalfExtent.X <= 0.0F ||
                            value.BoxHalfExtent.Y <= 0.0F || value.BoxHalfExtent.Z <= 0.0F ||
                            value.BoxHalfExtent.X > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Y > MaximumAuthoredScalar ||
                            value.BoxHalfExtent.Z > MaximumAuthoredScalar || !std::isfinite(value.Radius) ||
                            value.Radius <= 0.0F || value.Radius > MaximumAuthoredScalar)
                        {
                            throw std::invalid_argument("VFX kill-shape module is invalid.");
                        }
                    },
                    [&](const VfxRendererModule& value)
                    {
                        if (value.Type > VfxRendererType::Volumetric ||
                            (value.Type == VfxRendererType::Mesh && !value.Mesh))
                        {
                            throw std::invalid_argument("VFX renderer module is invalid.");
                        }
                    },
                },
                module.Payload);
        }

        if (bursts > MaximumBursts)
        {
            throw std::invalid_argument("VFX effect contains an invalid module multiplicity.");
        }

        if (!definition.Systems.empty() || !definition.Blackboard.empty() ||
            definition.ExecutionSource == VfxExecutionSource::Graph)
        {
            if ((definition.ExecutionSource == VfxExecutionSource::Graph && definition.Systems.empty()) ||
                definition.Systems.size() > MaximumSystems ||
                definition.Blackboard.size() > MaximumBlackboardParameters)
                throw std::invalid_argument("VFX graph system or blackboard count is invalid.");
            std::size_t nodeCount = 0;
            std::size_t connectionCount = 0;
            std::set<std::string> parameterNames;
            for (const auto& parameter : definition.Blackboard)
            {
                if (!parameter.Id || !stableIds.insert(parameter.Id).second || parameter.Name.empty() ||
                    parameter.Name.size() > MaximumNameBytes || !parameterNames.insert(parameter.Name).second ||
                    !IsPersistableValueType(parameter.Type) ||
                    !ValueMatchesType(parameter.Type, parameter.DefaultValue))
                    throw std::invalid_argument("VFX blackboard contains an invalid parameter.");
            }
            for (const auto& system : definition.Systems)
            {
                if (!system.Id || !stableIds.insert(system.Id).second || system.Name.empty() ||
                    system.Name.size() > MaximumNameBytes || system.DataType > VfxParticleDataType::ParticleStrip ||
                    system.ParticlesPerStrip == 0 ||
                    (system.DataType == VfxParticleDataType::ParticleStrip &&
                     system.ParticlesPerStrip > definition.Capacity))
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
                        node.Context > VfxContextType::Event || node.Kind > VfxGraphNodeKind::Subgraph ||
                        (definition.SchemaVersion >= 4 && (!ValidTypeId(node.TypeId) || node.DefinitionVersion == 0 ||
                                                           !ValidGraphProperties(node.Properties))))
                        throw std::invalid_argument("VFX graph contains an invalid node.");
                    for (const auto& pin : node.Pins)
                    {
                        if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                            pin.Name.empty() || pin.Name.size() > MaximumNameBytes ||
                            pin.Semantic.size() > MaximumNameBytes || pin.Type > VfxValueType::SignedDistanceField ||
                            (pin.DefaultValue && (pin.Type == VfxValueType::ParticleStream ||
                                                  !ValueMatchesType(pin.Type, *pin.DefaultValue))))
                            throw std::invalid_argument("VFX graph contains an invalid pin.");
                    }
                    if (definition.SchemaVersion >= 4)
                    {
                        if (std::ranges::any_of(node.ResolvedSignature, [](const VfxValueType type)
                                                { return type > VfxValueType::SignedDistanceField; }))
                        {
                            throw std::invalid_argument("VFX graph contains an invalid resolved signature.");
                        }
                        std::set<AssetId> dynamicPins;
                        for (const auto pin : node.DynamicPinOrder)
                        {
                            if (!pin || !dynamicPins.insert(pin).second ||
                                std::ranges::find(node.Pins, pin, &VfxGraphPin::Id) == node.Pins.end())
                            {
                                throw std::invalid_argument("VFX graph contains an invalid dynamic pin order.");
                            }
                        }
                        if (node.Kind != VfxGraphNodeKind::Context && !node.Blocks.empty())
                            throw std::invalid_argument("Only VFX Context nodes may own ordered blocks.");
                        nodeCount += node.Blocks.size();
                        for (const auto& block : node.Blocks)
                        {
                            if (!block.Id || !stableIds.insert(block.Id).second || !ValidTypeId(block.TypeId) ||
                                block.Type.empty() || block.Type.size() > MaximumNameBytes ||
                                block.DefinitionVersion == 0 ||
                                !ValidGraphProperties(block.Properties,
                                                      block.TypeId.View() == "keire.block.portable-hlsl"
                                                          ? MaximumDocumentBytes
                                                          : MaximumNameBytes))
                            {
                                throw std::invalid_argument("VFX graph contains an invalid block.");
                            }
                            for (const auto& pin : block.Pins)
                            {
                                if (!pin.Id || !stableIds.insert(pin.Id).second || !pinIds.insert(pin.Id).second ||
                                    pin.Name.empty() || pin.Name.size() > MaximumNameBytes ||
                                    pin.Semantic.size() > MaximumNameBytes ||
                                    pin.Type > VfxValueType::SignedDistanceField ||
                                    (pin.DefaultValue && (pin.Type == VfxValueType::ParticleStream ||
                                                          !ValueMatchesType(pin.Type, *pin.DefaultValue))))
                                {
                                    throw std::invalid_argument("VFX graph contains an invalid block pin.");
                                }
                            }
                        }
                    }
                }
                const auto findPin = [&system](const AssetId nodeId, const AssetId blockId,
                                               const AssetId pinId) -> const VfxGraphPin*
                {
                    const auto node = std::ranges::find(system.Nodes, nodeId, &VfxGraphNode::Id);
                    if (node == system.Nodes.end())
                        return nullptr;
                    if (blockId)
                    {
                        const auto block = std::ranges::find(node->Blocks, blockId, &VfxGraphBlock::Id);
                        if (block == node->Blocks.end())
                            return nullptr;
                        const auto pin = std::ranges::find(block->Pins, pinId, &VfxGraphPin::Id);
                        return pin == block->Pins.end() ? nullptr : std::addressof(*pin);
                    }
                    const auto pin = std::ranges::find(node->Pins, pinId, &VfxGraphPin::Id);
                    return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
                };
                for (const auto& connection : system.Connections)
                {
                    const auto* output = findPin(connection.OutputNode, connection.OutputBlock, connection.OutputPin);
                    const auto* input = findPin(connection.InputNode, connection.InputBlock, connection.InputPin);
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
        const auto hasRendererPayload =
            std::ranges::any_of(definition.Modules, [](const VfxModuleDefinition& module)
                                { return std::holds_alternative<VfxRendererModule>(module.Payload); });
        if (definition.ExecutionSource == VfxExecutionSource::LegacyModules &&
            (!hasEnabledEmission || !hasEnabledRenderer))
            throw std::invalid_argument("VFX effect requires enabled emission and renderer modules.");
        if (definition.ExecutionSource == VfxExecutionSource::Graph && !hasRendererPayload)
            throw std::invalid_argument("VFX graph requires renderer backing modules.");
        if (definition.ExecutionSource == VfxExecutionSource::Graph)
            for (const auto& system : definition.Systems)
                (void)LowerGraph(definition, system, false);
    }

    void ValidateVfxEffect(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffectAuthoring(definition);
        if (definition.ExecutionSource == VfxExecutionSource::Graph)
            for (const auto& system : definition.Systems)
                (void)LowerGraph(definition, system, true);
    }

    std::vector<AssetId> VfxEffectDependencies(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffect(definition);
        std::vector<AssetId> result;
        const auto appendValue = [&result](const auto& value)
        {
            if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                result.push_back(*asset);
        };
        const auto appendProperties = [&result](const std::span<const VfxGraphProperty> properties)
        {
            for (const auto& property : properties)
                if (const auto* asset = std::get_if<AssetId>(&property.Value); asset && *asset)
                    result.push_back(*asset);
        };
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
                        if (value.Type != VfxRendererType::Mesh && value.Sprite)
                            result.push_back(value.Sprite);
                        if (value.Type == VfxRendererType::Mesh && value.Mesh)
                            result.push_back(value.Mesh);
                        if (value.Material)
                            result.push_back(value.Material);
                    },
                    [](const auto&) {},
                },
                module.Payload);
        }
        for (const auto& parameter : definition.Blackboard)
            appendValue(parameter.DefaultValue);
        for (const auto& system : definition.Systems)
        {
            for (const auto& node : system.Nodes)
            {
                if (node.Kind == VfxGraphNodeKind::Subgraph && node.Reference)
                    result.push_back(node.Reference);
                appendProperties(node.Properties);
                for (const auto& pin : node.Pins)
                    if (pin.DefaultValue)
                        appendValue(*pin.DefaultValue);
                for (const auto& block : node.Blocks)
                {
                    appendProperties(block.Properties);
                    for (const auto& pin : block.Pins)
                        if (pin.DefaultValue)
                            appendValue(*pin.DefaultValue);
                }
            }
        }
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    namespace
    {
        VfxCompiledProgram CompileVfxEffectSystem(const VfxEffectDefinition& definition, const VfxBackend backend,
                                                  const VfxGraphSystem* system)
        {
            VfxCompiledProgram result;
            result.Backend = backend;
            try
            {
                ValidateVfxEffect(definition);
                auto plan = LowerEffect(definition, system);
                result.System = plan.System;
                result.DataType = plan.DataType;
                result.ParticlesPerStrip = plan.ParticlesPerStrip;
                result.EventName = plan.EventName;
                std::set<AssetId> reportedBackendNodes;
                for (const auto& instruction : plan.ValueInstructions)
                {
                    const VfxGraphNode* sourceNode = nullptr;
                    for (const auto& graphSystem : definition.Systems)
                    {
                        const auto node = std::ranges::find(graphSystem.Nodes, instruction.Node, &VfxGraphNode::Id);
                        if (node != graphSystem.Nodes.end())
                        {
                            sourceNode = std::addressof(*node);
                            break;
                        }
                    }
                    const auto* descriptor = sourceNode ? FindVfxNodeDescriptor(sourceNode->TypeId.View()) : nullptr;
                    if (!descriptor || descriptor->Class != VfxNodeClass::Operator)
                        throw std::logic_error("VFX value instruction has no catalog descriptor.");
                    const auto unsupported =
                        (backend == VfxBackend::Gpu && descriptor->BackendTier == VfxNodeBackendTier::CpuOnly) ||
                        (backend == VfxBackend::Cpu && descriptor->BackendTier == VfxNodeBackendTier::GpuRequired);
                    if (!unsupported || !reportedBackendNodes.insert(instruction.Node).second)
                        continue;
                    result.Diagnostics.push_back(
                        {VfxCompileDiagnosticSeverity::Error, instruction.Node,
                         backend == VfxBackend::Gpu
                             ? "This VFX Operator is CPU-only until its GPU semantics pass differential validation."
                             : "This VFX Operator requires the GPU backend."});
                }
                if (backend == VfxBackend::Gpu)
                {
                    const auto shaderModuleProperty = [](const VfxModuleProperty property) noexcept
                    {
                        switch (property)
                        {
                        case VfxModuleProperty::ShapeBoxHalfExtent:
                        case VfxModuleProperty::ShapeRadius:
                        case VfxModuleProperty::ShapeConeAngleDegrees:
                        case VfxModuleProperty::ShapeConeLength:
                        case VfxModuleProperty::InitializeLifetimeMinimum:
                        case VfxModuleProperty::InitializeLifetimeMaximum:
                        case VfxModuleProperty::InitializeVelocityMinimum:
                        case VfxModuleProperty::InitializeVelocityMaximum:
                        case VfxModuleProperty::InitializeRotationMinimum:
                        case VfxModuleProperty::InitializeRotationMaximum:
                        case VfxModuleProperty::ForceVector:
                        case VfxModuleProperty::ForceGravityMultiplier:
                        case VfxModuleProperty::SizeConstant:
                        case VfxModuleProperty::ColorConstant:
                        case VfxModuleProperty::CollisionRestitution:
                        case VfxModuleProperty::CollisionKillOnCollision:
                        case VfxModuleProperty::KillShapeCenter:
                        case VfxModuleProperty::KillShapeBoxHalfExtent:
                        case VfxModuleProperty::KillShapeRadius:
                        case VfxModuleProperty::KillShapeInverted:
                            return true;
                        case VfxModuleProperty::None:
                        case VfxModuleProperty::EmissionParticlesPerSecond:
                        case VfxModuleProperty::BurstTime:
                        case VfxModuleProperty::BurstCount:
                        case VfxModuleProperty::BurstCycles:
                        case VfxModuleProperty::BurstInterval:
                        case VfxModuleProperty::ShapeMesh:
                        case VfxModuleProperty::ShapeVolume:
                        case VfxModuleProperty::RendererSprite:
                        case VfxModuleProperty::RendererMesh:
                        case VfxModuleProperty::RendererMaterial:
                            return false;
                        }
                        return false;
                    };
                    std::set<AssetId> reportedNodes;
                    for (const auto& binding : plan.Bindings)
                    {
                        if (binding.ValueRegister == ~std::uint32_t{0})
                            continue;
                        const auto instruction = std::ranges::find(plan.ValueInstructions, binding.ValueRegister,
                                                                   &VfxCompiledValueInstruction::OutputRegister);
                        if (instruction == plan.ValueInstructions.end())
                            throw std::logic_error("VFX Block binding references an unknown value register.");
                        if (instruction->Domain <= VfxEvaluationDomain::PerFrame ||
                            shaderModuleProperty(binding.Property) || !reportedNodes.insert(instruction->Node).second)
                        {
                            continue;
                        }
                        result.Diagnostics.push_back(
                            {VfxCompileDiagnosticSeverity::Error, instruction->Node,
                             "This per-particle GPU expression targets a host-evaluated Block property. Particle-stage "
                             "bindings are supported for Shape, Initialize, Force, Size, Color, Collision, and Kill "
                             "Shape inputs; emission schedules and resource selections remain per-effect values."});
                    }
                }
                result.CanonicalIr = BuildCanonicalIr(definition, plan);
                result.Hash = HashBytes(result.CanonicalIr);
                result.StateLayoutHash = BuildStateLayoutHash(definition, plan);
                result.Parameters = std::move(plan.Parameters);
                result.Modules = std::move(plan.Modules);
                result.Bindings = std::move(plan.Bindings);
                result.ValueInstructions = std::move(plan.ValueInstructions);
                result.ValueRegisterCount = plan.ValueRegisterCount;
                if (backend == VfxBackend::Gpu)
                {
                    result.GpuValueProgram =
                        BuildGpuValueProgram(result.ValueInstructions, result.ValueRegisterCount,
                                             static_cast<std::uint32_t>(result.Parameters.size()), result.System);
                }
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
                const auto executable = Internal::ResolveVfxExecutableDefinition(definition, result, defaultParameters);
                if (backend == VfxBackend::Gpu)
                    AppendGpuCapabilityDiagnostics(executable, result, UsesStrictSchemaFourCapabilities(definition),
                                                   result.Diagnostics);
                if (backend == VfxBackend::Cpu)
                {
                    AppendCpuCapabilityDiagnostics(executable, result, UsesStrictSchemaFourCapabilities(definition),
                                                   result.Diagnostics);
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
                if (std::ranges::any_of(result.Diagnostics, [](const VfxCompileDiagnostic& diagnostic)
                                        { return diagnostic.Severity == VfxCompileDiagnosticSeverity::Error; }))
                    return result;
                result.Valid = true;
            }
            catch (const VfxNodeCompileError& error)
            {
                result.Diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, error.Node(), error.what()});
            }
            catch (const std::exception& error)
            {
                result.Diagnostics.push_back({VfxCompileDiagnosticSeverity::Error, {}, error.what()});
            }
            return result;
        }
    } // namespace

    VfxCompiledProgram CompileVfxEffect(const VfxEffectDefinition& definition, const VfxBackend backend)
    {
        if (definition.ExecutionSource == VfxExecutionSource::Graph && definition.Systems.size() != 1)
        {
            VfxCompiledProgram result;
            result.Backend = backend;
            result.Diagnostics.push_back(
                {VfxCompileDiagnosticSeverity::Error,
                 {},
                 "CompileVfxEffect requires one graph system; use CompileVfxEffectSystems for multi-system assets."});
            return result;
        }
        const auto* system =
            definition.ExecutionSource == VfxExecutionSource::Graph ? &definition.Systems.front() : nullptr;
        return CompileVfxEffectSystem(definition, backend, system);
    }

    std::vector<VfxCompiledProgram> CompileVfxEffectSystems(const VfxEffectDefinition& definition,
                                                            const VfxBackend backend)
    {
        if (definition.ExecutionSource != VfxExecutionSource::Graph)
            return {CompileVfxEffectSystem(definition, backend, nullptr)};
        std::vector<VfxCompiledProgram> result;
        result.reserve(definition.Systems.size());
        for (const auto& system : definition.Systems)
            result.push_back(CompileVfxEffectSystem(definition, backend, std::addressof(system)));
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
        result.TypeId.Value = ModuleTypeId(module.Payload);
        result.DefinitionVersion = ModuleDefinitionVersion(module.Payload);
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

    VfxGraphBlock CreateVfxGraphBlock(const VfxModuleDefinition& module)
    {
        if (!module.Id)
            throw std::invalid_argument("VFX Blocks require a valid Runtime Module payload reference.");
        VfxGraphBlock result;
        result.Id = AssetId::Generate();
        result.TypeId.Value = ModuleTypeId(module.Payload);
        result.Type = std::string(ModuleTypeName(module.Payload));
        result.Enabled = module.Enabled;
        result.Reference = module.Id;
        result.DefinitionVersion = ModuleDefinitionVersion(module.Payload);
        const auto specifications = ModulePinSpecifications(module.Payload);
        result.Pins.reserve(specifications.size());
        for (const auto& specification : specifications)
        {
            result.Pins.push_back({AssetId::Generate(), std::string(specification.Name), specification.Type, true,
                                   std::string(specification.Semantic), specification.DefaultValue});
        }
        return result;
    }

    VfxGraphBlock CreateVfxGraphPortableHlslBlock(std::string source)
    {
        if (source.empty() || source.size() > MaximumDocumentBytes)
            throw std::invalid_argument("Portable Custom HLSL Blocks require bounded non-empty source.");
        VfxGraphBlock result;
        result.Id = AssetId::Generate();
        result.TypeId.Value = "keire.block.portable-hlsl";
        result.Type = "Portable Custom HLSL";
        result.Properties.push_back({"Source", std::move(source)});
        return result;
    }

    VfxEffectDefinition MigrateVfxEffectToSchema4(const VfxEffectDefinition& definition)
    {
        if (definition.SchemaVersion < 1 || definition.SchemaVersion > CurrentVfxSchemaVersion)
            throw std::invalid_argument("VFX effect migration source schema is unsupported.");

        auto result = definition;
        if (result.SchemaVersion < CurrentVfxSchemaVersion)
        {
            result.CompatibilityMode = VfxCompatibilityMode::MigratedLegacyModules;
            if (result.SchemaVersion < 3)
                result.ExecutionSource = VfxExecutionSource::LegacyModules;
            for (auto& system : result.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    node.TypeId = MigratedNodeTypeId(result, node);
                    node.DefinitionVersion = 1;
                    for (auto& block : node.Blocks)
                    {
                        if (const auto module =
                                std::ranges::find(result.Modules, block.Reference, &VfxModuleDefinition::Id);
                            module != result.Modules.end())
                        {
                            block.TypeId.Value = ModuleTypeId(module->Payload);
                        }
                        else
                            block.TypeId.Value = "keire.block." + TypeSlug(block.Type);
                        block.DefinitionVersion = 1;
                    }
                }
            }
            MigrateLegacyExecutableNodesToBlocks(result);
            result.SchemaVersion = CurrentVfxSchemaVersion;
        }
        UpgradeModuleGraphLayouts(result);
        return result;
    }

    VfxEffectDefinition ConvertVfxEffectToGraph(const VfxEffectDefinition& definition)
    {
        auto result = MigrateVfxEffectToSchema4(definition);
        ValidateVfxEffect(result);
        if (result.ExecutionSource == VfxExecutionSource::Graph)
            return result;
        result.ExecutionSource = VfxExecutionSource::Graph;
        result.CompatibilityMode = VfxCompatibilityMode::MigratedLegacyModules;
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
            node.TypeId.Value = ContextTypeId(context);
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
            for (const auto& module : result.Modules)
            {
                if (ModuleContext(module.Payload) != context)
                    continue;
                auto block = CreateVfxGraphBlock(module);
                block.Id = AllocateDerivedId(module.Id, 0x3000, used);
                for (std::size_t pinIndex = 0; pinIndex < block.Pins.size(); ++pinIndex)
                    block.Pins[pinIndex].Id = AllocateDerivedId(module.Id, 0x3101 + pinIndex, used);
                node.Blocks.push_back(std::move(block));
            }
            system.Nodes.push_back(std::move(node));
        };

        appendContext(VfxContextType::Spawn);
        appendContext(VfxContextType::Initialize);
        appendContext(VfxContextType::Update);
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
            node.TypeId.Value = "keire.parameter";
            node.Pins.push_back({AllocateDerivedId(parameter->Id, 0x4100, used), parameter->Name, parameter->Type,
                                 false, "value", std::nullopt});
            system.Nodes.push_back(std::move(node));
            parameterY += 150.0F;
        }
        result.Systems.push_back(std::move(system));
        ValidateVfxEffect(result);
        return result;
    }

    Ref<VfxEffectAsset> VfxEffectAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("VFX effect asset is empty or exceeds the 4 MiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            const auto schemaVersion = document.value("schemaVersion", 0U);
            if (!document.is_object() || schemaVersion < 1U || schemaVersion > CurrentVfxSchemaVersion)
                throw std::runtime_error("VFX effect asset has an unsupported schema.");

            VfxEffectDefinition definition;
            definition.SchemaVersion = schemaVersion;
            definition.ExecutionSource = schemaVersion < 3
                                             ? VfxExecutionSource::LegacyModules
                                             : ParseExecutionSource(document.at("executionSource").get<std::string>());
            definition.CompatibilityMode =
                schemaVersion < CurrentVfxSchemaVersion
                    ? VfxCompatibilityMode::MigratedLegacyModules
                    : ParseCompatibilityMode(document.value("compatibilityMode", std::string("nativeSchema4")));
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
            return CreateRef<VfxEffectAsset>(MigrateVfxEffectToSchema4(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("VFX effect asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> VfxEffectAsset::Encode(const VfxEffectDefinition& definition)
    {
        auto published = MigrateVfxEffectToSchema4(definition);
        ValidateVfxEffect(published);
        auto modules = Json::array();
        for (const auto& module : published.Modules)
            modules.push_back(EncodeModule(module));
        const Json document{{"schemaVersion", CurrentVfxSchemaVersion},
                            {"emitterId", IdText(published.EmitterId)},
                            {"name", published.Name},
                            {"loop", published.Loop},
                            {"duration", published.Duration},
                            {"space", SpaceName(published.Space)},
                            {"seed", published.Seed},
                            {"capacity", published.Capacity},
                            {"executionSource", ExecutionSourceName(published.ExecutionSource)},
                            {"compatibilityMode", CompatibilityModeName(published.CompatibilityMode)},
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
        result.Version = VfxEffectImporterVersion;
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
