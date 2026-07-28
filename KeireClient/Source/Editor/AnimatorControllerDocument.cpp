#include "KeireClient/Editor/AnimatorControllerDocument.h"

#include "KeireInternal/FileSystem.h"

#include <memory>
#include <optional>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::size_t EstimateGraphBytes(const Keire::AnimationGraphDefinition& definition) noexcept
        {
            std::size_t result = sizeof(definition);
            for (const auto& parameter : definition.ParameterDefinitions)
                result += sizeof(parameter) + parameter.Id.size() + parameter.Name.size();
            for (const auto& layer : definition.Layers)
            {
                result += sizeof(layer) + layer.Id.size() + layer.Name.size() + layer.EntryStateId.size();
                for (const auto& state : layer.States)
                {
                    result += sizeof(state) + state.Id.size() + state.Name.size();
                    for (const auto& child : state.Motion.Children)
                        result += sizeof(child) + child.Id.size();
                    for (const auto& transition : state.Transitions)
                    {
                        result += sizeof(transition) + transition.Id.size() + transition.DestinationId.size();
                        for (const auto& condition : transition.Conditions)
                            result += sizeof(condition) + condition.ParameterId.size();
                    }
                }
            }
            return result;
        }
    } // namespace

    void AnimatorControllerDocument::Open(const Keire::AssetId asset, Keire::AnimationGraphDefinition definition,
                                          Keire::Ref<Keire::UndoContext> undo, std::filesystem::path source)
    {
        Close();
        m_Asset = asset;
        m_Definition = std::move(definition);
        m_Undo = std::move(undo);
        m_Source = std::move(source);
        if (!m_Definition.Layers.empty())
        {
            m_SelectedLayer = m_Definition.Layers.front().Id;
            if (!m_Definition.Layers.front().States.empty())
                m_SelectedState = m_Definition.Layers.front().States.front().Id;
        }
    }

    void AnimatorControllerDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("AnimatorControllerDocument cannot save without an asset and source path.");
        const auto bytes = Keire::AnimationGraphAsset::Encode(m_Definition);
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(m_Source, contents);
        m_Dirty = false;
    }

    void AnimatorControllerDocument::ReplaceDefinition(Keire::AnimationGraphDefinition definition, const bool dirty)
    {
        m_Definition = std::move(definition);
        m_Dirty = dirty;
    }

    void AnimatorControllerDocument::SelectParameter(std::string id)
    {
        m_SelectedParameter = std::move(id);
        m_SelectedLayer.clear();
        m_SelectedState.clear();
    }

    void AnimatorControllerDocument::SelectLayer(std::string id)
    {
        m_SelectedParameter.clear();
        m_SelectedLayer = std::move(id);
        m_SelectedState.clear();
    }

    void AnimatorControllerDocument::SelectState(std::string layer, std::string state)
    {
        m_SelectedParameter.clear();
        m_SelectedLayer = std::move(layer);
        m_SelectedState = std::move(state);
    }

    void AnimatorControllerDocument::ClearSelection() noexcept
    {
        m_SelectedParameter.clear();
        m_SelectedLayer.clear();
        m_SelectedState.clear();
    }

    void AnimatorControllerDocument::RecordApplied(const std::string_view name, Keire::AnimationGraphDefinition before)
    {
        m_Dirty = true;
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<Keire::AnimationGraphDefinition>>();
        const auto asset = m_Asset;
        const auto cost = EstimateGraphBytes(m_Definition);
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
            cost, [this, asset] { return m_Asset == asset; }));
    }

    bool AnimatorControllerDocument::Undo() { return m_Undo && m_Undo->Undo(); }

    bool AnimatorControllerDocument::Redo() { return m_Undo && m_Undo->Redo(); }

    void AnimatorControllerDocument::Close() noexcept
    {
        m_Asset = {};
        m_Definition = {};
        m_Source.clear();
        m_Undo.Reset();
        ClearSelection();
        m_Dirty = false;
    }
} // namespace KeireEditor
