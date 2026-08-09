#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        void WriteText(const std::filesystem::path& path, std::string_view value);
        [[nodiscard]] std::vector<ManagedBuildDiagnostic> ParseDiagnostics(const std::string& output,
                                                                           std::size_t maximum);
        [[nodiscard]] std::string ManagedApiSourceFingerprint(const std::filesystem::path& project);
        [[nodiscard]] std::string
        GenerateProject(const ManagedAssemblyGraphEntry& assembly, const std::map<AssetId, std::string>& names,
                        const std::filesystem::path& projectRoot, const std::filesystem::path& projectDirectory,
                        const std::filesystem::path& managedApi, const std::filesystem::path& managedApiProject,
                        std::string_view targetFramework, std::string_view languageVersion);
        [[nodiscard]] std::string GenerateSolution(const ManagedBuildRequest& request,
                                                   const std::map<AssetId, std::string>& names,
                                                   const std::filesystem::path& projectRoot,
                                                   const std::filesystem::path& managedApiProject);
        [[nodiscard]] std::string GenerateManagedBuildAggregator(const ManagedBuildRequest& request);
        [[nodiscard]] std::string
        GenerateManagedApiDesignTimeProject(const std::filesystem::path& managedApiSourceProject,
                                            const std::filesystem::path& designTimeProject);
    } // namespace Detail
} // namespace Keire
