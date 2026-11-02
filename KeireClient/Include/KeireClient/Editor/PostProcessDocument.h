#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"
#include "KeireClient/Editor/AssetDocumentHost.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireEditor
{
    class PostProcessDocument final
    {
      public:
        using Host = AssetDocumentHost<Keire::PostProcessProfileDefinition>;
        using Specification = Host::Specification;

        explicit PostProcessDocument(Specification specification);
        ~PostProcessDocument();

        void Open(Keire::AssetId asset, std::span<const std::byte> bytes, std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {});
        [[nodiscard]] AssetDocumentReloadResult Reload(std::span<const std::byte> bytes, std::uint64_t revision);
        void Save();
        void Discard();
        void Close() noexcept;
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        [[nodiscard]] bool Edit(std::string_view label, const Keire::PostProcessProfileDefinition& replacement);

        // Samples affect live preview and dirty state; only Commit records the final value in history.
        // Cancel restores the definition from Begin, including inherited effect activation.
        void BeginContinuousEdit(std::string_view label);
        [[nodiscard]] bool UpdateContinuousEdit(const Keire::PostProcessProfileDefinition& replacement);
        void CommitContinuousEdit();
        void CancelContinuousEdit();
        [[nodiscard]] bool ContinuousEditActive() const noexcept { return m_ContinuousDraft.has_value(); }
        [[nodiscard]] bool SetParameter(Keire::PostProcessParameter parameter,
                                        Keire::PostProcessOverride<double> value);
        [[nodiscard]] bool SetEffect(Keire::PostProcessEffect effect, Keire::PostProcessOverride<bool> value);
        [[nodiscard]] bool SetColorLookup(Keire::PostProcessOverride<Keire::AssetId> reference,
                                          Keire::PostProcessOverride<double> weight);
        [[nodiscard]] bool
        SetCustomEffects(Keire::PostProcessOverride<std::vector<Keire::PostProcessCustomEffect>> effects);
        [[nodiscard]] bool ResetParameter(Keire::PostProcessParameter parameter);

        [[nodiscard]] bool IsOpen() const noexcept { return m_Host.IsOpen(); }

        [[nodiscard]] bool Dirty() const noexcept
        {
            return m_ContinuousDraft ? *m_ContinuousDraft != m_Host.Baseline() : m_Host.Dirty();
        }

        [[nodiscard]] bool PreservesLivePreview(Keire::AssetId asset) const noexcept
        {
            return IsOpen() && Asset() == asset && Dirty();
        }

        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Host.Asset(); }

        [[nodiscard]] const Keire::PostProcessProfileDefinition& Definition() const
        {
            return m_ContinuousDraft ? *m_ContinuousDraft : m_Host.Draft();
        }

        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Host.UndoContext(); }

        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Host.Diagnostic(); }

      private:
        Host m_Host;
        std::optional<Keire::PostProcessProfileDefinition> m_ContinuousDraft;
        std::string m_ContinuousLabel;
        std::unique_ptr<Keire::UndoTransaction> m_ContinuousTransaction;
    };
} // namespace KeireEditor
