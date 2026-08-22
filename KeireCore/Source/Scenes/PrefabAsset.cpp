#include "Keire/Scenes/PrefabAsset.h"

#include "Keire/ECS/Components/TransformComponent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 64ULL * 1024ULL * 1024U;
        constexpr std::size_t MaximumPrefabDepth = 128;

        [[nodiscard]] Json EncodeProperty(const ComponentPropertyValue& value)
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

        [[nodiscard]] SceneObjectDefinition* FindObject(SceneDefinition& definition, const AssetId id)
        {
            const auto found = std::ranges::find(definition.Objects, id, &SceneObjectDefinition::Id);
            return found == definition.Objects.end() ? nullptr : &*found;
        }

        void ApplyOverrides(SceneDefinition& definition, const std::vector<PrefabOverrideDefinition>& overrides)
        {
            for (const auto& overrideValue : overrides)
            {
                if (overrideValue.Kind == PrefabOverrideKind::AddObject)
                {
                    if (FindObject(definition, overrideValue.AddedObject->Id))
                        throw std::invalid_argument("Prefab override adds a duplicate object ID.");
                    definition.Objects.push_back(*overrideValue.AddedObject);
                    continue;
                }

                auto* object = FindObject(definition, overrideValue.Object);
                if (!object)
                    throw std::invalid_argument("Prefab override targets an unavailable object.");
                switch (overrideValue.Kind)
                {
                case PrefabOverrideKind::RenameObject:
                    object->Name = overrideValue.Name;
                    break;
                case PrefabOverrideKind::SetObjectActive:
                    object->Active = overrideValue.Active;
                    break;
                case PrefabOverrideKind::SetObjectTransform:
                    object->Transform = overrideValue.Transform;
                    if (const auto transform = std::ranges::find(object->Components, TransformComponent::StaticType(),
                                                                 &SceneComponentDefinition::Type);
                        transform != object->Components.end())
                    {
                        transform->Data =
                            Json({{"position",
                                   Json::array({overrideValue.Transform.Position.X, overrideValue.Transform.Position.Y,
                                                overrideValue.Transform.Position.Z})},
                                  {"rotation",
                                   Json::array({overrideValue.Transform.Rotation.X, overrideValue.Transform.Rotation.Y,
                                                overrideValue.Transform.Rotation.Z,
                                                overrideValue.Transform.Rotation.W})},
                                  {"scale",
                                   Json::array({overrideValue.Transform.Scale.X, overrideValue.Transform.Scale.Y,
                                                overrideValue.Transform.Scale.Z})}})
                                .dump();
                    }
                    break;
                case PrefabOverrideKind::SetObjectLayer:
                    object->Layer = overrideValue.Layer;
                    break;
                case PrefabOverrideKind::SetObjectTags:
                    object->Tags = overrideValue.Tags;
                    break;
                case PrefabOverrideKind::SetComponentProperty:
                {
                    const auto component =
                        std::ranges::find(object->Components, overrideValue.Component, &SceneComponentDefinition::Type);
                    if (component == object->Components.end())
                        throw std::invalid_argument("Prefab property override targets an unavailable component.");
                    auto data = Json::parse(component->Data);
                    data[overrideValue.Property] = EncodeProperty(overrideValue.Value);
                    component->Data = data.dump();
                    break;
                }
                case PrefabOverrideKind::AddComponent:
                    if (std::ranges::find(object->Components, overrideValue.AddedComponent->Type,
                                          &SceneComponentDefinition::Type) != object->Components.end())
                        throw std::invalid_argument("Prefab override adds a duplicate component.");
                    object->Components.push_back(*overrideValue.AddedComponent);
                    break;
                case PrefabOverrideKind::RemoveComponent:
                {
                    const auto component =
                        std::ranges::find(object->Components, overrideValue.Component, &SceneComponentDefinition::Type);
                    if (component == object->Components.end() || component->Type == TransformComponent::StaticType())
                        throw std::invalid_argument("Prefab override removes an unavailable or required component.");
                    object->Components.erase(component);
                    break;
                }
                case PrefabOverrideKind::RemoveObject:
                {
                    std::set<AssetId> removed{object->Id};
                    bool changed = true;
                    while (changed)
                    {
                        changed = false;
                        for (const auto& candidate : definition.Objects)
                            if (removed.contains(candidate.Parent) && removed.insert(candidate.Id).second)
                                changed = true;
                    }
                    std::erase_if(definition.Objects, [&](const SceneObjectDefinition& candidate)
                                  { return removed.contains(candidate.Id); });
                    break;
                }
                case PrefabOverrideKind::AddObject:
                    break;
                }
            }
        }

        [[nodiscard]] SceneDefinition Compose(const AssetId id, const PrefabResolver& resolver,
                                              std::vector<AssetId>& stack)
        {
            if (!id || !resolver)
                throw std::invalid_argument("Prefab composition requires a valid identity and resolver.");
            if (stack.size() >= MaximumPrefabDepth || std::ranges::find(stack, id) != stack.end())
                throw std::invalid_argument("Prefab inheritance or nesting contains a cycle.");
            const auto asset = resolver(id);
            if (!asset)
                throw std::invalid_argument("Prefab resolver could not load a required prefab.");
            PrefabAsset::Validate(asset->Definition());
            stack.push_back(id);

            const auto& source = asset->Definition();
            SceneDefinition result;
            if (source.BasePrefab)
            {
                result = Compose(source.BasePrefab, resolver, stack);
                result.Name = source.Template.Name;
                for (const auto& object : source.Template.Objects)
                {
                    if (FindObject(result, object.Id))
                        throw std::invalid_argument("Prefab variant adds a duplicate object ID.");
                    result.Objects.push_back(object);
                }
                result.PrefabInstances.insert(result.PrefabInstances.end(), source.Template.PrefabInstances.begin(),
                                              source.Template.PrefabInstances.end());
            }
            else
            {
                result = source.Template;
            }
            ApplyOverrides(result, source.Template.PrefabOverrides);
            result.PrefabOverrides.clear();

            const auto instances = std::exchange(result.PrefabInstances, {});
            for (const auto& instance : instances)
            {
                auto nested = Compose(instance.Prefab, resolver, stack);
                ApplyOverrides(nested, instance.Overrides);
                std::unordered_map<AssetId, AssetId> mappings;
                for (const auto& mapping : instance.Objects)
                    mappings.emplace(mapping.Source, mapping.Instance);
                for (auto& object : nested.Objects)
                {
                    const auto mapped = mappings.find(object.Id);
                    if (mapped == mappings.end())
                        throw std::invalid_argument("Prefab instance does not map every nested object.");
                    const auto sourceParent = object.Parent;
                    object.Id = mapped->second;
                    if (sourceParent)
                    {
                        const auto parent = mappings.find(sourceParent);
                        if (parent == mappings.end())
                            throw std::invalid_argument("Prefab instance does not map a nested parent.");
                        object.Parent = parent->second;
                    }
                    else
                    {
                        object.Parent = {};
                    }
                }
                const auto nestedRoot = std::ranges::find(nested.Objects, instance.Root, &SceneObjectDefinition::Id);
                if (nestedRoot == nested.Objects.end())
                    throw std::invalid_argument("Prefab instance root mapping is unavailable after composition.");
                for (auto& object : nested.Objects)
                {
                    if (auto* existing = FindObject(result, object.Id))
                    {
                        if (object.Id == instance.Root)
                        {
                            object.Parent = existing->Parent;
                            object.Transform = existing->Transform;
                        }
                        *existing = std::move(object);
                    }
                    else
                    {
                        result.Objects.push_back(std::move(object));
                    }
                }
            }

            stack.pop_back();
            result.SchemaVersion = CurrentSceneSchemaVersion;
            SceneAsset::Validate(result);
            return result;
        }
    } // namespace

    PrefabAsset::PrefabAsset(PrefabDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.Template.Name.empty())
            Validate(m_Definition);
        m_ResidentBytes = m_Definition.Template.Name.empty() ? 0 : SceneAsset::Encode(m_Definition.Template).size();
    }

    std::size_t PrefabAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    Ref<PrefabAsset> PrefabAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("Prefab asset is empty or exceeds the supported size limit.");
        const auto* characters = reinterpret_cast<const char*>(bytes.data());
        const auto document = Json::parse(characters, characters + bytes.size());
        if (!document.is_object() || document.value("prefabSchemaVersion", 0U) != 1)
            throw std::runtime_error("Prefab asset uses an unsupported schema version.");
        PrefabDefinition definition;
        if (document.contains("basePrefab") && !document.at("basePrefab").is_null())
            definition.BasePrefab = AssetId::Parse(document.at("basePrefab").get<std::string>());
        const auto sceneText = document.dump();
        std::vector<std::byte> sceneBytes(sceneText.size());
        std::memcpy(sceneBytes.data(), sceneText.data(), sceneText.size());
        definition.Template = SceneAsset::Decode(sceneBytes)->Definition();
        Validate(definition);
        return CreateRef<PrefabAsset>(std::move(definition));
    }

    std::vector<std::byte> PrefabAsset::Encode(const PrefabDefinition& definition)
    {
        Validate(definition);
        const auto sceneBytes = SceneAsset::Encode(definition.Template);
        const auto* characters = reinterpret_cast<const char*>(sceneBytes.data());
        auto document = Json::parse(characters, characters + sceneBytes.size());
        document["prefabSchemaVersion"] = 1;
        document["basePrefab"] = definition.BasePrefab ? Json(definition.BasePrefab.ToString()) : Json(nullptr);
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    void PrefabAsset::Validate(const PrefabDefinition& definition)
    {
        if (definition.SchemaVersion != 1)
            throw std::invalid_argument("Prefab definition must use canonical schema version 1.");
        SceneAsset::Validate(definition.Template);
        if (!definition.BasePrefab && definition.Template.Objects.empty())
            throw std::invalid_argument("A root prefab must contain at least one object.");
    }

    SceneDefinition ComposePrefab(const AssetId prefab, const PrefabResolver& resolver)
    {
        std::vector<AssetId> stack;
        return Compose(prefab, resolver, stack);
    }

    AssetDecoderRegistration CreatePrefabAssetDecoder()
    {
        return {PrefabAsset::StaticType(), CreateRef<PrefabAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return PrefabAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreatePrefabAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Prefab";
        result.Version = 1;
        result.Type = PrefabAsset::StaticType();
        result.Extensions = {".keireprefab"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            const auto parsed = PrefabAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = PrefabAsset::Encode(parsed->Definition());
            if (parsed->Definition().BasePrefab)
                output.AssetDependencies.push_back(parsed->Definition().BasePrefab);
            const auto sceneBytes = SceneAsset::Encode(parsed->Definition().Template);
            auto sceneOutput = CreateSceneAssetImporter().ContextualImport(context, sceneBytes);
            output.AssetDependencies.insert(output.AssetDependencies.end(), sceneOutput.AssetDependencies.begin(),
                                            sceneOutput.AssetDependencies.end());
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            return output;
        };
        return result;
    }
} // namespace Keire
