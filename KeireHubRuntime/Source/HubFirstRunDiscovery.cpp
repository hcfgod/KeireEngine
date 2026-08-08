#include "KeireHubRuntime/HubFirstRunDiscovery.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/EditorInstallationManifest.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumDescriptorBytes = std::size_t{1024U} * 1024U;
        constexpr std::size_t MaximumDescriptorDepth = 32;
        constexpr std::size_t HardMaximumRoots = 32;
        constexpr std::size_t HardMaximumDepth = 16;
        constexpr std::size_t HardMaximumEntries = 1'000'000;
        constexpr std::size_t HardMaximumCandidates = 4096;
        constexpr std::size_t HardMaximumAncestryLevels = 32;

        enum class CandidateKind
        {
            Project,
            Editor
        };

        struct PendingDirectory final
        {
            std::filesystem::path Path;
            std::size_t Depth = 0;
        };

        struct PreparedRequest final
        {
            std::vector<std::filesystem::path> ProjectRoots;
            std::vector<std::filesystem::path> EditorRoots;
            std::optional<std::filesystem::path> Ancestry;
            std::string HostPlatform;
            std::string HostArchitecture;
        };

        [[nodiscard]] HubError DiscoveryError(const HubErrorCode code, std::string message, std::string item,
                                              std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string PortablePathKey(const std::filesystem::path& path)
        {
            auto result = Detail::PathToUtf8(path.lexically_normal());
            std::ranges::transform(result, result.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
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

        [[nodiscard]] bool ContainsSymlinkComponent(const std::filesystem::path& absolutePath)
        {
            auto current = absolutePath.root_path();
            for (const auto& component : absolutePath.relative_path())
            {
                current /= component;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(current, error);
                if (error || std::filesystem::is_symlink(status))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsSameOrWithin(const std::filesystem::path& root, const std::filesystem::path& value)
        {
            if (root == value)
                return true;
            return Detail::IsSafeRelativePath(value.lexically_relative(root));
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
            std::string result;
            result.reserve(32);
            for (const auto character : value)
            {
                if (character != '-')
                    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return result;
        }

        [[nodiscard]] bool IsProjectName(const std::string_view name) noexcept
        {
            if (name.empty() || name.size() > 128 || name == "." || name == ".." ||
                name.find_first_of("<>:\"/\\|?*\r\n\t") != std::string_view::npos)
                return false;
            return std::isspace(static_cast<unsigned char>(name.front())) == 0 &&
                   std::isspace(static_cast<unsigned char>(name.back())) == 0;
        }

        [[nodiscard]] bool ReadOptionalString(const Detail::Json& document, const std::string_view name,
                                              std::string& output)
        {
            const auto found = document.find(std::string(name));
            if (found == document.end())
                return true;
            if (!found->is_string())
                return false;
            output = found->get<std::string>();
            return output.size() <= 128;
        }

        [[nodiscard]] bool IsConfinedRegularFile(const std::filesystem::path& root,
                                                 const std::filesystem::path& relative);

        [[nodiscard]] std::optional<HubFirstRunProjectCandidate> InspectProject(const std::filesystem::path& root)
        {
            const std::filesystem::path descriptorRelative = "ProjectSettings/Project.keireproject";
            const auto descriptorPath = root / descriptorRelative;
            if (!IsConfinedRegularFile(root, descriptorRelative))
                return std::nullopt;
            auto text = Detail::ReadTextFile(descriptorPath, MaximumDescriptorBytes);
            if (!text)
                return std::nullopt;
            auto document =
                Detail::ParseStrictJson(text.Value(), MaximumDescriptorDepth, HubErrorCode::ProjectValidationFailed,
                                        "The project descriptor is malformed.");
            if (!document || !document.Value().is_object())
                return std::nullopt;
            try
            {
                const auto schema = document.Value().at("schemaVersion").get<std::uint32_t>();
                const auto id = document.Value().at("id").get<std::string>();
                const auto name = document.Value().at("name").get<std::string>();
                HubFirstRunProjectCandidate result{
                    .Id = id, .Name = name, .Root = root, .DescriptorPath = descriptorPath, .SchemaVersion = schema};
                if (schema == 0 || !IsProjectId(id) || !IsProjectName(name) ||
                    !ReadOptionalString(document.Value(), "createdWithEngineVersion",
                                        result.CreatedWithEngineVersion) ||
                    !ReadOptionalString(document.Value(), "lastSavedWithEngineVersion",
                                        result.LastSavedWithEngineVersion) ||
                    !ReadOptionalString(document.Value(), "minimumEngineVersion", result.MinimumEngineVersion))
                {
                    return std::nullopt;
                }
                return result;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] bool IsConfinedRegularFile(const std::filesystem::path& root,
                                                 const std::filesystem::path& relative)
        {
            if (!Detail::IsSafeRelativePath(relative))
                return false;
            auto current = root;
            for (const auto& component : relative)
            {
                current /= component;
                std::error_code error;
                const auto status = std::filesystem::symlink_status(current, error);
                if (error || std::filesystem::is_symlink(status))
                    return false;
            }
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(root / relative, error);
            return !error && IsSameOrWithin(root, canonical) && std::filesystem::is_regular_file(canonical, error) &&
                   !error;
        }

        [[nodiscard]] std::optional<HubFirstRunEditorCandidate> InspectEditor(const std::filesystem::path& root,
                                                                              const std::string_view hostPlatform,
                                                                              const std::string_view hostArchitecture)
        {
            auto manifest = Detail::ReadEditorPackageManifest(root);
            if (!manifest || manifest.Value().Platform != hostPlatform ||
                manifest.Value().Architecture != hostArchitecture)
            {
                return std::nullopt;
            }
            for (const auto& entrypoint : manifest.Value().Entrypoints)
            {
                if (!IsConfinedRegularFile(root, entrypoint))
                    return std::nullopt;
            }
            auto entrypoints = manifest.Value().Entrypoints;
            std::ranges::sort(entrypoints, PathLess);
            return HubFirstRunEditorCandidate{.Root = root,
                                              .Version = manifest.Value().Version,
                                              .Channel = manifest.Value().Channel,
                                              .Platform = manifest.Value().Platform,
                                              .Architecture = manifest.Value().Architecture,
                                              .ManifestFingerprint = manifest.Value().Fingerprint,
                                              .Entrypoints = std::move(entrypoints)};
        }

        [[nodiscard]] HubResult<std::filesystem::path> PreparePath(const std::filesystem::path& requested,
                                                                   const bool directoryOnly)
        {
            if (requested.empty() || !requested.is_absolute() || ContainsTraversal(requested) ||
                IsNetworkPath(requested) || IsFilesystemRoot(requested))
            {
                return HubResult<std::filesystem::path>::Failure(
                    DiscoveryError(HubErrorCode::InvalidArgument,
                                   "A discovery root is broad, relative, remote, or contains traversal.",
                                   Detail::PathToUtf8(requested)));
            }
            std::error_code error;
            const auto status = std::filesystem::symlink_status(requested, error);
            const bool expectedType =
                directoryOnly ? std::filesystem::is_directory(status)
                              : std::filesystem::is_directory(status) || std::filesystem::is_regular_file(status);
            if (error || !expectedType || std::filesystem::is_symlink(status) || ContainsSymlinkComponent(requested))
            {
                return HubResult<std::filesystem::path>::Failure(
                    DiscoveryError(HubErrorCode::InvalidArgument,
                                   "A discovery root is missing, inaccessible, or traverses a symbolic link.",
                                   Detail::PathToUtf8(requested), error.message()));
            }
            auto canonical = std::filesystem::weakly_canonical(requested, error);
            if (error || canonical.empty() || IsFilesystemRoot(canonical) || IsNetworkPath(canonical))
            {
                return HubResult<std::filesystem::path>::Failure(
                    DiscoveryError(HubErrorCode::InvalidArgument, "A discovery root could not be confined.",
                                   Detail::PathToUtf8(requested), error.message()));
            }
            return HubResult<std::filesystem::path>::Success(std::move(canonical));
        }

        [[nodiscard]] HubResult<std::vector<std::filesystem::path>>
        PrepareRoots(const std::vector<std::filesystem::path>& requested)
        {
            std::map<std::string, std::filesystem::path, std::less<>> unique;
            for (const auto& value : requested)
            {
                auto prepared = PreparePath(value, true);
                if (!prepared)
                    return HubResult<std::vector<std::filesystem::path>>::Failure(prepared.Error());
                const auto key = PortablePathKey(prepared.Value());
                const auto found = unique.find(key);
                if (found == unique.end() || PathLess(prepared.Value(), found->second))
                    unique.insert_or_assign(key, std::move(prepared).Value());
            }
            std::vector<std::filesystem::path> result;
            result.reserve(unique.size());
            for (auto& [key, path] : unique)
            {
                (void)key;
                result.push_back(std::move(path));
            }
            return HubResult<std::vector<std::filesystem::path>>::Success(std::move(result));
        }

        [[nodiscard]] HubStatus ValidateLimits(const HubFirstRunDiscoveryRequest& request)
        {
            const auto& limits = request.Limits;
            const auto roots = request.ProjectRoots.size() + request.EditorRoots.size() +
                               static_cast<std::size_t>(request.PackagedOrCombinedAncestry.has_value());
            if (limits.MaximumRoots == 0 || limits.MaximumRoots > HardMaximumRoots ||
                limits.MaximumDepth > HardMaximumDepth || limits.MaximumEntries == 0 ||
                limits.MaximumEntries > HardMaximumEntries || limits.MaximumCandidates == 0 ||
                limits.MaximumCandidates > HardMaximumCandidates || limits.MaximumAncestryLevels == 0 ||
                limits.MaximumAncestryLevels > HardMaximumAncestryLevels || roots > limits.MaximumRoots)
            {
                return HubStatus::Failure(DiscoveryError(HubErrorCode::InvalidArgument,
                                                         "The first-run discovery limits are invalid.",
                                                         "first-run-discovery"));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubResult<PreparedRequest> PrepareRequest(const HubFirstRunDiscoveryRequest& request)
        {
            if (const auto limits = ValidateLimits(request); !limits)
                return HubResult<PreparedRequest>::Failure(limits.Error());
            const auto platform = Detail::CanonicalEditorPlatform(request.HostPlatform);
            const auto architecture = Detail::CanonicalEditorArchitecture(request.HostArchitecture);
            if ((platform != "windows" && platform != "linux" && platform != "macos") ||
                (architecture != "x86_64" && architecture != "arm64"))
            {
                return HubResult<PreparedRequest>::Failure(
                    DiscoveryError(HubErrorCode::InvalidArgument, "The discovery host identity is invalid.",
                                   request.HostPlatform + "/" + request.HostArchitecture));
            }
            auto projects = PrepareRoots(request.ProjectRoots);
            if (!projects)
                return HubResult<PreparedRequest>::Failure(projects.Error());
            auto editors = PrepareRoots(request.EditorRoots);
            if (!editors)
                return HubResult<PreparedRequest>::Failure(editors.Error());
            std::optional<std::filesystem::path> ancestry;
            if (request.PackagedOrCombinedAncestry)
            {
                auto prepared = PreparePath(*request.PackagedOrCombinedAncestry, false);
                if (!prepared)
                    return HubResult<PreparedRequest>::Failure(prepared.Error());
                ancestry = std::move(prepared).Value();
            }
            return HubResult<PreparedRequest>::Success({.ProjectRoots = std::move(projects).Value(),
                                                        .EditorRoots = std::move(editors).Value(),
                                                        .Ancestry = std::move(ancestry),
                                                        .HostPlatform = platform,
                                                        .HostArchitecture = architecture});
        }

        class DiscoverySession final
        {
          public:
            DiscoverySession(const HubFirstRunDiscoveryRequest& request, PreparedRequest prepared,
                             HubFirstRunDiscoveryHooks hooks)
                : m_Request(request), m_Prepared(std::move(prepared)), m_Hooks(std::move(hooks))
            {
                m_Progress.TotalRoots = m_Prepared.ProjectRoots.size() + m_Prepared.EditorRoots.size() +
                                        static_cast<std::size_t>(m_Prepared.Ancestry.has_value());
            }

            [[nodiscard]] HubResult<HubFirstRunDiscoverySnapshot> Run()
            {
                Report(HubFirstRunDiscoveryPhase::Validating, {});
                if (Cancelled())
                    return Finish(true);
                if (const auto projects = ScanRoots(m_Prepared.ProjectRoots, CandidateKind::Project); !projects)
                    return HubResult<HubFirstRunDiscoverySnapshot>::Failure(projects.Error());
                if (m_Cancelled)
                    return Finish(true);
                if (const auto editors = ScanRoots(m_Prepared.EditorRoots, CandidateKind::Editor); !editors)
                    return HubResult<HubFirstRunDiscoverySnapshot>::Failure(editors.Error());
                if (m_Cancelled)
                    return Finish(true);
                if (m_Prepared.Ancestry)
                {
                    if (const auto ancestry = ScanAncestry(*m_Prepared.Ancestry); !ancestry)
                        return HubResult<HubFirstRunDiscoverySnapshot>::Failure(ancestry.Error());
                }
                return Finish(m_Cancelled);
            }

          private:
            [[nodiscard]] bool Cancelled()
            {
                if (!m_Hooks.IsCancelled || !m_Hooks.IsCancelled())
                    return false;
                m_Cancelled = true;
                return true;
            }

            void Report(const HubFirstRunDiscoveryPhase phase, const std::filesystem::path& path)
            {
                m_Progress.Phase = phase;
                m_Progress.EntriesVisited = m_EntriesVisited;
                m_Progress.ProjectsFound = m_Projects.size();
                m_Progress.EditorsFound = m_Editors.size();
                m_Progress.CurrentPath = path;
                if (m_Hooks.ReportProgress)
                    m_Hooks.ReportProgress(m_Progress);
            }

            [[nodiscard]] HubStatus VisitEntry(const HubFirstRunDiscoveryPhase phase, const std::filesystem::path& path)
            {
                if (m_EntriesVisited == m_Request.Limits.MaximumEntries)
                {
                    return HubStatus::Failure(DiscoveryError(HubErrorCode::InvalidArgument,
                                                             "First-run discovery exceeded its entry budget.",
                                                             Detail::PathToUtf8(path)));
                }
                ++m_EntriesVisited;
                Report(phase, path);
                (void)Cancelled();
                return HubStatus::Success();
            }

            [[nodiscard]] HubStatus AddProject(HubFirstRunProjectCandidate candidate)
            {
                const auto key = ProjectIdKey(candidate.Id);
                const auto found = m_Projects.find(key);
                if (found == m_Projects.end())
                {
                    if (m_Projects.size() + m_Editors.size() == m_Request.Limits.MaximumCandidates)
                    {
                        return HubStatus::Failure(DiscoveryError(HubErrorCode::InvalidArgument,
                                                                 "First-run discovery exceeded its candidate budget.",
                                                                 Detail::PathToUtf8(candidate.Root)));
                    }
                    m_Projects.emplace(key, std::move(candidate));
                }
                else if (PathLess(candidate.Root, found->second.Root))
                    found->second = std::move(candidate);
                return HubStatus::Success();
            }

            [[nodiscard]] HubStatus AddEditor(HubFirstRunEditorCandidate&& candidate)
            {
                const auto key = PortablePathKey(candidate.Root);
                if (!m_Editors.contains(key) &&
                    m_Projects.size() + m_Editors.size() == m_Request.Limits.MaximumCandidates)
                {
                    return HubStatus::Failure(DiscoveryError(HubErrorCode::InvalidArgument,
                                                             "First-run discovery exceeded its candidate budget.",
                                                             Detail::PathToUtf8(candidate.Root)));
                }
                m_Editors.try_emplace(key, std::move(candidate));
                return HubStatus::Success();
            }

            [[nodiscard]] HubStatus InspectCandidate(const PendingDirectory& directory, const CandidateKind kind,
                                                     bool& found)
            {
                found = false;
                if (kind == CandidateKind::Project)
                {
                    auto project = InspectProject(directory.Path);
                    if (!project)
                        return HubStatus::Success();
                    found = true;
                    return AddProject(std::move(*project));
                }
                auto editor = InspectEditor(directory.Path, m_Prepared.HostPlatform, m_Prepared.HostArchitecture);
                if (!editor)
                    return HubStatus::Success();
                found = true;
                return AddEditor(std::move(*editor));
            }

            [[nodiscard]] HubStatus ScanRoots(const std::vector<std::filesystem::path>& roots, const CandidateKind kind)
            {
                const auto phase = kind == CandidateKind::Project ? HubFirstRunDiscoveryPhase::Projects
                                                                  : HubFirstRunDiscoveryPhase::Editors;
                for (const auto& root : roots)
                {
                    Report(phase, root);
                    if (Cancelled())
                        return HubStatus::Success();
                    if (const auto scan = ScanRoot(root, kind, phase); !scan)
                        return scan;
                    if (m_Cancelled)
                        return HubStatus::Success();
                    ++m_Progress.RootsCompleted;
                    Report(phase, root);
                }
                return HubStatus::Success();
            }

            [[nodiscard]] HubStatus ScanRoot(const std::filesystem::path& root, const CandidateKind kind,
                                             const HubFirstRunDiscoveryPhase phase)
            {
                std::vector<PendingDirectory> pending{{root, 0}};
                while (!pending.empty())
                {
                    if (Cancelled())
                        return HubStatus::Success();
                    auto current = std::move(pending.back());
                    pending.pop_back();
                    bool found = false;
                    if (const auto inspected = InspectCandidate(current, kind, found); !inspected)
                        return inspected;
                    Report(phase, current.Path);
                    if (found || current.Depth == m_Request.Limits.MaximumDepth)
                        continue;

                    std::error_code error;
                    std::vector<std::filesystem::directory_entry> children;
                    std::filesystem::directory_iterator iterator(
                        current.Path, std::filesystem::directory_options::skip_permission_denied, error);
                    const std::filesystem::directory_iterator end;
                    while (!error && iterator != end)
                    {
                        if (const auto visited = VisitEntry(phase, iterator->path()); !visited)
                            return visited;
                        if (m_Cancelled)
                            return HubStatus::Success();
                        children.push_back(*iterator);
                        iterator.increment(error);
                    }
                    if (error && error != std::errc::permission_denied)
                    {
                        return HubStatus::Failure(DiscoveryError(HubErrorCode::IoRead,
                                                                 "A discovery folder could not be enumerated.",
                                                                 Detail::PathToUtf8(current.Path), error.message()));
                    }
                    std::ranges::sort(children, [](const auto& left, const auto& right)
                                      { return PathLess(left.path(), right.path()); });
                    for (auto child = children.rbegin(); child != children.rend(); ++child)
                    {
                        const auto status = child->symlink_status(error);
                        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                        {
                            error.clear();
                            continue;
                        }
                        auto canonical = std::filesystem::weakly_canonical(child->path(), error);
                        if (error || !IsSameOrWithin(root, canonical))
                        {
                            error.clear();
                            continue;
                        }
                        pending.push_back({std::move(canonical), current.Depth + 1});
                    }
                }
                return HubStatus::Success();
            }

            [[nodiscard]] HubStatus ScanAncestry(std::filesystem::path path)
            {
                Report(HubFirstRunDiscoveryPhase::PackagedAncestry, path);
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error) && !error)
                    path = path.parent_path();
                for (std::size_t level = 0;
                     level < m_Request.Limits.MaximumAncestryLevels && !path.empty() && !IsFilesystemRoot(path);
                     ++level)
                {
                    if (const auto visited = VisitEntry(HubFirstRunDiscoveryPhase::PackagedAncestry, path); !visited)
                        return visited;
                    if (m_Cancelled)
                        return HubStatus::Success();
                    if (auto editor = InspectEditor(path, m_Prepared.HostPlatform, m_Prepared.HostArchitecture))
                    {
                        if (const auto added = AddEditor(std::move(*editor)); !added)
                            return added;
                        break;
                    }
                    const auto parent = path.parent_path();
                    if (parent == path)
                        break;
                    path = parent;
                }
                ++m_Progress.RootsCompleted;
                Report(HubFirstRunDiscoveryPhase::PackagedAncestry, path);
                return HubStatus::Success();
            }

            [[nodiscard]] HubResult<HubFirstRunDiscoverySnapshot> Finish(const bool cancelled)
            {
                HubFirstRunDiscoverySnapshot result{.State = cancelled ? HubFirstRunDiscoveryState::Cancelled
                                                                       : HubFirstRunDiscoveryState::Completed,
                                                    .EntriesVisited = m_EntriesVisited};
                result.Projects.reserve(m_Projects.size());
                for (auto& [key, project] : m_Projects)
                {
                    (void)key;
                    result.Projects.push_back(std::move(project));
                }
                std::ranges::sort(result.Projects,
                                  [](const auto& left, const auto& right)
                                  {
                                      if (PathLess(left.Root, right.Root))
                                          return true;
                                      if (PathLess(right.Root, left.Root))
                                          return false;
                                      return left.Id < right.Id;
                                  });
                result.Editors.reserve(m_Editors.size());
                for (auto& [key, editor] : m_Editors)
                {
                    (void)key;
                    result.Editors.push_back(std::move(editor));
                }
                Report(cancelled ? HubFirstRunDiscoveryPhase::Cancelled : HubFirstRunDiscoveryPhase::Completed, {});
                return HubResult<HubFirstRunDiscoverySnapshot>::Success(std::move(result));
            }

            const HubFirstRunDiscoveryRequest& m_Request;
            PreparedRequest m_Prepared;
            HubFirstRunDiscoveryHooks m_Hooks;
            HubFirstRunDiscoveryProgress m_Progress;
            std::map<std::string, HubFirstRunProjectCandidate, std::less<>> m_Projects;
            std::map<std::string, HubFirstRunEditorCandidate, std::less<>> m_Editors;
            std::size_t m_EntriesVisited = 0;
            bool m_Cancelled = false;
        };
    } // namespace

    HubFirstRunDiscovery::HubFirstRunDiscovery() : m_Snapshot(std::make_shared<const HubFirstRunDiscoverySnapshot>()) {}

    HubStatus HubFirstRunDiscovery::Discover(const HubFirstRunDiscoveryRequest& request,
                                             HubFirstRunDiscoveryHooks hooks)
    {
        try
        {
            auto prepared = PrepareRequest(request);
            if (!prepared)
                return HubStatus::Failure(prepared.Error());
            DiscoverySession session(request, std::move(prepared).Value(), std::move(hooks));
            auto result = session.Run();
            if (!result)
                return HubStatus::Failure(result.Error());
            m_Snapshot = std::make_shared<const HubFirstRunDiscoverySnapshot>(std::move(result).Value());
            return HubStatus::Success();
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(DiscoveryError(HubErrorCode::InvalidData,
                                                     "First-run discovery stopped unexpectedly.", "first-run-discovery",
                                                     error.what()));
        }
    }

    std::shared_ptr<const HubFirstRunDiscoverySnapshot> HubFirstRunDiscovery::Snapshot() const noexcept
    {
        return m_Snapshot;
    }
} // namespace KeireHub
