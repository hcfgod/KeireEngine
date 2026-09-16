#include "Keire/Project/SharedShaderLibrary.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        constexpr std::size_t MaximumBytes = 4U * 1024U * 1024U;
        constexpr std::string_view LockPath = "ProjectSettings/SharedShaders.lock";

        std::string Digest(const std::vector<std::byte>& bytes)
        {
            return Detail::DigestToString(Detail::Sha256(bytes));
        }

        struct PreparedPackage
        {
            SharedShaderPackageReview Review;
            std::vector<Detail::ProjectFileReplacement> Files;
        };

        PreparedPackage Prepare(const std::filesystem::path& root, const std::filesystem::path& packageRoot)
        {
            const Detail::AnchoredFileSystem destination(root);
            const Detail::AnchoredFileSystem package(packageRoot, Detail::AnchoredRootPolicy::RejectLink);
            const auto previous = ReadSharedShaderLibrary(root);
            const auto next = ReadSharedShaderLibrary(package.Root());
            if (previous.Shaders.empty() || next.Shaders.empty() || next.Inputs.empty())
                throw std::invalid_argument("Install a shared library and select a package with pinned inputs.");
            if (previous.Version == next.Version)
                throw std::invalid_argument("A shader package upgrade must select a different version.");
            PreparedPackage prepared;
            prepared.Review.PackageRoot = package.Root();
            prepared.Review.PreviousVersion = previous.Version;
            prepared.Review.Version = next.Version;
            std::string fingerprint;
            std::set<std::string> destinations;
            const auto append = [&](const std::filesystem::path& path, const bool mayReplace)
            {
                auto key = path.generic_string();
                std::ranges::transform(key, key.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!destinations.insert(key).second)
                    throw std::invalid_argument("Shader package files overlap.");
                auto contents = package.Read(path, MaximumBytes);
                std::optional<std::vector<std::byte>> original;
                if (destination.Exists(path))
                {
                    original = destination.Read(path, MaximumBytes);
                    if (!mayReplace && *original != contents)
                        throw std::invalid_argument("Shader package would overwrite an unrelated project input: " +
                                                    key);
                }
                fingerprint += key + "\n" + (original ? Digest(*original) : "absent") + "\n" + Digest(contents) + "\n";
                prepared.Files.push_back({path, std::move(original), std::move(contents)});
            };
            for (const auto& shader : next.Shaders)
            {
                const auto found = std::ranges::find(previous.Shaders, shader.Id, &SharedShaderEntry::Id);
                if (found == previous.Shaders.end() || found->Name != shader.Name ||
                    found->SourcePath != shader.SourcePath || found->Target != shader.Target)
                    throw std::invalid_argument("Shader package must preserve shader identities, paths and targets.");
                const auto path = std::filesystem::path("Assets") / shader.SourcePath;
                const auto graph = ShaderGraphAsset::DecodeSource(package.Read(path, MaximumBytes));
                if (!CompileShaderGraph(graph).Succeeded())
                    throw std::invalid_argument("Shader package contains an invalid shader graph.");
                append(path, true);
                append(std::filesystem::path(path.string() + ".keiremeta"), true);
            }
            for (const auto& input : next.Inputs)
            {
                // A package may only replace files that the previous library already owns as pinned inputs.
                const auto owned = std::ranges::any_of(previous.Inputs, [&](const auto& old)
                                                       { return old.Path == input.Path && old.Kind == input.Kind; });
                append(input.Path, owned);
            }
            append(std::filesystem::path(LockPath), true);
            const auto bytes = std::as_bytes(std::span(fingerprint.data(), fingerprint.size()));
            prepared.Review.Fingerprint = Detail::DigestToString(Detail::Sha256(bytes));
            return prepared;
        }
    } // namespace

    SharedShaderPackageReview ReviewSharedShaderPackage(const std::filesystem::path& projectRoot,
                                                        const std::filesystem::path& packageRoot)
    {
        return Prepare(projectRoot, packageRoot).Review;
    }

    SharedShaderLibrary ApplySharedShaderPackage(const std::filesystem::path& projectRoot,
                                                 const SharedShaderPackageReview& review)
    {
        auto prepared = Prepare(projectRoot, review.PackageRoot);
        if (review.Fingerprint.empty() || prepared.Review != review)
            throw std::runtime_error("Shader package review is stale; review the current package and project again.");
        Detail::PublishMaterialMigrationFiles(projectRoot, prepared.Files);
        return ReadSharedShaderLibrary(projectRoot);
    }
} // namespace Keire
