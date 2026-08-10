#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Keire
{
    ShaderGraphDefinition CreateDefaultShaderGraph(const ShaderGraphOutput output)
    {
        ShaderGraphDefinition definition;
        definition.Output = output;
        auto master = CreateShaderGraphNode(ShaderGraphNodeKind::Master, ShaderGraphValueType::Color);
        master.EditorPosition = {480.0F, 120.0F};
        if (output == ShaderGraphOutput::Unlit || output == ShaderGraphOutput::Fullscreen)
        {
            master.Name = output == ShaderGraphOutput::Fullscreen ? "Fullscreen Shader Output" : "Unlit Shader Output";
            std::erase_if(master.Pins,
                          [](const ShaderGraphPin& pin)
                          {
                              return pin.Name != "BaseColor" && pin.Name != "Emission" && pin.Name != "Opacity" &&
                                     pin.Name != "WorldPositionOffset" && pin.Name != "PixelDepthOffset";
                          });
            master.Pins.front().Name = "Color";
        }
        else if (output == ShaderGraphOutput::Transparent)
            master.Name = "Transparent Shader Output";
        else if (output == ShaderGraphOutput::Decal)
            master.Name = "Decal Shader Output";
        else if (output == ShaderGraphOutput::Hair)
        {
            master.Name = "Hair Shader Output";
            const auto anisotropy = std::ranges::find(master.Pins, "Anisotropy", &ShaderGraphPin::Name);
            const auto roughness = std::ranges::find(master.Pins, "Roughness", &ShaderGraphPin::Name);
            const auto sheen = std::ranges::find(master.Pins, "SheenColor", &ShaderGraphPin::Name);
            anisotropy->DefaultValue = 0.8F;
            roughness->DefaultValue = 0.35F;
            sheen->DefaultValue = Color{0.12F, 0.08F, 0.04F, 1.0F};
        }
        else if (output == ShaderGraphOutput::Eye)
        {
            master.Name = "Eye Shader Output";
            const auto clearCoat = std::ranges::find(master.Pins, "ClearCoat", &ShaderGraphPin::Name);
            const auto clearCoatRoughness = std::ranges::find(master.Pins, "ClearCoatRoughness", &ShaderGraphPin::Name);
            const auto ior = std::ranges::find(master.Pins, "IndexOfRefraction", &ShaderGraphPin::Name);
            const auto refraction = std::ranges::find(master.Pins, "Refraction", &ShaderGraphPin::Name);
            clearCoat->DefaultValue = 1.0F;
            clearCoatRoughness->DefaultValue = 0.05F;
            ior->DefaultValue = 1.336F;
            refraction->DefaultValue = 0.2F;
        }
        definition.Nodes.push_back(std::move(master));
        return definition;
    }

    ShaderGraphDefinition CreateShaderGraphTemplate(const ShaderGraphTemplate graphTemplate)
    {
        switch (graphTemplate)
        {
        case ShaderGraphTemplate::Lit:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Surface);
        case ShaderGraphTemplate::Unlit:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Unlit);
        case ShaderGraphTemplate::Transparent:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Transparent);
        case ShaderGraphTemplate::Decal:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Decal);
        case ShaderGraphTemplate::Fullscreen:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Fullscreen);
        case ShaderGraphTemplate::Hair:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Hair);
        case ShaderGraphTemplate::Eye:
            return CreateDefaultShaderGraph(ShaderGraphOutput::Eye);
        }
        throw std::invalid_argument("Shader Graph template is unsupported.");
    }
} // namespace Keire
