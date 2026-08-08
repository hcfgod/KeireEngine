#include "KeireHub/HubPathMigration.h"

#include <array>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        constexpr std::u8string_view CanonicalComponent = u8"Kéire";
        constexpr std::u8string_view LegacyComponent = u8"KÃ©ire";

        [[nodiscard]] HubError MigrationError(const std::filesystem::path& path, const std::error_code& error)
        {
            return {.Code = HubErrorCode::MigrationFailed,
                    .Message = "Kéire Hub could not migrate its legacy storage directory.",
                    .Retryable = true,
                    .AffectedItem = path.filename().string(),
                    .TechnicalDetails = error.message()};
        }

        [[nodiscard]] HubStatus MigrateDirectory(const std::filesystem::path& legacy,
                                                 const std::filesystem::path& canonical)
        {
            if (legacy == canonical)
                return HubStatus::Success();

            std::error_code error;
            const bool legacyExists = std::filesystem::exists(legacy, error);
            if (error)
                return HubStatus::Failure(MigrationError(legacy, error));
            if (!legacyExists)
                return HubStatus::Success();

            const bool canonicalExists = std::filesystem::exists(canonical, error);
            if (error)
                return HubStatus::Failure(MigrationError(canonical, error));
            if (canonicalExists)
            {
                const bool empty =
                    std::filesystem::is_directory(canonical, error) && std::filesystem::is_empty(canonical, error);
                if (error)
                    return HubStatus::Failure(MigrationError(canonical, error));
                if (!empty)
                {
                    return HubStatus::Failure(
                        {.Code = HubErrorCode::MigrationFailed,
                         .Message = "Kéire Hub found both corrected and legacy storage directories.",
                         .Retryable = false,
                         .AffectedItem = canonical.filename().string(),
                         .TechnicalDetails = "The Hub refused to merge two non-empty storage roots automatically."});
                }
                std::filesystem::remove(canonical, error);
                if (error)
                    return HubStatus::Failure(MigrationError(canonical, error));
            }

            std::filesystem::create_directories(canonical.parent_path(), error);
            if (error)
                return HubStatus::Failure(MigrationError(canonical.parent_path(), error));
            std::filesystem::rename(legacy, canonical, error);
            if (error)
                return HubStatus::Failure(MigrationError(legacy, error));
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus MergeDisjointDirectory(const std::filesystem::path& legacy,
                                                       const std::filesystem::path& canonical)
        {
            std::error_code error;
            if (!std::filesystem::exists(legacy, error))
                return error ? HubStatus::Failure(MigrationError(legacy, error)) : HubStatus::Success();
            if (!std::filesystem::exists(canonical, error) ||
                (std::filesystem::is_directory(canonical, error) && std::filesystem::is_empty(canonical, error)))
            {
                if (error)
                    return HubStatus::Failure(MigrationError(canonical, error));
                return MigrateDirectory(legacy, canonical);
            }
            if (error || !std::filesystem::is_directory(legacy, error) ||
                !std::filesystem::is_directory(canonical, error))
            {
                return error ? HubStatus::Failure(MigrationError(legacy, error))
                             : HubStatus::Failure({.Code = HubErrorCode::MigrationFailed,
                                                   .Message = "Kéire Hub found an invalid legacy storage directory.",
                                                   .AffectedItem = legacy.filename().string()});
            }

            std::vector<std::filesystem::path> children;
            for (std::filesystem::directory_iterator iterator(legacy, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                const auto destination = canonical / iterator->path().filename();
                if (std::filesystem::exists(destination, error))
                {
                    return HubStatus::Failure(
                        {.Code = HubErrorCode::MigrationFailed,
                         .Message = "Kéire Hub found conflicting corrected and legacy preference data.",
                         .Retryable = false,
                         .AffectedItem = destination.filename().string(),
                         .TechnicalDetails = "The Hub refused to overwrite an existing preference area."});
                }
                if (error)
                    return HubStatus::Failure(MigrationError(destination, error));
                children.push_back(iterator->path());
            }
            if (error)
                return HubStatus::Failure(MigrationError(legacy, error));

            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
            for (const auto& child : children)
            {
                const auto destination = canonical / child.filename();
                std::filesystem::rename(child, destination, error);
                if (error)
                {
                    for (auto rollback = moved.rbegin(); rollback != moved.rend(); ++rollback)
                    {
                        std::error_code ignored;
                        std::filesystem::rename(rollback->second, rollback->first, ignored);
                    }
                    return HubStatus::Failure(MigrationError(child, error));
                }
                moved.emplace_back(child, destination);
            }
            std::filesystem::remove(legacy, error);
            return error ? HubStatus::Failure(MigrationError(legacy, error)) : HubStatus::Success();
        }

    } // namespace

    std::filesystem::path RepairLegacyHubPath(const std::filesystem::path& path)
    {
        std::filesystem::path repaired;
        for (const auto& component : path)
            repaired /= component == LegacyComponent ? CanonicalComponent : component;
        return repaired;
    }

    HubStatus MigrateLegacyHubPreferenceRoot(const std::filesystem::path& canonicalRoot)
    {
        std::filesystem::path legacyRoot;
        for (const auto& component : canonicalRoot)
            legacyRoot /= component == CanonicalComponent ? LegacyComponent : component;
        if (const auto status = MergeDisjointDirectory(legacyRoot, canonicalRoot); !status)
            return status;

        std::error_code ignored;
        auto parent = legacyRoot.parent_path();
        while (!parent.empty() && parent.filename() == LegacyComponent)
        {
            (void)std::filesystem::remove(parent, ignored);
            parent = parent.parent_path();
        }
        return HubStatus::Success();
    }

    HubStatus RepairLegacyHubStorageRoots(HubSettingsStore& settingsStore)
    {
        auto settings = *settingsStore.Snapshot();
        const std::array paths{settings.DefaultEditorRoot, settings.CacheRoot, settings.TemporaryRoot};
        for (const auto& path : paths)
        {
            if (const auto status = MigrateDirectory(path, RepairLegacyHubPath(path)); !status)
                return status;
        }
        for (const auto& path : paths)
        {
            std::error_code ignored;
            auto parent = path.parent_path();
            while (!parent.empty() && (parent.filename() == "Hub" || parent.filename() == LegacyComponent))
            {
                if (!std::filesystem::remove(parent, ignored))
                    break;
                parent = parent.parent_path();
            }
        }

        settings.DefaultEditorRoot = RepairLegacyHubPath(settings.DefaultEditorRoot);
        settings.CacheRoot = RepairLegacyHubPath(settings.CacheRoot);
        settings.TemporaryRoot = RepairLegacyHubPath(settings.TemporaryRoot);
        return settingsStore.Save(std::move(settings));
    }
} // namespace KeireHub
