#include "Keire/Project/ProjectUpgrade.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDescriptorBytes = std::size_t{1024} * 1024U;
        constexpr std::size_t MaximumJournalBytes = std::size_t{4} * 1024U * 1024U;

        [[nodiscard]] std::filesystem::path MarkerPath(const std::filesystem::path& root)
        {
            return root / "ProjectSettings" / "Project.keireproject";
        }

        [[nodiscard]] std::filesystem::path ResolveProjectRoot(const std::filesystem::path& value)
        {
            auto root = std::filesystem::absolute(value).lexically_normal();
            if (std::filesystem::is_regular_file(root))
                root = root.parent_path().parent_path();
            root = std::filesystem::weakly_canonical(root);
            if (!std::filesystem::is_directory(root) || !std::filesystem::is_regular_file(MarkerPath(root)))
                throw std::invalid_argument("Directory is not a Kéire project.");
            return root;
        }

        [[nodiscard]] std::filesystem::path ConfinedPath(const std::filesystem::path& root,
                                                         const std::filesystem::path& relative)
        {
            const auto normalized = relative.lexically_normal();
            if (normalized.empty() || normalized.is_absolute() || normalized == ".." ||
                (!normalized.empty() && *normalized.begin() == ".."))
                throw std::invalid_argument("Project upgrade path escapes the project root.");
            const auto canonicalRoot = std::filesystem::weakly_canonical(root);
            auto candidate = canonicalRoot;
            for (const auto& component : normalized)
            {
                candidate /= component;
                std::error_code statusError;
                const auto status = std::filesystem::symlink_status(candidate, statusError);
                if (!statusError && std::filesystem::is_symlink(status))
                    throw std::invalid_argument("Project upgrade paths may not traverse symbolic links.");
#if defined(_WIN32)
                const auto attributes = GetFileAttributesW(candidate.wstring().c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    throw std::invalid_argument("Project upgrade paths may not traverse reparse points.");
#endif
            }
            candidate = std::filesystem::weakly_canonical(candidate);
            const auto mismatch = std::ranges::mismatch(canonicalRoot, candidate);
            if (mismatch.in1 != canonicalRoot.end() || mismatch.in2 == candidate.end())
                throw std::invalid_argument("Project upgrade path escapes the project root.");
            return candidate;
        }

        [[nodiscard]] std::uint32_t ReadSchema(const std::filesystem::path& descriptor)
        {
            const auto document = Json::parse(Detail::ReadTextFile(descriptor, MaximumDescriptorBytes));
            if (!document.is_object())
                throw std::runtime_error("Project descriptor root must be an object.");
            return document.at("schemaVersion").get<std::uint32_t>();
        }

        [[nodiscard]] std::string FileTimestampUtc(const std::filesystem::path& path)
        {
            const auto fileNow = std::filesystem::file_time_type::clock::now();
            const auto systemNow = std::chrono::system_clock::now();
            const auto fileTime = std::filesystem::last_write_time(path);
            const auto converted =
                systemNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(fileTime - fileNow);
            const auto time = std::chrono::system_clock::to_time_t(converted);
            std::tm utc{};
#if defined(_WIN32)
            if (gmtime_s(&utc, &time) != 0)
                throw std::runtime_error("Cannot format the project descriptor timestamp.");
#else
            if (!gmtime_r(&time, &utc))
                throw std::runtime_error("Cannot format the project descriptor timestamp.");
#endif
            std::ostringstream stream;
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        void ValidateStagedProject(const std::filesystem::path& projectRoot, const std::filesystem::path& stagingRoot,
                                   const std::uint32_t expectedSchema)
        {
            if (!std::filesystem::is_directory(projectRoot / "Assets") ||
                !std::filesystem::is_directory(projectRoot / "ProjectSettings"))
                throw std::runtime_error("Staged project validation found missing project directories.");
            const auto descriptor = MarkerPath(stagingRoot);
            const auto document = Json::parse(Detail::ReadTextFile(descriptor, MaximumDescriptorBytes));
            if (!document.is_object() || document.at("schemaVersion").get<std::uint32_t>() != expectedSchema ||
                !document.at("id").is_string() || !document.at("name").is_string())
                throw std::runtime_error("Staged project descriptor failed validation.");
            if (expectedSchema >= 2 && !document.at("requiredModules").is_array())
                throw std::runtime_error("Staged project descriptor has no required source-module catalog.");
            if (expectedSchema >= 3 &&
                (!document.at("createdAt").is_string() || !document.at("lastSavedWithEngineVersion").is_string()))
                throw std::runtime_error("Staged project descriptor has no creation or last-saved metadata.");
        }

        [[nodiscard]] std::string TransactionId()
        {
            static std::atomic<std::uint32_t> sequence{0};
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            return std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        }

        class ProjectLock final
        {
          public:
            explicit ProjectLock(const std::filesystem::path& root)
            {
                const auto path = ConfinedPath(root, std::filesystem::path("Library") / "Editor.lock");
                std::filesystem::create_directories(path.parent_path());
#if defined(_WIN32)
                m_Handle = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (m_Handle == INVALID_HANDLE_VALUE)
                {
                    m_Handle = nullptr;
                    throw std::runtime_error("Project is open in another process.");
                }
#else
                m_Handle = open(path.c_str(), O_CREAT | O_RDWR, 0644);
                if (m_Handle < 0 || flock(m_Handle, LOCK_EX | LOCK_NB) != 0)
                {
                    if (m_Handle >= 0)
                        close(m_Handle);
                    m_Handle = -1;
                    throw std::runtime_error("Project is open in another process.");
                }
#endif
            }

            ~ProjectLock()
            {
#if defined(_WIN32)
                if (m_Handle)
                    CloseHandle(m_Handle);
#else
                if (m_Handle >= 0)
                {
                    (void)flock(m_Handle, LOCK_UN);
                    close(m_Handle);
                }
#endif
            }

          private:
#if defined(_WIN32)
            HANDLE m_Handle = nullptr;
#else
            int m_Handle = -1;
#endif
        };

        [[nodiscard]] Json ReadJournal(const std::filesystem::path& active)
        {
            auto journal = Json::parse(Detail::ReadTextFile(ConfinedPath(active, "journal.json"), MaximumJournalBytes));
            try
            {
                if (!journal.is_object() || journal.at("schemaVersion").get<std::uint32_t>() != 1 ||
                    !journal.at("transactionId").is_string() || !journal.at("phase").is_string() ||
                    !journal.at("fromSchema").is_number_unsigned() ||
                    !journal.at("targetSchema").is_number_unsigned() || !journal.at("published").is_number_unsigned() ||
                    !journal.at("steps").is_array() || !journal.at("files").is_array())
                    throw std::runtime_error("Project upgrade journal has an invalid schema.");

                const auto transactionId = journal.at("transactionId").get<std::string>();
                if (transactionId.empty() || transactionId.size() > 128 ||
                    !std::ranges::all_of(
                        transactionId, [](const unsigned char character)
                        { return std::isalnum(character) != 0 || character == '-' || character == '_'; }))
                    throw std::runtime_error("Project upgrade journal has an invalid transaction ID.");

                const auto phase = journal.at("phase").get<std::string>();
                constexpr std::array ValidPhases{"initializing", "snapshotted", "prepared",
                                                 "ready",        "publishing",  "completed"};
                if (std::ranges::find(ValidPhases, phase) == ValidPhases.end())
                    throw std::runtime_error("Project upgrade journal has an invalid transaction phase.");

                const auto& steps = journal.at("steps");
                const auto& files = journal.at("files");
                if (steps.size() > 4096 || files.size() > 4096 ||
                    journal.at("published").get<std::size_t>() > files.size())
                    throw std::runtime_error("Project upgrade journal exceeds its structural limits.");
                std::vector<std::string> uniqueSteps;
                for (const auto& step : steps)
                {
                    if (!step.is_string())
                        throw std::runtime_error("Project upgrade journal contains an invalid step.");
                    auto id = step.get<std::string>();
                    if (id.empty() || std::ranges::find(uniqueSteps, id) != uniqueSteps.end())
                        throw std::runtime_error("Project upgrade journal contains a duplicate or empty step.");
                    uniqueSteps.push_back(std::move(id));
                }
                std::vector<std::string> uniquePaths;
                for (const auto& file : files)
                {
                    if (!file.is_object() || !file.at("path").is_string() || !file.at("existed").is_boolean())
                        throw std::runtime_error("Project upgrade journal contains an invalid file record.");
                    auto path = file.at("path").get<std::string>();
                    if (path.empty() || path.size() > 1024 || std::ranges::find(uniquePaths, path) != uniquePaths.end())
                        throw std::runtime_error("Project upgrade journal contains a duplicate or invalid path.");
                    uniquePaths.push_back(std::move(path));
                }
            }
            catch (const nlohmann::json::exception& exception)
            {
                throw std::runtime_error("Project upgrade journal has an invalid schema: " +
                                         std::string(exception.what()));
            }
            return journal;
        }

        void WriteJournal(const std::filesystem::path& active, const Json& journal)
        {
            Detail::WriteTextFileAtomically(ConfinedPath(active, "journal.json"), journal.dump(2) + '\n');
        }

        [[nodiscard]] std::vector<std::byte> ReadFile(const std::filesystem::path& path)
        {
            const auto size = std::filesystem::file_size(path);
            if (size > (std::numeric_limits<std::size_t>::max)() ||
                size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
                throw std::runtime_error("Project upgrade file exceeds the addressable size.");
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()),
                                                           static_cast<std::streamsize>(bytes.size()))))
                throw std::runtime_error("Cannot read project upgrade file: " + path.string());
            return bytes;
        }

        void RetainThreeBackups(const std::filesystem::path& backups)
        {
            if (!std::filesystem::is_directory(backups))
                return;
            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(backups))
                if (entry.is_directory())
                    entries.push_back(entry.path());
            std::ranges::sort(entries);
            while (entries.size() > 3)
            {
                std::filesystem::remove_all(entries.front());
                entries.erase(entries.begin());
            }
        }
    } // namespace

    class ProjectUpgradeService::Impl final
    {
      public:
        Impl(const std::filesystem::path& root, std::vector<ProjectUpgradeStep> additional)
            : Root(ResolveProjectRoot(root)),
              Active(ConfinedPath(Root, std::filesystem::path("Library") / "ProjectUpgrades" / "Active"))
        {
            Steps.push_back(ProjectUpgradeService::CreateVersion1To2Step());
            Steps.push_back(ProjectUpgradeService::CreateVersion2To3Step());
            for (auto& step : additional)
                Steps.push_back(std::move(step));
            std::ranges::sort(Steps,
                              [](const auto& left, const auto& right)
                              {
                                  return std::tie(left.FromSchema, left.ToSchema, left.Id) <
                                         std::tie(right.FromSchema, right.ToSchema, right.Id);
                              });
            for (const auto& step : Steps)
            {
                if (step.Id.empty() || step.FromSchema == 0 || step.ToSchema <= step.FromSchema || !step.Apply)
                    throw std::invalid_argument(
                        "Project upgrade steps require an ID, forward transition, and apply callback.");
                for (const auto& path : step.AffectedPaths)
                    (void)ConfinedPath(Root, path);
            }
            for (auto first = Steps.begin(); first != Steps.end(); ++first)
                for (auto second = std::next(first); second != Steps.end(); ++second)
                    if (first->Id == second->Id || first->FromSchema == second->FromSchema)
                        throw std::invalid_argument("Project upgrade step IDs and source schemas must be unique.");
        }

        [[nodiscard]] const ProjectUpgradeStep& FindStep(const std::string_view id) const
        {
            const auto found = std::ranges::find(Steps, id, &ProjectUpgradeStep::Id);
            if (found == Steps.end())
                throw std::runtime_error("Interrupted project upgrade requires an unavailable step: " +
                                         std::string(id));
            return *found;
        }

        [[nodiscard]] std::filesystem::path TransactionPath(const std::filesystem::path& relative = {}) const
        {
            auto path = std::filesystem::path("Library") / "ProjectUpgrades" / "Active";
            if (!relative.empty())
                path /= relative;
            return ConfinedPath(Root, path);
        }

        [[nodiscard]] std::filesystem::path UpgradePath(const std::filesystem::path& relative = {}) const
        {
            auto path = std::filesystem::path("Library") / "ProjectUpgrades";
            if (!relative.empty())
                path /= relative;
            return ConfinedPath(Root, path);
        }

        [[nodiscard]] ProjectUpgradePlan BuildPlan() const
        {
            ProjectUpgradePlan plan;
            plan.ProjectRoot = Root;
            plan.CurrentSchema = ReadSchema(MarkerPath(Root));
            plan.TargetSchema = CurrentProjectSchemaVersion;
            const auto descriptor = Json::parse(Detail::ReadTextFile(MarkerPath(Root), MaximumDescriptorBytes));
            for (const auto& module : descriptor.value("requiredModules", Json::array()))
                plan.RequiredModules.push_back(
                    {module.at("id").get<std::string>(), module.at("version").get<std::string>()});
            if (plan.CurrentSchema > plan.TargetSchema)
                throw std::runtime_error("Project requires a newer Kéire project schema.");
            auto schema = plan.CurrentSchema;
            while (schema < plan.TargetSchema)
            {
                const auto found = std::ranges::find(Steps, schema, &ProjectUpgradeStep::FromSchema);
                if (found == Steps.end())
                    throw std::runtime_error("No registered project upgrade step can advance schema " +
                                             std::to_string(schema) + '.');
                ProjectUpgradePlanStep item{found->Id, found->FromSchema, found->ToSchema, found->AffectedPaths,
                                            found->Warning};
                plan.Steps.push_back(std::move(item));
                for (const auto& path : found->AffectedPaths)
                    if (std::ranges::find(plan.AffectedPaths, path) == plan.AffectedPaths.end())
                        plan.AffectedPaths.push_back(path);
                if (!found->Warning.empty())
                    plan.Warnings.push_back(found->Warning);
                schema = found->ToSchema;
            }
            std::ranges::sort(plan.AffectedPaths);
            for (const auto& path : plan.AffectedPaths)
            {
                const auto source = ConfinedPath(Root, path);
                if (std::filesystem::is_regular_file(source))
                    plan.EstimatedBackupBytes += std::filesystem::file_size(source);
            }
            return plan;
        }

        [[nodiscard]] bool JournalExists() const noexcept
        {
            std::error_code error;
            const bool exists = std::filesystem::is_regular_file(Active / "journal.json", error);
            return !error && exists;
        }

        void ValidateJournalPlan(const Json& journal) const
        {
            auto schema = journal.at("fromSchema").get<std::uint32_t>();
            std::vector<std::filesystem::path> expectedPaths;
            for (const auto& id : journal.at("steps"))
            {
                const auto& step = FindStep(id.get<std::string>());
                if (step.FromSchema != schema)
                    throw std::runtime_error("Project upgrade journal contains a discontinuous step sequence.");
                schema = step.ToSchema;
                for (const auto& path : step.AffectedPaths)
                    if (std::ranges::find(expectedPaths, path) == expectedPaths.end())
                        expectedPaths.push_back(path);
            }
            if (schema != journal.at("targetSchema").get<std::uint32_t>())
                throw std::runtime_error("Project upgrade journal does not reach its declared target schema.");
            std::ranges::sort(expectedPaths);

            std::vector<std::filesystem::path> persistedPaths;
            persistedPaths.reserve(journal.at("files").size());
            for (const auto& file : journal.at("files"))
            {
                auto path = std::filesystem::path(file.at("path").get<std::string>());
                (void)ConfinedPath(Root, path);
                persistedPaths.push_back(std::move(path));
            }
            std::ranges::sort(persistedPaths);
            if (persistedPaths != expectedPaths)
                throw std::runtime_error("Project upgrade journal file records do not match its registered steps.");
        }

        void SnapshotBefore(const Json& journal) const
        {
            const auto beforeRoot = TransactionPath("before");
            std::filesystem::remove_all(beforeRoot);
            std::filesystem::create_directories(beforeRoot);
            for (const auto& file : journal.at("files"))
            {
                if (!file.at("existed").get<bool>())
                    continue;
                const auto relative = std::filesystem::path(file.at("path").get<std::string>());
                const auto source = ConfinedPath(Root, relative);
                if (!std::filesystem::is_regular_file(source))
                    throw std::runtime_error("Project upgrade source disappeared before it was snapshotted: " +
                                             relative.string());
                Detail::WriteFileAtomically(TransactionPath(std::filesystem::path("before") / relative),
                                            ReadFile(source));
            }
        }

        void ResetStaging(const Json& journal) const
        {
            const auto stagingRoot = TransactionPath("staging");
            std::filesystem::remove_all(stagingRoot);
            std::filesystem::create_directories(stagingRoot);
            for (const auto& file : journal.at("files"))
            {
                if (!file.at("existed").get<bool>())
                    continue;
                const auto relative = std::filesystem::path(file.at("path").get<std::string>());
                const auto before = TransactionPath(std::filesystem::path("before") / relative);
                if (!std::filesystem::is_regular_file(before))
                    throw std::runtime_error("Project upgrade snapshot is incomplete: " + relative.string());
                Detail::WriteFileAtomically(TransactionPath(std::filesystem::path("staging") / relative),
                                            ReadFile(before));
            }
        }

        void RunSteps(Json& journal) const
        {
            ValidateJournalPlan(journal);
            ResetStaging(journal);
            journal["phase"] = "prepared";
            WriteJournal(TransactionPath(), journal);
            ProjectUpgradeStepContext context{Root, TransactionPath("staging")};
            for (const auto& id : journal.at("steps"))
            {
                const auto& step = FindStep(id.get<std::string>());
                step.Apply(context);
                if (step.Validate)
                    step.Validate(context);
            }
            ValidateStagedProject(Root, context.StagingRoot, journal.at("targetSchema").get<std::uint32_t>());
            journal["phase"] = "ready";
            WriteJournal(TransactionPath(), journal);
        }

        void Prepare(const ProjectUpgradePlan& plan)
        {
            std::error_code activeError;
            if (std::filesystem::exists(Active, activeError) || activeError)
                throw std::logic_error("An interrupted project upgrade must be recovered or rolled back first.");
            Json files = Json::array();
            for (const auto& relative : plan.AffectedPaths)
            {
                const auto source = ConfinedPath(Root, relative);
                const bool existed = std::filesystem::is_regular_file(source);
                files.push_back({{"path", relative.generic_string()}, {"existed", existed}});
            }
            Json steps = Json::array();
            for (const auto& step : plan.Steps)
                steps.push_back(step.Id);
            Json journal{{"schemaVersion", 1},
                         {"transactionId", TransactionId()},
                         {"phase", "initializing"},
                         {"fromSchema", plan.CurrentSchema},
                         {"targetSchema", plan.TargetSchema},
                         {"published", 0},
                         {"steps", std::move(steps)},
                         {"files", std::move(files)}};
            std::filesystem::create_directories(TransactionPath());
            WriteJournal(TransactionPath(), journal);
            SnapshotBefore(journal);
            journal["phase"] = "snapshotted";
            WriteJournal(TransactionPath(), journal);
            RunSteps(journal);
        }

        void Publish(Json journal)
        {
            ValidateJournalPlan(journal);
            const auto phase = journal.at("phase").get<std::string>();
            if (phase == "completed")
            {
                std::filesystem::remove_all(TransactionPath());
                RetainThreeBackups(UpgradePath("Backups"));
                return;
            }
            if (phase != "ready" && phase != "publishing")
                throw std::runtime_error("Project upgrade journal is not ready for publication.");
            const auto& files = journal.at("files");
            std::size_t published = journal.value("published", std::size_t{0});
            if (published > files.size())
                throw std::runtime_error("Project upgrade journal has an invalid publication cursor.");
            journal["phase"] = "publishing";
            WriteJournal(TransactionPath(), journal);
            for (; published < files.size(); ++published)
            {
                const auto relative = std::filesystem::path(files[published].at("path").get<std::string>());
                const auto staged = TransactionPath(std::filesystem::path("staging") / relative);
                if (!std::filesystem::is_regular_file(staged))
                    throw std::runtime_error("Project upgrade staged output is missing: " + relative.string());
                Detail::WriteFileAtomically(ConfinedPath(Root, relative), ReadFile(staged));
                journal["published"] = published + 1;
                WriteJournal(TransactionPath(), journal);
            }

            const auto transactionId = journal.at("transactionId").get<std::string>();
            for (const auto& file : files)
            {
                if (!file.at("existed").get<bool>())
                    continue;
                const auto relative = std::filesystem::path(file.at("path").get<std::string>());
                Detail::WriteFileAtomically(UpgradePath(std::filesystem::path("Backups") / transactionId / relative),
                                            ReadFile(TransactionPath(std::filesystem::path("before") / relative)));
            }
            journal["phase"] = "completed";
            Detail::WriteTextFileAtomically(
                UpgradePath(std::filesystem::path("Backups") / transactionId / "receipt.json"), journal.dump(2) + '\n');
            WriteJournal(TransactionPath(), journal);
            std::filesystem::remove_all(TransactionPath());
            RetainThreeBackups(UpgradePath("Backups"));
        }

        void RestoreBefore()
        {
            auto journal = ReadJournal(TransactionPath());
            ValidateJournalPlan(journal);
            const auto& files = journal.at("files");
            for (auto iterator = files.rbegin(); iterator != files.rend(); ++iterator)
            {
                const auto relative = std::filesystem::path(iterator->at("path").get<std::string>());
                const auto destination = ConfinedPath(Root, relative);
                if (iterator->at("existed").get<bool>())
                    Detail::WriteFileAtomically(destination,
                                                ReadFile(TransactionPath(std::filesystem::path("before") / relative)));
                else
                    std::filesystem::remove(destination);
            }
            std::filesystem::remove_all(TransactionPath());
        }

        void Abort()
        {
            if (!JournalExists())
            {
                std::filesystem::remove_all(TransactionPath());
                return;
            }
            const auto journal = ReadJournal(TransactionPath());
            const auto phase = journal.at("phase").get<std::string>();
            if (phase == "publishing" || phase == "completed")
                RestoreBefore();
            else
                std::filesystem::remove_all(TransactionPath());
        }

        std::filesystem::path Root;
        std::filesystem::path Active;
        std::vector<ProjectUpgradeStep> Steps;
    };

    ProjectUpgradeService::ProjectUpgradeService(std::filesystem::path projectRoot,
                                                 std::vector<ProjectUpgradeStep> additionalSteps)
        : m_Impl(std::make_unique<Impl>(projectRoot, std::move(additionalSteps)))
    {
    }

    ProjectUpgradeService::~ProjectUpgradeService() = default;

    ProjectUpgradePlan ProjectUpgradeService::Plan() const { return m_Impl->BuildPlan(); }

    void ProjectUpgradeService::Apply(const ProjectUpgradePlan& plan)
    {
        ProjectLock lock(m_Impl->Root);
        const auto current = m_Impl->BuildPlan();
        if (plan.ProjectRoot != current.ProjectRoot || plan.CurrentSchema != current.CurrentSchema ||
            plan.TargetSchema != current.TargetSchema || plan.Steps.size() != current.Steps.size())
            throw std::invalid_argument("Project upgrade plan is stale or belongs to another project.");
        for (std::size_t index = 0; index < plan.Steps.size(); ++index)
            if (plan.Steps[index].Id != current.Steps[index].Id)
                throw std::invalid_argument("Project upgrade plan no longer matches registered upgrade steps.");
        if (plan.Steps.empty())
            return;
        try
        {
            m_Impl->Prepare(current);
            m_Impl->Publish(ReadJournal(m_Impl->TransactionPath()));
        }
        catch (...)
        {
            const auto original = std::current_exception();
            try
            {
                m_Impl->Abort();
            }
            catch (...)
            {
            }
            std::rethrow_exception(original);
        }
    }

    void ProjectUpgradeService::Recover()
    {
        ProjectLock lock(m_Impl->Root);
        std::error_code activeError;
        if (!std::filesystem::exists(m_Impl->Active, activeError) || activeError)
            throw std::logic_error("There is no interrupted project upgrade to recover.");
        if (!m_Impl->JournalExists())
        {
            std::filesystem::remove_all(m_Impl->TransactionPath());
            return;
        }
        auto journal = ReadJournal(m_Impl->TransactionPath());
        m_Impl->ValidateJournalPlan(journal);
        auto phase = journal.at("phase").get<std::string>();
        if (phase == "initializing")
        {
            m_Impl->SnapshotBefore(journal);
            journal["phase"] = "snapshotted";
            WriteJournal(m_Impl->TransactionPath(), journal);
            phase = "snapshotted";
        }
        if (phase == "snapshotted" || phase == "prepared")
        {
            m_Impl->RunSteps(journal);
            phase = "ready";
        }
        if (phase != "ready" && phase != "publishing" && phase != "completed")
            throw std::runtime_error("Interrupted project upgrade has an unsupported journal phase.");
        m_Impl->Publish(std::move(journal));
    }

    void ProjectUpgradeService::Rollback()
    {
        ProjectLock lock(m_Impl->Root);
        std::error_code activeError;
        if (!std::filesystem::exists(m_Impl->Active, activeError) || activeError)
            throw std::logic_error("There is no interrupted project upgrade to roll back.");
        m_Impl->Abort();
    }

    ProjectUpgradeTransactionState ProjectUpgradeService::State() const noexcept
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(m_Impl->Active, error);
        return error || exists ? ProjectUpgradeTransactionState::Interrupted : ProjectUpgradeTransactionState::Clean;
    }

    const std::filesystem::path& ProjectUpgradeService::Root() const noexcept { return m_Impl->Root; }

    ProjectUpgradeStep ProjectUpgradeService::CreateVersion1To2Step()
    {
        ProjectUpgradeStep step;
        step.Id = "keire.project.v1-to-v2-required-source-modules";
        step.FromSchema = 1;
        step.ToSchema = 2;
        step.AffectedPaths = {"ProjectSettings/Project.keireproject"};
        step.Apply = [](const ProjectUpgradeStepContext& context)
        {
            const auto path = MarkerPath(context.StagingRoot);
            auto document = Json::parse(Detail::ReadTextFile(path, MaximumDescriptorBytes));
            document["schemaVersion"] = 2;
            document["requiredModules"] = Json::array();
            Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
        };
        step.Validate = [](const ProjectUpgradeStepContext& context)
        {
            if (ReadSchema(MarkerPath(context.StagingRoot)) != 2)
                throw std::runtime_error("v1 to v2 project upgrade did not produce schema 2.");
        };
        return step;
    }

    ProjectUpgradeStep ProjectUpgradeService::CreateVersion2To3Step()
    {
        ProjectUpgradeStep step;
        step.Id = "keire.project.v2-to-v3-hub-metadata";
        step.FromSchema = 2;
        step.ToSchema = 3;
        step.AffectedPaths = {"ProjectSettings/Project.keireproject"};
        step.Apply = [](const ProjectUpgradeStepContext& context)
        {
            const auto path = MarkerPath(context.StagingRoot);
            auto document = Json::parse(Detail::ReadTextFile(path, MaximumDescriptorBytes));
            document["schemaVersion"] = 3;
            document["createdAt"] = FileTimestampUtc(MarkerPath(context.ProjectRoot));
            document["lastSavedWithEngineVersion"] = document.at("createdWithEngineVersion");
            document["template"] = nullptr;
            Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
        };
        step.Validate = [](const ProjectUpgradeStepContext& context)
        {
            const auto document =
                Json::parse(Detail::ReadTextFile(MarkerPath(context.StagingRoot), MaximumDescriptorBytes));
            if (document.at("schemaVersion").get<std::uint32_t>() != 3 || !document.at("createdAt").is_string() ||
                !document.at("lastSavedWithEngineVersion").is_string())
                throw std::runtime_error("v2 to v3 project upgrade did not produce Hub metadata.");
        };
        return step;
    }
} // namespace Keire
