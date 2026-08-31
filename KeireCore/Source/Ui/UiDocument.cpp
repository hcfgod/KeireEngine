#include "Keire/Ui/UiToolkit.h"

#include "Keire/Ui/UiElements.h"
#include "KeireInternal/Ui/RuntimeUiStyleParsingInternal.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] bool ParseBoolean(const std::string_view value)
        {
            if (value == "true")
                return true;
            if (value == "false")
                return false;
            throw std::runtime_error("UI property requires 'true' or 'false'.");
        }

        [[nodiscard]] float ParseScalar(const std::string_view source)
        {
            auto value = source;
            if (value.ends_with("px"))
                value.remove_suffix(2);
            float result = 0.0F;
            const auto parse = std::from_chars(value.data(), value.data() + value.size(), result);
            if (parse.ec != std::errc{} || parse.ptr != value.data() + value.size() || !std::isfinite(result))
                throw std::runtime_error("UI property contains an invalid scalar value.");
            return result;
        }

        void ParseLength(const std::string_view source, float& pixels, float& percent)
        {
            if (!source.ends_with('%'))
            {
                pixels = ParseScalar(source);
                percent = -1.0F;
                return;
            }
            const auto percentage = ParseScalar(source.substr(0, source.size() - 1));
            if (percentage < 0.0F || percentage > 100.0F)
                throw std::runtime_error("UI percentage properties must be between 0% and 100%.");
            pixels = 0.0F;
            percent = percentage / 100.0F;
        }

        [[nodiscard]] float ParseByte(const std::string_view value)
        {
            unsigned int result = 0;
            const auto parse = std::from_chars(value.data(), value.data() + value.size(), result, 16);
            if (parse.ec != std::errc{} || parse.ptr != value.data() + value.size() || result > 255)
                throw std::runtime_error("UI color contains an invalid hexadecimal component.");
            return static_cast<float>(result) / 255.0F;
        }

        [[nodiscard]] Color ParseColor(const std::string_view source)
        {
            const auto value = Lower(source);
            if (value == "transparent")
                return {};
            if (value == "white")
                return {1.0F, 1.0F, 1.0F, 1.0F};
            if (value == "black")
                return {0.0F, 0.0F, 0.0F, 1.0F};
            if (value.size() != 7 && value.size() != 9)
                throw std::runtime_error("UI colors must use #RRGGBB or #RRGGBBAA syntax.");
            if (value.front() != '#')
                throw std::runtime_error("UI colors must begin with '#'.");
            return {ParseByte(std::string_view(value).substr(1, 2)), ParseByte(std::string_view(value).substr(3, 2)),
                    ParseByte(std::string_view(value).substr(5, 2)),
                    value.size() == 9 ? ParseByte(std::string_view(value).substr(7, 2)) : 1.0F};
        }

        [[nodiscard]] std::string_view Trim(const std::string_view value) noexcept
        {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
                return {};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        [[nodiscard]] std::vector<std::string_view> SplitCommaList(const std::string_view value)
        {
            std::vector<std::string_view> result;
            std::size_t cursor = 0;
            while (cursor <= value.size())
            {
                const auto separator = value.find(',', cursor);
                const auto item = Trim(value.substr(cursor, separator == std::string_view::npos ? std::string_view::npos
                                                                                                : separator - cursor));
                if (item.empty())
                    throw std::runtime_error("UI comma-separated property contains an empty value.");
                result.push_back(item);
                if (separator == std::string_view::npos)
                    break;
                cursor = separator + 1;
            }
            return result;
        }

        [[nodiscard]] float ParsePercentage(const std::string_view source)
        {
            if (!source.ends_with('%'))
                throw std::runtime_error("UI gradient values must use normalized percentages.");
            const auto value = ParseScalar(source.substr(0, source.size() - 1));
            if (value < 0.0F || value > 100.0F)
                throw std::runtime_error("UI gradient percentages must be between 0% and 100%.");
            return value / 100.0F;
        }

        [[nodiscard]] RuntimeUiGradientStop ParseGradientStop(const std::string_view source)
        {
            const auto separator = source.find_last_of(" \t\r\n");
            if (separator == std::string_view::npos)
                throw std::runtime_error("UI gradient stops require an explicit percentage offset.");
            const auto color = Trim(source.substr(0, separator));
            const auto offset = Trim(source.substr(separator + 1));
            if (color.empty() || offset.empty())
                throw std::runtime_error("UI gradient stop syntax is invalid.");
            return {.Offset = ParsePercentage(offset), .ColorValue = ParseColor(color)};
        }

        void ParseGradientStops(RuntimeUiGradient& result, const std::span<const std::string_view> values)
        {
            if (values.size() < 2 || values.size() > result.Stops.size())
                throw std::runtime_error("UI gradients require between two and eight stops.");
            float previous = -1.0F;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                result.Stops[index] = ParseGradientStop(values[index]);
                if (result.Stops[index].Offset < previous)
                    throw std::runtime_error("UI gradient stop offsets must be sorted.");
                previous = result.Stops[index].Offset;
            }
            result.StopCount = static_cast<std::uint8_t>(values.size());
        }

        [[nodiscard]] RuntimeUiGradient ParseGradient(const std::string_view source)
        {
            const auto value = Trim(source);
            if (value == "none")
                return {};
            constexpr std::string_view LinearPrefix = "linear-gradient(";
            constexpr std::string_view RadialPrefix = "radial-gradient(";
            if (!value.ends_with(')') || (!value.starts_with(LinearPrefix) && !value.starts_with(RadialPrefix)))
                throw std::runtime_error("UI backgrounds support linear-gradient(...) or radial-gradient(...).");
            const bool linear = value.starts_with(LinearPrefix);
            const auto prefix = linear ? LinearPrefix : RadialPrefix;
            const auto values = SplitCommaList(value.substr(prefix.size(), value.size() - prefix.size() - 1));
            if (values.size() < 3)
                throw std::runtime_error("UI gradients require geometry followed by at least two stops.");

            RuntimeUiGradient result;
            if (linear)
            {
                if (!values.front().ends_with("deg"))
                    throw std::runtime_error("UI linear gradients require an angle in degrees.");
                result.Kind = RuntimeUiGradientKind::Linear;
                result.LinearAngleDegrees = ParseScalar(values.front().substr(0, values.front().size() - 3));
            }
            else
            {
                const auto geometry = values.front();
                constexpr std::string_view CirclePrefix = "circle ";
                constexpr std::string_view At = " at ";
                if (!geometry.starts_with(CirclePrefix))
                    throw std::runtime_error("UI radial gradients require 'circle [radius%] at x% y%'.");
                const bool defaultRadius = geometry.starts_with("circle at ");
                const auto at = defaultRadius ? CirclePrefix.size() - 1 : geometry.find(At, CirclePrefix.size());
                if (!defaultRadius && at == std::string_view::npos)
                    throw std::runtime_error("UI radial gradients require 'circle [radius%] at x% y%'.");
                const auto radius = defaultRadius
                                        ? std::string_view{}
                                        : Trim(geometry.substr(CirclePrefix.size(), at - CirclePrefix.size()));
                const auto center = Trim(geometry.substr(at + At.size()));
                const auto centerSeparator = center.find_first_of(" \t\r\n");
                if (centerSeparator == std::string_view::npos)
                    throw std::runtime_error("UI radial gradients require two center percentages.");
                result.Kind = RuntimeUiGradientKind::Radial;
                result.RadialRadius = radius.empty() ? 0.5F : ParsePercentage(radius);
                result.RadialCenter = {ParsePercentage(Trim(center.substr(0, centerSeparator))),
                                       ParsePercentage(Trim(center.substr(centerSeparator + 1)))};
            }
            ParseGradientStops(result, std::span<const std::string_view>(values).subspan(1));
            return result;
        }

        [[nodiscard]] RuntimeUiTransitionProperty ParseTransitionProperty(const std::string_view value)
        {
            if (value == "all")
                return RuntimeUiTransitionProperty::All;
            if (value == "background-color")
                return RuntimeUiTransitionProperty::BackgroundColor;
            if (value == "color")
                return RuntimeUiTransitionProperty::ForegroundColor;
            if (value == "border-color")
                return RuntimeUiTransitionProperty::BorderColor;
            if (value == "opacity")
                return RuntimeUiTransitionProperty::Opacity;
            if (value == "left")
                return RuntimeUiTransitionProperty::Left;
            if (value == "top")
                return RuntimeUiTransitionProperty::Top;
            if (value == "width")
                return RuntimeUiTransitionProperty::Width;
            if (value == "height")
                return RuntimeUiTransitionProperty::Height;
            if (value == "border-radius")
                return RuntimeUiTransitionProperty::BorderRadius;
            throw std::runtime_error("UI transition-property contains an unsupported property.");
        }

        void ParseTransitionProperties(RuntimeUiStyle& style, const std::string_view source)
        {
            style.TransitionPropertyCount = 0;
            if (source == "none")
                return;
            const auto values = SplitCommaList(source);
            if (values.size() > style.TransitionProperties.size())
                throw std::runtime_error("UI transition-property exceeds the limit of eight values.");
            for (const auto value : values)
            {
                const auto property = ParseTransitionProperty(value);
                if (std::ranges::find(style.TransitionProperties.begin(),
                                      style.TransitionProperties.begin() + style.TransitionPropertyCount,
                                      property) != style.TransitionProperties.begin() + style.TransitionPropertyCount)
                    throw std::runtime_error("UI transition-property contains a duplicate value.");
                style.TransitionProperties[style.TransitionPropertyCount++] = property;
            }
        }

        [[nodiscard]] float ParseDuration(const std::string_view source)
        {
            auto value = source;
            float multiplier = 1.0F;
            if (value.ends_with("ms"))
            {
                value.remove_suffix(2);
                multiplier = 0.001F;
            }
            else if (value.ends_with('s'))
            {
                value.remove_suffix(1);
            }
            else
            {
                throw std::runtime_error("UI transition-duration requires seconds or milliseconds.");
            }
            const auto result = ParseScalar(Trim(value)) * multiplier;
            if (result < 0.0F || result > 60.0F)
                throw std::runtime_error("UI transition-duration must be between zero and 60 seconds.");
            return result;
        }

        void ParseTransitionDurations(RuntimeUiStyle& style, const std::string_view source)
        {
            const auto values = SplitCommaList(source);
            if (values.size() > style.TransitionDurations.size())
                throw std::runtime_error("UI transition-duration exceeds the limit of eight values.");
            style.TransitionDurationCount = static_cast<std::uint8_t>(values.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                style.TransitionDurations[index] = ParseDuration(values[index]);
        }

        [[nodiscard]] std::vector<float> ParseScalarList(const std::string_view value)
        {
            std::vector<float> result;
            std::size_t cursor = 0;
            while (cursor < value.size())
            {
                const auto begin = value.find_first_not_of(" \t\r\n", cursor);
                if (begin == std::string_view::npos)
                    break;
                const auto end = value.find_first_of(" \t\r\n", begin);
                result.push_back(ParseScalar(value.substr(begin, end - begin)));
                cursor = end == std::string_view::npos ? value.size() : end;
            }
            if (result.empty() || result.size() > 4)
                throw std::runtime_error("UI inset shorthand requires one to four scalar values.");
            return result;
        }

        void SetInsets(RuntimeUiInsets& destination, const std::string_view source)
        {
            const auto values = ParseScalarList(source);
            if (values.size() == 1)
                destination = {values[0], values[0], values[0], values[0]};
            else if (values.size() == 2)
                destination = {values[1], values[0], values[1], values[0]};
            else if (values.size() == 3)
                destination = {values[1], values[0], values[1], values[2]};
            else
                destination = {values[3], values[0], values[1], values[2]};
        }

        [[nodiscard]] RuntimeUiAlignment ParseAlignment(const std::string_view value)
        {
            if (value == "start" || value == "flex-start" || value == "left" || value == "top")
                return RuntimeUiAlignment::Start;
            if (value == "center")
                return RuntimeUiAlignment::Center;
            if (value == "end" || value == "flex-end" || value == "right" || value == "bottom")
                return RuntimeUiAlignment::End;
            if (value == "stretch")
                return RuntimeUiAlignment::Stretch;
            throw std::runtime_error("UI alignment property contains an unsupported value.");
        }

        [[nodiscard]] RuntimeUiJustification ParseJustification(const std::string_view value)
        {
            if (value == "start" || value == "flex-start")
                return RuntimeUiJustification::Start;
            if (value == "center")
                return RuntimeUiJustification::Center;
            if (value == "end" || value == "flex-end")
                return RuntimeUiJustification::End;
            if (value == "space-between")
                return RuntimeUiJustification::SpaceBetween;
            if (value == "space-around")
                return RuntimeUiJustification::SpaceAround;
            if (value == "space-evenly")
                return RuntimeUiJustification::SpaceEvenly;
            throw std::runtime_error("UI justification property contains an unsupported value.");
        }

        [[nodiscard]] RuntimeUiWrapMode ParseWrapMode(const std::string_view value)
        {
            if (value == "nowrap")
                return RuntimeUiWrapMode::NoWrap;
            if (value == "wrap")
                return RuntimeUiWrapMode::Wrap;
            if (value == "wrap-reverse")
                return RuntimeUiWrapMode::WrapReverse;
            throw std::runtime_error("UI flex-wrap property contains an unsupported value.");
        }

        [[nodiscard]] RuntimeUiElementType RuntimeType(const UiVisualElementType type) noexcept
        {
            switch (type)
            {
            case UiVisualElementType::Label:
                return RuntimeUiElementType::Text;
            case UiVisualElementType::Image:
                return RuntimeUiElementType::Image;
            case UiVisualElementType::Button:
                return RuntimeUiElementType::Button;
            case UiVisualElementType::TextField:
                return RuntimeUiElementType::InputField;
            case UiVisualElementType::Toggle:
                return RuntimeUiElementType::Toggle;
            case UiVisualElementType::Slider:
            case UiVisualElementType::ProgressBar:
                return RuntimeUiElementType::Slider;
            case UiVisualElementType::ScrollView:
            case UiVisualElementType::ListView:
            case UiVisualElementType::TreeView:
                return RuntimeUiElementType::ScrollView;
            case UiVisualElementType::Spacer:
                return RuntimeUiElementType::Spacer;
            default:
                // Unity-style retained VisualElements are flex containers whose default main axis is vertical.
                // Explicit flex-direction properties still replace this type during the style cascade.
                return RuntimeUiElementType::VerticalLayout;
            }
        }

        [[nodiscard]] Ref<Ui::VisualElement> CreateVisualElement(const UiVisualElementDefinition& definition)
        {
            using namespace Ui;
            switch (definition.Type)
            {
            case UiVisualElementType::TemplateContainer:
                return CreateRef<TemplateContainer>();
            case UiVisualElementType::Label:
                return CreateRef<Label>();
            case UiVisualElementType::Image:
                return CreateRef<Image>();
            case UiVisualElementType::Button:
                return CreateRef<Button>();
            case UiVisualElementType::TextField:
                return CreateRef<TextField>();
            case UiVisualElementType::Toggle:
                return CreateRef<Toggle>();
            case UiVisualElementType::Slider:
                return CreateRef<Slider>();
            case UiVisualElementType::ProgressBar:
                return CreateRef<ProgressBar>();
            case UiVisualElementType::ScrollView:
                return CreateRef<ScrollView>();
            case UiVisualElementType::ListView:
                return CreateRef<ListView>();
            case UiVisualElementType::TreeView:
                return CreateRef<TreeView>();
            case UiVisualElementType::DropdownField:
                return CreateRef<DropdownField>();
            case UiVisualElementType::Foldout:
                return CreateRef<Foldout>();
            case UiVisualElementType::TabView:
                return CreateRef<TabView>();
            case UiVisualElementType::Toolbar:
                return CreateRef<Toolbar>();
            case UiVisualElementType::Custom:
            {
                auto custom = UxmlElementRegistry::Create(definition.CustomType);
                return custom ? std::move(custom) : CreateRef<VisualElement>();
            }
            default:
                return CreateRef<VisualElement>();
            }
        }

        [[nodiscard]] RuntimeUiElementType RuntimeType(const Ui::VisualElement& element,
                                                       const UiVisualElementType fallback) noexcept
        {
            if (dynamic_cast<const Ui::Button*>(&element))
                return RuntimeUiElementType::Button;
            if (dynamic_cast<const Ui::TextField*>(&element))
                return RuntimeUiElementType::InputField;
            if (dynamic_cast<const Ui::Toggle*>(&element))
                return RuntimeUiElementType::Toggle;
            if (dynamic_cast<const Ui::Slider*>(&element) || dynamic_cast<const Ui::ProgressBar*>(&element))
                return RuntimeUiElementType::Slider;
            if (dynamic_cast<const Ui::ListView*>(&element) || dynamic_cast<const Ui::ScrollView*>(&element))
                return RuntimeUiElementType::ScrollView;
            if (dynamic_cast<const Ui::Image*>(&element))
                return RuntimeUiElementType::Image;
            if (dynamic_cast<const Ui::TextElement*>(&element))
                return RuntimeUiElementType::Text;
            return RuntimeType(fallback);
        }

        [[nodiscard]] std::string_view TypeName(const UiVisualElementDefinition& element) noexcept
        {
            if (element.Type == UiVisualElementType::Custom)
                return element.CustomType;
            switch (element.Type)
            {
            case UiVisualElementType::VisualElement:
                return "VisualElement";
            case UiVisualElementType::TemplateContainer:
                return "TemplateContainer";
            case UiVisualElementType::Slot:
                return "Slot";
            case UiVisualElementType::Label:
                return "Label";
            case UiVisualElementType::Image:
                return "Image";
            case UiVisualElementType::Button:
                return "Button";
            case UiVisualElementType::TextField:
                return "TextField";
            case UiVisualElementType::Toggle:
                return "Toggle";
            case UiVisualElementType::Slider:
                return "Slider";
            case UiVisualElementType::ProgressBar:
                return "ProgressBar";
            case UiVisualElementType::ScrollView:
                return "ScrollView";
            case UiVisualElementType::ListView:
                return "ListView";
            case UiVisualElementType::TreeView:
                return "TreeView";
            case UiVisualElementType::DropdownField:
                return "DropdownField";
            case UiVisualElementType::Foldout:
                return "Foldout";
            case UiVisualElementType::TabView:
                return "TabView";
            case UiVisualElementType::Toolbar:
                return "Toolbar";
            case UiVisualElementType::Spacer:
                return "Spacer";
            case UiVisualElementType::Custom:
                break;
            }
            return {};
        }

        [[nodiscard]] const std::string* NamedValue(const std::vector<UiNamedValue>& values,
                                                    const std::string_view name) noexcept
        {
            const auto found = std::ranges::find(values, name, &UiNamedValue::Name);
            return found == values.end() ? nullptr : &found->Value;
        }

        [[nodiscard]] AssetId ParseOptionalAsset(const std::string_view value)
        {
            return value.empty() ? AssetId{} : AssetId::Parse(value);
        }

        [[nodiscard]] bool HasState(const UiStylePseudoState values, const UiStylePseudoState state) noexcept
        {
            return (static_cast<std::uint16_t>(values) & static_cast<std::uint16_t>(state)) != 0;
        }
    } // namespace

    class UiDocument::Impl final
    {
      public:
        struct Element final
        {
            const UiVisualElementDefinition* Definition = nullptr;
            Ref<Ui::VisualElement> Visual;
            RuntimeUiElementId Runtime;
            RuntimeUiElementId Parent;
            UiStylePseudoState PseudoStates = UiStylePseudoState::None;
            std::unordered_map<std::string, std::string> Variables;
            std::vector<UiResolvedStyleSelectorTrace> SelectorTrace;
        };

        using SlotContent = std::unordered_map<std::string, std::vector<const UiVisualElementDefinition*>>;

        Impl(Ref<const UiVisualTreeAsset> visualTree, std::vector<Ref<const UiStyleSheetAsset>> styleSheets,
             Ref<RuntimeUiTree> sharedTree, const RuntimeUiElementId parent, UiTemplateResolver templateResolver,
             const std::size_t maximumEvents)
            : VisualTree(std::move(visualTree)), StyleSheets(std::move(styleSheets)), Tree(std::move(sharedTree)),
              TemplateResolver(std::move(templateResolver))
        {
            if (!VisualTree)
                throw std::invalid_argument("UI document requires a visual tree asset.");
            if (VisualTree->Definition().Root.Type == UiVisualElementType::Slot)
                throw std::invalid_argument("UI document roots cannot be slot declarations.");
            if (!Tree)
                Tree = CreateRef<RuntimeUiTree>(MaximumUiElements, maximumEvents);
            try
            {
                Build(VisualTree->Definition().Root, parent);
                if (Elements.empty())
                    throw std::runtime_error("UI document produced no runtime root element.");
                Root = Elements.front().Runtime;
                VisualRoot = Elements.front().Visual;
                RefreshStyles();
                Rebind({});
            }
            catch (...)
            {
                if (!Elements.empty())
                    (void)Tree->Destroy(Elements.front().Runtime);
                throw;
            }
        }

        ~Impl() noexcept
        {
            try
            {
                if (Tree && Root)
                    (void)Tree->Destroy(Root);
            }
            catch (...)
            {
                // Scene teardown must remain noexcept even if a corrupted tree cannot release its subtree.
            }
        }

        void ExpandTemplate(const UiVisualElementDefinition& instance, const RuntimeUiElementId parent,
                            Ui::VisualElement& visualParent)
        {
            if (!TemplateResolver)
                throw std::runtime_error("UI template " + instance.Template.ToString() + " has no asset resolver.");
            if (ActiveTemplates.size() >= MaximumUiTemplateDepth)
                throw std::runtime_error("UI template expansion exceeds the depth limit of 32.");
            if (std::ranges::find(ActiveTemplates, instance.Template) != ActiveTemplates.end())
                throw std::runtime_error("UI template expansion contains a cycle at " + instance.Template.ToString() +
                                         ".");
            auto visualTree = TemplateResolver(instance.Template);
            if (!visualTree)
                throw std::runtime_error("UI template " + instance.Template.ToString() + " could not be resolved.");

            SlotContent content;
            for (const auto& child : instance.Children)
                content[child.Slot].push_back(&child);
            ResolvedTemplates.push_back(std::move(visualTree));
            ActiveTemplates.push_back(instance.Template);
            std::unordered_set<std::string> declaredSlots;
            try
            {
                Build(ResolvedTemplates.back()->Definition().Root, parent, &visualParent, &content, &declaredSlots);
                for (const auto& [name, values] : content)
                {
                    if (!values.empty() && !declaredSlots.contains(name))
                    {
                        const auto displayName = name.empty() ? std::string("default") : name;
                        throw std::runtime_error("UI template content targets an unknown " + displayName + " slot.");
                    }
                }
                ActiveTemplates.pop_back();
            }
            catch (...)
            {
                ActiveTemplates.pop_back();
                throw;
            }
        }

        void Build(const UiVisualElementDefinition& definition, const RuntimeUiElementId parent,
                   Ui::VisualElement* visualParent = nullptr, const SlotContent* slotContent = nullptr,
                   std::unordered_set<std::string>* declaredSlots = nullptr)
        {
            if (definition.Type == UiVisualElementType::Slot)
            {
                if (declaredSlots != nullptr && !declaredSlots->insert(definition.Name).second)
                    throw std::runtime_error("UI template declares a duplicate slot named '" + definition.Name + "'.");
                if (slotContent != nullptr)
                {
                    const auto supplied = slotContent->find(definition.Name);
                    if (supplied != slotContent->end() && !supplied->second.empty())
                    {
                        for (const auto* child : supplied->second)
                            Build(*child, parent, visualParent);
                        return;
                    }
                }
                for (const auto& child : definition.Children)
                    Build(child, parent, visualParent, slotContent, declaredSlots);
                return;
            }

            auto visual = CreateVisualElement(definition);
            visual->SetStableId(definition.StableId);
            visual->SetName(definition.Name);
            for (const auto& className : definition.Classes)
                visual->AddToClassList(className);
            for (const auto& binding : definition.Bindings)
                visual->SetAuthoredBinding(binding);
            if (visualParent)
                visualParent->Add(visual);

            const auto runtime = Tree->Create(RuntimeType(*visual, definition.Type), parent);
            Elements.push_back({&definition, visual, runtime, parent});
            StableIds.emplace(definition.StableId, runtime);
            if (!definition.Name.empty())
                Names.emplace(definition.Name, runtime);
            RuntimeIndices.emplace(runtime.Value(), Elements.size() - 1);

            RuntimeUiContent content;
            if (const auto* value = NamedValue(definition.Attributes, "text"))
            {
                content.Text = *value;
                if (auto* text = dynamic_cast<Ui::TextElement*>(visual.Get()))
                    text->SetText(*value);
                else if (auto* foldout = dynamic_cast<Ui::Foldout*>(visual.Get()))
                    foldout->SetText(*value);
            }
            if (const auto* value = NamedValue(definition.Attributes, "image"))
            {
                content.Image = ParseOptionalAsset(*value);
                if (auto* image = dynamic_cast<Ui::Image*>(visual.Get()))
                    image->SetSource(content.Image);
            }
            if (const auto* value = NamedValue(definition.Attributes, "render-texture"))
                content.RenderTexture = ParseOptionalAsset(*value);
            if (const auto* value = NamedValue(definition.Attributes, "font"))
                content.Font = ParseOptionalAsset(*value);
            if (content.Image && content.RenderTexture)
                throw std::runtime_error("UI image elements cannot reference both an asset image and render texture.");
            if (const auto* value = NamedValue(definition.Attributes, "accessibility-label"))
                content.AccessibilityLabel = *value;
            if (const auto* value = NamedValue(definition.Attributes, "accessibility-hint"))
                content.AccessibilityHint = *value;

            RuntimeUiControlState control;
            if (const auto* value = NamedValue(definition.Attributes, "minimum"))
                control.Minimum = ParseScalar(*value);
            if (const auto* value = NamedValue(definition.Attributes, "maximum"))
                control.Maximum = ParseScalar(*value);
            if (const auto* value = NamedValue(definition.Attributes, "value"))
            {
                if (definition.Type == UiVisualElementType::TextField ||
                    definition.Type == UiVisualElementType::DropdownField)
                {
                    content.Text = *value;
                }
                else
                {
                    control.Value = ParseScalar(*value);
                }
            }
            if (const auto* value = NamedValue(definition.Attributes, "checked"))
                control.Checked = ParseBoolean(*value);
            if (auto* slider = dynamic_cast<Ui::Slider*>(visual.Get()))
            {
                slider->SetRange(control.Minimum, control.Maximum);
                slider->SetValueWithoutNotify(control.Value);
            }
            else if (auto* progress = dynamic_cast<Ui::ProgressBar*>(visual.Get()))
            {
                progress->SetRange(control.Minimum, control.Maximum);
                progress->SetValueWithoutNotify(control.Value);
            }
            else if (auto* toggle = dynamic_cast<Ui::Toggle*>(visual.Get()))
            {
                toggle->SetValueWithoutNotify(control.Checked);
            }
            else if (auto* textField = dynamic_cast<Ui::TextField*>(visual.Get()))
            {
                textField->SetValueWithoutNotify(content.Text);
            }
            if (!Tree->SetContent(runtime, content))
                throw std::runtime_error("UI element content is invalid.");
            if (const auto* value = NamedValue(definition.Attributes, "content-width"))
                control.ContentSize.X = ParseScalar(*value);
            if (const auto* value = NamedValue(definition.Attributes, "content-height"))
                control.ContentSize.Y = ParseScalar(*value);
            if (const auto* value = NamedValue(definition.Attributes, "scroll-sensitivity"))
                control.ScrollSensitivity = ParseScalar(*value);
            (void)Tree->SetControl(runtime, control);
            const bool defaultInteractable =
                definition.Type == UiVisualElementType::Button || definition.Type == UiVisualElementType::TextField ||
                definition.Type == UiVisualElementType::Toggle || definition.Type == UiVisualElementType::Slider ||
                definition.Type == UiVisualElementType::DropdownField ||
                definition.Type == UiVisualElementType::Foldout;
            const auto* interactable = NamedValue(definition.Attributes, "interactable");
            const bool isInteractable = interactable == nullptr ? defaultInteractable : ParseBoolean(*interactable);
            (void)Tree->SetInteractable(runtime, isInteractable);
            visual->SetFocusable(isInteractable);
            if (const auto* value = NamedValue(definition.Attributes, "enabled"))
            {
                (void)Tree->SetEnabled(runtime, ParseBoolean(*value));
                visual->SetEnabled(ParseBoolean(*value));
            }
            if (const auto* value = NamedValue(definition.Attributes, "visible"))
                (void)Tree->SetVisible(runtime, ParseBoolean(*value));
            if (definition.Type == UiVisualElementType::TemplateContainer)
                ExpandTemplate(definition, runtime, *visual);
            else
            {
                for (const auto& child : definition.Children)
                    Build(child, runtime, visual.Get(), slotContent, declaredSlots);
            }
        }

        [[nodiscard]] bool MatchesPart(const Element& element, const UiStyleSelectorPart& part) const
        {
            if (!part.Type.empty() && part.Type != element.Visual->Type())
                return false;
            if (!part.Name.empty() && part.Name != element.Visual->Name())
                return false;
            if (!std::ranges::all_of(part.Classes,
                                     [&](const auto& value) { return element.Visual->ClassListContains(value); }))
                return false;
            auto required = static_cast<std::uint16_t>(part.States);
            const auto root = static_cast<std::uint16_t>(UiStylePseudoState::Root);
            if ((required & root) != 0U)
            {
                if (element.Parent)
                    return false;
                required &= static_cast<std::uint16_t>(~root);
            }
            const auto current = static_cast<std::uint16_t>(element.PseudoStates);
            return (required & current) == required;
        }

        [[nodiscard]] const Element* ParentOf(const Element& element) const noexcept
        {
            if (!element.Parent)
                return nullptr;
            const auto found = RuntimeIndices.find(element.Parent.Value());
            return found == RuntimeIndices.end() ? nullptr : &Elements[found->second];
        }

        [[nodiscard]] bool Matches(const Element& element, const UiStyleRuleDefinition& rule) const
        {
            const Element* current = &element;
            for (std::size_t reverse = rule.Parts.size(); reverse > 0; --reverse)
            {
                const auto& part = rule.Parts[reverse - 1];
                if (!current || !MatchesPart(*current, part))
                    return false;
                if (reverse == 1)
                    return true;
                if (part.Combinator == UiStyleCombinator::Child)
                {
                    current = ParentOf(*current);
                }
                else if (part.Combinator == UiStyleCombinator::Descendant)
                {
                    const auto& required = rule.Parts[reverse - 2];
                    current = ParentOf(*current);
                    while (current && !MatchesPart(*current, required))
                        current = ParentOf(*current);
                    if (!current)
                        return false;
                    --reverse;
                    if (reverse == 1)
                        return true;
                    current = ParentOf(*current);
                }
                else
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Element* FindElement(const RuntimeUiElementId runtime) noexcept
        {
            const auto found = RuntimeIndices.find(runtime.Value());
            return found == RuntimeIndices.end() ? nullptr : &Elements[found->second];
        }

        [[nodiscard]] const Element* FindElement(const RuntimeUiElementId runtime) const noexcept
        {
            const auto found = RuntimeIndices.find(runtime.Value());
            return found == RuntimeIndices.end() ? nullptr : &Elements[found->second];
        }

        [[nodiscard]] bool SynchronizeVisualState(Element& element)
        {
            const auto state = Tree->State(element.Runtime);
            if (!state)
                return false;
            if (auto* field = dynamic_cast<Ui::TextField*>(element.Visual.Get()))
                field->SetValueWithoutNotify(state->Content.Text);
            else if (auto* text = dynamic_cast<Ui::TextElement*>(element.Visual.Get()))
                text->SetText(state->Content.Text);
            else if (auto* foldout = dynamic_cast<Ui::Foldout*>(element.Visual.Get()))
                foldout->SetText(state->Content.Text);
            if (auto* image = dynamic_cast<Ui::Image*>(element.Visual.Get()))
                image->SetSource(state->Content.Image);
            if (auto* toggle = dynamic_cast<Ui::Toggle*>(element.Visual.Get()))
                toggle->SetValueWithoutNotify(state->Control.Checked);
            else if (auto* slider = dynamic_cast<Ui::Slider*>(element.Visual.Get()))
                slider->SetValueWithoutNotify(state->Control.Value);
            else if (auto* progress = dynamic_cast<Ui::ProgressBar*>(element.Visual.Get()))
                progress->SetValueWithoutNotify(state->Control.Value);
            element.Visual->SetEnabled(state->Enabled);

            UiStylePseudoState pseudoStates = UiStylePseudoState::None;
            if (state->Hovered)
                pseudoStates = pseudoStates | UiStylePseudoState::Hover;
            if (state->Pressed)
                pseudoStates = pseudoStates | UiStylePseudoState::Active;
            if (state->Focused)
                pseudoStates = pseudoStates | UiStylePseudoState::Focus;
            if (!state->Enabled)
                pseudoStates = pseudoStates | UiStylePseudoState::Disabled;
            if (state->Control.Checked)
                pseudoStates = pseudoStates | UiStylePseudoState::Checked;
            if (pseudoStates == element.PseudoStates)
                return false;
            element.PseudoStates = pseudoStates;
            return true;
        }

        void SynchronizeRuntimeState(Element& element)
        {
            const auto state = Tree->State(element.Runtime);
            if (!state)
                return;
            auto content = state->Content;
            if (const auto* field = dynamic_cast<const Ui::TextField*>(element.Visual.Get()))
                content.Text = field->Value();
            else if (const auto* text = dynamic_cast<const Ui::TextElement*>(element.Visual.Get()))
                content.Text = text->Text();
            else if (const auto* foldout = dynamic_cast<const Ui::Foldout*>(element.Visual.Get()))
                content.Text = foldout->Text();
            if (const auto* image = dynamic_cast<const Ui::Image*>(element.Visual.Get()))
                content.Image = image->Source();
            (void)Tree->SetContent(element.Runtime, std::move(content));

            auto control = state->Control;
            if (const auto* toggle = dynamic_cast<const Ui::Toggle*>(element.Visual.Get()))
                control.Checked = toggle->Value();
            else if (const auto* slider = dynamic_cast<const Ui::Slider*>(element.Visual.Get()))
                control.Value = slider->Value();
            else if (const auto* progress = dynamic_cast<const Ui::ProgressBar*>(element.Visual.Get()))
                control.Value = progress->Value();
            (void)Tree->SetControl(element.Runtime, control);
            (void)Tree->SetEnabled(element.Runtime, element.Visual->EnabledInHierarchy());
        }

        [[nodiscard]] bool SynchronizeInteractionStates()
        {
            bool cascadeChanged = false;
            for (auto& element : Elements)
                cascadeChanged = SynchronizeVisualState(element) || cascadeChanged;
            if (cascadeChanged)
                RefreshStyles();
            return cascadeChanged;
        }

        [[nodiscard]] bool DispatchRuntimeEvent(const RuntimeUiEvent& runtimeEvent)
        {
            auto* element = FindElement(runtimeEvent.Target);
            if (!element)
                return false;
            (void)SynchronizeInteractionStates();
            const auto initializePointer = [&runtimeEvent](Ui::PointerEventBase& event)
            {
                event.PointerId = 0;
                event.Button = static_cast<std::int32_t>(runtimeEvent.Button);
                event.Position = {runtimeEvent.PointerX, runtimeEvent.PointerY};
            };
            const auto dispatchPointer = [&](auto& event)
            {
                initializePointer(event);
                element->Visual->SendEvent(event);
                return event.DefaultPrevented();
            };
            switch (runtimeEvent.Type)
            {
            case RuntimeUiEventType::PointerEnter:
            {
                Ui::PointerEnterEvent event;
                return dispatchPointer(event);
            }
            case RuntimeUiEventType::PointerExit:
            {
                Ui::PointerLeaveEvent event;
                return dispatchPointer(event);
            }
            case RuntimeUiEventType::PointerDown:
            {
                Ui::PointerDownEvent event;
                return dispatchPointer(event);
            }
            case RuntimeUiEventType::PointerUp:
            {
                Ui::PointerUpEvent event;
                return dispatchPointer(event);
            }
            case RuntimeUiEventType::Click:
            {
                Ui::ClickEvent event;
                return dispatchPointer(event);
            }
            case RuntimeUiEventType::Focus:
            {
                element->Visual->Focus();
                return false;
            }
            case RuntimeUiEventType::Blur:
            {
                element->Visual->Blur();
                return false;
            }
            case RuntimeUiEventType::Submit:
            {
                Ui::SubmitEvent event;
                element->Visual->SendEvent(event);
                return event.DefaultPrevented();
            }
            case RuntimeUiEventType::Cancel:
            {
                Ui::CancelEvent event;
                element->Visual->SendEvent(event);
                return event.DefaultPrevented();
            }
            case RuntimeUiEventType::ValueChanged:
            {
                if (const auto* toggle = dynamic_cast<const Ui::Toggle*>(element->Visual.Get()))
                {
                    Ui::ChangeEvent<bool> event;
                    event.PreviousValue = toggle->Value();
                    event.NewValue = toggle->Value();
                    element->Visual->SendEvent(event);
                    return event.DefaultPrevented();
                }
                Ui::ChangeEvent<float> event;
                if (const auto* slider = dynamic_cast<const Ui::Slider*>(element->Visual.Get()))
                    event.PreviousValue = event.NewValue = slider->Value();
                element->Visual->SendEvent(event);
                return event.DefaultPrevented();
            }
            case RuntimeUiEventType::TextChanged:
            {
                Ui::ChangeEvent<std::string> event;
                if (const auto* field = dynamic_cast<const Ui::TextField*>(element->Visual.Get()))
                    event.PreviousValue = event.NewValue = field->Value();
                element->Visual->SendEvent(event);
                return event.DefaultPrevented();
            }
            }
            return false;
        }

        [[nodiscard]] bool Advance(const float deltaSeconds)
        {
            bool changed = SynchronizeInteractionStates();
            UpdateBindings();
            return Tree->AdvanceTransitions(deltaSeconds) || changed;
        }

        void UpdateBindings()
        {
            for (auto& element : Elements)
            {
                element.Visual->UpdateBindings();
                SynchronizeRuntimeState(element);
            }
        }

        void Rebind(Ref<UiDocumentBindingSource> source)
        {
            BindingDiagnostics.clear();
            if (!source)
            {
                for (auto& element : Elements)
                {
                    for (const auto& binding : element.Definition->Bindings)
                    {
                        (void)element.Visual->RemoveBinding(binding.Property);
                        BindingDiagnostics.push_back({.Element = element.Definition->StableId,
                                                      .Property = binding.Property,
                                                      .Path = binding.Path,
                                                      .Code = "UiBindingSourceUnavailable",
                                                      .Message = "Authored binding '" + binding.Property +
                                                                 "' cannot resolve path '" + binding.Path +
                                                                 "' until the UI Document receives an explicit "
                                                                 "binding source."});
                    }
                }
                BindingSource.Reset();
                return;
            }

            try
            {
                for (auto& element : Elements)
                {
                    for (const auto& authored : element.Definition->Bindings)
                    {
                        Ui::DataBinding binding;
                        binding.SourcePath = authored.Path;
                        binding.Mode = authored.Mode == "TwoWay"    ? Ui::BindingMode::TwoWay
                                       : authored.Mode == "OneTime" ? Ui::BindingMode::OneTime
                                                                    : Ui::BindingMode::OneWay;
                        binding.Read = [source, path = authored.Path]() { return source->Read(path); };
                        if (binding.Mode == Ui::BindingMode::TwoWay)
                        {
                            binding.Write = [source, path = authored.Path](const std::any& value)
                            { source->Write(path, value); };
                        }
                        element.Visual->SetBinding(authored.Property, std::move(binding));
                    }
                }
                for (auto& element : Elements)
                {
                    element.Visual->UpdateBindings();
                    SynchronizeRuntimeState(element);
                }
                BindingSource = std::move(source);
            }
            catch (const std::exception& error)
            {
                for (auto& element : Elements)
                {
                    for (const auto& binding : element.Definition->Bindings)
                        (void)element.Visual->RemoveBinding(binding.Property);
                }
                BindingDiagnostics.push_back({.Code = "UiBindingResolutionFailed", .Message = error.what()});
                throw;
            }
        }

        [[nodiscard]] static std::string ResolveVariable(std::string value,
                                                         const std::unordered_map<std::string, std::string>& variables)
        {
            if (!value.starts_with("var(") || !value.ends_with(')'))
                return value;
            const auto name = value.substr(4, value.size() - 5);
            const auto found = variables.find(name);
            if (found == variables.end())
                throw std::runtime_error("UI style references an undefined variable.");
            return found->second;
        }

        static void ApplyProperty(RuntimeUiStyle& style, RuntimeUiElementType& runtimeType, bool& visible,
                                  const std::string_view name, const std::string_view value)
        {
            if (Detail::TryApplyRuntimeUiStyleV2Property(style, name, value))
                return;
            if (name == "width")
                ParseLength(value, style.Width, style.WidthPercent);
            else if (name == "height")
                ParseLength(value, style.Height, style.HeightPercent);
            else if (name == "min-width")
                ParseLength(value, style.MinimumWidth, style.MinimumWidthPercent);
            else if (name == "min-height")
                ParseLength(value, style.MinimumHeight, style.MinimumHeightPercent);
            else if (name == "max-width")
                ParseLength(value, style.MaximumWidth, style.MaximumWidthPercent);
            else if (name == "max-height")
                ParseLength(value, style.MaximumHeight, style.MaximumHeightPercent);
            else if (name == "left")
                ParseLength(value, style.X, style.XPercent);
            else if (name == "top")
                ParseLength(value, style.Y, style.YPercent);
            else if (name == "position")
            {
                if (value == "absolute")
                    style.Position = RuntimeUiPositionMode::Absolute;
                else if (value == "relative" || value == "flow")
                    style.Position = RuntimeUiPositionMode::Flow;
                else
                    throw std::runtime_error("UI position property contains an unsupported value.");
            }
            else if (name == "flex-grow")
                style.FlexGrow = ParseScalar(value);
            else if (name == "flex-shrink")
                style.FlexShrink = ParseScalar(value);
            else if (name == "flex-direction")
            {
                if (value == "row" || value == "row-reverse")
                    runtimeType = RuntimeUiElementType::HorizontalLayout;
                else if (value == "column" || value == "column-reverse")
                    runtimeType = RuntimeUiElementType::VerticalLayout;
                else
                    throw std::runtime_error("UI flex-direction property contains an unsupported value.");
                style.ReverseChildren = value == "row-reverse" || value == "column-reverse";
            }
            else if (name == "flex-wrap")
                style.Wrap = ParseWrapMode(value);
            else if (name == "gap")
                style.Gap = ParseScalar(value);
            else if (name == "margin")
                SetInsets(style.Margin, value);
            else if (name == "padding")
                SetInsets(style.Padding, value);
            else if (name == "margin-left")
                style.Margin.Left = ParseScalar(value);
            else if (name == "margin-top")
                style.Margin.Top = ParseScalar(value);
            else if (name == "margin-right")
                style.Margin.Right = ParseScalar(value);
            else if (name == "margin-bottom")
                style.Margin.Bottom = ParseScalar(value);
            else if (name == "padding-left")
                style.Padding.Left = ParseScalar(value);
            else if (name == "padding-top")
                style.Padding.Top = ParseScalar(value);
            else if (name == "padding-right")
                style.Padding.Right = ParseScalar(value);
            else if (name == "padding-bottom")
                style.Padding.Bottom = ParseScalar(value);
            else if (name == "background-color")
                style.Background = ParseColor(value);
            else if (name == "background" || name == "background-image")
                style.BackgroundGradient = ParseGradient(value);
            else if (name == "color")
                style.Foreground = ParseColor(value);
            else if (name == "border-color")
                style.Border = ParseColor(value);
            else if (name == "border-width")
                style.BorderWidth = ParseScalar(value);
            else if (name == "border-radius")
                style.CornerRadius = ParseScalar(value);
            else if (name == "opacity")
                style.Opacity = ParseScalar(value);
            else if (name == "transition-property")
                ParseTransitionProperties(style, value);
            else if (name == "transition-duration")
                ParseTransitionDurations(style, value);
            else if (name == "font-size")
                style.FontSize = ParseScalar(value);
            else if (name == "text-align")
                style.HorizontalAlignment = ParseAlignment(value);
            else if (name == "vertical-align")
                style.VerticalAlignment = ParseAlignment(value);
            else if (name == "overflow")
            {
                if (value == "visible")
                    style.ClipChildren = false;
                else if (value == "hidden" || value == "clip")
                    style.ClipChildren = true;
                else
                    throw std::runtime_error("UI overflow property contains an unsupported value.");
            }
            else if (name == "align-items")
            {
                const auto alignment = ParseAlignment(value);
                style.ChildHorizontalAlignment = alignment;
                style.ChildVerticalAlignment = alignment;
                style.ControlChildWidth = value == "stretch";
                style.ControlChildHeight = value == "stretch";
                style.ForceExpandWidth = value == "stretch";
                style.ForceExpandHeight = value == "stretch";
            }
            else if (name == "align-self")
            {
                style.HasAlignSelf = value != "auto";
                if (style.HasAlignSelf)
                    style.AlignSelf = ParseAlignment(value);
            }
            else if (name == "justify-content")
                style.JustifyContent = ParseJustification(value);
            else if (name == "display")
                visible = value != "none";
            else if (name == "sorting-order")
                style.SortingOrder = static_cast<std::int32_t>(ParseScalar(value));
            else if (!name.starts_with("--"))
                throw std::runtime_error("UI style contains an unsupported runtime property: " + std::string(name));
        }

        void RefreshStyles()
        {
            const auto started = std::chrono::steady_clock::now();
            for (auto& element : Elements)
            {
                struct CascadedValue final
                {
                    std::uint32_t Specificity = 0;
                    std::size_t Order = 0;
                    std::size_t TraceIndex = std::numeric_limits<std::size_t>::max();
                    std::string Value;
                };
                std::unordered_map<std::string, CascadedValue> cascade;
                element.SelectorTrace.clear();
                std::size_t order = 0;
                for (const auto& sheet : StyleSheets)
                {
                    if (!sheet)
                        continue;
                    for (const auto& rule : sheet->Definition().Rules)
                    {
                        ++order;
                        if (rule.Media && !MatchesUiStyleMediaCondition(*rule.Media, StyleEvaluationContext))
                            continue;
                        if (!Matches(element, rule))
                            continue;
                        std::size_t traceIndex = std::numeric_limits<std::size_t>::max();
                        if (element.SelectorTrace.size() < MaximumUiSelectorTraceEntries)
                        {
                            traceIndex = element.SelectorTrace.size();
                            element.SelectorTrace.push_back(
                                {.Selector = rule.Selector, .Specificity = rule.Specificity, .SourceOrder = order});
                        }
                        for (const auto& property : rule.Properties)
                        {
                            auto& selected = cascade[property.Name];
                            if (selected.Value.empty() || rule.Specificity > selected.Specificity ||
                                (rule.Specificity == selected.Specificity && order >= selected.Order))
                                selected = {rule.Specificity, order, traceIndex, property.Value};
                        }
                    }
                }
                std::size_t inlineTrace = std::numeric_limits<std::size_t>::max();
                if (!element.Definition->InlineStyles.empty() &&
                    element.SelectorTrace.size() < MaximumUiSelectorTraceEntries)
                {
                    inlineTrace = element.SelectorTrace.size();
                    element.SelectorTrace.push_back(
                        {.Selector = "<inline>", .Specificity = 1'000'000, .SourceOrder = order + 1});
                }
                for (const auto& property : element.Definition->InlineStyles)
                    cascade[property.Name] = {1'000'000, ++order, inlineTrace, property.Value};

                std::unordered_map<std::string, std::string> variables;
                RuntimeUiStyle style;
                if (const auto* parent = ParentOf(element))
                {
                    variables = parent->Variables;
                    if (const auto parentState = Tree->State(parent->Runtime))
                    {
                        style.Foreground = parentState->Style.Foreground;
                        style.FontSize = parentState->Style.FontSize;
                        style.FontFamily = parentState->Style.FontFamily;
                        style.FontWeight = parentState->Style.FontWeight;
                        style.FontSlant = parentState->Style.FontSlant;
                        style.LineHeight = parentState->Style.LineHeight;
                        style.LetterSpacing = parentState->Style.LetterSpacing;
                        style.WordSpacing = parentState->Style.WordSpacing;
                        style.TextWrap = parentState->Style.TextWrap;
                        style.TextOverflow = parentState->Style.TextOverflow;
                        style.TextDirection = parentState->Style.TextDirection;
                        style.Language = parentState->Style.Language;
                        style.MaximumLines = parentState->Style.MaximumLines;
                    }
                }
                for (const auto& [name, property] : cascade)
                {
                    if (name.starts_with("--"))
                        variables[name] = ResolveVariable(property.Value, variables);
                }
                element.Variables = variables;
                RuntimeUiElementType runtimeType = RuntimeType(element.Definition->Type);
                bool visible = NamedValue(element.Definition->Attributes, "visible") == nullptr ||
                               ParseBoolean(*NamedValue(element.Definition->Attributes, "visible"));
                std::vector<std::pair<std::string, CascadedValue>> ordered(cascade.begin(), cascade.end());
                std::ranges::sort(ordered, {}, [](const auto& item) { return item.second.Order; });
                for (const auto& [name, property] : ordered)
                {
                    if (property.TraceIndex < element.SelectorTrace.size())
                        element.SelectorTrace[property.TraceIndex].AppliedProperties.push_back(name);
                    ApplyProperty(style, runtimeType, visible, name, ResolveVariable(property.Value, variables));
                }
                // Scrolling is live interaction state and selector recascade must not reset it.
                if (const auto runtimeState = Tree->State(element.Runtime))
                    style.ContentOffset = runtimeState->Style.ContentOffset;
                (void)Tree->SetType(element.Runtime, runtimeType);
                (void)Tree->SetStyle(element.Runtime, style);
                element.Visual->Style() = style;
                (void)Tree->SetVisible(element.Runtime, visible);
            }
            Tree->ReportStylePass(
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count());
        }

        Ref<const UiVisualTreeAsset> VisualTree;
        std::vector<Ref<const UiStyleSheetAsset>> StyleSheets;
        Ref<RuntimeUiTree> Tree;
        UiStyleEvaluationContext StyleEvaluationContext;
        Ref<Ui::VisualElement> VisualRoot;
        UiTemplateResolver TemplateResolver;
        Ref<UiDocumentBindingSource> BindingSource;
        std::vector<UiDocumentBindingDiagnostic> BindingDiagnostics;
        std::vector<Ref<const UiVisualTreeAsset>> ResolvedTemplates;
        std::vector<AssetId> ActiveTemplates;
        RuntimeUiElementId Root;
        std::vector<Element> Elements;
        std::unordered_map<AssetId, RuntimeUiElementId> StableIds;
        std::unordered_map<std::string, RuntimeUiElementId> Names;
        std::unordered_map<std::uint64_t, std::size_t> RuntimeIndices;
    };

    UiDocument::UiDocument(Ref<const UiVisualTreeAsset> visualTree,
                           std::vector<Ref<const UiStyleSheetAsset>> styleSheets, const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(std::move(visualTree), std::move(styleSheets), Ref<RuntimeUiTree>{},
                                        RuntimeUiElementId{}, UiTemplateResolver{}, maximumEvents))
    {
    }

    UiDocument::UiDocument(Ref<const UiVisualTreeAsset> visualTree,
                           std::vector<Ref<const UiStyleSheetAsset>> styleSheets, UiTemplateResolver templateResolver,
                           const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(std::move(visualTree), std::move(styleSheets), Ref<RuntimeUiTree>{},
                                        RuntimeUiElementId{}, std::move(templateResolver), maximumEvents))
    {
    }

    UiDocument::UiDocument(Ref<const UiVisualTreeAsset> visualTree,
                           std::vector<Ref<const UiStyleSheetAsset>> styleSheets, Ref<RuntimeUiTree> sharedTree,
                           const RuntimeUiElementId parent, const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(std::move(visualTree), std::move(styleSheets), std::move(sharedTree), parent,
                                        UiTemplateResolver{}, maximumEvents))
    {
    }

    UiDocument::UiDocument(Ref<const UiVisualTreeAsset> visualTree,
                           std::vector<Ref<const UiStyleSheetAsset>> styleSheets, Ref<RuntimeUiTree> sharedTree,
                           const RuntimeUiElementId parent, UiTemplateResolver templateResolver,
                           const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(std::move(visualTree), std::move(styleSheets), std::move(sharedTree), parent,
                                        std::move(templateResolver), maximumEvents))
    {
    }

    UiDocument::~UiDocument() noexcept = default;

    void UiDocumentBindingSource::Write(const std::string_view path, const std::any&)
    {
        throw std::logic_error("UI binding path '" + std::string(path) + "' is read-only.");
    }

    const Ref<RuntimeUiTree>& UiDocument::Tree() const noexcept { return m_Impl->Tree; }

    RuntimeUiElementId UiDocument::Root() const noexcept { return m_Impl->Root; }

    const Ref<Ui::VisualElement>& UiDocument::VisualRoot() const noexcept { return m_Impl->VisualRoot; }

    Ref<Ui::VisualElement> UiDocument::Visual(const AssetId stableId) const noexcept
    {
        const auto runtime = Find(stableId);
        const auto* element = runtime ? m_Impl->FindElement(*runtime) : nullptr;
        return element ? element->Visual : Ref<Ui::VisualElement>{};
    }

    Ref<Ui::VisualElement> UiDocument::Visual(const std::string_view name) const noexcept
    {
        const auto runtime = Find(name);
        const auto* element = runtime ? m_Impl->FindElement(*runtime) : nullptr;
        return element ? element->Visual : Ref<Ui::VisualElement>{};
    }

    std::optional<RuntimeUiElementId> UiDocument::Find(const AssetId stableId) const noexcept
    {
        const auto found = m_Impl->StableIds.find(stableId);
        return found == m_Impl->StableIds.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<RuntimeUiElementId> UiDocument::Find(const std::string_view name) const noexcept
    {
        const auto found = m_Impl->Names.find(std::string(name));
        return found == m_Impl->Names.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<UiDocumentElementInfo> UiDocument::Describe(const RuntimeUiElementId element) const
    {
        const auto found = m_Impl->RuntimeIndices.find(element.Value());
        if (found == m_Impl->RuntimeIndices.end())
            return std::nullopt;
        const auto& value = m_Impl->Elements[found->second];
        const auto state = m_Impl->Tree->State(value.Runtime);
        if (!state)
            return std::nullopt;
        return UiDocumentElementInfo{.StableId = value.Definition->StableId,
                                     .Name = value.Definition->Name,
                                     .SourceType = value.Definition->Type,
                                     .RuntimeType = state->Type};
    }

    std::vector<UiResolvedStyleSelectorTrace> UiDocument::ResolvedStyleTrace(const RuntimeUiElementId element) const
    {
        const auto found = m_Impl->RuntimeIndices.find(element.Value());
        return found == m_Impl->RuntimeIndices.end() ? std::vector<UiResolvedStyleSelectorTrace>{}
                                                     : m_Impl->Elements[found->second].SelectorTrace;
    }

    std::vector<RuntimeUiElementId> UiDocument::Query(const UiQuery& query) const
    {
        std::vector<RuntimeUiElementId> result;
        for (const auto& element : m_Impl->Elements)
        {
            const auto& definition = *element.Definition;
            if (query.Type && definition.Type != *query.Type)
                continue;
            if (!query.Name.empty() && definition.Name != query.Name)
                continue;
            if (!query.Class.empty() && std::ranges::find(definition.Classes, query.Class) == definition.Classes.end())
                continue;
            result.push_back(element.Runtime);
        }
        return result;
    }

    void UiDocument::SetBindingSource(Ref<UiDocumentBindingSource> source) { m_Impl->Rebind(std::move(source)); }

    const std::vector<UiDocumentBindingDiagnostic>& UiDocument::BindingDiagnostics() const noexcept
    {
        return m_Impl->BindingDiagnostics;
    }

    void UiDocument::UpdateBindings() { m_Impl->UpdateBindings(); }

    bool UiDocument::DispatchRuntimeEvent(const RuntimeUiEvent& event) { return m_Impl->DispatchRuntimeEvent(event); }

    bool UiDocument::SynchronizeInteractionStates() { return m_Impl->SynchronizeInteractionStates(); }

    bool UiDocument::Advance(const float deltaSeconds) { return m_Impl->Advance(deltaSeconds); }

    void UiDocument::SetPseudoState(const RuntimeUiElementId element, const UiStylePseudoState state,
                                    const bool enabled)
    {
        const auto found = m_Impl->RuntimeIndices.find(element.Value());
        if (found == m_Impl->RuntimeIndices.end() || state == UiStylePseudoState::None ||
            state == UiStylePseudoState::Root)
            throw std::invalid_argument("UI pseudo-state target or state is invalid.");
        auto& values = m_Impl->Elements[found->second].PseudoStates;
        const auto current = static_cast<std::uint16_t>(values);
        const auto flag = static_cast<std::uint16_t>(state);
        values = static_cast<UiStylePseudoState>(enabled ? current | flag : current & ~flag);
        m_Impl->RefreshStyles();
    }

    bool UiDocument::SetStyleEvaluationContext(UiStyleEvaluationContext context)
    {
        if (!std::isfinite(context.Width) || context.Width <= 0.0F || !std::isfinite(context.Height) ||
            context.Height <= 0.0F || !std::isfinite(context.Dpi) || context.Dpi <= 0.0F)
            throw std::invalid_argument("UI style evaluation context dimensions and DPI must be finite and positive.");
        if (m_Impl->StyleEvaluationContext == context)
            return false;
        m_Impl->StyleEvaluationContext = context;
        m_Impl->RefreshStyles();
        return true;
    }

    void UiDocument::RefreshStyles() { m_Impl->RefreshStyles(); }
} // namespace Keire
