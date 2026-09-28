#include "KeireClient/Editor/UiBuilderPanel.h"

#include <algorithm>
#include <cctype>

namespace KeireEditor
{
    namespace
    {
        bool Flatten(const Keire::UiVisualElementDefinition& element, const std::size_t depth,
                     std::vector<UiBuilderHierarchyRow>& rows, const std::string_view query,
                     const Keire::AssetId parent = {}, const std::size_t childIndex = 0)
        {
            const auto first = rows.size();
            rows.push_back({&element, parent, childIndex, depth});
            bool matches = UiBuilderSearchMatches(element.Name, query) ||
                           UiBuilderSearchMatches(UiBuilderElementTypeName(element.Type), query) ||
                           UiBuilderSearchMatches(element.CustomType, query);
            for (const auto& name : element.Classes)
                matches = UiBuilderSearchMatches(name, query) || matches;
            for (const auto& attribute : element.Attributes)
                if (attribute.Name == "text")
                    matches = UiBuilderSearchMatches(attribute.Value, query) || matches;
            for (std::size_t index = 0; index < element.Children.size(); ++index)
                matches = Flatten(element.Children[index], depth + 1, rows, query, element.StableId, index) || matches;
            if (!matches)
                rows.resize(first);
            return matches;
        }

    } // namespace

    bool UiBuilderSearchMatches(const std::string_view value, const std::string_view query) noexcept
    {
        return query.empty() || std::search(value.begin(), value.end(), query.begin(), query.end(),
                                            [](const unsigned char left, const unsigned char right)
                                            { return std::tolower(left) == std::tolower(right); }) != value.end();
    }

    std::vector<UiBuilderHierarchyRow> BuildUiBuilderHierarchyRows(const Keire::UiVisualElementDefinition& root,
                                                                   const std::string_view query)
    {
        std::vector<UiBuilderHierarchyRow> rows;
        (void)Flatten(root, 0, rows, query);
        return rows;
    }

    std::string_view UiBuilderElementTypeName(const Keire::UiVisualElementType type) noexcept
    {
        switch (type)
        {
        case Keire::UiVisualElementType::VisualElement:
            return "Visual Element";
        case Keire::UiVisualElementType::TemplateContainer:
            return "Template Container";
        case Keire::UiVisualElementType::Label:
            return "Label";
        case Keire::UiVisualElementType::Image:
            return "Image";
        case Keire::UiVisualElementType::Button:
            return "Button";
        case Keire::UiVisualElementType::TextField:
            return "Text Field";
        case Keire::UiVisualElementType::Toggle:
            return "Toggle";
        case Keire::UiVisualElementType::Slider:
            return "Slider";
        case Keire::UiVisualElementType::ProgressBar:
            return "Progress Bar";
        case Keire::UiVisualElementType::ScrollView:
            return "Scroll View";
        case Keire::UiVisualElementType::ListView:
            return "List View";
        case Keire::UiVisualElementType::TreeView:
            return "Tree View";
        case Keire::UiVisualElementType::DropdownField:
            return "Dropdown";
        case Keire::UiVisualElementType::Foldout:
            return "Foldout";
        case Keire::UiVisualElementType::TabView:
            return "Tab View";
        case Keire::UiVisualElementType::Toolbar:
            return "Toolbar";
        case Keire::UiVisualElementType::Spacer:
            return "Spacer";
        case Keire::UiVisualElementType::Custom:
            return "Custom Element";
        case Keire::UiVisualElementType::Slot:
            return "Slot";
        }
        return "Unknown";
    }

} // namespace KeireEditor
