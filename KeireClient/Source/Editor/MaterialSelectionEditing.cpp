#include "KeireClient/Editor/MaterialSelectionEditing.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void PublishMaterialSelection(const std::filesystem::path& root, std::span<const MaterialSourceSnapshot> before,
                                  std::span<const MaterialSourceSnapshot> after)
    {
        if (before.empty() || before.size() != after.size())
            throw std::invalid_argument("Material edits require a complete source snapshot.");
        std::vector<Keire::Detail::ProjectFileReplacement> files;
        std::set<Keire::AssetId> assets;
        for (std::size_t index = 0; index < before.size(); ++index)
        {
            const auto& old = before[index];
            const auto& replacement = after[index];
            if (!old.Asset || old.Asset != replacement.Asset || old.RelativePath != replacement.RelativePath ||
                !assets.insert(old.Asset).second)
                throw std::invalid_argument("Material edit snapshot identities must match uniquely.");
            const auto extension = old.RelativePath.extension();
            if (extension != Keire::MaterialAssetSourceExtension &&
                extension != Keire::LegacyMaterialAssetSourceExtension)
                throw std::invalid_argument("Only property material sources can be edited as a selection.");
            (void)Keire::MaterialAsset::DecodeAuthoringSource(replacement.Source);
            files.push_back({old.RelativePath, old.Source, replacement.Source});
        }
        const Keire::Detail::AnchoredFileSystem fs(root);
        fs.CreateDirectories("Library/AssetOperations");
        Keire::Detail::InterprocessMutex mutex(fs.Root() / "Library/AssetOperations/project.lock");
        std::scoped_lock lock(mutex);
        Keire::Detail::PublishMaterialMigrationFiles(fs.Root(), files);
    }

    namespace
    {
        class MaterialSelectionEdit final : public Keire::UndoCommand
        {
          public:
            MaterialSelectionEdit(std::vector<MaterialSourceSnapshot> before, std::vector<MaterialSourceSnapshot> after,
                                  std::uint64_t serial, MaterialSelectionSourceWriter apply,
                                  Keire::UndoAvailability available)
                : m_Before(std::move(before)), m_After(std::move(after)), m_Serial(serial), m_Apply(std::move(apply)),
                  m_Available(std::move(available))
            {
                if (m_Before.empty() || m_Before.size() != m_After.size() || !m_Apply)
                    throw std::invalid_argument("Material selection history requires snapshots and a writer.");
            }
            std::string_view Name() const noexcept override { return "Edit Selected Materials"; }
            std::size_t EstimatedBytes() const noexcept override
            {
                std::size_t bytes = 0;
                for (const auto& source : m_Before)
                    bytes += source.Source.size();
                for (const auto& source : m_After)
                    bytes += source.Source.size();
                return bytes;
            }
            bool Available() const noexcept override
            {
                try
                {
                    return !m_Available || m_Available();
                }
                catch (...)
                {
                    return false;
                }
            }
            void Redo() override { m_Apply(m_Before, m_After); }
            void Undo() override { m_Apply(m_After, m_Before); }
            bool TryMerge(const Keire::UndoCommand& other) override
            {
                const auto* edit = dynamic_cast<const MaterialSelectionEdit*>(&other);
                if (!edit || edit->m_Serial != m_Serial || edit->m_Before != m_After)
                    return false;
                auto replacement = edit->m_After;
                m_After.swap(replacement);
                return true;
            }

          private:
            std::vector<MaterialSourceSnapshot> m_Before;
            std::vector<MaterialSourceSnapshot> m_After;
            std::uint64_t m_Serial;
            MaterialSelectionSourceWriter m_Apply;
            Keire::UndoAvailability m_Available;
        };
    } // namespace

    std::unique_ptr<Keire::UndoCommand> CreateMaterialSelectionEdit(std::vector<MaterialSourceSnapshot> before,
                                                                    std::vector<MaterialSourceSnapshot> after,
                                                                    std::uint64_t editSerial,
                                                                    MaterialSelectionSourceWriter apply,
                                                                    Keire::UndoAvailability available)
    {
        return std::make_unique<MaterialSelectionEdit>(std::move(before), std::move(after), editSerial,
                                                       std::move(apply), std::move(available));
    }
} // namespace KeireEditor
