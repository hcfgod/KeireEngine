#include "KeireClient/Editor/ShaderGraphPublication.h"

#include "KeireInternal/FileSystem.h"

#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

namespace KeireEditor
{
    namespace
    {
        class TransactionDirectory final
        {
          public:
            explicit TransactionDirectory(std::filesystem::path path) : m_Path(std::move(path)) {}

            ~TransactionDirectory()
            {
                try
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(m_Path, ignored);
                }
                catch (...)
                {
                    // Transaction cleanup cannot escape a destructor during another failure.
                    (void)0;
                }
            }

            TransactionDirectory(const TransactionDirectory&) = delete;
            TransactionDirectory& operator=(const TransactionDirectory&) = delete;

          private:
            std::filesystem::path m_Path;
        };

        [[nodiscard]] bool SameOrChild(const std::filesystem::path& parent, const std::filesystem::path& candidate)
        {
            const auto relative = candidate.lexically_normal().lexically_relative(parent.lexically_normal());
            return relative.empty() || (!relative.is_absolute() && !relative.generic_string().starts_with(".."));
        }

        [[nodiscard]] std::filesystem::path ResolveConfinedPath(const std::filesystem::path& projectRoot,
                                                                const std::filesystem::path& relative,
                                                                const std::filesystem::path& confinedRoot)
        {
            if (relative.empty() || relative.is_absolute())
                throw std::invalid_argument("Shader Graph publication paths must be project-relative.");

            std::error_code error;
            const auto parent = std::filesystem::weakly_canonical((projectRoot / relative).parent_path(), error);
            if (error || !SameOrChild(confinedRoot, parent))
                throw std::invalid_argument("Shader Graph publication path escaped its asset-owned directory.");
            return (parent / relative.filename()).lexically_normal();
        }

        void CopyMetadataIfPresent(const std::filesystem::path& published, const std::filesystem::path& staged)
        {
            auto publishedMetadata = published;
            publishedMetadata += ".keiremeta";
            std::error_code error;
            if (!std::filesystem::exists(publishedMetadata, error))
            {
                if (error && error != std::errc::no_such_file_or_directory)
                    throw std::runtime_error("Cannot inspect generated Shader Graph metadata: " + error.message());
                return;
            }
            if (!std::filesystem::is_regular_file(publishedMetadata, error) || error)
                throw std::runtime_error("Generated Shader Graph metadata is not a regular file.");

            auto stagedMetadata = staged;
            stagedMetadata += ".keiremeta";
            std::filesystem::copy_file(publishedMetadata, stagedMetadata,
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error)
                throw std::runtime_error("Cannot stage generated Shader Graph metadata: " + error.message());
        }

        [[noreturn]] void RethrowPublicationFailure(const std::exception_ptr& failure,
                                                    const std::error_code& rollbackError)
        {
            if (!rollbackError)
                std::rethrow_exception(failure);
            try
            {
                std::rethrow_exception(failure);
            }
            catch (...)
            {
                std::throw_with_nested(
                    std::runtime_error("Shader Graph publication rollback failed: " + rollbackError.message()));
            }
        }
    } // namespace

    void PublishShaderGraph(const ShaderGraphPublication& publication)
    {
        if (!publication.Asset || publication.ProjectRoot.empty() || publication.SourceDirectory.empty() ||
            publication.GraphRelativePath.empty() || publication.Variants.empty())
        {
            throw std::invalid_argument("Shader Graph publication requires a complete asset specification.");
        }

        std::error_code error;
        const auto projectRoot = std::filesystem::weakly_canonical(publication.ProjectRoot, error);
        if (error)
            throw std::runtime_error("Cannot resolve the Shader Graph project root: " + error.message());
        const auto sourceRoot = std::filesystem::weakly_canonical(projectRoot / publication.SourceDirectory, error);
        if (error || !SameOrChild(projectRoot, sourceRoot))
            throw std::runtime_error("Cannot resolve the Shader Graph source root.");

        const auto generatedRelative =
            publication.SourceDirectory / "Generated" / "ShaderGraphs" / publication.Asset.ToString();
        const auto generatedRoot = std::filesystem::weakly_canonical(projectRoot / generatedRelative, error);
        if (error || !SameOrChild(sourceRoot, generatedRoot))
            throw std::runtime_error("Cannot resolve the Shader Graph generated-shader directory.");
        const auto graphSource = ResolveConfinedPath(sourceRoot, publication.GraphRelativePath, sourceRoot);
        if (graphSource.extension() != ".keireshadergraph")
            throw std::invalid_argument("Shader Graph publication requires a graph source path.");

        const auto transactionRoot =
            projectRoot / "Library" / "Transactions" / ("mg-" + std::to_string(Keire::AssetId::Generate().Low()));
        const auto stagedRoot = transactionRoot / "staged";
        const auto previousRoot = transactionRoot / "previous";
        std::filesystem::create_directories(stagedRoot, error);
        if (error)
            throw std::runtime_error("Cannot create the Shader Graph transaction directory: " + error.message());
        const TransactionDirectory cleanup(transactionRoot);

        std::set<std::filesystem::path> stagedFiles;
        for (const auto& variant : publication.Variants)
        {
            const auto publishedSource = ResolveConfinedPath(projectRoot, variant.GeneratedSource, generatedRoot);
            if (publishedSource.extension() != ".hlsl" ||
                !publishedSource.filename().string().starts_with("ShaderGraph-"))
            {
                throw std::invalid_argument("Generated Shader Graph shader paths must use the owned naming scheme.");
            }
            auto publishedManifest = publishedSource;
            publishedManifest.replace_extension(".keireshader");
            const auto stagedSource = stagedRoot / publishedSource.filename();
            const auto stagedManifest = stagedRoot / publishedManifest.filename();
            if (!stagedFiles.insert(stagedSource).second || !stagedFiles.insert(stagedManifest).second)
                throw std::invalid_argument("Shader Graph publication contains duplicate generated variants.");

            Keire::Detail::WriteTextFileAtomically(stagedSource, variant.Hlsl);
            Keire::Detail::WriteTextFileAtomically(stagedManifest, variant.Manifest);
            CopyMetadataIfPresent(publishedSource, stagedSource);
            CopyMetadataIfPresent(publishedManifest, stagedManifest);
        }

        std::filesystem::create_directories(generatedRoot.parent_path(), error);
        if (error)
            throw std::runtime_error("Cannot create the Shader Graph generated-shader parent: " + error.message());

        const bool hadPrevious = std::filesystem::exists(generatedRoot, error);
        if (error)
            throw std::runtime_error("Cannot inspect the existing Shader Graph publication: " + error.message());
        bool previousMoved = false;
        bool stagedMoved = false;
        try
        {
            if (hadPrevious)
            {
                Keire::Detail::RenamePathWithRetry(generatedRoot, previousRoot);
                previousMoved = true;
            }
            Keire::Detail::RenamePathWithRetry(stagedRoot, generatedRoot);
            stagedMoved = true;
            Keire::Detail::WriteFileAtomically(graphSource, publication.GraphBytes);
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            std::error_code rollbackError;
            if (stagedMoved)
                std::filesystem::remove_all(generatedRoot, rollbackError);
            if (previousMoved)
            {
                std::error_code restoreError;
                if (!Keire::Detail::TryRenamePathWithRetry(previousRoot, generatedRoot, restoreError) && !rollbackError)
                {
                    rollbackError = restoreError;
                }
            }
            RethrowPublicationFailure(failure, rollbackError);
        }
    }
} // namespace KeireEditor
