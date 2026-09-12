#include "KeireClient/Editor/ShaderCodeTemplate.h"

#include "Keire/Rendering/ShaderGraph.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    ShaderCodeTemplate CreateUnlitShaderCodeTemplate(const std::filesystem::path& projectSource)
    {
        if (projectSource.empty() || projectSource.is_absolute() || projectSource.has_root_name() ||
            projectSource.extension() != ".hlsl")
            throw std::invalid_argument("Shader code requires a project-relative HLSL source path.");
        for (const auto& part : projectSource)
            if (part == "..")
                throw std::invalid_argument("Shader code source paths cannot traverse outside their project.");
        auto graph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
        auto tint =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
        tint.Name = "Tint";
        tint.Symbol = "Tint";
        tint.Value = Keire::Color{0.25F, 0.55F, 1.0F, 1.0F};
        const auto& output = graph.Nodes.front();
        const auto color = std::ranges::find(output.Pins, "Color", &Keire::ShaderGraphPin::Name);
        if (color == output.Pins.end())
            throw std::logic_error("The Unlit shader template is missing its color input.");
        graph.Connections.push_back(
            {Keire::AssetId::Generate(), {tint.Id, tint.Pins.front().Id}, {output.Id, color->Id}});
        graph.Nodes.push_back(std::move(tint));
        auto compilation = Keire::CompileShaderGraph(graph, {.GeneratedSource = projectSource});
        if (!compilation.Succeeded() || compilation.Variants.size() != 1)
            throw std::runtime_error("The Unlit shader code template could not be generated.");
        auto& variant = compilation.Variants.front();
        auto manifest = nlohmann::json::parse(variant.Manifest);
        manifest["source"] = Keire::Detail::PathToUtf8(projectSource);
        variant.Manifest = manifest.dump(2);
        return {std::move(variant.Hlsl), std::move(variant.Manifest)};
    }
} // namespace KeireEditor
