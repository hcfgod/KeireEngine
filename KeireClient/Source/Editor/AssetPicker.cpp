#include "KeireClient/Editor/AssetPicker.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    [[nodiscard]] bool ContainsInsensitive(const std::string_view value, const std::string_view search)
    {
        return search.empty() || Lower(std::string(value)).find(Lower(std::string(search))) != std::string::npos;
    }

    [[nodiscard]] std::vector<Keire::AssetId> DecodeAssetPayload(const std::span<const std::byte> bytes)
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

    struct AssetPickerCandidate final
    {
        Keire::AssetId Id;
        Keire::AssetId DropAlias;
        std::string DisplayLabel;
        std::string SelectionLabel;
    };

    [[nodiscard]] std::string GeneratedAssetLabel(const Keire::AssetTypeId type)
    {
        if (type == Keire::SkeletonAsset::StaticType())
            return "Skeleton";
        if (type == Keire::SkinnedMeshAsset::StaticType())
            return "Skinned Mesh";
        if (type == Keire::AnimationClipAsset::StaticType())
            return "Animation Clip";
        if (type == Keire::RigDefinitionAsset::StaticType())
            return "Rig";
        if (type == Keire::ShaderAsset::StaticType())
            return "Compiled Shader";
        if (type == Keire::MaterialAsset::StaticType())
            return "Compiled Material";
        return "Generated Asset";
    }

    [[nodiscard]] bool AcceptsCandidate(const Keire::AssetSourceRecord& source, const Keire::AssetId id,
                                        const Keire::AssetTypeId type, const KeireEditor::AssetPickerOptions& options)
    {
        if (options.ExpectedType && type != *options.ExpectedType)
            return false;
        if (!options.Filter)
            return true;
        if (id == source.Id)
            return options.Filter(source);
        auto generated = source;
        generated.Id = id;
        generated.Type = type;
        return options.Filter(generated);
    }

    [[nodiscard]] std::vector<AssetPickerCandidate>
    BuildCandidates(const std::span<const Keire::AssetSourceRecord> records,
                    const KeireEditor::AssetPickerOptions& options)
    {
        std::size_t capacity = records.size();
        for (const auto& record : records)
            capacity += record.SubAssets.size();

        std::vector<AssetPickerCandidate> result;
        result.reserve(capacity + Keire::BuiltinMeshCatalog().size());
        if (options.ExpectedType == Keire::MeshAsset::StaticType())
        {
            for (const auto& mesh : Keire::BuiltinMeshCatalog())
            {
                const auto label = "Built-in / " + std::string(mesh.Name);
                result.push_back({mesh.Id, {}, label, label + "##" + mesh.Id.ToString()});
            }
        }
        for (const auto& record : records)
        {
            if (AcceptsCandidate(record, record.Id, record.Type, options))
            {
                auto label = record.RelativePath.generic_string();
                result.push_back({record.Id, {}, label, label + "##" + record.Id.ToString()});
            }
            if (!options.ResolveType)
            {
                continue;
            }
            for (const auto subAsset : record.SubAssets)
            {
                auto type = options.ResolveType(subAsset);
                if (!type || !AcceptsCandidate(record, subAsset, *type, options))
                    continue;
                auto label = record.RelativePath.generic_string() + " / " + GeneratedAssetLabel(*type);
                const auto materialSource = record.Type == Keire::MaterialGraphAsset::StaticType() ||
                                            record.Type == Keire::MaterialGraphInstanceAsset::StaticType();
                const auto dropAlias =
                    materialSource && *type == Keire::MaterialAsset::StaticType() ? record.Id : Keire::AssetId{};
                result.push_back({subAsset, dropAlias, label, label + "##" + subAsset.ToString()});
            }
        }
        return result;
    }
} // namespace

namespace KeireEditor
{
    bool AssetPicker::Accepts(const Keire::AssetSourceRecord& record, const AssetPickerOptions& options)
    {
        return (!options.ExpectedType || record.Type == *options.ExpectedType) &&
               (!options.Filter || options.Filter(record));
    }

    bool AssetPicker::AcceptsEnvironmentTexture(const Keire::AssetSourceRecord& record)
    {
        if (record.Type != Keire::Texture2DAsset::StaticType())
            return false;
        const auto semantic = record.ImportSettings.find("semantic");
        if (semantic != record.ImportSettings.end())
        {
            const auto* value = std::get_if<std::string>(&semantic->second);
            return value && *value == "environment";
        }
        const auto extension = Lower(record.RelativePath.extension().string());
        return extension == ".hdr" || extension == ".exr";
    }

    std::optional<Keire::AssetId>
    AssetPicker::ResolveCompatibleAsset(const std::span<const Keire::AssetSourceRecord> records,
                                        const Keire::AssetId dropped, const AssetPickerOptions& options)
    {
        if (!dropped)
            return std::nullopt;
        const auto candidates = BuildCandidates(records, options);
        const auto found = std::ranges::find_if(candidates, [dropped](const auto& candidate)
                                                { return candidate.Id == dropped || candidate.DropAlias == dropped; });
        return found == candidates.end() ? std::nullopt : std::optional<Keire::AssetId>(found->Id);
    }

    bool AssetPicker::Draw(Keire::UiFrame& ui, const std::span<const Keire::AssetSourceRecord> records,
                           Keire::AssetId& value, const AssetPickerOptions& options)
    {
        if (options.Label.empty())
            throw std::invalid_argument("An asset picker requires a non-empty label.");

        const auto candidates = BuildCandidates(records, options);
        const auto selected = std::ranges::find(candidates, value, &AssetPickerCandidate::Id);
        const bool selectedCompatible = selected != candidates.end();
        const std::string preview = !value               ? std::string(options.EmptyLabel)
                                    : selectedCompatible ? selected->DisplayLabel
                                                         : "Missing or incompatible asset";
        bool changed = false;
        {
            auto id = ui.PushId(options.Label);
            if (auto combo = ui.BeginCombo("##Asset", preview); combo)
            {
                (void)ui.InputTextWithHint("##Search", "Search project assets", m_Search);
                ui.Separator();
                if (options.AllowNone && ui.Selectable(options.EmptyLabel, !value))
                {
                    value = {};
                    changed = true;
                    m_Diagnostic.clear();
                    ui.CloseCurrentPopup();
                }

                std::size_t visible = 0;
                for (const auto& candidate : candidates)
                {
                    if (!ContainsInsensitive(candidate.DisplayLabel, m_Search))
                        continue;
                    ++visible;
                    if (ui.Selectable(candidate.SelectionLabel, candidate.Id == value))
                    {
                        value = candidate.Id;
                        changed = true;
                        m_Diagnostic.clear();
                        ui.CloseCurrentPopup();
                    }
                }
                if (visible == 0)
                    ui.Text(m_Search.empty() ? "No compatible assets in this project."
                                             : "No assets match this search.");
            }

            if (auto target = ui.BeginDragTarget(); target)
            {
                std::vector<std::byte> payload;
                if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
                {
                    try
                    {
                        const auto assets = DecodeAssetPayload(payload);
                        const auto dropped = assets.empty() ? std::optional<Keire::AssetId>{}
                                                            : ResolveCompatibleAsset(records, assets.front(), options);
                        if (!dropped)
                            m_Diagnostic = "The dropped asset is not compatible with this field.";
                        else
                        {
                            value = *dropped;
                            changed = true;
                            m_Diagnostic.clear();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        m_Diagnostic = error.what();
                    }
                }
            }
            ui.SameLine();
            const bool reveal = ui.IconButton("Reveal", Keire::UiIcon::Search, false, {24.0F, 0.0F});
            if (value && reveal && options.Reveal)
                options.Reveal(value);
            ui.SameLine();
            ui.Text(options.Label);
        }
        return changed;
    }

    void AssetPicker::Clear() noexcept
    {
        m_Search.clear();
        m_Diagnostic.clear();
    }
} // namespace KeireEditor
