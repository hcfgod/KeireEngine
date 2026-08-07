#pragma once

#include "Keire/Api.h"
#include "Keire/Project/Project.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Keire
{
    struct ProjectUpgradeStepContext
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path StagingRoot;
    };

    struct ProjectUpgradeStep
    {
        std::string Id;
        std::uint32_t FromSchema = 0;
        std::uint32_t ToSchema = 0;
        std::vector<std::filesystem::path> AffectedPaths;
        std::string Warning;
        std::function<void(const ProjectUpgradeStepContext&)> Apply;
        std::function<void(const ProjectUpgradeStepContext&)> Validate;
    };

    struct ProjectUpgradePlanStep
    {
        std::string Id;
        std::uint32_t FromSchema = 0;
        std::uint32_t ToSchema = 0;
        std::vector<std::filesystem::path> AffectedPaths;
        std::string Warning;
    };

    struct ProjectUpgradePlan
    {
        std::filesystem::path ProjectRoot;
        std::uint32_t CurrentSchema = 0;
        std::uint32_t TargetSchema = CurrentProjectSchemaVersion;
        std::vector<ProjectUpgradePlanStep> Steps;
        std::vector<std::filesystem::path> AffectedPaths;
        std::vector<std::string> Warnings;
        std::vector<RequiredSourceModule> RequiredModules;
        std::uintmax_t EstimatedBackupBytes = 0;
    };

    enum class ProjectUpgradeTransactionState : std::uint8_t
    {
        Clean,
        Interrupted
    };

    class KEIRE_API ProjectUpgradeService final
    {
      public:
        explicit ProjectUpgradeService(std::filesystem::path projectRoot,
                                       std::vector<ProjectUpgradeStep> additionalSteps = {});
        ~ProjectUpgradeService();

        ProjectUpgradeService(const ProjectUpgradeService&) = delete;
        ProjectUpgradeService& operator=(const ProjectUpgradeService&) = delete;

        [[nodiscard]] ProjectUpgradePlan Plan() const;
        void Apply(const ProjectUpgradePlan& plan);
        void Recover();
        void Rollback();
        [[nodiscard]] ProjectUpgradeTransactionState State() const noexcept;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;

        [[nodiscard]] static ProjectUpgradeStep CreateVersion1To2Step();
        [[nodiscard]] static ProjectUpgradeStep CreateVersion2To3Step();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
