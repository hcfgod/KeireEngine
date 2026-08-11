#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace KeireEditor
{
    std::string DisplayName(const std::filesystem::path& path)
    {
        const auto stem = path.stem().string();
        return stem.empty() ? path.filename().string() : stem;
    }

    bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate)
    {
        const auto normalizedParent = parent.lexically_normal();
        const auto normalizedCandidate = candidate.lexically_normal();
        if (normalizedParent == normalizedCandidate)
            return true;
        const auto relative = normalizedCandidate.lexically_relative(normalizedParent);
        return !relative.empty() && !relative.is_absolute() && !relative.generic_string().starts_with("..");
    }

    std::string AssetTypeName(const Keire::AssetSourceRecord& record)
    {
        if (const auto content = record.ImportSettings.find("contentType"); content != record.ImportSettings.end())
        {
            if (const auto* value = std::get_if<std::string>(&content->second); value && *value == "animation")
                return "Animation Source";
        }
        const auto extension = record.RelativePath.extension().string();
        if (extension == ".keirescene")
            return "Scene";
        if (extension == ".keireinput")
            return "Input Actions";
        if (extension == ".keireshader")
            return "Shader";
        if (extension == ".keirematerial")
            return "Material";
        if (extension == ".keirematerialgraph")
            return "Material Graph";
        if (extension == ".keireshadergraph")
            return "Shader Graph";
        if (extension == Keire::MaterialFunctionAssetSourceExtension)
            return "Material Function";
        if (extension == Keire::ShaderFunctionAssetSourceExtension)
            return "Shader Function";
        if (extension == Keire::MaterialLayerAssetSourceExtension)
            return "Material Layer";
        if (extension == Keire::MaterialLayerBlendAssetSourceExtension)
            return "Material Layer Blend";
        if (extension == Keire::MaterialParameterCollectionAssetSourceExtension)
            return "Material Parameter Collection";
        if (extension == ".keirematerialinstance")
            return "Material Instance";
        if (extension == ".keireshadergraphinstance")
            return "Legacy Shader Graph Instance";
        if (extension == ".keireanimgraph")
            return "Animator Controller";
        if (extension == ".keireanim")
            return "Animation Clip";
        if (extension == ".keiremixer")
            return "Audio Mixer";
        if (extension == ".keirephysicsmaterial")
            return "Physics Material";
        if (extension == ".keirevfx")
            return "VFX Effect";
        if (extension == ".keiredata")
            return "Managed Data";
        if (extension == ".keireprefab")
            return "Prefab";
        if (extension == ".keireasm")
            return "Managed Assembly";
        if (extension == ".cs")
            return "C# Script";
        if (extension == ".hlsl")
            return "HLSL Source";
        return "Asset";
    }

    AssetBrowserOpenAction ResolveAssetBrowserOpenAction(const std::filesystem::path& path) noexcept
    {
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (extension == ".keireinput")
            return AssetBrowserOpenAction::InputActions;
        if (extension == ".keireanimgraph")
            return AssetBrowserOpenAction::AnimationGraph;
        if (extension == ".keiremixer")
            return AssetBrowserOpenAction::AudioMixer;
        if (extension == ".keirevfx")
            return AssetBrowserOpenAction::VfxEffect;
        if (extension == ".keirematerial")
            return AssetBrowserOpenAction::Material;
        if (extension == ".keirematerialgraph")
            return AssetBrowserOpenAction::MaterialGraph;
        if (extension == ".keirematerialinstance")
            return AssetBrowserOpenAction::MaterialInstance;
        if (extension == ".keireshadergraph" || extension == Keire::MaterialFunctionAssetSourceExtension ||
            extension == Keire::ShaderFunctionAssetSourceExtension ||
            extension == Keire::MaterialLayerAssetSourceExtension ||
            extension == Keire::MaterialLayerBlendAssetSourceExtension)
            return AssetBrowserOpenAction::ShaderGraph;
        if (extension == Keire::MaterialParameterCollectionAssetSourceExtension)
            return AssetBrowserOpenAction::MaterialParameterCollection;
        if (extension == ".keirescene")
            return AssetBrowserOpenAction::Scene;
        if (extension == ".keireprefab")
            return AssetBrowserOpenAction::Prefab;
        return AssetBrowserOpenAction::External;
    }

    std::vector<Keire::AssetId> DecodeAssetPayload(const std::span<const std::byte> bytes)
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream stream(text);
        std::vector<Keire::AssetId> result;
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;
            const auto id = Keire::AssetId::Parse(line);
            if (!id || result.size() >= 1024)
                throw std::invalid_argument("Asset drag payload is invalid or exceeds 1024 entries.");
            result.push_back(id);
        }
        if (result.empty())
            throw std::invalid_argument("Asset drag payload is empty.");
        return result;
    }

    std::string EncodeAssetPayload(const std::span<const Keire::AssetId> assets)
    {
        std::string result;
        for (const auto asset : assets)
        {
            result += asset.ToString();
            result.push_back('\n');
        }
        return result;
    }
} // namespace KeireEditor
