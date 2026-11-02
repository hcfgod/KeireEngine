#include "KeireClient/Editor/PostProcessDocument.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        PostProcessDocument::Specification
        ConfigurePostProcessDocument(PostProcessDocument::Specification specification)
        {
            specification.Validate = Keire::PostProcessProfileAsset::Validate;
            specification.Encode = Keire::PostProcessProfileAsset::Encode;
            specification.EstimateSize = [](const Keire::PostProcessProfileDefinition& definition)
            {
                return sizeof(Keire::PostProcessProfileDefinition) +
                       definition.CustomEffects.Value.size() * sizeof(Keire::PostProcessCustomEffect);
            };
            return specification;
        }
    } // namespace

    PostProcessDocument::PostProcessDocument(Specification specification)
        : m_Host(ConfigurePostProcessDocument(std::move(specification)))
    {
    }

    PostProcessDocument::~PostProcessDocument() { Close(); }

    void PostProcessDocument::Open(const Keire::AssetId asset, const std::span<const std::byte> bytes,
                                   const std::uint64_t revision, Keire::Ref<Keire::UndoContext> undo)
    {
        // Decode first so invalid replacement data cannot close or overwrite an existing draft.
        auto definition = Keire::PostProcessProfileAsset::Decode(bytes)->Definition();
        if (!asset || revision == 0)
        {
            throw std::invalid_argument("Opening a profile requires an asset and revision.");
        }
        CancelContinuousEdit();
        m_Host.Open(asset, std::move(definition), revision, std::move(undo));
    }

    AssetDocumentReloadResult PostProcessDocument::Reload(const std::span<const std::byte> bytes,
                                                          const std::uint64_t revision)
    {
        if (revision == 0)
        {
            throw std::invalid_argument("A profile reload requires a non-zero revision.");
        }
        const auto definition = Keire::PostProcessProfileAsset::Decode(bytes)->Definition();
        CommitContinuousEdit();
        if (definition == m_Host.Draft())
        {
            m_Host.AcknowledgeRevision(revision);
            return AssetDocumentReloadResult::Unchanged;
        }
        return m_Host.Reload(definition, revision);
    }

    void PostProcessDocument::Save()
    {
        CommitContinuousEdit();
        m_Host.Save();
    }

    void PostProcessDocument::Discard()
    {
        CancelContinuousEdit();
        m_Host.Discard();
    }

    void PostProcessDocument::Close() noexcept
    {
        m_ContinuousDraft.reset();
        m_ContinuousLabel.clear();
        m_ContinuousTransaction.reset();
        m_Host.Close();
    }

    bool PostProcessDocument::Undo()
    {
        CommitContinuousEdit();
        return m_Host.Undo();
    }

    bool PostProcessDocument::Redo()
    {
        CommitContinuousEdit();
        return m_Host.Redo();
    }

    bool PostProcessDocument::Edit(const std::string_view label, const Keire::PostProcessProfileDefinition& replacement)
    {
        // Callers may pass Definition(), whose transient storage is released by committing.
        auto candidate = replacement;
        CommitContinuousEdit();
        return m_Host.Edit(label, std::move(candidate));
    }

    void PostProcessDocument::BeginContinuousEdit(const std::string_view label)
    {
        if (label.empty())
        {
            throw std::invalid_argument("A continuous profile edit requires an undo name.");
        }
        CommitContinuousEdit();
        auto draft = m_Host.Draft();
        std::string name(label);
        const auto undo = m_Host.UndoContext();
        auto transaction = undo && undo->IsOpen() ? undo->BeginTransaction(name) : nullptr;
        m_ContinuousDraft = std::move(draft);
        m_ContinuousLabel = std::move(name);
        m_ContinuousTransaction = std::move(transaction);
    }

    bool PostProcessDocument::UpdateContinuousEdit(const Keire::PostProcessProfileDefinition& replacement)
    {
        if (!m_ContinuousDraft)
        {
            throw std::logic_error("Begin a continuous profile edit before updating it.");
        }
        if (*m_ContinuousDraft == replacement)
        {
            return false;
        }
        try
        {
            m_Host.PreviewTransient(replacement);
            m_ContinuousDraft = replacement;
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                CancelContinuousEdit();
            }
            catch (...)
            {
                // Keep the original validation/preview failure when restoration also fails.
            }
            std::rethrow_exception(failure);
        }
        return true;
    }

    void PostProcessDocument::CommitContinuousEdit()
    {
        if (!m_ContinuousDraft)
        {
            return;
        }
        // Transient samples never enter history. Record just the starting and final definitions.
        (void)m_Host.Edit(m_ContinuousLabel, *m_ContinuousDraft);
        if (m_ContinuousTransaction && m_ContinuousTransaction->Active())
        {
            m_ContinuousTransaction->Commit();
        }
        m_ContinuousTransaction.reset();
        m_ContinuousDraft.reset();
        m_ContinuousLabel.clear();
    }

    void PostProcessDocument::CancelContinuousEdit()
    {
        if (!m_ContinuousDraft)
        {
            return;
        }
        auto transaction = std::move(m_ContinuousTransaction);
        m_ContinuousDraft.reset();
        m_ContinuousLabel.clear();
        if (transaction && transaction->Active())
        {
            transaction->Cancel();
        }
        m_Host.RestorePreview();
    }

    bool PostProcessDocument::SetParameter(const Keire::PostProcessParameter parameter,
                                           const Keire::PostProcessOverride<double> value)
    {
        const auto index = static_cast<std::size_t>(parameter);
        const auto descriptions = Keire::PostProcessParameterDescriptions();
        if (index >= descriptions.size())
        {
            throw std::invalid_argument("Unknown post-processing parameter.");
        }
        auto definition = Definition();
        definition.Parameters[index] = value;
        return Edit("Change " + std::string(descriptions[index].Label), definition);
    }

    bool PostProcessDocument::SetEffect(const Keire::PostProcessEffect effect,
                                        const Keire::PostProcessOverride<bool> value)
    {
        const auto index = static_cast<std::size_t>(effect);
        const auto keys = Keire::PostProcessEffectKeys();
        if (index >= keys.size())
        {
            throw std::invalid_argument("Unknown post-processing effect.");
        }
        auto definition = Definition();
        definition.Effects[index] = value;
        return Edit("Change " + std::string(keys[index]), definition);
    }

    bool PostProcessDocument::ResetParameter(const Keire::PostProcessParameter parameter)
    {
        const auto index = static_cast<std::size_t>(parameter);
        const auto descriptions = Keire::PostProcessParameterDescriptions();
        if (index >= descriptions.size())
        {
            throw std::invalid_argument("Unknown post-processing parameter.");
        }
        return SetParameter(parameter, {false, descriptions[index].Default});
    }

    bool PostProcessDocument::SetColorLookup(const Keire::PostProcessOverride<Keire::AssetId> reference,
                                             const Keire::PostProcessOverride<double> weight)
    {
        auto definition = Definition();
        if (reference.Value && reference.Value == m_Host.Asset())
        {
            throw std::invalid_argument("A profile cannot reference itself as a color lookup texture.");
        }
        if (definition.ColorLookup == reference && definition.ColorLookupWeight == weight)
        {
            return false;
        }
        // Upgrade the draft in the same history transaction as the edit, never merely on opening a legacy profile.
        definition.SchemaVersion = 3;
        definition.ColorLookup = reference;
        definition.ColorLookupWeight = weight;
        return Edit("Change color lookup", definition);
    }
    bool PostProcessDocument::SetCustomEffects(
        Keire::PostProcessOverride<std::vector<Keire::PostProcessCustomEffect>> effects)
    {
        auto definition = Definition();
        if (definition.CustomEffects == effects)
        {
            return false;
        }
        for (const auto& effect : effects.Value)
        {
            if (effect.Material == m_Host.Asset())
            {
                throw std::invalid_argument("A profile cannot reference itself as a custom effect material.");
            }
        }
        definition.SchemaVersion = 3;
        definition.CustomEffects = std::move(effects);
        return Edit("Change custom effects", definition);
    }
} // namespace KeireEditor
