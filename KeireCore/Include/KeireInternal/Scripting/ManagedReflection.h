#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Coral
{
    class ManagedAssembly;
    class Type;
} // namespace Coral

namespace Keire::Detail
{
    struct ManagedAssetMetadataResult final
    {
        struct BehaviourGraph final
        {
            std::string FullName;
            std::vector<ManagedReferenceGraphDescriptor> Fields;
        };

        std::vector<ManagedAssetTypeDescriptor> Types;
        std::vector<BehaviourGraph> Behaviours;
        std::vector<ManagedAssetTypeDiagnostic> Diagnostics;
    };

    struct ManagedInspectorAttributeTypes final
    {
        const Coral::Type* SerializeField = nullptr;
        const Coral::Type* SerializeReference = nullptr;
        const Coral::Type* HideInInspector = nullptr;
        const Coral::Type* Serializable = nullptr;
        const Coral::Type* Range = nullptr;
        const Coral::Type* Minimum = nullptr;
        const Coral::Type* Maximum = nullptr;
        const Coral::Type* Step = nullptr;
        const Coral::Type* Multiline = nullptr;
        const Coral::Type* InspectorName = nullptr;
        const Coral::Type* ReadOnly = nullptr;
        const Coral::Type* Header = nullptr;
        const Coral::Type* Tooltip = nullptr;
        const Coral::Type* Group = nullptr;
        const Coral::Type* StableComponentId = nullptr;
        const Coral::Type* StableAssetTypeId = nullptr;
        const Coral::Type* StableSerializedTypeId = nullptr;
    };

    [[nodiscard]] std::string PathText(const std::filesystem::path& path);
    [[nodiscard]] std::string ManagedTypeName(Coral::Type& type);
    [[nodiscard]] ManagedInspectorAttributeTypes ResolveManagedInspectorAttributeTypes(Coral::ManagedAssembly& api);
    [[nodiscard]] std::vector<ComponentProperty>
    ReflectManagedProperties(const Coral::Type& concreteType, const Coral::Type& behaviourType,
                             const ManagedInspectorAttributeTypes& attributeTypes);
    [[nodiscard]] std::vector<ComponentMethod> ReflectManagedMethods(const Coral::Type& concreteType);

    template <typename BehaviourRange>
    void PopulateManagedBehaviourReferenceCompatibility(
        BehaviourRange& behaviours, const std::map<std::string, const Coral::Type*, std::less<>>& runtimeTypes)
    {
        for (auto& behaviour : behaviours)
        {
            for (auto& property : behaviour.Properties)
            {
                if (property.ReferenceKind != ManagedReferenceKind::Behaviour ||
                    !property.CompatibleComponentTypes.empty())
                    continue;
                const auto declared = runtimeTypes.find(property.DeclaredManagedType);
                if (declared == runtimeTypes.end() || !declared->second)
                    continue;
                for (const auto& compatible : behaviours)
                {
                    if (compatible.Type && compatible.Type->IsAssignableTo(*declared->second))
                    {
                        property.CompatibleComponentTypes.push_back(compatible.ComponentType);
                        property.CompatibleBehaviourTypes.push_back(compatible.Name);
                    }
                }
            }
        }
    }

    [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                           const std::vector<ComponentProperty>& properties);
    [[nodiscard]] std::string ApplyManagedState(const std::string& state, const ComponentPropertyBag& values,
                                                const std::vector<ComponentProperty>& properties);
    [[nodiscard]] ManagedAssetMetadataResult ParseManagedAssetMetadata(std::string_view text);
} // namespace Keire::Detail
