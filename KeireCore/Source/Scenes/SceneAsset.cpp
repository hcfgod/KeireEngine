#include "Keire/Scenes/SceneAsset.h"

#include "KeireInternal/Scenes/SceneSerialization.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/ECS/Components/VfxEmitterComponent.h"
#include "KeireInternal/ECS/RetiredUiComponentTypesInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 64ULL * 1024ULL * 1024U;
        constexpr std::size_t MaximumObjects = 100'000;
        constexpr std::size_t MaximumComponentsPerEntity = 1024;
        constexpr std::size_t MaximumComponentDataBytes = 4ULL * 1024ULL * 1024U;
        constexpr std::size_t MaximumHierarchyDepth = 512;
        constexpr std::size_t MaximumNameBytes = 256;
        constexpr std::uint32_t SceneAssetImporterVersion = 8;

        [[nodiscard]] std::string_view LegacyUiComponentName(const ComponentTypeId type) noexcept
        {
            const auto* retired = Detail::FindRetiredUiComponentType(type);
            return retired ? retired->Name : std::string_view{};
        }

        void ValidateNoLegacyUiComponent(const SceneObjectDefinition& object, const SceneComponentDefinition& component)
        {
            const auto componentName = LegacyUiComponentName(component.Type);
            if (componentName.empty())
                return;
            throw std::invalid_argument("Scene entity '" + object.Name + "' (" + object.Id.ToString() +
                                        ") contains retired legacy component '" + std::string(componentName) + "' (" +
                                        component.Type.ToString() +
                                        "). Recreate this UI as a UI Document that references .keireui and "
                                        ".keireuipanel assets.");
        }

        void ValidateNoLegacySceneUi(const SceneDefinition& definition)
        {
            for (const auto& object : definition.Objects)
                for (const auto& component : object.Components)
                    ValidateNoLegacyUiComponent(object, component);

            const auto validateOverrides = [&definition](const std::vector<PrefabOverrideDefinition>& overrides)
            {
                for (const auto& override : overrides)
                {
                    if (override.AddedComponent)
                    {
                        const auto found =
                            std::ranges::find(definition.Objects, override.Object, &SceneObjectDefinition::Id);
                        const SceneObjectDefinition fallback{.Id = override.Object, .Name = "Prefab override"};
                        ValidateNoLegacyUiComponent(found == definition.Objects.end() ? fallback : *found,
                                                    *override.AddedComponent);
                    }
                    if (override.AddedObject)
                        for (const auto& component : override.AddedObject->Components)
                            ValidateNoLegacyUiComponent(*override.AddedObject, component);
                }
            };

            validateOverrides(definition.PrefabOverrides);
            for (const auto& instance : definition.PrefabInstances)
                validateOverrides(instance.Overrides);
        }

        [[nodiscard]] const Json* FindMember(const Json& value, const std::string_view canonical,
                                             const std::string_view alternate = {})
        {
            if (!value.is_object())
                return nullptr;
            if (const auto found = value.find(canonical); found != value.end())
                return std::addressof(*found);
            if (!alternate.empty())
            {
                if (const auto found = value.find(alternate); found != value.end())
                    return std::addressof(*found);
            }
            return nullptr;
        }

        void InsertDependency(std::set<AssetId>& dependencies, const AssetId dependency)
        {
            if (dependency)
                dependencies.insert(dependency);
        }

        void CollectSerializedAsset(const Json& data, const std::string_view key, std::set<AssetId>& dependencies)
        {
            const auto* value = FindMember(data, key);
            if (!value || value->is_null())
                return;
            if (value->is_array())
            {
                for (const auto& element : *value)
                    if (!element.is_null())
                        InsertDependency(dependencies, AssetId::Parse(element.get<std::string>()));
                return;
            }
            InsertDependency(dependencies, AssetId::Parse(value->get<std::string>()));
        }

        [[nodiscard]] std::optional<AssetId> ManagedAssetReferenceId(const Json& value)
        {
            if (value.is_string())
                return AssetId::Parse(value.get<std::string>());
            if (!value.is_object())
                return std::nullopt;

            const auto* id = FindMember(value, "Id", "id");
            const auto& serializedId = id ? *id : value;
            if (serializedId.is_string())
                return AssetId::Parse(serializedId.get<std::string>());
            const auto* high = FindMember(serializedId, "High", "high");
            const auto* low = FindMember(serializedId, "Low", "low");
            if (!high || !low || !high->is_number_unsigned() || !low->is_number_unsigned())
                return std::nullopt;
            return AssetId(high->get<std::uint64_t>(), low->get<std::uint64_t>());
        }

        [[nodiscard]] std::optional<AssetId> TryParseAssetId(const std::string_view value)
        {
            try
            {
                return AssetId::Parse(value);
            }
            catch (const std::invalid_argument&)
            {
                return std::nullopt;
            }
        }

        void CollectManagedAssetReferenceValue(const Json& value, std::set<AssetId>& dependencies)
        {
            if (const auto direct = ManagedAssetReferenceId(value))
            {
                InsertDependency(dependencies, *direct);
                return;
            }
            if (value.is_array())
            {
                for (const auto& element : value)
                    CollectManagedAssetReferenceValue(element, dependencies);
                return;
            }
            if (value.is_object())
                for (const auto& member : value)
                    CollectManagedAssetReferenceValue(member, dependencies);
        }

        void CollectProjectedManagedAssetDependencies(const AssetImportContext& context, const Json& data,
                                                      std::set<AssetId>& dependencies)
        {
            if (!context.ResolveAssetSource || !data.is_object())
                return;

            for (const auto& [name, value] : data.items())
            {
                if (name == "managedState" || !value.is_string())
                    continue;
                const auto candidate = TryParseAssetId(value.get_ref<const std::string&>());
                if (candidate && *candidate && context.ResolveAssetSource(*candidate))
                    dependencies.insert(*candidate);
            }
        }

        void CollectManagedStateDependencies(const AssetImportContext& context, const Json& data,
                                             std::set<AssetId>& dependencies)
        {
            const auto* serializedState = FindMember(data, "managedState");
            if (!serializedState)
                return;
            if (!serializedState->is_string())
                throw std::invalid_argument("Managed component state must be text.");

            const auto document = Json::parse(serializedState->get_ref<const std::string&>());
            const auto* fields = FindMember(document, "Fields", "fields");
            if (!fields || !fields->is_array())
                throw std::invalid_argument("Managed component state fields must be an array.");
            for (const auto& field : *fields)
            {
                const auto* type = FindMember(field, "Type", "type");
                const auto* value = FindMember(field, "Value", "value");
                if (!type || !type->is_string() || !value ||
                    type->get_ref<const std::string&>().find("Keire.AssetReference") == std::string::npos)
                {
                    continue;
                }
                CollectManagedAssetReferenceValue(*value, dependencies);
            }
            CollectProjectedManagedAssetDependencies(context, data, dependencies);
        }

        void CollectVfxParameterOverrideDependencies(const std::string& source, std::set<AssetId>& dependencies)
        {
            const auto document = Json::parse(source.empty() ? "[]" : source);
            if (!document.is_array())
                throw std::invalid_argument("VFX parameter overrides must be an array.");
            for (const auto& entry : document)
            {
                const auto* kind = FindMember(entry, "kind");
                const auto* value = FindMember(entry, "value");
                if (kind && value && kind->is_string() && kind->get_ref<const std::string&>() == "asset" &&
                    value->is_string())
                {
                    const auto text = value->get_ref<const std::string&>();
                    if (!text.empty())
                        InsertDependency(dependencies, AssetId::Parse(text));
                }
            }
        }

        void CollectComponentDependencies(const AssetImportContext& context, const SceneComponentDefinition& component,
                                          std::set<AssetId>& dependencies)
        {
            const auto data = Json::parse(component.Data);
            if (component.Type == MeshRendererComponent::StaticType())
            {
                for (const auto& [key, value] : data.items())
                {
                    if (key != "mesh" && key != "material" && !Detail::IsMeshMaterialSlotKey(key))
                        continue;
                    if (value.is_null())
                        continue;
                    const auto dependency = AssetId::Parse(value.get<std::string>());
                    if (!MeshAsset::IsBuiltin(dependency))
                        InsertDependency(dependencies, dependency);
                }
            }
            else if (component.Type == AnimatorComponent::StaticType())
            {
                for (const auto key : {"graph", "skeleton", "skinnedMesh", "avatarMask", "avatarMasks",
                                       "proceduralProfile", "rigDefinition"})
                    CollectSerializedAsset(data, key, dependencies);
            }
            else if (component.Type == ColliderComponent::StaticType())
            {
                CollectSerializedAsset(data, "collisionMesh", dependencies);
                CollectSerializedAsset(data, "physicsMaterial", dependencies);
            }
            else if (component.Type == AudioSourceComponent::StaticType())
            {
                CollectSerializedAsset(data, "clip", dependencies);
                CollectSerializedAsset(data, "mixer", dependencies);
            }
            else if (component.Type == AudioReverbZoneComponent::StaticType())
            {
                CollectSerializedAsset(data, "mixer", dependencies);
            }
            else if (component.Type == VfxEmitterComponent::StaticType())
            {
                CollectSerializedAsset(data, "effect", dependencies);
                if (const auto* overrides = FindMember(data, "parameterOverrides"); overrides)
                {
                    if (!overrides->is_string())
                        throw std::invalid_argument("VFX parameter overrides must be serialized as text.");
                    CollectVfxParameterOverrideDependencies(overrides->get_ref<const std::string&>(), dependencies);
                }
            }
            else if (component.Type == UiDocumentComponent::StaticType())
            {
                CollectSerializedAsset(data, "visualTree", dependencies);
                CollectSerializedAsset(data, "panelSettings", dependencies);
            }
            CollectManagedStateDependencies(context, data, dependencies);
        }

        void CollectObjectDependencies(const AssetImportContext& context, const SceneObjectDefinition& object,
                                       std::set<AssetId>& dependencies)
        {
            for (const auto& component : object.Components)
                CollectComponentDependencies(context, component, dependencies);
        }

        void CollectOverrideDependencies(const AssetImportContext& context,
                                         const std::vector<PrefabOverrideDefinition>& overrides,
                                         std::set<AssetId>& dependencies)
        {
            for (const auto& overrideValue : overrides)
            {
                if (overrideValue.Kind == PrefabOverrideKind::SetComponentProperty &&
                    std::holds_alternative<AssetId>(overrideValue.Value))
                {
                    InsertDependency(dependencies, std::get<AssetId>(overrideValue.Value));
                }
                if (overrideValue.Kind == PrefabOverrideKind::SetComponentProperty &&
                    overrideValue.Component == VfxEmitterComponent::StaticType() &&
                    overrideValue.Property == "parameterOverrides" &&
                    std::holds_alternative<std::string>(overrideValue.Value))
                {
                    CollectVfxParameterOverrideDependencies(std::get<std::string>(overrideValue.Value), dependencies);
                }
                if (overrideValue.AddedComponent)
                    CollectComponentDependencies(context, *overrideValue.AddedComponent, dependencies);
                if (overrideValue.AddedObject)
                    CollectObjectDependencies(context, *overrideValue.AddedObject, dependencies);
            }
        }

        [[nodiscard]] std::vector<AssetId> AuthoredDependencies(const AssetImportContext& context,
                                                                const SceneDefinition& definition)
        {
            std::set<AssetId> unique;
            for (const auto& instance : definition.PrefabInstances)
            {
                InsertDependency(unique, instance.Prefab);
                CollectOverrideDependencies(context, instance.Overrides, unique);
            }
            CollectOverrideDependencies(context, definition.PrefabOverrides, unique);
            for (const auto& object : definition.Objects)
                CollectObjectDependencies(context, object, unique);
            InsertDependency(unique, definition.BakedLighting);
            return {unique.begin(), unique.end()};
        }

        [[nodiscard]] LightingBakeSettings ParseLightingSettings(const Json& value)
        {
            if (!value.is_object())
                throw std::runtime_error("Scene lighting settings must be an object.");
            LightingBakeSettings result;
            result.Backend = static_cast<LightingBakeBackend>(value.value("backend", 0U));
            result.Quality = static_cast<LightingBakeQuality>(value.value("quality", 1U));
            result.LightmapResolution = value.value("lightmapResolution", 1024U);
            result.MaximumLightmapResolution = value.value("maximumLightmapResolution", 4096U);
            result.TexelsPerUnit = value.value("texelsPerUnit", 32U);
            result.PaddingTexels = value.value("paddingTexels", 4U);
            result.IndirectBounceCount = value.value("indirectBounceCount", 2U);
            result.SamplesPerTexel = value.value("samplesPerTexel", 64U);
            result.BakeAmbientOcclusion = value.value("bakeAmbientOcclusion", true);
            result.Denoise = value.value("denoise", true);
            return result;
        }

        [[nodiscard]] Json EncodeLightingSettings(const LightingBakeSettings& value)
        {
            return {{"backend", static_cast<std::uint8_t>(value.Backend)},
                    {"quality", static_cast<std::uint8_t>(value.Quality)},
                    {"lightmapResolution", value.LightmapResolution},
                    {"maximumLightmapResolution", value.MaximumLightmapResolution},
                    {"texelsPerUnit", value.TexelsPerUnit},
                    {"paddingTexels", value.PaddingTexels},
                    {"indirectBounceCount", value.IndirectBounceCount},
                    {"samplesPerTexel", value.SamplesPerTexel},
                    {"bakeAmbientOcclusion", value.BakeAmbientOcclusion},
                    {"denoise", value.Denoise}};
        }

        [[nodiscard]] SceneVector3 ParseVector3(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("Scene vector must contain exactly three numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }

        [[nodiscard]] SceneQuaternion ParseQuaternion(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("Scene quaternion must contain exactly four numbers.");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
        }

        [[nodiscard]] Json EncodeVector3(const SceneVector3& value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Json EncodeQuaternion(const SceneQuaternion& value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Json TransformData(const SceneTransform& transform)
        {
            return {{"position", EncodeVector3(transform.Position)},
                    {"rotation", EncodeQuaternion(transform.Rotation)},
                    {"scale", EncodeVector3(transform.Scale)}};
        }

        [[nodiscard]] SceneTransform ParseTransformData(const Json& data)
        {
            if (!data.is_object())
                throw std::runtime_error("Transform component data must be an object.");
            return {ParseVector3(data.at("position")), ParseQuaternion(data.at("rotation")),
                    ParseVector3(data.at("scale"))};
        }

        [[nodiscard]] SceneComponentDefinition MakeTransformDefinition(const SceneTransform& transform)
        {
            return {TransformComponent::StaticType(), 1, true, TransformData(transform).dump()};
        }

        [[nodiscard]] std::size_t ApproximateResidentBytes(const SceneDefinition& definition) noexcept
        {
            std::size_t result = definition.Name.size() + definition.Objects.size() * sizeof(SceneObjectDefinition);
            for (const auto& object : definition.Objects)
            {
                result += object.Name.size();
                for (const auto& tag : object.Tags)
                    result += tag.size();
                for (const auto& component : object.Components)
                    result += component.Data.size() + sizeof(SceneComponentDefinition);
            }
            return result;
        }

        [[nodiscard]] SceneObjectDefinition DecodeEntity(const Json& value, const bool requireLayer = false)
        {
            SceneObjectDefinition object;
            object.Id = AssetId::Parse(value.at("id").get<std::string>());
            if (value.contains("parent") && !value["parent"].is_null())
                object.Parent = AssetId::Parse(value["parent"].get<std::string>());
            object.Name = value.at("name").get<std::string>();
            object.Active = value.value("active", true);
            if (const auto found = value.find("tags"); found != value.end())
            {
                if (!found->is_array() || found->size() > MaximumEntityTagCount)
                    throw std::runtime_error("Scene entity tags must be a bounded array.");
                object.Tags.reserve(found->size());
                for (const auto& tag : *found)
                    object.Tags.push_back(tag.get<std::string>());
            }
            if (requireLayer && !value.contains("layer"))
                throw std::runtime_error("Scene schema v4 entity is missing its layer.");
            std::optional<std::uint32_t> legacyLayer;
            const auto& components = value.at("components");
            if (!components.is_array() || components.size() > MaximumComponentsPerEntity)
                throw std::runtime_error("Scene entity components must be a bounded array.");
            object.Components.reserve(components.size());
            for (const auto& serialized : components)
            {
                if (!serialized.is_object())
                    throw std::runtime_error("Scene component record must be an object.");
                SceneComponentDefinition component;
                component.Type = ComponentTypeId::Parse(serialized.at("type").get<std::string>());
                component.SchemaVersion = serialized.at("version").get<std::uint32_t>();
                component.Enabled = serialized.value("enabled", true);
                component.Data = serialized.at("data").dump();
                if (component.Type == TransformComponent::StaticType())
                    object.Transform = ParseTransformData(serialized.at("data"));
                if (!value.contains("layer") && !legacyLayer &&
                    (component.Type == ColliderComponent::StaticType() ||
                     component.Type == CharacterControllerComponent::StaticType()))
                {
                    const auto collisionLayer = serialized.at("data").value("layer", 1U);
                    if (std::has_single_bit(collisionLayer))
                        legacyLayer = std::countr_zero(collisionLayer);
                }
                object.Components.push_back(std::move(component));
            }
            object.Layer = value.value("layer", legacyLayer.value_or(0U));
            return object;
        }

        [[nodiscard]] Json EncodePropertyValue(const ComponentPropertyValue& value)
        {
            return std::visit(
                [](const auto& current) -> Json
                {
                    using T = std::remove_cvref_t<decltype(current)>;
                    if constexpr (std::same_as<T, Vector2>)
                        return Json::array({current.X, current.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({current.X, current.Y, current.Z});
                    else if constexpr (std::same_as<T, Vector4> || std::same_as<T, Quaternion>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({current.Red, current.Green, current.Blue, current.Alpha});
                    else if constexpr (std::same_as<T, AssetId> || std::same_as<T, EntityId>)
                        return current ? Json(current.ToString()) : Json(nullptr);
                    else if constexpr (std::same_as<T, ComponentReferenceValue>)
                        return Json{
                            {"entity", current.Entity ? Json(current.Entity.ToString()) : Json(nullptr)},
                            {"component", current.Component ? Json(current.Component.ToString()) : Json(nullptr)}};
                    else if constexpr (std::same_as<T, ComponentEventValue>)
                    {
                        Json listeners = Json::array();
                        for (const auto& listener : current.Listeners)
                        {
                            listeners.push_back(
                                {{"enabled", listener.Enabled},
                                 {"target", listener.Target ? Json(listener.Target.ToString()) : Json(nullptr)},
                                 {"component",
                                  listener.Component ? Json(listener.Component.ToString()) : Json(nullptr)},
                                 {"method", listener.Method}});
                        }
                        return listeners;
                    }
                    else if constexpr (std::same_as<T, Curve1D>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back({{"time", key.Time},
                                            {"value", key.Value},
                                            {"inTangent", key.InTangent},
                                            {"outTangent", key.OutTangent},
                                            {"interpolation", static_cast<std::uint8_t>(key.Interpolation)}});
                        }
                        return keys;
                    }
                    else if constexpr (std::same_as<T, ColorGradient>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back(
                                {{"time", key.Time},
                                 {"color", {key.Value.Red, key.Value.Green, key.Value.Blue, key.Value.Alpha}}});
                        }
                        return Json{{"interpolation", static_cast<std::uint8_t>(current.Interpolation())},
                                    {"keys", std::move(keys)}};
                    }
                    else
                        return Json(current);
                },
                value);
        }

        [[nodiscard]] ComponentPropertyValue DecodePropertyValue(const std::size_t type, const Json& value)
        {
            const auto array = [&](const std::size_t count)
            {
                if (!value.is_array() || value.size() != count)
                    throw std::runtime_error("Prefab override value has an invalid shape.");
            };
            switch (type)
            {
            case 0:
                return value.get<bool>();
            case 1:
                return value.get<std::int64_t>();
            case 2:
                return value.get<double>();
            case 3:
                return value.get<std::string>();
            case 4:
                array(2);
                return Vector2{value[0].get<float>(), value[1].get<float>()};
            case 5:
                array(3);
                return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            case 6:
                array(4);
                return Vector4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                               value[3].get<float>()};
            case 7:
                array(4);
                return Quaternion{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                  value[3].get<float>()};
            case 8:
                array(4);
                return Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                             value[3].get<float>()};
            case 9:
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            case 10:
                return value.is_null() ? EntityId{} : EntityId::Parse(value.get<std::string>());
            case 11:
            {
                if (!value.is_array())
                    throw std::runtime_error("Prefab event override must be an array.");
                ComponentEventValue result;
                result.Listeners.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Prefab event listener override must be an object.");
                    ComponentEventListener listener;
                    listener.Enabled = serialized.value("enabled", true);
                    if (const auto found = serialized.find("target"); found != serialized.end() && !found->is_null())
                        listener.Target = EntityId::Parse(found->get<std::string>());
                    if (const auto found = serialized.find("component"); found != serialized.end() && !found->is_null())
                    {
                        listener.Component = ComponentTypeId::Parse(found->get<std::string>());
                    }
                    listener.Method = serialized.value("method", std::string{});
                    result.Listeners.push_back(std::move(listener));
                }
                return result;
            }
            case 12:
            {
                if (!value.is_array())
                    throw std::runtime_error("Prefab curve override must be an array.");
                std::vector<CurveKey> keys;
                keys.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Prefab curve key must be an object.");
                    CurveKey key;
                    key.Time = serialized.at("time").get<float>();
                    key.Value = serialized.at("value").get<float>();
                    key.InTangent = serialized.value("inTangent", 0.0F);
                    key.OutTangent = serialized.value("outTangent", 0.0F);
                    const auto interpolation = serialized.value("interpolation", std::uint8_t{1});
                    if (interpolation > static_cast<std::uint8_t>(CurveInterpolation::Cubic))
                        throw std::runtime_error("Prefab curve key interpolation is invalid.");
                    key.Interpolation = static_cast<CurveInterpolation>(interpolation);
                    keys.push_back(key);
                }
                return Curve1D(std::move(keys));
            }
            case 13:
            {
                if (!value.is_object())
                    throw std::runtime_error("Prefab gradient override must be an object.");
                const auto interpolation = value.value("interpolation", std::uint8_t{0});
                if (interpolation > static_cast<std::uint8_t>(GradientInterpolation::Linear))
                    throw std::runtime_error("Prefab gradient interpolation is invalid.");
                const auto& serializedKeys = value.at("keys");
                if (!serializedKeys.is_array())
                    throw std::runtime_error("Prefab gradient keys must be an array.");
                std::vector<ColorGradientKey> keys;
                keys.reserve(serializedKeys.size());
                for (const auto& serialized : serializedKeys)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Prefab gradient key must be an object.");
                    const auto& color = serialized.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Prefab gradient color must contain four channels.");
                    keys.push_back(
                        {serialized.at("time").get<float>(),
                         {color[0].get<float>(), color[1].get<float>(), color[2].get<float>(), color[3].get<float>()}});
                }
                return ColorGradient(std::move(keys), static_cast<GradientInterpolation>(interpolation));
            }
            case 14:
            {
                if (!value.is_object())
                    throw std::runtime_error("Prefab component-reference override must be an object.");
                ComponentReferenceValue result;
                if (const auto entity = value.find("entity"); entity != value.end() && !entity->is_null())
                    result.Entity = EntityId::Parse(entity->get<std::string>());
                if (const auto component = value.find("component"); component != value.end() && !component->is_null())
                {
                    result.Component = ComponentTypeId::Parse(component->get<std::string>());
                }
                return result;
            }
            default:
                throw std::runtime_error("Prefab override value uses an unsupported type.");
            }
        }

        [[nodiscard]] Json EncodeEntity(const SceneObjectDefinition& object)
        {
            Json components = Json::array();
            bool hasTransform = false;
            for (const auto& component : object.Components)
            {
                components.push_back({{"type", component.Type.ToString()},
                                      {"version", component.SchemaVersion},
                                      {"enabled", component.Enabled},
                                      {"data", Json::parse(component.Data)}});
                hasTransform |= component.Type == TransformComponent::StaticType();
            }
            if (!hasTransform)
            {
                const auto transform = MakeTransformDefinition(object.Transform);
                Json serializedTransform{{"type", transform.Type.ToString()},
                                         {"version", transform.SchemaVersion},
                                         {"enabled", transform.Enabled},
                                         {"data", Json::parse(transform.Data)}};
                components.insert(components.begin(), std::move(serializedTransform));
            }
            Json result{{"id", object.Id.ToString()}, {"name", object.Name}, {"active", object.Active},
                        {"layer", object.Layer},      {"tags", object.Tags}, {"components", std::move(components)}};
            result["parent"] = object.Parent ? Json(object.Parent.ToString()) : Json(nullptr);
            return result;
        }

        [[nodiscard]] Json EncodeOverride(const PrefabOverrideDefinition& value)
        {
            Json result{{"kind", static_cast<std::uint8_t>(value.Kind)},
                        {"object", value.Object ? Json(value.Object.ToString()) : Json(nullptr)}};
            switch (value.Kind)
            {
            case PrefabOverrideKind::RenameObject:
                result["name"] = value.Name;
                break;
            case PrefabOverrideKind::SetObjectActive:
                result["active"] = value.Active;
                break;
            case PrefabOverrideKind::SetObjectTransform:
                result["transform"] = TransformData(value.Transform);
                break;
            case PrefabOverrideKind::SetObjectLayer:
                result["layer"] = value.Layer;
                break;
            case PrefabOverrideKind::SetObjectTags:
                result["tags"] = value.Tags;
                break;
            case PrefabOverrideKind::SetComponentProperty:
                result["component"] = value.Component.ToString();
                result["property"] = value.Property;
                result["value"] = {{"type", value.Value.index()}, {"data", EncodePropertyValue(value.Value)}};
                break;
            case PrefabOverrideKind::AddComponent:
                result["componentValue"] = {{"type", value.AddedComponent->Type.ToString()},
                                            {"version", value.AddedComponent->SchemaVersion},
                                            {"enabled", value.AddedComponent->Enabled},
                                            {"data", Json::parse(value.AddedComponent->Data)}};
                break;
            case PrefabOverrideKind::RemoveComponent:
                result["component"] = value.Component.ToString();
                break;
            case PrefabOverrideKind::AddObject:
                result["objectValue"] = EncodeEntity(*value.AddedObject);
                break;
            case PrefabOverrideKind::RemoveObject:
                break;
            }
            return result;
        }

        [[nodiscard]] PrefabOverrideDefinition DecodeOverride(const Json& value, const bool requireLayer = false)
        {
            PrefabOverrideDefinition result;
            result.Kind = static_cast<PrefabOverrideKind>(value.at("kind").get<std::uint8_t>());
            if (value.contains("object") && !value.at("object").is_null())
                result.Object = AssetId::Parse(value.at("object").get<std::string>());
            switch (result.Kind)
            {
            case PrefabOverrideKind::RenameObject:
                result.Name = value.at("name").get<std::string>();
                break;
            case PrefabOverrideKind::SetObjectActive:
                result.Active = value.at("active").get<bool>();
                break;
            case PrefabOverrideKind::SetObjectTransform:
                result.Transform = ParseTransformData(value.at("transform"));
                break;
            case PrefabOverrideKind::SetObjectLayer:
                result.Layer = value.at("layer").get<std::uint32_t>();
                break;
            case PrefabOverrideKind::SetObjectTags:
                result.Tags = value.at("tags").get<std::vector<std::string>>();
                break;
            case PrefabOverrideKind::SetComponentProperty:
                result.Component = ComponentTypeId::Parse(value.at("component").get<std::string>());
                result.Property = value.at("property").get<std::string>();
                result.Value =
                    DecodePropertyValue(value.at("value").at("type").get<std::size_t>(), value.at("value").at("data"));
                break;
            case PrefabOverrideKind::AddComponent:
            {
                const auto& component = value.at("componentValue");
                result.AddedComponent =
                    SceneComponentDefinition{ComponentTypeId::Parse(component.at("type").get<std::string>()),
                                             component.at("version").get<std::uint32_t>(),
                                             component.value("enabled", true), component.at("data").dump()};
                break;
            }
            case PrefabOverrideKind::RemoveComponent:
                result.Component = ComponentTypeId::Parse(value.at("component").get<std::string>());
                break;
            case PrefabOverrideKind::AddObject:
                result.AddedObject = DecodeEntity(value.at("objectValue"), requireLayer);
                break;
            case PrefabOverrideKind::RemoveObject:
                break;
            default:
                throw std::runtime_error("Prefab override uses an unsupported operation.");
            }
            return result;
        }

        [[nodiscard]] Json EncodeInstance(const PrefabInstanceDefinition& instance)
        {
            Json mappings = Json::array();
            for (const auto& mapping : instance.Objects)
                mappings.push_back({{"source", mapping.Source.ToString()}, {"instance", mapping.Instance.ToString()}});
            Json overrides = Json::array();
            for (const auto& value : instance.Overrides)
                overrides.push_back(EncodeOverride(value));
            return {{"prefab", instance.Prefab.ToString()},
                    {"root", instance.Root.ToString()},
                    {"objects", std::move(mappings)},
                    {"overrides", std::move(overrides)}};
        }

        [[nodiscard]] PrefabInstanceDefinition DecodeInstance(const Json& value, const bool requireLayer = false)
        {
            PrefabInstanceDefinition result;
            result.Prefab = AssetId::Parse(value.at("prefab").get<std::string>());
            result.Root = AssetId::Parse(value.at("root").get<std::string>());
            for (const auto& mapping : value.at("objects"))
                result.Objects.push_back({AssetId::Parse(mapping.at("source").get<std::string>()),
                                          AssetId::Parse(mapping.at("instance").get<std::string>())});
            for (const auto& overrideValue : value.value("overrides", Json::array()))
                result.Overrides.push_back(DecodeOverride(overrideValue, requireLayer));
            return result;
        }

        [[nodiscard]] SceneDefinition DecodeVersionOne(const Json& document)
        {
            SceneDefinition definition{.SchemaVersion = CurrentSceneSchemaVersion,
                                       .Name = document.at("name").get<std::string>()};
            const auto& objects = document.at("objects");
            if (!objects.is_array())
                throw std::runtime_error("Scene objects must be an array.");
            definition.Objects.reserve(objects.size());
            for (const auto& value : objects)
            {
                SceneObjectDefinition object;
                object.Id = AssetId::Parse(value.at("id").get<std::string>());
                if (value.contains("parent") && !value["parent"].is_null())
                    object.Parent = AssetId::Parse(value["parent"].get<std::string>());
                object.Name = value.at("name").get<std::string>();
                object.Active = value.value("active", true);
                const auto& transform = value.at("transform");
                object.Transform = {ParseVector3(transform.at("position")), ParseQuaternion(transform.at("rotation")),
                                    ParseVector3(transform.at("scale"))};
                object.Components.push_back(MakeTransformDefinition(object.Transform));
                definition.Objects.push_back(std::move(object));
            }
            return definition;
        }
    } // namespace

    SceneAsset::SceneAsset(SceneDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.Name.empty() || !m_Definition.Objects.empty())
            Validate(m_Definition);
        m_ResidentBytes = ApproximateResidentBytes(m_Definition);
    }

    std::size_t SceneAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    const SceneObjectDefinition* SceneAsset::FindObject(const AssetId id) const noexcept
    {
        const auto found = std::ranges::find(m_Definition.Objects, id, &SceneObjectDefinition::Id);
        return found == m_Definition.Objects.end() ? nullptr : &*found;
    }

    Ref<SceneAsset> SceneAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("Scene asset is empty or exceeds the supported size limit.");
        const auto* characters = reinterpret_cast<const char*>(bytes.data());
        const auto document = Json::parse(characters, characters + bytes.size());
        if (!document.is_object())
            throw std::runtime_error("Scene asset root must be an object.");

        const auto version = document.at("schemaVersion").get<std::uint32_t>();
        SceneDefinition definition;
        if (version == 1)
        {
            definition = DecodeVersionOne(document);
        }
        else if (version == 2 || version == 3 || version == 4 || version == 5 || version == CurrentSceneSchemaVersion)
        {
            definition.SchemaVersion = CurrentSceneSchemaVersion;
            definition.Name = document.at("name").get<std::string>();
            const auto& entities = document.at("entities");
            if (!entities.is_array())
                throw std::runtime_error("Scene entities must be an array.");
            definition.Objects.reserve(entities.size());
            for (const auto& value : entities)
                definition.Objects.push_back(DecodeEntity(value, version >= 4));
            if (version >= 3)
            {
                for (const auto& instance : document.value("prefabInstances", Json::array()))
                    definition.PrefabInstances.push_back(DecodeInstance(instance, version >= 4));
                for (const auto& overrideValue : document.value("prefabOverrides", Json::array()))
                    definition.PrefabOverrides.push_back(DecodeOverride(overrideValue, version >= 4));
            }
            if (version >= 5)
            {
                definition.Lighting = ParseLightingSettings(document.value("lighting", Json::object()));
                if (document.contains("bakedLighting") && !document.at("bakedLighting").is_null())
                    definition.BakedLighting = AssetId::Parse(document.at("bakedLighting").get<std::string>());
            }
        }
        else
        {
            throw std::runtime_error("Scene asset uses an unsupported schema version.");
        }
        Validate(definition);
        return CreateRef<SceneAsset>(std::move(definition));
    }

    std::vector<std::byte> SceneAsset::Encode(const SceneDefinition& definition)
    {
        Validate(definition);
        Json entities = Json::array();
        for (const auto& object : definition.Objects)
            entities.push_back(EncodeEntity(object));
        Json instances = Json::array();
        for (const auto& instance : definition.PrefabInstances)
            instances.push_back(EncodeInstance(instance));
        Json overrides = Json::array();
        for (const auto& overrideValue : definition.PrefabOverrides)
            overrides.push_back(EncodeOverride(overrideValue));
        const Json document{
            {"schemaVersion", CurrentSceneSchemaVersion},
            {"name", definition.Name},
            {"entities", std::move(entities)},
            {"prefabInstances", std::move(instances)},
            {"prefabOverrides", std::move(overrides)},
            {"lighting", EncodeLightingSettings(definition.Lighting)},
            {"bakedLighting", definition.BakedLighting ? Json(definition.BakedLighting.ToString()) : Json(nullptr)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    SceneDefinition SceneAsset::EmptyDefinition(std::string name)
    {
        return {.SchemaVersion = CurrentSceneSchemaVersion, .Name = std::move(name)};
    }

    SceneDefinition SceneAsset::SampleDefinition()
    {
        return SampleDefinition(AssetId::Parse("b1b2c3d4-1000-4000-8000-000000000002"));
    }

    SceneDefinition SceneAsset::SampleDefinition(const AssetId material)
    {
        if (!material)
            throw std::invalid_argument("Sample scene material identity must be valid.");
        SceneDefinition result = EmptyDefinition("SampleScene");
        SceneObjectDefinition camera{AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000001"),
                                     {},
                                     "Main Camera",
                                     true,
                                     {{0.0F, 1.0F, -10.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        camera.Components.push_back(MakeTransformDefinition(camera.Transform));
        camera.Components.push_back({CameraComponent::StaticType(), 1, true,
                                     Json({{"projection", 0},
                                           {"clearMode", 0},
                                           {"primary", true},
                                           {"priority", 0},
                                           {"fieldOfView", 60.0},
                                           {"orthographicSize", 10.0},
                                           {"nearPlane", 0.1},
                                           {"farPlane", 1000.0},
                                           {"clearColor", Json::array({0.10F, 0.12F, 0.16F, 1.0F})}})
                                         .dump()});
        result.Objects.push_back(std::move(camera));

        SceneObjectDefinition cube{AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000003"),
                                   {},
                                   "Cube",
                                   true,
                                   {{0.0F, 0.5F, 0.0F}, {}, {2.0F, 2.0F, 2.0F}}};
        cube.Components.push_back(MakeTransformDefinition(cube.Transform));
        cube.Components.push_back({MeshRendererComponent::StaticType(), 1, true,
                                   Json({{"mesh", MeshAsset::CubeId().ToString()},
                                         {"material", material.ToString()},
                                         {"tint", Json::array({0.25F, 0.55F, 1.0F, 1.0F})},
                                         {"visible", true}})
                                       .dump()});
        result.Objects.push_back(std::move(cube));

        SceneObjectDefinition light{
            AssetId::Parse("a1b2c3d4-1000-4000-8000-000000000002"),
            {},
            "Directional Light",
            true,
            {{0.0F, 3.0F, 0.0F}, Math::EulerDegreesToQuaternion({50.0F, -30.0F, 0.0F}), {1.0F, 1.0F, 1.0F}}};
        light.Components.push_back(MakeTransformDefinition(light.Transform));
        light.Components.push_back({DirectionalLightComponent::StaticType(), 1, true,
                                    Json({{"color", Json::array({1.0F, 1.0F, 1.0F, 1.0F})},
                                          {"intensity", 1.0},
                                          {"useTemperature", false},
                                          {"temperature", 6500.0},
                                          {"shadows", 2},
                                          {"shadowStrength", 1.0},
                                          {"shadowBias", 0.005}})
                                        .dump()});
        result.Objects.push_back(std::move(light));
        return result;
    }

    bool SceneAsset::IsValidEntityTag(const std::string_view tag) noexcept
    {
        const auto isLetter = [](const unsigned char value)
        { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z'); };
        const auto isDigit = [](const unsigned char value) { return value >= '0' && value <= '9'; };
        if (tag.empty() || tag.size() > MaximumEntityTagBytes || !isLetter(static_cast<unsigned char>(tag.front())))
            return false;
        return std::ranges::all_of(tag,
                                   [&](const char character)
                                   {
                                       const auto value = static_cast<unsigned char>(character);
                                       return isLetter(value) || isDigit(value) || character == '_' ||
                                              character == '-' || character == '.';
                                   });
    }

    void SceneAsset::Validate(const SceneDefinition& definition)
    {
        if (definition.SchemaVersion != CurrentSceneSchemaVersion)
            throw std::invalid_argument("Scene definition must use the canonical schema version.");
        if (static_cast<std::uint8_t>(definition.Lighting.Backend) >
                static_cast<std::uint8_t>(LightingBakeBackend::CPU) ||
            static_cast<std::uint8_t>(definition.Lighting.Quality) >
                static_cast<std::uint8_t>(LightingBakeQuality::Production) ||
            definition.Lighting.LightmapResolution < 64U || definition.Lighting.LightmapResolution > 16'384U ||
            definition.Lighting.MaximumLightmapResolution < definition.Lighting.LightmapResolution ||
            definition.Lighting.MaximumLightmapResolution > 16'384U || definition.Lighting.TexelsPerUnit == 0U ||
            definition.Lighting.TexelsPerUnit > 4096U || definition.Lighting.PaddingTexels > 128U ||
            definition.Lighting.IndirectBounceCount > 16U || definition.Lighting.SamplesPerTexel == 0U ||
            definition.Lighting.SamplesPerTexel > 65'536U)
            throw std::invalid_argument("Scene lighting bake settings are invalid.");
        if (definition.Name.empty() || definition.Name.size() > MaximumNameBytes)
            throw std::invalid_argument("Scene name is empty or exceeds 256 UTF-8 bytes.");
        if (definition.Objects.size() > MaximumObjects)
            throw std::invalid_argument("Scene exceeds the supported entity limit.");

        std::unordered_map<AssetId, std::size_t> depths;
        depths.reserve(definition.Objects.size());
        for (const auto& object : definition.Objects)
        {
            if (!object.Id || object.Name.empty() || object.Name.size() > MaximumNameBytes ||
                depths.contains(object.Id))
                throw std::invalid_argument("Scene entity has an invalid ID or name, or duplicates another entity.");
            if (!IsValidEntityLayer(object.Layer))
                throw std::invalid_argument("Scene entity layer must be between 0 and 31.");
            if (object.Tags.size() > MaximumEntityTagCount)
                throw std::invalid_argument("Scene entity exceeds the supported tag limit.");
            std::set<std::string, std::less<>> uniqueTags;
            for (const auto& tag : object.Tags)
                if (!IsValidEntityTag(tag) || !uniqueTags.insert(tag).second)
                    throw std::invalid_argument("Scene entity tag is invalid or duplicated.");
            std::size_t depth = 1;
            if (object.Parent)
            {
                const auto parent = depths.find(object.Parent);
                if (parent == depths.end())
                    throw std::invalid_argument("Scene parents must exist and precede their children.");
                depth = parent->second + 1;
            }
            if (depth > MaximumHierarchyDepth)
                throw std::invalid_argument("Scene hierarchy exceeds the supported depth limit.");
            if (object.Components.size() > MaximumComponentsPerEntity)
                throw std::invalid_argument("Scene entity exceeds the supported component limit.");

            std::size_t transformCount = 0;
            for (const auto& component : object.Components)
            {
                if (!component.Type || component.SchemaVersion == 0 || component.Data.empty() ||
                    component.Data.size() > MaximumComponentDataBytes)
                    throw std::invalid_argument("Scene component record is incomplete or exceeds its size limit.");
                const auto data = Json::parse(component.Data);
                if (!data.is_object())
                    throw std::invalid_argument("Scene component data must be an object.");
                if (component.Type == TransformComponent::StaticType())
                {
                    ++transformCount;
                    const auto transform = ParseTransformData(data);
                    if (!Math::IsFinite(transform.Position) || !Math::IsFinite(transform.Rotation) ||
                        !TransformComponent::IsValidLocalScale(transform.Scale) ||
                        std::abs(Math::Length(transform.Rotation) - 1.0F) > 0.001F)
                        throw std::invalid_argument("Scene Transform contains invalid or non-normalized values.");
                }
            }
            if (transformCount > 1)
                throw std::invalid_argument("Scene entity contains more than one Transform component.");

            if (!Math::IsFinite(object.Transform.Position) || !Math::IsFinite(object.Transform.Rotation) ||
                !TransformComponent::IsValidLocalScale(object.Transform.Scale) ||
                std::abs(Math::Length(object.Transform.Rotation) - 1.0F) > 0.001F)
                throw std::invalid_argument("Scene compatibility transform contains invalid values.");
            depths.emplace(object.Id, depth);
        }

        for (const auto& instance : definition.PrefabInstances)
        {
            if (!instance.Prefab || !instance.Root || !depths.contains(instance.Root) || instance.Objects.empty())
                throw std::invalid_argument("Prefab instance is incomplete or references a missing scene root.");
            std::set<AssetId> sources;
            std::set<AssetId> instances;
            for (const auto& mapping : instance.Objects)
            {
                if (!mapping.Source || !mapping.Instance || !depths.contains(mapping.Instance) ||
                    !sources.insert(mapping.Source).second || !instances.insert(mapping.Instance).second)
                    throw std::invalid_argument("Prefab instance object mapping is invalid.");
            }
            if (!instances.contains(instance.Root))
                throw std::invalid_argument("Prefab instance root must participate in its object mapping.");
        }

        const auto validateOverrides = [](const std::vector<PrefabOverrideDefinition>& overrides)
        {
            for (const auto& value : overrides)
            {
                switch (value.Kind)
                {
                case PrefabOverrideKind::RenameObject:
                    if (!value.Object || value.Name.empty() || value.Name.size() > MaximumNameBytes)
                        throw std::invalid_argument("Prefab rename override is invalid.");
                    break;
                case PrefabOverrideKind::SetObjectActive:
                    if (!value.Object)
                        throw std::invalid_argument("Prefab active override has no target.");
                    break;
                case PrefabOverrideKind::SetObjectTransform:
                    if (!value.Object || !Math::IsFinite(value.Transform.Position) ||
                        !Math::IsFinite(value.Transform.Rotation) ||
                        !TransformComponent::IsValidLocalScale(value.Transform.Scale) ||
                        std::abs(Math::Length(value.Transform.Rotation) - 1.0F) > 0.001F)
                        throw std::invalid_argument("Prefab transform override is invalid.");
                    break;
                case PrefabOverrideKind::SetObjectLayer:
                    if (!value.Object || !IsValidEntityLayer(value.Layer))
                        throw std::invalid_argument("Prefab layer override is invalid.");
                    break;
                case PrefabOverrideKind::SetObjectTags:
                {
                    std::set<std::string, std::less<>> uniqueTags;
                    if (!value.Object || value.Tags.size() > MaximumEntityTagCount)
                        throw std::invalid_argument("Prefab tag override is invalid.");
                    for (const auto& tag : value.Tags)
                        if (!SceneAsset::IsValidEntityTag(tag) || !uniqueTags.insert(tag).second)
                            throw std::invalid_argument("Prefab tag override is invalid.");
                    break;
                }
                case PrefabOverrideKind::SetComponentProperty:
                    if (!value.Object || !value.Component || value.Property.empty() || value.Property.size() > 256)
                        throw std::invalid_argument("Prefab component property override is invalid.");
                    break;
                case PrefabOverrideKind::AddComponent:
                    if (!value.Object || !value.AddedComponent || !value.AddedComponent->Type ||
                        value.AddedComponent->SchemaVersion == 0 || value.AddedComponent->Data.empty() ||
                        !Json::parse(value.AddedComponent->Data).is_object())
                        throw std::invalid_argument("Prefab add-component override is invalid.");
                    break;
                case PrefabOverrideKind::RemoveComponent:
                    if (!value.Object || !value.Component || value.Component == TransformComponent::StaticType())
                        throw std::invalid_argument("Prefab remove-component override is invalid.");
                    break;
                case PrefabOverrideKind::AddObject:
                    if (!value.AddedObject || !value.AddedObject->Id || value.AddedObject->Name.empty() ||
                        !IsValidEntityLayer(value.AddedObject->Layer))
                        throw std::invalid_argument("Prefab add-object override is invalid.");
                    if (value.AddedObject->Tags.size() > MaximumEntityTagCount)
                        throw std::invalid_argument("Prefab add-object override is invalid.");
                    {
                        std::set<std::string, std::less<>> uniqueTags;
                        for (const auto& tag : value.AddedObject->Tags)
                            if (!SceneAsset::IsValidEntityTag(tag) || !uniqueTags.insert(tag).second)
                                throw std::invalid_argument("Prefab add-object override is invalid.");
                    }
                    break;
                case PrefabOverrideKind::RemoveObject:
                    if (!value.Object)
                        throw std::invalid_argument("Prefab remove-object override has no target.");
                    break;
                default:
                    throw std::invalid_argument("Prefab override uses an unsupported operation.");
                }
            }
        };
        validateOverrides(definition.PrefabOverrides);
        for (const auto& instance : definition.PrefabInstances)
            validateOverrides(instance.Overrides);
    }

    AssetDecoderRegistration CreateSceneAssetDecoder()
    {
        return {SceneAsset::StaticType(), CreateRef<SceneAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return SceneAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateSceneAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Scene";
        result.Version = SceneAssetImporterVersion;
        result.Type = SceneAsset::StaticType();
        result.Extensions = {".keirescene"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            const auto parsed = SceneAsset::Decode(bytes);
            ValidateNoLegacySceneUi(parsed->Definition());
            AssetImportOutput output;
            output.Bytes = SceneAsset::Encode(parsed->Definition());
            output.AssetDependencies = AuthoredDependencies(context, parsed->Definition());
            return output;
        };
        return result;
    }
} // namespace Keire
