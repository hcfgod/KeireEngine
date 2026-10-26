#include "KeireInternal/ProjectFileTransaction.h"

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumFileBytes = 32U * 1024U * 1024U;
        constexpr std::size_t MaximumTransactionBytes = 512U * 1024U * 1024U;
        constexpr std::size_t MaximumJournalBytes = 4U * 1024U * 1024U;
        struct TransactionPolicy
        {
            std::filesystem::path JournalRoot;
            std::filesystem::path LockPath;
            bool SettingsOnly = false;
        };

        const TransactionPolicy MaterialPolicy{"Library/MaterialShaderUpgrade", "Library/MaterialShaderUpgrade.lock"};
        const TransactionPolicy SettingsPolicy{"Library/ProjectSettingsSave", "Library/ProjectSettingsSave.lock", true};

        [[nodiscard]] std::string Digest(const std::span<const std::byte> bytes)
        {
            return DigestToString(Sha256(bytes));
        }

        void WriteJournal(const AnchoredFileSystem& fs, const std::filesystem::path& path, const Json& journal)
        {
            const auto text = journal.dump(2) + '\n';
            if (text.size() > MaximumJournalBytes)
                throw std::invalid_argument("Project file transaction recovery journal exceeds its bound.");
            fs.WriteFileAtomically(path, std::as_bytes(std::span(text.data(), text.size())));
        }

        [[nodiscard]] Json ReadJournal(const AnchoredFileSystem& fs, const std::filesystem::path& path)
        {
            const auto bytes = fs.Read(path, MaximumJournalBytes);
            const auto journal = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                             reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (journal.at("schemaVersion") != 1 || !journal.at("files").is_array() ||
                journal.at("files").size() > 4096)
                throw std::runtime_error("Project file transaction recovery journal is invalid.");
            const auto state = journal.at("state").get<std::string>();
            if (state != "prepared" && state != "committed" && state != "rolledBack")
                throw std::runtime_error("Project file transaction recovery state is invalid.");
            return journal;
        }

        void ValidateDestination(const std::filesystem::path& destination, const TransactionPolicy& policy)
        {
            if (policy.SettingsOnly)
            {
                if (destination != "ProjectSettings/Rendering.keiresettings" &&
                    destination != "ProjectSettings/Authoring.keiresettings" &&
                    destination != "ProjectSettings/EnvironmentTime.keiresettings" &&
                    destination != "ProjectSettings/EnvironmentLighting.keiresettings")
                {
                    throw std::invalid_argument("Settings transaction destination is not an editable settings file.");
                }
                return;
            }
            if (destination.empty() || destination.is_absolute() || destination.has_root_name() ||
                destination.lexically_normal() != destination)
                throw std::invalid_argument(
                    "Material migration destinations must be normalized project-relative paths.");
            for (const auto& part : destination)
                if (part == ".." || part == ".")
                    throw std::invalid_argument("Material migration destinations cannot traverse directories.");
            const auto first = *destination.begin();
            const auto runtime = destination.lexically_relative("Library/AssetCache/Runtime");
            if (!runtime.empty() && runtime != "." && *runtime.begin() != "..")
                return;
            if (first != "Assets" && first != "ProjectSettings" && first != "Packages")
                throw std::invalid_argument(
                    "Material migration destination is outside authoring content or runtime publication.");
        }

        [[nodiscard]] std::vector<std::filesystem::path> Journals(const std::filesystem::path& root,
                                                                  const TransactionPolicy& policy)
        {
            std::vector<std::filesystem::path> result;
            const auto directory = root / policy.JournalRoot;
            if (!std::filesystem::exists(directory))
                return result;
            if (std::filesystem::is_symlink(directory))
                throw std::runtime_error("Project file transaction journal directory cannot be a link.");
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (entry.is_symlink())
                    throw std::runtime_error("Project file transaction directory cannot be a link.");
                if (entry.is_directory() && std::filesystem::exists(entry.path() / "journal.json"))
                    result.push_back(policy.JournalRoot / entry.path().filename() / "journal.json");
            }
            std::ranges::sort(result);
            return result;
        }

        void ValidateRecoveryDestination(const AnchoredFileSystem& fs, const std::filesystem::path& destination,
                                         const Json& file)
        {
            const auto& original = file.at("original");
            if (fs.Exists(destination))
            {
                const auto current = Digest(fs.Read(destination, MaximumFileBytes));
                if (current != file.at("replacement").get<std::string>() &&
                    (original.is_null() || current != original.get<std::string>()))
                {
                    throw std::runtime_error("Project file transaction recovery found an external edit: " +
                                             PathToUtf8(destination));
                }
            }
            else if (!original.is_null())
            {
                throw std::runtime_error("Project file transaction recovery found an externally removed source.");
            }
        }

        void Rollback(const AnchoredFileSystem& fs, const std::filesystem::path& path, Json journal,
                      const TransactionPolicy& policy)
        {
            const auto directory = path.parent_path();
            std::set<std::filesystem::path> destinations;
            // Preflight the entire group so existing conflicts do not cause a partial rollback.
            for (std::size_t index = 0; index < journal.at("files").size(); ++index)
            {
                const auto& file = journal.at("files")[index];
                const auto destination = PathFromUtf8(file.at("destination").get<std::string>());
                ValidateDestination(destination, policy);
                if (!destinations.insert(destination).second)
                    throw std::runtime_error("Project file transaction journal repeats a destination.");
                const auto original = file.at("original");
                if (!original.is_null() && Digest(fs.Read(directory / (std::to_string(index) + ".backup"),
                                                          MaximumFileBytes)) != original.get<std::string>())
                    throw std::runtime_error("Project file transaction backup failed its integrity check.");
                ValidateRecoveryDestination(fs, destination, file);
            }
            for (std::size_t index = journal.at("files").size(); index > 0; --index)
            {
                const auto& file = journal.at("files")[index - 1];
                const auto destination = PathFromUtf8(file.at("destination").get<std::string>());
                // Earlier restores may take time. Recheck changes made since group preflight; this is not
                // an atomic compare-and-replace against writers that do not participate in our lock.
                ValidateRecoveryDestination(fs, destination, file);
                if (file.at("original").is_null())
                {
                    if (fs.Exists(destination))
                        fs.Remove(destination);
                }
                else
                    fs.WriteFileAtomically(
                        destination, fs.Read(directory / (std::to_string(index - 1) + ".backup"), MaximumFileBytes));
            }
            journal["state"] = "rolledBack";
            WriteJournal(fs, path, journal);
        }
        void RemoveUnpreparedSettingsStaging(const std::filesystem::path& root, const AnchoredFileSystem& fs,
                                             const TransactionPolicy& policy)
        {
            if (!policy.SettingsOnly)
            {
                return;
            }
            const auto directory = policy.JournalRoot / "active";
            if (!fs.Exists(directory) || fs.Exists(directory / "journal.json"))
            {
                return;
            }
            // No destination is published until the prepared journal exists. A single reusable staging
            // directory bounds interrupted preparation, including failures before its first journal write.
            for (std::size_t index = 0; index < 4; ++index)
            {
                for (const auto* extension : {".backup", ".staged"})
                {
                    const auto payload = directory / (std::to_string(index) + extension);
                    if (fs.Exists(payload))
                    {
                        fs.Remove(payload);
                    }
                }
            }
            if (!std::filesystem::is_empty(root / directory))
            {
                throw std::runtime_error(
                    "Unprepared settings staging contains unknown files; preserve them before retrying.");
            }
            fs.Remove(directory);
        }

        void RemoveCompletedSettingsJournals(const std::filesystem::path& root, const AnchoredFileSystem& fs,
                                             const TransactionPolicy& policy)
        {
            if (!policy.SettingsOnly)
            {
                return;
            }
            for (const auto& path : Journals(root, policy))
            {
                const auto journal = ReadJournal(fs, path);
                if (journal.at("state") == "prepared")
                {
                    continue;
                }
                if (journal.at("files").size() > 4)
                {
                    throw std::runtime_error("Settings recovery journal exceeds its file-count bound.");
                }
                for (const auto& file : journal.at("files"))
                {
                    ValidateDestination(PathFromUtf8(file.at("destination").get<std::string>()), policy);
                }
                // Keep the completion marker until every known payload is removed. An interrupted cleanup
                // can safely retry; never recursively remove a directory or unknown diagnostic files.
                for (std::size_t index = 0; index < journal.at("files").size(); ++index)
                {
                    for (const auto* extension : {".backup", ".staged"})
                    {
                        const auto payload = path.parent_path() / (std::to_string(index) + extension);
                        if (fs.Exists(payload))
                        {
                            fs.Remove(payload);
                        }
                    }
                }
                fs.Remove(path);
                // An unexpected file belongs to the user; leave its directory intact.
                if (std::filesystem::is_empty(root / path.parent_path()))
                {
                    fs.Remove(path.parent_path());
                }
            }
        }

        bool HasPending(const std::filesystem::path& root, const TransactionPolicy& policy)
        {
            const AnchoredFileSystem fs(root);
            for (const auto& path : Journals(root, policy))
                if (ReadJournal(fs, path).at("state") == "prepared")
                    return true;
            return false;
        }

        std::size_t Recover(const std::filesystem::path& root, const TransactionPolicy& policy)
        {
            const AnchoredFileSystem fs(root);
            fs.CreateDirectories("Library");
            InterprocessMutex mutex(root / policy.LockPath);
            std::scoped_lock lock(mutex);
            RemoveUnpreparedSettingsStaging(root, fs, policy);
            std::size_t recovered = 0;
            for (const auto& path : Journals(root, policy))
            {
                auto journal = ReadJournal(fs, path);
                if (policy.SettingsOnly && journal.at("files").size() > 4)
                {
                    throw std::runtime_error("Settings recovery journal exceeds its file-count bound.");
                }
                if (journal.at("state") == "prepared")
                {
                    Rollback(fs, path, std::move(journal), policy);
                    ++recovered;
                }
            }
            RemoveCompletedSettingsJournals(root, fs, policy);
            return recovered;
        }

        void Publish(const std::filesystem::path& root, const std::span<const ProjectFileReplacement> files,
                     const TransactionPolicy& policy)
        {
            if (files.empty())
                return;
            if (files.size() > (policy.SettingsOnly ? 4U : 4096U))
                throw std::invalid_argument("Project file transaction exceeds its file-count bound.");
            const AnchoredFileSystem fs(root);
            fs.CreateDirectories("Library");
            InterprocessMutex mutex(root / policy.LockPath);
            std::scoped_lock lock(mutex);
            if (HasPending(root, policy))
                throw std::runtime_error("Recover the interrupted project file transaction before publishing another.");
            RemoveUnpreparedSettingsStaging(root, fs, policy);
            RemoveCompletedSettingsJournals(root, fs, policy);
            std::set<std::filesystem::path> destinations;
            std::size_t total = 0;
            for (const auto& file : files)
            {
                ValidateDestination(file.Destination, policy);
                if (!destinations.insert(file.Destination).second || file.Contents.size() > MaximumFileBytes ||
                    (file.ExpectedOriginal && file.ExpectedOriginal->size() > MaximumFileBytes))
                    throw std::invalid_argument("Project transaction files are duplicated or exceed their size bound.");
                total += file.Contents.size() + (file.ExpectedOriginal ? file.ExpectedOriginal->size() : 0);
                if (total > MaximumTransactionBytes)
                    throw std::invalid_argument("Project file transaction exceeds its transaction byte bound.");
                if (fs.Exists(file.Destination) != file.ExpectedOriginal.has_value() ||
                    (file.ExpectedOriginal && fs.Read(file.Destination, MaximumFileBytes) != *file.ExpectedOriginal))
                    throw std::runtime_error("Project file transaction source changed after review: " +
                                             PathToUtf8(file.Destination));
            }
            const auto directory =
                policy.JournalRoot / (policy.SettingsOnly ? "active" : AssetId::Generate().ToString());
            fs.CreateDirectories(directory);
            Json journal{{"schemaVersion", 1}, {"state", "prepared"}, {"files", Json::array()}};
            for (std::size_t index = 0; index < files.size(); ++index)
            {
                const auto& file = files[index];
                fs.WriteFileAtomically(directory / (std::to_string(index) + ".staged"), file.Contents, false);
                if (file.ExpectedOriginal)
                    fs.WriteFileAtomically(directory / (std::to_string(index) + ".backup"), *file.ExpectedOriginal,
                                           false);
                journal["files"].push_back(
                    {{"destination", PathToUtf8(file.Destination)},
                     {"original", file.ExpectedOriginal ? Json(Digest(*file.ExpectedOriginal)) : Json(nullptr)},
                     {"replacement", Digest(file.Contents)}});
            }
            const auto path = directory / "journal.json";
            WriteJournal(fs, path, journal);
            try
            {
                for (const auto& file : files)
                {
                    if (fs.Exists(file.Destination) != file.ExpectedOriginal.has_value() ||
                        (file.ExpectedOriginal &&
                         fs.Read(file.Destination, MaximumFileBytes) != *file.ExpectedOriginal))
                        throw std::runtime_error("Project file transaction source changed during publication.");
                    fs.CreateDirectories(file.Destination.parent_path());
                    fs.WriteFileAtomically(file.Destination, file.Contents, file.ExpectedOriginal.has_value());
                }
                journal["state"] = "committed";
                WriteJournal(fs, path, journal);
            }
            catch (...)
            {
                try
                {
                    Rollback(fs, path, journal, policy);
                }
                catch (...)
                { /* Retain the journal and preserve the original publication failure. */
                }
                throw;
            }
            // Publication is already committed. Cleanup failure must not misreport a failed save; the next
            // save/recovery retries cleanup before allowing another journal to accumulate.
            try
            {
                RemoveCompletedSettingsJournals(root, fs, policy);
            }
            catch (...)
            {
            }
        }
    } // namespace

    void PublishMaterialMigrationFiles(const std::filesystem::path& root,
                                       const std::span<const ProjectFileReplacement> files)
    {
        Publish(root, files, MaterialPolicy);
    }

    bool HasPendingMaterialMigration(const std::filesystem::path& root) { return HasPending(root, MaterialPolicy); }

    std::size_t RecoverMaterialMigrationFiles(const std::filesystem::path& root)
    {
        return Recover(root, MaterialPolicy);
    }

    void PublishProjectSettingsFiles(const std::filesystem::path& root,
                                     const std::span<const ProjectFileReplacement> files)
    {
        Publish(root, files, SettingsPolicy);
    }

    bool HasPendingProjectSettingsSave(const std::filesystem::path& root) { return HasPending(root, SettingsPolicy); }

    std::size_t RecoverProjectSettingsFiles(const std::filesystem::path& root) { return Recover(root, SettingsPolicy); }
} // namespace Keire::Detail
