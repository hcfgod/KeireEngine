#include "Keire/Project/SharedShaderLibrary.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::string_view Version = "1.0.0";
        constexpr std::string_view LockPath = "ProjectSettings/SharedShaders.lock";
        constexpr std::size_t MaximumBytes = 4U * 1024U * 1024U;

        bool ValidVersion(const std::string& version)
        {
            unsigned int components = 1;
            bool digit = false;
            for (const auto value : version)
            {
                if (value == '.' && digit)
                {
                    ++components;
                    digit = false;
                }
                else if (value >= '0' && value <= '9')
                    digit = true;
                else
                    return false;
            }
            return digit && components == 3 && version.size() <= 32;
        }

        std::vector<std::byte> Bytes(const std::string_view value)
        {
            const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
            return {bytes.begin(), bytes.end()};
        }

        std::string Digest(const std::span<const std::byte> bytes)
        {
            return Detail::DigestToString(Detail::Sha256(bytes));
        }

        AssetId Identity(const std::string& name)
        {
            const auto digest = Digest(Bytes("Keire/SharedShaders/1/" + name));
            return AssetId::Parse(digest.substr(0, 8) + "-" + digest.substr(8, 4) + "-" + digest.substr(12, 4) + "-" +
                                  digest.substr(16, 4) + "-" + digest.substr(20, 12));
        }

        ShaderGraphDefinition Definition(const ShaderGraphTemplate graphTemplate, const std::string& name)
        {
            auto graph = CreateShaderGraphTemplate(graphTemplate);
            auto& output = graph.Nodes.front();
            output.Id = Identity(name + "/output");
            for (auto& pin : output.Pins)
                pin.Id = Identity(name + "/output/" + pin.Name);
            const auto outputId = output.Id;
            const auto pins = output.Pins;
            for (const auto& pin : pins)
            {
                if (pin.Name != "BaseColor" && pin.Name != "Color" && pin.Name != "Metallic" &&
                    pin.Name != "Roughness" && pin.Name != "Emission" && pin.Name != "Opacity")
                    continue;
                auto node = CreateShaderGraphNode(ShaderGraphNodeKind::Parameter, pin.Type);
                node.Symbol = pin.Name == "Color" ? "BaseColor" : pin.Name;
                node.Name = node.Symbol;
                node.Id = Identity(name + "/property/" + node.Symbol);
                node.Value = pin.DefaultValue;
                node.ParameterMetadata.Category = "Surface";
                if (pin.Type == ShaderGraphValueType::Scalar)
                {
                    node.ParameterMetadata.Minimum = 0.0F;
                    node.ParameterMetadata.Maximum = 1.0F;
                }
                node.EditorPosition = {40.0F, 120.0F + 120.0F * static_cast<float>(graph.Nodes.size() - 1)};
                for (auto& propertyPin : node.Pins)
                    propertyPin.Id = Identity(name + "/property/" + node.Symbol + "/" + propertyPin.Name);
                graph.Connections.push_back({Identity(name + "/connection/" + node.Symbol),
                                             {node.Id, node.Pins.front().Id},
                                             {outputId, pin.Id},
                                             {}});
                graph.Nodes.push_back(std::move(node));
            }
            return graph;
        }
    } // namespace

    bool IsSharedShaderPath(const std::filesystem::path& sourceRelativePath)
    {
        auto path = sourceRelativePath.lexically_normal().generic_string();
        std::ranges::transform(path, path.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return path == "keire/sharedshaders" || path.starts_with("keire/sharedshaders/");
    }

    static SharedShaderLibrary ReadLibrary(const std::filesystem::path& projectRoot, const bool verifyInputs)
    {
        const Detail::AnchoredFileSystem fs(projectRoot);
        if (!fs.Exists(LockPath))
            return {};
        const auto bytes = fs.Read(LockPath, MaximumBytes);
        const auto lock = Json::parse(bytes);
        if ((lock.at("schemaVersion") != 1 && lock.at("schemaVersion") != 2) ||
            (lock.at("schemaVersion") == 1 && lock.at("version").get<std::string>() != Version) ||
            !ValidVersion(lock.at("version").get<std::string>()) || !lock.at("shaders").is_array() ||
            lock.at("shaders").size() != 6)
            throw std::runtime_error("Shared shaders require an explicit supported library upgrade.");
        SharedShaderLibrary result{lock.at("version").get<std::string>(), {}};
        if (lock.at("schemaVersion") == 2 && verifyInputs)
        {
            if (lock.at("graphImporterVersion") != CreateShaderGraphAssetImporter().Version)
                throw std::runtime_error("Shared shader compiler contract changed; review the package inputs.");
            std::vector<SharedShaderInput> inputs;
            for (const auto& input : lock.at("inputs"))
            {
                const auto kind = input.at("kind").get<unsigned int>();
                if (kind > static_cast<unsigned int>(SharedShaderInputKind::PackageLock))
                    throw std::runtime_error("Shared shader lock contains an unsupported input kind.");
                inputs.push_back({static_cast<SharedShaderInputKind>(kind), input.at("path").get<std::string>(),
                                  input.at("sha256").get<std::string>()});
            }
            const auto review = ReviewSharedShaderInputs(projectRoot, inputs);
            if (review.Inputs != inputs)
                throw std::runtime_error("Pinned shared shader inputs changed; review the package inputs.");
            result.Inputs = std::move(inputs);
        }
        for (const auto& entry : lock.at("shaders"))
        {
            const auto name = entry.at("name").get<std::string>();
            const auto path = std::filesystem::path(entry.at("path").get<std::string>());
            const auto id = AssetId::Parse(entry.at("id").get<std::string>());
            if (!IsSharedShaderPath(path) || path.is_absolute() || path != path.lexically_normal() ||
                id != Identity(name) ||
                std::ranges::any_of(result.Shaders,
                                    [&](const auto& other) { return other.Id == id || other.SourcePath == path; }))
                throw std::runtime_error("Shared shader lock contains invalid or duplicate identities.");
            const auto source = fs.Read(std::filesystem::path("Assets") / path, MaximumBytes);
            if (Digest(source) != entry.at("sha256").get<std::string>())
                throw std::runtime_error("A pinned shared shader changed. Restore it or copy it to an editable asset.");
            const auto metadata =
                Json::parse(fs.Read(std::filesystem::path("Assets") / (path.string() + ".keiremeta"), MaximumBytes));
            if (metadata.at("id") != id.ToString())
                throw std::runtime_error("A pinned shared shader's source identity changed.");
            const auto graph = ShaderGraphAsset::DecodeSource(source);
            result.Shaders.push_back({id, name, path, graph.Target.Target});
        }
        return result;
    }

    SharedShaderLibrary ReadSharedShaderLibrary(const std::filesystem::path& projectRoot)
    {
        return ReadLibrary(projectRoot, true);
    }

    std::vector<SharedShaderInput> DefaultSharedShaderInputs()
    {
        return {{SharedShaderInputKind::Compiler, "ProjectSettings/SharedShaderInputs/Compiler.json", {}},
                {SharedShaderInputKind::Include, "ProjectSettings/SharedShaderInputs/Includes.json", {}},
                {SharedShaderInputKind::VisualFixture, "ProjectSettings/SharedShaderInputs/VisualFixtures.json", {}},
                {SharedShaderInputKind::PackageLock, "Packages/packages-lock.keirejson", {}}};
    }

    SharedShaderUpgradeReview ReviewSharedShaderInputs(const std::filesystem::path& projectRoot)
    {
        return ReviewSharedShaderInputs(projectRoot, DefaultSharedShaderInputs());
    }

    SharedShaderUpgradeReview ReviewSharedShaderInputs(const std::filesystem::path& projectRoot,
                                                       const std::vector<SharedShaderInput>& inputs)
    {
        if (ReadLibrary(projectRoot, false).Shaders.empty())
            throw std::runtime_error("Install the shared shader library before reviewing its inputs.");
        if (inputs.size() > 1024)
            throw std::invalid_argument("Shared shader input count exceeds the package limit.");
        const Detail::AnchoredFileSystem fs(projectRoot);
        SharedShaderUpgradeReview review;
        review.PreviousLockSha256 = Digest(fs.Read(LockPath, MaximumBytes));
        review.GraphImporterVersion = CreateShaderGraphAssetImporter().Version;
        std::array<bool, 4> kinds{};
        std::set<std::string> paths;
        for (const auto& input : inputs)
        {
            const auto kind = static_cast<std::size_t>(input.Kind);
            const auto& path = input.Path;
            if (kind >= kinds.size() || path.empty() || path.is_absolute() || path.has_root_name() ||
                path != path.lexically_normal() || path == LockPath ||
                std::ranges::any_of(path, [](const auto& part) { return part == ".." || part == "."; }))
                throw std::invalid_argument("Shared shader input has an invalid kind or project-relative path.");
            auto key = path.generic_string();
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (key == "projectsettings/sharedshaders.lock")
                throw std::invalid_argument("The shared shader lock cannot pin itself as an input.");
            if (!paths.insert(key).second)
                throw std::invalid_argument("Shared shader inputs contain duplicate paths.");
            if (!fs.Exists(path))
                throw std::runtime_error("Shared shader review requires the missing input: " + path.generic_string());
            kinds[kind] = true;
            review.Inputs.push_back({input.Kind, path, Digest(fs.Read(path, MaximumBytes))});
        }
        if (!std::ranges::all_of(kinds, [](const bool present) { return present; }))
            throw std::invalid_argument(
                "Shared shaders require compiler, include, visual fixture and package lock inputs.");
        return review;
    }

    SharedShaderLibrary ApplySharedShaderInputs(const std::filesystem::path& projectRoot,
                                                const SharedShaderUpgradeReview& review)
    {
        if (ReviewSharedShaderInputs(projectRoot, review.Inputs) != review)
            throw std::runtime_error("Shared shader upgrade review is stale; review the current inputs again.");
        const Detail::AnchoredFileSystem fs(projectRoot);
        const auto original = fs.Read(LockPath, MaximumBytes);
        auto lock = Json::parse(original);
        lock["schemaVersion"] = 2;
        lock["graphImporterVersion"] = review.GraphImporterVersion;
        lock["inputs"] = Json::array();
        for (const auto& input : review.Inputs)
            lock["inputs"].push_back({{"kind", static_cast<unsigned int>(input.Kind)},
                                      {"path", input.Path.generic_string()},
                                      {"sha256", input.Sha256}});
        const auto contents = Bytes(lock.dump(2) + '\n');
        if (contents != original)
        {
            const std::array files{Detail::ProjectFileReplacement{std::filesystem::path(LockPath), original, contents}};
            Detail::PublishMaterialMigrationFiles(projectRoot, files);
        }
        return ReadSharedShaderLibrary(projectRoot);
    }

    SharedShaderLibrary EnsureSharedShaderLibrary(const std::filesystem::path& projectRoot)
    {
        auto existing = ReadSharedShaderLibrary(projectRoot);
        if (!existing.Version.empty())
            return existing;
        const Detail::AnchoredFileSystem fs(projectRoot);
        constexpr std::array templates{ShaderGraphTemplate::Lit, ShaderGraphTemplate::Unlit,
                                       ShaderGraphTemplate::Ui,  ShaderGraphTemplate::Fullscreen,
                                       ShaderGraphTemplate::Vfx, ShaderGraphTemplate::CustomGraphics};
        constexpr std::array names{"Lit", "Unlit", "UI", "Fullscreen", "VFX", "CustomGraphics"};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot / "Assets"))
        {
            if (entry.is_symlink())
                throw std::runtime_error("Shared shader installation requires source paths without symbolic links.");
            if (!entry.is_regular_file() || entry.path().extension() != ".keiremeta")
                continue;
            const auto metadata = Json::parse(fs.Read(entry.path().lexically_relative(projectRoot), MaximumBytes));
            for (const auto name : names)
                if (metadata.at("id") == Identity(std::string("Kéire/") + name).ToString())
                    throw std::runtime_error("A shared shader identity is already owned by another source asset.");
        }
        Json entries = Json::array();
        std::vector<Detail::ProjectFileReplacement> files;
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            const std::string name = std::string("Kéire/") + names[index];
            const auto id = Identity(name);
            const auto path = std::filesystem::path("Keire/SharedShaders") / Version /
                              (std::string(names[index]) + ".keireshadergraph");
            const auto source = ShaderGraphAsset::EncodeSource(Definition(templates[index], name));
            const auto metadata = Json{{"schemaVersion", 1},
                                       {"id", id.ToString()},
                                       {"type", ShaderGraphAsset::StaticType().Value().ToString()},
                                       {"importer", "Keire.ShaderGraph"},
                                       {"importerVersion", CreateShaderGraphAssetImporter().Version},
                                       {"dependencies", Json::array()},
                                       {"subAssets", Json::array()}};
            files.push_back({std::filesystem::path("Assets") / path, std::nullopt, source});
            files.push_back({std::filesystem::path("Assets") / (path.string() + ".keiremeta"), std::nullopt,
                             Bytes(metadata.dump(2) + '\n')});
            entries.push_back(
                {{"name", name}, {"id", id.ToString()}, {"path", path.generic_string()}, {"sha256", Digest(source)}});
        }
        files.push_back({std::filesystem::path(LockPath), std::nullopt,
                         Bytes(Json{{"schemaVersion", 1}, {"version", Version}, {"shaders", entries}}.dump(2) + '\n')});
        Detail::PublishMaterialMigrationFiles(projectRoot, files);
        return ReadSharedShaderLibrary(projectRoot);
    }
} // namespace Keire
