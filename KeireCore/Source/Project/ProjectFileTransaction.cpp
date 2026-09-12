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
        const std::filesystem::path JournalRoot = "Library/MaterialShaderUpgrade";

        [[nodiscard]] std::string Digest(const std::span<const std::byte> bytes)
        {
            return DigestToString(Sha256(bytes));
        }

        void WriteJournal(const AnchoredFileSystem& fs, const std::filesystem::path& path, const Json& journal)
        {
            const auto text = journal.dump(2) + '\n';
            if (text.size() > MaximumJournalBytes)
                throw std::invalid_argument("Material migration recovery journal exceeds its bound.");
            fs.WriteFileAtomically(path, std::as_bytes(std::span(text.data(), text.size())));
        }

        [[nodiscard]] Json ReadJournal(const AnchoredFileSystem& fs, const std::filesystem::path& path)
        {
            const auto bytes = fs.Read(path, MaximumJournalBytes);
            const auto journal = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                             reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (journal.at("schemaVersion") != 1 || !journal.at("files").is_array() ||
                journal.at("files").size() > 4096)
                throw std::runtime_error("Material migration recovery journal is invalid.");
            const auto state = journal.at("state").get<std::string>();
            if (state != "prepared" && state != "committed" && state != "rolledBack")
                throw std::runtime_error("Material migration recovery state is invalid.");
            return journal;
        }

        void ValidateDestination(const std::filesystem::path& destination)
        {
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

        [[nodiscard]] std::vector<std::filesystem::path> Journals(const std::filesystem::path& root)
        {
            std::vector<std::filesystem::path> result;
            const auto directory = root / JournalRoot;
            if (!std::filesystem::exists(directory))
                return result;
            if (std::filesystem::is_symlink(directory))
                throw std::runtime_error("Material migration journal directory cannot be a link.");
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (entry.is_symlink())
                    throw std::runtime_error("Material migration transaction directory cannot be a link.");
                if (entry.is_directory() && std::filesystem::exists(entry.path() / "journal.json"))
                    result.push_back(JournalRoot / entry.path().filename() / "journal.json");
            }
            std::ranges::sort(result);
            return result;
        }

        void Rollback(const AnchoredFileSystem& fs, const std::filesystem::path& path, Json journal)
        {
            const auto directory = path.parent_path();
            std::set<std::filesystem::path> destinations;
            // Check every destination before touching any of them. External edits must never be overwritten.
            for (std::size_t index = 0; index < journal.at("files").size(); ++index)
            {
                const auto& file = journal.at("files")[index];
                const auto destination = PathFromUtf8(file.at("destination").get<std::string>());
                ValidateDestination(destination);
                if (!destinations.insert(destination).second)
                    throw std::runtime_error("Material migration journal repeats a destination.");
                const auto original = file.at("original");
                if (!original.is_null() && Digest(fs.Read(directory / (std::to_string(index) + ".backup"),
                                                          MaximumFileBytes)) != original.get<std::string>())
                    throw std::runtime_error("Material migration backup failed its integrity check.");
                if (fs.Exists(destination))
                {
                    const auto current = Digest(fs.Read(destination, MaximumFileBytes));
                    if (current != file.at("replacement").get<std::string>() &&
                        (original.is_null() || current != original.get<std::string>()))
                        throw std::runtime_error("Material migration recovery found an external edit: " +
                                                 PathToUtf8(destination));
                }
                else if (!original.is_null())
                    throw std::runtime_error("Material migration recovery found an externally removed source.");
            }
            for (std::size_t index = journal.at("files").size(); index > 0; --index)
            {
                const auto& file = journal.at("files")[index - 1];
                const auto destination = PathFromUtf8(file.at("destination").get<std::string>());
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
    } // namespace

    bool HasPendingMaterialMigration(const std::filesystem::path& root)
    {
        const AnchoredFileSystem fs(root);
        for (const auto& path : Journals(root))
            if (ReadJournal(fs, path).at("state") == "prepared")
                return true;
        return false;
    }

    std::size_t RecoverMaterialMigrationFiles(const std::filesystem::path& root)
    {
        const AnchoredFileSystem fs(root);
        fs.CreateDirectories("Library");
        InterprocessMutex mutex(root / "Library/MaterialShaderUpgrade.lock");
        std::scoped_lock lock(mutex);
        std::size_t recovered = 0;
        for (const auto& path : Journals(root))
        {
            auto journal = ReadJournal(fs, path);
            if (journal.at("state") == "prepared")
            {
                Rollback(fs, path, std::move(journal));
                ++recovered;
            }
        }
        return recovered;
    }

    void PublishMaterialMigrationFiles(const std::filesystem::path& root,
                                       const std::span<const ProjectFileReplacement> files)
    {
        if (files.empty())
            return;
        if (files.size() > 4096)
            throw std::invalid_argument("Material migration exceeds its file-count bound.");
        const AnchoredFileSystem fs(root);
        fs.CreateDirectories("Library");
        InterprocessMutex mutex(root / "Library/MaterialShaderUpgrade.lock");
        std::scoped_lock lock(mutex);
        if (HasPendingMaterialMigration(root))
            throw std::runtime_error("Recover the interrupted material migration before starting another upgrade.");
        std::set<std::filesystem::path> destinations;
        std::size_t total = 0;
        for (const auto& file : files)
        {
            ValidateDestination(file.Destination);
            if (!destinations.insert(file.Destination).second || file.Contents.size() > MaximumFileBytes ||
                (file.ExpectedOriginal && file.ExpectedOriginal->size() > MaximumFileBytes))
                throw std::invalid_argument("Material migration files are duplicated or exceed their size bound.");
            total += file.Contents.size() + (file.ExpectedOriginal ? file.ExpectedOriginal->size() : 0);
            if (total > MaximumTransactionBytes)
                throw std::invalid_argument("Material migration exceeds its transaction byte bound.");
            if (fs.Exists(file.Destination) != file.ExpectedOriginal.has_value() ||
                (file.ExpectedOriginal && fs.Read(file.Destination, MaximumFileBytes) != *file.ExpectedOriginal))
                throw std::runtime_error("Material migration source changed after review: " +
                                         PathToUtf8(file.Destination));
        }
        const auto directory = JournalRoot / AssetId::Generate().ToString();
        fs.CreateDirectories(directory);
        Json journal{{"schemaVersion", 1}, {"state", "prepared"}, {"files", Json::array()}};
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            const auto& file = files[index];
            fs.WriteFileAtomically(directory / (std::to_string(index) + ".staged"), file.Contents, false);
            if (file.ExpectedOriginal)
                fs.WriteFileAtomically(directory / (std::to_string(index) + ".backup"), *file.ExpectedOriginal, false);
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
                    (file.ExpectedOriginal && fs.Read(file.Destination, MaximumFileBytes) != *file.ExpectedOriginal))
                    throw std::runtime_error("Material migration source changed during publication.");
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
                Rollback(fs, path, journal);
            }
            catch (...)
            { /* Retain the journal and preserve the original publication failure. */
            }
            throw;
        }
    }
} // namespace Keire::Detail
