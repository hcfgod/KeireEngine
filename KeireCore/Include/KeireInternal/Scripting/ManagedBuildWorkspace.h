#pragma once

#include "Keire/Scripting/ScriptSystem.h"
#include "KeireInternal/Process.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        // Access only from the build worker, or after that worker has joined.
        class ManagedCompilerServer final
        {
          public:
            ~ManagedCompilerServer();
            [[nodiscard]] bool Prepare(const std::filesystem::path& dotnet, const std::filesystem::path& project,
                                       const std::filesystem::path& workingDirectory, std::stop_token cancellation);
            void Stop() noexcept;
            [[nodiscard]] const std::string& PipeName() const noexcept { return m_PipeName; }
            [[nodiscard]] std::uint64_t ProcessId() const noexcept;

          private:
            std::optional<ChildProcess> m_Process;
            std::filesystem::path m_Dotnet;
            std::filesystem::path m_Compiler;
            std::filesystem::path m_WorkingDirectory;
            std::string m_PipeName;
            std::string m_SdkInputs;
        };

        void WriteText(const std::filesystem::path& path, std::string_view value);
        [[nodiscard]] std::vector<ManagedBuildDiagnostic> ParseDiagnostics(const std::string& output,
                                                                           std::size_t maximum);
        [[nodiscard]] std::string ManagedApiSourceFingerprint(const std::filesystem::path& project);
        [[nodiscard]] std::string
        GenerateProject(const ManagedAssemblyGraphEntry& assembly, const std::map<AssetId, std::string>& names,
                        const std::filesystem::path& projectRoot, const std::filesystem::path& projectDirectory,
                        const std::filesystem::path& managedApi, const std::filesystem::path& managedApiProject,
                        const std::filesystem::path& managedEditorApi, const std::filesystem::path& managedGenerator,
                        bool includeEditorApi, std::string_view targetFramework, std::string_view languageVersion);
        [[nodiscard]] std::string
        GenerateSolution(const ManagedBuildRequest& request, const std::map<AssetId, std::string>& names,
                         const std::filesystem::path& projectRoot, const std::filesystem::path& managedApiProject,
                         const std::map<AssetId, std::filesystem::path>& designTimeProjects = {});
        [[nodiscard]] std::string GenerateManagedBuildAggregator(const ManagedBuildRequest& request);
        [[nodiscard]] std::string GenerateManagedIdeAggregator(const ManagedBuildRequest& request,
                                                               std::string_view targetFramework);
        [[nodiscard]] std::string
        GenerateManagedApiDesignTimeProject(const std::filesystem::path& managedApiSourceProject,
                                            const std::filesystem::path& designTimeProject);
        [[nodiscard]] std::string GenerateUserAssemblyDesignTimeProject(
            const ManagedAssemblyGraphEntry& assembly, const std::map<AssetId, std::string>& names,
            const std::filesystem::path& managedApi, const std::filesystem::path& managedApiDesignTimeProject,
            const std::filesystem::path& managedEditorApi, const std::filesystem::path& managedGenerator,
            const std::filesystem::path& projectRoot, const std::filesystem::path& projectDirectory,
            std::string_view targetFramework, std::string_view languageVersion);

    } // namespace Detail
} // namespace Keire
