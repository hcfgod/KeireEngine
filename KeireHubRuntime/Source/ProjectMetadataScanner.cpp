#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include "KeireHubRuntime/ProjectStatusProbe.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/ProjectThumbnailDecode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <future>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumDescriptorBytes = std::size_t{1024U} * 1024U;
        constexpr std::size_t MaximumDescriptorDepth = 32;
        constexpr std::size_t HardMaximumCandidates = 4096;
        constexpr std::size_t HardMaximumDepth = 64;
        constexpr std::size_t HardMaximumEntries = 1'000'000;
        constexpr std::uint64_t HardMaximumBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t HardMaximumThumbnailBytes = 64ULL * 1024ULL * 1024ULL;
        constexpr std::size_t ProgressEntryInterval = 64;
        constexpr std::string_view DescriptorRelativePath = "ProjectSettings/Project.keireproject";
        constexpr std::string_view ThumbnailRelativePath = "ProjectSettings/HubThumbnail.png";
        constexpr std::array<unsigned char, 8> PngSignature{0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};

        struct PreparedCandidate final
        {
            std::string ProjectId;
            std::filesystem::path Root;
        };

        struct PendingDirectory final
        {
            std::filesystem::path Path;
            std::size_t Depth = 0;
        };

        enum class FileProbeState
        {
            Ready,
            Missing,
            Invalid
        };

        struct FileProbe final
        {
            FileProbeState State = FileProbeState::Invalid;
            std::filesystem::path Path;
            std::string Details;
        };

        enum class TraversalState
        {
            Completed,
            Cancelled,
            Invalid,
            LimitReached
        };

        struct TraversalResult final
        {
            TraversalState State = TraversalState::Invalid;
            std::uint64_t SizeBytes = 0;
            std::optional<std::uint64_t> ModifiedUnixSeconds;
            std::string Details;
        };

        using ScanResult = HubResult<std::shared_ptr<const ProjectMetadataScanSnapshot>>;

        [[nodiscard]] HubError ScanError(const HubErrorCode code, std::string message, std::string item,
                                         std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsBoundedText(const std::string_view value, const std::size_t maximumBytes) noexcept
        {
            if (value.empty() || value.size() > maximumBytes)
                return false;
            return std::ranges::none_of(value, [](const unsigned char character)
                                        { return character < 0x20U || character == 0x7fU; });
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

        [[nodiscard]] std::string ProjectIdKey(const std::string_view value)
        {
            if (!IsProjectId(value))
                return std::string(value);
            std::string result;
            result.reserve(32);
            for (const auto character : value)
            {
                if (character != '-')
                    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return result;
        }

        [[nodiscard]] bool IsProjectName(const std::string_view value) noexcept
        {
            if (!IsBoundedText(value, 128) || value == "." || value == ".." ||
                value.find_first_of("<>:\"/\\|?*\r\n\t") != std::string_view::npos)
            {
                return false;
            }
            return std::isspace(static_cast<unsigned char>(value.front())) == 0 &&
                   std::isspace(static_cast<unsigned char>(value.back())) == 0;
        }

        [[nodiscard]] std::string PortablePathKey(const std::filesystem::path& path)
        {
            auto result = Detail::PathToUtf8(path.lexically_normal());
            std::ranges::transform(result, result.begin(),
                                   [](const unsigned char value)
                                   {
                                       return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                                                           : static_cast<char>(value);
                                   });
            return result;
        }

        [[nodiscard]] bool PathLess(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            const auto leftKey = PortablePathKey(left);
            const auto rightKey = PortablePathKey(right);
            if (leftKey != rightKey)
                return leftKey < rightKey;
            return Detail::PathToUtf8(left) < Detail::PathToUtf8(right);
        }

        [[nodiscard]] bool ContainsTraversal(const std::filesystem::path& path)
        {
            return std::ranges::find(path, std::filesystem::path("..")) != path.end();
        }

        [[nodiscard]] bool IsNetworkPath(const std::filesystem::path& path)
        {
            const auto text = Detail::PathToUtf8(path);
            return text.starts_with("//") || text.starts_with("\\\\");
        }

        [[nodiscard]] bool IsFilesystemRoot(const std::filesystem::path& path)
        {
            return !path.empty() && path.lexically_normal() == path.root_path().lexically_normal();
        }

        [[nodiscard]] bool IsSameOrWithin(const std::filesystem::path& root, const std::filesystem::path& value)
        {
            return root == value || Detail::IsSafeRelativePath(value.lexically_relative(root));
        }

        [[nodiscard]] bool IsMissingError(const std::error_code& error) noexcept
        {
            return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
        }

        [[nodiscard]] HubResult<std::filesystem::path> PrepareRoot(const std::filesystem::path& requested)
        {
            if (requested.empty() || !requested.is_absolute() || ContainsTraversal(requested) ||
                IsNetworkPath(requested) || IsFilesystemRoot(requested))
            {
                return HubResult<std::filesystem::path>::Failure(
                    ScanError(HubErrorCode::InvalidArgument,
                              "A project scan root is broad, relative, remote, or contains traversal.",
                              Detail::PathToUtf8(requested)));
            }

            const auto normalized = requested.lexically_normal();
            auto current = normalized.root_path();
            for (const auto& component : normalized.relative_path())
            {
                current /= component;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(current, error);
                if (error)
                {
                    if (IsMissingError(error))
                        break;
                    return HubResult<std::filesystem::path>::Failure(
                        ScanError(HubErrorCode::InvalidArgument, "A project scan root could not be inspected.",
                                  Detail::PathToUtf8(requested), error.message()));
                }
                if (!std::filesystem::exists(status))
                    break;
                if (std::filesystem::is_symlink(status))
                {
                    return HubResult<std::filesystem::path>::Failure(ScanError(
                        HubErrorCode::InvalidArgument, "A project scan root must not traverse a symbolic link.",
                        Detail::PathToUtf8(requested)));
                }
            }

            std::error_code error;
            const auto status = std::filesystem::symlink_status(normalized, error);
            if (error && !IsMissingError(error))
            {
                return HubResult<std::filesystem::path>::Failure(
                    ScanError(HubErrorCode::InvalidArgument, "A project scan root could not be inspected.",
                              Detail::PathToUtf8(requested), error.message()));
            }
            if (!error && std::filesystem::exists(status))
            {
                auto canonical = std::filesystem::weakly_canonical(normalized, error);
                if (error || canonical.empty() || IsFilesystemRoot(canonical) || IsNetworkPath(canonical))
                {
                    return HubResult<std::filesystem::path>::Failure(
                        ScanError(HubErrorCode::InvalidArgument, "A project scan root could not be confined.",
                                  Detail::PathToUtf8(requested), error.message()));
                }
                return HubResult<std::filesystem::path>::Success(std::move(canonical));
            }
            return HubResult<std::filesystem::path>::Success(normalized);
        }

        [[nodiscard]] HubStatus ValidateLimits(const ProjectMetadataScanRequest& request)
        {
            const auto& limits = request.Limits;
            if (limits.MaximumCandidates == 0 || limits.MaximumCandidates > HardMaximumCandidates ||
                request.Projects.size() > limits.MaximumCandidates || limits.MaximumDepth > HardMaximumDepth ||
                limits.MaximumEntries == 0 || limits.MaximumEntries > HardMaximumEntries || limits.MaximumBytes == 0 ||
                limits.MaximumBytes > HardMaximumBytes || limits.MaximumThumbnailBytes == 0 ||
                limits.MaximumThumbnailBytes > HardMaximumThumbnailBytes)
            {
                return HubStatus::Failure(ScanError(HubErrorCode::InvalidArgument,
                                                    "The project metadata scan limits are invalid.",
                                                    "project-metadata-scan"));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubResult<std::vector<PreparedCandidate>>
        PrepareRequest(const ProjectMetadataScanRequest& request)
        {
            if (const auto limits = ValidateLimits(request); !limits)
                return HubResult<std::vector<PreparedCandidate>>::Failure(limits.Error());

            std::map<std::string, std::filesystem::path, std::less<>> ids;
            std::map<std::string, std::string, std::less<>> roots;
            std::vector<PreparedCandidate> result;
            result.reserve(request.Projects.size());
            for (const auto& candidate : request.Projects)
            {
                if (!IsBoundedText(candidate.ProjectId, 128))
                {
                    return HubResult<std::vector<PreparedCandidate>>::Failure(ScanError(
                        HubErrorCode::InvalidArgument, "A project scan identity is invalid.", candidate.ProjectId));
                }
                auto root = PrepareRoot(candidate.Root);
                if (!root)
                    return HubResult<std::vector<PreparedCandidate>>::Failure(root.Error());
                const auto idKey = ProjectIdKey(candidate.ProjectId);
                const auto rootKey = PortablePathKey(root.Value());
                if (!ids.emplace(idKey, root.Value()).second)
                {
                    return HubResult<std::vector<PreparedCandidate>>::Failure(ScanError(
                        HubErrorCode::DuplicateIdentifier,
                        "A project identity appears more than once in the scan request.", candidate.ProjectId));
                }
                if (!roots.emplace(rootKey, candidate.ProjectId).second)
                {
                    return HubResult<std::vector<PreparedCandidate>>::Failure(ScanError(
                        HubErrorCode::DuplicateIdentifier, "A project root appears more than once in the scan request.",
                        Detail::PathToUtf8(root.Value())));
                }
                result.push_back({candidate.ProjectId, std::move(root).Value()});
            }
            return HubResult<std::vector<PreparedCandidate>>::Success(std::move(result));
        }

        [[nodiscard]] FileProbe ProbeConfinedRegularFile(const std::filesystem::path& root,
                                                         const std::filesystem::path& relative)
        {
            if (!Detail::IsSafeRelativePath(relative))
                return {.Details = "The requested metadata path is not relative and confined."};
            auto current = root;
            for (const auto& component : relative)
            {
                current /= component;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(current, error);
                if (error)
                {
                    if (IsMissingError(error))
                        return {.State = FileProbeState::Missing};
                    return {.Details = error.message()};
                }
                if (!std::filesystem::exists(status))
                    return {.State = FileProbeState::Missing};
                if (std::filesystem::is_symlink(status))
                    return {.Details = "The metadata path traverses a symbolic link."};
            }

            std::error_code error;
            auto canonical = std::filesystem::weakly_canonical(root / relative, error);
            if (error || !IsSameOrWithin(root, canonical))
                return {.Details = error ? error.message() : "The metadata path escapes the project root."};
            const auto status = std::filesystem::symlink_status(canonical, error);
            if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
            {
                return {.Details = error ? error.message() : "The metadata path is not a regular file."};
            }
            return {.State = FileProbeState::Ready, .Path = std::move(canonical)};
        }

        [[nodiscard]] bool ReadOptionalBoundedString(const Detail::Json& document, const std::string_view key,
                                                     std::optional<std::string>& output)
        {
            const auto found = document.find(std::string(key));
            if (found == document.end() || found->is_null())
                return true;
            if (!found->is_string())
                return false;
            auto value = found->get<std::string>();
            if (!IsBoundedText(value, 128))
                return false;
            output = std::move(value);
            return true;
        }

        [[nodiscard]] std::optional<std::uint64_t> ToUnixSeconds(const std::filesystem::file_time_type value) noexcept
        {
            const auto converted =
                std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                       value - std::filesystem::file_time_type::clock::now());
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(converted.time_since_epoch()).count();
            if (seconds < 0)
                return std::nullopt;
            return static_cast<std::uint64_t>(seconds);
        }

        [[nodiscard]] std::optional<std::uint64_t> LastWriteSeconds(const std::filesystem::path& path,
                                                                    std::string& details)
        {
            std::error_code error;
            const auto value = std::filesystem::last_write_time(path, error);
            if (error)
            {
                details = error.message();
                return std::nullopt;
            }
            auto seconds = ToUnixSeconds(value);
            if (!seconds)
                details = "The file modification time predates the supported epoch.";
            return seconds;
        }

        [[nodiscard]] std::optional<HubError> ParseDescriptor(const PreparedCandidate& candidate,
                                                              ProjectMetadataScanResult& result)
        {
            const auto probe = ProbeConfinedRegularFile(candidate.Root, DescriptorRelativePath);
            if (probe.State == FileProbeState::Missing)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project descriptor is missing or inaccessible.", candidate.ProjectId);
            }
            if (probe.State != FileProbeState::Ready)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project descriptor is not safely confined.", candidate.ProjectId, probe.Details);
            }
            result.DescriptorPath = probe.Path;
            auto text = Detail::ReadTextFile(probe.Path, MaximumDescriptorBytes);
            if (!text)
                return text.Error();
            auto parsed =
                Detail::ParseStrictJson(text.Value(), MaximumDescriptorDepth, HubErrorCode::ProjectValidationFailed,
                                        "The project descriptor is malformed.", candidate.ProjectId);
            if (!parsed)
                return parsed.Error();
            if (!parsed.Value().is_object())
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project descriptor must contain an object.", candidate.ProjectId);
            }

            try
            {
                const auto schema = parsed.Value().at("schemaVersion").get<std::uint32_t>();
                const auto id = parsed.Value().at("id").get<std::string>();
                const auto name = parsed.Value().at("name").get<std::string>();
                if (schema == 0 || !IsProjectId(id) || ProjectIdKey(id) != ProjectIdKey(candidate.ProjectId) ||
                    !IsProjectName(name))
                {
                    return ScanError(HubErrorCode::ProjectValidationFailed,
                                     "The project descriptor identity or name is invalid.", candidate.ProjectId);
                }

                std::optional<std::string> createdWith;
                std::optional<std::string> minimum;
                std::optional<std::string> lastSaved;
                if (!ReadOptionalBoundedString(parsed.Value(), "createdWithEngineVersion", createdWith) ||
                    !ReadOptionalBoundedString(parsed.Value(), "minimumEngineVersion", minimum) ||
                    !ReadOptionalBoundedString(parsed.Value(), "lastSavedWithEngineVersion", lastSaved) ||
                    !createdWith || !minimum || (schema >= 3 && !lastSaved))
                {
                    return ScanError(HubErrorCode::ProjectValidationFailed,
                                     "The project descriptor contains invalid engine version metadata.",
                                     candidate.ProjectId);
                }

                result.DisplayName = name;
                result.Metadata.ProjectSchemaVersion = schema;
                result.Metadata.CreatedWithEngineVersion = std::move(createdWith);
                result.Metadata.MinimumEngineVersion = std::move(minimum);
                result.Metadata.LastSavedWithEngineVersion = std::move(lastSaved);
                result.Metadata.Status = schema > 3   ? HubProjectStatus::UnsupportedSchema
                                         : schema < 3 ? HubProjectStatus::UpgradeAvailable
                                                      : HubProjectStatus::Ready;

                const auto createdAt = parsed.Value().find("createdAt");
                if (schema >= 3 && (createdAt == parsed.Value().end() || !createdAt->is_string()))
                {
                    return ScanError(HubErrorCode::ProjectValidationFailed,
                                     "The project descriptor contains invalid creation metadata.", candidate.ProjectId);
                }
                if (createdAt != parsed.Value().end() && !createdAt->is_null())
                {
                    if (!createdAt->is_string())
                    {
                        return ScanError(HubErrorCode::ProjectValidationFailed,
                                         "The project descriptor contains invalid creation metadata.",
                                         candidate.ProjectId);
                    }
                    const auto instant = Detail::ParseUtcInstant(createdAt->get_ref<const std::string&>());
                    if (!instant || instant->UnixSeconds < 0)
                    {
                        return ScanError(HubErrorCode::ProjectValidationFailed,
                                         "The project descriptor contains invalid creation metadata.",
                                         candidate.ProjectId);
                    }
                    result.Metadata.CreatedUnixSeconds = static_cast<std::uint64_t>(instant->UnixSeconds);
                }

                const auto provenance = parsed.Value().find("template");
                if (provenance != parsed.Value().end() && !provenance->is_null())
                {
                    if (!provenance->is_object() || !ReadOptionalBoundedString(*provenance, "id", result.TemplateId) ||
                        !ReadOptionalBoundedString(*provenance, "version", result.TemplateVersion) ||
                        !result.TemplateId || !result.TemplateVersion)
                    {
                        return ScanError(HubErrorCode::ProjectValidationFailed,
                                         "The project descriptor contains invalid template provenance.",
                                         candidate.ProjectId);
                    }
                }
            }
            catch (const std::exception& error)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project descriptor is missing required metadata.", candidate.ProjectId,
                                 error.what());
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<HubError> InspectThumbnail(const PreparedCandidate& candidate,
                                                               const ProjectMetadataScanLimits& limits,
                                                               ProjectMetadataScanResult& result)
        {
            const auto probe = ProbeConfinedRegularFile(candidate.Root, ThumbnailRelativePath);
            if (probe.State == FileProbeState::Missing)
                return std::nullopt;
            if (probe.State != FileProbeState::Ready)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed, "The project thumbnail is not safely confined.",
                                 candidate.ProjectId, probe.Details);
            }
            std::error_code error;
            const auto size = std::filesystem::file_size(probe.Path, error);
            if (error || size > limits.MaximumThumbnailBytes)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project thumbnail is inaccessible or exceeds the size limit.",
                                 candidate.ProjectId, error.message());
            }
            std::ifstream stream(probe.Path, std::ios::binary);
            std::array<unsigned char, PngSignature.size()> signature{};
            stream.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
            if (!stream || signature != PngSignature)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project thumbnail does not have a PNG signature.", candidate.ProjectId);
            }
            std::string timeDetails;
            auto modified = LastWriteSeconds(probe.Path, timeDetails);
            if (!modified)
            {
                return ScanError(HubErrorCode::IoRead, "The project thumbnail modification time could not be read.",
                                 candidate.ProjectId, std::move(timeDetails));
            }
            std::string decodeDetails;
            auto image = Detail::DecodeProjectThumbnail(probe.Path, size, decodeDetails);
            if (!image)
            {
                return ScanError(HubErrorCode::ProjectValidationFailed,
                                 "The project thumbnail could not be decoded safely.", candidate.ProjectId,
                                 std::move(decodeDetails));
            }
            const auto sizeAfterDecode = std::filesystem::file_size(probe.Path, error);
            std::string timeAfterDecodeDetails;
            const auto modifiedAfterDecode = LastWriteSeconds(probe.Path, timeAfterDecodeDetails);
            if (error || !modifiedAfterDecode || sizeAfterDecode != size || *modifiedAfterDecode != *modified)
            {
                return ScanError(HubErrorCode::IoRead, "The project thumbnail changed while it was being read.",
                                 candidate.ProjectId, error ? error.message() : std::move(timeAfterDecodeDetails));
            }
            result.Thumbnail = ProjectThumbnailMetadata{
                .Path = probe.Path, .SizeBytes = sizeAfterDecode, .ModifiedUnixSeconds = *modifiedAfterDecode};
            result.ThumbnailImage = std::move(image);
            return std::nullopt;
        }

        [[nodiscard]] std::future<ScanResult> ReadyFuture(ScanResult result)
        {
            std::promise<ScanResult> promise;
            auto future = promise.get_future();
            promise.set_value(std::move(result));
            return future;
        }

        class ScanSession final
        {
          public:
            ScanSession(const ProjectMetadataScanRequest& request, std::vector<PreparedCandidate> candidates,
                        ProjectMetadataScanHooks hooks)
                : m_Request(request), m_Candidates(std::move(candidates)), m_Hooks(std::move(hooks))
            {
                m_Snapshot.TotalCandidates = m_Candidates.size();
            }

            [[nodiscard]] ScanResult Run()
            {
                Report(ProjectMetadataScanPhase::Validating, {}, {});
                if (Cancelled())
                    return Finish(ProjectMetadataScanState::Cancelled, ProjectMetadataScanPhase::Cancelled);

                for (const auto& candidate : m_Candidates)
                {
                    if (Cancelled())
                        return Finish(ProjectMetadataScanState::Cancelled, ProjectMetadataScanPhase::Cancelled);
                    ProjectMetadataScanResult result{.ProjectId = candidate.ProjectId,
                                                     .Root = candidate.Root,
                                                     .DescriptorPath = candidate.Root / DescriptorRelativePath};
                    Report(ProjectMetadataScanPhase::Descriptor, candidate.ProjectId, result.DescriptorPath);
                    std::error_code rootError;
                    const auto rootStatus = std::filesystem::symlink_status(candidate.Root, rootError);
                    if ((rootError && IsMissingError(rootError)) ||
                        (!rootError && !std::filesystem::exists(rootStatus)))
                    {
                        result.State = ProjectMetadataItemState::Missing;
                        result.Metadata.Status = HubProjectStatus::Missing;
                        result.Error = ScanError(HubErrorCode::NotFound, "The project folder could not be found.",
                                                 candidate.ProjectId);
                        Complete(std::move(result));
                        continue;
                    }
                    if (rootError || std::filesystem::is_symlink(rootStatus) ||
                        !std::filesystem::is_directory(rootStatus))
                    {
                        result.State = ProjectMetadataItemState::Invalid;
                        result.Metadata.Status = HubProjectStatus::Invalid;
                        result.Error = ScanError(HubErrorCode::ProjectValidationFailed,
                                                 "The project root is inaccessible or is not a directory.",
                                                 candidate.ProjectId, rootError.message());
                        Complete(std::move(result));
                        continue;
                    }

                    if (auto error = ParseDescriptor(candidate, result))
                    {
                        result.State = ProjectMetadataItemState::Invalid;
                        result.Metadata.Status = HubProjectStatus::Invalid;
                        result.Error = std::move(error);
                        Complete(std::move(result));
                        continue;
                    }
                    auto recovery = ProbeProjectRecovery(candidate.Root);
                    auto locked = ProbeProjectLock(candidate.Root);
                    if (!recovery || !locked)
                    {
                        result.State = ProjectMetadataItemState::Invalid;
                        result.Metadata.Status = HubProjectStatus::Invalid;
                        result.Error = !recovery ? recovery.Error() : locked.Error();
                        Complete(std::move(result));
                        continue;
                    }
                    if (result.Metadata.Status != HubProjectStatus::UnsupportedSchema)
                    {
                        if (locked.Value())
                            result.Metadata.Status = HubProjectStatus::Locked;
                        else if (recovery.Value())
                            result.Metadata.Status = HubProjectStatus::RecoveryRequired;
                    }
                    result.ThumbnailError = InspectThumbnail(candidate, m_Request.Limits, result);
                    Report(ProjectMetadataScanPhase::Files, candidate.ProjectId, candidate.Root);
                    auto traversal = Traverse(candidate, result);
                    if (traversal.State == TraversalState::Cancelled)
                        return Finish(ProjectMetadataScanState::Cancelled, ProjectMetadataScanPhase::Cancelled);
                    if (traversal.State == TraversalState::Invalid)
                    {
                        result.State = ProjectMetadataItemState::Invalid;
                        result.Metadata.Status = HubProjectStatus::Invalid;
                        result.Error =
                            ScanError(HubErrorCode::IoRead, "The project files could not be inspected safely.",
                                      candidate.ProjectId, std::move(traversal.Details));
                        Complete(std::move(result));
                        continue;
                    }
                    if (traversal.State == TraversalState::LimitReached)
                    {
                        result.State = ProjectMetadataItemState::LimitExceeded;
                        result.Metadata.Status = HubProjectStatus::Unknown;
                        result.Error = ScanError(HubErrorCode::ProjectValidationFailed,
                                                 "The project exceeded the metadata scan limits.", candidate.ProjectId,
                                                 std::move(traversal.Details));
                        Complete(std::move(result));
                        return Finish(ProjectMetadataScanState::LimitReached, ProjectMetadataScanPhase::LimitReached);
                    }
                    result.State = ProjectMetadataItemState::Ready;
                    result.Metadata.SizeBytes = traversal.SizeBytes;
                    result.Metadata.ModifiedUnixSeconds = traversal.ModifiedUnixSeconds;
                    Complete(std::move(result));
                }
                return Finish(ProjectMetadataScanState::Completed, ProjectMetadataScanPhase::Completed);
            }

          private:
            [[nodiscard]] bool Cancelled() const { return m_Hooks.IsCancelled && m_Hooks.IsCancelled(); }

            void Report(const ProjectMetadataScanPhase phase, std::string projectId, std::filesystem::path path) const
            {
                if (!m_Hooks.ReportProgress)
                    return;
                m_Hooks.ReportProgress({.Phase = phase,
                                        .CandidatesCompleted = m_Snapshot.CandidatesCompleted,
                                        .TotalCandidates = m_Snapshot.TotalCandidates,
                                        .EntriesVisited = m_Snapshot.EntriesVisited,
                                        .BytesVisited = m_Snapshot.BytesVisited,
                                        .CurrentProjectId = std::move(projectId),
                                        .CurrentPath = std::move(path)});
            }

            void Complete(ProjectMetadataScanResult result)
            {
                ++m_Snapshot.CandidatesCompleted;
                m_Snapshot.Results.push_back(std::move(result));
                Report(ProjectMetadataScanPhase::Files, m_Snapshot.Results.back().ProjectId,
                       m_Snapshot.Results.back().Root);
            }

            [[nodiscard]] TraversalResult Traverse(const PreparedCandidate& candidate,
                                                   ProjectMetadataScanResult& result)
            {
                TraversalResult traversal{.State = TraversalState::Completed};
                std::string timeDetails;
                traversal.ModifiedUnixSeconds = LastWriteSeconds(candidate.Root, timeDetails);
                if (!traversal.ModifiedUnixSeconds)
                    return {.State = TraversalState::Invalid, .Details = std::move(timeDetails)};

                std::vector<PendingDirectory> pending{{candidate.Root, 0}};
                std::size_t nextDirectory = 0;
                while (nextDirectory < pending.size())
                {
                    if (Cancelled())
                        return {.State = TraversalState::Cancelled};
                    auto directory = std::move(pending[nextDirectory++]);
                    std::error_code error;
                    std::filesystem::directory_iterator iterator(directory.Path, error);
                    const std::filesystem::directory_iterator end;
                    if (error)
                        return {.State = TraversalState::Invalid, .Details = error.message()};

                    std::vector<std::filesystem::path> children;
                    const auto remaining = m_Request.Limits.MaximumEntries - m_Snapshot.EntriesVisited;
                    while (iterator != end)
                    {
                        if (children.size() == remaining)
                        {
                            return {.State = TraversalState::LimitReached,
                                    .Details = "The project entry limit was reached."};
                        }
                        children.push_back(iterator->path());
                        iterator.increment(error);
                        if (error)
                            return {.State = TraversalState::Invalid, .Details = error.message()};
                    }
                    std::ranges::sort(children, PathLess);

                    for (const auto& path : children)
                    {
                        if (Cancelled())
                            return {.State = TraversalState::Cancelled};
                        ++m_Snapshot.EntriesVisited;
                        ++result.EntriesVisited;
                        const auto status = std::filesystem::symlink_status(path, error);
                        if (error)
                        {
                            return {.State = TraversalState::Invalid, .Details = error.message()};
                        }
                        if (std::filesystem::is_symlink(status))
                            continue;
                        auto canonical = std::filesystem::weakly_canonical(path, error);
                        if (error || !IsSameOrWithin(candidate.Root, canonical))
                        {
                            return {.State = TraversalState::Invalid,
                                    .Details = error ? error.message() : "A project entry escapes the project root."};
                        }

                        std::string modifiedDetails;
                        auto modified = LastWriteSeconds(canonical, modifiedDetails);
                        if (!modified)
                            return {.State = TraversalState::Invalid, .Details = std::move(modifiedDetails)};
                        traversal.ModifiedUnixSeconds = std::max(*traversal.ModifiedUnixSeconds, *modified);

                        if (std::filesystem::is_directory(status))
                        {
                            if (directory.Depth == m_Request.Limits.MaximumDepth)
                            {
                                return {.State = TraversalState::LimitReached,
                                        .Details = "The project directory depth limit was reached."};
                            }
                            pending.push_back({canonical, directory.Depth + 1});
                        }
                        else if (std::filesystem::is_regular_file(status))
                        {
                            const auto size = std::filesystem::file_size(canonical, error);
                            if (error)
                                return {.State = TraversalState::Invalid, .Details = error.message()};
                            if (size > m_Request.Limits.MaximumBytes - m_Snapshot.BytesVisited)
                            {
                                return {.State = TraversalState::LimitReached,
                                        .Details = "The project byte limit was reached."};
                            }
                            m_Snapshot.BytesVisited += size;
                            traversal.SizeBytes += size;
                        }
                        else
                        {
                            return {.State = TraversalState::Invalid,
                                    .Details = "A project entry is not a regular file or directory."};
                        }

                        if (m_Snapshot.EntriesVisited % ProgressEntryInterval == 0)
                        {
                            Report(ProjectMetadataScanPhase::Files, candidate.ProjectId, canonical);
                        }
                    }
                }
                return traversal;
            }

            [[nodiscard]] ScanResult Finish(const ProjectMetadataScanState state, const ProjectMetadataScanPhase phase)
            {
                m_Snapshot.State = state;
                Report(phase, {}, {});
                return ScanResult::Success(std::make_shared<const ProjectMetadataScanSnapshot>(std::move(m_Snapshot)));
            }

            const ProjectMetadataScanRequest& m_Request;
            std::vector<PreparedCandidate> m_Candidates;
            ProjectMetadataScanHooks m_Hooks;
            ProjectMetadataScanSnapshot m_Snapshot;
        };
    } // namespace

    class ProjectMetadataScanner::WorkerState final
    {
      public:
        std::atomic_bool Busy = false;
    };

    ProjectMetadataScanner::ProjectMetadataScanner() : m_WorkerState(std::make_shared<WorkerState>()) {}

    std::future<HubResult<std::shared_ptr<const ProjectMetadataScanSnapshot>>>
    ProjectMetadataScanner::ScanAsync(ProjectMetadataScanRequest request, ProjectMetadataScanHooks hooks) const
    {
        bool expected = false;
        if (!m_WorkerState->Busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return ReadyFuture(ScanResult::Failure(ScanError(HubErrorCode::InvalidTransition,
                                                             "A project metadata scan is already running.",
                                                             "project-metadata-scan", {}, true)));
        }

        try
        {
            const auto worker = m_WorkerState;
            return std::async(std::launch::async,
                              [worker, request = std::move(request), hooks = std::move(hooks)]() mutable
                              {
                                  struct BusyReset final
                                  {
                                      std::atomic_bool& Busy;
                                      ~BusyReset() { Busy.store(false, std::memory_order_release); }
                                  } reset{worker->Busy};
                                  try
                                  {
                                      auto prepared = PrepareRequest(request);
                                      if (!prepared)
                                          return ScanResult::Failure(prepared.Error());
                                      ScanSession session(request, std::move(prepared).Value(), std::move(hooks));
                                      return session.Run();
                                  }
                                  catch (const std::exception& error)
                                  {
                                      return ScanResult::Failure(ScanError(
                                          HubErrorCode::InvalidData, "The project metadata scan stopped unexpectedly.",
                                          "project-metadata-scan", error.what(), true));
                                  }
                                  catch (...)
                                  {
                                      return ScanResult::Failure(ScanError(
                                          HubErrorCode::InvalidData, "The project metadata scan stopped unexpectedly.",
                                          "project-metadata-scan", "An unknown worker exception was caught.", true));
                                  }
                              });
        }
        catch (const std::exception& error)
        {
            m_WorkerState->Busy.store(false, std::memory_order_release);
            return ReadyFuture(ScanResult::Failure(ScanError(HubErrorCode::InvalidData,
                                                             "The project metadata worker could not be started.",
                                                             "project-metadata-scan", error.what(), true)));
        }
    }

    ProjectThumbnailMetadataCache::ProjectThumbnailMetadataCache(const std::size_t capacity) : m_Capacity(capacity)
    {
        if (capacity == 0 || capacity > MaximumCapacity)
            throw std::invalid_argument("Project thumbnail metadata cache capacity is invalid.");
        m_Entries.reserve(capacity);
    }

    void ProjectThumbnailMetadataCache::Store(std::string projectId, ProjectThumbnailMetadata metadata)
    {
        if (!IsBoundedText(projectId, 128) || metadata.Path.empty() || !metadata.Path.is_absolute())
            throw std::invalid_argument("Project thumbnail metadata is invalid.");
        std::erase_if(m_Entries, [&](const auto& entry) { return entry.ProjectId == projectId; });
        if (m_Entries.size() == m_Capacity)
            m_Entries.pop_back();
        m_Entries.insert(m_Entries.begin(), Entry{.ProjectId = std::move(projectId), .Metadata = std::move(metadata)});
    }

    std::optional<ProjectThumbnailMetadata> ProjectThumbnailMetadataCache::Find(const std::string_view projectId)
    {
        const auto found = std::ranges::find(m_Entries, projectId, &Entry::ProjectId);
        if (found == m_Entries.end())
            return std::nullopt;
        auto entry = std::move(*found);
        m_Entries.erase(found);
        m_Entries.insert(m_Entries.begin(), std::move(entry));
        return m_Entries.front().Metadata;
    }

    void ProjectThumbnailMetadataCache::Erase(const std::string_view projectId) noexcept
    {
        std::erase_if(m_Entries, [&](const auto& entry) { return entry.ProjectId == projectId; });
    }

    void ProjectThumbnailMetadataCache::Clear() noexcept { m_Entries.clear(); }

    std::size_t ProjectThumbnailMetadataCache::Size() const noexcept { return m_Entries.size(); }

    std::size_t ProjectThumbnailMetadataCache::Capacity() const noexcept { return m_Capacity; }

    bool ProjectThumbnailImage::IsValid() const noexcept
    {
        return Width == PixelWidth && Height == PixelHeight && RgbaPixels &&
               RgbaPixels->size() == static_cast<std::size_t>(Width) * Height * 4U;
    }

    ProjectThumbnailCache::ProjectThumbnailCache(const std::size_t capacity) : m_Capacity(capacity)
    {
        if (capacity == 0 || capacity > MaximumCapacity)
            throw std::invalid_argument("Project thumbnail cache capacity is invalid.");
        m_Entries.reserve(capacity);
    }

    void ProjectThumbnailCache::Store(ProjectThumbnail thumbnail)
    {
        if (!IsBoundedText(thumbnail.ProjectId, 128) || thumbnail.Metadata.Path.empty() ||
            !thumbnail.Metadata.Path.is_absolute() || !thumbnail.Image.IsValid())
        {
            throw std::invalid_argument("Project thumbnail cache entry is invalid.");
        }
        std::erase_if(m_Entries, [&](const auto& entry) { return entry.ProjectId == thumbnail.ProjectId; });
        if (m_Entries.size() == m_Capacity)
            m_Entries.pop_back();
        m_Entries.insert(m_Entries.begin(), std::move(thumbnail));
    }

    std::optional<ProjectThumbnail> ProjectThumbnailCache::Find(const std::string_view projectId)
    {
        const auto found = std::ranges::find(m_Entries, projectId, &ProjectThumbnail::ProjectId);
        if (found == m_Entries.end())
            return std::nullopt;
        auto entry = std::move(*found);
        m_Entries.erase(found);
        m_Entries.insert(m_Entries.begin(), std::move(entry));
        return m_Entries.front();
    }

    void ProjectThumbnailCache::Erase(const std::string_view projectId) noexcept
    {
        std::erase_if(m_Entries, [&](const auto& entry) { return entry.ProjectId == projectId; });
    }

    void ProjectThumbnailCache::Clear() noexcept { m_Entries.clear(); }

    std::shared_ptr<const std::vector<ProjectThumbnail>> ProjectThumbnailCache::Snapshot() const
    {
        return std::make_shared<const std::vector<ProjectThumbnail>>(m_Entries);
    }

    std::size_t ProjectThumbnailCache::Size() const noexcept { return m_Entries.size(); }

    std::size_t ProjectThumbnailCache::Capacity() const noexcept { return m_Capacity; }
} // namespace KeireHub
