#include "KeireInternal/Assets/ImportedMaterialShader.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        std::optional<ImportedMaterialShader> FindImportedMaterialShader(const AssetImportContext& context)
        {
            constexpr std::size_t MaximumShaderManifestBytes = std::size_t{16U} * 1024U * 1024U;
            constexpr std::size_t MaximumShaderMetadataBytes = std::size_t{1024U} * 1024U;
            const auto resolve = [&context](const std::filesystem::path& relative)
            { return ResolveConfinedPath(context.SourceRoot, relative); };
            const auto regularFile = [&resolve](const std::filesystem::path& relative)
            {
                std::error_code error;
                const bool regular = std::filesystem::is_regular_file(resolve(relative), error);
                if (error)
                {
                    if (error == std::errc::no_such_file_or_directory)
                        return false;
                    throw std::runtime_error("Could not inspect a project material shader candidate: " +
                                             error.message());
                }
                return regular;
            };
            const auto read =
                [&context, &resolve](const std::filesystem::path& relative, const std::size_t maximumBytes)
            {
                const auto confined = resolve(relative);
                if (context.ReadProjectFile)
                {
                    const auto projectRelative = std::filesystem::relative(confined, context.ProjectRoot);
                    auto bytes = context.ReadProjectFile(projectRelative);
                    if (bytes.size() > maximumBytes)
                        throw std::runtime_error("Project material shader input exceeds its safety limit.");
                    if (bytes.empty())
                        return std::string{};
                    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                }
                return ReadTextFile(resolve(relative), maximumBytes);
            };

            const std::filesystem::path shaderRootRelative = "Shaders";
            const auto shaderRoot = resolve(shaderRootRelative);
            std::error_code error;
            const bool shaderDirectory = std::filesystem::is_directory(shaderRoot, error);
            if (error)
            {
                if (error == std::errc::no_such_file_or_directory)
                    return std::nullopt;
                throw std::runtime_error("Could not inspect the project material shader directory: " + error.message());
            }
            if (!shaderDirectory)
                return std::nullopt;

            std::vector<std::filesystem::path> candidates;
            const auto standard = shaderRootRelative / "DefaultUnlit.keireshader";
            if (regularFile(standard))
                candidates.push_back(standard);
            for (std::filesystem::recursive_directory_iterator iterator(shaderRoot, error), end;
                 !error && iterator != end; iterator.increment(error))
            {
                const auto relative = shaderRootRelative / iterator->path().lexically_relative(shaderRoot);
                if (relative.extension() == ".keireshader" && relative != standard && regularFile(relative))
                    candidates.push_back(relative);
            }
            if (error)
                throw std::runtime_error("Could not enumerate the project material shader directory: " +
                                         error.message());
            std::ranges::stable_sort(candidates,
                                     [&standard](const std::filesystem::path& left, const std::filesystem::path& right)
                                     {
                                         const bool leftIsDefault = left == standard;
                                         const bool rightIsDefault = right == standard;
                                         if (leftIsDefault != rightIsDefault)
                                             return leftIsDefault;
                                         return PathToUtf8(left) < PathToUtf8(right);
                                     });

            for (const auto& relative : candidates)
            {
                const auto metadata = PathWithSuffix(relative, ".keiremeta");
                if (!regularFile(metadata))
                    continue;
                const auto metadataText = read(metadata, MaximumShaderMetadataBytes);
                const auto manifestText = read(relative, MaximumShaderManifestBytes);
                try
                {
                    const auto metadataJson = nlohmann::json::parse(metadataText);
                    const auto manifest = nlohmann::json::parse(manifestText);
                    ImportedMaterialShader result;
                    result.Id = AssetId::Parse(metadataJson.at("id").get<std::string>());
                    for (const auto& property : manifest.at("properties"))
                        result.Properties.insert(property.at("name").get<std::string>());
                    if (result.Id)
                        return result;
                }
                catch (const std::exception&)
                {
                }
            }
            return std::nullopt;
        }
    } // namespace Detail
} // namespace Keire
