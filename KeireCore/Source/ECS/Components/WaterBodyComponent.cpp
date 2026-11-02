#include "Keire/ECS/Components/WaterBodyComponent.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Keire
{
    namespace
    {
        struct WaveProperty
        {
            const char* Key;
            const char* Label;
            double WaterWaveDefinition::* Member;
        };

        constexpr std::array WaveProperties{
            WaveProperty{"wavelength", "Wavelength (m)", &WaterWaveDefinition::WavelengthMetres},
            WaveProperty{"amplitude", "Amplitude (m)", &WaterWaveDefinition::AmplitudeMetres},
            WaveProperty{"speed", "Speed (m/s)", &WaterWaveDefinition::SpeedMetresPerSecond},
            WaveProperty{"direction", "Direction (degrees)", &WaterWaveDefinition::DirectionDegrees},
            WaveProperty{"phase", "Phase (degrees)", &WaterWaveDefinition::PhaseDegrees},
            WaveProperty{"steepness", "Steepness", &WaterWaveDefinition::Steepness}};

        template <typename T>
        [[nodiscard]] T ReadProperty(const ComponentPropertyBag& values, const std::string_view key, const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
            {
                return fallback;
            }
            if (const auto* value = std::get_if<T>(&found->second))
            {
                return *value;
            }
            throw std::invalid_argument("Water Body property has an incompatible type.");
        }

        [[nodiscard]] float ReadUnitScalar(const ComponentPropertyBag& values, const std::string_view key,
                                           const float fallback)
        {
            const auto value = ReadProperty(values, key, static_cast<double>(fallback));
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
            {
                throw std::invalid_argument("Water Body appearance values must be between zero and one.");
            }
            return static_cast<float>(value);
        }

        [[nodiscard]] std::string WaveKey(const std::size_t index, const char* field)
        {
            return "wave" + std::to_string(index) + "." + field;
        }

        [[nodiscard]] WaterSurfaceDefinition SurfaceDefinition(const WaterBodySettings& settings)
        {
            WaterSurfaceDefinition definition;
            definition.Shape = settings.Shape;
            definition.HalfExtentMetres = {settings.HalfExtentXMetres, settings.HalfExtentZMetres};
            definition.RadiusMetres = settings.RadiusMetres;
            definition.DepthMetres = settings.DepthMetres;
            definition.DensityKilogramsPerCubicMetre = settings.DensityKilogramsPerCubicMetre;
            definition.FlowVelocityLocal = settings.FlowVelocityLocal;
            return definition;
        }
    } // namespace

    bool WaterBodySettings::operator==(const WaterBodySettings& other) const noexcept
    {
        if (Material != other.Material || SurfaceColor != other.SurfaceColor || Roughness != other.Roughness ||
            CrestFoamStrength != other.CrestFoamStrength || GridSpacingMetres != other.GridSpacingMetres ||
            RenderDistanceMetres != other.RenderDistanceMetres || Shape != other.Shape ||
            HalfExtentXMetres != other.HalfExtentXMetres || HalfExtentZMetres != other.HalfExtentZMetres ||
            RadiusMetres != other.RadiusMetres || DepthMetres != other.DepthMetres ||
            DensityKilogramsPerCubicMetre != other.DensityKilogramsPerCubicMetre ||
            FlowVelocityLocal != other.FlowVelocityLocal || Waves.size() != other.Waves.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < Waves.size(); ++index)
        {
            for (const auto& property : WaveProperties)
            {
                if (Waves[index].*property.Member != other.Waves[index].*property.Member)
                {
                    return false;
                }
            }
        }
        return true;
    }

    WaterBodyComponent::WaterBodyComponent() : Component(StaticType()) {}

    void WaterBodyComponent::Validate(const WaterBodySettings& settings)
    {
        const auto color = settings.SurfaceColor;
        if (!Math::IsFinite(color) || color.Red < 0.0F || color.Red > 1.0F || color.Green < 0.0F ||
            color.Green > 1.0F || color.Blue < 0.0F || color.Blue > 1.0F || color.Alpha < 0.0F || color.Alpha > 1.0F ||
            !std::isfinite(settings.Roughness) || settings.Roughness < 0.0F || settings.Roughness > 1.0F ||
            !std::isfinite(settings.CrestFoamStrength) || settings.CrestFoamStrength < 0.0F ||
            settings.CrestFoamStrength > 1.0F)
        {
            throw std::invalid_argument("Water Body albedo, opacity, roughness and foam must be between zero and one.");
        }
        if (!std::isfinite(settings.GridSpacingMetres) || settings.GridSpacingMetres <= 0.0 ||
            !std::isfinite(settings.RenderDistanceMetres) || settings.RenderDistanceMetres <= 0.0)
        {
            throw std::invalid_argument("Water Body grid spacing and render distance must be finite and positive.");
        }
        const WaterSurface surface(SurfaceDefinition(settings));
        if (settings.Shape == WaterSurfaceShape::Sphere && !settings.Waves.empty())
        {
            throw std::invalid_argument("Water Body directional waves require a planar surface.");
        }
        const WaterWaveField waves(settings.Waves);
    }

    void WaterBodyComponent::Configure(const WaterBodySettings& settings)
    {
        Validate(settings);
        if (settings == m_Settings)
        {
            return;
        }
        // Copy before replacement so allocation failure cannot partially update the authored state.
        WaterBodySettings replacement = settings;
        m_Settings = std::move(replacement);
        NotifyChanged();
    }

    void WaterBodyComponent::Reset() { Configure({}); }

    WaterSurfaceDefinition WaterBodyComponent::CreateSurfaceDefinition(const Vector3d origin,
                                                                       const std::array<double, 4> rotation,
                                                                       const Vector3d frameLinearVelocity,
                                                                       const Vector3d frameAngularVelocity) const
    {
        auto definition = SurfaceDefinition(m_Settings);
        definition.Origin = origin;
        definition.Rotation = rotation;
        definition.FrameLinearVelocity = frameLinearVelocity;
        definition.FrameAngularVelocity = frameAngularVelocity;
        return WaterSurface(definition).Definition();
    }

    WaterWaveField WaterBodyComponent::CreateWaveField(const Vector3d localOrigin) const
    {
        return WaterWaveField(m_Settings.Waves, localOrigin);
    }

    ComponentRegistration CreateWaterBodyComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = WaterBodyComponent::StaticType();
        result.Name = "Water Body";
        result.Category = "Rendering";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"material", "Material", "Appearance", ComponentPropertyKind::Asset},
            {"surfaceColor", "Surface Albedo / Opacity", "Appearance", ComponentPropertyKind::Color},
            {"roughness", "Roughness", "Appearance", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"crestFoamStrength", "Crest Foam Strength", "Appearance", ComponentPropertyKind::Scalar, false, 0.0, 1.0,
             0.01},
            {"gridSpacing", "Target Grid Spacing (m)", "Quality", ComponentPropertyKind::Scalar, false, 0.001},
            {"renderDistance", "Render Distance (m)", "Quality", ComponentPropertyKind::Scalar, false, 0.001},
            {"shape", "Shape (Plane / Rectangle / Ellipse / Sphere)", "Geometry", ComponentPropertyKind::Integer, false,
             0.0, 3.0, 1.0},
            {"halfExtentX", "Half Extent X (m)", "Geometry", ComponentPropertyKind::Scalar, false, 0.001},
            {"halfExtentZ", "Half Extent Z (m)", "Geometry", ComponentPropertyKind::Scalar, false, 0.001},
            {"radius", "Sphere Radius (m)", "Geometry", ComponentPropertyKind::Scalar, false, 0.001},
            {"depth", "Depth (m)", "Fluid", ComponentPropertyKind::Scalar, false, 0.001},
            {"density", "Density (kg/m3)", "Fluid", ComponentPropertyKind::Scalar, false, 0.001},
            {"flowVelocity", "Local Flow (m/s)", "Fluid", ComponentPropertyKind::Vector3d},
            {"waveCount", "Wave Count", "Waves", ComponentPropertyKind::Integer, false, 0.0,
             static_cast<double>(WaterWaveField::MaximumWaves), 1.0}};
        result.Properties.front().ExpectedAssetType = MaterialAsset::StaticType();
        for (std::size_t index = 0; index < WaterWaveField::MaximumWaves; ++index)
        {
            for (const auto& property : WaveProperties)
            {
                result.Properties.push_back({WaveKey(index, property.Key), property.Label,
                                             "Wave " + std::to_string(index + 1), ComponentPropertyKind::Scalar});
            }
        }
        result.Factory = [] { return Ref<Component>(CreateRef<WaterBodyComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& settings = dynamic_cast<const WaterBodyComponent&>(component).Settings();
            ComponentPropertyBag values{{"material", settings.Material},
                                        {"surfaceColor", settings.SurfaceColor},
                                        {"roughness", static_cast<double>(settings.Roughness)},
                                        {"crestFoamStrength", static_cast<double>(settings.CrestFoamStrength)},
                                        {"gridSpacing", settings.GridSpacingMetres},
                                        {"renderDistance", settings.RenderDistanceMetres},
                                        {"shape", static_cast<std::int64_t>(settings.Shape)},
                                        {"halfExtentX", settings.HalfExtentXMetres},
                                        {"halfExtentZ", settings.HalfExtentZMetres},
                                        {"radius", settings.RadiusMetres},
                                        {"depth", settings.DepthMetres},
                                        {"density", settings.DensityKilogramsPerCubicMetre},
                                        {"flowVelocity", settings.FlowVelocityLocal},
                                        {"waveCount", static_cast<std::int64_t>(settings.Waves.size())}};
            for (std::size_t index = 0; index < WaterWaveField::MaximumWaves; ++index)
            {
                const auto wave = index < settings.Waves.size() ? settings.Waves[index] : WaterWaveDefinition{};
                for (const auto& property : WaveProperties)
                {
                    values.emplace(WaveKey(index, property.Key), wave.*property.Member);
                }
            }
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
            {
                throw std::invalid_argument("Unsupported Water Body component schema version.");
            }
            WaterBodySettings settings;
            settings.Material = ReadProperty(values, "material", settings.Material);
            settings.SurfaceColor = ReadProperty(values, "surfaceColor", settings.SurfaceColor);
            settings.Roughness = ReadUnitScalar(values, "roughness", settings.Roughness);
            settings.CrestFoamStrength = ReadUnitScalar(values, "crestFoamStrength", settings.CrestFoamStrength);
            settings.GridSpacingMetres = ReadProperty(values, "gridSpacing", settings.GridSpacingMetres);
            settings.RenderDistanceMetres = ReadProperty(values, "renderDistance", settings.RenderDistanceMetres);
            const auto shape = ReadProperty(values, "shape", static_cast<std::int64_t>(settings.Shape));
            if (shape < 0 || shape > static_cast<std::int64_t>(WaterSurfaceShape::Sphere))
            {
                throw std::invalid_argument("Water Body shape is invalid.");
            }
            settings.Shape = static_cast<WaterSurfaceShape>(shape);
            settings.HalfExtentXMetres = ReadProperty(values, "halfExtentX", settings.HalfExtentXMetres);
            settings.HalfExtentZMetres = ReadProperty(values, "halfExtentZ", settings.HalfExtentZMetres);
            settings.RadiusMetres = ReadProperty(values, "radius", settings.RadiusMetres);
            settings.DepthMetres = ReadProperty(values, "depth", settings.DepthMetres);
            settings.DensityKilogramsPerCubicMetre =
                ReadProperty(values, "density", settings.DensityKilogramsPerCubicMetre);
            settings.FlowVelocityLocal = ReadProperty(values, "flowVelocity", settings.FlowVelocityLocal);
            const auto count = ReadProperty(values, "waveCount", std::int64_t{0});
            if (count < 0 || count > static_cast<std::int64_t>(WaterWaveField::MaximumWaves))
            {
                throw std::invalid_argument("Water Body wave count exceeds the supported range.");
            }
            settings.Waves.resize(static_cast<std::size_t>(count));
            for (std::size_t index = 0; index < WaterWaveField::MaximumWaves; ++index)
            {
                WaterWaveDefinition wave;
                for (const auto& property : WaveProperties)
                {
                    wave.*property.Member = ReadProperty(values, WaveKey(index, property.Key), wave.*property.Member);
                }
                if (index < settings.Waves.size())
                {
                    settings.Waves[index] = wave;
                }
            }
            dynamic_cast<WaterBodyComponent&>(component).Configure(settings);
        };
        return result;
    }
} // namespace Keire
