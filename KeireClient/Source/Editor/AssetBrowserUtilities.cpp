#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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

    std::vector<AssetBrowserHierarchyEntry>
    BuildAssetBrowserHierarchy(const std::span<const Keire::AssetSourceRecord* const> records,
                               const std::span<const Keire::AssetId> expandedParents, const bool expandAll)
    {
        std::unordered_map<Keire::AssetId, const Keire::AssetSourceRecord*> byId;
        byId.reserve(records.size());
        for (const auto* record : records)
            if (record)
                byId.emplace(record->Id, record);

        std::unordered_map<Keire::AssetId, std::vector<const Keire::AssetSourceRecord*>> children;
        for (const auto* record : records)
            if (record && record->ParentSource && record->ParentSource != record->Id &&
                byId.contains(record->ParentSource))
                children[record->ParentSource].push_back(record);

        const std::unordered_set<Keire::AssetId> expanded(expandedParents.begin(), expandedParents.end());
        std::unordered_set<Keire::AssetId> visited;
        std::unordered_set<Keire::AssetId> grouped;
        std::vector<AssetBrowserHierarchyEntry> result;
        result.reserve(records.size());
        const auto markGrouped = [&](const auto& self, const Keire::AssetSourceRecord& record) -> void
        {
            if (!grouped.emplace(record.Id).second)
                return;
            if (const auto found = children.find(record.Id); found != children.end())
                for (const auto* child : found->second)
                    self(self, *child);
        };
        const auto append = [&](const auto& self, const Keire::AssetSourceRecord& record,
                                const std::size_t depth) -> void
        {
            if (!visited.emplace(record.Id).second)
                return;
            const auto found = children.find(record.Id);
            const bool hasChildren = found != children.end() && !found->second.empty();
            const auto directChildCount = hasChildren ? found->second.size() : 0;
            result.push_back({&record, depth, directChildCount, hasChildren});
            if (!hasChildren || (!expandAll && !expanded.contains(record.Id)))
                return;
            for (const auto* child : found->second)
                self(self, *child, depth + 1);
        };

        for (const auto* record : records)
            if (record &&
                (!record->ParentSource || record->ParentSource == record->Id || !byId.contains(record->ParentSource)))
            {
                markGrouped(markGrouped, *record);
                append(append, *record, 0);
            }
        for (const auto* record : records)
            if (record && !grouped.contains(record->Id))
                append(append, *record, 0);
        return result;
    }

    std::array<Keire::UiPosition, 3> AssetBrowserDisclosureTriangle(const Keire::UiItemRect area,
                                                                    const bool expanded) noexcept
    {
        const auto center =
            Keire::UiPosition{area.Minimum.X + area.Size().Width * 0.5F, area.Minimum.Y + area.Size().Height * 0.5F};
        constexpr float halfWidth = 4.5F;
        constexpr float halfHeight = 3.5F;
        if (expanded)
        {
            return {{{center.X - halfWidth, center.Y - halfHeight},
                     {center.X + halfWidth, center.Y - halfHeight},
                     {center.X, center.Y + halfHeight}}};
        }
        return {{{center.X - halfHeight, center.Y - halfWidth},
                 {center.X - halfHeight, center.Y + halfWidth},
                 {center.X + halfHeight, center.Y}}};
    }

    Keire::UiItemRect AssetBrowserGridNameArea(const Keire::UiItemRect card, const float thumbnailSize,
                                               const float countBadgeWidth, const bool hasChildren) noexcept
    {
        const float thumbnailBottom = card.Minimum.Y + 8.0F + std::max(thumbnailSize, 0.0F);
        const float left = card.Minimum.X + (hasChildren ? 30.0F : 8.0F);
        const float right =
            hasChildren ? card.Maximum.X - 12.0F - std::max(countBadgeWidth, 0.0F) : card.Maximum.X - 8.0F;
        return {{left, thumbnailBottom + 3.0F}, {std::max(left, right), thumbnailBottom + 21.0F}};
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
        if (extension == ".keireui")
            return "UI Document";
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
        if (extension == ".keireui")
            return AssetBrowserOpenAction::UiDocument;
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

    Keire::AssetId DecodeSingleAssetPayload(const std::span<const std::byte> bytes)
    {
        const auto assets = DecodeAssetPayload(bytes);
        if (assets.size() != 1)
            throw std::invalid_argument("Asset drag payload must contain exactly one identity.");
        return assets.front();
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

    ManagedScriptPlacement
    ResolveManagedScriptPlacement(const std::span<const ManagedScriptAssemblyCandidate> assemblies,
                                  const std::filesystem::path& selectedAssetFolder)
    {
        const auto normalizedFolder = selectedAssetFolder.lexically_normal();
        if (selectedAssetFolder.is_absolute() || normalizedFolder == ".." ||
            std::ranges::any_of(normalizedFolder, [](const auto& part) { return part == ".."; }))
            throw std::invalid_argument("C# script creation requires a project-relative asset folder.");

        const auto selectedRoot = (std::filesystem::path("Assets") / normalizedFolder).lexically_normal();
        const ManagedScriptAssemblyCandidate* covering = nullptr;
        std::size_t coveringLength = 0;
        const ManagedScriptAssemblyCandidate* descendant = nullptr;
        std::filesystem::path descendantRoot;
        const ManagedScriptAssemblyCandidate* runtime = nullptr;
        for (const auto& candidate : assemblies)
        {
            if (!candidate.Asset)
                throw std::invalid_argument("Managed script placement requires valid assembly identities.");
            Keire::ManagedAssemblyAsset::Validate(candidate.Definition);
            if (candidate.Definition.Classification == Keire::ManagedAssemblyClassification::Runtime &&
                (!runtime || std::tie(candidate.Definition.Name, candidate.Asset) <
                                 std::tie(runtime->Definition.Name, runtime->Asset)))
                runtime = &candidate;
            for (const auto& root : candidate.Definition.SourceRoots)
            {
                const auto normalizedRoot = root.lexically_normal();
                if (SameOrChild(normalizedRoot, selectedRoot))
                {
                    const auto length = normalizedRoot.generic_string().size();
                    if (!covering || length > coveringLength ||
                        (length == coveringLength && candidate.Asset < covering->Asset))
                    {
                        covering = &candidate;
                        coveringLength = length;
                    }
                }
                else if (SameOrChild(selectedRoot, normalizedRoot) &&
                         (!descendant || normalizedRoot.generic_string() < descendantRoot.generic_string() ||
                          (normalizedRoot == descendantRoot && candidate.Asset < descendant->Asset)))
                {
                    descendant = &candidate;
                    descendantRoot = normalizedRoot;
                }
            }
        }

        if (covering)
            return {covering->Asset, covering->Definition.RootNamespace, {}};
        const auto* selected = descendant ? descendant : runtime;
        if (!selected)
            throw std::runtime_error("Create a runtime .keireasm asset before creating a C# script.");
        return {selected->Asset, selected->Definition.RootNamespace, selectedRoot};
    }

    std::string BuildManagedScriptSource(const ManagedScriptTemplateKind kind, const std::string_view rootNamespace,
                                         const std::string_view typeName, const Keire::AssetId stableTypeId)
    {
        if (rootNamespace.empty() || typeName.empty() || !stableTypeId)
            throw std::invalid_argument("Managed script templates require a namespace, type name, and stable ID.");
        const auto prefix = "using Keire;\n\nnamespace " + std::string(rootNamespace) + ";\n\n";
        if (kind == ManagedScriptTemplateKind::ScriptableObject)
        {
            return prefix + "[StableAssetTypeId(\"" + stableTypeId.ToString() + "\")]\n[CreateAssetMenu(\"" +
                   std::string(typeName) + "\", \"" + std::string(typeName) + "\")]\npublic sealed class " +
                   std::string(typeName) + " : ScriptableObject\n{\n    public int Value = 0;\n}\n";
        }
        return prefix + "[StableComponentId(\"" + stableTypeId.ToString() + "\")]\npublic sealed class " +
               std::string(typeName) +
               " : Behaviour\n{\n    protected override void Start()\n    {\n    }\n\n"
               "    protected override void Update()\n    {\n    }\n}\n";
    }

    bool ExtendManagedAssemblySourceRoots(Keire::ManagedAssemblyDefinition& assembly,
                                          const std::filesystem::path& sourceRoot)
    {
        const auto normalizedRoot = sourceRoot.lexically_normal();
        if (sourceRoot.empty() || sourceRoot.is_absolute() || normalizedRoot == "." ||
            std::ranges::any_of(normalizedRoot, [](const auto& part) { return part == ".."; }))
            throw std::invalid_argument("Managed assembly source coverage requires a project-relative folder.");
        if (std::ranges::any_of(assembly.SourceRoots,
                                [&](const auto& existing) { return SameOrChild(existing, normalizedRoot); }))
            return false;

        std::erase_if(assembly.SourceRoots,
                      [&](const auto& existing) { return SameOrChild(normalizedRoot, existing); });
        assembly.SourceRoots.push_back(normalizedRoot);
        std::ranges::sort(assembly.SourceRoots);
        Keire::ManagedAssemblyAsset::Validate(assembly);
        return true;
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
    void DuplicateAssetBrowserFolders(const std::span<const std::filesystem::path> folders,
                                      const std::filesystem::path& assetRoot, IAssetBrowserController& controller)
    {
        try
        {
            for (const auto& folder : folders)
            {
                const auto destination =
                    UniqueAssetBrowserFolder(assetRoot, folder.parent_path() / (folder.filename().string() + " Copy"));
                controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateFolder,
                                               .Source = folder,
                                               .Destination = destination},
                                              {}, "Duplicate Folder");
            }
            controller.SetAssetBrowserStatus("Queued " + std::to_string(folders.size()) + " folder duplicate(s).");
        }
        catch (const std::exception& error)
        {
            controller.ReportAssetBrowserError(std::string("Folder duplication failed: ") + error.what());
        }
    }

    void SelectAssetBrowserAsset(std::vector<Keire::AssetId>& selection,
                                 std::vector<std::filesystem::path>& folderSelection, Keire::AssetId& anchor,
                                 const Keire::AssetId asset, const bool additive, IAssetBrowserController& controller)
    {
        if (!additive)
        {
            selection.clear();
            folderSelection.clear();
        }
        const auto found = std::ranges::find(selection, asset);
        if (additive && found != selection.end())
            selection.erase(found);
        else if (found == selection.end())
            selection.push_back(asset);
        anchor = asset;
        controller.SetAssetBrowserSelected(selection.empty() ? Keire::AssetId{} : selection.back());
        controller.ClearAssetBrowserSceneSelection();
    }

    void MoveAssetBrowserAssets(const std::span<const Keire::AssetId> assets, const std::filesystem::path& folder,
                                IAssetBrowserController& controller)
    {
        std::vector<std::pair<Keire::AssetSourceRecord, std::filesystem::path>> moves;
        std::set<std::string> destinations;
        for (const auto asset : assets)
        {
            const auto record = controller.AssetBrowserDatabase()->Find(asset);
            if (!record)
                throw std::invalid_argument("Cannot move an asset that no longer exists.");
            const auto destination = (folder / record->RelativePath.filename()).lexically_normal();
            if (destination == record->RelativePath)
                continue;
            if (controller.AssetBrowserDatabase()->Find(destination) ||
                !destinations.insert(destination.generic_string()).second)
                throw std::runtime_error("Asset move destination already exists: " + destination.generic_string());
            moves.emplace_back(*record, destination);
        }
        for (const auto& [record, destination] : moves)
        {
            controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                           .Asset = record.Id,
                                           .Destination = destination},
                                          {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                           .Asset = record.Id,
                                           .Destination = record.RelativePath},
                                          "Move Asset");
        }
        controller.SetAssetBrowserStatus("Queued " + std::to_string(moves.size()) + " asset move(s).");
    }

    void SetAssetBrowserClipboard(const std::span<const Keire::AssetId> assets,
                                  std::vector<AssetBrowserClipboardEntry>& clipboard)
    {
        clipboard.clear();
        for (const auto asset : assets)
            clipboard.push_back({asset, {}});
    }

    void SetAssetBrowserFolderClipboard(const std::span<const std::filesystem::path> folders,
                                        std::vector<AssetBrowserClipboardEntry>& clipboard)
    {
        clipboard.clear();
        for (const auto& folder : folders)
            clipboard.push_back({Keire::AssetId{}, folder});
    }

    void PasteAssetBrowserClipboard(AssetBrowserClipboardMode& mode, std::vector<AssetBrowserClipboardEntry>& clipboard,
                                    const std::filesystem::path& assetRoot, const std::filesystem::path& folder,
                                    IAssetBrowserController& controller)
    {
        if (mode == AssetBrowserClipboardMode::Empty || clipboard.empty())
            return;
        try
        {
            for (const auto& entry : clipboard)
            {
                if (entry.Asset)
                {
                    const auto record = controller.AssetBrowserDatabase()->Find(entry.Asset);
                    if (!record)
                        throw std::runtime_error("Clipboard asset no longer exists.");
                    if (mode == AssetBrowserClipboardMode::Cut)
                    {
                        const auto destination = folder / record->RelativePath.filename();
                        controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = destination},
                                                      {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = record->RelativePath},
                                                      "Move Asset");
                    }
                    else
                    {
                        const auto destination =
                            UniqueAssetBrowserPath(*record, folder, *controller.AssetBrowserDatabase());
                        controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateAsset,
                                                       .Asset = entry.Asset,
                                                       .Destination = destination},
                                                      {}, "Paste Asset", true);
                    }
                }
                else
                {
                    const auto destinationBase = folder / entry.Folder.filename();
                    if (mode == AssetBrowserClipboardMode::Cut)
                    {
                        controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                       .Source = entry.Folder,
                                                       .Destination = destinationBase},
                                                      {.Kind = Keire::Detail::AssetWorkerMutationKind::MoveFolder,
                                                       .Source = destinationBase,
                                                       .Destination = entry.Folder},
                                                      "Move Folder");
                    }
                    else
                    {
                        const auto destination = UniqueAssetBrowserFolder(assetRoot, destinationBase);
                        controller.MutateAssetBrowser({.Kind = Keire::Detail::AssetWorkerMutationKind::DuplicateFolder,
                                                       .Source = entry.Folder,
                                                       .Destination = destination},
                                                      {}, "Paste Folder");
                    }
                }
            }
            if (mode == AssetBrowserClipboardMode::Cut)
            {
                clipboard.clear();
                mode = AssetBrowserClipboardMode::Empty;
            }
            controller.SetAssetBrowserStatus("Pasted asset selection into " +
                                             (folder.empty() ? std::string("Assets") : folder.generic_string()) + ".");
        }
        catch (const std::exception& error)
        {
            controller.ReportAssetBrowserError(std::string("Asset paste failed: ") + error.what());
        }
    }
} // namespace KeireEditor
