#include "Keire/Vfx/VfxSubgraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include "KeireInternal/Authoring/GraphAuthoringSerialization.h"
#include "KeireInternal/Vfx/VfxAssetCompilerInternal.h"
#include "KeireInternal/Vfx/VfxAssetValueCodec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <map>
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
        const auto KillShapeModeName = Detail::VfxKillShapeModeName;
        const auto ParseKillShapeMode = Detail::ParseVfxKillShapeMode;
        const auto EncodeCurve = Detail::EncodeVfxCurve;
        const auto DecodeCurve = Detail::DecodeVfxCurve;
        const auto EncodeGradient = Detail::EncodeVfxGradient;
        const auto DecodeGradient = Detail::DecodeVfxGradient;

        using Detail::ContextName;
        using Detail::HashBytes;
        using Detail::LoweredPlan;
        using Detail::MaximumDocumentBytes;
        using Detail::MaximumGraphRoutingPointsPerConnection;
        using Detail::MaximumModules;
        using Detail::ModuleTypeName;
        using Detail::Overloaded;
        using Detail::ValueTypeName;
        using Detail::VfxEffectImporterVersion;

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

    } // namespace

    namespace Detail
    {
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

    } // namespace Detail

    namespace
    {
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

    } // namespace

    namespace Detail
    {
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
    } // namespace Detail

    namespace
    {

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

    } // namespace

    namespace Detail
    {
        [[nodiscard]] bool UsesStrictSchemaFourCapabilities(const VfxEffectDefinition& definition) noexcept
        {
            return definition.SchemaVersion >= CurrentVfxSchemaVersion &&
                   definition.ExecutionSource == VfxExecutionSource::Graph &&
                   definition.CompatibilityMode == VfxCompatibilityMode::NativeSchema4;
        }
    } // namespace Detail

    namespace
    {

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

    } // namespace

    namespace Detail
    {
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
    } // namespace Detail

    namespace
    {

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
                {
                    auto routing = Json::array();
                    for (const auto point : connection.RoutingPoints)
                        routing.push_back({point.X, point.Y});
                    connections.push_back({{"id", IdText(connection.Id)},
                                           {"outputNode", IdText(connection.OutputNode)},
                                           {"outputBlock", IdText(connection.OutputBlock)},
                                           {"outputPin", IdText(connection.OutputPin)},
                                           {"inputNode", IdText(connection.InputNode)},
                                           {"inputBlock", IdText(connection.InputBlock)},
                                           {"inputPin", IdText(connection.InputPin)},
                                           {"routing", std::move(routing)}});
                }
                encodedSystems.push_back(
                    {{"id", IdText(system.Id)},
                     {"name", system.Name},
                     {"dataType",
                      system.DataType == VfxParticleDataType::ParticleStrip ? "particle-strip" : "particle"},
                     {"particlesPerStrip", system.ParticlesPerStrip},
                     {"nodes", std::move(nodes)},
                     {"connections", std::move(connections)},
                     {"authoring", Detail::EncodeGraphAuthoringMetadata(system.Authoring)}});
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
                    const auto& routing = encodedConnection.value("routing", Json::array());
                    if (!routing.is_array() || routing.size() > MaximumGraphRoutingPointsPerConnection)
                        throw std::runtime_error("VFX cable routing points exceed their bounds.");
                    for (const auto& point : routing)
                    {
                        if (!point.is_array() || point.size() != 2)
                            throw std::runtime_error("VFX cable routing point is invalid.");
                        connection.RoutingPoints.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
                    }
                    system.Connections.push_back(connection);
                }
                if (schemaVersion >= 5)
                {
                    std::vector<AssetId> nodeIds;
                    nodeIds.reserve(system.Nodes.size());
                    std::ranges::transform(system.Nodes, std::back_inserter(nodeIds), &VfxGraphNode::Id);
                    system.Authoring =
                        Detail::DecodeGraphAuthoringMetadata(encodedSystem.value("authoring", Json::object()), nodeIds);
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
        [[nodiscard]] std::vector<std::byte> JsonBytes(const Json& value)
        {
            const auto encoded = value.dump();
            std::vector<std::byte> result(encoded.size());
            std::memcpy(result.data(), encoded.data(), encoded.size());
            return result;
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

        [[nodiscard]] std::vector<std::byte> BuildCanonicalIrImpl(const VfxEffectDefinition& definition,
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

        [[nodiscard]] std::uint64_t BuildStateLayoutHashImpl(const VfxEffectDefinition& definition,
                                                             const LoweredPlan& plan)
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
    } // namespace

    namespace Detail
    {
        std::vector<std::byte> BuildCanonicalIr(const VfxEffectDefinition& definition, const LoweredPlan& plan)
        {
            return BuildCanonicalIrImpl(definition, plan);
        }

        std::uint64_t BuildStateLayoutHash(const VfxEffectDefinition& definition, const LoweredPlan& plan)
        {
            return BuildStateLayoutHashImpl(definition, plan);
        }
    } // namespace Detail

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
                schemaVersion < 4
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
            return CreateRef<VfxEffectAsset>(MigrateVfxEffectToCurrentSchema(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("VFX effect asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> VfxEffectAsset::Encode(const VfxEffectDefinition& definition)
    {
        auto published = MigrateVfxEffectToCurrentSchema(definition);
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
