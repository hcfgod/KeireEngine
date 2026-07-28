#include "KeireClient/Editor/VfxEffectDocument.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    VfxEffectDocument::VfxEffectDocument(VfxEffectDocumentSpecification specification)
        : m_Host(
              {.Validate = [](const Keire::VfxEffectDefinition& definition) { Keire::ValidateVfxEffect(definition); },
               .Encode = [](const Keire::VfxEffectDefinition& definition)
               { return Keire::VfxEffectAsset::Encode(definition); },
               .Preview = std::move(specification.Preview),
               .CancelPreview = std::move(specification.StopPreview),
               .Persist = std::move(specification.Persist)})
    {
    }

    void VfxEffectDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                 const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        Open(asset, Keire::VfxEffectAsset::Decode(bytes)->Definition(), revision, std::move(undo));
    }

    void VfxEffectDocument::Open(const Keire::AssetId asset, Keire::VfxEffectDefinition definition,
                                 const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
    }

    void VfxEffectDocument::Create(const Keire::AssetId asset, Keire::VfxEffectDefinition definition,
                                   Keire::Ref<Keire::UndoContext> undo)
    {
        m_Host.Create(asset, std::move(definition), std::move(undo));
    }

    void VfxEffectDocument::Save() { m_Host.Save(); }

    void VfxEffectDocument::Discard() { m_Host.Discard(); }

    AssetDocumentReloadResult VfxEffectDocument::Reload(const std::span<const std::byte> bytes,
                                                        const std::uint64_t revision)
    {
        return Reload(Keire::VfxEffectAsset::Decode(bytes)->Definition(), revision);
    }

    AssetDocumentReloadResult VfxEffectDocument::Reload(Keire::VfxEffectDefinition definition,
                                                        const std::uint64_t revision)
    {
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        return m_Host.Reload(std::move(definition), revision);
    }

    bool VfxEffectDocument::Undo() { return m_Host.Undo(); }

    bool VfxEffectDocument::Redo() { return m_Host.Redo(); }

    void VfxEffectDocument::Close() noexcept { m_Host.Close(); }

    bool VfxEffectDocument::Edit(const std::string_view name,
                                 const std::function<void(Keire::VfxEffectDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX effect edits require an operation.");
        auto candidate = m_Host.Draft();
        operation(candidate);
        return m_Host.Edit(name, std::move(candidate));
    }

    bool VfxEffectDocument::AddModule(Keire::VfxModuleDefinition module)
    {
        return Edit("Add VFX module", [module = std::move(module)](Keire::VfxEffectDefinition& definition) mutable
                    { definition.Modules.push_back(std::move(module)); });
    }

    bool VfxEffectDocument::EditModule(const Keire::AssetId module,
                                       const std::function<void(Keire::VfxModuleDefinition&)>& operation)
    {
        if (!operation)
            throw std::invalid_argument("VFX module edits require an operation.");
        return Edit("Edit VFX module",
                    [module, operation](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                        if (found == definition.Modules.end())
                            throw std::invalid_argument("VFX module is unavailable.");
                        operation(*found);
                        if (found->Id != module)
                            throw std::invalid_argument("VFX module edits cannot replace the stable ID.");
                    });
    }

    bool VfxEffectDocument::RemoveModule(const Keire::AssetId module)
    {
        return Edit("Remove VFX module",
                    [module](Keire::VfxEffectDefinition& definition)
                    {
                        const auto found =
                            std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                        if (found == definition.Modules.end())
                            throw std::invalid_argument("VFX module is unavailable.");
                        definition.Modules.erase(found);
                    });
    }

    bool VfxEffectDocument::MoveModule(const Keire::AssetId module, const std::size_t destination)
    {
        return Edit(
            "Reorder VFX module",
            [module, destination](Keire::VfxEffectDefinition& definition)
            {
                if (destination >= definition.Modules.size())
                    throw std::invalid_argument("VFX module destination is out of range.");
                const auto found = std::ranges::find(definition.Modules, module, &Keire::VfxModuleDefinition::Id);
                if (found == definition.Modules.end())
                    throw std::invalid_argument("VFX module is unavailable.");
                const auto source = static_cast<std::size_t>(std::distance(definition.Modules.begin(), found));
                if (source == destination)
                    return;
                auto moved = std::move(*found);
                definition.Modules.erase(found);
                definition.Modules.insert(
                    std::next(definition.Modules.begin(), static_cast<std::ptrdiff_t>(destination)), std::move(moved));
            });
    }
} // namespace KeireEditor
