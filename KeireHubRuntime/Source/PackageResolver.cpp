#include "KeireHubRuntime/PackageResolver.h"

#include "Persistence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool IsIdentifier(const std::string_view value, const bool rejectNumericLeadingZero) noexcept
        {
            if (value.empty())
                return false;
            const bool numeric =
                std::ranges::all_of(value, [](const unsigned char character) { return std::isdigit(character) != 0; });
            if (numeric && rejectNumericLeadingZero && value.size() > 1 && value.front() == '0')
                return false;
            return std::ranges::all_of(value,
                                       [](const unsigned char character)
                                       {
                                           return (character >= '0' && character <= '9') ||
                                                  (character >= 'A' && character <= 'Z') ||
                                                  (character >= 'a' && character <= 'z') || character == '-';
                                       });
        }

        [[nodiscard]] HubResult<std::vector<std::string>> ParseIdentifiers(const std::string_view value,
                                                                           const bool rejectNumericLeadingZero)
        {
            std::vector<std::string> result;
            std::size_t offset = 0;
            while (offset <= value.size())
            {
                const auto end = value.find('.', offset);
                const auto identifier =
                    value.substr(offset, end == std::string_view::npos ? value.size() - offset : end - offset);
                if (!IsIdentifier(identifier, rejectNumericLeadingZero))
                    return HubResult<std::vector<std::string>>::Failure(
                        {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
                result.emplace_back(identifier);
                if (end == std::string_view::npos)
                    break;
                offset = end + 1;
            }
            return HubResult<std::vector<std::string>>::Success(std::move(result));
        }

        [[nodiscard]] HubResult<std::uint64_t> ParseNumber(const std::string_view value)
        {
            if (value.empty() || (value.size() > 1 && value.front() == '0'))
                return HubResult<std::uint64_t>::Failure(
                    {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
            std::uint64_t result = 0;
            const auto [position, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (error != std::errc{} || position != value.data() + value.size())
                return HubResult<std::uint64_t>::Failure(
                    {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
            return HubResult<std::uint64_t>::Success(result);
        }

        [[nodiscard]] bool IsNumericIdentifier(const std::string& value) noexcept
        {
            return std::ranges::all_of(value,
                                       [](const unsigned char character) { return std::isdigit(character) != 0; });
        }

        [[nodiscard]] std::strong_ordering ComparePrerelease(const std::vector<std::string>& left,
                                                             const std::vector<std::string>& right) noexcept
        {
            if (left.empty() || right.empty())
            {
                if (left.empty() == right.empty())
                    return std::strong_ordering::equal;
                return left.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
            }
            const auto count = std::min(left.size(), right.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto leftNumeric = IsNumericIdentifier(left[index]);
                const auto rightNumeric = IsNumericIdentifier(right[index]);
                if (leftNumeric != rightNumeric)
                    return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
                if (leftNumeric)
                {
                    if (left[index].size() != right[index].size())
                        return left[index].size() <=> right[index].size();
                }
                const auto comparison = left[index] <=> right[index];
                if (comparison != 0)
                    return comparison;
            }
            return left.size() <=> right.size();
        }

        [[nodiscard]] std::string Join(const std::vector<std::string>& values, const char separator)
        {
            std::string result;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index != 0)
                    result += separator;
                result += values[index];
            }
            return result;
        }

        [[nodiscard]] std::string Trim(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            return std::string(value);
        }

        [[nodiscard]] SemanticVersion CaretUpperBound(const SemanticVersion& version)
        {
            if (version.Major != 0)
                return {.Major = version.Major + 1};
            if (version.Minor != 0)
                return {.Minor = version.Minor + 1};
            return {.Patch = version.Patch + 1};
        }

        [[nodiscard]] SemanticVersion TildeUpperBound(const SemanticVersion& version)
        {
            return {.Major = version.Major, .Minor = version.Minor + 1};
        }

        [[nodiscard]] std::string_view ComparisonText(const VersionComparison comparison) noexcept
        {
            switch (comparison)
            {
            case VersionComparison::Equal:
                return "=";
            case VersionComparison::Less:
                return "<";
            case VersionComparison::LessOrEqual:
                return "<=";
            case VersionComparison::Greater:
                return ">";
            case VersionComparison::GreaterOrEqual:
                return ">=";
            }
            return "=";
        }

        [[nodiscard]] std::string_view PackageKindText(const PackageKind kind) noexcept
        {
            constexpr std::array names{"hubInstaller", "editor",          "buildSupport",
                                       "template",     "learningContent", "toolchain"};
            return names[static_cast<std::size_t>(kind)];
        }

        [[nodiscard]] std::optional<PackageKind> ParsePackageKind(const std::string_view value) noexcept
        {
            constexpr std::array values{PackageKind::HubInstaller,    PackageKind::Editor,
                                        PackageKind::BuildSupport,    PackageKind::Template,
                                        PackageKind::LearningContent, PackageKind::Toolchain};
            for (const auto candidate : values)
            {
                if (PackageKindText(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsPortablePackagePath(const std::filesystem::path& path)
        {
            if (!Detail::IsSafeRelativePath(path))
                return false;
            constexpr std::array reserved{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                          "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                          "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
            for (const auto& component : path)
            {
                auto text = Detail::PathToUtf8(component);
                if (text.empty() || text.back() == '.' || text.back() == ' ' ||
                    std::ranges::any_of(text,
                                        [](const unsigned char character)
                                        {
                                            return character < 0x20U || character >= 0x7fU || character == '\\' ||
                                                   character == ':' || character == '"' || character == '<' ||
                                                   character == '>' || character == '|' || character == '?' ||
                                                   character == '*';
                                        }))
                {
                    return false;
                }
                const auto separator = text.find('.');
                if (separator != std::string::npos)
                    text.resize(separator);
                std::ranges::transform(text, text.begin(), [](const unsigned char character)
                                       { return static_cast<char>(std::tolower(character)); });
                if (std::ranges::any_of(reserved,
                                        [&](const std::string_view reservedName) { return reservedName == text; }))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HostMatches(const PackageManifest& package, const PackageHost& host) noexcept
        {
            if (package.Platform != "any" && package.Platform != host.Platform)
                return false;
            if (package.Architecture != "any" && package.Architecture != host.Architecture)
                return false;
            if (package.EngineCompatibility)
                return host.EngineVersion && package.EngineCompatibility->Matches(*host.EngineVersion);
            return true;
        }

        [[nodiscard]] bool Conflicts(const PackageManifest& left, const PackageManifest& right) noexcept
        {
            const auto matches = [](const PackageManifest& source, const PackageManifest& target)
            {
                return std::ranges::any_of(
                    source.Conflicts, [&](const auto& conflict)
                    { return conflict.PackageId == target.Id && conflict.Versions.Matches(target.Version); });
            };
            return matches(left, right) || matches(right, left);
        }

        struct ResolutionState final
        {
            std::map<std::string, std::vector<VersionConstraint>, std::less<>> Constraints;
            std::map<std::string, const PackageManifest*, std::less<>> Selected;
            std::set<std::string, std::less<>> Pending;
        };

        using PackageIndex = std::map<std::string, std::vector<const PackageManifest*>, std::less<>>;

        [[nodiscard]] bool Satisfies(const PackageManifest& package,
                                     const std::vector<VersionConstraint>& constraints) noexcept
        {
            return std::ranges::all_of(constraints,
                                       [&](const auto& constraint) { return constraint.Matches(package.Version); });
        }

        [[nodiscard]] HubResult<ResolutionState> Solve(const PackageIndex& index, const PackageHost& host,
                                                       ResolutionState state)
        {
            if (state.Pending.empty())
                return HubResult<ResolutionState>::Success(std::move(state));
            const auto packageId = *state.Pending.begin();
            state.Pending.erase(state.Pending.begin());
            if (const auto selected = state.Selected.find(packageId); selected != state.Selected.end())
            {
                if (!Satisfies(*selected->second, state.Constraints[packageId]))
                {
                    return HubResult<ResolutionState>::Failure(
                        {.Code = HubErrorCode::PackageVersionUnsatisfied,
                         .Message = "Installed package requirements select incompatible versions.",
                         .AffectedItem = packageId});
                }
                return Solve(index, host, std::move(state));
            }
            const auto available = index.find(packageId);
            if (available == index.end())
            {
                return HubResult<ResolutionState>::Failure({.Code = HubErrorCode::PackageMissingDependency,
                                                            .Message = "A required package is unavailable.",
                                                            .AffectedItem = packageId});
            }

            std::vector<const PackageManifest*> versionMatches;
            for (const auto* candidate : available->second)
            {
                if (Satisfies(*candidate, state.Constraints[packageId]))
                    versionMatches.push_back(candidate);
            }
            if (versionMatches.empty())
            {
                return HubResult<ResolutionState>::Failure({.Code = HubErrorCode::PackageVersionUnsatisfied,
                                                            .Message = "No package version satisfies all requirements.",
                                                            .AffectedItem = packageId});
            }

            HubError lastError{.Code = HubErrorCode::PackageHostIncompatible,
                               .Message = "The package is not compatible with this computer or editor.",
                               .AffectedItem = packageId};
            bool hadHostMatch = false;
            for (const auto* candidate : versionMatches)
            {
                if (!HostMatches(*candidate, host))
                    continue;
                hadHostMatch = true;
                bool conflict = false;
                for (const auto& [otherId, other] : state.Selected)
                {
                    if (Conflicts(*candidate, *other))
                    {
                        conflict = true;
                        lastError = {.Code = HubErrorCode::PackageConflict,
                                     .Message = "Selected packages conflict with one another.",
                                     .AffectedItem = candidate->Id + " / " + otherId};
                        break;
                    }
                }
                if (conflict)
                    continue;

                auto branch = state;
                branch.Selected.emplace(packageId, candidate);
                bool invalidSelectedDependency = false;
                for (const auto& dependency : candidate->Dependencies)
                {
                    branch.Constraints[dependency.PackageId].push_back(dependency.Versions);
                    if (const auto selectedDependency = branch.Selected.find(dependency.PackageId);
                        selectedDependency != branch.Selected.end() &&
                        !Satisfies(*selectedDependency->second, branch.Constraints[dependency.PackageId]))
                    {
                        invalidSelectedDependency = true;
                        break;
                    }
                    branch.Pending.insert(dependency.PackageId);
                }
                if (invalidSelectedDependency)
                {
                    lastError = {.Code = HubErrorCode::PackageVersionUnsatisfied,
                                 .Message = "Package dependencies require incompatible versions.",
                                 .AffectedItem = packageId};
                    continue;
                }
                auto solved = Solve(index, host, std::move(branch));
                if (solved)
                    return solved;
                lastError = solved.Error();
            }
            if (!hadHostMatch)
                return HubResult<ResolutionState>::Failure(std::move(lastError));
            return HubResult<ResolutionState>::Failure(std::move(lastError));
        }
    } // namespace

    HubResult<SemanticVersion> SemanticVersion::Parse(const std::string_view value)
    {
        if (value.empty() || value.size() > 256)
            return HubResult<SemanticVersion>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
        auto coreAndPrerelease = value;
        std::string_view build;
        if (const auto plus = value.find('+'); plus != std::string_view::npos)
        {
            if (value.find('+', plus + 1) != std::string_view::npos)
                return HubResult<SemanticVersion>::Failure(
                    {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
            coreAndPrerelease = value.substr(0, plus);
            build = value.substr(plus + 1);
        }
        auto core = coreAndPrerelease;
        std::string_view prerelease;
        if (const auto dash = coreAndPrerelease.find('-'); dash != std::string_view::npos)
        {
            core = coreAndPrerelease.substr(0, dash);
            prerelease = coreAndPrerelease.substr(dash + 1);
        }
        const auto first = core.find('.');
        const auto second = first == std::string_view::npos ? std::string_view::npos : core.find('.', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos ||
            core.find('.', second + 1) != std::string_view::npos)
        {
            return HubResult<SemanticVersion>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
        }
        auto major = ParseNumber(core.substr(0, first));
        auto minor = ParseNumber(core.substr(first + 1, second - first - 1));
        auto patch = ParseNumber(core.substr(second + 1));
        if (!major || !minor || !patch)
            return HubResult<SemanticVersion>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
        SemanticVersion result{.Major = major.Value(), .Minor = minor.Value(), .Patch = patch.Value()};
        if (!prerelease.empty())
        {
            auto values = ParseIdentifiers(prerelease, true);
            if (!values)
                return HubResult<SemanticVersion>::Failure(values.Error());
            result.Prerelease = std::move(values).Value();
        }
        else if (coreAndPrerelease.ends_with('-'))
            return HubResult<SemanticVersion>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
        if (!build.empty())
        {
            auto values = ParseIdentifiers(build, false);
            if (!values)
                return HubResult<SemanticVersion>::Failure(values.Error());
            result.BuildMetadata = std::move(values).Value();
        }
        else if (value.ends_with('+'))
            return HubResult<SemanticVersion>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The semantic version is invalid."});
        return HubResult<SemanticVersion>::Success(std::move(result));
    }

    std::string SemanticVersion::ToString() const
    {
        auto result = std::to_string(Major) + '.' + std::to_string(Minor) + '.' + std::to_string(Patch);
        if (!Prerelease.empty())
            result += '-' + Join(Prerelease, '.');
        if (!BuildMetadata.empty())
            result += '+' + Join(BuildMetadata, '.');
        return result;
    }

    bool operator==(const SemanticVersion& left, const SemanticVersion& right) noexcept
    {
        return left.Major == right.Major && left.Minor == right.Minor && left.Patch == right.Patch &&
               ComparePrerelease(left.Prerelease, right.Prerelease) == 0;
    }

    std::strong_ordering operator<=>(const SemanticVersion& left, const SemanticVersion& right) noexcept
    {
        if (const auto result = left.Major <=> right.Major; result != 0)
            return result;
        if (const auto result = left.Minor <=> right.Minor; result != 0)
            return result;
        if (const auto result = left.Patch <=> right.Patch; result != 0)
            return result;
        return ComparePrerelease(left.Prerelease, right.Prerelease);
    }

    HubResult<VersionConstraint> VersionConstraint::Parse(const std::string_view value)
    {
        auto normalized = Trim(value);
        if (normalized.empty() || normalized == "*")
            return HubResult<VersionConstraint>::Success(VersionConstraint());
        if (normalized.find("||") != std::string::npos)
            return HubResult<VersionConstraint>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "Alternative version ranges are not supported."});
        std::ranges::replace(normalized, ',', ' ');
        std::istringstream stream(normalized);
        std::vector<std::string> tokens;
        for (std::string token; stream >> token;)
            tokens.push_back(std::move(token));
        std::vector<VersionClause> clauses;
        for (std::size_t index = 0; index < tokens.size(); ++index)
        {
            auto token = tokens[index];
            if ((token == ">" || token == ">=" || token == "<" || token == "<=" || token == "=") &&
                index + 1 < tokens.size())
                token += tokens[++index];
            if (token == "*")
                continue;
            VersionComparison comparison = VersionComparison::Equal;
            std::size_t prefix = 0;
            bool caret = false;
            bool tilde = false;
            if (token.starts_with(">="))
            {
                comparison = VersionComparison::GreaterOrEqual;
                prefix = 2;
            }
            else if (token.starts_with("<="))
            {
                comparison = VersionComparison::LessOrEqual;
                prefix = 2;
            }
            else if (token.starts_with('>'))
            {
                comparison = VersionComparison::Greater;
                prefix = 1;
            }
            else if (token.starts_with('<'))
            {
                comparison = VersionComparison::Less;
                prefix = 1;
            }
            else if (token.starts_with('='))
                prefix = 1;
            else if (token.starts_with('^'))
            {
                caret = true;
                prefix = 1;
            }
            else if (token.starts_with('~'))
            {
                tilde = true;
                prefix = 1;
            }
            auto version = SemanticVersion::Parse(std::string_view(token).substr(prefix));
            if (!version)
                return HubResult<VersionConstraint>::Failure(version.Error());
            if (caret || tilde)
            {
                const bool upperBoundOverflows =
                    (caret && ((version.Value().Major != 0 &&
                                version.Value().Major == std::numeric_limits<std::uint64_t>::max()) ||
                               (version.Value().Major == 0 && version.Value().Minor != 0 &&
                                version.Value().Minor == std::numeric_limits<std::uint64_t>::max()) ||
                               (version.Value().Major == 0 && version.Value().Minor == 0 &&
                                version.Value().Patch == std::numeric_limits<std::uint64_t>::max()))) ||
                    (tilde && version.Value().Minor == std::numeric_limits<std::uint64_t>::max());
                if (upperBoundOverflows)
                {
                    return HubResult<VersionConstraint>::Failure(
                        {.Code = HubErrorCode::InvalidArgument,
                         .Message = "The semantic-version range exceeds supported limits."});
                }
                clauses.push_back({VersionComparison::GreaterOrEqual, version.Value()});
                clauses.push_back({VersionComparison::Less,
                                   caret ? CaretUpperBound(version.Value()) : TildeUpperBound(version.Value())});
            }
            else
                clauses.push_back({comparison, std::move(version).Value()});
        }
        return HubResult<VersionConstraint>::Success(VersionConstraint(std::move(clauses)));
    }

    bool VersionConstraint::Matches(const SemanticVersion& version) const noexcept
    {
        return std::ranges::all_of(m_Clauses,
                                   [&](const auto& clause)
                                   {
                                       const auto comparison = version <=> clause.Version;
                                       switch (clause.Comparison)
                                       {
                                       case VersionComparison::Equal:
                                           return comparison == 0;
                                       case VersionComparison::Less:
                                           return comparison < 0;
                                       case VersionComparison::LessOrEqual:
                                           return comparison <= 0;
                                       case VersionComparison::Greater:
                                           return comparison > 0;
                                       case VersionComparison::GreaterOrEqual:
                                           return comparison >= 0;
                                       }
                                       return false;
                                   });
    }

    std::string VersionConstraint::ToString() const
    {
        if (m_Clauses.empty())
            return "*";
        std::string result;
        for (std::size_t index = 0; index < m_Clauses.size(); ++index)
        {
            if (index != 0)
                result += ' ';
            result += ComparisonText(m_Clauses[index].Comparison);
            result += m_Clauses[index].Version.ToString();
        }
        return result;
    }

    bool VersionConstraint::IsAny() const noexcept { return m_Clauses.empty(); }

    VersionConstraint::VersionConstraint(std::vector<VersionClause> clauses) : m_Clauses(std::move(clauses)) {}

    HubStatus ValidatePackageManifest(const PackageManifest& manifest)
    {
        if (manifest.SchemaVersion != PackageManifest::CurrentSchemaVersion ||
            manifest.Kind < PackageKind::HubInstaller || manifest.Kind > PackageKind::Toolchain ||
            !Detail::IsBoundedIdentifier(manifest.Id) || manifest.DisplayName.empty() ||
            manifest.DisplayName.size() > 256 || !Detail::IsBoundedIdentifier(manifest.Channel, 64) ||
            (manifest.Platform != "any" && manifest.Platform != "windows" && manifest.Platform != "linux" &&
             manifest.Platform != "macos") ||
            (manifest.Architecture != "any" && manifest.Architecture != "x86_64" && manifest.Architecture != "arm64") ||
            manifest.ArtifactSizeBytes == 0 || !Detail::IsSha256(manifest.ArtifactSha256) ||
            manifest.InstalledSizeBytes == 0 || manifest.Files.empty() || manifest.Files.size() > 100000 ||
            !Detail::IsBoundedIdentifier(manifest.SignatureKeyId) || manifest.Dependencies.size() > 128 ||
            manifest.Conflicts.size() > 128 || manifest.LicenseReferences.size() > 128)
        {
            return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                       .Message = "The package manifest is invalid.",
                                       .AffectedItem = manifest.Id});
        }
        std::uint64_t declaredSize = 0;
        std::set<std::string, std::less<>> filePaths;
        std::map<std::string, std::string, std::less<>> directoryPaths;
        for (const auto& file : manifest.Files)
        {
            if (!IsPortablePackagePath(file.Path) || !Detail::IsSha256(file.Sha256) ||
                (file.Mode != 0644U && file.Mode != 0755U) || file.SizeBytes > manifest.InstalledSizeBytes ||
                declaredSize > std::numeric_limits<std::uint64_t>::max() - file.SizeBytes)
            {
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "The package file inventory is unsafe or inconsistent.",
                                           .AffectedItem = manifest.Id});
            }
            declaredSize += file.SizeBytes;
            std::string originalPrefix;
            std::string foldedPrefix;
            bool collision = false;
            auto component = file.Path.begin();
            while (component != file.Path.end())
            {
                auto original = Detail::PathToUtf8(*component);
                auto folded = original;
                std::ranges::transform(folded, folded.begin(), [](const unsigned char value)
                                       { return static_cast<char>(std::tolower(value)); });
                if (!originalPrefix.empty())
                {
                    originalPrefix += '/';
                    foldedPrefix += '/';
                }
                originalPrefix += original;
                foldedPrefix += folded;
                const bool final = std::next(component) == file.Path.end();
                if (final)
                {
                    collision = !filePaths.insert(foldedPrefix).second || directoryPaths.contains(foldedPrefix);
                }
                else
                {
                    const auto [existing, inserted] = directoryPaths.emplace(foldedPrefix, originalPrefix);
                    collision = filePaths.contains(foldedPrefix) || (!inserted && existing->second != originalPrefix);
                }
                if (collision)
                    break;
                ++component;
            }
            if (collision)
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "The package contains colliding file paths.",
                                           .AffectedItem = manifest.Id});
        }
        if (declaredSize != manifest.InstalledSizeBytes)
            return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                       .Message = "The package installed size does not match its file inventory.",
                                       .AffectedItem = manifest.Id});
        std::set<std::string, std::less<>> dependencyIds;
        for (const auto& dependency : manifest.Dependencies)
        {
            if (!Detail::IsBoundedIdentifier(dependency.PackageId) || dependency.PackageId == manifest.Id ||
                !dependencyIds.insert(dependency.PackageId).second)
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "The package dependency list is invalid.",
                                           .AffectedItem = manifest.Id});
        }
        std::set<std::string, std::less<>> conflictIds;
        for (const auto& conflict : manifest.Conflicts)
        {
            if (!Detail::IsBoundedIdentifier(conflict.PackageId) || conflict.PackageId == manifest.Id ||
                !conflictIds.insert(conflict.PackageId).second)
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "The package conflict list is invalid.",
                                           .AffectedItem = manifest.Id});
        }
        for (const auto& license : manifest.LicenseReferences)
        {
            if (!IsPortablePackagePath(Detail::PathFromUtf8(license)))
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "The package license reference is unsafe.",
                                           .AffectedItem = manifest.Id});
        }
        return HubStatus::Success();
    }

    HubResult<std::string> EncodePackageManifest(const PackageManifest& manifest)
    {
        if (const auto status = ValidatePackageManifest(manifest); !status)
            return HubResult<std::string>::Failure(status.Error());
        try
        {
            Detail::Json document{
                {"schemaVersion", manifest.SchemaVersion},
                {"packageId", manifest.Id},
                {"version", manifest.Version.ToString()},
                {"type", PackageKindText(manifest.Kind)},
                {"displayName", manifest.DisplayName},
                {"channel", manifest.Channel},
                {"platform", manifest.Platform},
                {"architecture", manifest.Architecture},
                {"artifact", {{"sizeBytes", manifest.ArtifactSizeBytes}, {"sha256", manifest.ArtifactSha256}}},
                {"installedSizeBytes", manifest.InstalledSizeBytes},
                {"files", Detail::Json::array()},
                {"signatureKeyId", manifest.SignatureKeyId}};
            if (manifest.EngineCompatibility)
                document["engineCompatibility"] = manifest.EngineCompatibility->ToString();
            if (!manifest.Dependencies.empty())
            {
                document["dependencies"] = Detail::Json::array();
                for (const auto& dependency : manifest.Dependencies)
                {
                    document["dependencies"].push_back(
                        {{"packageId", dependency.PackageId}, {"version", dependency.Versions.ToString()}});
                }
            }
            if (!manifest.Conflicts.empty())
            {
                document["conflicts"] = Detail::Json::array();
                for (const auto& conflict : manifest.Conflicts)
                {
                    document["conflicts"].push_back(
                        {{"packageId", conflict.PackageId}, {"version", conflict.Versions.ToString()}});
                }
            }
            for (const auto& file : manifest.Files)
            {
                document["files"].push_back({{"path", Detail::PathToUtf8(file.Path)},
                                             {"sizeBytes", file.SizeBytes},
                                             {"sha256", file.Sha256},
                                             {"mode", file.Mode}});
            }
            if (!manifest.LicenseReferences.empty())
                document["licenses"] = manifest.LicenseReferences;
            return HubResult<std::string>::Success(document.dump());
        }
        catch (const std::exception& error)
        {
            return HubResult<std::string>::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                                    .Message = "The package manifest could not be encoded.",
                                                    .AffectedItem = manifest.Id,
                                                    .TechnicalDetails = error.what()});
        }
    }

    HubResult<PackageManifest> ParsePackageManifest(const std::string_view document)
    {
        if (document.size() > 8 * 1024 * 1024)
            return HubResult<PackageManifest>::Failure(
                {.Code = HubErrorCode::PackageManifestInvalid, .Message = "The package manifest is too large."});
        try
        {
            const auto json = Detail::Json::parse(document);
            PackageManifest result;
            result.SchemaVersion = json.at("schemaVersion").get<std::uint32_t>();
            if (result.SchemaVersion != PackageManifest::CurrentSchemaVersion)
                return HubResult<PackageManifest>::Failure(
                    {.Code = HubErrorCode::UnsupportedSchema,
                     .Message = "This package manifest uses an unsupported schema."});
            result.Id = json.at("packageId").get<std::string>();
            auto version = SemanticVersion::Parse(json.at("version").get<std::string>());
            if (!version)
                throw std::invalid_argument(version.Error().Message);
            result.Version = std::move(version).Value();
            const auto kind = ParsePackageKind(json.at("type").get<std::string>());
            if (!kind)
                throw std::invalid_argument("Unknown package type.");
            result.Kind = *kind;
            result.DisplayName = json.at("displayName").get<std::string>();
            result.Channel = json.at("channel").get<std::string>();
            result.Platform = json.at("platform").get<std::string>();
            result.Architecture = json.at("architecture").get<std::string>();
            if (json.contains("engineCompatibility"))
            {
                auto constraint = VersionConstraint::Parse(json.at("engineCompatibility").get<std::string>());
                if (!constraint)
                    throw std::invalid_argument(constraint.Error().Message);
                result.EngineCompatibility = std::move(constraint).Value();
            }
            if (json.contains("dependencies"))
            {
                for (const auto& value : json.at("dependencies"))
                {
                    auto constraint = VersionConstraint::Parse(value.value("version", "*"));
                    if (!constraint)
                        throw std::invalid_argument(constraint.Error().Message);
                    result.Dependencies.push_back(
                        {value.at("packageId").get<std::string>(), std::move(constraint).Value()});
                }
            }
            if (json.contains("conflicts"))
            {
                for (const auto& value : json.at("conflicts"))
                {
                    auto constraint = VersionConstraint::Parse(value.value("version", "*"));
                    if (!constraint)
                        throw std::invalid_argument(constraint.Error().Message);
                    result.Conflicts.push_back(
                        {value.at("packageId").get<std::string>(), std::move(constraint).Value()});
                }
            }
            const auto& artifact = json.at("artifact");
            result.ArtifactSizeBytes = artifact.at("sizeBytes").get<std::uint64_t>();
            result.ArtifactSha256 = artifact.at("sha256").get<std::string>();
            result.InstalledSizeBytes = json.at("installedSizeBytes").get<std::uint64_t>();
            for (const auto& value : json.at("files"))
                result.Files.push_back({.Path = Detail::PathFromUtf8(value.at("path").get<std::string>()),
                                        .SizeBytes = value.at("sizeBytes").get<std::uint64_t>(),
                                        .Sha256 = value.at("sha256").get<std::string>(),
                                        .Mode = value.value("mode", 0644U)});
            result.LicenseReferences = json.value("licenses", std::vector<std::string>{});
            result.SignatureKeyId = json.at("signatureKeyId").get<std::string>();
            if (const auto status = ValidatePackageManifest(result); !status)
                return HubResult<PackageManifest>::Failure(status.Error());
            return HubResult<PackageManifest>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageManifest>::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                                        .Message = "The package manifest is malformed.",
                                                        .TechnicalDetails = error.what()});
        }
    }

    HubResult<PackageResolution> PackageResolver::Resolve(const std::vector<PackageManifest>& available,
                                                          const std::vector<PackageRequirement>& requested,
                                                          const PackageHost& host) const
    {
        if ((host.Platform != "windows" && host.Platform != "linux" && host.Platform != "macos") ||
            (host.Architecture != "x86_64" && host.Architecture != "arm64") || requested.empty())
            return HubResult<PackageResolution>::Failure(
                {.Code = HubErrorCode::InvalidArgument, .Message = "The package resolution request is invalid."});
        PackageIndex index;
        std::set<std::pair<std::string, SemanticVersion>> identities;
        for (const auto& package : available)
        {
            if (const auto status = ValidatePackageManifest(package); !status)
                return HubResult<PackageResolution>::Failure(status.Error());
            if (!identities.emplace(package.Id, package.Version).second)
                return HubResult<PackageResolution>::Failure(
                    {.Code = HubErrorCode::DuplicateIdentifier,
                     .Message = "The package catalog contains a duplicate version.",
                     .AffectedItem = package.Id});
            index[package.Id].push_back(&package);
        }
        for (auto& [id, packages] : index)
        {
            std::ranges::sort(packages,
                              [](const auto* left, const auto* right) { return left->Version > right->Version; });
        }

        ResolutionState initial;
        for (const auto& requirement : requested)
        {
            if (!Detail::IsBoundedIdentifier(requirement.PackageId))
                return HubResult<PackageResolution>::Failure(
                    {.Code = HubErrorCode::InvalidArgument, .Message = "A requested package identity is invalid."});
            initial.Constraints[requirement.PackageId].push_back(requirement.Versions);
            initial.Pending.insert(requirement.PackageId);
        }
        auto solved = Solve(index, host, std::move(initial));
        if (!solved)
            return HubResult<PackageResolution>::Failure(solved.Error());

        std::map<std::string, int, std::less<>> marks;
        std::vector<const PackageManifest*> ordered;
        std::function<HubStatus(const std::string&)> visit = [&](const std::string& id) -> HubStatus
        {
            if (marks[id] == 2)
                return HubStatus::Success();
            if (marks[id] == 1)
                return HubStatus::Failure({.Code = HubErrorCode::PackageDependencyCycle,
                                           .Message = "The selected packages contain a dependency cycle.",
                                           .AffectedItem = id});
            marks[id] = 1;
            const auto* package = solved.Value().Selected.at(id);
            std::vector<std::string> dependencies;
            for (const auto& dependency : package->Dependencies)
                dependencies.push_back(dependency.PackageId);
            std::ranges::sort(dependencies);
            for (const auto& dependency : dependencies)
            {
                if (const auto status = visit(dependency); !status)
                    return status;
            }
            marks[id] = 2;
            ordered.push_back(package);
            return HubStatus::Success();
        };
        for (const auto& [id, package] : solved.Value().Selected)
        {
            if (const auto status = visit(id); !status)
                return HubResult<PackageResolution>::Failure(status.Error());
        }

        PackageResolution resolution;
        for (const auto* package : ordered)
        {
            if (resolution.RequiredDiskBytes > std::numeric_limits<std::uint64_t>::max() - package->InstalledSizeBytes)
                return HubResult<PackageResolution>::Failure({.Code = HubErrorCode::InsufficientDiskSpace,
                                                              .Message = "The package size exceeds supported limits."});
            resolution.RequiredDiskBytes += package->InstalledSizeBytes;
            resolution.InstallOrder.push_back(*package);
        }
        if (host.AvailableDiskBytes && resolution.RequiredDiskBytes > *host.AvailableDiskBytes)
            return HubResult<PackageResolution>::Failure(
                {.Code = HubErrorCode::InsufficientDiskSpace,
                 .Message = "There is not enough disk space for these packages."});
        return HubResult<PackageResolution>::Success(std::move(resolution));
    }
} // namespace KeireHub
