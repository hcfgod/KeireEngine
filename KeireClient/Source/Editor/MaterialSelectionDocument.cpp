#include "KeireClient/Editor/MaterialSelectionDocument.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        const Keire::ShaderPropertyDefinition* Match(const MaterialDocument& document,
                                                     const Keire::ShaderPropertyDefinition& property)
        {
            const auto declarations = document.Properties();
            const auto found =
                std::ranges::find_if(declarations,
                                     [&](const auto& candidate)
                                     {
                                         return candidate.Type == property.Type &&
                                                (property.Id ? candidate.Id == property.Id
                                                             : !candidate.Id && candidate.Name == property.Name);
                                     });
            return found == declarations.end() ? nullptr : &*found;
        }

        bool SameSources(const std::span<const MaterialDocument> left, const std::span<const MaterialDocument> right)
        {
            return std::ranges::equal(left, right,
                                      [](const auto& a, const auto& b) { return a.SaveSource() == b.SaveSource(); });
        }
    } // namespace

    MaterialSelectionDocument::MaterialSelectionDocument(std::vector<MaterialDocument> documents, Publisher publish)
        : m_Documents(std::move(documents)), m_Publish(std::move(publish))
    {
        if (m_Documents.empty())
            throw std::invalid_argument("Select at least one material.");
        std::set<Keire::AssetId> assets;
        for (const auto& document : m_Documents)
            if (document.Asset() && !assets.insert(document.Asset()).second)
                throw std::invalid_argument("A material selection cannot contain duplicate assets.");
    }

    std::optional<MaterialSelectionDocument::PropertyState>
    MaterialSelectionDocument::Property(const Keire::ShaderPropertyDefinition& property) const
    {
        std::optional<PropertyState> result;
        for (const auto& document : m_Documents)
        {
            const auto* declared = Match(document, property);
            if (!declared)
                return std::nullopt;
            const auto value = document.Property(declared->Name);
            if (!result)
                result = PropertyState{value, false};
            else
                result->Mixed |= result->Value != value;
        }
        return result;
    }

    bool MaterialSelectionDocument::SetProperty(const Keire::ShaderPropertyDefinition& property,
                                                const Keire::MaterialPropertyValue& value)
    {
        auto replacement = m_Documents;
        for (auto& document : replacement)
        {
            const auto* declared = Match(document, property);
            if (!declared)
                throw std::invalid_argument("The property is not common to every selected material.");
            (void)document.SetProperty(declared->Name, value);
        }
        return Commit(std::move(replacement));
    }

    bool MaterialSelectionDocument::ResetProperty(const Keire::ShaderPropertyDefinition& property)
    {
        auto replacement = m_Documents;
        for (auto& document : replacement)
        {
            const auto* declared = Match(document, property);
            if (!declared)
                throw std::invalid_argument("The property is not common to every selected material.");
            (void)document.ResetProperty(declared->Name);
        }
        return Commit(std::move(replacement));
    }

    bool MaterialSelectionDocument::SetTextureTransform(const Keire::ShaderPropertyDefinition& property,
                                                         std::optional<Keire::Vector2> tiling,
                                                         std::optional<Keire::Vector2> offset)
    {
        if (property.Type != Keire::ShaderPropertyType::Vector4)
            throw std::invalid_argument("Texture transforms require a Vector4 property.");
        auto replacement = m_Documents;
        for (auto& document : replacement)
        {
            const auto* declared = Match(document, property);
            if (!declared)
                throw std::invalid_argument("The transform is not common to every selected material.");
            auto value = std::get<Keire::Vector4>(document.Property(declared->Name));
            if (tiling)
            {
                value.X = tiling->X;
                value.Y = tiling->Y;
            }
            if (offset)
            {
                value.Z = offset->X;
                value.W = offset->Y;
            }
            if (tiling || offset)
                (void)document.SetProperty(declared->Name, value);
        }
        return Commit(std::move(replacement));
    }

    bool MaterialSelectionDocument::SetSurface(std::optional<Keire::MaterialAlphaMode> alphaMode,
                                               std::optional<float> alphaCutoff, std::optional<bool> doubleSided)
    {
        auto replacement = m_Documents;
        for (auto& document : replacement)
        {
            auto surface = document.Surface();
            if (alphaMode)
                surface.AlphaMode = *alphaMode;
            if (alphaCutoff)
                surface.AlphaCutoff = *alphaCutoff;
            if (doubleSided)
                surface.DoubleSided = *doubleSided;
            (void)document.SetSurface(surface);
        }
        return Commit(std::move(replacement));
    }

    bool MaterialSelectionDocument::Commit(Snapshot replacement)
    {
        if (SameSources(m_Documents, replacement))
            return false;
        for (auto& document : replacement)
        {
            document.CaptureDraft();
            if (m_Publish)
                document.AcceptSavedSource(document.SaveSource());
        }
        m_Undo.reserve(m_Undo.size() + 1);
        if (m_Publish)
            m_Publish(m_Documents, replacement);
        m_Undo.push_back(std::move(m_Documents));
        m_Documents = std::move(replacement);
        m_Redo.clear();
        return true;
    }

    bool MaterialSelectionDocument::Undo()
    {
        if (!CanUndo())
            return false;
        m_Redo.reserve(m_Redo.size() + 1);
        if (m_Publish)
            m_Publish(m_Documents, m_Undo.back());
        m_Redo.push_back(std::move(m_Documents));
        m_Documents = std::move(m_Undo.back());
        m_Undo.pop_back();
        return true;
    }

    bool MaterialSelectionDocument::Redo()
    {
        if (!CanRedo())
            return false;
        m_Undo.reserve(m_Undo.size() + 1);
        if (m_Publish)
            m_Publish(m_Documents, m_Redo.back());
        m_Undo.push_back(std::move(m_Documents));
        m_Documents = std::move(m_Redo.back());
        m_Redo.pop_back();
        return true;
    }
} // namespace KeireEditor
