#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"

#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] Entity Find(const Ref<Scene>& scene, const AssetId entity)
        {
            return scene ? scene->FindEntity(EntityId(entity)) : Entity{};
        }
    } // namespace

    std::optional<float> ReadManagedUiScalar(const Ref<Scene>& scene, const AssetId entity,
                                             const ManagedUiScalarProperty property) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto slider = target ? target.GetComponent<UiSliderComponent>() : Ref<UiSliderComponent>{};
            if (!slider)
                return std::nullopt;
            switch (property)
            {
            case ManagedUiScalarProperty::Minimum:
                return slider->Minimum();
            case ManagedUiScalarProperty::Maximum:
                return slider->Maximum();
            case ManagedUiScalarProperty::Value:
                return slider->Value();
            }
        }
        catch (...)
        {
        }
        return std::nullopt;
    }

    bool SetManagedUiScalar(const Ref<Scene>& scene, const AssetId entity, const ManagedUiScalarProperty property,
                            const float value) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto slider = target ? target.GetComponent<UiSliderComponent>() : Ref<UiSliderComponent>{};
            if (!slider)
                return false;
            switch (property)
            {
            case ManagedUiScalarProperty::Minimum:
                slider->SetRange(value, slider->Maximum());
                break;
            case ManagedUiScalarProperty::Maximum:
                slider->SetRange(slider->Minimum(), value);
                break;
            case ManagedUiScalarProperty::Value:
                slider->SetValue(value);
                break;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<bool> ReadManagedUiFlag(const Ref<Scene>& scene, const Ref<ScenePresentationRuntime>& presentation,
                                          const AssetId entity, const ManagedUiFlagProperty property) noexcept
    {
        try
        {
            const auto id = EntityId(entity);
            const auto target = Find(scene, entity);
            if (!target)
                return std::nullopt;
            if (property == ManagedUiFlagProperty::Focused)
                return presentation && presentation->FocusedUiEntity() == id;
            if (const auto slider = target.GetComponent<UiSliderComponent>())
                return property == ManagedUiFlagProperty::Interactable ? std::optional<bool>(slider->Interactable())
                                                                       : std::nullopt;
            if (const auto toggle = target.GetComponent<UiToggleComponent>())
                return property == ManagedUiFlagProperty::Checked ? std::optional<bool>(toggle->IsOn())
                                                                  : std::optional<bool>(toggle->Interactable());
            if (const auto input = target.GetComponent<UiInputFieldComponent>())
                return property == ManagedUiFlagProperty::Interactable ? std::optional<bool>(input->Interactable())
                                                                       : std::nullopt;
            if (const auto scroll = target.GetComponent<UiScrollViewComponent>())
                return property == ManagedUiFlagProperty::Interactable ? std::optional<bool>(scroll->Interactable())
                                                                       : std::nullopt;
        }
        catch (...)
        {
        }
        return std::nullopt;
    }

    bool SetManagedUiFlag(const Ref<Scene>& scene, const Ref<ScenePresentationRuntime>& presentation,
                          const AssetId entity, const ManagedUiFlagProperty property, const bool value) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            if (!target)
                return false;
            if (property == ManagedUiFlagProperty::Focused)
                return value && presentation && presentation->SetFocus(target.Id());
            if (const auto slider = target.GetComponent<UiSliderComponent>())
            {
                if (property != ManagedUiFlagProperty::Interactable)
                    return false;
                slider->SetInteractable(value);
                return true;
            }
            if (const auto toggle = target.GetComponent<UiToggleComponent>())
            {
                if (property == ManagedUiFlagProperty::Checked)
                    toggle->SetIsOn(value);
                else if (property == ManagedUiFlagProperty::Interactable)
                    toggle->SetInteractable(value);
                else
                    return false;
                return true;
            }
            if (const auto input = target.GetComponent<UiInputFieldComponent>())
            {
                if (property != ManagedUiFlagProperty::Interactable)
                    return false;
                input->SetInteractable(value);
                return true;
            }
            if (const auto scroll = target.GetComponent<UiScrollViewComponent>())
            {
                if (property != ManagedUiFlagProperty::Interactable)
                    return false;
                scroll->SetInteractable(value);
                return true;
            }
        }
        catch (...)
        {
        }
        return false;
    }

    std::optional<Vector2> ReadManagedUiVector(const Ref<Scene>& scene, const AssetId entity,
                                               const ManagedUiVectorProperty property) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto scroll = target ? target.GetComponent<UiScrollViewComponent>() : Ref<UiScrollViewComponent>{};
            if (!scroll)
                return std::nullopt;
            return property == ManagedUiVectorProperty::ScrollOffset ? scroll->Offset() : scroll->ContentSize();
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedUiVector(const Ref<Scene>& scene, const AssetId entity, const ManagedUiVectorProperty property,
                            const Vector2 value) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto scroll = target ? target.GetComponent<UiScrollViewComponent>() : Ref<UiScrollViewComponent>{};
            if (!scroll)
                return false;
            if (property == ManagedUiVectorProperty::ScrollOffset)
                scroll->SetOffset(value);
            else
                scroll->SetContentSize(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<std::string> ReadManagedUiInputText(const Ref<Scene>& scene, const AssetId entity) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto input = target ? target.GetComponent<UiInputFieldComponent>() : Ref<UiInputFieldComponent>{};
            return input ? std::optional<std::string>(input->Text()) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool SetManagedUiInputText(const Ref<Scene>& scene, const AssetId entity, const std::string_view text) noexcept
    {
        try
        {
            const auto target = Find(scene, entity);
            const auto input = target ? target.GetComponent<UiInputFieldComponent>() : Ref<UiInputFieldComponent>{};
            if (!input)
                return false;
            input->SetText(std::string(text));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ConsumeManagedUiEvent(const Ref<ScenePresentationRuntime>& presentation, const AssetId entity,
                               const RuntimeUiEventType type) noexcept
    {
        try
        {
            return presentation && presentation->ConsumeUiEvent(EntityId(entity), type);
        }
        catch (...)
        {
            return false;
        }
    }

    bool FocusManagedUi(const Ref<ScenePresentationRuntime>& presentation, const AssetId entity) noexcept
    {
        try
        {
            return presentation && presentation->SetFocus(EntityId(entity));
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace Keire::Detail
