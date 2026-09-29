#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "KeireInternal/Scripting/ManagedAssemblyPolicies.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = std::size_t{1024} * 1024U;

        [[nodiscard]] bool IsIdentifier(const std::string_view value, const bool allowDots)
        {
            if (value.empty())
                return false;
            bool segmentStart = true;
            for (const char input : value)
            {
                const auto character = static_cast<unsigned char>(input);
                if (allowDots && character == '.')
                {
                    if (segmentStart)
                        return false;
                    segmentStart = true;
                    continue;
                }
                if (segmentStart ? !(std::isalpha(character) || character == '_')
                                 : !(std::isalnum(character) || character == '_'))
                    return false;
                segmentStart = false;
            }
            return !segmentStart;
        }

        [[nodiscard]] std::string ClassificationName(const ManagedAssemblyClassification classification)
        {
            switch (classification)
            {
            case ManagedAssemblyClassification::Runtime:
                return "runtime";
            case ManagedAssemblyClassification::Editor:
                return "editor";
            case ManagedAssemblyClassification::Tests:
                return "tests";
            }
            throw std::invalid_argument("Managed assembly classification is invalid.");
        }

        [[nodiscard]] ManagedAssemblyClassification ParseClassification(const std::string_view value)
        {
            if (value == "runtime")
                return ManagedAssemblyClassification::Runtime;
            if (value == "editor")
                return ManagedAssemblyClassification::Editor;
            if (value == "tests")
                return ManagedAssemblyClassification::Tests;
            throw std::invalid_argument("Managed assembly classification must be runtime, editor, or tests.");
        }

        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] bool IsExactPackageVersion(const std::string_view value)
        {
            return !value.empty() &&
                   std::ranges::all_of(value,
                                       [](const unsigned char character)
                                       {
                                           return std::isalnum(character) || character == '.' || character == '-' ||
                                                  character == '+';
                                       }) &&
                   std::ranges::any_of(value, [](const unsigned char character) { return std::isdigit(character); });
        }

        [[nodiscard]] bool IsPackageIdentifier(const std::string_view value)
        {
            return !value.empty() && value.front() != '.' && value.front() != '-' && value.back() != '.' &&
                   value.back() != '-' &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isalnum(character) || character == '.' || character == '-' || character == '_'; });
        }
    } // namespace

    ManagedAssemblyAsset::ManagedAssemblyAsset(ManagedAssemblyDefinition definition)
        : m_Definition(std::move(definition))
    {
        if (!m_Definition.Name.empty())
        {
            Validate(m_Definition);
            m_ResidentBytes = Encode(m_Definition).size();
        }
    }

    std::size_t ManagedAssemblyAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    Ref<ManagedAssemblyAsset> ManagedAssemblyAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::invalid_argument("Managed assembly document is empty or exceeds the size limit.");
        const auto* characters = reinterpret_cast<const char*>(bytes.data());
        const auto document = Json::parse(characters, characters + bytes.size());
        if (!document.is_object())
            throw std::invalid_argument("Managed assembly document root must be an object.");

        ManagedAssemblyDefinition definition;
        definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        definition.Name = document.at("name").get<std::string>();
        definition.RootNamespace = document.value("rootNamespace", definition.Name);
        definition.Classification = ParseClassification(document.value("classification", "runtime"));
        for (const auto& root : document.value("sourceRoots", Json::array()))
            definition.SourceRoots.emplace_back(root.get<std::string>());
        for (const auto& reference : document.value("references", Json::array()))
            definition.References.push_back(AssetId::Parse(reference.get<std::string>()));
        if (definition.SchemaVersion >= 2)
        {
            for (const auto& package : document.value("packages", Json::array()))
                definition.Packages.push_back(
                    {package.at("name").get<std::string>(), package.at("version").get<std::string>()});
            definition.DefineSymbols = document.value("defineSymbols", std::vector<std::string>{});
            definition.AllowUnsafe = document.value("allowUnsafe", false);
        }
        definition.AutoReferenced = document.value("autoReferenced", true);
        definition.IncludePlatforms = document.value("includePlatforms", std::vector<std::string>{});
        definition.ExcludePlatforms = document.value("excludePlatforms", std::vector<std::string>{});
        definition.DefineConstraints = document.value("defineConstraints", std::vector<std::string>{});
        definition.OverrideReferences = document.value("overrideReferences", false);
        for (const auto& path : document.value("precompiledReferences", Json::array()))
            definition.PrecompiledReferences.emplace_back(path.get<std::string>());
        for (const auto& item : document.value("versionDefines", Json::array()))
            definition.VersionDefines.push_back({item.at("resource").get<std::string>(),
                                                 item.at("expression").get<std::string>(),
                                                 item.at("define").get<std::string>()});
        Validate(definition);
        return CreateRef<ManagedAssemblyAsset>(std::move(definition));
    }

    std::vector<std::byte> ManagedAssemblyAsset::Encode(const ManagedAssemblyDefinition& definition)
    {
        Validate(definition);
        Json document{{"schemaVersion", definition.SchemaVersion},
                      {"name", definition.Name},
                      {"rootNamespace", definition.RootNamespace},
                      {"classification", ClassificationName(definition.Classification)},
                      {"sourceRoots", Json::array()},
                      {"references", Json::array()}};
        for (const auto& root : definition.SourceRoots)
            document["sourceRoots"].push_back(PathText(root));
        for (const auto reference : definition.References)
            document["references"].push_back(reference.ToString());
        if (definition.SchemaVersion >= 3)
            document["autoReferenced"] = definition.AutoReferenced;
        if (definition.SchemaVersion >= 2)
        {
            document["packages"] = Json::array();
            for (const auto& package : definition.Packages)
                document["packages"].push_back({{"name", package.Name}, {"version", package.Version}});
            document["defineSymbols"] = definition.DefineSymbols;
            document["allowUnsafe"] = definition.AllowUnsafe;
        }
        if (definition.SchemaVersion >= 4)
        {
            document["includePlatforms"] = definition.IncludePlatforms;
            document["excludePlatforms"] = definition.ExcludePlatforms;
            document["defineConstraints"] = definition.DefineConstraints;
            document["overrideReferences"] = definition.OverrideReferences;
            document["precompiledReferences"] = Json::array();
            for (const auto& path : definition.PrecompiledReferences)
                document["precompiledReferences"].push_back(PathText(path));
            document["versionDefines"] = Json::array();
            for (const auto& item : definition.VersionDefines)
                document["versionDefines"].push_back(
                    {{"resource", item.Resource}, {"expression", item.Expression}, {"define", item.Define}});
        }
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    void ManagedAssemblyAsset::Validate(const ManagedAssemblyDefinition& definition)
    {
        if (definition.SchemaVersion < 1 || definition.SchemaVersion > ManagedAssemblySchemaVersion)
            throw std::invalid_argument("Managed assembly definition uses an unsupported schema version.");
        Detail::ValidateManagedAssemblyPolicies(definition);
        auto identifier = definition.Name;
        std::ranges::replace(identifier, '-', '_');
        if (!IsIdentifier(identifier, true) || !IsIdentifier(definition.RootNamespace, true))
            throw std::invalid_argument("Managed assembly name or root namespace is not a valid C# identifier.");
        if ((definition.SourceRoots.empty() && definition.SchemaVersion < 3) || definition.SourceRoots.size() > 64 ||
            definition.References.size() > 256 || definition.Packages.size() > 256 ||
            definition.DefineSymbols.size() > 256)
            throw std::invalid_argument("Managed assembly source-root or reference count is invalid.");
        if (definition.SchemaVersion < 3 && !definition.AutoReferenced)
            throw std::invalid_argument("Auto Referenced requires managed assembly schema version 3.");
        if (definition.SchemaVersion == 1 &&
            (!definition.Packages.empty() || !definition.DefineSymbols.empty() || definition.AllowUnsafe))
            throw std::invalid_argument("Managed assembly schema version 1 cannot contain schema version 2 settings.");

        std::set<std::string, std::less<>> roots;
        for (const auto& root : definition.SourceRoots)
        {
            const auto normalized = root.lexically_normal();
            const auto text = PathText(normalized);
            if (root.empty() || root.is_absolute() || normalized.empty() || normalized == "." ||
                std::ranges::any_of(normalized, [](const auto& part) { return part == ".."; }) ||
                !roots.insert(text).second)
                throw std::invalid_argument("Managed assembly source roots must be unique project-relative paths.");
        }
        if (std::ranges::any_of(definition.References, [](const AssetId reference) { return !reference; }))
            throw std::invalid_argument("Managed assembly references must contain valid asset IDs.");
        auto references = definition.References;
        std::ranges::sort(references);
        if (std::adjacent_find(references.begin(), references.end()) != references.end())
            throw std::invalid_argument("Managed assembly references must be unique.");

        std::set<std::string, std::less<>> packages;
        for (const auto& package : definition.Packages)
        {
            if (!IsPackageIdentifier(package.Name) || !IsExactPackageVersion(package.Version) ||
                !packages.insert(package.Name).second)
                throw std::invalid_argument(
                    "Managed packages must have unique identifiers and exact non-floating versions.");
        }
        std::set<std::string, std::less<>> symbols;
        for (const auto& symbol : definition.DefineSymbols)
        {
            if (!IsIdentifier(symbol, false) || !symbols.insert(symbol).second)
                throw std::invalid_argument("Managed assembly define symbols must be unique C# identifiers.");
        }
    }

    void ValidateManagedAssemblyGraph(const std::span<const ManagedAssemblyGraphEntry> assemblies)
    {
        std::map<AssetId, const ManagedAssemblyDefinition*> definitions;
        std::set<std::string, std::less<>> names;
        for (const auto& assembly : assemblies)
        {
            if (!assembly.Asset || !definitions.emplace(assembly.Asset, &assembly.Definition).second)
                throw std::invalid_argument("Managed assembly graph contains a missing or duplicate asset ID.");
            ManagedAssemblyAsset::Validate(assembly.Definition);
            auto foldedName = assembly.Definition.Name;
            std::ranges::transform(foldedName, foldedName.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            if (!names.insert(foldedName).second)
                throw std::invalid_argument("Managed assembly names must be unique.");
        }

        std::map<AssetId, std::uint8_t> state;
        const auto visit = [&](const auto& self, const AssetId asset) -> void
        {
            if (state[asset] == 1)
                throw std::invalid_argument("Managed assembly references contain a cycle.");
            if (state[asset] == 2)
                return;
            state[asset] = 1;
            for (const auto reference : definitions.at(asset)->References)
            {
                if (!definitions.contains(reference))
                    throw std::invalid_argument("Managed assembly references an unavailable assembly definition.");
                const auto source = definitions.at(asset)->Classification;
                const auto target = definitions.at(reference)->Classification;
                if ((source == ManagedAssemblyClassification::Runtime &&
                     target != ManagedAssemblyClassification::Runtime) ||
                    (source == ManagedAssemblyClassification::Editor && target == ManagedAssemblyClassification::Tests))
                    throw std::invalid_argument("Managed assembly reference violates runtime/editor/test isolation.");
                self(self, reference);
            }
            state[asset] = 2;
        };
        for (const auto& [asset, definition] : definitions)
        {
            (void)definition;
            visit(visit, asset);
        }
    }

    AssetDecoderRegistration CreateManagedAssemblyAssetDecoder()
    {
        return {ManagedAssemblyAsset::StaticType(), CreateRef<ManagedAssemblyAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return ManagedAssemblyAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateManagedAssemblyAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ManagedAssembly";
        result.Version = 4;
        result.Type = ManagedAssemblyAsset::StaticType();
        result.Extensions = {".keireasm"};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto assembly = ManagedAssemblyAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = ManagedAssemblyAsset::Encode(assembly->Definition());
            output.AssetDependencies = assembly->Definition().References;
            return output;
        };
        return result;
    }
} // namespace Keire
