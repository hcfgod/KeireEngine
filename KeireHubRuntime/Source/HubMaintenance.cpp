#include "KeireHubRuntime/HubMaintenance.h"

#include "Persistence.h"

#include <ranges>
#include <system_error>

namespace KeireHub
{
    HubStatus ValidateVerifiedPackageCacheClear(const std::span<const HubTask> tasks)
    {
        if (std::ranges::any_of(tasks, [](const HubTask& task) { return !IsTerminal(task.State); }))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Finish or cancel package tasks before clearing the cache.",
                                       .AffectedItem = "cache"});
        }
        return HubStatus::Success();
    }

    HubStatus ClearVerifiedPackageCache(const std::filesystem::path& cacheRoot)
    {
        if (cacheRoot.empty() || !cacheRoot.is_absolute())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The verified cache root must be an absolute path.",
                                       .AffectedItem = "cacheRoot"});
        }

        std::error_code error;
        const auto canonicalRoot = std::filesystem::weakly_canonical(cacheRoot, error);
        if (error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                       .Message = "The verified cache root could not be inspected.",
                                       .AffectedItem = Detail::PathToUtf8(cacheRoot),
                                       .TechnicalDetails = error.message()});
        }
        const auto target = canonicalRoot / "sha256";
        const auto status = std::filesystem::symlink_status(target, error);
        if (error == std::errc::no_such_file_or_directory)
            return HubStatus::Success();
        if (error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                       .Message = "The verified package cache could not be inspected.",
                                       .AffectedItem = Detail::PathToUtf8(target),
                                       .TechnicalDetails = error.message()});
        }
        if (status.type() == std::filesystem::file_type::symlink ||
            status.type() != std::filesystem::file_type::directory)
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The verified package cache is not a safe directory.",
                                       .AffectedItem = Detail::PathToUtf8(target)});
        }
        const auto canonicalTarget = std::filesystem::canonical(target, error);
        if (error || canonicalTarget.parent_path() != canonicalRoot)
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The verified package cache escapes its configured root.",
                                       .AffectedItem = Detail::PathToUtf8(target),
                                       .TechnicalDetails = error.message()});
        }
        (void)std::filesystem::remove_all(canonicalTarget, error);
        if (error)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                       .Message = "The verified package cache could not be cleared.",
                                       .Retryable = true,
                                       .AffectedItem = Detail::PathToUtf8(canonicalTarget),
                                       .TechnicalDetails = error.message()});
        }
        return HubStatus::Success();
    }
} // namespace KeireHub
