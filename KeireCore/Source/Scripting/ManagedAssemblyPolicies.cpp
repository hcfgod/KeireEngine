#include "KeireInternal/Scripting/ManagedAssemblyPolicies.h"
#include "Keire/BuildInfo.h"
#include "Keire/Project/ProjectPackageManager.h"
#include "KeireInternal/FileSystem.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace Keire::Detail
{
    namespace
    {
        std::string_view Trim(std::string_view text)
        {
            const auto start = text.find_first_not_of(" \t");
            if (start == text.npos)
                return {};
            return text.substr(start, text.find_last_not_of(" \t") - start + 1);
        }
        bool Identifier(const std::string_view text)
        {
            return !text.empty() && (std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_') &&
                   std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
        }
        std::vector<std::string_view> Split(std::string_view text, const std::string_view separator)
        {
            std::vector<std::string_view> result;
            while (true)
            {
                const auto end = text.find(separator);
                result.push_back(Trim(text.substr(0, end)));
                if (end == text.npos)
                    return result;
                text.remove_prefix(end + separator.size());
            }
        }
        struct Version
        {
            std::array<std::uint64_t, 3> Numbers{};
            std::vector<std::string> Prerelease;
        };
        bool Numeric(std::string_view text)
        {
            return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c); });
        }
        Version ParseVersion(std::string_view text)
        {
            Version result;
            if (const auto plus = text.find('+'); plus != text.npos)
            {
                for (const auto part : Split(text.substr(plus + 1), "."))
                    if (part.empty() ||
                        !std::ranges::all_of(part, [](unsigned char c) { return std::isalnum(c) || c == '-'; }))
                        throw std::invalid_argument("Invalid version build metadata.");
                text = text.substr(0, plus);
            }
            if (const auto dash = text.find('-'); dash != text.npos)
            {
                for (const auto part : Split(text.substr(dash + 1), "."))
                {
                    if (part.empty() ||
                        !std::ranges::all_of(part, [](unsigned char c) { return std::isalnum(c) || c == '-'; }) ||
                        (Numeric(part) && part.size() > 1 && part.front() == '0'))
                        throw std::invalid_argument("Invalid version prerelease identifier.");
                    result.Prerelease.emplace_back(part);
                }
                text = text.substr(0, dash);
            }
            const auto parts = Split(text, ".");
            if (parts.empty() || parts.size() > 3)
                throw std::invalid_argument("Expected a semantic version.");
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                const auto part = parts[i];
                if (!Numeric(part) || (part.size() > 1 && part.front() == '0'))
                    throw std::invalid_argument("Invalid numeric version component.");
                const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result.Numbers[i]);
                if (parsed.ec != std::errc{})
                    throw std::invalid_argument("Version component exceeds its range.");
            }
            return result;
        }
        int Compare(const Version& a, const Version& b)
        {
            if (a.Numbers != b.Numbers)
                return a.Numbers < b.Numbers ? -1 : 1;
            if (a.Prerelease.empty() != b.Prerelease.empty())
                return a.Prerelease.empty() ? 1 : -1;
            for (std::size_t i = 0; i < std::min(a.Prerelease.size(), b.Prerelease.size()); ++i)
            {
                const auto& x = a.Prerelease[i];
                const auto& y = b.Prerelease[i];
                if (x == y)
                    continue;
                const bool xn = Numeric(x), yn = Numeric(y);
                if (xn != yn)
                    return xn ? -1 : 1;
                if (xn && x.size() != y.size())
                    return x.size() < y.size() ? -1 : 1;
                return x < y ? -1 : 1;
            }
            return a.Prerelease.size() == b.Prerelease.size() ? 0 : a.Prerelease.size() < b.Prerelease.size() ? -1 : 1;
        }
        bool VersionMatches(std::string_view expression, const Version& version)
        {
            expression = Trim(expression);
            if (expression.empty())
                return true;
            if (expression.front() != '[' && expression.front() != '(')
                return Compare(version, ParseVersion(expression)) >= 0;
            if (expression.back() != ']' && expression.back() != ')')
                throw std::invalid_argument("Version range is missing its closing delimiter.");
            const auto body = expression.substr(1, expression.size() - 2);
            const auto bounds = Split(body, ",");
            if (bounds.size() == 1 && expression.front() == '[' && expression.back() == ']')
                return Compare(version, ParseVersion(bounds.front())) == 0;
            if (bounds.size() != 2 || (bounds[0].empty() && bounds[1].empty()))
                throw std::invalid_argument("Invalid version range.");
            std::optional<Version> lower, upper;
            if (!bounds[0].empty())
                lower = ParseVersion(bounds[0]);
            if (!bounds[1].empty())
                upper = ParseVersion(bounds[1]);
            if (lower && upper &&
                (Compare(*lower, *upper) > 0 ||
                 (Compare(*lower, *upper) == 0 && (expression.front() != '[' || expression.back() != ']'))))
                throw std::invalid_argument("Version range is empty or reversed.");
            return (!lower || Compare(version, *lower) > 0 ||
                    (expression.front() == '[' && Compare(version, *lower) == 0)) &&
                   (!upper || Compare(version, *upper) < 0 ||
                    (expression.back() == ']' && Compare(version, *upper) == 0));
        }
        bool ConstraintMatches(const std::string_view expression, const std::set<std::string, std::less<>>& symbols)
        {
            bool result = false;
            for (auto term : Split(expression, "||"))
            {
                const bool negate = term.starts_with('!');
                if (negate)
                    term = Trim(term.substr(1));
                if (!Identifier(term))
                    throw std::invalid_argument("Define constraints require symbols, !symbol, or || alternatives.");
                result |= symbols.contains(term) != negate;
            }
            return result;
        }
        bool Within(const std::filesystem::path& root, const std::filesystem::path& file)
        {
            const auto relative =
                std::filesystem::weakly_canonical(file).lexically_relative(std::filesystem::weakly_canonical(root));
            return !relative.empty() && !relative.is_absolute() &&
                   std::ranges::none_of(relative, [](const auto& part) { return part == ".."; });
        }
        bool ManagedDll(const std::filesystem::path& path)
        {
            // PE's CLR data-directory distinguishes managed assemblies from native plug-ins on every host.
            std::ifstream stream(path, std::ios::binary);
            const auto read = [&](std::uint64_t offset, unsigned count) -> std::uint32_t
            {
                stream.clear();
                stream.seekg(static_cast<std::streamoff>(offset));
                std::uint32_t value = 0;
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto byte = stream.get();
                    if (byte == std::char_traits<char>::eof())
                        return 0;
                    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (8U * i);
                }
                return value;
            };
            if (read(0, 2) != 0x5a4d)
                return false;
            const auto pe = read(0x3c, 4);
            if (read(pe, 4) != 0x4550)
                return false;
            const auto optional = static_cast<std::uint64_t>(pe) + 24;
            const auto magic = read(optional, 2);
            if (magic != 0x10b && magic != 0x20b)
                return false;
            const auto directories = optional + (magic == 0x10b ? 96 : 112);
            return read(directories - 4, 4) > 14 && read(directories + 14 * 8, 4) != 0 &&
                   read(directories + 14 * 8 + 4, 4) >= 72;
        }
    } // namespace

    void ValidateManagedAssemblyPolicies(const ManagedAssemblyDefinition& definition)
    {
        if (definition.SchemaVersion < 4 &&
            (!definition.IncludePlatforms.empty() || !definition.ExcludePlatforms.empty() ||
             !definition.DefineConstraints.empty() || !definition.VersionDefines.empty() ||
             definition.OverrideReferences || !definition.PrecompiledReferences.empty()))
            throw std::invalid_argument("Assembly filtering and DLL controls require schema version 4.");
        if (!definition.IncludePlatforms.empty() && !definition.ExcludePlatforms.empty())
            throw std::invalid_argument("Choose either included or excluded assembly platforms.");
        for (const auto* list : {&definition.IncludePlatforms, &definition.ExcludePlatforms})
        {
            std::set<std::string> seen;
            for (const auto& name : *list)
                if ((name != "Editor" && name != "Windows" && name != "Linux" && name != "macOS") ||
                    !seen.insert(name).second)
                    throw std::invalid_argument(
                        "Assembly platforms must be unique Editor, Windows, Linux, or macOS targets.");
        }
        if (definition.DefineConstraints.size() > 256 || definition.VersionDefines.size() > 256 ||
            definition.PrecompiledReferences.size() > 256)
            throw std::invalid_argument("Assembly policy exceeds the entry limit.");
        for (const auto& expression : definition.DefineConstraints)
            (void)ConstraintMatches(expression, {});
        std::set<std::string> defines;
        for (const auto& item : definition.VersionDefines)
        {
            if (item.Resource.empty() || !Identifier(item.Define) || !defines.insert(item.Define).second)
                throw std::invalid_argument("Version defines require a resource and unique valid symbols.");
            (void)VersionMatches(item.Expression, ParseVersion("0.0.0"));
        }
        std::set<std::filesystem::path> paths;
        for (const auto& path : definition.PrecompiledReferences)
        {
            if (!definition.OverrideReferences || path.empty() || path.is_absolute() || path.extension() != ".dll" ||
                *path.begin() != "Assets" || std::ranges::any_of(path, [](const auto& part) { return part == ".."; }) ||
                !paths.insert(path.lexically_normal()).second)
                throw std::invalid_argument(
                    "Precompiled references require Override References and unique Assets-relative DLL paths.");
        }
    }

    void ApplyManagedAssemblyPolicies(const std::filesystem::path& projectRoot,
                                      std::vector<ManagedAssemblyGraphEntry>& assemblies,
                                      const ManagedAssemblyBuildContext& context)
    {
        auto platform = context.Platform;
        if (platform.empty())
        {
#if defined(_WIN32)
            platform = "Windows";
#elif defined(__APPLE__)
            platform = "macOS";
#else
            platform = "Linux";
#endif
        }
        if (platform != "Windows" && platform != "Linux" && platform != "macOS")
            throw std::invalid_argument("Unsupported managed assembly target platform.");
        std::set<std::string, std::less<>> global(context.DefineSymbols.begin(), context.DefineSymbols.end());
        for (const auto& symbol : global)
            if (!Identifier(symbol))
                throw std::invalid_argument("Invalid build define symbol.");
        global.insert(platform == "macOS" ? "KEIRE_MACOS" : platform == "Windows" ? "KEIRE_WINDOWS" : "KEIRE_LINUX");
        if (context.IsEditor)
            global.insert("KEIRE_EDITOR");
        std::map<std::string, std::string, std::less<>> versions;
        for (const auto& resource : context.ResourceVersions)
        {
            (void)ParseVersion(resource.Version);
            if (resource.Name.empty() || !versions.emplace(resource.Name, resource.Version).second)
                throw std::invalid_argument("Build resource versions must have unique names.");
        }
        versions.try_emplace("Keire", std::string(GetBuildInfo().Version));
        const auto lockPath = ProjectPackageManager::LockPath(projectRoot);
        if (std::filesystem::is_regular_file(lockPath))
            for (const auto& package : DecodeProjectPackageLock(ReadTextFile(lockPath, 16U * 1024U * 1024U)).Packages)
                versions.try_emplace(package.PackageId, package.Version);
        std::set<AssetId> excluded;
        for (auto& assembly : assemblies)
        {
            auto& definition = assembly.Definition;
            definition.SchemaVersion = ManagedAssemblySchemaVersion;
            auto symbols = global;
            symbols.insert(definition.DefineSymbols.begin(), definition.DefineSymbols.end());
            auto available = versions;
            for (const auto& package : definition.Packages)
                available.try_emplace(package.Name, package.Version);
            for (const auto& item : definition.VersionDefines)
                if (const auto resource = available.find(item.Resource);
                    resource != available.end() && VersionMatches(item.Expression, ParseVersion(resource->second)))
                    symbols.insert(item.Define);
            const auto matches = [&](const auto& name)
            { return name == platform || (context.IsEditor && name == "Editor"); };
            const bool enabled =
                (context.IsEditor || definition.Classification == ManagedAssemblyClassification::Runtime) &&
                (definition.IncludePlatforms.empty() || std::ranges::any_of(definition.IncludePlatforms, matches)) &&
                !std::ranges::any_of(definition.ExcludePlatforms, matches) &&
                std::ranges::all_of(definition.DefineConstraints,
                                    [&](const auto& condition) { return ConstraintMatches(condition, symbols); });
            if (!enabled)
                excluded.insert(assembly.Asset);
            definition.DefineSymbols.assign(symbols.begin(), symbols.end());
        }
        std::erase_if(assemblies, [&](const auto& assembly) { return excluded.contains(assembly.Asset); });
        for (auto& assembly : assemblies)
        {
            if (assembly.DefinitionPath.empty() && assembly.Definition.Name.starts_with("Assembly-CSharp"))
                std::erase_if(assembly.Definition.References, [&](auto id) { return excluded.contains(id); });
            else
                for (const auto id : assembly.Definition.References)
                    if (excluded.contains(id))
                        throw std::invalid_argument("Assembly " + assembly.Definition.Name +
                                                    " references an assembly excluded by the current build settings.");
        }
        std::vector<std::filesystem::path> plugins;
        if (std::filesystem::is_directory(projectRoot / "Assets"))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot / "Assets"))
                if (entry.is_regular_file() && entry.path().extension() == ".dll" && ManagedDll(entry.path()))
                {
                    if (!Within(projectRoot / "Assets", entry.path()))
                        throw std::invalid_argument("Managed DLLs must remain inside Assets.");
                    plugins.push_back(entry.path().lexically_relative(projectRoot));
                }
        std::ranges::sort(plugins);
        std::set<std::string> names;
        for (const auto& assembly : assemblies)
            names.insert(assembly.Definition.Name);
        for (const auto& path : plugins)
            if (path.stem() == "Keire.Managed" || path.stem() == "Keire.Editor.Managed" ||
                !names.insert(path.stem().string()).second)
                throw std::invalid_argument(
                    "Managed DLL names must be unique and cannot shadow project or engine assemblies.");
        for (auto& assembly : assemblies)
        {
            assembly.PrecompiledFiles =
                assembly.Definition.OverrideReferences ? assembly.Definition.PrecompiledReferences : plugins;
            for (const auto& path : assembly.PrecompiledFiles)
                if (!Within(projectRoot / "Assets", projectRoot / path) || !ManagedDll(projectRoot / path))
                    throw std::invalid_argument(
                        "Precompiled reference is missing, outside Assets, or is not a managed DLL: " +
                        PathToUtf8(path));
        }
    }
} // namespace Keire::Detail
