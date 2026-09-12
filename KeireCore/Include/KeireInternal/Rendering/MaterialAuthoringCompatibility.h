#pragma once

#include "Keire/Rendering/MaterialGraph.h"

#include <utility>

namespace Keire::Detail
{
    // Keep the historical imported asset type available to instances and runtime consumers.
    // The source remains property-only; these compatibility pins never represent executable shader code.
    [[nodiscard]] inline MaterialGraphDefinition MaterialGraphFromAuthoring(const MaterialAuthoringDefinition& source)
    {
        ShaderInterfaceDefinition interface;
        auto properties = source.Properties;
        for (const auto& property : source.PropertyOverrides)
            properties.insert_or_assign(property.Name, property.Value);
        for (const auto& [name, value] : properties)
        {
            ShaderPropertyDefinition property;
            property.Name = name;
            property.Type = static_cast<ShaderPropertyType>(value.index());
            interface.Properties.push_back(std::move(property));
        }
        auto result = CreateMaterialGraph(source.Shader, interface);
        for (auto& property : result.Properties)
            property.Value = properties.at(property.Name);
        result.Surface = source.Surface;
        result.ContributeEmissionToGI = source.ContributeEmissionToGI;
        result.EmissiveGIIntensity = source.EmissiveGIIntensity;
        ValidateMaterialGraph(result);
        return result;
    }

    [[nodiscard]] inline AssetImportOutput ImportPropertyMaterial(const AssetImportContext& context,
                                                                  const std::span<const std::byte> bytes)
    {
        // Resolve existing shader references; property edits must never compile shader programs.
        auto output = CreateMaterialAssetImporter().ContextualImport(context, bytes);
        auto runtime = std::move(output.Bytes);
        auto authoring = MaterialAsset::DecodeAuthoringSource(bytes);
        authoring.Properties = MaterialAsset::Decode(runtime)->Definition().Properties;
        authoring.PropertyOverrides.clear();
        output.Bytes = MaterialGraphAsset::Encode(MaterialGraphFromAuthoring(authoring));
        output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                    "material/default", "Runtime Material", std::move(runtime),
                                    output.AssetDependencies});
        return output;
    }
} // namespace Keire::Detail
