#include "KeireClient/Editor/InputActionsDocument.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void InputActionsDocument::Open(const Keire::AssetId asset, Keire::InputActionAssetDefinition definition,
                                    Keire::Ref<Keire::UndoContext> undo, std::filesystem::path source,
                                    std::optional<std::vector<std::byte>> loadedBytes)
    {
        DocumentSourcePersistence persistence;
        persistence.Bind(source, std::move(loadedBytes));
        Close();
        m_Persistence = std::move(persistence);
        m_Asset = asset;
        m_Definition = std::move(definition);
        m_Undo = std::move(undo);
        m_Source = std::move(source);
    }

    void InputActionsDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("InputActionsDocument cannot save without an asset and source path.");
        m_Persistence.Publish(Keire::InputActionAsset::Encode(m_Definition));
        m_Dirty = false;
    }

    void InputActionsDocument::ReplaceDefinition(Keire::InputActionAssetDefinition definition, const bool dirty)
    {
        Keire::InputActionAsset::Validate(definition);
        m_Definition = std::move(definition);
        m_Dirty = dirty;
    }

    bool InputActionsDocument::TryReplaceDefinition(Keire::InputActionAssetDefinition definition,
                                                    std::string& diagnostic, const bool dirty)
    {
        try
        {
            const auto candidateBytes = Keire::InputActionAsset::Encode(definition);
            if (candidateBytes == Keire::InputActionAsset::Encode(m_Definition))
            {
                diagnostic.clear();
                return false;
            }
            m_Definition = std::move(definition);
            m_Dirty = dirty;
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

    void InputActionsDocument::SelectMap(const Keire::AssetId map) noexcept
    {
        m_Map = map;
        m_Scheme = {};
        m_Action = {};
        m_Binding = {};
    }

    void InputActionsDocument::SelectScheme(const Keire::AssetId scheme) noexcept
    {
        m_Scheme = scheme;
        m_Map = {};
        m_Action = {};
        m_Binding = {};
    }

    void InputActionsDocument::SelectAction(const Keire::AssetId action) noexcept
    {
        m_Action = action;
        m_Binding = {};
    }

    void InputActionsDocument::SelectBinding(const Keire::AssetId binding) noexcept { m_Binding = binding; }

    void InputActionsDocument::SetSelection(const Keire::AssetId map, const Keire::AssetId scheme,
                                            const Keire::AssetId action, const Keire::AssetId binding) noexcept
    {
        m_Map = map;
        m_Scheme = scheme;
        m_Action = action;
        m_Binding = binding;
    }

    void InputActionsDocument::ClearSelection() noexcept
    {
        m_Map = {};
        m_Scheme = {};
        m_Action = {};
        m_Binding = {};
    }

    void InputActionsDocument::RecordApplied(const std::string_view name, Keire::InputActionAssetDefinition before)
    {
        m_Dirty = true;
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<Keire::InputActionAssetDefinition>>();
        const auto asset = m_Asset;
        m_Undo->RecordApplied(Keire::CreateUndoCommand(
            std::string(name),
            [this, after, asset]
            {
                if (m_Asset != asset)
                    return;
                if (after->has_value())
                    m_Definition = **after;
                m_Dirty = true;
            },
            [this, after, before = std::move(before), asset]() mutable
            {
                if (m_Asset != asset)
                    return;
                if (!after->has_value())
                    *after = m_Definition;
                m_Definition = before;
                m_Dirty = true;
            },
            Keire::InputActionAsset::Encode(m_Definition).size(), [this, asset] { return m_Asset == asset; }));
    }

    bool InputActionsDocument::Undo() { return m_Undo && m_Undo->Undo(); }

    bool InputActionsDocument::Redo() { return m_Undo && m_Undo->Redo(); }

    void InputActionsDocument::Close() noexcept
    {
        m_Asset = {};
        m_Map = {};
        m_Scheme = {};
        m_Action = {};
        m_Binding = {};
        m_Definition = {};
        m_Source.clear();
        m_Persistence = {};
        m_Undo.Reset();
        m_Dirty = false;
    }
} // namespace KeireEditor
