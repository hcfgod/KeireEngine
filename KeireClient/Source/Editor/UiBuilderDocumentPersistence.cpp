#include "KeireClient/Editor/DocumentSourcePersistence.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/UiBuilderDocument.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void UiBuilderDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI document before saving it.");
        auto baseline = m_Definition;
        m_Persistence.Publish(Keire::UiVisualTreeAsset::EncodeSource(m_Definition));
        m_Baseline = std::move(baseline);
        m_Dirty = false;
        AdvanceGeneration();
    }

    void UiBuilderDocument::ReloadFromSource(const bool discardLocalChanges)
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI document before reloading it.");
        if (m_Dirty && !discardLocalChanges)
            throw std::logic_error("The UI document has unsaved changes. Revert explicitly to discard them.");
        auto bytes = Detail::ReadBytes(m_Source, "UI document", Keire::MaximumUiDocumentBytes);
        auto definition = Keire::UiVisualTreeAsset::ParseSource(bytes);
        Keire::UiVisualTreeAsset::Validate(definition);
        DocumentSourcePersistence persistence;
        persistence.Bind(m_Source, std::move(bytes));
        auto before = m_Definition;
        const bool changed = Keire::UiVisualTreeAsset::Encode(before) != Keire::UiVisualTreeAsset::Encode(definition);
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        NormalizeSelection();
        if (!Find(m_Selection))
            Select(m_Definition.Root.StableId);
        if (changed)
            RecordApplied("Revert UI document", std::move(before));
        m_Persistence = std::move(persistence);
        RefreshDirtyState();
        if (changed)
            AdvanceGeneration();
    }

} // namespace KeireEditor
