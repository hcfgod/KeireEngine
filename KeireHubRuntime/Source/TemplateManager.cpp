#include "KeireHubRuntime/TemplateManager.h"

#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <system_error>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = std::size_t{16} * 1024 * 1024;
        constexpr std::uint64_t ProjectMetadataReserveBytes = 64ULL * 1024ULL;
        constexpr std::uint64_t MaximumTemplatePayloadBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;

        class StagingGuard final
        {
          public:
            explicit StagingGuard(std::filesystem::path path) : m_Path(std::move(path)) {}

            ~StagingGuard()
            {
                if (!m_Active)
                    return;
                std::error_code ignored;
                std::filesystem::remove_all(m_Path, ignored);
            }

            void Release() noexcept { m_Active = false; }

          private:
            std::filesystem::path m_Path;
            bool m_Active = true;
        };

        [[nodiscard]] HubError TemplateError(const HubErrorCode code, std::string message, const std::string& item,
                                             std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = item,
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool CancellationRequested(const std::function<bool()>& callback) noexcept
        {
            if (!callback)
                return false;
            try
            {
                return callback();
            }
            catch (...)
            {
                return true;
            }
        }

        [[nodiscard]] HubError CancellationError(const std::string& projectName)
        {
            auto error = TemplateError(HubErrorCode::WorkerInterrupted, "Project creation was cancelled.", projectName,
                                       "The project-creation worker received a stop request.");
            error.Retryable = true;
            return error;
        }

        [[nodiscard]] bool IsUuid(const std::string_view value, const bool requireVersionFour) noexcept
        {
            if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-')
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 8 || index == 13 || index == 18 || index == 23)
                    continue;
                const auto character = value[index];
                if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
                    return false;
            }
            if (!requireVersionFour)
                return true;
            return value[14] == '4' && (value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b');
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

        [[nodiscard]] std::string PathKey(const std::filesystem::path& path)
        {
            auto result = Detail::PathToUtf8(path.lexically_normal());
            std::ranges::transform(result, result.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return result;
        }

        [[nodiscard]] bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& value)
        {
            const auto relative = value.lexically_relative(root);
            return Detail::IsSafeRelativePath(relative);
        }

        [[nodiscard]] bool IsSameOrWithin(const std::filesystem::path& root, const std::filesystem::path& value)
        {
            const auto rootKey = PathKey(root);
            const auto valueKey = PathKey(value);
            if (rootKey == valueKey)
                return true;
            if (rootKey.empty() || valueKey.size() <= rootKey.size() || !valueKey.starts_with(rootKey))
                return false;
            return rootKey.ends_with('/') || valueKey[rootKey.size()] == '/';
        }

        [[nodiscard]] bool HasSymlinkComponent(const std::filesystem::path& root, const std::filesystem::path& relative,
                                               std::error_code& error)
        {
            auto current = root;
            for (const auto& component : relative)
            {
                current /= component;
                const auto status = std::filesystem::symlink_status(current, error);
                if (error || status.type() == std::filesystem::file_type::symlink)
                    return true;
            }
            return false;
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
                std::string result(36, '0');
                constexpr std::array<std::size_t, 16> positions{0,  2,  4,  6,  9,  11, 14, 16,
                                                                19, 21, 24, 26, 28, 30, 32, 34};
                for (std::size_t index = 0; index < bytes.size(); ++index)
                {
                    result[positions[index]] = Hex[bytes[index] >> 4U];
                    result[positions[index] + 1] = Hex[bytes[index] & 0x0fU];
                }
                result[8] = '-';
                result[13] = '-';
                result[18] = '-';
                result[23] = '-';
                return HubResult<std::string>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::string>::Failure(TemplateError(
                    HubErrorCode::IoWrite, "A new project identity could not be generated.", "project", error.what()));
            }
        }

        [[nodiscard]] HubResult<std::string> DefaultTimestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto value = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
#if defined(_WIN32)
            if (gmtime_s(&utc, &value) != 0)
#else
            if (!gmtime_r(&value, &utc))
#endif
            {
                return HubResult<std::string>::Failure(TemplateError(
                    HubErrorCode::IoWrite, "The project creation timestamp could not be generated.", "project"));
            }
            std::ostringstream stream;
            stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
            return HubResult<std::string>::Success(stream.str());
        }

        [[nodiscard]] HubResult<std::uint64_t> DefaultAvailableSpace(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto information = std::filesystem::space(path, error);
            if (error)
            {
                return HubResult<std::uint64_t>::Failure(
                    TemplateError(HubErrorCode::IoRead, "Available project disk space could not be determined.",
                                  Detail::PathToUtf8(path.filename()), error.message()));
            }
            return HubResult<std::uint64_t>::Success(information.available);
        }

        [[nodiscard]] HubStatus ValidatePayload(const std::filesystem::path& templatesRoot,
                                                const HubTemplateManifest& manifest)
        {
            try
            {
                std::error_code error;
                const auto canonicalRoot = std::filesystem::weakly_canonical(templatesRoot, error);
                if (error || !std::filesystem::is_directory(canonicalRoot))
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "The template content root is unavailable.", manifest.Id,
                                                            error.message()));
                error.clear();
                if (HasSymlinkComponent(templatesRoot, manifest.PayloadRoot, error))
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "The template payload path contains a symbolic link.",
                                                            manifest.Id, error.message()));
                const auto payload = templatesRoot / manifest.PayloadRoot;
                const auto payloadStatus = std::filesystem::symlink_status(payload, error);
                if (error || payloadStatus.type() != std::filesystem::file_type::directory)
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "The template payload directory is invalid.", manifest.Id,
                                                            error.message()));
                const auto canonicalPayload = std::filesystem::weakly_canonical(payload, error);
                if (error || !IsWithin(canonicalRoot, canonicalPayload))
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "The template payload escapes its content root.",
                                                            manifest.Id));

                std::map<std::string, const TemplatePayloadFile*, std::less<>> declared;
                for (const auto& file : manifest.PayloadFiles)
                    declared.emplace(PathKey(file.Path), &file);
                std::set<std::string, std::less<>> discovered;
                for (std::filesystem::recursive_directory_iterator iterator(canonicalPayload), end; iterator != end;
                     ++iterator)
                {
                    const auto status = iterator->symlink_status(error);
                    if (error || (status.type() != std::filesystem::file_type::directory &&
                                  status.type() != std::filesystem::file_type::regular))
                    {
                        return HubStatus::Failure(TemplateError(
                            HubErrorCode::TemplatePayloadInvalid,
                            "Template payloads cannot contain links or special files.", manifest.Id, error.message()));
                    }
                    if (status.type() == std::filesystem::file_type::directory)
                        continue;
                    const auto relative = iterator->path().lexically_relative(canonicalPayload);
                    const auto key = PathKey(relative);
                    const auto declaration = declared.find(key);
                    if (declaration == declared.end() ||
                        Detail::PathToUtf8(relative.lexically_normal()) !=
                            Detail::PathToUtf8(declaration->second->Path.lexically_normal()))
                        return HubStatus::Failure(
                            TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                          "The template payload contains an undeclared or case-colliding file.",
                                          manifest.Id, Detail::PathToUtf8(relative)));
                    const auto size = iterator->file_size(error);
                    if (error || size != declaration->second->SizeBytes || size > MaximumTemplatePayloadBytes)
                        return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                                "A template payload file has an unexpected size.",
                                                                manifest.Id, Detail::PathToUtf8(relative)));
                    auto digest = Detail::Sha256File(iterator->path(), declaration->second->SizeBytes);
                    if (!digest || digest.Value() != declaration->second->Sha256)
                        return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                                "A template payload file failed integrity validation.",
                                                                manifest.Id, Detail::PathToUtf8(relative)));
                    discovered.insert(key);
                }
                if (discovered.size() != declared.size())
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "The template payload is missing a declared file.",
                                                            manifest.Id));
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "The template payload could not be inspected.", manifest.Id,
                                                        error.what()));
            }
        }

        [[nodiscard]] HubStatus CopyPayloadFile(const std::filesystem::path& source,
                                                const std::filesystem::path& destination,
                                                const TemplatePayloadFile& declaration, const std::string& templateId,
                                                const std::string& projectName,
                                                const std::function<bool()>& cancellationRequested)
        {
            if (CancellationRequested(cancellationRequested))
                return HubStatus::Failure(CancellationError(projectName));
            std::error_code error;
            const auto before = std::filesystem::symlink_status(source, error);
            if (error || before.type() != std::filesystem::file_type::regular ||
                std::filesystem::file_size(source, error) != declaration.SizeBytes || error)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "A template payload file changed before it could be copied.",
                                                        templateId, Detail::PathToUtf8(declaration.Path)));
            }
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error)
                return HubStatus::Failure(TemplateError(HubErrorCode::IoWrite,
                                                        "A project staging directory could not be created.", templateId,
                                                        error.message()));
            std::ifstream input(source, std::ios::binary);
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!input || !output)
                return HubStatus::Failure(TemplateError(HubErrorCode::IoWrite,
                                                        "A template payload file could not be staged.", templateId,
                                                        Detail::PathToUtf8(declaration.Path)));

            Detail::Sha256Builder digest;
            std::array<std::byte, std::size_t{64U} * 1024U> buffer{};
            std::uint64_t copied = 0;
            while (input)
            {
                if (CancellationRequested(cancellationRequested))
                    return HubStatus::Failure(CancellationError(projectName));
                input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (count <= 0)
                    continue;
                const auto bytes = static_cast<std::uint64_t>(count);
                if (copied > declaration.SizeBytes || bytes > declaration.SizeBytes - copied)
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "A template payload file exceeded its declared size.",
                                                            templateId, Detail::PathToUtf8(declaration.Path)));
                output.write(reinterpret_cast<const char*>(buffer.data()), count);
                if (!output)
                    return HubStatus::Failure(TemplateError(HubErrorCode::IoWrite,
                                                            "A template payload file could not be written.", templateId,
                                                            Detail::PathToUtf8(declaration.Path)));
                digest.Update(std::span(buffer).first(static_cast<std::size_t>(count)));
                copied += bytes;
            }
            output.flush();
            const auto copiedDigest = Detail::DigestToString(digest.Finish());
            if (!input.eof() || !output || copied != declaration.SizeBytes || copiedDigest != declaration.Sha256)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "A staged template file failed integrity validation.",
                                                        templateId, Detail::PathToUtf8(declaration.Path)));
            }
            output.close();
            if (!output)
                return HubStatus::Failure(TemplateError(HubErrorCode::IoWrite,
                                                        "A template payload file could not be committed to staging.",
                                                        templateId, Detail::PathToUtf8(declaration.Path)));
            std::error_code destinationError;
            const auto destinationStatus = std::filesystem::symlink_status(destination, destinationError);
            if (destinationError || destinationStatus.type() != std::filesystem::file_type::regular)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "A staged template path is not a regular file.", templateId,
                                                        Detail::PathToUtf8(declaration.Path)));
            }
            auto destinationDigest = Detail::Sha256File(destination, declaration.SizeBytes);
            if (!destinationDigest || destinationDigest.Value() != declaration.Sha256)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "A staged template path failed integrity validation.",
                                                        templateId, Detail::PathToUtf8(declaration.Path)));
            }
            const auto after = std::filesystem::symlink_status(source, error);
            if (error || after.type() != std::filesystem::file_type::regular ||
                std::filesystem::file_size(source, error) != declaration.SizeBytes || error)
            {
                return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                        "A template payload file changed while it was copied.",
                                                        templateId, Detail::PathToUtf8(declaration.Path)));
            }
            return HubStatus::Success();
        }
    } // namespace

    bool IsValidProjectName(const std::string_view name) noexcept
    {
        if (name.empty() || name.size() > 128 || name == "." || name == ".." ||
            name.find_first_of("<>:\"/\\|?*\r\n\t") != std::string_view::npos)
            return false;
        return std::isspace(static_cast<unsigned char>(name.front())) == 0 &&
               std::isspace(static_cast<unsigned char>(name.back())) == 0;
    }

    TemplateManager::TemplateManager(std::filesystem::path templatesRoot, TemplateManagerServices services)
        : m_Root(std::move(templatesRoot)), m_Services(std::move(services)),
          m_Snapshot(std::make_shared<const std::vector<HubTemplateManifest>>())
    {
        if (!m_Services.GenerateProjectId)
            m_Services.GenerateProjectId = &DefaultProjectId;
        if (!m_Services.CurrentUtcTimestamp)
            m_Services.CurrentUtcTimestamp = &DefaultTimestamp;
        if (!m_Services.AvailableSpace)
            m_Services.AvailableSpace = &DefaultAvailableSpace;
    }

    HubStatus TemplateManager::Load()
    {
        const auto catalogPath = m_Root / "catalog.json";
        auto text = Detail::ReadTextFile(catalogPath, MaximumCatalogBytes);
        if (!text)
            return HubStatus::Failure(text.Error());
        auto catalog = ParseTemplateCatalog(text.Value());
        if (!catalog)
            return HubStatus::Failure(catalog.Error());
        for (const auto& manifest : catalog.Value().Templates)
        {
            if (const auto status = ValidatePayload(m_Root, manifest); !status)
                return status;
        }
        std::ranges::sort(catalog.Value().Templates,
                          [](const auto& left, const auto& right)
                          {
                              if (left.Id != right.Id)
                                  return left.Id < right.Id;
                              return left.Version > right.Version;
                          });
        m_Snapshot = std::make_shared<const std::vector<HubTemplateManifest>>(std::move(catalog).Value().Templates);
        return HubStatus::Success();
    }

    HubResult<TemplateCreationPlan> TemplateManager::Preflight(const TemplateCreateRequest& request) const
    {
        if (!IsValidProjectName(request.ProjectName) || !Detail::IsBoundedIdentifier(request.TemplateId) ||
            request.Destination.empty() || request.EditorMinimumProjectSchema == 0 ||
            request.EditorMaximumProjectSchema < request.EditorMinimumProjectSchema ||
            !Detail::IsBoundedIdentifier(request.PlatformTarget, 64))
        {
            return HubResult<TemplateCreationPlan>::Failure(TemplateError(
                HubErrorCode::InvalidArgument, "The project creation request is invalid.", request.ProjectName));
        }

        const auto hasIdentity =
            std::ranges::any_of(*m_Snapshot, [&](const auto& value) { return value.Id == request.TemplateId; });
        const auto hasRequestedVersion =
            std::ranges::any_of(*m_Snapshot,
                                [&](const auto& value)
                                {
                                    return value.Id == request.TemplateId &&
                                           (!request.TemplateVersion || value.Version == *request.TemplateVersion);
                                });
        const HubTemplateManifest* selected = nullptr;
        for (const auto& candidate : *m_Snapshot)
        {
            if (candidate.Id != request.TemplateId ||
                (request.TemplateVersion && candidate.Version != *request.TemplateVersion))
                continue;
            if (candidate.ProjectSchema != 3 || candidate.ProjectSchema < request.EditorMinimumProjectSchema ||
                candidate.ProjectSchema > request.EditorMaximumProjectSchema ||
                !candidate.CompatibleEditors.Matches(request.EditorVersion) ||
                candidate.PlatformTarget != request.PlatformTarget)
                continue;
            selected = &candidate;
            break;
        }
        if (!selected)
        {
            const auto code = hasIdentity && hasRequestedVersion ? HubErrorCode::TemplateIncompatible
                                                                 : HubErrorCode::TemplateNotFound;
            return HubResult<TemplateCreationPlan>::Failure(
                TemplateError(code,
                              code == HubErrorCode::TemplateNotFound
                                  ? "The requested template version is unavailable."
                                  : "The template is not compatible with the selected editor or platform.",
                              request.TemplateId));
        }

        std::error_code error;
        auto destination = std::filesystem::absolute(request.Destination, error).lexically_normal();
        if (error || destination.filename().empty())
            return HubResult<TemplateCreationPlan>::Failure(TemplateError(HubErrorCode::InvalidArgument,
                                                                          "The project destination is invalid.",
                                                                          request.ProjectName, error.message()));
        const auto parent = std::filesystem::weakly_canonical(destination.parent_path(), error);
        if (error || !std::filesystem::is_directory(parent))
            return HubResult<TemplateCreationPlan>::Failure(
                TemplateError(HubErrorCode::InvalidArgument, "The project destination parent does not exist.",
                              request.ProjectName, error.message()));
        destination = parent / destination.filename();
        for (const auto& forbiddenRoot : request.ForbiddenDestinationRoots)
        {
            const auto canonicalRoot = std::filesystem::weakly_canonical(forbiddenRoot, error);
            if (error || !std::filesystem::is_directory(canonicalRoot))
            {
                return HubResult<TemplateCreationPlan>::Failure(TemplateError(
                    HubErrorCode::InvalidArgument, "A protected project-destination root could not be validated.",
                    request.ProjectName, error.message()));
            }
            if (IsSameOrWithin(canonicalRoot, destination))
            {
                return HubResult<TemplateCreationPlan>::Failure(TemplateError(
                    HubErrorCode::InvalidArgument, "Projects cannot be created inside the installed Hub directory.",
                    request.ProjectName, Detail::PathToUtf8(canonicalRoot)));
            }
        }
        if (std::filesystem::exists(destination, error) || error)
            return HubResult<TemplateCreationPlan>::Failure(TemplateError(HubErrorCode::DestinationConflict,
                                                                          "The project destination already exists.",
                                                                          request.ProjectName, error.message()));

        auto available = m_Services.AvailableSpace(parent);
        if (!available)
            return HubResult<TemplateCreationPlan>::Failure(available.Error());
        PackageResolution packages;
        if (!selected->RequiredPackages.empty())
        {
            auto resolution = PackageResolver{}.Resolve(request.AvailablePackages, selected->RequiredPackages,
                                                        {.Platform = request.HostPlatform,
                                                         .Architecture = request.HostArchitecture,
                                                         .EngineVersion = request.EditorVersion,
                                                         .AvailableDiskBytes = available.Value()});
            if (!resolution)
                return HubResult<TemplateCreationPlan>::Failure(resolution.Error());
            packages = std::move(resolution).Value();
        }

        std::uint64_t payloadBytes = 0;
        for (const auto& file : selected->PayloadFiles)
        {
            if (payloadBytes > std::numeric_limits<std::uint64_t>::max() - file.SizeBytes)
                return HubResult<TemplateCreationPlan>::Failure(
                    TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                  "The template payload size exceeds supported limits.", selected->Id));
            payloadBytes += file.SizeBytes;
        }
        const auto templateBytes = std::max(payloadBytes, selected->EstimatedSizeBytes);
        if (templateBytes > std::numeric_limits<std::uint64_t>::max() - ProjectMetadataReserveBytes ||
            templateBytes + ProjectMetadataReserveBytes >
                std::numeric_limits<std::uint64_t>::max() - packages.RequiredDiskBytes)
        {
            return HubResult<TemplateCreationPlan>::Failure(
                TemplateError(HubErrorCode::TemplatePayloadInvalid,
                              "The template disk estimate exceeds supported limits.", selected->Id));
        }
        const auto requiredBytes = templateBytes + ProjectMetadataReserveBytes + packages.RequiredDiskBytes;
        if (requiredBytes > available.Value())
            return HubResult<TemplateCreationPlan>::Failure(
                TemplateError(HubErrorCode::InsufficientDiskSpace,
                              "There is not enough disk space to create this project.", request.ProjectName));

        return HubResult<TemplateCreationPlan>::Success({.Template = *selected,
                                                         .RequiredPackages = std::move(packages),
                                                         .Destination = std::move(destination),
                                                         .PayloadBytes = payloadBytes,
                                                         .RequiredDiskBytes = requiredBytes});
    }

    HubResult<TemplateCreationResult> TemplateManager::CreateProject(const TemplateCreateRequest& request) const
    {
        auto preflight = Preflight(request);
        if (!preflight)
            return HubResult<TemplateCreationResult>::Failure(preflight.Error());
        if (CancellationRequested(request.CancellationRequested))
            return HubResult<TemplateCreationResult>::Failure(CancellationError(request.ProjectName));
        auto projectId = m_Services.GenerateProjectId();
        if (!projectId)
            return HubResult<TemplateCreationResult>::Failure(projectId.Error());
        if (!IsUuid(projectId.Value(), true))
            return HubResult<TemplateCreationResult>::Failure(TemplateError(
                HubErrorCode::InvalidData, "The generated project identity is invalid.", request.ProjectName));
        auto timestamp = m_Services.CurrentUtcTimestamp();
        if (!timestamp)
            return HubResult<TemplateCreationResult>::Failure(timestamp.Error());
        if (!IsTimestamp(timestamp.Value()))
            return HubResult<TemplateCreationResult>::Failure(TemplateError(
                HubErrorCode::InvalidData, "The generated project timestamp is invalid.", request.ProjectName));

        const auto& plan = preflight.Value();
        const auto staging = plan.Destination.parent_path() / (".keire-stage-" + projectId.Value());
        std::error_code error;
        if (std::filesystem::exists(staging, error) || error)
            return HubResult<TemplateCreationResult>::Failure(
                TemplateError(HubErrorCode::DestinationConflict, "A project staging directory already exists.",
                              request.ProjectName, error.message()));
        if (!std::filesystem::create_directory(staging, error) || error)
            return HubResult<TemplateCreationResult>::Failure(
                TemplateError(HubErrorCode::IoWrite, "The project staging directory could not be created.",
                              request.ProjectName, error.message()));
        StagingGuard cleanup(staging);

        const auto payload = std::filesystem::weakly_canonical(m_Root / plan.Template.PayloadRoot, error);
        if (error)
            return HubResult<TemplateCreationResult>::Failure(
                TemplateError(HubErrorCode::TemplatePayloadInvalid, "The template payload is no longer available.",
                              plan.Template.Id, error.message()));
        for (const auto& file : plan.Template.PayloadFiles)
        {
            const auto declaredSource = m_Root / plan.Template.PayloadRoot / file.Path;
            error.clear();
            if (HasSymlinkComponent(m_Root, plan.Template.PayloadRoot / file.Path, error))
            {
                return HubResult<TemplateCreationResult>::Failure(TemplateError(
                    HubErrorCode::TemplatePayloadInvalid, "A template payload path contains a symbolic link.",
                    plan.Template.Id, error.message()));
            }
            const auto canonicalSource = std::filesystem::weakly_canonical(declaredSource, error);
            if (error || !IsWithin(payload, canonicalSource))
            {
                return HubResult<TemplateCreationResult>::Failure(TemplateError(
                    HubErrorCode::TemplatePayloadInvalid, "A template payload file escapes its declared root.",
                    plan.Template.Id, error.message()));
            }
            if (const auto status = CopyPayloadFile(canonicalSource, staging / file.Path, file, plan.Template.Id,
                                                    request.ProjectName, request.CancellationRequested);
                !status)
                return HubResult<TemplateCreationResult>::Failure(status.Error());
        }

        const auto editorVersion = request.EditorVersion.ToString();
        Detail::Json descriptor{{"schemaVersion", 3},
                                {"id", projectId.Value()},
                                {"name", request.ProjectName},
                                {"createdWithEngineVersion", editorVersion},
                                {"minimumEngineVersion", editorVersion},
                                {"createdAt", timestamp.Value()},
                                {"lastSavedWithEngineVersion", editorVersion},
                                {"template", {{"id", plan.Template.Id}, {"version", plan.Template.Version.ToString()}}},
                                {"startupScene", nullptr},
                                {"defaultInput", nullptr},
                                {"requiredModules", Detail::Json::array()}};
        const auto applyAssetIdentity = [&](const char* configuration, const char* descriptorField) -> HubStatus
        {
            if (const auto value = plan.Template.DefaultProjectConfiguration.find(configuration);
                value != plan.Template.DefaultProjectConfiguration.end())
            {
                if (!IsUuid(value->second, false))
                    return HubStatus::Failure(TemplateError(HubErrorCode::TemplatePayloadInvalid,
                                                            "A template default asset identity is invalid.",
                                                            plan.Template.Id, configuration));
                descriptor[descriptorField] = value->second;
            }
            return HubStatus::Success();
        };
        if (const auto status = applyAssetIdentity("startupScene", "startupScene"); !status)
            return HubResult<TemplateCreationResult>::Failure(status.Error());
        if (const auto status = applyAssetIdentity("defaultInput", "defaultInput"); !status)
            return HubResult<TemplateCreationResult>::Failure(status.Error());
        if (const auto status =
                Detail::WriteJsonFileAtomically(staging / "ProjectSettings" / "Project.keireproject", descriptor);
            !status)
            return HubResult<TemplateCreationResult>::Failure(status.Error());
        if (CancellationRequested(request.CancellationRequested))
            return HubResult<TemplateCreationResult>::Failure(CancellationError(request.ProjectName));

        if (request.ValidateStagedProject)
        {
            try
            {
                if (const auto status = request.ValidateStagedProject(staging); !status)
                {
                    if (status.Error().Code == HubErrorCode::WorkerInterrupted)
                        return HubResult<TemplateCreationResult>::Failure(status.Error());
                    return HubResult<TemplateCreationResult>::Failure(
                        {.Code = HubErrorCode::ProjectValidationFailed,
                         .Message = "The selected editor could not validate the staged project.",
                         .Retryable = status.Error().Retryable,
                         .AffectedItem = request.ProjectName,
                         .TechnicalDetails =
                             std::string(ToString(status.Error().Code)) + ": " + status.Error().TechnicalDetails,
                         .LogReference = status.Error().LogReference});
                }
            }
            catch (const std::exception& callbackError)
            {
                return HubResult<TemplateCreationResult>::Failure(TemplateError(
                    HubErrorCode::ProjectValidationFailed, "The selected editor could not validate the staged project.",
                    request.ProjectName, callbackError.what()));
            }
            catch (...)
            {
                return HubResult<TemplateCreationResult>::Failure(TemplateError(
                    HubErrorCode::ProjectValidationFailed, "The selected editor could not validate the staged project.",
                    request.ProjectName, "The validation callback failed with a non-standard exception."));
            }
        }
        if (CancellationRequested(request.CancellationRequested))
            return HubResult<TemplateCreationResult>::Failure(CancellationError(request.ProjectName));

        std::error_code stagingError;
        const auto stagedStatus = std::filesystem::symlink_status(staging, stagingError);
        std::error_code descriptorError;
        const auto descriptorStatus =
            std::filesystem::symlink_status(staging / "ProjectSettings" / "Project.keireproject", descriptorError);
        if (stagingError || descriptorError || stagedStatus.type() != std::filesystem::file_type::directory ||
            descriptorStatus.type() != std::filesystem::file_type::regular)
        {
            return HubResult<TemplateCreationResult>::Failure(TemplateError(
                HubErrorCode::ProjectValidationFailed, "Project validation left the staging directory invalid.",
                request.ProjectName, stagingError ? stagingError.message() : descriptorError.message()));
        }
        if (std::filesystem::exists(plan.Destination, error) || error)
            return HubResult<TemplateCreationResult>::Failure(TemplateError(
                HubErrorCode::DestinationConflict, "The project destination was created by another operation.",
                request.ProjectName, error.message()));
        if (!Detail::TryRenamePathWithRetry(staging, plan.Destination, error))
            return HubResult<TemplateCreationResult>::Failure(
                TemplateError(HubErrorCode::IoWrite, "The staged project could not be published.", request.ProjectName,
                              error.message()));
        cleanup.Release();
        return HubResult<TemplateCreationResult>::Success({.ProjectId = std::move(projectId).Value(),
                                                           .Root = plan.Destination,
                                                           .TemplateId = plan.Template.Id,
                                                           .TemplateVersion = plan.Template.Version,
                                                           .RequiredPackages = plan.RequiredPackages});
    }

    std::shared_ptr<const std::vector<HubTemplateManifest>> TemplateManager::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    const std::filesystem::path& TemplateManager::Root() const noexcept { return m_Root; }
} // namespace KeireHub
