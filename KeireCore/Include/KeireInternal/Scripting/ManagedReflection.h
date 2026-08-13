#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Coral
{
    class Type;
}

namespace Keire::Detail
{
    struct ManagedAssetMetadataResult final
    {
        std::vector<ManagedAssetTypeDescriptor> Types;
        std::vector<ManagedAssetTypeDiagnostic> Diagnostics;
    };

    [[nodiscard]] std::string PathText(const std::filesystem::path& path);
    [[nodiscard]] std::string ManagedTypeName(Coral::Type& type);
    [[nodiscard]] std::vector<ComponentProperty>
    ReflectManagedProperties(const Coral::Type& concreteType, const Coral::Type& behaviourType,
                             const Coral::Type& serializeFieldType, const Coral::Type* hideInInspectorType,
                             const Coral::Type* serializableType, const Coral::Type* rangeType,
                             const Coral::Type* tooltipType, const Coral::Type* groupType);
    [[nodiscard]] std::vector<ComponentMethod> ReflectManagedMethods(const Coral::Type& concreteType);
    [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                           const std::vector<ComponentProperty>& properties);
    [[nodiscard]] std::string ApplyManagedState(const std::string& state, const ComponentPropertyBag& values,
                                                const std::vector<ComponentProperty>& properties);
    [[nodiscard]] ManagedAssetMetadataResult ParseManagedAssetMetadata(std::string_view text);
} // namespace Keire::Detail
