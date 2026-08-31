#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"

#include "Keire/Ui/UiStyleProperties.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <functional>
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

        [[nodiscard]] std::vector<std::string_view> Lines(const std::string_view source)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor <= source.size())
            {
                const auto end = source.find('\n', cursor);
                auto line = source.substr(cursor, end - cursor);
                if (line.ends_with('\r'))
                    line.remove_suffix(1U);
                result.push_back(line);
                if (end == std::string_view::npos)
                    break;
                cursor = end + 1U;
            }
            return result;
        }

        void AppendComparisonLine(std::string& output, const char marker, const std::size_t line,
                                  std::string_view source)
        {
            constexpr std::size_t maximumLineBytes = 1'024U;
            output += marker;
            output += std::to_string(line);
            output += " | ";
            output.append(source.substr(0, maximumLineBytes));
            if (source.size() > maximumLineBytes)
                output += " ...";
            output += '\n';
        }

        struct StylePropertySpan final
        {
            std::string Name;
            std::size_t Begin = 0;
            std::size_t ValueBegin = 0;
            std::size_t ValueEnd = 0;
            std::size_t End = 0;
        };

        struct StyleRuleSpan final
        {
            std::size_t SelectorBegin = 0;
            std::size_t SelectorEnd = 0;
            std::size_t BodyBegin = 0;
            std::size_t BodyEnd = 0;
            std::size_t End = 0;
            std::size_t OuterBegin = 0;
            std::size_t OuterEnd = 0;
            std::optional<std::pair<std::size_t, std::size_t>> MediaHeader;
            std::vector<StylePropertySpan> Properties;
        };

        struct StyleMediaSpan final
        {
            std::size_t Begin = 0;
            std::size_t HeaderEnd = 0;
            std::size_t End = 0;
        };

        [[nodiscard]] std::size_t SkipTrivia(const std::string_view source, std::size_t cursor,
                                             const std::size_t end) noexcept
        {
            while (cursor < end)
            {
                if (std::isspace(static_cast<unsigned char>(source[cursor])))
                {
                    ++cursor;
                    continue;
                }
                if (cursor + 1 < end && source[cursor] == '/' && source[cursor + 1] == '*')
                {
                    const auto close = source.find("*/", cursor + 2);
                    cursor = close == std::string_view::npos || close + 2 > end ? end : close + 2;
                    continue;
                }
                break;
            }
            return cursor;
        }

        [[nodiscard]] std::size_t MatchingBrace(const std::string_view source, const std::size_t open,
                                                const std::size_t end) noexcept
        {
            std::size_t depth = 0;
            bool quoted = false;
            char quote = 0;
            for (std::size_t cursor = open; cursor < end; ++cursor)
            {
                if (!quoted && cursor + 1 < end && source[cursor] == '/' && source[cursor + 1] == '*')
                {
                    const auto close = source.find("*/", cursor + 2);
                    if (close == std::string_view::npos)
                        return std::string_view::npos;
                    cursor = close + 1;
                    continue;
                }
                if (source[cursor] == '"' || source[cursor] == '\'')
                {
                    if (!quoted)
                    {
                        quoted = true;
                        quote = source[cursor];
                    }
                    else if (source[cursor] == quote && (cursor == 0 || source[cursor - 1] != '\\'))
                        quoted = false;
                    continue;
                }
                if (quoted)
                    continue;
                if (source[cursor] == '{')
                    ++depth;
                else if (source[cursor] == '}' && --depth == 0)
                    return cursor;
            }
            return std::string_view::npos;
        }

        [[nodiscard]] std::vector<StylePropertySpan> PropertySpans(const std::string_view source,
                                                                   const std::size_t begin, const std::size_t end)
        {
            std::vector<StylePropertySpan> result;
            std::size_t cursor = begin;
            while ((cursor = SkipTrivia(source, cursor, end)) < end)
            {
                const auto semicolon = source.find(';', cursor);
                const auto declarationEnd = semicolon == std::string_view::npos || semicolon > end ? end : semicolon;
                const auto colon = source.find(':', cursor);
                if (colon == std::string_view::npos || colon >= declarationEnd)
                    break;
                auto nameEnd = colon;
                while (nameEnd > cursor && std::isspace(static_cast<unsigned char>(source[nameEnd - 1])))
                    --nameEnd;
                auto valueBegin = colon + 1;
                while (valueBegin < declarationEnd && std::isspace(static_cast<unsigned char>(source[valueBegin])))
                    ++valueBegin;
                auto valueEnd = declarationEnd;
                while (valueEnd > valueBegin && std::isspace(static_cast<unsigned char>(source[valueEnd - 1])))
                    --valueEnd;
                result.push_back({std::string(source.substr(cursor, nameEnd - cursor)), cursor, valueBegin, valueEnd,
                                  declarationEnd + (semicolon == declarationEnd ? 1 : 0)});
                cursor = declarationEnd + (semicolon == declarationEnd ? 1 : 0);
            }
            return result;
        }

        [[nodiscard]] std::vector<StyleRuleSpan> RuleSpans(const std::string_view source)
        {
            std::vector<StyleRuleSpan> result;
            const auto header = source.find(';');
            if (header == std::string_view::npos)
                return result;
            std::function<void(std::size_t, std::size_t, std::optional<StyleMediaSpan>)> scan =
                [&](const std::size_t begin, const std::size_t end, const std::optional<StyleMediaSpan> media)
            {
                auto cursor = begin;
                while ((cursor = SkipTrivia(source, cursor, end)) < end)
                {
                    const auto open = source.find('{', cursor);
                    if (open == std::string_view::npos || open >= end)
                        break;
                    auto selectorEnd = open;
                    while (selectorEnd > cursor && std::isspace(static_cast<unsigned char>(source[selectorEnd - 1])))
                        --selectorEnd;
                    const auto close = MatchingBrace(source, open, end);
                    if (close == std::string_view::npos)
                        return;
                    const auto headerText = source.substr(cursor, selectorEnd - cursor);
                    if (headerText.starts_with("@media"))
                        scan(open + 1, close, StyleMediaSpan{cursor, selectorEnd, close + 1});
                    else
                        result.push_back(
                            {cursor, selectorEnd, open + 1, close, close + 1, media ? media->Begin : cursor,
                             media ? media->End : close + 1,
                             media ? std::optional(std::pair{media->Begin, media->HeaderEnd}) : std::nullopt,
                             PropertySpans(source, open + 1, close)});
                    cursor = close + 1;
                }
            };
            scan(header + 1, source.size(), std::nullopt);
            return result;
        }

        [[nodiscard]] std::string IndentRule(std::string_view source)
        {
            std::string result = "  ";
            std::size_t cursor = 0;
            while (cursor < source.size())
            {
                const auto end = source.find('\n', cursor);
                result.append(source.substr(cursor, end - cursor));
                if (end == std::string_view::npos)
                    break;
                result += "\n  ";
                cursor = end + 1;
            }
            return result;
        }

        [[nodiscard]] std::size_t MediaRuleCount(const std::span<const StyleRuleSpan> rules,
                                                 const StyleRuleSpan& selected) noexcept
        {
            if (!selected.MediaHeader)
                return 0;
            return static_cast<std::size_t>(std::ranges::count_if(rules,
                                                                  [&selected](const StyleRuleSpan& rule)
                                                                  {
                                                                      return rule.MediaHeader &&
                                                                             rule.OuterBegin == selected.OuterBegin &&
                                                                             rule.OuterEnd == selected.OuterEnd;
                                                                  }));
        }

        [[nodiscard]] std::optional<std::string> ReplacePropertyValue(const std::string_view source,
                                                                      const std::size_t rule,
                                                                      const std::string_view name,
                                                                      const std::string_view value)
        {
            const auto rules = RuleSpans(source);
            if (rule >= rules.size())
                return std::nullopt;
            const auto found = std::ranges::find(rules[rule].Properties, name, &StylePropertySpan::Name);
            std::string result(source);
            if (found != rules[rule].Properties.end())
                result.replace(found->ValueBegin, found->ValueEnd - found->ValueBegin, value);
            else
                result.insert(rules[rule].BodyEnd, "\n  " + std::string(name) + ": " + std::string(value) + ";");
            return result;
        }

        void ReplaceSchemaVersion(std::string& source, const std::uint32_t schemaVersion)
        {
            constexpr std::string_view directive = "@keire-style";
            const auto directiveBegin = source.find(directive);
            if (directiveBegin == std::string::npos)
                throw std::runtime_error("UI stylesheet source is missing its schema directive.");
            const auto valueBegin = source.find_first_not_of(" \t", directiveBegin + directive.size());
            const auto semicolon = source.find(';', valueBegin);
            if (valueBegin == std::string::npos || semicolon == std::string::npos)
                throw std::runtime_error("UI stylesheet schema directive is malformed.");
            auto valueEnd = semicolon;
            while (valueEnd > valueBegin && std::isspace(static_cast<unsigned char>(source[valueEnd - 1])))
                --valueEnd;
            source.replace(valueBegin, valueEnd - valueBegin, std::to_string(schemaVersion));
        }

        class ContinuousStyleUndoCommand final : public Keire::UndoCommand
        {
          public:
            ContinuousStyleUndoCommand(std::string name, std::string mergeKey, Keire::UndoOperation redo,
                                       Keire::UndoOperation undo)
                : m_Name(std::move(name)), m_MergeKey(std::move(mergeKey)), m_Redo(std::move(redo)),
                  m_Undo(std::move(undo))
            {
            }

            [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return 1024; }
            void Redo() override { m_Redo(); }
            void Undo() override { m_Undo(); }
            [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
            {
                const auto* command = dynamic_cast<const ContinuousStyleUndoCommand*>(&newer);
                return command && !m_MergeKey.empty() && command->m_MergeKey == m_MergeKey;
            }

          private:
            std::string m_Name;
            std::string m_MergeKey;
            Keire::UndoOperation m_Redo;
            Keire::UndoOperation m_Undo;
        };
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
        m_SourceText = Keire::Detail::ReadTextFile(m_Source, Keire::MaximumUiDocumentBytes);
        m_BaselineSource = m_SourceText;
        m_SourceDiagnostic.reset();
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
        m_SourceText.clear();
        m_BaselineSource.clear();
        m_SourceDiagnostic.reset();
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
        candidate.Rules.push_back(ParseRule(selector, declarations, candidate.SchemaVersion));
        auto source = m_SourceText;
        if (!source.empty() && source.back() != '\n')
            source += '\n';
        source += "\n" + std::string(selector) + " {\n" + std::string(declarations) + "\n}\n";
        if (!Edit("Add UI style rule", std::move(candidate), std::move(source)))
            return false;
        m_Selection = m_Definition.Rules.size() - 1;
        return true;
    }

    bool UiBuilderStyleSheetDocument::EditRule(const std::size_t rule, const std::string_view selector,
                                               const std::string_view declarations)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        if (declarations == RuleDeclarations(rule))
            return SetSelector(rule, selector);
        auto candidate = m_Definition;
        auto replacement = ParseRule(selector, declarations, candidate.SchemaVersion);
        replacement.Media = candidate.Rules[rule].Media;
        candidate.Rules[rule] = std::move(replacement);
        if (!Edit("Edit UI style rule", std::move(candidate)))
            return false;
        m_Selection = rule;
        return true;
    }

    bool UiBuilderStyleSheetDocument::SetSelector(const std::size_t rule, const std::string_view selector)
    {
        if (rule >= m_Definition.Rules.size() || selector.empty())
            return false;
        auto candidate = m_Definition;
        auto replacement = ParseRule(selector, RuleDeclarations(rule), candidate.SchemaVersion);
        replacement.Media = candidate.Rules[rule].Media;
        candidate.Rules[rule].Selector = std::move(replacement.Selector);
        candidate.Rules[rule].Parts = std::move(replacement.Parts);
        candidate.Rules[rule].Specificity = replacement.Specificity;
        const auto rules = RuleSpans(m_SourceText);
        if (rule >= rules.size())
            return Edit("Edit UI style selector", std::move(candidate));
        auto source = m_SourceText;
        source.replace(rules[rule].SelectorBegin, rules[rule].SelectorEnd - rules[rule].SelectorBegin, selector);
        return Edit("Edit UI style selector", std::move(candidate), std::move(source));
    }

    bool UiBuilderStyleSheetDocument::SetMediaCondition(const std::size_t rule,
                                                        std::optional<Keire::UiStyleMediaCondition> condition)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        if (condition)
        {
            if (condition->Empty())
                throw std::invalid_argument("A responsive UI style rule requires at least one condition.");
            candidate.SchemaVersion = std::max(candidate.SchemaVersion, 2U);
        }
        candidate.Rules[rule].Media = condition;
        const auto rules = RuleSpans(m_SourceText);
        if (rule >= rules.size())
            return Edit("Edit UI style responsive condition", std::move(candidate));

        auto source = m_SourceText;
        const auto& selected = rules[rule];
        if (selected.MediaHeader && condition)
        {
            const auto header = "@media " + Keire::EncodeUiStyleMediaCondition(*condition);
            source.replace(selected.MediaHeader->first, selected.MediaHeader->second - selected.MediaHeader->first,
                           header);
        }
        else if (!selected.MediaHeader && condition)
        {
            const auto ruleSource = source.substr(selected.SelectorBegin, selected.End - selected.SelectorBegin);
            const auto wrapped =
                "@media " + Keire::EncodeUiStyleMediaCondition(*condition) + "\n{\n" + IndentRule(ruleSource) + "\n}";
            source.replace(selected.SelectorBegin, selected.End - selected.SelectorBegin, wrapped);
        }
        else if (selected.MediaHeader && !condition && MediaRuleCount(rules, selected) == 1U)
        {
            const auto ruleSource = source.substr(selected.SelectorBegin, selected.End - selected.SelectorBegin);
            source.replace(selected.OuterBegin, selected.OuterEnd - selected.OuterBegin, ruleSource);
        }
        else
        {
            return Edit("Edit UI style responsive condition", std::move(candidate));
        }
        if (candidate.SchemaVersion != m_Definition.SchemaVersion)
            ReplaceSchemaVersion(source, candidate.SchemaVersion);
        return Edit("Edit UI style responsive condition", std::move(candidate), std::move(source));
    }

    bool UiBuilderStyleSheetDocument::SetProperty(const std::size_t rule, const std::string_view name,
                                                  const std::string_view value)
    {
        if (rule >= m_Definition.Rules.size() || name.empty() || value.empty())
            return false;
        auto candidate = m_Definition;
        const auto previousSchemaVersion = candidate.SchemaVersion;
        const bool legacyGradientAlias = name == "background-image" && (value.starts_with("linear-gradient(") ||
                                                                        value.starts_with("radial-gradient("));
        if (const auto* descriptor = Keire::FindUiStylePropertyDescriptor(name); descriptor && !legacyGradientAlias)
            candidate.SchemaVersion = std::max(candidate.SchemaVersion, descriptor->MinimumSchemaVersion);
        auto& properties = candidate.Rules[rule].Properties;
        const auto found = std::ranges::find(properties, name, &Keire::UiNamedValue::Name);
        if (found == properties.end())
            properties.push_back({std::string(name), std::string(value)});
        else
            found->Value = value;
        if (!m_Asset)
            throw std::logic_error("Open a UI style sheet before editing it.");
        Keire::UiStyleSheetAsset::Validate(candidate);
        if (Keire::UiStyleSheetAsset::Encode(candidate) == Keire::UiStyleSheetAsset::Encode(m_Definition))
            return false;
        auto before = State();
        m_Definition = std::move(candidate);
        if (auto source = ReplacePropertyValue(m_SourceText, rule, name, value))
            m_SourceText = std::move(*source);
        else
        {
            const auto encoded = Keire::UiStyleSheetAsset::EncodeSource(m_Definition);
            m_SourceText.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
        if (m_Definition.SchemaVersion != previousSchemaVersion)
            ReplaceSchemaVersion(m_SourceText, m_Definition.SchemaVersion);
        m_SourceDiagnostic.reset();
        RecordApplied("Edit UI style property", std::move(before),
                      m_Asset.ToString() + ':' + std::to_string(rule) + ':' + std::string(name));
        m_Dirty = true;
        AdvanceGeneration();
        return true;
    }

    bool UiBuilderStyleSheetDocument::RemoveProperty(const std::size_t rule, const std::string_view name)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        auto& properties = candidate.Rules[rule].Properties;
        const auto removed = std::erase_if(properties, [name](const auto& property) { return property.Name == name; });
        if (removed == 0)
            return false;
        const auto rules = RuleSpans(m_SourceText);
        if (rule >= rules.size())
            return Edit("Remove UI style property", std::move(candidate));
        const auto found = std::ranges::find(rules[rule].Properties, name, &StylePropertySpan::Name);
        if (found == rules[rule].Properties.end())
            return Edit("Remove UI style property", std::move(candidate));
        auto source = m_SourceText;
        source.erase(found->Begin, found->End - found->Begin);
        return Edit("Remove UI style property", std::move(candidate), std::move(source));
    }

    bool UiBuilderStyleSheetDocument::SetToken(const std::string_view name, const std::string_view value)
    {
        if (!name.starts_with("--") || name.size() < 3 || value.empty())
            throw std::invalid_argument("UI design-token names must begin with '--' and have a value.");
        for (std::size_t rule = 0; rule < m_Definition.Rules.size(); ++rule)
            if (std::ranges::find(m_Definition.Rules[rule].Properties, name, &Keire::UiNamedValue::Name) !=
                m_Definition.Rules[rule].Properties.end())
                return SetProperty(rule, name, value);
        const auto root = std::ranges::find(m_Definition.Rules, ":root", &Keire::UiStyleRuleDefinition::Selector);
        if (root != m_Definition.Rules.end())
            return SetProperty(static_cast<std::size_t>(std::distance(m_Definition.Rules.begin(), root)), name, value);
        if (!AddRule(":root", std::string(name) + ": " + std::string(value) + ';'))
            return false;
        return true;
    }

    bool UiBuilderStyleSheetDocument::RenameToken(const std::string_view currentName,
                                                  const std::string_view replacementName)
    {
        if (!currentName.starts_with("--") || !replacementName.starts_with("--") || replacementName.size() < 3)
            throw std::invalid_argument("UI design-token names must begin with '--'.");
        if (currentName == replacementName)
            return false;
        auto source = m_SourceText;
        std::size_t cursor = 0;
        bool replaced = false;
        while ((cursor = source.find(currentName, cursor)) != std::string::npos)
        {
            const auto end = cursor + currentName.size();
            const bool leftBoundary = cursor == 0 || !std::isalnum(static_cast<unsigned char>(source[cursor - 1]));
            const bool rightBoundary =
                end == source.size() ||
                (!std::isalnum(static_cast<unsigned char>(source[end])) && source[end] != '-' && source[end] != '_');
            if (leftBoundary && rightBoundary)
            {
                source.replace(cursor, currentName.size(), replacementName);
                cursor += replacementName.size();
                replaced = true;
            }
            else
                cursor = end;
        }
        if (!replaced)
            return false;
        auto candidate = Keire::UiStyleSheetAsset::ParseSource(Bytes(source));
        return Edit("Rename UI design token", std::move(candidate), std::move(source));
    }

    bool UiBuilderStyleSheetDocument::DuplicateRule(const std::size_t rule)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        candidate.Rules.insert(candidate.Rules.begin() + static_cast<std::ptrdiff_t>(rule + 1), candidate.Rules[rule]);
        const auto rules = RuleSpans(m_SourceText);
        std::optional<std::string> source;
        if (rule < rules.size())
        {
            source = m_SourceText;
            const auto& selected = rules[rule];
            std::string duplicate;
            std::size_t insert = selected.End;
            if (selected.MediaHeader)
            {
                const auto ruleSource =
                    m_SourceText.substr(selected.SelectorBegin, selected.End - selected.SelectorBegin);
                duplicate = "@media " + Keire::EncodeUiStyleMediaCondition(*candidate.Rules[rule].Media) + "\n{\n" +
                            IndentRule(ruleSource) + "\n}";
                insert = selected.OuterEnd;
            }
            else
            {
                duplicate = m_SourceText.substr(selected.SelectorBegin, selected.End - selected.SelectorBegin);
            }
            source->insert(insert, "\n\n" + duplicate);
        }
        if (!Edit("Duplicate UI style rule", std::move(candidate), std::move(source)))
            return false;
        m_Selection = rule + 1;
        return true;
    }

    bool UiBuilderStyleSheetDocument::MoveRule(const std::size_t rule, const std::size_t destination)
    {
        if (rule >= m_Definition.Rules.size() || destination >= m_Definition.Rules.size() || rule == destination)
            return false;
        auto candidate = m_Definition;
        auto moved = std::move(candidate.Rules[rule]);
        candidate.Rules.erase(candidate.Rules.begin() + static_cast<std::ptrdiff_t>(rule));
        candidate.Rules.insert(candidate.Rules.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
        if (!Edit("Reorder UI style rule", std::move(candidate)))
            return false;
        m_Selection = destination;
        return true;
    }

    bool UiBuilderStyleSheetDocument::RemoveRule(const std::size_t rule)
    {
        if (rule >= m_Definition.Rules.size())
            return false;
        auto candidate = m_Definition;
        candidate.Rules.erase(candidate.Rules.begin() + static_cast<std::ptrdiff_t>(rule));
        const auto rules = RuleSpans(m_SourceText);
        std::optional<std::string> source;
        if (rule < rules.size())
        {
            source = m_SourceText;
            const auto& selected = rules[rule];
            if (selected.MediaHeader && MediaRuleCount(rules, selected) == 1U)
                source->erase(selected.OuterBegin, selected.OuterEnd - selected.OuterBegin);
            else
                source->erase(selected.SelectorBegin, selected.End - selected.SelectorBegin);
        }
        if (!Edit("Remove UI style rule", std::move(candidate), std::move(source)))
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

    std::string UiBuilderStyleSheetDocument::SourcePreview() const { return m_Asset ? m_SourceText : std::string{}; }

    bool UiBuilderStyleSheetDocument::ApplySourceDraft(std::string source)
    {
        if (!m_Asset)
            throw std::logic_error("Open a UI style sheet before editing its source.");
        if (source == m_SourceText)
            return false;

        auto before = State();
        m_SourceText = std::move(source);
        try
        {
            auto candidate = Keire::UiStyleSheetAsset::ParseSource(Bytes(m_SourceText));
            Keire::UiStyleSheetAsset::Validate(candidate);
            m_Definition = std::move(candidate);
            m_SourceDiagnostic.reset();
        }
        catch (const std::exception& error)
        {
            m_SourceDiagnostic = Diagnose(m_SourceText, error.what());
        }
        RecordApplied("Edit UI style source", std::move(before), m_Asset.ToString() + ":source");
        m_Dirty = true;
        AdvanceGeneration();
        return true;
    }

    bool UiBuilderStyleSheetDocument::ExternalConflict() const
    {
        if (!m_Asset || m_Source.empty() || !std::filesystem::exists(m_Source))
            return false;
        return Keire::Detail::ReadTextFile(m_Source, Keire::MaximumUiDocumentBytes) != m_BaselineSource;
    }

    std::string UiBuilderStyleSheetDocument::ExternalComparison(const std::size_t maximumLines) const
    {
        if (!m_Asset || m_Source.empty() || maximumLines == 0U || !std::filesystem::exists(m_Source))
            return {};
        const auto external = Keire::Detail::ReadTextFile(m_Source, Keire::MaximumUiDocumentBytes);
        if (external == m_SourceText)
            return "The draft and disk source are identical.\n";
        const auto localLines = Lines(m_SourceText);
        const auto externalLines = Lines(external);
        std::size_t prefix = 0;
        while (prefix < localLines.size() && prefix < externalLines.size() &&
               localLines[prefix] == externalLines[prefix])
        {
            ++prefix;
        }
        std::size_t suffix = 0;
        while (suffix < localLines.size() - prefix && suffix < externalLines.size() - prefix &&
               localLines[localLines.size() - suffix - 1U] == externalLines[externalLines.size() - suffix - 1U])
        {
            ++suffix;
        }

        constexpr std::size_t contextLines = 3U;
        std::string result = "--- unsaved draft\n+++ disk source\n";
        std::size_t emitted = 0;
        const auto contextBegin = prefix > contextLines ? prefix - contextLines : 0U;
        for (std::size_t line = contextBegin; line < prefix && emitted < maximumLines; ++line, ++emitted)
            AppendComparisonLine(result, ' ', line + 1U, localLines[line]);
        const auto localEnd = localLines.size() - suffix;
        for (std::size_t line = prefix; line < localEnd && emitted < maximumLines; ++line, ++emitted)
            AppendComparisonLine(result, '-', line + 1U, localLines[line]);
        const auto externalEnd = externalLines.size() - suffix;
        for (std::size_t line = prefix; line < externalEnd && emitted < maximumLines; ++line, ++emitted)
            AppendComparisonLine(result, '+', line + 1U, externalLines[line]);
        for (std::size_t line = externalEnd;
             line < std::min(externalLines.size(), externalEnd + contextLines) && emitted < maximumLines;
             ++line, ++emitted)
        {
            AppendComparisonLine(result, ' ', line + 1U, externalLines[line]);
        }
        const auto changed = localEnd - prefix + externalEnd - prefix;
        if (emitted < changed)
            result += "... comparison truncated; narrow the edit or inspect the source file for the remaining lines.\n";
        return result;
    }

    void UiBuilderStyleSheetDocument::Save()
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI style sheet before saving it.");
        if (m_SourceDiagnostic)
            throw std::logic_error("Repair the UI style source diagnostic before saving.");
        if (ExternalConflict())
            throw std::logic_error("The UI style sheet changed on disk. Compare or reload it before saving; external "
                                   "changes were preserved.");
        Keire::Detail::WriteTextFileAtomically(m_Source, m_SourceText);
        m_Baseline = m_Definition;
        m_BaselineSource = m_SourceText;
        m_Dirty = false;
        AdvanceGeneration();
    }

    void UiBuilderStyleSheetDocument::SaveAs(const std::filesystem::path& destination) const
    {
        if (!m_Asset || m_Source.empty())
            throw std::logic_error("Open a UI style sheet before saving it as a new asset.");
        if (m_SourceDiagnostic)
            throw std::logic_error("Repair the UI style source diagnostic before saving it as a new asset.");
        if (destination.empty() || destination.extension() != ".keirestyle")
            throw std::invalid_argument("UI style sheet Save As requires a .keirestyle destination.");

        std::error_code error;
        const auto parentStatus = std::filesystem::symlink_status(destination.parent_path(), error);
        if (error || !std::filesystem::is_directory(parentStatus) || std::filesystem::is_symlink(parentStatus))
            throw std::invalid_argument("UI style sheet Save As requires an existing ordinary destination folder.");
        const auto destinationStatus = std::filesystem::symlink_status(destination, error);
        if (!error && destinationStatus.type() != std::filesystem::file_type::not_found)
            throw std::invalid_argument("UI style sheet Save As will not overwrite an existing file.");
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::invalid_argument("UI style sheet Save As could not inspect the destination.");

        Keire::Detail::WriteTextFileAtomically(destination, m_SourceText);
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
        m_SourceText = source;
        m_BaselineSource = source;
        m_SourceDiagnostic.reset();
        m_Selection = m_Definition.Rules.empty() ? std::nullopt : std::optional<std::size_t>(0);
        m_Dirty = false;
        AdvanceGeneration();
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Clear();
    }

    bool UiBuilderStyleSheetDocument::Undo() { return m_Undo && m_Undo->Undo(); }

    bool UiBuilderStyleSheetDocument::Redo() { return m_Undo && m_Undo->Redo(); }

    Keire::UiStyleRuleDefinition UiBuilderStyleSheetDocument::ParseRule(const std::string_view selector,
                                                                        const std::string_view declarations,
                                                                        const std::uint32_t schemaVersion)
    {
        if (selector.empty())
            throw std::invalid_argument("A UI style rule requires a selector.");
        std::string source = "@keire-style " + std::to_string(schemaVersion) + ";\n";
        source += selector;
        source += " {\n";
        source += declarations;
        source += "\n}\n";
        auto parsed = Keire::UiStyleSheetAsset::ParseSource(Bytes(source));
        if (parsed.Rules.size() != 1)
            throw std::invalid_argument("A UI style edit must describe exactly one selector rule.");
        return std::move(parsed.Rules.front());
    }

    bool UiBuilderStyleSheetDocument::Edit(const std::string_view name, Keire::UiStyleSheetDefinition candidate,
                                           std::optional<std::string> source)
    {
        if (!m_Asset)
            throw std::logic_error("Open a UI style sheet before editing it.");
        Keire::UiStyleSheetAsset::Validate(candidate);
        if (Keire::UiStyleSheetAsset::Encode(candidate) == Keire::UiStyleSheetAsset::Encode(m_Definition))
            return false;
        auto before = State();
        m_Definition = std::move(candidate);
        if (source)
            m_SourceText = std::move(*source);
        else
        {
            const auto encoded = Keire::UiStyleSheetAsset::EncodeSource(m_Definition);
            m_SourceText.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
        m_SourceDiagnostic.reset();
        RecordApplied(name, std::move(before));
        m_Dirty = true;
        AdvanceGeneration();
        return true;
    }

    void UiBuilderStyleSheetDocument::RecordApplied(const std::string_view name, DocumentState before,
                                                    std::string mergeKey)
    {
        if (!m_Undo || !m_Undo->IsOpen())
            return;
        auto after = std::make_shared<std::optional<DocumentState>>();
        const auto asset = m_Asset;
        m_Undo->RecordApplied(std::make_unique<ContinuousStyleUndoCommand>(
            std::string(name), std::move(mergeKey),
            [this, after, asset]
            {
                if (m_Asset != asset || !after->has_value())
                    return;
                Restore(**after);
                Select(m_Selection);
                m_Dirty = true;
                AdvanceGeneration();
            },
            [this, after, before = std::move(before), asset]() mutable
            {
                if (m_Asset != asset)
                    return;
                *after = State();
                Restore(before);
                Select(m_Selection);
                m_Dirty = true;
                AdvanceGeneration();
            }));
    }

    UiBuilderStyleSheetDocument::DocumentState UiBuilderStyleSheetDocument::State() const
    {
        return {.Definition = m_Definition, .Source = m_SourceText, .Diagnostic = m_SourceDiagnostic};
    }

    void UiBuilderStyleSheetDocument::Restore(DocumentState state)
    {
        m_Definition = std::move(state.Definition);
        m_SourceText = std::move(state.Source);
        m_SourceDiagnostic = std::move(state.Diagnostic);
    }

    UiBuilderStyleSourceDiagnostic UiBuilderStyleSheetDocument::Diagnose(const std::string_view source,
                                                                         const std::string_view message) noexcept
    {
        std::size_t offset = source.find_last_not_of(" \t\r\n");
        if (offset == std::string_view::npos)
            offset = 0;
        const auto lineBegin = source.rfind('\n', offset);
        const auto line = static_cast<std::size_t>(std::count(source.begin(), source.begin() + offset, '\n')) + 1;
        const auto column = lineBegin == std::string_view::npos ? offset + 1 : offset - lineBegin;
        return {.Line = line, .Column = column, .Message = std::string(message)};
    }

    void UiBuilderStyleSheetDocument::AdvanceGeneration() noexcept
    {
        if (++m_Generation == 0)
            ++m_Generation;
    }
} // namespace KeireEditor
