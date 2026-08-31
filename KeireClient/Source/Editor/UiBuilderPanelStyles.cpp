#include "KeireClient/Editor/UiBuilderPanel.h"

#include "Keire/Ui/UiStyleProperties.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <exception>
#include <iomanip>
#include <ranges>
#include <sstream>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] bool ContainsInsensitive(const std::string_view value, const std::string_view search)
        {
            return search.empty() || Lower(std::string(value)).find(Lower(std::string(search))) != std::string::npos;
        }

        [[nodiscard]] std::string AssetLabel(const std::span<const Keire::AssetSourceRecord> records,
                                             const Keire::AssetId asset)
        {
            const auto found = std::ranges::find(records, asset, &Keire::AssetSourceRecord::Id);
            return found == records.end() ? asset.ToString() : found->RelativePath.generic_string();
        }

        [[nodiscard]] std::string_view CategoryName(const Keire::UiStylePropertyCategory category) noexcept
        {
            using Category = Keire::UiStylePropertyCategory;
            switch (category)
            {
            case Category::Layout:
                return "Layout";
            case Category::Size:
                return "Size";
            case Category::Flex:
                return "Flex";
            case Category::Spacing:
                return "Spacing";
            case Category::Position:
                return "Position";
            case Category::Typography:
                return "Typography";
            case Category::Background:
                return "Background";
            case Category::Border:
                return "Borders & Corners";
            case Category::Effects:
                return "Effects & Shadows";
            case Category::Transform:
                return "Transforms";
            case Category::Clipping:
                return "Clipping & Masks";
            case Category::Transition:
                return "Transitions";
            case Category::Accessibility:
                return "Accessibility";
            }
            return "Other";
        }

        [[nodiscard]] std::vector<std::string_view> SplitKeywords(const std::string_view value)
        {
            std::vector<std::string_view> result;
            std::size_t begin = 0;
            while (begin < value.size())
            {
                const auto end = value.find('|', begin);
                result.push_back(value.substr(begin, end - begin));
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return result;
        }

        [[nodiscard]] bool ParseHexColor(const std::string_view value, Keire::UiColor& color) noexcept
        {
            if ((value.size() != 7 && value.size() != 9) || value.front() != '#')
                return false;
            std::uint32_t encoded = 0;
            const auto digits = value.substr(1);
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), encoded, 16);
            if (error != std::errc{} || end != digits.data() + digits.size())
                return false;
            if (digits.size() == 6)
                encoded = encoded << 8U | 0xffU;
            color = {.Red = static_cast<float>((encoded >> 24U) & 0xffU) / 255.0F,
                     .Green = static_cast<float>((encoded >> 16U) & 0xffU) / 255.0F,
                     .Blue = static_cast<float>((encoded >> 8U) & 0xffU) / 255.0F,
                     .Alpha = static_cast<float>(encoded & 0xffU) / 255.0F};
            return true;
        }

        [[nodiscard]] std::string EncodeHexColor(const Keire::UiColor color)
        {
            const auto channel = [](const float value)
            { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)); };
            const auto encoded = channel(color.Red) << 24U | channel(color.Green) << 16U | channel(color.Blue) << 8U |
                                 channel(color.Alpha);
            std::ostringstream stream;
            stream << '#' << std::hex << std::setfill('0') << std::setw(8) << encoded;
            return stream.str();
        }

        [[nodiscard]] Keire::AssetId ParseAssetReference(const std::string_view value) noexcept
        {
            try
            {
                if (!value.starts_with("asset(") || !value.ends_with(')'))
                    return {};
                return Keire::AssetId::Parse(value.substr(6, value.size() - 7));
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] std::string AssetReference(const Keire::AssetId value)
        {
            return value ? "asset(" + value.ToString() + ')' : "none";
        }

        [[nodiscard]] const Keire::UiNamedValue* FindProperty(const Keire::UiStyleRuleDefinition& rule,
                                                              const std::string_view name) noexcept
        {
            const auto found = std::ranges::find(rule.Properties, name, &Keire::UiNamedValue::Name);
            return found == rule.Properties.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::string MediaSummary(const Keire::UiStyleRuleDefinition& rule)
        {
            if (!rule.Media)
                return "All viewports";
            std::vector<std::string> parts;
            if (rule.Media->MinimumWidth)
                parts.push_back("width >= " + std::to_string(static_cast<int>(*rule.Media->MinimumWidth)) + " px");
            if (rule.Media->MaximumWidth)
                parts.push_back("width <= " + std::to_string(static_cast<int>(*rule.Media->MaximumWidth)) + " px");
            if (rule.Media->Orientation == Keire::UiStyleOrientation::Landscape)
                parts.emplace_back("landscape");
            else if (rule.Media->Orientation == Keire::UiStyleOrientation::Portrait)
                parts.emplace_back("portrait");
            std::string result;
            for (const auto& part : parts)
            {
                if (!result.empty())
                    result += " | ";
                result += part;
            }
            return result.empty() ? "Conditional" : result;
        }

        [[nodiscard]] std::string ElementSelector(const Keire::UiVisualElementDefinition& element)
        {
            if (!element.Name.empty())
                return '#' + element.Name;
            if (!element.Classes.empty())
                return '.' + element.Classes.front();
            return std::string(UiBuilderElementTypeName(element.Type));
        }

        [[nodiscard]] std::string ClassName(const Keire::UiVisualElementDefinition& element)
        {
            auto result =
                Lower(element.Name.empty() ? std::string(UiBuilderElementTypeName(element.Type)) : element.Name);
            for (auto& character : result)
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
                    character = '-';
            while (!result.empty() &&
                   (result.front() == '-' || std::isdigit(static_cast<unsigned char>(result.front()))))
                result.erase(result.begin());
            return result.empty() ? "ui-element" : result;
        }

        [[nodiscard]] std::string UniqueTokenName(const Keire::UiStyleSheetDefinition& definition,
                                                  const std::string_view property)
        {
            std::string base = "--" + std::string(property);
            std::ranges::replace(base, ' ', '-');
            auto exists = [&definition](const std::string_view name)
            {
                for (const auto& rule : definition.Rules)
                    if (std::ranges::find(rule.Properties, name, &Keire::UiNamedValue::Name) != rule.Properties.end())
                        return true;
                return false;
            };
            if (!exists(base))
                return base;
            for (std::size_t copy = 2;; ++copy)
            {
                auto candidate = base + '-' + std::to_string(copy);
                if (!exists(candidate))
                    return candidate;
            }
        }

        [[nodiscard]] std::string JoinClasses(const std::vector<std::string>& classes)
        {
            std::string result;
            for (const auto& value : classes)
            {
                if (!result.empty())
                    result += ' ';
                result += value;
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string> ParseClasses(const std::string_view source)
        {
            std::vector<std::string> result;
            std::size_t cursor = 0;
            while (cursor < source.size())
            {
                const auto begin = source.find_first_not_of(" .\t\r\n", cursor);
                if (begin == std::string_view::npos)
                    break;
                const auto end = source.find_first_of(" .\t\r\n", begin);
                result.emplace_back(source.substr(begin, end - begin));
                cursor = end == std::string_view::npos ? source.size() : end;
            }
            std::ranges::sort(result);
            const auto duplicate = std::ranges::unique(result);
            result.erase(duplicate.begin(), duplicate.end());
            return result;
        }

        [[nodiscard]] bool HasPseudoState(const Keire::UiStylePseudoState states,
                                          const Keire::UiStylePseudoState state) noexcept
        {
            return (static_cast<std::uint16_t>(states) & static_cast<std::uint16_t>(state)) != 0;
        }

        void SetPseudoState(Keire::UiStylePseudoState& states, const Keire::UiStylePseudoState state,
                            const bool enabled) noexcept
        {
            auto value = static_cast<std::uint16_t>(states);
            if (enabled)
                value |= static_cast<std::uint16_t>(state);
            else
                value &= ~static_cast<std::uint16_t>(state);
            states = static_cast<Keire::UiStylePseudoState>(value);
        }

        [[nodiscard]] std::string SelectorSource(const std::vector<Keire::UiStyleSelectorPart>& parts)
        {
            std::string result;
            for (std::size_t index = 0; index < parts.size(); ++index)
            {
                const auto& part = parts[index];
                if (index > 0)
                    result += part.Combinator == Keire::UiStyleCombinator::Child ? " > " : " ";
                result += part.Type.empty() ? "*" : part.Type;
                if (!part.Name.empty())
                    result += '#' + part.Name;
                for (const auto& className : part.Classes)
                    result += '.' + className;
                for (const auto [state, name] :
                     {std::pair{Keire::UiStylePseudoState::Hover, std::string_view("hover")},
                      std::pair{Keire::UiStylePseudoState::Active, std::string_view("active")},
                      std::pair{Keire::UiStylePseudoState::Focus, std::string_view("focus")},
                      std::pair{Keire::UiStylePseudoState::Disabled, std::string_view("disabled")},
                      std::pair{Keire::UiStylePseudoState::Checked, std::string_view("checked")}})
                    if (HasPseudoState(part.States, state))
                        result += ':' + std::string(name);
            }
            return result;
        }

        [[nodiscard]] bool ParseNumericValue(const std::string_view source, double& number, std::string& unit) noexcept
        {
            const auto begin = source.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return false;
            const auto end = source.find_last_not_of(" \t\r\n") + 1U;
            const auto trimmed = source.substr(begin, end - begin);
            const auto [cursor, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number);
            if (error != std::errc{} || cursor == trimmed.data())
                return false;
            unit.assign(cursor, trimmed.data() + trimmed.size());
            return unit.empty() || unit == "px" || unit == "%" || unit == "ms" || unit == "s" || unit == "deg";
        }

        [[nodiscard]] std::string NumericValue(const double number, const std::string_view unit)
        {
            std::ostringstream stream;
            stream << std::setprecision(6) << number << unit;
            return stream.str();
        }

        struct VisualGradientDraft final
        {
            bool Radial = false;
            double Angle = 180.0;
            double CenterX = 50.0;
            double CenterY = 50.0;
            double Radius = 50.0;
            Keire::UiColor FirstColor{1.0F, 1.0F, 1.0F, 1.0F};
            Keire::UiColor SecondColor{0.0F, 0.0F, 0.0F, 1.0F};
            double FirstPosition = 0.0;
            double SecondPosition = 100.0;
        };

        [[nodiscard]] std::string_view Trim(const std::string_view value) noexcept
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1U);
        }

        [[nodiscard]] bool ParseGradientScalar(std::string_view value, const std::string_view suffix,
                                               double& result) noexcept
        {
            value = Trim(value);
            if (!value.ends_with(suffix))
                return false;
            value.remove_suffix(suffix.size());
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            return error == std::errc{} && end == value.data() + value.size() && std::isfinite(result);
        }

        [[nodiscard]] bool ParseGradientStop(const std::string_view source, Keire::UiColor& color,
                                             double& position) noexcept
        {
            const auto split = source.find_last_of(" \t");
            return split != std::string_view::npos && ParseHexColor(Trim(source.substr(0, split)), color) &&
                   ParseGradientScalar(source.substr(split + 1U), "%", position);
        }

        [[nodiscard]] bool ParseVisualGradient(const std::string_view source, VisualGradientDraft& result) noexcept
        {
            constexpr std::string_view linear = "linear-gradient(";
            constexpr std::string_view radial = "radial-gradient(";
            if (!source.ends_with(')') || (!source.starts_with(linear) && !source.starts_with(radial)))
                return false;
            result.Radial = source.starts_with(radial);
            const auto prefix = result.Radial ? radial.size() : linear.size();
            const auto body = source.substr(prefix, source.size() - prefix - 1U);
            const auto firstComma = body.find(',');
            const auto secondComma =
                firstComma == std::string_view::npos ? std::string_view::npos : body.find(',', firstComma + 1U);
            if (firstComma == std::string_view::npos || secondComma == std::string_view::npos ||
                body.find(',', secondComma + 1U) != std::string_view::npos)
            {
                return false;
            }
            const auto geometry = Trim(body.substr(0, firstComma));
            if (result.Radial)
            {
                if (!geometry.starts_with("circle "))
                    return false;
                const auto at = geometry.find(" at ", 7U);
                if (at == std::string_view::npos ||
                    !ParseGradientScalar(geometry.substr(7U, at - 7U), "%", result.Radius))
                {
                    return false;
                }
                const auto center = geometry.substr(at + 4U);
                const auto separator = center.find_first_of(" \t");
                if (separator == std::string_view::npos ||
                    !ParseGradientScalar(center.substr(0, separator), "%", result.CenterX) ||
                    !ParseGradientScalar(center.substr(separator + 1U), "%", result.CenterY))
                {
                    return false;
                }
            }
            else if (!ParseGradientScalar(geometry, "deg", result.Angle))
                return false;
            return ParseGradientStop(body.substr(firstComma + 1U, secondComma - firstComma - 1U), result.FirstColor,
                                     result.FirstPosition) &&
                   ParseGradientStop(body.substr(secondComma + 1U), result.SecondColor, result.SecondPosition);
        }

        [[nodiscard]] std::string EncodeVisualGradient(const VisualGradientDraft& value)
        {
            std::string result = value.Radial
                                     ? "radial-gradient(circle " + NumericValue(value.Radius, "%") + " at " +
                                           NumericValue(value.CenterX, "%") + ' ' + NumericValue(value.CenterY, "%")
                                     : "linear-gradient(" + NumericValue(value.Angle, "deg");
            result += ", " + EncodeHexColor(value.FirstColor) + ' ' + NumericValue(value.FirstPosition, "%");
            result += ", " + EncodeHexColor(value.SecondColor) + ' ' + NumericValue(value.SecondPosition, "%") + ')';
            return result;
        }

        [[nodiscard]] bool DrawVisualGradient(Keire::UiFrame& ui, const std::string_view name, std::string& value)
        {
            if (value == "none")
            {
                if (ui.Button("Create Linear Gradient##" + std::string(name)))
                {
                    value = "linear-gradient(180deg, #ffffffff 0%, #000000ff 100%)";
                    return true;
                }
                ui.SameLine();
                if (ui.Button("Create Radial Gradient##" + std::string(name)))
                {
                    value = "radial-gradient(circle 50% at 50% 50%, #ffffffff 0%, #00000000 100%)";
                    return true;
                }
                return false;
            }
            VisualGradientDraft draft;
            if (!ParseVisualGradient(value, draft))
                return ui.InputText("Advanced Gradient##" + std::string(name), value);
            bool changed = false;
            if (auto combo = ui.BeginCombo("Type##" + std::string(name), draft.Radial ? "Radial" : "Linear"); combo)
            {
                if (ui.Selectable("Linear", !draft.Radial))
                {
                    draft.Radial = false;
                    changed = true;
                }
                if (ui.Selectable("Radial", draft.Radial))
                {
                    draft.Radial = true;
                    changed = true;
                }
            }
            if (draft.Radial)
            {
                changed |= ui.DragScalar("Radius (%)##" + std::string(name), draft.Radius, 0.5, 0.1, 200.0);
                changed |= ui.DragScalar("Center X (%)##" + std::string(name), draft.CenterX, 0.5, 0.0, 100.0);
                changed |= ui.DragScalar("Center Y (%)##" + std::string(name), draft.CenterY, 0.5, 0.0, 100.0);
            }
            else
                changed |= ui.DragScalar("Angle (deg)##" + std::string(name), draft.Angle, 0.5, -360.0, 360.0);
            changed |= ui.ColorEdit("Start Color##" + std::string(name), draft.FirstColor);
            changed |= ui.DragScalar("Start Position (%)##" + std::string(name), draft.FirstPosition, 0.5, 0.0, 100.0);
            changed |= ui.ColorEdit("End Color##" + std::string(name), draft.SecondColor);
            changed |= ui.DragScalar("End Position (%)##" + std::string(name), draft.SecondPosition, 0.5, 0.0, 100.0);
            if (changed)
            {
                if (draft.FirstPosition > draft.SecondPosition)
                    std::swap(draft.FirstPosition, draft.SecondPosition);
                value = EncodeVisualGradient(draft);
            }
            return changed;
        }

        [[nodiscard]] bool SupportsPercentage(const std::string_view property) noexcept
        {
            return property == "width" || property == "height" || property == "min-width" || property == "min-height" ||
                   property == "max-width" || property == "max-height" || property == "left" || property == "top";
        }

        constexpr std::array Categories{
            Keire::UiStylePropertyCategory::Layout,       Keire::UiStylePropertyCategory::Size,
            Keire::UiStylePropertyCategory::Flex,         Keire::UiStylePropertyCategory::Spacing,
            Keire::UiStylePropertyCategory::Position,     Keire::UiStylePropertyCategory::Typography,
            Keire::UiStylePropertyCategory::Background,   Keire::UiStylePropertyCategory::Border,
            Keire::UiStylePropertyCategory::Effects,      Keire::UiStylePropertyCategory::Transform,
            Keire::UiStylePropertyCategory::Clipping,     Keire::UiStylePropertyCategory::Transition,
            Keire::UiStylePropertyCategory::Accessibility};
    } // namespace

    void UiBuilderPanel::DrawStyleSheets(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.UiBuilderState();
        auto& styleDocument = m_Controller.UiBuilderStyleSheetState();
        const auto& theme = m_Controller.UiBuilderTheme();
        const auto records = m_Controller.UiBuilderAssetRecords();

        ui.TextColored(theme.Accent, "STYLE SOURCES");
        ui.TextColoredWrapped(theme.MutedText, "Linked sheets cascade top-to-bottom. Later rules win ties.");
        ui.Separator();
        const auto linked = document.Definition().StyleSheets;
        for (std::size_t index = 0; index < linked.size(); ++index)
        {
            const auto asset = linked[index];
            const auto label = AssetLabel(records, asset);
            if (ui.Selectable(label + "##UiBuilderLinkedStyle" + asset.ToString(), styleDocument.Asset() == asset))
            {
                try
                {
                    m_Controller.OpenUiBuilderStyleSheet(asset);
                    m_StyleRuleAsset = {};
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
            if (ui.LastItemState().Hovered)
                ui.SetTooltip(label + "\n" + asset.ToString(), {.Delayed = true});
            if (index > 0)
            {
                ui.SameLine();
                if (ui.Button("Up##StyleSheet" + asset.ToString()))
                {
                    auto candidate = document.Definition();
                    std::swap(candidate.StyleSheets[index - 1], candidate.StyleSheets[index]);
                    (void)document.Edit("Reorder UI style sheets", std::move(candidate));
                }
            }
            ui.SameLine();
            if (ui.Button("Reveal##StyleSheet" + asset.ToString()))
                m_Controller.RevealUiBuilderAsset(asset);
            ui.SameLine();
            if (ui.Button("Unlink##StyleSheet" + asset.ToString()))
            {
                auto candidate = document.Definition();
                candidate.StyleSheets.erase(candidate.StyleSheets.begin() + static_cast<std::ptrdiff_t>(index));
                (void)document.Edit("Unlink UI style sheet", std::move(candidate));
                break;
            }
        }
        (void)m_StyleSheetPicker.Draw(
            ui, records, m_StyleSheetDraft,
            {.Label = "Link",
             .EmptyLabel = "Drop or choose a .keirestyle",
             .ExpectedType = Keire::UiStyleSheetAsset::StaticType(),
             .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealUiBuilderAsset(asset); },
             .AllowNone = true});
        if (ui.Button("Link Style Sheet"))
        {
            if (m_StyleSheetDraft)
            {
                auto candidate = document.Definition();
                if (std::ranges::find(candidate.StyleSheets, m_StyleSheetDraft) == candidate.StyleSheets.end())
                    candidate.StyleSheets.push_back(m_StyleSheetDraft);
                (void)document.Edit("Link UI style sheet", std::move(candidate));
                m_StyleSheetDraft = {};
                m_StyleSheetPicker.Clear();
            }
        }

        ui.Separator();
        if (!styleDocument.Asset())
        {
            ui.TextColoredWrapped(theme.MutedText, "Select a linked sheet to inspect rules and design tokens.");
            return;
        }
        m_Controller.ActivateUiBuilderStyleSheetHistory();
        ui.TextColored(theme.Accent, "RULES");
        (void)ui.InputTextWithHint("##UiBuilderRuleSearch", "Search selector or property", m_StyleRuleSearch);
        const auto rules = styleDocument.Definition().Rules;
        for (std::size_t index = 0; index < rules.size(); ++index)
        {
            const auto& rule = rules[index];
            bool visible = ContainsInsensitive(rule.Selector, m_StyleRuleSearch);
            for (const auto& property : rule.Properties)
                visible = visible || ContainsInsensitive(property.Name, m_StyleRuleSearch) ||
                          ContainsInsensitive(property.Value, m_StyleRuleSearch);
            if (!visible)
                continue;
            const auto label = rule.Selector + "\n  " + MediaSummary(rule) + " | " +
                               std::to_string(rule.Properties.size()) + " properties##UiBuilderStyleRule" +
                               std::to_string(index);
            if (ui.Selectable(label, styleDocument.Selection() == index))
            {
                styleDocument.Select(index);
                m_StyleRuleAsset = {};
            }
        }
        if (ui.Button("+ Rule"))
        {
            try
            {
                (void)styleDocument.AddRule(".new-class", "color: #ffffffff;");
                m_StyleRuleAsset = {};
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
            }
        }
        if (styleDocument.Selection())
        {
            const auto selected = *styleDocument.Selection();
            ui.SameLine();
            if (ui.Button("Duplicate"))
                (void)styleDocument.DuplicateRule(selected);
            ui.SameLine();
            if (ui.Button("Up##Rule") && selected > 0)
                (void)styleDocument.MoveRule(selected, selected - 1);
            ui.SameLine();
            if (ui.Button("Down##Rule") && selected + 1 < rules.size())
                (void)styleDocument.MoveRule(selected, selected + 1);
            ui.SameLine();
            if (ui.Button("Delete"))
                (void)styleDocument.RemoveRule(selected);
        }

        ui.Separator();
        ui.TextColored(theme.Accent, "DESIGN TOKENS");
        (void)ui.InputTextWithHint("##UiBuilderTokenSearch", "Search tokens or values", m_StyleTokenSearch);
        std::vector<Keire::UiNamedValue> tokens;
        for (const auto& rule : rules)
            for (const auto& property : rule.Properties)
                if (property.Name.starts_with("--") &&
                    std::ranges::find(tokens, property.Name, &Keire::UiNamedValue::Name) == tokens.end())
                    tokens.push_back(property);
        for (const auto& token : tokens)
        {
            if (!ContainsInsensitive(token.Name, m_StyleTokenSearch) &&
                !ContainsInsensitive(token.Value, m_StyleTokenSearch))
            {
                continue;
            }
            const auto needle = "var(" + token.Name + ')';
            std::size_t usages = 0;
            for (const auto& rule : rules)
                usages += static_cast<std::size_t>(
                    std::ranges::count_if(rule.Properties, [&needle](const auto& property)
                                          { return property.Value.find(needle) != std::string::npos; }));
            if (ui.Selectable(token.Name + "  " + token.Value + " | " + std::to_string(usages) + " use(s)##StyleToken" +
                                  token.Name,
                              m_StyleTokenSelection == token.Name))
            {
                m_StyleTokenSelection = token.Name;
                m_StyleTokenNameDraft = token.Name;
                m_StyleTokenValueDraft = token.Value;
            }
        }
        if (tokens.empty())
            ui.TextColoredWrapped(theme.MutedText, "Use --tokens to share colors, spacing, and typography values.");
        if (ui.Button("+ Token"))
        {
            m_StyleTokenSelection = "--new-token";
            m_StyleTokenNameDraft = m_StyleTokenSelection;
            m_StyleTokenValueDraft = "#ffffffff";
        }
        if (!m_StyleTokenSelection.empty())
        {
            (void)ui.InputText("Name##StyleToken", m_StyleTokenNameDraft);
            Keire::UiColor tokenColor;
            if (ParseHexColor(m_StyleTokenValueDraft, tokenColor))
            {
                if (ui.ColorEdit("Value##StyleTokenColor", tokenColor))
                    m_StyleTokenValueDraft = EncodeHexColor(tokenColor);
            }
            else
                (void)ui.InputText("Value##StyleToken", m_StyleTokenValueDraft);
            if (ui.Button("Apply Token"))
            {
                try
                {
                    if (m_StyleTokenSelection != m_StyleTokenNameDraft &&
                        std::ranges::find(tokens, m_StyleTokenSelection, &Keire::UiNamedValue::Name) != tokens.end())
                    {
                        (void)styleDocument.RenameToken(m_StyleTokenSelection, m_StyleTokenNameDraft);
                    }
                    (void)styleDocument.SetToken(m_StyleTokenNameDraft, m_StyleTokenValueDraft);
                    m_StyleTokenSelection = m_StyleTokenNameDraft;
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
            if (m_StyleTokenSelection != m_StyleTokenNameDraft &&
                std::ranges::find(tokens, m_StyleTokenSelection, &Keire::UiNamedValue::Name) != tokens.end())
            {
                ui.SameLine();
                if (ui.Button("Preview Project Rename"))
                {
                    try
                    {
                        m_StyleTokenRefactorPreview =
                            m_Controller.PreviewUiBuilderTokenRefactor(m_StyleTokenSelection, m_StyleTokenNameDraft);
                        m_StyleTokenRefactorConfirmed = false;
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                    }
                }
            }
        }
        if (m_StyleTokenRefactorPreview)
        {
            const auto& preview = *m_StyleTokenRefactorPreview;
            ui.Separator();
            ui.TextColored(theme.Warning, "PROJECT-WIDE REFACTOR PREVIEW");
            ui.TextWrapped(preview.CurrentName + " -> " + preview.ReplacementName + " | " +
                           std::to_string(preview.OccurrenceCount) + " occurrence(s) in " +
                           std::to_string(preview.Changes.size()) + " asset(s)");
            if (auto files = ui.BeginChild("UiTokenRefactorPreview", {0.0F, 160.0F}, true); files)
            {
                for (const auto& change : preview.Changes)
                {
                    ui.TextColored(theme.Accent, change.RelativePath.generic_string());
                    for (const auto& occurrence : change.Occurrences)
                    {
                        ui.TextColoredWrapped(theme.MutedText, "  " + std::to_string(occurrence.Line) + ":" +
                                                                   std::to_string(occurrence.Column) + "  " +
                                                                   occurrence.Preview);
                    }
                }
            }
            (void)ui.Checkbox("I reviewed every affected asset", m_StyleTokenRefactorConfirmed);
            if (auto disabled = ui.BeginDisabled(!m_StyleTokenRefactorConfirmed); disabled)
            {
                if (ui.Button("Apply Project Rename"))
                {
                    try
                    {
                        m_Controller.ApplyUiBuilderTokenRefactor(preview);
                        m_StyleTokenSelection = preview.ReplacementName;
                        m_StyleTokenRefactorPreview.reset();
                        m_StyleTokenRefactorConfirmed = false;
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                    }
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel Refactor"))
            {
                m_StyleTokenRefactorPreview.reset();
                m_StyleTokenRefactorConfirmed = false;
            }
        }
    }

    void UiBuilderPanel::DrawStyleProperties(Keire::UiFrame& ui)
    {
        auto& visualDocument = m_Controller.UiBuilderState();
        auto& styleDocument = m_Controller.UiBuilderStyleSheetState();
        const auto& theme = m_Controller.UiBuilderTheme();
        const auto records = m_Controller.UiBuilderAssetRecords();
        const auto* selectedElement = visualDocument.Find(visualDocument.Selection());
        if (ui.Button(m_StyleEditInline ? "Rule Properties" : "Rule Properties [active]"))
            m_StyleEditInline = false;
        ui.SameLine();
        if (ui.Button(m_StyleEditInline ? "Inline Overrides [active]" : "Inline Overrides"))
            m_StyleEditInline = true;
        if (m_StyleEditInline && !selectedElement)
        {
            ui.TextColoredWrapped(theme.MutedText, "Select a visual element to edit its inline overrides.");
            return;
        }
        if (!m_StyleEditInline && (!styleDocument.Asset() || !styleDocument.Selection()))
        {
            ui.TextColoredWrapped(theme.MutedText, "Select a style rule to edit its selector and properties.");
            return;
        }
        const auto selected = styleDocument.Selection().value_or(0);
        if (!m_StyleEditInline &&
            (m_StyleRuleAsset != styleDocument.Asset() || m_StyleRuleGeneration != styleDocument.Generation() ||
             m_StyleRuleSelection != styleDocument.Selection()))
        {
            m_StyleRuleAsset = styleDocument.Asset();
            m_StyleRuleGeneration = styleDocument.Generation();
            m_StyleRuleSelection = styleDocument.Selection();
            const auto& selectedRule = styleDocument.Definition().Rules[selected];
            m_StyleSelectorDraft = selectedRule.Selector;
            if (!selectedRule.Parts.empty())
            {
                const auto& target = selectedRule.Parts.back();
                m_StyleSelectorTypeDraft = target.Type;
                m_StyleSelectorNameDraft = target.Name;
                m_StyleSelectorClassesDraft = JoinClasses(target.Classes);
                m_StyleSelectorCombinator = target.Combinator;
                m_StyleSelectorStates = target.States;
            }
        }
        Keire::UiStyleRuleDefinition rule;
        if (m_StyleEditInline)
        {
            rule.Selector = "<inline>";
            rule.Properties = selectedElement->InlineStyles;
            rule.Specificity = 1'000'000;
        }
        else
            rule = styleDocument.Definition().Rules[selected];
        if (m_StyleEditInline)
        {
            ui.TextColored(theme.Accent, "INLINE OVERRIDES");
            ui.TextColoredWrapped(theme.MutedText,
                                  "Inline values win the cascade. Promote reusable values into a class rule.");
            if (styleDocument.Asset() && !rule.Properties.empty() && ui.Button("Move Overrides Into Class Rule"))
            {
                auto classes = selectedElement->Classes;
                const auto className = ClassName(*selectedElement);
                if (std::ranges::find(classes, className) == classes.end())
                    classes.push_back(className);
                std::string declarations;
                for (const auto& property : rule.Properties)
                    declarations += property.Name + ": " + property.Value + ";\n";
                (void)styleDocument.AddRule('.' + className, declarations);
                (void)visualDocument.SetClasses(visualDocument.Selections(), std::move(classes));
                for (const auto& property : rule.Properties)
                    (void)visualDocument.RemoveInlineStyleProperty(visualDocument.Selections(), property.Name);
                return;
            }
        }
        else
        {
            ui.TextColored(theme.Accent, "VISUAL SELECTOR");
            ui.TextColoredWrapped(theme.MutedText,
                                  "Build the target selector visually. The advanced source stays synchronized.");
            bool visualSelectorChanged = false;
            visualSelectorChanged |=
                ui.InputTextWithHint("Type##UiBuilderSelectorType", "Button, Label, *", m_StyleSelectorTypeDraft);
            visualSelectorChanged |=
                ui.InputTextWithHint("Name##UiBuilderSelectorName", "optional element name", m_StyleSelectorNameDraft);
            visualSelectorChanged |= ui.InputTextWithHint("Classes##UiBuilderSelectorClasses", "primary compact danger",
                                                          m_StyleSelectorClassesDraft);
            if (rule.Parts.size() > 1)
            {
                const auto relationship = m_StyleSelectorCombinator == Keire::UiStyleCombinator::Child
                                              ? "Direct child (>)"
                                              : "Descendant (space)";
                if (auto combo = ui.BeginCombo("Relationship", relationship); combo)
                {
                    if (ui.Selectable("Descendant (space)",
                                      m_StyleSelectorCombinator == Keire::UiStyleCombinator::Descendant))
                    {
                        m_StyleSelectorCombinator = Keire::UiStyleCombinator::Descendant;
                        visualSelectorChanged = true;
                    }
                    if (ui.Selectable("Direct child (>)", m_StyleSelectorCombinator == Keire::UiStyleCombinator::Child))
                    {
                        m_StyleSelectorCombinator = Keire::UiStyleCombinator::Child;
                        visualSelectorChanged = true;
                    }
                }
            }
            ui.TextColored(theme.MutedText, "Pseudo states");
            for (const auto [state, name] :
                 {std::pair{Keire::UiStylePseudoState::Hover, std::string_view("Hover")},
                  std::pair{Keire::UiStylePseudoState::Active, std::string_view("Active")},
                  std::pair{Keire::UiStylePseudoState::Focus, std::string_view("Focus")},
                  std::pair{Keire::UiStylePseudoState::Disabled, std::string_view("Disabled")},
                  std::pair{Keire::UiStylePseudoState::Checked, std::string_view("Checked")}})
            {
                bool enabled = HasPseudoState(m_StyleSelectorStates, state);
                if (ui.Checkbox(std::string(name) + "##UiBuilderSelectorState" + std::string(name), enabled))
                {
                    SetPseudoState(m_StyleSelectorStates, state, enabled);
                    visualSelectorChanged = true;
                }
                ui.SameLine();
            }
            ui.Spacing();
            if (visualSelectorChanged && !rule.Parts.empty())
            {
                auto parts = rule.Parts;
                auto& target = parts.back();
                target.Type = m_StyleSelectorTypeDraft == "*" ? std::string{} : m_StyleSelectorTypeDraft;
                target.Name = m_StyleSelectorNameDraft;
                target.Classes = ParseClasses(m_StyleSelectorClassesDraft);
                target.States = m_StyleSelectorStates;
                if (parts.size() > 1)
                    target.Combinator = m_StyleSelectorCombinator;
                m_StyleSelectorDraft = SelectorSource(parts);
            }
            if (ui.Button("Apply Visual Selector"))
            {
                try
                {
                    (void)styleDocument.SetSelector(selected, m_StyleSelectorDraft);
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
            if (auto advanced = ui.BeginTreeNode("Advanced Selector Source"); advanced)
            {
                (void)ui.InputText("##UiBuilderVisualSelector", m_StyleSelectorDraft);
                if (ui.Button("Apply Advanced Selector"))
                {
                    try
                    {
                        (void)styleDocument.SetSelector(selected, m_StyleSelectorDraft);
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                    }
                }
            }
            ui.TextColored(theme.MutedText,
                           "Specificity " + std::to_string(rule.Specificity) + " | " + MediaSummary(rule));
            if (!rule.Media)
            {
                if (ui.Button("+ Responsive Condition"))
                {
                    Keire::UiStyleMediaCondition condition;
                    condition.MinimumWidth = 768.0F;
                    (void)styleDocument.SetMediaCondition(selected, condition);
                    m_StyleRuleAsset = {};
                }
            }
            else
            {
                auto condition = *rule.Media;
                const auto optionalScalar = [&ui, &condition, &styleDocument,
                                             selected](const std::string_view label, std::optional<float>& field,
                                                       const double minimum, const double maximum, const double speed)
                {
                    bool enabled = field.has_value();
                    bool changed = ui.Checkbox("##Enable" + std::string(label), enabled);
                    ui.SameLine();
                    double value = field.value_or(minimum);
                    if (auto disabled = ui.BeginDisabled(!enabled); disabled)
                        changed |= ui.DragScalar(label, value, speed, minimum, maximum);
                    if (!changed)
                        return false;
                    field = enabled ? std::optional(static_cast<float>(value)) : std::nullopt;
                    (void)styleDocument.SetMediaCondition(selected, condition);
                    return true;
                };
                (void)optionalScalar("Minimum Width (px)", condition.MinimumWidth, 0.0, 65'536.0, 1.0);
                (void)optionalScalar("Maximum Width (px)", condition.MaximumWidth, 0.0, 65'536.0, 1.0);
                (void)optionalScalar("Minimum Height (px)", condition.MinimumHeight, 0.0, 65'536.0, 1.0);
                (void)optionalScalar("Maximum Height (px)", condition.MaximumHeight, 0.0, 65'536.0, 1.0);
                (void)optionalScalar("Minimum Aspect Ratio", condition.MinimumAspectRatio, 0.01, 100.0, 0.01);
                (void)optionalScalar("Maximum Aspect Ratio", condition.MaximumAspectRatio, 0.01, 100.0, 0.01);
                (void)optionalScalar("Minimum DPI", condition.MinimumDpi, 1.0, 2'400.0, 1.0);
                (void)optionalScalar("Maximum DPI", condition.MaximumDpi, 1.0, 2'400.0, 1.0);
                if (auto combo = ui.BeginCombo(
                        "Orientation", condition.Orientation == Keire::UiStyleOrientation::Landscape  ? "Landscape"
                                       : condition.Orientation == Keire::UiStyleOrientation::Portrait ? "Portrait"
                                                                                                      : "Any");
                    combo)
                {
                    for (const auto [value, name] :
                         {std::pair{Keire::UiStyleOrientation::Any, std::string_view("Any")},
                          std::pair{Keire::UiStyleOrientation::Landscape, std::string_view("Landscape")},
                          std::pair{Keire::UiStyleOrientation::Portrait, std::string_view("Portrait")}})
                        if (ui.Selectable(name, condition.Orientation == value))
                        {
                            condition.Orientation = value;
                            (void)styleDocument.SetMediaCondition(selected, condition);
                        }
                }
                if (auto combo = ui.BeginCombo("Pointer",
                                               condition.Pointer == Keire::UiStylePointerPrecision::Fine     ? "Fine"
                                               : condition.Pointer == Keire::UiStylePointerPrecision::Coarse ? "Coarse"
                                               : condition.Pointer == Keire::UiStylePointerPrecision::None   ? "None"
                                                                                                             : "Any");
                    combo)
                {
                    for (const auto [value, name] :
                         {std::pair{Keire::UiStylePointerPrecision::Any, std::string_view("Any")},
                          std::pair{Keire::UiStylePointerPrecision::Fine, std::string_view("Fine")},
                          std::pair{Keire::UiStylePointerPrecision::Coarse, std::string_view("Coarse")},
                          std::pair{Keire::UiStylePointerPrecision::None, std::string_view("None")}})
                        if (ui.Selectable(name, condition.Pointer == value))
                        {
                            condition.Pointer = value;
                            (void)styleDocument.SetMediaCondition(selected, condition);
                        }
                }
                if (auto combo =
                        ui.BeginCombo("Primary Navigation",
                                      condition.Navigation == Keire::UiStyleNavigationMode::Pointer    ? "Pointer"
                                      : condition.Navigation == Keire::UiStyleNavigationMode::Keyboard ? "Keyboard"
                                      : condition.Navigation == Keire::UiStyleNavigationMode::Gamepad  ? "Gamepad"
                                                                                                       : "Any");
                    combo)
                {
                    for (const auto [value, name] :
                         {std::pair{Keire::UiStyleNavigationMode::Any, std::string_view("Any")},
                          std::pair{Keire::UiStyleNavigationMode::Pointer, std::string_view("Pointer")},
                          std::pair{Keire::UiStyleNavigationMode::Keyboard, std::string_view("Keyboard")},
                          std::pair{Keire::UiStyleNavigationMode::Gamepad, std::string_view("Gamepad")}})
                        if (ui.Selectable(name, condition.Navigation == value))
                        {
                            condition.Navigation = value;
                            (void)styleDocument.SetMediaCondition(selected, condition);
                        }
                }
                bool reducedMotionSpecified = condition.ReducedMotion.has_value();
                if (ui.Checkbox("Specify Reduced Motion", reducedMotionSpecified))
                {
                    condition.ReducedMotion = reducedMotionSpecified ? std::optional(false) : std::nullopt;
                    (void)styleDocument.SetMediaCondition(selected, condition);
                }
                if (condition.ReducedMotion)
                {
                    bool reducedMotion = *condition.ReducedMotion;
                    if (ui.Checkbox("Reduced Motion Requested", reducedMotion))
                    {
                        condition.ReducedMotion = reducedMotion;
                        (void)styleDocument.SetMediaCondition(selected, condition);
                    }
                }
                if (ui.Button("Remove Responsive Condition"))
                    (void)styleDocument.SetMediaCondition(selected, std::nullopt);
            }
            for (const auto state : {std::string_view("hover"), std::string_view("active"), std::string_view("focus"),
                                     std::string_view("disabled"), std::string_view("checked")})
            {
                if (ui.Button("+:" + std::string(state) + "##StyleVariant" + std::string(state)))
                {
                    try
                    {
                        (void)styleDocument.AddRule(rule.Selector + ':' + std::string(state),
                                                    styleDocument.RuleDeclarations(selected));
                    }
                    catch (const std::exception& error)
                    {
                        m_Message = error.what();
                    }
                }
                ui.SameLine();
            }
            ui.Spacing();
            if (const auto* element = visualDocument.Find(visualDocument.Selection()))
            {
                if (ui.Button("Create Rule From Element"))
                    (void)styleDocument.AddRule(ElementSelector(*element), "color: #ffffffff;");
                if (ui.Button("Create & Assign Class"))
                {
                    auto classes = element->Classes;
                    const auto className = ClassName(*element);
                    if (std::ranges::find(classes, className) == classes.end())
                        classes.push_back(className);
                    (void)visualDocument.SetClasses(visualDocument.Selections(), std::move(classes));
                    (void)styleDocument.AddRule('.' + className, "color: #ffffffff;");
                }
            }
        }

        ui.Separator();
        ui.TextColored(theme.Accent, "PROPERTIES");
        (void)ui.InputTextWithHint("##UiBuilderPropertySearch", "Search properties", m_StylePropertySearch);
        for (const auto category : Categories)
        {
            if (auto section = ui.BeginTreeNode(std::string(CategoryName(category)) + "##StyleCategory" +
                                                std::to_string(static_cast<int>(category)));
                section)
            {
                for (const auto& descriptor : Keire::UiStylePropertyDescriptors())
                {
                    if (descriptor.Category != category ||
                        (!ContainsInsensitive(descriptor.DisplayName, m_StylePropertySearch) &&
                         !ContainsInsensitive(descriptor.Name, m_StylePropertySearch)))
                        continue;
                    const auto* property = FindProperty(rule, descriptor.Name);
                    std::string value = property ? property->Value : std::string(descriptor.DefaultValue);
                    bool changed = false;
                    if (descriptor.ValueKind == Keire::UiStyleValueKind::Asset)
                    {
                        m_StyleValueAssetDraft = ParseAssetReference(value);
                        const auto expectedType = descriptor.Name == "font-family"
                                                      ? Keire::UiFontFamilyAsset::StaticType()
                                                      : Keire::Texture2DAsset::StaticType();
                        if (m_StyleValueAssetPicker.Draw(ui, records, m_StyleValueAssetDraft,
                                                         {.Label = descriptor.DisplayName,
                                                          .EmptyLabel = "None",
                                                          .ExpectedType = expectedType,
                                                          .Reveal = [this](const Keire::AssetId asset)
                                                          { m_Controller.RevealUiBuilderAsset(asset); },
                                                          .AllowNone = true}))
                        {
                            value = AssetReference(m_StyleValueAssetDraft);
                            changed = true;
                        }
                    }
                    else if (descriptor.ValueKind == Keire::UiStyleValueKind::Gradient)
                        changed = DrawVisualGradient(ui, descriptor.Name, value);
                    else if (descriptor.ValueKind == Keire::UiStyleValueKind::Color)
                    {
                        Keire::UiColor color;
                        if (ParseHexColor(value, color) && ui.ColorEdit(descriptor.DisplayName, color))
                        {
                            value = EncodeHexColor(color);
                            changed = true;
                        }
                    }
                    else if (descriptor.ValueKind == Keire::UiStyleValueKind::Keyword && !descriptor.Keywords.empty())
                    {
                        if (auto combo = ui.BeginCombo(descriptor.DisplayName, value); combo)
                            for (const auto keyword : SplitKeywords(descriptor.Keywords))
                                if (ui.Selectable(keyword, value == keyword))
                                {
                                    value = keyword;
                                    changed = true;
                                }
                    }
                    else if (descriptor.ValueKind == Keire::UiStyleValueKind::Number ||
                             descriptor.ValueKind == Keire::UiStyleValueKind::Integer ||
                             descriptor.ValueKind == Keire::UiStyleValueKind::Length)
                    {
                        double number = 0.0;
                        std::string unit;
                        if (ParseNumericValue(value, number, unit))
                        {
                            double minimum = -100'000.0;
                            double maximum = 100'000.0;
                            double speed = descriptor.ValueKind == Keire::UiStyleValueKind::Number ? 0.01 : 1.0;
                            if (descriptor.Name == "opacity")
                            {
                                minimum = 0.0;
                                maximum = 1.0;
                            }
                            else if (descriptor.Name == "font-weight")
                            {
                                minimum = 1.0;
                                maximum = 1'000.0;
                            }
                            else if (descriptor.Name == "max-lines")
                            {
                                minimum = 0.0;
                                maximum = 65'535.0;
                            }
                            else if (descriptor.Name == "flex-grow" || descriptor.Name == "flex-shrink" ||
                                     descriptor.Name == "border-width" || descriptor.Name == "border-radius" ||
                                     descriptor.Name == "font-size")
                            {
                                minimum = 0.0;
                            }
                            if (ui.DragScalar(std::string(descriptor.DisplayName) + "##Typed" +
                                                  std::string(descriptor.Name),
                                              number, speed, minimum, maximum))
                            {
                                if (descriptor.ValueKind == Keire::UiStyleValueKind::Integer)
                                    number = std::round(number);
                                value = NumericValue(number, unit);
                                changed = true;
                            }
                            if (descriptor.ValueKind == Keire::UiStyleValueKind::Length &&
                                (unit == "px" || (unit == "%" && SupportsPercentage(descriptor.Name))))
                            {
                                ui.SameLine();
                                if (auto combo = ui.BeginCombo("##Unit" + std::string(descriptor.Name), unit); combo)
                                {
                                    if (ui.Selectable("px", unit == "px"))
                                    {
                                        unit = "px";
                                        value = NumericValue(number, unit);
                                        changed = true;
                                    }
                                    if (SupportsPercentage(descriptor.Name) && ui.Selectable("%", unit == "%"))
                                    {
                                        unit = "%";
                                        value = NumericValue(number, unit);
                                        changed = true;
                                    }
                                }
                            }
                        }
                        else
                        {
                            changed = ui.InputText(
                                std::string(descriptor.DisplayName) + "##" + std::string(descriptor.Name), value);
                            if (ui.LastItemState().Hovered)
                                ui.SetTooltip("This value uses a token, keyword, or advanced expression.",
                                              {.Delayed = true});
                        }
                    }
                    else
                        changed = ui.InputText(
                            std::string(descriptor.DisplayName) + "##" + std::string(descriptor.Name), value);
                    if (changed)
                    {
                        if (m_StyleEditInline)
                            (void)visualDocument.SetInlineStyleProperty(visualDocument.Selections(), descriptor.Name,
                                                                        value);
                        else
                            (void)styleDocument.SetProperty(selected, descriptor.Name, value);
                    }
                    ui.TextColored(
                        theme.MutedText,
                        std::string(property ? (m_StyleEditInline ? "Inline override | " : "Rule | ") : "Default | ") +
                            (descriptor.Inherited ? "Inherited | " : "") +
                            (descriptor.Animatable ? "Animatable" : "Static"));
                    ui.SameLine();
                    if (auto disabled = ui.BeginDisabled(!property); disabled)
                    {
                        if (ui.Button("Reset##StyleProperty" + std::string(descriptor.Name)))
                        {
                            if (m_StyleEditInline)
                                (void)visualDocument.RemoveInlineStyleProperty(visualDocument.Selections(),
                                                                               descriptor.Name);
                            else
                                (void)styleDocument.RemoveProperty(selected, descriptor.Name);
                        }
                    }
                    if (styleDocument.Asset())
                    {
                        ui.SameLine();
                        if (ui.Button("Promote##StyleProperty" + std::string(descriptor.Name)))
                        {
                            const auto token = UniqueTokenName(styleDocument.Definition(), descriptor.Name);
                            (void)styleDocument.SetToken(token, value);
                            if (m_StyleEditInline)
                                (void)visualDocument.SetInlineStyleProperty(visualDocument.Selections(),
                                                                            descriptor.Name, "var(" + token + ')');
                            else
                                (void)styleDocument.SetProperty(selected, descriptor.Name, "var(" + token + ')');
                        }
                    }
                }
            }
        }

        ui.Separator();
        if (auto combo = ui.BeginCombo("Add Property", m_NewStyleProperty.empty() ? "Choose" : m_NewStyleProperty);
            combo)
            for (const auto& descriptor : Keire::UiStylePropertyDescriptors())
                if (!FindProperty(rule, descriptor.Name) &&
                    ui.Selectable(std::string(CategoryName(descriptor.Category)) + " / " +
                                  std::string(descriptor.DisplayName)))
                {
                    m_NewStyleProperty = descriptor.Name;
                    m_NewStyleValue = descriptor.DefaultValue;
                }
        if (!m_NewStyleProperty.empty())
        {
            (void)ui.InputText("Value##NewStyleProperty", m_NewStyleValue);
            if (ui.Button("Add Property"))
            {
                if (m_StyleEditInline)
                    (void)visualDocument.SetInlineStyleProperty(visualDocument.Selections(), m_NewStyleProperty,
                                                                m_NewStyleValue);
                else
                    (void)styleDocument.SetProperty(selected, m_NewStyleProperty, m_NewStyleValue);
                m_NewStyleProperty.clear();
                m_NewStyleValue.clear();
            }
        }
    }

    void UiBuilderPanel::DrawStyleComputed(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.UiBuilderTheme();
        RefreshPreviewSnapshot();
        if (!m_PreviewSnapshot || !m_PreviewSnapshot->SelectedState)
        {
            ui.TextColoredWrapped(theme.MutedText, "Select an element to inspect its resolved style and layout.");
            return;
        }
        const auto& state = *m_PreviewSnapshot->SelectedState;
        ui.TextColored(theme.Accent, "COMPUTED STYLE");
        ui.Text("x " + std::to_string(state.Rect.X) + "  y " + std::to_string(state.Rect.Y));
        ui.Text("width " + std::to_string(state.Rect.Width) + "  height " + std::to_string(state.Rect.Height));
        ui.Separator();
        ui.Text("Opacity  " + std::to_string(state.Style.Opacity));
        ui.Text("Font size  " + std::to_string(state.Style.FontSize) + " px");
        ui.Text("Border  " + std::to_string(state.Style.BorderWidth) + " px");
        ui.Text("Radius  " + std::to_string(state.Style.CornerRadius) + " px");
        ui.Separator();
        ui.TextColored(theme.Accent, "PROVENANCE");
        if (m_PreviewSnapshot->SelectedStyleTrace.empty())
            ui.TextColoredWrapped(theme.MutedText, "No stylesheet rule matches the selected element.");
        for (const auto& trace : m_PreviewSnapshot->SelectedStyleTrace)
        {
            ui.Text(trace.Selector + "  | specificity " + std::to_string(trace.Specificity) + " | order " +
                    std::to_string(trace.SourceOrder));
            for (const auto& property : trace.AppliedProperties)
                ui.TextColored(theme.MutedText, "  " + property);
        }
        ui.Text("Style passes  " + std::to_string(m_PreviewSnapshot->Statistics.StylePasses));
        ui.Text("Layout passes  " + std::to_string(m_PreviewSnapshot->Statistics.LayoutPasses));
        ui.Text("Repaint passes  " + std::to_string(m_PreviewSnapshot->Statistics.RepaintPasses));
    }

    void UiBuilderPanel::DrawStyleSource(Keire::UiFrame& ui)
    {
        auto& styleDocument = m_Controller.UiBuilderStyleSheetState();
        const auto& theme = m_Controller.UiBuilderTheme();
        if (!styleDocument.Asset())
        {
            ui.TextColored(theme.MutedText, "Open a linked style sheet to edit source.");
            return;
        }
        if (m_StyleSourceGeneration != styleDocument.Generation())
        {
            m_StyleSourceGeneration = styleDocument.Generation();
            m_StyleSourceDraft = styleDocument.SourceText();
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            m_StyleSourceEditorState.CursorOffset =
                std::min(m_StyleSourceEditorState.CursorOffset, m_StyleSourceDraft.size());
        }
        ui.TextColored(theme.Accent, "SOURCE");
        ui.TextColoredWrapped(theme.MutedText,
                              "Completion, brace matching, property documentation, search, and formatting use the "
                              "same property registry as the runtime. Invalid drafts preserve the last valid preview.");

        (void)ui.InputTextWithHint("##UiStyleFind", "Find", m_StyleSourceFind);
        ui.SameLine();
        (void)ui.InputTextWithHint("##UiStyleReplace", "Replace", m_StyleSourceReplace);
        ui.SameLine();
        (void)ui.Checkbox("Case", m_StyleSourceFindCaseSensitive);
        ui.SameLine();
        if (ui.Button("Find All"))
        {
            m_StyleSourceMatches = m_StyleSourceEditor.Find(m_StyleSourceFind, m_StyleSourceFindCaseSensitive);
            m_StyleSourceMatch = 0U;
            if (!m_StyleSourceMatches.empty())
            {
                const auto& match = m_StyleSourceMatches.front();
                m_StyleSourceEditorState.CursorOffset = match.Offset;
                m_StyleSourceEditorState.SelectionBegin = match.Offset;
                m_StyleSourceEditorState.SelectionEnd = match.Offset + match.Length;
                m_StyleSourceEditorState.RequestCursor = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_StyleSourceMatches.empty()); disabled)
        {
            if (ui.Button("Next"))
            {
                m_StyleSourceMatch = (m_StyleSourceMatch + 1U) % m_StyleSourceMatches.size();
                const auto& match = m_StyleSourceMatches[m_StyleSourceMatch];
                m_StyleSourceEditorState.CursorOffset = match.Offset;
                m_StyleSourceEditorState.SelectionBegin = match.Offset;
                m_StyleSourceEditorState.SelectionEnd = match.Offset + match.Length;
                m_StyleSourceEditorState.RequestCursor = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_StyleSourceFind.empty()); disabled)
        {
            if (ui.Button("Replace All"))
            {
                m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
                const auto replacements = m_StyleSourceEditor.ReplaceAll(m_StyleSourceFind, m_StyleSourceReplace,
                                                                         m_StyleSourceFindCaseSensitive);
                if (replacements > 0U)
                {
                    m_StyleSourceDraft = m_StyleSourceEditor.Source();
                    (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
                    m_StyleSourceGeneration = styleDocument.Generation();
                    m_Message = "Replaced " + std::to_string(replacements) + " source occurrence(s).";
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Format"))
        {
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            if (m_StyleSourceEditor.Format())
            {
                m_StyleSourceDraft = m_StyleSourceEditor.Source();
                (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
                m_StyleSourceGeneration = styleDocument.Generation();
            }
        }

        if (!m_StyleSourceEditor.Rules().empty())
        {
            if (auto rules = ui.BeginCombo("Go to rule", "Select a selector"); rules)
            {
                for (const auto& rule : m_StyleSourceEditor.Rules())
                {
                    if (ui.Selectable(rule.Selector + "  (line " + std::to_string(rule.Line) + ")"))
                    {
                        m_StyleSourceEditorState.CursorOffset = rule.Offset;
                        m_StyleSourceEditorState.SelectionBegin = rule.Offset;
                        m_StyleSourceEditorState.SelectionEnd = rule.Offset + rule.Selector.size();
                        m_StyleSourceEditorState.RequestCursor = true;
                    }
                }
            }
        }

        (void)ui.InputCodeEditor("##UiBuilderStyleSource", m_StyleSourceDraft, m_StyleSourceEditorState, 30);
        const auto sourceState = ui.LastItemState();
        m_StyleSourceEditor.SetCursor(m_StyleSourceEditorState.CursorOffset);
        if (sourceState.Edited)
        {
            m_StyleSourceEditor.SetSource(m_StyleSourceDraft);
            m_StyleSourceEditor.SetCursor(m_StyleSourceEditorState.CursorOffset);
            m_StyleSourceParsePending = true;
            m_StyleSourceEditTime = std::chrono::steady_clock::now();
        }
        const auto cursor = m_StyleSourceEditor.CursorLocation();
        ui.TextColored(theme.MutedText, "Ln " + std::to_string(cursor.Line) + ", Col " + std::to_string(cursor.Column) +
                                            " | " + std::to_string(m_StyleSourceEditor.LineCount()) + " lines | " +
                                            std::to_string(m_StyleSourceEditor.Tokens().size()) + " syntax tokens");
        if (const auto brace = m_StyleSourceEditor.MatchingBrace(m_StyleSourceEditor.Cursor()))
        {
            ui.SameLine();
            ui.TextColored(theme.Accent, "matching brace at byte " + std::to_string(*brace));
        }
        if (const auto documentation = m_StyleSourceEditor.HoverDocumentation(m_StyleSourceEditor.Cursor()))
        {
            ui.TextColoredWrapped(theme.MutedText, *documentation);
        }
        if (sourceState.Active)
        {
            const auto completions = m_StyleSourceEditor.Completions(m_StyleSourceEditor.Cursor(), 8U);
            if (!completions.empty())
            {
                if (auto suggestions = ui.BeginChild("UiStyleSourceCompletions", {0.0F, 120.0F}, true); suggestions)
                {
                    ui.TextColored(theme.Accent, "COMPLETION");
                    for (const auto& completion : completions)
                    {
                        if (ui.Selectable(completion.Label + "##UiStyleCompletion" + completion.Insertion))
                        {
                            if (m_StyleSourceEditor.ApplyCompletion(m_StyleSourceEditor.Cursor(), completion))
                            {
                                m_StyleSourceDraft = m_StyleSourceEditor.Source();
                                m_StyleSourceEditorState.CursorOffset = m_StyleSourceEditor.Cursor();
                                m_StyleSourceEditorState.SelectionBegin = m_StyleSourceEditor.Cursor();
                                m_StyleSourceEditorState.SelectionEnd = m_StyleSourceEditor.Cursor();
                                m_StyleSourceEditorState.RequestCursor = true;
                                (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
                                m_StyleSourceGeneration = styleDocument.Generation();
                            }
                        }
                        if (ui.LastItemState().Hovered)
                            ui.SetTooltip(completion.Documentation);
                    }
                }
            }
        }
        const bool parseNow = m_StyleSourceParsePending && (sourceState.DeactivatedAfterEdit ||
                                                            std::chrono::steady_clock::now() - m_StyleSourceEditTime >=
                                                                std::chrono::milliseconds(150));
        if (parseNow)
        {
            (void)styleDocument.ApplySourceDraft(m_StyleSourceDraft);
            m_StyleSourceGeneration = styleDocument.Generation();
            m_StyleSourceParsePending = false;
        }
        if (const auto& diagnostic = styleDocument.SourceDiagnostic())
            ui.TextColoredWrapped(theme.Error, "Line " + std::to_string(diagnostic->Line) + ", column " +
                                                   std::to_string(diagnostic->Column) + ": " + diagnostic->Message);
        else
            ui.TextColored(theme.Success, "Valid draft | preview published");
        const bool externalConflict = styleDocument.ExternalConflict();
        if (externalConflict)
        {
            ui.TextColoredWrapped(theme.Warning, "The file changed outside Kéire. Compare or reload it before saving.");
            if (ui.Button(m_StyleExternalComparison.empty() ? "Compare External" : "Hide Comparison"))
            {
                m_StyleExternalComparison =
                    m_StyleExternalComparison.empty() ? styleDocument.ExternalComparison() : std::string{};
            }
            ui.SameLine();
            if (ui.Button("Reload External"))
            {
                try
                {
                    m_Controller.ReloadUiBuilderStyleSheet();
                    m_StyleSourceGeneration = 0;
                    m_StyleExternalComparison.clear();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
            if (!m_StyleExternalComparison.empty())
                if (auto comparison = ui.BeginChild("UiStyleExternalComparison", {0.0F, 180.0F}, true); comparison)
                    ui.TextColoredWrapped(theme.MutedText, m_StyleExternalComparison);
            if (ui.Button("Save Draft As..."))
                m_Controller.RequestSaveUiBuilderStyleSheetAs();
        }
        if (auto disabled = ui.BeginDisabled(!styleDocument.SourceValid() || externalConflict); disabled)
        {
            if (ui.Button("Save Style Sheet"))
            {
                try
                {
                    m_Controller.SaveUiBuilderStyleSheet();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Save As..."))
            m_Controller.RequestSaveUiBuilderStyleSheetAs();
        ui.SameLine();
        if (ui.Button("Reload"))
        {
            try
            {
                m_Controller.ReloadUiBuilderStyleSheet();
                m_StyleSourceGeneration = 0;
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanUndo());
            disabled)
            if (ui.Button("Undo"))
                (void)styleDocument.Undo();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!styleDocument.UndoContext() || !styleDocument.UndoContext()->CanRedo());
            disabled)
            if (ui.Button("Redo"))
                (void)styleDocument.Redo();
    }
} // namespace KeireEditor
