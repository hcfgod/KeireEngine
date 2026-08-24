#include "KeireHubRuntime/ProjectWorkflowManager.h"

#include "KeireHubRuntime/ProjectStatusProbe.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <system_error>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumDescriptorBytes = std::size_t{1024} * 1024;
        constexpr std::size_t HardMaximumCopiedEntries = 1'000'000;
        constexpr std::uint64_t HardMaximumCopiedBytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::array GeneratedDirectories{".git", ".vs", "Build", "Library", "Logs", "Temp"};

        struct ProjectDocument final
        {
            std::filesystem::path Root;
            std::filesystem::path DescriptorPath;
            std::string OriginalText;
            Detail::Json Descriptor;
            std::string Id;
            std::string Name;
            std::uint32_t SchemaVersion = 0;
        };

        struct CopyStatistics final
        {
            std::uint64_t Bytes = 0;
            std::size_t Entries = 0;
        };

        struct DuplicateCancelled final
        {
        };

        class OwnedDirectoryGuard final
        {
          public:
            explicit OwnedDirectoryGuard(std::filesystem::path path) : m_Path(std::move(path)) {}

            ~OwnedDirectoryGuard()
            {
                if (!m_Active)
                    return;
                std::error_code ignored;
                std::filesystem::remove_all(m_Path, ignored);
            }

            void Reset(std::filesystem::path path) { m_Path = std::move(path); }
            void Release() noexcept { m_Active = false; }

          private:
            std::filesystem::path m_Path;
            bool m_Active = true;
        };

        [[nodiscard]] HubError WorkflowError(const HubErrorCode code, std::string message, std::string item,
                                             std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsProjectName(const std::string_view name) noexcept
        {
            if (name.empty() || name.size() > 128 || name == "." || name == ".." ||
                name.find_first_of("<>:\"/\\|?*\r\n\t") != std::string_view::npos)
                return false;
            return std::isspace(static_cast<unsigned char>(name.front())) == 0 &&
                   std::isspace(static_cast<unsigned char>(name.back())) == 0;
        }

        [[nodiscard]] bool IsHexadecimal(const char value) noexcept
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        }

        [[nodiscard]] bool IsProjectId(const std::string_view value) noexcept
        {
            const bool compact = value.size() == 32;
            const bool canonical =
                value.size() == 36 && value[8] == '-' && value[13] == '-' && value[18] == '-' && value[23] == '-';
            if (!compact && !canonical)
                return false;
            std::size_t digits = 0;
            for (const auto character : value)
            {
                if (character == '-')
                    continue;
                if (!IsHexadecimal(character))
                    return false;
                ++digits;
            }
            return digits == 32;
        }

        [[nodiscard]] bool IsVersionFourProjectId(const std::string_view value) noexcept
        {
            return IsProjectId(value) && value.size() == 36 && value[14] == '4' &&
                   (value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b' || value[19] == 'A' ||
                    value[19] == 'B');
        }

        [[nodiscard]] std::string ProjectIdKey(const std::string_view value)
        {
            std::string result;
            result.reserve(32);
            for (const auto character : value)
            {
                if (character != '-')
                    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return result;
        }

        [[nodiscard]] bool SameProjectId(const std::string_view left, const std::string_view right)
        {
            return IsProjectId(left) && IsProjectId(right) && ProjectIdKey(left) == ProjectIdKey(right);
        }

        [[nodiscard]] bool IsTimestamp(const std::string_view value) noexcept
        {
            if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
                value[16] != ':' || value[19] != 'Z')
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || index == 19)
                    continue;
                if (value[index] < '0' || value[index] > '9')
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsSameOrWithin(const std::filesystem::path& root, const std::filesystem::path& value)
        {
            if (root == value)
                return true;
            const auto relative = value.lexically_relative(root);
            return Detail::IsSafeRelativePath(relative);
        }

        [[nodiscard]] std::string PortablePathKey(const std::filesystem::path& path)
        {
            auto result = Detail::PathToUtf8(path.lexically_normal());
            std::ranges::transform(result, result.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return result;
        }

        [[nodiscard]] HubResult<ProjectDocument> ReadProject(const std::filesystem::path& requestedRoot)
        {
            try
            {
                std::error_code error;
                auto candidate = std::filesystem::absolute(requestedRoot, error).lexically_normal();
                if (error)
                    throw std::filesystem::filesystem_error("Could not resolve the project path.", requestedRoot,
                                                            error);
                if (std::filesystem::is_regular_file(candidate, error))
                {
                    if (candidate.filename() != "Project.keireproject" ||
                        candidate.parent_path().filename() != "ProjectSettings")
                    {
                        return HubResult<ProjectDocument>::Failure(
                            WorkflowError(HubErrorCode::ProjectValidationFailed,
                                          "The selected file is not a Kéire project descriptor.",
                                          Detail::PathToUtf8(requestedRoot.filename())));
                    }
                    candidate = candidate.parent_path().parent_path();
                }
                else if (error)
                {
                    throw std::filesystem::filesystem_error("Could not inspect the project path.", requestedRoot,
                                                            error);
                }

                auto root = std::filesystem::weakly_canonical(candidate, error);
                if (error || !std::filesystem::is_directory(root, error) || error)
                    throw std::filesystem::filesystem_error("The project root is unavailable.", candidate, error);
                const auto descriptorPath = root / "ProjectSettings" / "Project.keireproject";
                const auto descriptorStatus = std::filesystem::symlink_status(descriptorPath, error);
                if (error || descriptorStatus.type() != std::filesystem::file_type::regular)
                {
                    return HubResult<ProjectDocument>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "The selected folder is not a valid Kéire project.",
                        Detail::PathToUtf8(root.filename()), error.message()));
                }

                auto text = Detail::ReadTextFile(descriptorPath, MaximumDescriptorBytes);
                if (!text)
                    return HubResult<ProjectDocument>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "The project descriptor could not be read.",
                        Detail::PathToUtf8(root.filename()), text.Error().TechnicalDetails, text.Error().Retryable));
                auto descriptor = Detail::Json::parse(text.Value());
                if (!descriptor.is_object() || !descriptor.contains("schemaVersion") ||
                    !descriptor.at("schemaVersion").is_number_unsigned() || !descriptor.contains("id") ||
                    !descriptor.at("id").is_string() || !descriptor.contains("name") ||
                    !descriptor.at("name").is_string())
                {
                    throw std::invalid_argument("Project identity fields are missing or malformed.");
                }
                ProjectDocument result{.Root = std::move(root),
                                       .DescriptorPath = descriptorPath,
                                       .OriginalText = std::move(text).Value(),
                                       .Descriptor = std::move(descriptor)};
                result.SchemaVersion = result.Descriptor.at("schemaVersion").get<std::uint32_t>();
                result.Id = result.Descriptor.at("id").get<std::string>();
                result.Name = result.Descriptor.at("name").get<std::string>();
                if (result.SchemaVersion == 0 || !IsProjectId(result.Id) || !IsProjectName(result.Name))
                    throw std::invalid_argument("Project identity fields are invalid.");
                return HubResult<ProjectDocument>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<ProjectDocument>::Failure(WorkflowError(
                    HubErrorCode::ProjectValidationFailed, "The selected folder is not a valid Kéire project.",
                    Detail::PathToUtf8(requestedRoot.filename()), error.what()));
            }
        }

        [[nodiscard]] HubResult<std::string> DefaultProjectId()
        {
            try
            {
                std::random_device random;
                std::array<unsigned char, 16> bytes{};
                for (auto& byte : bytes)
                    byte = static_cast<unsigned char>(random() & 0xffU);
                bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
                bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
                constexpr char Hex[] = "0123456789abcdef";
                constexpr std::array<std::size_t, 16> positions{0,  2,  4,  6,  9,  11, 14, 16,
                                                                19, 21, 24, 26, 28, 30, 32, 34};
                std::string result(36, '0');
                result[8] = result[13] = result[18] = result[23] = '-';
                for (std::size_t index = 0; index < bytes.size(); ++index)
                {
                    result[positions[index]] = Hex[bytes[index] >> 4U];
                    result[positions[index] + 1] = Hex[bytes[index] & 0x0fU];
                }
                return HubResult<std::string>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::string>::Failure(WorkflowError(HubErrorCode::InvalidData,
                                                                     "A new project identity could not be generated.",
                                                                     "project", error.what()));
            }
        }

        [[nodiscard]] HubResult<std::string> DefaultTimestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
#if defined(_WIN32)
            if (gmtime_s(&utc, &time) != 0)
#else
            if (!gmtime_r(&time, &utc))
#endif
            {
                return HubResult<std::string>::Failure(WorkflowError(
                    HubErrorCode::InvalidData, "The project creation time could not be generated.", "project"));
            }
            std::ostringstream stream;
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return HubResult<std::string>::Success(stream.str());
        }

        [[nodiscard]] std::uint64_t DefaultUnixSeconds()
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        [[nodiscard]] HubResult<HubRecentProject> FindProject(const HubProjectCatalog& catalog,
                                                              const std::string& projectId)
        {
            const auto snapshot = catalog.Snapshot();
            const auto found = std::ranges::find(*snapshot, projectId, &HubRecentProject::Id);
            if (found == snapshot->end())
            {
                return HubResult<HubRecentProject>::Failure(
                    WorkflowError(HubErrorCode::NotFound, "The project is no longer in the Hub.", projectId));
            }
            return HubResult<HubRecentProject>::Success(*found);
        }

        [[nodiscard]] HubResult<bool> IsLocked(ProjectWorkflowServices& services, const std::filesystem::path& root,
                                               const std::string& item)
        {
            try
            {
                return services.IsProjectLocked(root);
            }
            catch (const std::exception& error)
            {
                return HubResult<bool>::Failure(WorkflowError(
                    HubErrorCode::IoRead, "The project lock state could not be checked.", item, error.what(), true));
            }
            catch (...)
            {
                return HubResult<bool>::Failure(
                    WorkflowError(HubErrorCode::IoRead, "The project lock state could not be checked.", item,
                                  "The lock provider failed with a non-standard exception.", true));
            }
        }

        void ThrowIfCancelled(const ProjectDuplicateCallbacks& callbacks)
        {
            if (callbacks.IsCancelled && callbacks.IsCancelled())
                throw DuplicateCancelled{};
        }

        void ReportProgress(const ProjectDuplicateCallbacks& callbacks, const CopyStatistics& progress)
        {
            if (callbacks.ReportProgress)
                callbacks.ReportProgress(progress.Bytes, progress.Entries);
        }

        [[nodiscard]] HubStatus CopyRegularFile(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const std::uint64_t expectedSize, const std::string& projectId,
                                                const ProjectDuplicateCallbacks& callbacks,
                                                const CopyStatistics& progress)
        {
            ThrowIfCancelled(callbacks);
            std::error_code error;
            const auto before = std::filesystem::symlink_status(source, error);
            if (error || before.type() != std::filesystem::file_type::regular ||
                std::filesystem::file_size(source, error) != expectedSize || error)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                        "A project file changed before it could be duplicated.",
                                                        projectId, Detail::PathToUtf8(source.filename())));
            }
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::IoWrite,
                                                        "The duplicate staging directory could not be created.",
                                                        projectId, error.message(), true));
            }
            std::ifstream input(source, std::ios::binary);
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!input || !output)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::IoWrite,
                                                        "A project file could not be copied into staging.", projectId,
                                                        Detail::PathToUtf8(source.filename()), true));
            }
            std::array<char, std::size_t{64} * 1024> buffer{};
            std::uint64_t copied = 0;
            while (input)
            {
                ThrowIfCancelled(callbacks);
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (count <= 0)
                    continue;
                const auto bytes = static_cast<std::uint64_t>(count);
                if (copied > expectedSize || bytes > expectedSize - copied)
                {
                    return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                            "A project file changed while it was duplicated.",
                                                            projectId, Detail::PathToUtf8(source.filename())));
                }
                output.write(buffer.data(), count);
                if (!output)
                {
                    return HubStatus::Failure(WorkflowError(HubErrorCode::IoWrite,
                                                            "A project file could not be written into staging.",
                                                            projectId, Detail::PathToUtf8(source.filename()), true));
                }
                copied += bytes;
                ReportProgress(callbacks, {.Bytes = progress.Bytes + copied, .Entries = progress.Entries});
            }
            output.flush();
            if (!input.eof() || !output || copied != expectedSize)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                        "A staged project file is incomplete.", projectId,
                                                        Detail::PathToUtf8(source.filename())));
            }
            output.close();
            const auto after = std::filesystem::symlink_status(source, error);
            if (error || after.type() != std::filesystem::file_type::regular ||
                std::filesystem::file_size(source, error) != expectedSize || error)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                        "A project file changed while it was duplicated.", projectId,
                                                        Detail::PathToUtf8(source.filename())));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubResult<CopyStatistics> CopyCleanProject(const std::filesystem::path& source,
                                                                 const std::filesystem::path& staging,
                                                                 const ProjectDuplicateRequest& request,
                                                                 const ProjectDuplicateCallbacks& callbacks)
        {
            CopyStatistics result;
            ThrowIfCancelled(callbacks);
            std::set<std::string, std::less<>> paths;
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(source, std::filesystem::directory_options::none,
                                                                   error);
            const std::filesystem::recursive_directory_iterator end;
            if (error)
            {
                return HubResult<CopyStatistics>::Failure(
                    WorkflowError(HubErrorCode::IoRead, "The project files could not be enumerated.",
                                  request.SourceProjectId, error.message(), true));
            }
            while (iterator != end)
            {
                ThrowIfCancelled(callbacks);
                const auto relative = iterator->path().lexically_relative(source);
                const auto status = iterator->symlink_status(error);
                if (error || !Detail::IsSafeRelativePath(relative))
                {
                    return HubResult<CopyStatistics>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "A project path could not be safely duplicated.",
                        request.SourceProjectId, error.message()));
                }
                if (status.type() == std::filesystem::file_type::symlink)
                {
                    return HubResult<CopyStatistics>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "Project symbolic links cannot be duplicated safely.",
                        request.SourceProjectId, Detail::PathToUtf8(relative)));
                }
                const bool generatedDirectory =
                    status.type() == std::filesystem::file_type::directory && relative.parent_path().empty() &&
                    std::ranges::find(GeneratedDirectories, relative.filename().string()) != GeneratedDirectories.end();
                if (generatedDirectory)
                {
                    iterator.disable_recursion_pending();
                    iterator.increment(error);
                    if (error)
                    {
                        return HubResult<CopyStatistics>::Failure(
                            WorkflowError(HubErrorCode::IoRead, "The project files could not be enumerated.",
                                          request.SourceProjectId, error.message(), true));
                    }
                    continue;
                }
                const auto key = PortablePathKey(relative);
                if (!paths.insert(key).second)
                {
                    return HubResult<CopyStatistics>::Failure(
                        WorkflowError(HubErrorCode::ProjectValidationFailed,
                                      "The project contains paths that collide on a supported filesystem.",
                                      request.SourceProjectId, Detail::PathToUtf8(relative)));
                }
                if (++result.Entries > request.MaximumCopiedEntries)
                {
                    return HubResult<CopyStatistics>::Failure(
                        WorkflowError(HubErrorCode::ProjectValidationFailed,
                                      "The project contains too many files to duplicate.", request.SourceProjectId));
                }
                const auto destination = staging / relative;
                if (!IsSameOrWithin(staging, destination))
                {
                    return HubResult<CopyStatistics>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "A project path escapes the duplicate staging root.",
                        request.SourceProjectId, Detail::PathToUtf8(relative)));
                }
                if (status.type() == std::filesystem::file_type::directory)
                {
                    std::filesystem::create_directories(destination, error);
                    if (error)
                    {
                        return HubResult<CopyStatistics>::Failure(
                            WorkflowError(HubErrorCode::IoWrite, "A duplicate project directory could not be staged.",
                                          request.SourceProjectId, error.message(), true));
                    }
                }
                else if (status.type() == std::filesystem::file_type::regular)
                {
                    const auto size = iterator->file_size(error);
                    if (error || result.Bytes > request.MaximumCopiedBytes ||
                        size > request.MaximumCopiedBytes - result.Bytes)
                    {
                        return HubResult<CopyStatistics>::Failure(
                            WorkflowError(HubErrorCode::ProjectValidationFailed,
                                          "The project is larger than the configured duplicate limit.",
                                          request.SourceProjectId, error.message()));
                    }
                    if (const auto copy = CopyRegularFile(iterator->path(), destination, size, request.SourceProjectId,
                                                          callbacks, result);
                        !copy)
                        return HubResult<CopyStatistics>::Failure(copy.Error());
                    result.Bytes += size;
                }
                else
                {
                    return HubResult<CopyStatistics>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed, "The project contains an unsupported filesystem entry.",
                        request.SourceProjectId, Detail::PathToUtf8(relative)));
                }
                ReportProgress(callbacks, result);
                iterator.increment(error);
                if (error)
                {
                    return HubResult<CopyStatistics>::Failure(
                        WorkflowError(HubErrorCode::IoRead, "The project files could not be enumerated.",
                                      request.SourceProjectId, error.message(), true));
                }
            }
            return HubResult<CopyStatistics>::Success(result);
        }

        [[nodiscard]] HubStatus ValidateStagedTree(const std::filesystem::path& staging,
                                                   const ProjectDuplicateRequest& request)
        {
            std::size_t entries = 0;
            std::uint64_t bytes = 0;
            std::set<std::string, std::less<>> paths;
            std::error_code error;
            const auto rootStatus = std::filesystem::symlink_status(staging, error);
            if (error || rootStatus.type() != std::filesystem::file_type::directory)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                        "Project validation left staging unavailable.",
                                                        request.SourceProjectId, error.message()));
            }
            std::filesystem::recursive_directory_iterator iterator(staging, std::filesystem::directory_options::none,
                                                                   error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                const auto relative = iterator->path().lexically_relative(staging);
                const auto status = iterator->symlink_status(error);
                if (error || !Detail::IsSafeRelativePath(relative) ||
                    status.type() == std::filesystem::file_type::symlink ||
                    (status.type() != std::filesystem::file_type::directory &&
                     status.type() != std::filesystem::file_type::regular) ||
                    !paths.insert(PortablePathKey(relative)).second || ++entries > request.MaximumCopiedEntries)
                {
                    return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                            "Project validation left an unsafe staging tree.",
                                                            request.SourceProjectId, error.message()));
                }
                if (status.type() == std::filesystem::file_type::regular)
                {
                    const auto size = iterator->file_size(error);
                    if (error || bytes > request.MaximumCopiedBytes || size > request.MaximumCopiedBytes - bytes)
                    {
                        return HubStatus::Failure(
                            WorkflowError(HubErrorCode::ProjectValidationFailed,
                                          "Project validation exceeded the configured duplicate limit.",
                                          request.SourceProjectId, error.message()));
                    }
                    bytes += size;
                }
                iterator.increment(error);
            }
            if (error)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                        "Project staging could not be validated.",
                                                        request.SourceProjectId, error.message()));
            }
            return HubStatus::Success();
        }

        void PopulateMetadata(HubRecentProject& recent, const ProjectDocument& document,
                              const std::optional<std::uint64_t> sizeBytes = std::nullopt)
        {
            recent.CachedMetadata.ProjectSchemaVersion = document.SchemaVersion;
            recent.CachedMetadata.SizeBytes = sizeBytes;
            recent.CachedMetadata.Status = HubProjectStatus::Ready;
            const auto assignString = [&](const char* field, std::optional<std::string>& destination)
            {
                if (document.Descriptor.contains(field) && document.Descriptor.at(field).is_string())
                    destination = document.Descriptor.at(field).get<std::string>();
            };
            assignString("createdWithEngineVersion", recent.CachedMetadata.CreatedWithEngineVersion);
            assignString("lastSavedWithEngineVersion", recent.CachedMetadata.LastSavedWithEngineVersion);
            assignString("minimumEngineVersion", recent.CachedMetadata.MinimumEngineVersion);
        }
    } // namespace

    ProjectWorkflowManager::ProjectWorkflowManager(HubProjectCatalog& catalog, ProjectWorkflowServices services)
        : m_Catalog(catalog), m_Services(std::move(services))
    {
        if (!m_Services.GenerateProjectId)
            m_Services.GenerateProjectId = &DefaultProjectId;
        if (!m_Services.CurrentUtcTimestamp)
            m_Services.CurrentUtcTimestamp = &DefaultTimestamp;
        if (!m_Services.CurrentUnixSeconds)
            m_Services.CurrentUnixSeconds = &DefaultUnixSeconds;
        if (!m_Services.IsProjectLocked)
            m_Services.IsProjectLocked = &ProbeProjectLock;
    }

    HubResult<ProjectDuplicateResult> ProjectWorkflowManager::Duplicate(const ProjectDuplicateRequest& request)
    {
        auto plan = PrepareDuplicate(request);
        if (!plan)
            return HubResult<ProjectDuplicateResult>::Failure(plan.Error());
        auto staged = StageDuplicate(std::move(plan).Value());
        if (!staged)
            return HubResult<ProjectDuplicateResult>::Failure(staged.Error());
        if (staged.Value().State == ProjectDuplicateStageState::Cancelled)
        {
            return HubResult<ProjectDuplicateResult>::Failure(WorkflowError(
                HubErrorCode::InvalidTransition, "The project duplicate was cancelled.", request.SourceProjectId));
        }
        auto result = CommitDuplicate(staged.Value());
        if (!result)
            (void)DiscardDuplicate(staged.Value());
        return result;
    }

    HubResult<ProjectDuplicatePlan> ProjectWorkflowManager::PrepareDuplicate(const ProjectDuplicateRequest& request)
    {
        if (!IsProjectName(request.DisplayName) || request.Destination.empty() || request.SourceProjectId.empty() ||
            request.MaximumCopiedEntries == 0 || request.MaximumCopiedEntries > HardMaximumCopiedEntries ||
            request.MaximumCopiedBytes == 0 || request.MaximumCopiedBytes > HardMaximumCopiedBytes)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(
                HubErrorCode::InvalidArgument, "The duplicate project request is invalid.", request.SourceProjectId));
        }
        auto recentResult = FindProject(m_Catalog, request.SourceProjectId);
        if (!recentResult)
            return HubResult<ProjectDuplicatePlan>::Failure(recentResult.Error());
        auto recent = std::move(recentResult).Value();
        auto sourceResult = ReadProject(recent.Root);
        if (!sourceResult)
            return HubResult<ProjectDuplicatePlan>::Failure(sourceResult.Error());
        auto source = std::move(sourceResult).Value();
        if (!SameProjectId(source.Id, request.SourceProjectId))
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::ProjectValidationFailed,
                              "The registered folder belongs to a different project.", request.SourceProjectId));
        }
        if (!IsProjectWorkflowSchemaSupported(source.SchemaVersion))
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::UnsupportedSchema,
                              "This project must be upgraded before it can be duplicated.", request.SourceProjectId));
        }
        auto locked = IsLocked(m_Services, source.Root, request.SourceProjectId);
        if (!locked)
            return HubResult<ProjectDuplicatePlan>::Failure(locked.Error());
        if (locked.Value())
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(
                HubErrorCode::InvalidTransition, "Close the project before duplicating it.", request.SourceProjectId));
        }

        std::error_code error;
        auto destination = std::filesystem::absolute(request.Destination, error).lexically_normal();
        if (error || destination.filename().empty())
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(HubErrorCode::InvalidArgument,
                                                                          "The duplicate destination is invalid.",
                                                                          request.SourceProjectId, error.message()));
        }
        const auto parent = std::filesystem::weakly_canonical(destination.parent_path(), error);
        if (error || !std::filesystem::is_directory(parent, error) || error)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidArgument, "The duplicate destination parent is unavailable.",
                              request.SourceProjectId, error.message()));
        }
        destination = parent / destination.filename();
        if (IsSameOrWithin(source.Root, destination))
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidArgument, "A duplicate cannot be created inside its source project.",
                              request.SourceProjectId));
        }
        if (std::filesystem::exists(destination, error) || error)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(HubErrorCode::DestinationConflict,
                                                                          "The duplicate destination already exists.",
                                                                          request.SourceProjectId, error.message()));
        }

        HubResult<std::string> newId = HubResult<std::string>::Failure(WorkflowError(
            HubErrorCode::InvalidData, "A new project identity could not be generated.", request.SourceProjectId));
        try
        {
            newId = m_Services.GenerateProjectId();
        }
        catch (const std::exception& callbackError)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "A new project identity could not be generated.",
                              request.SourceProjectId, callbackError.what()));
        }
        catch (...)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "A new project identity could not be generated.",
                              request.SourceProjectId, "The identity provider failed with a non-standard exception."));
        }
        if (!newId)
            return HubResult<ProjectDuplicatePlan>::Failure(newId.Error());
        if (!IsVersionFourProjectId(newId.Value()))
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(
                HubErrorCode::InvalidData, "The generated project identity is invalid.", request.SourceProjectId));
        }
        const auto catalogSnapshot = m_Catalog.Snapshot();
        const bool catalogCollision = std::ranges::any_of(*catalogSnapshot, [&](const HubRecentProject& project)
                                                          { return SameProjectId(project.Id, newId.Value()); });
        if (SameProjectId(newId.Value(), source.Id) || catalogCollision)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::DuplicateIdentifier, "The generated project identity is already in use.",
                              request.SourceProjectId));
        }
        HubResult<std::string> timestamp = HubResult<std::string>::Failure(WorkflowError(
            HubErrorCode::InvalidData, "A project timestamp could not be generated.", request.SourceProjectId));
        try
        {
            timestamp = m_Services.CurrentUtcTimestamp();
        }
        catch (const std::exception& callbackError)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "A project timestamp could not be generated.",
                              request.SourceProjectId, callbackError.what()));
        }
        catch (...)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "A project timestamp could not be generated.",
                              request.SourceProjectId, "The timestamp provider failed with a non-standard exception."));
        }
        if (!timestamp)
            return HubResult<ProjectDuplicatePlan>::Failure(timestamp.Error());
        if (!IsTimestamp(timestamp.Value()))
        {
            return HubResult<ProjectDuplicatePlan>::Failure(WorkflowError(
                HubErrorCode::InvalidData, "The generated project timestamp is invalid.", request.SourceProjectId));
        }
        std::uint64_t addedUnixSeconds = 0;
        try
        {
            addedUnixSeconds = m_Services.CurrentUnixSeconds();
        }
        catch (const std::exception& callbackError)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "The project registration time could not be generated.",
                              request.SourceProjectId, callbackError.what()));
        }
        catch (...)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::InvalidData, "The project registration time could not be generated.",
                              request.SourceProjectId, "The time provider failed with a non-standard exception."));
        }

        const auto staging = parent / (".keire-duplicate-" + newId.Value());
        if (!IsSameOrWithin(parent, staging) || std::filesystem::exists(staging, error) || error)
        {
            return HubResult<ProjectDuplicatePlan>::Failure(
                WorkflowError(HubErrorCode::DestinationConflict, "A duplicate staging directory already exists.",
                              request.SourceProjectId, error.message()));
        }

        const auto sourceProjectSchemaVersion = source.SchemaVersion;
        return HubResult<ProjectDuplicatePlan>::Success(
            {.Request = request,
             .SourceRoot = std::move(source.Root),
             .Destination = std::move(destination),
             .Staging = staging,
             .NewProjectId = std::move(newId).Value(),
             .CreatedAt = std::move(timestamp).Value(),
             .AddedUnixSeconds = addedUnixSeconds,
             .PreferredEditorInstallationId = std::move(recent.PreferredEditorInstallationId),
             .SourceProjectSchemaVersion = sourceProjectSchemaVersion});
    }

    HubResult<ProjectDuplicateStagedResult>
    ProjectWorkflowManager::StageDuplicate(ProjectDuplicatePlan plan, const ProjectDuplicateCallbacks& callbacks) const
    {
        try
        {
            ThrowIfCancelled(callbacks);
            std::error_code error;
            if (!std::filesystem::create_directory(plan.Staging, error) || error)
            {
                return HubResult<ProjectDuplicateStagedResult>::Failure(
                    WorkflowError(HubErrorCode::IoWrite, "The duplicate staging directory could not be created.",
                                  plan.Request.SourceProjectId, error.message(), true));
            }
            OwnedDirectoryGuard cleanup(plan.Staging);
            auto copied = CopyCleanProject(plan.SourceRoot, plan.Staging, plan.Request, callbacks);
            if (!copied)
                return HubResult<ProjectDuplicateStagedResult>::Failure(copied.Error());
            ThrowIfCancelled(callbacks);
            auto stagedSource = ReadProject(plan.Staging);
            auto source = ReadProject(plan.SourceRoot);
            if (!source || !stagedSource || !SameProjectId(stagedSource.Value().Id, source.Value().Id))
            {
                return HubResult<ProjectDuplicateStagedResult>::Failure(WorkflowError(
                    HubErrorCode::ProjectValidationFailed, "The staged duplicate did not preserve the source identity.",
                    plan.Request.SourceProjectId,
                    !source ? source.Error().TechnicalDetails
                            : (stagedSource ? std::string{} : stagedSource.Error().TechnicalDetails)));
            }
            auto stagedDocument = std::move(stagedSource).Value();
            stagedDocument.Descriptor["id"] = plan.NewProjectId;
            stagedDocument.Descriptor["name"] = plan.Request.DisplayName;
            stagedDocument.Descriptor["createdAt"] = plan.CreatedAt;
            if (const auto write =
                    Detail::WriteJsonFileAtomically(stagedDocument.DescriptorPath, stagedDocument.Descriptor);
                !write)
            {
                return HubResult<ProjectDuplicateStagedResult>::Failure(write.Error());
            }

            ThrowIfCancelled(callbacks);
            if (plan.Request.ValidateStagedProject)
            {
                if (const auto validation = plan.Request.ValidateStagedProject(plan.Staging); !validation)
                {
                    return HubResult<ProjectDuplicateStagedResult>::Failure(WorkflowError(
                        HubErrorCode::ProjectValidationFailed,
                        "The selected editor could not validate the staged duplicate.", plan.Request.SourceProjectId,
                        std::string(ToString(validation.Error().Code)) + ": " + validation.Error().TechnicalDetails,
                        validation.Error().Retryable));
                }
            }
            ThrowIfCancelled(callbacks);
            if (const auto validation = ValidateStagedTree(plan.Staging, plan.Request); !validation)
                return HubResult<ProjectDuplicateStagedResult>::Failure(validation.Error());
            auto verified = ReadProject(plan.Staging);
            if (!verified || !SameProjectId(verified.Value().Id, plan.NewProjectId) ||
                verified.Value().Name != plan.Request.DisplayName ||
                verified.Value().SchemaVersion != plan.SourceProjectSchemaVersion)
            {
                return HubResult<ProjectDuplicateStagedResult>::Failure(WorkflowError(
                    HubErrorCode::ProjectValidationFailed, "The staged duplicate failed final identity validation.",
                    plan.Request.SourceProjectId, verified ? std::string{} : verified.Error().TechnicalDetails));
            }
            if (std::filesystem::exists(plan.Destination, error) || error)
            {
                return HubResult<ProjectDuplicateStagedResult>::Failure(WorkflowError(
                    HubErrorCode::DestinationConflict, "The duplicate destination was created by another operation.",
                    plan.Request.SourceProjectId, error.message()));
            }
            ThrowIfCancelled(callbacks);
            cleanup.Release();
            return HubResult<ProjectDuplicateStagedResult>::Success({.Plan = std::move(plan),
                                                                     .CopiedBytes = copied.Value().Bytes,
                                                                     .CopiedEntries = copied.Value().Entries});
        }
        catch (const DuplicateCancelled&)
        {
            return HubResult<ProjectDuplicateStagedResult>::Success(
                {.Plan = std::move(plan), .State = ProjectDuplicateStageState::Cancelled});
        }
        catch (const std::exception& error)
        {
            return HubResult<ProjectDuplicateStagedResult>::Failure(
                WorkflowError(HubErrorCode::ProjectValidationFailed, "The project duplicate could not be staged.",
                              plan.Request.SourceProjectId, error.what()));
        }
        catch (...)
        {
            return HubResult<ProjectDuplicateStagedResult>::Failure(WorkflowError(
                HubErrorCode::ProjectValidationFailed, "The project duplicate could not be staged.",
                plan.Request.SourceProjectId, "The staging worker failed with a non-standard exception."));
        }
    }

    HubResult<ProjectDuplicateResult> ProjectWorkflowManager::CommitDuplicate(ProjectDuplicateStagedResult staged)
    {
        const auto& plan = staged.Plan;
        if (staged.State != ProjectDuplicateStageState::ReadyToCommit)
        {
            return HubResult<ProjectDuplicateResult>::Failure(
                WorkflowError(HubErrorCode::InvalidTransition, "The cancelled duplicate cannot be published.",
                              plan.Request.SourceProjectId));
        }
        auto recentResult = FindProject(m_Catalog, plan.Request.SourceProjectId);
        if (!recentResult)
            return HubResult<ProjectDuplicateResult>::Failure(recentResult.Error());
        auto recent = std::move(recentResult).Value();
        auto source = ReadProject(recent.Root);
        if (!source || !SameProjectId(source.Value().Id, plan.Request.SourceProjectId) ||
            source.Value().Root != plan.SourceRoot)
        {
            return HubResult<ProjectDuplicateResult>::Failure(WorkflowError(
                HubErrorCode::InvalidTransition, "The source project changed before its duplicate could be published.",
                plan.Request.SourceProjectId, source ? std::string{} : source.Error().TechnicalDetails));
        }
        auto locked = IsLocked(m_Services, plan.SourceRoot, plan.Request.SourceProjectId);
        if (!locked)
            return HubResult<ProjectDuplicateResult>::Failure(locked.Error());
        if (locked.Value())
        {
            return HubResult<ProjectDuplicateResult>::Failure(
                WorkflowError(HubErrorCode::InvalidTransition,
                              "The source project was opened before its duplicate could be published.",
                              plan.Request.SourceProjectId));
        }
        const auto catalog = m_Catalog.Snapshot();
        if (std::ranges::any_of(*catalog,
                                [&](const auto& project) { return SameProjectId(project.Id, plan.NewProjectId); }))
        {
            return HubResult<ProjectDuplicateResult>::Failure(
                WorkflowError(HubErrorCode::DuplicateIdentifier, "The generated project identity is already in use.",
                              plan.Request.SourceProjectId));
        }
        const auto expectedStaging = plan.Destination.parent_path() / (".keire-duplicate-" + plan.NewProjectId);
        if (plan.Staging != expectedStaging || !IsSameOrWithin(plan.Destination.parent_path(), plan.Staging))
        {
            return HubResult<ProjectDuplicateResult>::Failure(
                WorkflowError(HubErrorCode::InvalidArgument, "The duplicate staging identity is invalid.",
                              plan.Request.SourceProjectId));
        }
        std::error_code error;
        const auto stagingStatus = std::filesystem::symlink_status(plan.Staging, error);
        if (error || stagingStatus.type() != std::filesystem::file_type::directory)
        {
            return HubResult<ProjectDuplicateResult>::Failure(WorkflowError(
                HubErrorCode::ProjectValidationFailed, "The duplicate staging directory is no longer trustworthy.",
                plan.Request.SourceProjectId, error.message()));
        }
        // StageDuplicate owns the bounded recursive validation. Commit repeats only constant-size identity and
        // publication checks so the catalog owner thread never scans the project tree.
        auto verified = ReadProject(plan.Staging);
        if (!verified || !SameProjectId(verified.Value().Id, plan.NewProjectId) ||
            verified.Value().Name != plan.Request.DisplayName ||
            verified.Value().SchemaVersion != plan.SourceProjectSchemaVersion)
        {
            return HubResult<ProjectDuplicateResult>::Failure(WorkflowError(
                HubErrorCode::ProjectValidationFailed, "The staged duplicate failed final identity validation.",
                plan.Request.SourceProjectId, verified ? std::string{} : verified.Error().TechnicalDetails));
        }
        if (std::filesystem::exists(plan.Destination, error) || error)
        {
            return HubResult<ProjectDuplicateResult>::Failure(WorkflowError(
                HubErrorCode::DestinationConflict, "The duplicate destination was created by another operation.",
                plan.Request.SourceProjectId, error.message()));
        }
        if (!Detail::TryRenamePathWithRetry(plan.Staging, plan.Destination, error))
        {
            return HubResult<ProjectDuplicateResult>::Failure(
                WorkflowError(HubErrorCode::IoWrite, "The staged duplicate could not be published.",
                              plan.Request.SourceProjectId, error.message(), true));
        }
        OwnedDirectoryGuard cleanup(plan.Destination);
        auto duplicated = std::move(verified).Value();
        duplicated.Root = plan.Destination;
        duplicated.DescriptorPath = plan.Destination / "ProjectSettings" / "Project.keireproject";
        HubRecentProject entry{.Id = plan.NewProjectId,
                               .Root = plan.Destination,
                               .Name = plan.Request.DisplayName,
                               .AddedUnixSeconds = plan.AddedUnixSeconds,
                               .PreferredEditorInstallationId = plan.PreferredEditorInstallationId};
        PopulateMetadata(entry, duplicated, staged.CopiedBytes);
        entry.CachedMetadata.CreatedUnixSeconds = entry.AddedUnixSeconds;
        if (const auto commit = m_Catalog.Upsert(std::move(entry)); !commit)
            return HubResult<ProjectDuplicateResult>::Failure(commit.Error());
        cleanup.Release();
        return HubResult<ProjectDuplicateResult>::Success({.ProjectId = plan.NewProjectId,
                                                           .Root = plan.Destination,
                                                           .DisplayName = plan.Request.DisplayName,
                                                           .CopiedBytes = staged.CopiedBytes,
                                                           .CopiedEntries = staged.CopiedEntries});
    }

    HubStatus ProjectWorkflowManager::DiscardDuplicate(const ProjectDuplicateStagedResult& staged) const
    {
        const auto& plan = staged.Plan;
        const auto expected = plan.Destination.parent_path() / (".keire-duplicate-" + plan.NewProjectId);
        if (plan.Staging.empty() || plan.Staging != expected || plan.NewProjectId.empty() ||
            !IsSameOrWithin(plan.Destination.parent_path(), plan.Staging))
        {
            return HubStatus::Failure(WorkflowError(HubErrorCode::InvalidArgument,
                                                    "The duplicate staging identity is invalid.",
                                                    plan.Request.SourceProjectId));
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(plan.Staging, error);
        if (!error && !exists)
            return HubStatus::Success();
        if (error)
        {
            return HubStatus::Failure(WorkflowError(HubErrorCode::IoRead,
                                                    "The duplicate staging directory could not be inspected.",
                                                    plan.Request.SourceProjectId, error.message(), true));
        }
        (void)std::filesystem::remove_all(plan.Staging, error);
        if (error)
        {
            return HubStatus::Failure(WorkflowError(HubErrorCode::IoWrite,
                                                    "The duplicate staging directory could not be removed.",
                                                    plan.Request.SourceProjectId, error.message(), true));
        }
        return HubStatus::Success();
    }

    HubStatus ProjectWorkflowManager::LocateMovedProject(const std::string& projectId,
                                                         const std::filesystem::path& candidateRoot)
    {
        auto recent = FindProject(m_Catalog, projectId);
        if (!recent)
            return HubStatus::Failure(recent.Error());
        if (const auto registered = ReadProject(recent.Value().Root);
            registered && SameProjectId(registered.Value().Id, projectId))
        {
            return HubStatus::Failure(WorkflowError(
                HubErrorCode::InvalidTransition,
                "The registered project is still available. Locate is only for a project that has moved.", projectId));
        }
        auto candidate = ReadProject(candidateRoot);
        if (!candidate)
            return HubStatus::Failure(candidate.Error());
        if (!SameProjectId(candidate.Value().Id, projectId))
        {
            return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                    "The selected folder belongs to a different project.", projectId));
        }
        for (const auto& entry : *m_Catalog.Snapshot())
        {
            if (entry.Id == projectId)
                continue;
            std::error_code error;
            const auto registered = std::filesystem::weakly_canonical(entry.Root, error);
            if (!error && registered == candidate.Value().Root)
            {
                return HubStatus::Failure(WorkflowError(HubErrorCode::DuplicateIdentifier,
                                                        "The selected folder is already registered as another project.",
                                                        projectId));
            }
        }
        return m_Catalog.Locate(projectId, candidate.Value().Root, projectId);
    }

    HubStatus ProjectWorkflowManager::RenameDisplayName(const std::string& projectId, const std::string& displayName)
    {
        if (!IsProjectName(displayName))
        {
            return HubStatus::Failure(
                WorkflowError(HubErrorCode::InvalidArgument, "The project display name is invalid.", projectId));
        }
        auto recentResult = FindProject(m_Catalog, projectId);
        if (!recentResult)
            return HubStatus::Failure(recentResult.Error());
        auto recent = std::move(recentResult).Value();
        auto documentResult = ReadProject(recent.Root);
        if (!documentResult)
            return HubStatus::Failure(documentResult.Error());
        auto document = std::move(documentResult).Value();
        if (!SameProjectId(document.Id, projectId))
        {
            return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                    "The registered folder belongs to a different project.",
                                                    projectId));
        }
        if (!IsProjectWorkflowSchemaSupported(document.SchemaVersion))
        {
            return HubStatus::Failure(WorkflowError(
                HubErrorCode::UnsupportedSchema,
                "A compatible editor must upgrade this project before its display name can change.", projectId));
        }
        auto locked = IsLocked(m_Services, document.Root, projectId);
        if (!locked)
            return HubStatus::Failure(locked.Error());
        if (locked.Value())
        {
            return HubStatus::Failure(
                WorkflowError(HubErrorCode::InvalidTransition, "Close the project before renaming it.", projectId));
        }
        if (document.Name == displayName && recent.Name == displayName)
            return HubStatus::Success();

        document.Descriptor["name"] = displayName;
        if (const auto write = Detail::WriteJsonFileAtomically(document.DescriptorPath, document.Descriptor); !write)
            return write;
        auto verification = ReadProject(document.Root);
        if (!verification || !SameProjectId(verification.Value().Id, projectId) ||
            verification.Value().Name != displayName)
        {
            const auto rollback = Detail::WriteTextFileAtomically(document.DescriptorPath, document.OriginalText);
            return HubStatus::Failure(WorkflowError(HubErrorCode::ProjectValidationFailed,
                                                    "The renamed project failed identity validation.", projectId,
                                                    rollback ? std::string{} : rollback.Error().TechnicalDetails));
        }
        recent.Root = document.Root;
        recent.Name = displayName;
        PopulateMetadata(recent, verification.Value(), recent.CachedMetadata.SizeBytes);
        if (auto commit = m_Catalog.Upsert(std::move(recent)); !commit)
        {
            const auto rollback = Detail::WriteTextFileAtomically(document.DescriptorPath, document.OriginalText);
            if (!rollback)
            {
                return HubStatus::Failure(WorkflowError(
                    HubErrorCode::IoWrite, "The project registry failed and the descriptor could not be restored.",
                    projectId, std::string(ToString(commit.Error().Code)) + "; " + rollback.Error().TechnicalDetails,
                    true));
            }
            return commit;
        }
        return HubStatus::Success();
    }

    HubStatus ProjectWorkflowManager::RemoveFromHub(const std::string& projectId)
    {
        return m_Catalog.Remove(projectId);
    }
} // namespace KeireHub
