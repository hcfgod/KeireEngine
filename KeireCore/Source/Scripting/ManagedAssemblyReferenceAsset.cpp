#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Scripting/ManagedAssemblyPolicies.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <ranges>
#include <stdexcept>

namespace Keire
{
    ManagedAssemblyReferenceAsset::ManagedAssemblyReferenceAsset(std::string reference)
        : m_Reference(std::move(reference))
    {
        if (!m_Reference.empty())
            (void)Encode(m_Reference);
    }
    Ref<ManagedAssemblyReferenceAsset> ManagedAssemblyReferenceAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > 65536)
            throw std::invalid_argument("Invalid assembly reference document size.");
        const auto* text = reinterpret_cast<const char*>(bytes.data());
        const auto json = nlohmann::json::parse(text, text + bytes.size());
        const auto reference = json.at("reference").get<std::string>();
        (void)Encode(reference);
        return CreateRef<ManagedAssemblyReferenceAsset>(reference);
    }
    std::vector<std::byte> ManagedAssemblyReferenceAsset::Encode(const std::string_view reference)
    {
        if (reference.size() > 256 || reference.find_first_of("\r\n/\\") != reference.npos)
            throw std::invalid_argument("Assembly reference requires a custom assembly name or GUID:asset-id.");
        if (reference.starts_with("GUID:") && !AssetId::Parse(reference.substr(5)))
            throw std::invalid_argument("Assembly reference GUID is invalid.");
        const auto text = nlohmann::json{{"reference", reference}}.dump(2) + '\n';
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }
    AssetDecoderRegistration CreateManagedAssemblyReferenceAssetDecoder()
    {
        return {ManagedAssemblyReferenceAsset::StaticType(), CreateRef<ManagedAssemblyReferenceAsset>(),
                [](std::span<const std::byte> bytes) -> Ref<Asset>
                { return ManagedAssemblyReferenceAsset::Decode(bytes); }};
    }
    AssetImporterRegistration CreateManagedAssemblyReferenceAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ManagedAssemblyReference";
        result.Version = 1;
        result.Type = ManagedAssemblyReferenceAsset::StaticType();
        result.Extensions = {".asmref"};
        result.ContextualImport = [](const AssetImportContext&, std::span<const std::byte> bytes)
        {
            const auto asset = ManagedAssemblyReferenceAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = ManagedAssemblyReferenceAsset::Encode(asset->Reference());
            if (asset->Reference().starts_with("GUID:"))
                output.AssetDependencies.push_back(AssetId::Parse(asset->Reference().substr(5)));
            return output;
        };
        return result;
    }

    namespace Detail
    {
        void AddManagedAssemblyReferenceRoots(const std::filesystem::path& projectRoot,
                                              const std::vector<ManagedAssemblyGraphEntry>& assemblies,
                                              std::map<std::filesystem::path, std::size_t>& owners)
        {
            const auto assets = projectRoot / "Assets";
            if (!std::filesystem::is_directory(assets))
                return;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assets))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".asmref")
                    continue;
                const auto relative = std::filesystem::weakly_canonical(entry.path())
                                          .lexically_relative(std::filesystem::weakly_canonical(assets));
                if (relative.empty() || relative.is_absolute() ||
                    std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
                    throw std::invalid_argument("Assembly references must remain inside Assets.");
                const auto text = ReadTextFile(entry.path(), 65536);
                const auto reference =
                    ManagedAssemblyReferenceAsset::Decode(std::as_bytes(std::span(text)))->Reference();
                const auto target =
                    std::ranges::find_if(assemblies,
                                         [&](const auto& assembly)
                                         {
                                             return reference.starts_with("GUID:")
                                                        ? assembly.Asset == AssetId::Parse(reference.substr(5))
                                                        : assembly.Definition.Name == reference;
                                         });
                if (target == assemblies.end())
                    throw std::invalid_argument("Assembly reference target was not found: " + reference);
                const auto folder = entry.path().parent_path().lexically_relative(projectRoot).lexically_normal();
                if (!owners.emplace(folder, static_cast<std::size_t>(target - assemblies.begin())).second)
                    throw std::invalid_argument("A folder cannot contain multiple assembly definitions or references.");
            }
        }
    } // namespace Detail
} // namespace Keire
