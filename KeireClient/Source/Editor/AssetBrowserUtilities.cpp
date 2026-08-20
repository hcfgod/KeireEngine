#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace KeireEditor
{
    bool AssetBrowserRecordViewCache::Refresh(const std::span<const Keire::AssetSourceRecord> records,
                                              const std::uint64_t revision, const std::filesystem::path& folder,
                                              const std::string_view search)
    {
        if (m_Initialized && m_Revision == revision && m_Folder == folder && m_Search == search)
            return false;

        std::vector<const Keire::AssetSourceRecord*> visible;
        visible.reserve(records.size());
        for (const auto& record : records)
        {
            if (record.RelativePath.parent_path() == folder &&
                (search.empty() || record.RelativePath.filename().string().find(search) != std::string::npos))
            {
                visible.push_back(&record);
            }
        }
        std::ranges::sort(visible, {}, [](const auto* record) { return record->RelativePath.filename(); });

        m_Revision = revision;
        m_Folder = folder;
        m_Search = search;
        m_Records = std::move(visible);
        m_Initialized = true;
        return true;
    }

    void AssetBrowserRecordViewCache::Clear() noexcept
    {
        m_Revision = 0;
        m_Folder.clear();
        m_Search.clear();
        m_Records.clear();
        m_Initialized = false;
    }

    std::string DisplayName(const std::filesystem::path& path)
    {
        const auto stem = path.stem().string();
        return stem.empty() ? path.filename().string() : stem;
    }

    std::string ElideAssetDisplayName(const std::string_view name, const float maximumWidth,
                                      const std::function<float(std::string_view)>& measureText)
    {
        if (!measureText)
            throw std::invalid_argument("Asset-name elision requires a text measurement callback.");
        if (name.empty())
            return std::string(name);
        if (maximumWidth <= 0.0F)
            return {};
        if (measureText(name) <= maximumWidth)
            return std::string(name);

        constexpr std::string_view ellipsis = "...";
        if (measureText(ellipsis) > maximumWidth)
            return {};

        std::size_t end = name.size();
        while (end > 0)
        {
            --end;
            while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0U) == 0x80U)
                --end;
            std::string candidate(name.substr(0, end));
            candidate += ellipsis;
            if (measureText(candidate) <= maximumWidth)
                return candidate;
        }
        return std::string(ellipsis);
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

    AssetBrowserPreferences LoadAssetBrowserPreferences(const std::filesystem::path& path) noexcept
    {
        AssetBrowserPreferences preferences;
        try
        {
            std::ifstream input(path);
            std::string line;
            while (std::getline(input, line))
            {
                if (line == "view=grid")
                    preferences.GridView = true;
                else if (line == "view=list")
                    preferences.GridView = false;
                else if (line.starts_with("size="))
                {
                    try
                    {
                        preferences.ThumbnailSize = std::clamp(std::stof(line.substr(5)), 48.0F, 160.0F);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        catch (...)
        {
        }
        return preferences;
    }

    void SaveAssetBrowserPreferences(const std::filesystem::path& path,
                                     const AssetBrowserPreferences& preferences) noexcept
    {
        if (path.empty())
            return;
        try
        {
            const auto text = std::string("view=") + (preferences.GridView ? "grid\n" : "list\n") +
                              "size=" + std::to_string(std::clamp(preferences.ThumbnailSize, 48.0F, 160.0F)) + "\n";
            Keire::Detail::WriteTextFileAtomically(path, text);
        }
        catch (...)
        {
        }
    }

    std::vector<std::filesystem::path> DirectChildAssetFolders(const std::span<const std::filesystem::path> folders,
                                                               const std::filesystem::path& parent)
    {
        std::vector<std::filesystem::path> result;
        for (const auto& folder : folders)
            if (folder.parent_path() == parent)
                result.push_back(folder);
        return result;
    }

    std::filesystem::path UniqueAssetBrowserFolder(const std::filesystem::path& assetRoot,
                                                   std::filesystem::path desired)
    {
        const auto parent = desired.parent_path();
        const auto base = desired.filename().string();
        for (std::size_t copy = 2; std::filesystem::exists(assetRoot / desired); ++copy)
            desired = parent / (base + " " + std::to_string(copy));
        return desired;
    }

    std::filesystem::path UniqueAssetBrowserPath(const Keire::AssetSourceRecord& source,
                                                 const std::filesystem::path& folder,
                                                 const Keire::AssetDatabase& database)
    {
        const auto stem = source.RelativePath.stem().string();
        const auto extension = source.RelativePath.extension().string();
        auto copyName = stem;
        copyName.append(" Copy").append(extension);
        auto destination = folder / copyName;
        for (std::size_t copy = 2; database.Find(destination); ++copy)
        {
            copyName = stem;
            copyName.append(" Copy ").append(std::to_string(copy)).append(extension);
            destination = folder / copyName;
        }
        return destination;
    }

    std::filesystem::path ResolveAssetBrowserDropFolder(const std::span<const AssetBrowserDropTarget> targets,
                                                        const Keire::UiPosition position,
                                                        const std::filesystem::path& fallback)
    {
        for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator)
            if (iterator->Rect.Contains(position))
                return iterator->Folder;
        return fallback;
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

    std::vector<std::filesystem::path> BuildFolderRangeSelection(const std::span<const std::filesystem::path> order,
                                                                 const std::filesystem::path& anchor,
                                                                 const std::filesystem::path& target,
                                                                 const std::span<const std::filesystem::path> existing,
                                                                 const bool additive)
    {
        std::vector<std::filesystem::path> result;
        if (additive)
            result.assign(existing.begin(), existing.end());
        const auto anchorIterator = std::ranges::find(order, anchor);
        const auto targetIterator = std::ranges::find(order, target);
        if (anchorIterator == order.end() || targetIterator == order.end())
        {
            if (std::ranges::find(result, target) == result.end())
                result.push_back(target);
            return result;
        }
        const auto first = std::min(anchorIterator, targetIterator);
        const auto last = std::max(anchorIterator, targetIterator);
        for (auto iterator = first; iterator <= last; ++iterator)
            if (std::ranges::find(result, *iterator) == result.end())
                result.push_back(*iterator);
        return result;
    }

    std::vector<std::filesystem::path> DecodeFolderPayload(const std::span<const std::byte> bytes)
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream stream(text);
        std::vector<std::filesystem::path> result;
        std::string line;
        while (std::getline(stream, line))
        {
            const auto path = std::filesystem::path(line).lexically_normal();
            if (path.empty() || path.is_absolute() || path.generic_string().starts_with("..") || result.size() >= 1024)
                throw std::invalid_argument("Folder drag payload is invalid or exceeds 1024 entries.");
            result.push_back(path);
        }
        if (result.empty())
            throw std::invalid_argument("Folder drag payload is empty.");
        return result;
    }

    std::string EncodeFolderPayload(const std::span<const std::filesystem::path> folders)
    {
        std::string result;
        for (const auto& folder : folders)
        {
            result += folder.generic_string();
            result.push_back('\n');
        }
        return result;
    }
} // namespace KeireEditor
