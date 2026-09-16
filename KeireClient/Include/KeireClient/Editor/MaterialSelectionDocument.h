#pragma once

#include "KeireClient/Editor/MaterialDocument.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace KeireEditor
{
    /// Owns an Inspector selection. Publication must atomically persist the complete before/after set or throw.
    class MaterialSelectionDocument final
    {
      public:
        using Publisher = std::function<void(std::span<const MaterialDocument>, std::span<const MaterialDocument>)>;
        struct PropertyState final
        {
            Keire::MaterialPropertyValue Value;
            bool Mixed = false;
        };

        explicit MaterialSelectionDocument(std::vector<MaterialDocument> documents, Publisher publish = {});
        [[nodiscard]] std::span<const MaterialDocument> Documents() const noexcept { return m_Documents; }
        [[nodiscard]] std::optional<PropertyState> Property(const Keire::ShaderPropertyDefinition& property) const;
        [[nodiscard]] bool SetProperty(const Keire::ShaderPropertyDefinition& property,
                                       const Keire::MaterialPropertyValue& value);
        [[nodiscard]] bool ResetProperty(const Keire::ShaderPropertyDefinition& property);
        /// Edits the transform's xy tiling or zw offset while retaining each material's untouched pair.
        [[nodiscard]] bool SetTextureTransform(const Keire::ShaderPropertyDefinition& property,
                                               std::optional<Keire::Vector2> tiling,
                                               std::optional<Keire::Vector2> offset);
        [[nodiscard]] bool SetSurface(std::optional<Keire::MaterialAlphaMode> alphaMode,
                                      std::optional<float> alphaCutoff, std::optional<bool> doubleSided);
        [[nodiscard]] bool CanUndo() const noexcept { return !m_Undo.empty(); }
        [[nodiscard]] bool CanRedo() const noexcept { return !m_Redo.empty(); }
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

      private:
        using Snapshot = std::vector<MaterialDocument>;
        [[nodiscard]] bool Commit(Snapshot replacement);
        Snapshot m_Documents;
        std::vector<Snapshot> m_Undo;
        std::vector<Snapshot> m_Redo;
        Publisher m_Publish;
    };
} // namespace KeireEditor
