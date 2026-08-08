#include "KeireHubRuntime/BuildSupportOperationStore.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <set>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumStoreBytes = std::size_t{2U} * 1024U * 1024U;
        constexpr std::size_t MaximumPathBytes = std::size_t{32U} * 1024U;
        constexpr std::size_t MaximumMessageBytes = 4096U;
        constexpr std::size_t MaximumShortTextBytes = 512U;
        constexpr std::size_t MaximumRemovalJournalBytes = std::size_t{64U} * 1024U;
        constexpr std::size_t MaximumRemovalDirectoryEntries = 512U;
        constexpr std::size_t MaximumRemovalJournals = 128U;

        [[nodiscard]] std::string_view ToString(const BuildSupportOperationKind kind) noexcept
        {
            constexpr std::array names{"import", "repair", "remove"};
            return names[static_cast<std::size_t>(kind)];
        }

        [[nodiscard]] std::string_view ToString(const BuildSupportOperationState state) noexcept
        {
            constexpr std::array names{"launching", "running", "cancelling", "completed", "failed", "cancelled"};
            return names[static_cast<std::size_t>(state)];
        }

        template <typename Enum, std::size_t Size>
        [[nodiscard]] std::optional<Enum> ParseEnum(const std::string_view value, const std::array<Enum, Size>& values,
                                                    std::string_view (*name)(Enum) noexcept)
        {
            for (const auto candidate : values)
            {
                if (name(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsBoundedAbsolutePath(const std::filesystem::path& path)
        {
            if (path.empty() || !path.is_absolute() || path == path.root_path() || !path.has_filename())
                return false;
            try
            {
                return path.generic_u8string().size() <= MaximumPathBytes;
            }
            catch (...)
            {
                return false;
            }
        }

        [[nodiscard]] bool IsConfinedTo(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            if (!IsBoundedAbsolutePath(root) || !IsBoundedAbsolutePath(candidate))
                return false;
            const auto normalizedRoot = root.lexically_normal();
            const auto normalizedCandidate = candidate.lexically_normal();
            const auto relative = normalizedCandidate.lexically_relative(normalizedRoot);
            return !relative.empty() && !relative.is_absolute() && !relative.has_root_name() &&
                   !relative.has_root_directory() && *relative.begin() != "..";
        }

        [[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return left.lexically_normal() == right.lexically_normal();
        }

        [[nodiscard]] bool IsOperationId(const std::string_view value) noexcept
        {
            if (value.size() != 36)
                return false;
            constexpr std::array separators{8U, 13U, 18U, 23U};
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (std::ranges::find(separators, index) != separators.end())
                {
                    if (value[index] != '-')
                        return false;
                }
                else if (!((value[index] >= '0' && value[index] <= '9') ||
                           (value[index] >= 'a' && value[index] <= 'f')))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] HubStatus Validate(const BuildSupportOperationRecord& record,
                                         const std::filesystem::path& storeRoot)
        {
            if (record.Kind < BuildSupportOperationKind::Import || record.Kind > BuildSupportOperationKind::Remove ||
                record.State < BuildSupportOperationState::Launching ||
                record.State > BuildSupportOperationState::Cancelled || !IsOperationId(record.Id) ||
                !Detail::IsBoundedIdentifier(record.TargetInstallationId, 256) ||
                !SemanticVersion::Parse(record.EngineVersion) || record.ComponentId.size() > MaximumShortTextBytes ||
                record.CurrentPackage.size() > MaximumShortTextBytes || record.Phase.size() > MaximumShortTextBytes ||
                record.Message.empty() || record.Message.size() > MaximumMessageBytes ||
                !std::isfinite(record.Progress) || record.Progress < 0.0F || record.Progress > 1.0F ||
                record.CreatedUnixSeconds == 0 || record.UpdatedUnixSeconds < record.CreatedUnixSeconds ||
                (record.ChildProcessId && *record.ChildProcessId == 0))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The Build Support operation contains invalid metadata.",
                                           .AffectedItem = record.Id});
            }
            if (!IsBoundedAbsolutePath(record.EditorRoot) ||
                !IsConfinedTo(record.EditorRoot, record.AssetToolEntrypoint) ||
                !IsConfinedTo(storeRoot, record.OperationRoot) ||
                !SamePath(record.OperationRoot, storeRoot / record.Id))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The Build Support operation paths are not confined.",
                                           .AffectedItem = record.Id});
            }
            if (record.Kind == BuildSupportOperationKind::Remove)
            {
                if (!record.StatusPath.empty() || !record.CancelPath.empty() ||
                    !Detail::IsBoundedIdentifier(record.ComponentId, 256))
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                               .Message = "The Build Support removal metadata is invalid.",
                                               .AffectedItem = record.Id});
                }
            }
            else if (!SamePath(record.StatusPath, record.OperationRoot / "status.json") ||
                     !SamePath(record.CancelPath, record.OperationRoot / "cancel") ||
                     (record.Kind == BuildSupportOperationKind::Repair &&
                      !Detail::IsBoundedIdentifier(record.ComponentId, 256)) ||
                     (record.Kind == BuildSupportOperationKind::Import && !record.ComponentId.empty()))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The Build Support status or cancellation path is invalid.",
                                           .AffectedItem = record.Id});
            }
            const bool processIdentityInvalid =
                (record.State == BuildSupportOperationState::Launching && record.ChildProcessId) ||
                (record.State == BuildSupportOperationState::Running && !record.ChildProcessId) ||
                (IsTerminal(record.State) && record.ChildProcessId);
            if (processIdentityInvalid)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A launching Build Support operation cannot have a child PID.",
                                           .AffectedItem = record.Id});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateStoreLocation(const std::filesystem::path& root,
                                                      const std::filesystem::path& path)
        {
            if (!IsBoundedAbsolutePath(root) || !SamePath(path, root / "operations.json"))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The Build Support operation journal location is invalid.",
                                           .AffectedItem = "build-support"});
            }
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(root, error);
            if (error && error != std::errc::no_such_file_or_directory)
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                           .Message = "The Build Support operation directory could not be inspected.",
                                           .AffectedItem = "build-support",
                                           .TechnicalDetails = error.message()});
            }
            if (!error && rootStatus.type() != std::filesystem::file_type::not_found &&
                (!std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus)))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The Build Support operation directory is unsafe.",
                                           .AffectedItem = "build-support"});
            }
            error.clear();
            const auto pathStatus = std::filesystem::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                           .Message = "The Build Support operation journal could not be inspected.",
                                           .AffectedItem = "build-support",
                                           .TechnicalDetails = error.message()});
            }
            if (!error && pathStatus.type() != std::filesystem::file_type::not_found &&
                (!std::filesystem::is_regular_file(pathStatus) || std::filesystem::is_symlink(pathStatus)))
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The Build Support operation journal is unsafe.",
                                           .AffectedItem = "build-support"});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] Detail::Json Serialize(const std::vector<BuildSupportOperationRecord>& records)
        {
            Detail::Json values = Detail::Json::array();
            for (const auto& record : records)
            {
                Detail::Json value{{"id", record.Id},
                                   {"kind", ToString(record.Kind)},
                                   {"state", ToString(record.State)},
                                   {"targetInstallationId", record.TargetInstallationId},
                                   {"engineVersion", record.EngineVersion},
                                   {"editorRoot", Detail::PathToUtf8(record.EditorRoot)},
                                   {"assetToolEntrypoint", Detail::PathToUtf8(record.AssetToolEntrypoint)},
                                   {"operationRoot", Detail::PathToUtf8(record.OperationRoot)},
                                   {"statusPath", Detail::PathToUtf8(record.StatusPath)},
                                   {"cancelPath", Detail::PathToUtf8(record.CancelPath)},
                                   {"componentId", record.ComponentId},
                                   {"currentPackage", record.CurrentPackage},
                                   {"phase", record.Phase},
                                   {"message", record.Message},
                                   {"progress", record.Progress},
                                   {"created", record.CreatedUnixSeconds},
                                   {"updated", record.UpdatedUnixSeconds}};
                if (record.ChildProcessId)
                    value["childProcessId"] = *record.ChildProcessId;
                values.push_back(std::move(value));
            }
            return {{"schemaVersion", BuildSupportOperationStore::CurrentSchemaVersion},
                    {"operations", std::move(values)}};
        }

        [[nodiscard]] HubResult<std::vector<BuildSupportOperationRecord>> Parse(const Detail::Json& document,
                                                                                const std::filesystem::path& storeRoot)
        {
            try
            {
                if (!document.is_object() || document.at("schemaVersion").get<std::uint32_t>() !=
                                                 BuildSupportOperationStore::CurrentSchemaVersion)
                {
                    return HubResult<std::vector<BuildSupportOperationRecord>>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This Build Support operation journal uses an unsupported schema.",
                         .AffectedItem = "build-support"});
                }
                const auto& values = document.at("operations");
                if (!values.is_array() || values.size() > BuildSupportOperationStore::MaximumRecords)
                    throw std::invalid_argument("Invalid Build Support operation collection.");
                constexpr std::array kinds{BuildSupportOperationKind::Import, BuildSupportOperationKind::Repair,
                                           BuildSupportOperationKind::Remove};
                constexpr std::array states{
                    BuildSupportOperationState::Launching,  BuildSupportOperationState::Running,
                    BuildSupportOperationState::Cancelling, BuildSupportOperationState::Completed,
                    BuildSupportOperationState::Failed,     BuildSupportOperationState::Cancelled};
                std::vector<BuildSupportOperationRecord> result;
                result.reserve(values.size());
                std::set<std::string, std::less<>> identities;
                std::size_t active = 0;
                for (const auto& value : values)
                {
                    const auto kind =
                        ParseEnum(value.at("kind").get<std::string>(), kinds,
                                  static_cast<std::string_view (*)(BuildSupportOperationKind) noexcept>(&ToString));
                    const auto state =
                        ParseEnum(value.at("state").get<std::string>(), states,
                                  static_cast<std::string_view (*)(BuildSupportOperationState) noexcept>(&ToString));
                    if (!kind || !state)
                        throw std::invalid_argument("Unknown Build Support operation kind or state.");
                    BuildSupportOperationRecord record{
                        .Id = value.at("id").get<std::string>(),
                        .Kind = *kind,
                        .State = *state,
                        .TargetInstallationId = value.at("targetInstallationId").get<std::string>(),
                        .EngineVersion = value.at("engineVersion").get<std::string>(),
                        .EditorRoot = Detail::PathFromUtf8(value.at("editorRoot").get<std::string>()),
                        .AssetToolEntrypoint = Detail::PathFromUtf8(value.at("assetToolEntrypoint").get<std::string>()),
                        .OperationRoot = Detail::PathFromUtf8(value.at("operationRoot").get<std::string>()),
                        .StatusPath = Detail::PathFromUtf8(value.value("statusPath", std::string{})),
                        .CancelPath = Detail::PathFromUtf8(value.value("cancelPath", std::string{})),
                        .ComponentId = value.value("componentId", std::string{}),
                        .CurrentPackage = value.value("currentPackage", std::string{}),
                        .Phase = value.at("phase").get<std::string>(),
                        .Message = value.at("message").get<std::string>(),
                        .Progress = value.at("progress").get<float>(),
                        .CreatedUnixSeconds = value.at("created").get<std::uint64_t>(),
                        .UpdatedUnixSeconds = value.at("updated").get<std::uint64_t>()};
                    if (value.contains("childProcessId"))
                        record.ChildProcessId = value.at("childProcessId").get<std::uint64_t>();
                    if (const auto status = Validate(record, storeRoot); !status)
                        throw std::invalid_argument(status.Error().Message);
                    if (!identities.insert(record.Id).second)
                        throw std::invalid_argument("Duplicate Build Support operation identity.");
                    if (IsActive(record.State) && ++active > 1)
                        throw std::invalid_argument("Multiple active Build Support operations are not supported.");
                    result.push_back(std::move(record));
                }
                return HubResult<std::vector<BuildSupportOperationRecord>>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::vector<BuildSupportOperationRecord>>::Failure(
                    {.Code = HubErrorCode::InvalidData,
                     .Message = "The Build Support operation journal is malformed.",
                     .AffectedItem = "build-support",
                     .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] bool SameMutableState(const BuildSupportOperationRecord& record,
                                            const BuildSupportOperationState state, const std::string_view phase,
                                            const std::string_view message, const float progress) noexcept
        {
            return record.State == state && record.Phase == phase && record.Message == message &&
                   record.Progress == progress;
        }

        void Prune(std::vector<BuildSupportOperationRecord>& records)
        {
            std::ranges::stable_sort(records,
                                     [](const auto& left, const auto& right)
                                     {
                                         if (IsActive(left.State) != IsActive(right.State))
                                             return IsActive(left.State);
                                         if (left.UpdatedUnixSeconds != right.UpdatedUnixSeconds)
                                             return left.UpdatedUnixSeconds > right.UpdatedUnixSeconds;
                                         return left.Id < right.Id;
                                     });
            const auto active = static_cast<std::size_t>(
                std::ranges::count_if(records, [](const auto& record) { return IsActive(record.State); }));
            const auto keep = std::min(records.size(), active + BuildSupportOperationStore::MaximumTerminalHistory);
            records.resize(keep);
        }
    } // namespace

    bool IsActive(const BuildSupportOperationState state) noexcept
    {
        return state == BuildSupportOperationState::Launching || state == BuildSupportOperationState::Running ||
               state == BuildSupportOperationState::Cancelling;
    }

    bool IsTerminal(const BuildSupportOperationState state) noexcept { return !IsActive(state); }

    std::string BuildSupportTaskId(const std::string_view operationId)
    {
        return "build-support-" + std::string(operationId);
    }

    HubResult<bool> HasPendingBuildSupportRemovalJournal(const std::filesystem::path& storageRoot,
                                                         const std::string_view engineVersion,
                                                         const std::string_view componentId)
    {
        if (!IsBoundedAbsolutePath(storageRoot) || !SemanticVersion::Parse(engineVersion) ||
            !Detail::IsBoundedIdentifier(componentId, 256))
        {
            return HubResult<bool>::Failure({.Code = HubErrorCode::InvalidArgument,
                                             .Message = "The Build Support removal recovery target is invalid.",
                                             .AffectedItem = std::string(componentId)});
        }
        try
        {
            std::error_code error;
            const auto storageStatus = std::filesystem::symlink_status(storageRoot, error);
            if (error == std::errc::no_such_file_or_directory ||
                (!error && storageStatus.type() == std::filesystem::file_type::not_found))
            {
                return HubResult<bool>::Success(false);
            }
            if (error || !std::filesystem::is_directory(storageStatus) || std::filesystem::is_symlink(storageStatus))
                throw std::runtime_error("The Build Support storage root is unsafe.");
            const auto versionRoot = (storageRoot / Detail::PathFromUtf8(engineVersion)).lexically_normal();
            if (!IsConfinedTo(storageRoot, versionRoot))
                throw std::runtime_error("The Build Support version root is not confined.");
            error.clear();
            const auto versionStatus = std::filesystem::symlink_status(versionRoot, error);
            if (error == std::errc::no_such_file_or_directory ||
                (!error && versionStatus.type() == std::filesystem::file_type::not_found))
            {
                return HubResult<bool>::Success(false);
            }
            if (error || !std::filesystem::is_directory(versionStatus) || std::filesystem::is_symlink(versionStatus))
                throw std::runtime_error("The Build Support version root is unsafe.");

            std::size_t entries = 0;
            std::size_t journals = 0;
            bool pending = false;
            for (const auto& entry : std::filesystem::directory_iterator(versionRoot))
            {
                if (++entries > MaximumRemovalDirectoryEntries)
                    throw std::runtime_error("The Build Support version directory exceeds its recovery scan limit.");
                const auto filename = Detail::PathToUtf8(entry.path().filename());
                if (!filename.starts_with(".remove-") || !filename.ends_with(".json"))
                    continue;
                if (++journals > MaximumRemovalJournals)
                    throw std::runtime_error("The Build Support removal journal count exceeds its limit.");
                const auto status = entry.symlink_status();
                if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                    throw std::runtime_error("A Build Support removal journal is unsafe.");
                auto document = Detail::ReadJsonFile(entry.path(), MaximumRemovalJournalBytes);
                if (!document)
                    throw std::runtime_error(document.Error().TechnicalDetails);
                if (!document.Value().is_object() || document.Value().value("schemaVersion", 0U) != 1U)
                    throw std::runtime_error("A Build Support removal journal has an invalid schema.");
                const auto journalEngineVersion = document.Value().at("engineVersion").get<std::string>();
                const auto journalComponentId = document.Value().at("packId").get<std::string>();
                const auto tombstone = document.Value().at("tombstone").get<std::string>();
                if (journalEngineVersion != engineVersion || !Detail::IsBoundedIdentifier(journalComponentId, 256) ||
                    !tombstone.starts_with(".remove-") || !IsOperationId(tombstone.substr(8)) ||
                    filename != tombstone + ".json")
                {
                    throw std::runtime_error("A Build Support removal journal has invalid identity metadata.");
                }
                pending = pending || journalComponentId == componentId;
            }
            return HubResult<bool>::Success(pending);
        }
        catch (const std::exception& error)
        {
            return HubResult<bool>::Failure({.Code = HubErrorCode::IoRead,
                                             .Message = "Build Support removal recovery could not be inspected.",
                                             .Retryable = true,
                                             .AffectedItem = std::string(componentId),
                                             .TechnicalDetails = error.what()});
        }
    }

    BuildSupportRemovalInventoryAction
    EvaluateBuildSupportRemovalInventory(const BuildSupportRemovalInventoryGate& gate, const std::uint64_t revision,
                                         const bool loading) noexcept
    {
        if (revision <= gate.BaselineRevision || loading)
            return BuildSupportRemovalInventoryAction::Wait;
        return gate.RefreshAfterCurrentLoad ? BuildSupportRemovalInventoryAction::StartFreshRefresh
                                            : BuildSupportRemovalInventoryAction::Reconcile;
    }

    BuildSupportOperationStore::BuildSupportOperationStore(std::filesystem::path operationRoot)
        : m_Root(std::move(operationRoot).lexically_normal()), m_Path(m_Root / "operations.json"),
          m_Snapshot(std::make_shared<const std::vector<BuildSupportOperationRecord>>())
    {
    }

    HubStatus BuildSupportOperationStore::Load()
    {
        if (const auto location = ValidateStoreLocation(m_Root, m_Path); !location)
            return location;
        std::error_code error;
        if (!std::filesystem::exists(m_Path, error))
        {
            if (error)
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                           .Message = "The Build Support operation journal could not be inspected.",
                                           .AffectedItem = "build-support",
                                           .TechnicalDetails = error.message()});
            }
            m_Snapshot = std::make_shared<const std::vector<BuildSupportOperationRecord>>();
            return HubStatus::Success();
        }
        auto document = Detail::ReadJsonFile(m_Path, MaximumStoreBytes);
        if (!document)
            return HubStatus::Failure(document.Error());
        auto parsed = Parse(document.Value(), m_Root);
        if (!parsed)
            return HubStatus::Failure(parsed.Error());
        auto records = std::move(parsed).Value();
        Prune(records);
        m_Snapshot = std::make_shared<const std::vector<BuildSupportOperationRecord>>(std::move(records));
        return HubStatus::Success();
    }

    HubStatus BuildSupportOperationStore::Add(BuildSupportOperationRecord record)
    {
        if (record.State != BuildSupportOperationState::Launching || record.ChildProcessId)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "A Build Support operation must begin in the launching state.",
                                       .AffectedItem = record.Id});
        }
        if (const auto status = Validate(record, m_Root); !status)
            return status;
        auto records = *Snapshot();
        if (std::ranges::find(records, record.Id, &BuildSupportOperationRecord::Id) != records.end() ||
            std::ranges::any_of(records, [](const auto& value) { return IsActive(value.State); }))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                       .Message = "Another Build Support operation is already active.",
                                       .Retryable = true,
                                       .AffectedItem = record.TargetInstallationId});
        }
        records.push_back(std::move(record));
        return Commit(std::move(records));
    }

    HubStatus BuildSupportOperationStore::AttachProcess(const std::string& operationId, const std::uint64_t processId,
                                                        const std::uint64_t updatedUnixSeconds)
    {
        auto records = *Snapshot();
        const auto found = std::ranges::find(records, operationId, &BuildSupportOperationRecord::Id);
        if (found == records.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The Build Support operation is no longer available.",
                                       .AffectedItem = operationId});
        }
        if (found->State != BuildSupportOperationState::Launching || found->ChildProcessId || processId == 0 ||
            updatedUnixSeconds < found->UpdatedUnixSeconds)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The Build Support child process cannot be attached.",
                                       .AffectedItem = operationId});
        }
        found->State = BuildSupportOperationState::Running;
        found->ChildProcessId = processId;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        return Commit(std::move(records));
    }

    HubStatus BuildSupportOperationStore::Update(const std::string& operationId, const BuildSupportOperationState state,
                                                 std::string phase, std::string message, const float progress,
                                                 const std::uint64_t updatedUnixSeconds)
    {
        if (!IsActive(state))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "A terminal Build Support operation requires Finish.",
                                       .AffectedItem = operationId});
        }
        auto records = *Snapshot();
        const auto found = std::ranges::find(records, operationId, &BuildSupportOperationRecord::Id);
        if (found == records.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The Build Support operation is no longer available.",
                                       .AffectedItem = operationId});
        }
        const bool validTransition =
            found->State == state ||
            (found->State == BuildSupportOperationState::Launching &&
             state == BuildSupportOperationState::Cancelling) ||
            (found->State == BuildSupportOperationState::Running && state == BuildSupportOperationState::Cancelling);
        if (!IsActive(found->State) || !validTransition || updatedUnixSeconds < found->UpdatedUnixSeconds)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The Build Support operation update is invalid.",
                                       .AffectedItem = operationId});
        }
        if (SameMutableState(*found, state, phase, message, progress))
            return HubStatus::Success();
        found->State = state;
        found->Phase = std::move(phase);
        found->Message = std::move(message);
        found->Progress = progress;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        return Commit(std::move(records));
    }

    HubStatus BuildSupportOperationStore::Finish(const std::string& operationId, const BuildSupportOperationState state,
                                                 std::string phase, std::string message, const float progress,
                                                 const std::uint64_t updatedUnixSeconds)
    {
        if (!IsTerminal(state))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The Build Support operation result is not terminal.",
                                       .AffectedItem = operationId});
        }
        auto records = *Snapshot();
        const auto found = std::ranges::find(records, operationId, &BuildSupportOperationRecord::Id);
        if (found == records.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The Build Support operation is no longer available.",
                                       .AffectedItem = operationId});
        }
        if (!IsActive(found->State) || updatedUnixSeconds < found->UpdatedUnixSeconds)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The Build Support operation is already terminal.",
                                       .AffectedItem = operationId});
        }
        found->State = state;
        found->ChildProcessId.reset();
        found->Phase = std::move(phase);
        found->Message = std::move(message);
        found->Progress = progress;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        return Commit(std::move(records));
    }

    std::shared_ptr<const std::vector<BuildSupportOperationRecord>>
    BuildSupportOperationStore::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    const std::filesystem::path& BuildSupportOperationStore::Root() const noexcept { return m_Root; }
    const std::filesystem::path& BuildSupportOperationStore::Path() const noexcept { return m_Path; }

    HubStatus BuildSupportOperationStore::Commit(std::vector<BuildSupportOperationRecord> records)
    {
        if (const auto location = ValidateStoreLocation(m_Root, m_Path); !location)
            return location;
        if (records.size() > MaximumRecords)
            Prune(records);
        std::size_t active = 0;
        for (const auto& record : records)
        {
            if (const auto status = Validate(record, m_Root); !status)
                return status;
            if (IsActive(record.State) && ++active > 1)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "Multiple Build Support operations cannot run concurrently.",
                                           .AffectedItem = "build-support"});
            }
        }
        Prune(records);
        if (const auto write = Detail::WriteJsonFileAtomically(m_Path, Serialize(records)); !write)
            return write;
        m_Snapshot = std::make_shared<const std::vector<BuildSupportOperationRecord>>(std::move(records));
        return HubStatus::Success();
    }
} // namespace KeireHub
