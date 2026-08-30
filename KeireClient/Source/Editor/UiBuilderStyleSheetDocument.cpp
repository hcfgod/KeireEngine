#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::span<const std::byte> Bytes(const std::string_view value) noexcept
        {
            return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
        }
    } // namespace

    void UiBuilderStyleSheetDocument::Open(const Keire::AssetId asset, Keire::UiStyleSheetDefinition definition,
                                           const std::uint64_t revision, std::filesystem::path source,
                                           Keire::Ref<Keire::UndoContext> undo)
    {
        if (!asset || revision == 0 || source.empty())
            throw std::invalid_argument("Opening a UI style sheet requires an asset, revision, and source path.");
        Keire::UiStyleSheetAsset::Validate(definition);
        Close();
        m_Asset = asset;
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        m_Selection = m_Definition.Rules.empty() ? std::nullopt : std::optional<std::size_t>(0);
        m_Revision = revision;
        m_Generation = 1;
        m_Source = std::move(source);
        m_Undo = std::move(undo);
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Clear();
    }

    void UiBuilderStyleSheetDocument::Close() noexcept
    {
        m_Asset = {};
        m_Definition = {};
        m_Baseline = {};
        m_Selection.reset();
        m_Revision = 0;
        m_Generation = 0;
        m_Source.clear();
        m_Undo.Reset();
        m_Dirty = false;
    }

    void UiBuilderStyleSheetDocument::Select(const std::optional<std::size_t> rule) noexcept
    {
        m_Selection = rule && *rule < m_Definition.Rules.size() ? rule : std::nullopt;
    }

    bool UiBuilderStyleSheetDocument::AddRule(const std::string_view selector, const std::string_view declarations)
    {
        auto candidate = m_Definition;
        candidate.Rules.push_back(ParseRule(selector, declarations));
        if (!Edit("Add UI style rule", std::move(candidate)))
            return false;
        m_Selection = m_Definition.Rules.size() - 1;
        return true;
    }

    bool UiBuilderStyleSheetDocument::EditRule(const std::size_t rule, const std::string_view selector,
                                               const std::string_view declarations)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        candidate.Rules[rule] = ParseRule(selector, declarations);
        if (!Edit("Edit UI style rule", std::move(candidate)))
            return false;
        m_Selection = rule;
        return true;
    }

    bool UiBuilderStyleSheetDocument::RemoveRule(const std::size_t rule)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        candidate.Rules.erase(candidate.Rules.begin() + static_cast<std::ptrdiff_t>(rule));
        if (!Edit("Remove UI style rule", std::move(candidate)))
            return false;
        m_Selection =
            m_Definition.Rules.empty() ? std::nullopt : std::optional(std::min(rule, m_Definition.Rules.size() - 1));
        return true;
    }

    std::string UiBuilderStyleSheetDocument::RuleDeclarations(const std::size_t rule) const
    {
        if (rule >= m_Definition.Rules.size())
            return {};
        std::string result;
        for (const auto& property : m_Definition.Rules[rule].Properties)
        {
            if (!result.empty())
                result += '\n';
            result += property.Name + ": " + property.Value + ';';
        }
        return result;
    }

    std::string UiBuilderStyleSheetDocument::SourcePreview() const
    {
        if (!m_Asset)
            return {};
        const auto bytes = Keire::UiStyleSheetAsset::EncodeSource(m_Definition);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    void UiBuilderStyleSheetDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI style sheet before saving it.");
        const auto bytes = Keire::UiStyleSheetAsset::EncodeSource(m_Definition);
        Keire::Detail::WriteTextFileAtomically(m_Source, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        m_Baseline = m_Definition;
        m_Dirty = false;
        AdvanceGeneration();
    }

    void UiBuilderStyleSheetDocument::ReloadFromSource(const bool discardLocalChanges)
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI style sheet before reloading it.");
        if (m_Dirty && !discardLocalChanges)
            throw std::logic_error("The UI style sheet has unsaved changes. Revert explicitly to discard them.");
        const auto source = Keire::Detail::ReadTextFile(m_Source, Keire::MaximumUiDocumentBytes);
        auto definition = Keire::UiStyleSheetAsset::ParseSource(Bytes(source));
        Keire::UiStyleSheetAsset::Validate(definition);
        m_Definition = std::move(definition);
        m_Baseline = m_Definition;
        m_Selection = m_Definition.Rules.empty() ? std::nullopt : std::optional<std::size_t>(0);
        m_Dirty = false;
        AdvanceGeneration();
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Clear();
    }

    bool UiBuilderStyleSheetDocument::Undo() { return m_Undo && m_Undo->Undo(); }

    bool UiBuilderStyleSheetDocument::Redo() { return m_Undo && m_Undo->Redo(); }

    Keire::UiStyleRuleDefinition UiBuilderStyleSheetDocument::ParseRule(const std::string_view selector,
                                                                        const std::string_view declarations)
    {
        if (selector.empty())
            throw std::invalid_argument("A UI style rule requires a selector.");
        std::string source = "@keire-style 1;\n";
        source += selector;
        source += " {\n";
        source += declarations;
        source += "\n}\n";
        auto parsed = Keire::UiStyleSheetAsset::ParseSource(Bytes(source));
        if (parsed.Rules.size() != 1)
            throw std::invalid_argument("A UI style edit must describe exactly one selector rule.");
        return std::move(parsed.Rules.front());
    }

    bool UiBuilderStyleSheetDocument::Edit(const std::string_view name, Keire::UiStyleSheetDefinition candidate)
    {
        if (!m_Asset)
            throw std::logic_error("Open a UI style sheet before editing it.");
        Keire::UiStyleSheetAsset::Validate(candidate);
        if (Keire::UiStyleSheetAsset::Encode(candidate) == Keire::UiStyleSheetAsset::Encode(m_Definition))
            return false;
        auto before = m_Definition;
        m_Definition = std::move(candidate);
        RecordApplied(name, std::move(before));
        m_Dirty = true;
        AdvanceGeneration();
        return true;
    }

    void UiBuilderStyleSheetDocument::RecordApplied(const std::string_view name, Keire::UiStyleSheetDefinition before)
    {
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<Keire::UiStyleSheetDefinition>>();
        const auto asset = m_Asset;
        m_Undo->RecordApplied(Keire::CreateUndoCommand(
            std::string(name),
            [this, after, asset]
            {
                if (m_Asset != asset || !after->has_value())
                    return;
                m_Definition = **after;
                Select(m_Selection);
                m_Dirty = true;
                AdvanceGeneration();
            },
            [this, after, before = std::move(before), asset]() mutable
            {
                if (m_Asset != asset)
                    return;
                *after = std::move(m_Definition);
                m_Definition = before;
                Select(m_Selection);
                m_Dirty = true;
                AdvanceGeneration();
            },
            sizeof(Keire::UiStyleSheetDefinition)));
    }

    void UiBuilderStyleSheetDocument::AdvanceGeneration() noexcept
    {
        if (++m_Generation == 0)
            ++m_Generation;
    }
} // namespace KeireEditor
