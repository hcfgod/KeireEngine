#pragma once

#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/VfxEffectPanelModel.h"
#include "KeireClient/Editor/VfxNodeCatalog.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace KeireEditor
{
    namespace
    {
        using Detail::BurstCount;
        using Detail::ContextColor;
        using Detail::ModuleName;
        using Detail::ModuleRunsInContext;
        using Detail::NewContextNode;
        using Detail::NewCustomHlslNode;
        using Detail::NewParameterNode;
        using Detail::NodeColor;
        using Detail::NodeLabel;
        using Detail::PinColor;
        using Detail::PreferredCanvasId;

        template <typename... Ts> struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

        template <typename Value> struct EnumEntry
        {
            Value Type;
            std::string_view Name;
        };

        constexpr std::array SimulationSpaces{
            EnumEntry{Keire::VfxSimulationSpace::Local, std::string_view("Local")},
            EnumEntry{Keire::VfxSimulationSpace::World, std::string_view("World")},
        };
        constexpr std::array ParticleDataTypes{
            EnumEntry{Keire::VfxParticleDataType::Particle, std::string_view("Particle")},
            EnumEntry{Keire::VfxParticleDataType::ParticleStrip, std::string_view("Particle Strip")},
        };
        constexpr std::array PreviewBackends{
            EnumEntry{Keire::VfxBackend::Cpu, std::string_view("CPU (Authoring)")},
            EnumEntry{Keire::VfxBackend::Gpu, std::string_view("GPU (Runtime)")},
        };
        constexpr std::array ContextTypes{
            EnumEntry{Keire::VfxContextType::Spawn, std::string_view("Spawn")},
            EnumEntry{Keire::VfxContextType::Initialize, std::string_view("Initialize")},
            EnumEntry{Keire::VfxContextType::Update, std::string_view("Update")},
            EnumEntry{Keire::VfxContextType::Output, std::string_view("Output")},
            EnumEntry{Keire::VfxContextType::Event, std::string_view("Event")},
        };
        constexpr std::array ValueTypes{
            EnumEntry{Keire::VfxValueType::Boolean, std::string_view("Boolean")},
            EnumEntry{Keire::VfxValueType::Integer, std::string_view("Integer")},
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
            EnumEntry{Keire::VfxValueType::Texture, std::string_view("Texture")},
            EnumEntry{Keire::VfxValueType::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxValueType::Asset, std::string_view("Asset")},
        };
        constexpr std::array GraphValueTypes{
            EnumEntry{Keire::VfxValueType::Boolean, std::string_view("Boolean")},
            EnumEntry{Keire::VfxValueType::Integer, std::string_view("Integer")},
            EnumEntry{Keire::VfxValueType::UnsignedInteger, std::string_view("Unsigned Integer")},
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Vector4, std::string_view("Vector 4")},
            EnumEntry{Keire::VfxValueType::Quaternion, std::string_view("Quaternion")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
            EnumEntry{Keire::VfxValueType::Matrix, std::string_view("Matrix")},
            EnumEntry{Keire::VfxValueType::Curve, std::string_view("Curve")},
            EnumEntry{Keire::VfxValueType::Gradient, std::string_view("Gradient")},
            EnumEntry{Keire::VfxValueType::ScalarRange, std::string_view("Scalar Range")},
            EnumEntry{Keire::VfxValueType::IntegerRange, std::string_view("Integer Range")},
            EnumEntry{Keire::VfxValueType::UnsignedIntegerRange, std::string_view("Unsigned Integer Range")},
            EnumEntry{Keire::VfxValueType::Vector2Range, std::string_view("Vector 2 Range")},
            EnumEntry{Keire::VfxValueType::Vector3Range, std::string_view("Vector 3 Range")},
            EnumEntry{Keire::VfxValueType::Vector4Range, std::string_view("Vector 4 Range")},
            EnumEntry{Keire::VfxValueType::ColorRange, std::string_view("Color Range")},
            EnumEntry{Keire::VfxValueType::Texture, std::string_view("Texture")},
            EnumEntry{Keire::VfxValueType::Texture2DArray, std::string_view("Texture 2D Array")},
            EnumEntry{Keire::VfxValueType::Texture3D, std::string_view("Texture 3D")},
            EnumEntry{Keire::VfxValueType::TextureCube, std::string_view("Texture Cube")},
            EnumEntry{Keire::VfxValueType::Mesh, std::string_view("Mesh")},
            EnumEntry{Keire::VfxValueType::Buffer, std::string_view("Buffer")},
            EnumEntry{Keire::VfxValueType::PointCache, std::string_view("Point Cache")},
            EnumEntry{Keire::VfxValueType::SignedDistanceField, std::string_view("Signed Distance Field")},
            EnumEntry{Keire::VfxValueType::Asset, std::string_view("Asset")},
            EnumEntry{Keire::VfxValueType::ParticleStream, std::string_view("Particle Stream")},
        };
        constexpr std::array CustomHlslValueTypes{
            EnumEntry{Keire::VfxValueType::Scalar, std::string_view("Scalar")},
            EnumEntry{Keire::VfxValueType::Vector2, std::string_view("Vector 2")},
            EnumEntry{Keire::VfxValueType::Vector3, std::string_view("Vector 3")},
            EnumEntry{Keire::VfxValueType::Vector4, std::string_view("Vector 4")},
            EnumEntry{Keire::VfxValueType::Color, std::string_view("Color")},
        };
        template <typename Value, std::size_t Size>
        [[nodiscard]] std::string_view EnumName(const Value value, const std::array<EnumEntry<Value>, Size>& entries)
        {
            const auto found = std::ranges::find(entries, value, &EnumEntry<Value>::Type);
            return found == entries.end() ? std::string_view("Unsupported") : found->Name;
        }
        template <typename Value, std::size_t Size>
        [[nodiscard]] bool DrawEnum(Keire::UiFrame& ui, const std::string_view label, Value& value,
                                    const std::array<EnumEntry<Value>, Size>& entries)
        {
            bool changed = false;
            if (auto combo = ui.BeginCombo(label, EnumName(value, entries)); combo)
            {
                for (const auto& entry : entries)
                {
                    if (ui.Selectable(entry.Name, entry.Type == value))
                    {
                        value = entry.Type;
                        changed = true;
                    }
                }
            }
            return changed;
        }
        template <typename Module> [[nodiscard]] bool ContainsModule(const Keire::VfxEffectDefinition& definition)
        {
            return std::ranges::any_of(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                       { return std::holds_alternative<Module>(module.Payload); });
        }

        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueName(const Range& values, const std::string_view base, Projection projection)
        {
            std::string candidate(base);
            for (std::size_t suffix = 2; std::ranges::any_of(values, [&](const auto& value)
                                                             { return std::invoke(projection, value) == candidate; });
                 ++suffix)
            {
                candidate = std::string(base) + " " + std::to_string(suffix);
            }
            return candidate;
        }
    } // namespace
} // namespace KeireEditor
