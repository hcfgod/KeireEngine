#include "Keire/ECS/Components/BuoyancyComponent.h"

#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Keire
{
    namespace
    {
        template <typename T>
        T ReadProperty(const ComponentPropertyBag& values, const std::string_view key, const T& fallback)
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
            throw std::invalid_argument("Buoyancy property has an incompatible type.");
        }

        std::string ProbeKey(const std::size_t index, const char* field)
        {
            return "probe" + std::to_string(index) + "." + field;
        }
    } // namespace

    BuoyancyComponent::BuoyancyComponent() : Component(StaticType()) {}

    void BuoyancyComponent::Configure(const WaterHydrodynamicSettings& settings)
    {
        ValidateWaterHydrodynamicSettings(settings);
        if (settings == m_Settings)
        {
            return;
        }
        WaterHydrodynamicSettings replacement = settings;
        m_Settings = std::move(replacement);
        NotifyChanged();
    }

    void BuoyancyComponent::Reset() { Configure({}); }

    ComponentRegistration CreateBuoyancyComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = BuoyancyComponent::StaticType();
        result.Name = "Buoyancy";
        result.Category = "Physics";
        result.RequiredComponents = {TransformComponent::StaticType(), RigidBodyComponent::StaticType()};
        result.Properties = {
            {"buoyancyScale", "Buoyancy Strength", "Fluid Response", ComponentPropertyKind::Scalar, false, 0.0},
            {"dragRate", "Velocity Damping (1/s)", "Fluid Response", ComponentPropertyKind::Scalar, false, 0.0},
            {"waterLayers", "Water Layers", "Fluid Response", ComponentPropertyKind::Integer, false, 0.0,
             static_cast<double>(std::numeric_limits<std::uint32_t>::max()), 1.0},
            {"probeCount", "Hull Probe Count", "Hull", ComponentPropertyKind::Integer, false, 1.0,
             static_cast<double>(WaterHydrodynamicSettings::MaximumProbes), 1.0}};
        for (std::size_t index = 0; index < WaterHydrodynamicSettings::MaximumProbes; ++index)
        {
            const auto group = "Hull Probe " + std::to_string(index + 1);
            result.Properties.push_back(
                {ProbeKey(index, "offset"), "Offset from Center of Mass (m)", group, ComponentPropertyKind::Vector3d});
            result.Properties.push_back(
                {ProbeKey(index, "radius"), "Radius (m)", group, ComponentPropertyKind::Scalar, false, 0.001});
            result.Properties.push_back(
                {ProbeKey(index, "weight"), "Volume Weight", group, ComponentPropertyKind::Scalar, false, 0.0});
        }
        result.Factory = [] { return Ref<Component>(CreateRef<BuoyancyComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& settings = dynamic_cast<const BuoyancyComponent&>(component).Settings();
            ComponentPropertyBag values{{"buoyancyScale", settings.BuoyancyScale},
                                        {"dragRate", settings.DragRatePerSecond},
                                        {"waterLayers", static_cast<std::int64_t>(settings.WaterLayerMask)},
                                        {"probeCount", static_cast<std::int64_t>(settings.Probes.size())}};
            for (std::size_t index = 0; index < WaterHydrodynamicSettings::MaximumProbes; ++index)
            {
                const auto probe = index < settings.Probes.size() ? settings.Probes[index] : WaterHullProbe{};
                values.emplace(ProbeKey(index, "offset"), probe.Offset);
                values.emplace(ProbeKey(index, "radius"), probe.RadiusMetres);
                values.emplace(ProbeKey(index, "weight"), probe.VolumeScale);
            }
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
            {
                throw std::invalid_argument("Unsupported Buoyancy component schema version.");
            }
            WaterHydrodynamicSettings settings;
            settings.BuoyancyScale = ReadProperty(values, "buoyancyScale", settings.BuoyancyScale);
            settings.DragRatePerSecond = ReadProperty(values, "dragRate", settings.DragRatePerSecond);
            const auto layers = ReadProperty(values, "waterLayers", static_cast<std::int64_t>(settings.WaterLayerMask));
            if (layers < 0 || layers > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Buoyancy water layer mask is outside its supported range.");
            }
            settings.WaterLayerMask = static_cast<std::uint32_t>(layers);
            const auto count = ReadProperty(values, "probeCount", std::int64_t{1});
            if (count < 1 || count > static_cast<std::int64_t>(WaterHydrodynamicSettings::MaximumProbes))
            {
                throw std::invalid_argument("Buoyancy requires between one and 32 hull probes.");
            }
            settings.Probes.resize(static_cast<std::size_t>(count));
            for (std::size_t index = 0; index < WaterHydrodynamicSettings::MaximumProbes; ++index)
            {
                WaterHullProbe probe;
                probe.Offset = ReadProperty(values, ProbeKey(index, "offset"), probe.Offset);
                probe.RadiusMetres = ReadProperty(values, ProbeKey(index, "radius"), probe.RadiusMetres);
                probe.VolumeScale = ReadProperty(values, ProbeKey(index, "weight"), probe.VolumeScale);
                if (index < settings.Probes.size())
                {
                    settings.Probes[index] = probe;
                }
            }
            dynamic_cast<BuoyancyComponent&>(component).Configure(settings);
        };
        return result;
    }
} // namespace Keire
