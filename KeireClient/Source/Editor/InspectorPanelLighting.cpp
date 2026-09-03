#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/InspectorPropertyEditor.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    void InspectorPanel::DrawDirectionalLightAdvancedProperties(
        Keire::UiFrame& ui, const Keire::Ref<Keire::DirectionalLightComponent>& light, SceneDocument& sceneDocument,
        const std::span<const Keire::AssetId> editTargets, const bool multiEditing, const std::size_t componentOrdinal)
    {
        const auto setProperty = [&](const std::string_view property, Keire::ComponentPropertyValue value)
        {
            if (multiEditing)
                sceneDocument.SetComponentsProperty(editTargets, light->Type(), property, value, componentOrdinal);
            else
                sceneDocument.SetComponentProperty(Keire::EntityId(editTargets.front()), light->Type(), property,
                                                   std::move(value));
        };

        auto contactShadows = light->ContactShadows();
        if (ui.Checkbox("Contact Shadows", contactShadows))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("contactShadows", contactShadows);
        }

        const auto shadowResolution = light->ShadowResolution();
        constexpr std::array shadowResolutions{Keire::ShadowResolutionHint::Low, Keire::ShadowResolutionHint::Medium,
                                               Keire::ShadowResolutionHint::High,
                                               Keire::ShadowResolutionHint::VeryHigh};
        constexpr std::array<std::string_view, 4> shadowResolutionLabels{"Low", "Medium", "High", "Very High"};
        const auto selectedResolution = static_cast<std::size_t>(shadowResolution);
        if (auto resolution = ui.BeginCombo("Shadow Resolution", selectedResolution < shadowResolutionLabels.size()
                                                                     ? shadowResolutionLabels[selectedResolution]
                                                                     : "Invalid");
            resolution)
        {
            for (std::size_t index = 0; index < shadowResolutions.size(); ++index)
            {
                if (ui.Selectable(shadowResolutionLabels[index], shadowResolution == shadowResolutions[index]))
                {
                    m_Controller.RecordInspectorUndo();
                    setProperty("shadowResolution", static_cast<std::int64_t>(shadowResolutions[index]));
                }
            }
        }

        ui.Separator();
        const auto bakeMode = light->BakeMode();
        constexpr std::array bakeModes{Keire::LightBakeMode::Realtime, Keire::LightBakeMode::Mixed,
                                       Keire::LightBakeMode::Baked};
        constexpr std::array<std::string_view, 3> bakeModeLabels{"Realtime", "Mixed", "Baked"};
        const auto selectedBakeMode = static_cast<std::size_t>(bakeMode);
        if (auto baking = ui.BeginCombo(
                "Bake Mode", selectedBakeMode < bakeModeLabels.size() ? bakeModeLabels[selectedBakeMode] : "Invalid");
            baking)
        {
            for (std::size_t index = 0; index < bakeModes.size(); ++index)
            {
                if (ui.Selectable(bakeModeLabels[index], bakeMode == bakeModes[index]))
                {
                    m_Controller.RecordInspectorUndo();
                    setProperty("bakeMode", static_cast<std::int64_t>(bakeModes[index]));
                }
            }
        }

        const auto& theme = m_Controller.InspectorTheme();
        if (bakeMode == Keire::LightBakeMode::Baked)
            ui.TextColored(theme.MutedText,
                           "Bake-only: no realtime direct light or moving shadows. Re-bake Lighting after edits.");
        else if (bakeMode == Keire::LightBakeMode::Mixed)
            ui.TextColored(theme.MutedText, "Baked indirect lighting with realtime direct light and shadows.");
        else
            ui.TextColored(theme.MutedText, "Realtime direct light and dynamic shadows.");
        ui.TextColored(theme.MutedText, "Shadow Resolution scales the project base: 0.25x / 0.5x / 1x / 2x.");

        auto indirectMultiplier = light->IndirectMultiplier();
        if (ui.SliderFloat("Indirect Multiplier", indirectMultiplier, 0.0F, 10.0F))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("indirectMultiplier", static_cast<double>(indirectMultiplier));
        }

        ui.Separator();
        const auto records = m_Controller.InspectorAssetRecords();
        const auto assets = m_Controller.InspectorAssetSystem();
        const auto managedAssetTypes = m_Controller.InspectorManagedAssetTypes();
        const auto resolveManagedType = [this](const Keire::AssetId asset)
        { return m_Controller.InspectorManagedDataType(asset); };
        InspectorPropertyEditor propertyEditor(ui, records, assets, sceneDocument.ActiveScene(), *m_AssetPicker,
                                               managedAssetTypes, resolveManagedType);
        auto cookie = light->Cookie();
        if (propertyEditor.EditAsset("Cookie", cookie, Keire::Texture2DAsset::StaticType()))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("cookie", cookie);
        }
        auto cookieScale = light->CookieScale();
        if (ui.DragVector2("Cookie Scale", cookieScale, 0.01F))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("cookieScale", cookieScale);
        }
        auto cookieOffset = light->CookieOffset();
        if (ui.DragVector2("Cookie Offset", cookieOffset, 0.01F))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("cookieOffset", cookieOffset);
        }
        auto cookieRotation = light->CookieRotationDegrees();
        if (ui.SliderFloat("Cookie Rotation", cookieRotation, -180.0F, 180.0F))
        {
            m_Controller.RecordInspectorUndo();
            setProperty("cookieRotation", static_cast<double>(cookieRotation));
        }
    }
} // namespace KeireEditor
