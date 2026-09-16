#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Keire
{
    struct SharedShaderEntry
    {
        AssetId Id;
        std::string Name;
        std::filesystem::path SourcePath;
        ShaderGraphTarget Target = ShaderGraphTarget::Material;
    };

    enum class SharedShaderInputKind : std::uint8_t
    {
        Compiler,
        Include,
        VisualFixture,
        PackageLock
    };

    struct SharedShaderInput
    {
        SharedShaderInputKind Kind = SharedShaderInputKind::Include;
        /// Project-relative file. Compiler inputs should contain the compiler/toolchain identity manifest.
        std::filesystem::path Path;
        std::string Sha256;
        [[nodiscard]] bool operator==(const SharedShaderInput&) const = default;
    };

    struct SharedShaderLibrary
    {
        std::string Version;
        std::vector<SharedShaderEntry> Shaders;
        std::vector<SharedShaderInput> Inputs;
    };

    struct SharedShaderUpgradeReview
    {
        std::string PreviousLockSha256;
        std::vector<SharedShaderInput> Inputs;
        std::uint32_t GraphImporterVersion = 0;
        [[nodiscard]] bool operator==(const SharedShaderUpgradeReview&) const = default;
    };

    struct SharedShaderPackageReview
    {
        std::filesystem::path PackageRoot;
        std::string PreviousVersion;
        std::string Version;
        std::string Fingerprint;
        [[nodiscard]] bool operator==(const SharedShaderPackageReview&) const = default;
    };

    /// Reviews a schema-2 package with the same six shader identities and source paths as the installed library.
    /// Package input files are project-relative and must not overwrite unrelated project files.
    [[nodiscard]] KEIRE_API SharedShaderPackageReview
    ReviewSharedShaderPackage(const std::filesystem::path& projectRoot, const std::filesystem::path& packageRoot);
    /// Revalidates both trees and publishes source, metadata, input files and lock as one recoverable transaction.
    /// Caller owns exclusive project access and pauses imports and source mutations throughout publication.
    [[nodiscard]] KEIRE_API SharedShaderLibrary ApplySharedShaderPackage(const std::filesystem::path& projectRoot,
                                                                         const SharedShaderPackageReview& review);

    /// Reviews adoption of build/package/visual inputs without changing shader bytes or stable identities.
    /// Requires at least one input of each kind; hashes are captured from disk, not trusted from the caller.
    [[nodiscard]] KEIRE_API SharedShaderUpgradeReview
    ReviewSharedShaderInputs(const std::filesystem::path& projectRoot, const std::vector<SharedShaderInput>& inputs);
    /// Conventional project inputs; this does not create evidence or claim that visual validation passed.
    [[nodiscard]] KEIRE_API std::vector<SharedShaderInput> DefaultSharedShaderInputs();
    [[nodiscard]] KEIRE_API SharedShaderUpgradeReview
    ReviewSharedShaderInputs(const std::filesystem::path& projectRoot);
    /// Publishes only the exact reviewed lock and inputs. Caller owns exclusive project access.
    [[nodiscard]] KEIRE_API SharedShaderLibrary ApplySharedShaderInputs(const std::filesystem::path& projectRoot,
                                                                        const SharedShaderUpgradeReview& review);

    /// Installs once on explicit material creation. Existing pinned sources are verified, never regenerated.
    /// The caller must own the project's exclusive access and pause source mutations during publication.
    [[nodiscard]] KEIRE_API SharedShaderLibrary EnsureSharedShaderLibrary(const std::filesystem::path& projectRoot);
    /// Returns an empty library when the project has not opted in. Missing or modified pinned content is an error.
    [[nodiscard]] KEIRE_API SharedShaderLibrary ReadSharedShaderLibrary(const std::filesystem::path& projectRoot);
    /// Paths are relative to Assets. The namespace is reserved for immutable shared shader sources.
    [[nodiscard]] KEIRE_API bool IsSharedShaderPath(const std::filesystem::path& sourceRelativePath);
} // namespace Keire
