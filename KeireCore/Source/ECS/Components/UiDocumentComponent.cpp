#include "Keire/ECS/Components/UiDocumentComponent.h"

#include "Keire/Ui/UiToolkit.h"

#include <limits>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T Read(const ComponentPropertyBag& values, const std::string_view key, T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            const auto* value = std::get_if<T>(&found->second);
            if (!value)
                throw std::invalid_argument("UI Document component property type is invalid.");
            return *value;
        }
    } // namespace

    UiDocumentComponent::UiDocumentComponent() : Component(StaticType()) {}

    void UiDocumentComponent::SetVisualTree(const AssetId value)
    {
        m_VisualTree = value;
        NotifyChanged();
    }

    void UiDocumentComponent::SetPanelSettings(const AssetId value)
    {
        m_PanelSettings = value;
        NotifyChanged();
    }

    void UiDocumentComponent::SetSortingOrder(const std::int32_t value)
    {
        m_SortingOrder = value;
        NotifyChanged();
    }

    void UiDocumentComponent::SetReceivesInput(const bool value)
    {
        m_ReceivesInput = value;
        NotifyChanged();
    }

    ComponentRegistration CreateUiDocumentComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = UiDocumentComponent::StaticType();
        result.Name = "UI Document";
        result.Category = "UI Toolkit";
        result.SchemaVersion = 1;
        result.Properties = {
            {"visualTree", "Visual Tree", "Document", ComponentPropertyKind::Asset, false, std::nullopt, std::nullopt,
             0.1, UiVisualTreeAsset::StaticType()},
            {"panelSettings", "Panel Settings", "Document", ComponentPropertyKind::Asset, false, std::nullopt,
             std::nullopt, 0.1, UiPanelSettingsAsset::StaticType()},
            {"sortingOrder", "Sorting Order", "Presentation", ComponentPropertyKind::Integer},
            {"receivesInput", "Receives Input", "Input", ComponentPropertyKind::Boolean},
        };
        result.Properties[0].Tooltip = "Visual tree instantiated by this scene document.";
        result.Properties[1].Tooltip =
            "Panel target, scaling, render-texture, camera, and world-surface presentation settings.";
        result.Properties[2].Tooltip = "Added to the shared Panel Settings sorting order for deterministic stacking.";
        result.Factory = [] { return Ref<Component>(CreateRef<UiDocumentComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& document = dynamic_cast<const UiDocumentComponent&>(component);
            return ComponentPropertyBag{{"visualTree", document.m_VisualTree},
                                        {"panelSettings", document.m_PanelSettings},
                                        {"sortingOrder", static_cast<std::int64_t>(document.m_SortingOrder)},
                                        {"receivesInput", document.m_ReceivesInput}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported UI Document component schema version.");
            auto& document = dynamic_cast<UiDocumentComponent&>(component);
            document.SetVisualTree(Read(values, "visualTree", AssetId{}));
            document.SetPanelSettings(Read(values, "panelSettings", AssetId{}));
            const auto sortingOrder = Read(values, "sortingOrder", std::int64_t{0});
            if (sortingOrder < std::numeric_limits<std::int32_t>::min() ||
                sortingOrder > std::numeric_limits<std::int32_t>::max())
            {
                throw std::invalid_argument("UI Document sorting order is outside the supported range.");
            }
            document.SetSortingOrder(static_cast<std::int32_t>(sortingOrder));
            document.SetReceivesInput(Read(values, "receivesInput", true));
        };
        return result;
    }
} // namespace Keire
