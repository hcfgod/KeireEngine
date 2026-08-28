#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace
{
    struct AnchorPreset final
    {
        std::string_view Name;
        Keire::Vector2 Minimum;
        Keire::Vector2 Maximum;
    };

    struct AnchorPresetSelection final
    {
        AnchorPreset Preset;
        bool FitStretch = false;
    };

    constexpr std::array AnchorPresets{
        AnchorPreset{"Top Left", {0.0F, 0.0F}, {0.0F, 0.0F}},
        AnchorPreset{"Top Center", {0.5F, 0.0F}, {0.5F, 0.0F}},
        AnchorPreset{"Top Right", {1.0F, 0.0F}, {1.0F, 0.0F}},
        AnchorPreset{"Top Stretch", {0.0F, 0.0F}, {1.0F, 0.0F}},
        AnchorPreset{"Middle Left", {0.0F, 0.5F}, {0.0F, 0.5F}},
        AnchorPreset{"Middle Center", {0.5F, 0.5F}, {0.5F, 0.5F}},
        AnchorPreset{"Middle Right", {1.0F, 0.5F}, {1.0F, 0.5F}},
        AnchorPreset{"Middle Stretch", {0.0F, 0.5F}, {1.0F, 0.5F}},
        AnchorPreset{"Bottom Left", {0.0F, 1.0F}, {0.0F, 1.0F}},
        AnchorPreset{"Bottom Center", {0.5F, 1.0F}, {0.5F, 1.0F}},
        AnchorPreset{"Bottom Right", {1.0F, 1.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Bottom Stretch", {0.0F, 1.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Stretch Left", {0.0F, 0.0F}, {0.0F, 1.0F}},
        AnchorPreset{"Stretch Center", {0.5F, 0.0F}, {0.5F, 1.0F}},
        AnchorPreset{"Stretch Right", {1.0F, 0.0F}, {1.0F, 1.0F}},
        AnchorPreset{"Stretch Both", {0.0F, 0.0F}, {1.0F, 1.0F}},
    };

    [[nodiscard]] bool ApproximatelyEqual(const Keire::Vector2 left, const Keire::Vector2 right) noexcept
    {
        constexpr float Epsilon = 0.0001F;
        return std::abs(left.X - right.X) <= Epsilon && std::abs(left.Y - right.Y) <= Epsilon;
    }

    [[nodiscard]] std::string_view AnchorPresetName(const Keire::Vector2 minimum, const Keire::Vector2 maximum) noexcept
    {
        const auto found = std::ranges::find_if(
            AnchorPresets, [minimum, maximum](const AnchorPreset& preset)
            { return ApproximatelyEqual(preset.Minimum, minimum) && ApproximatelyEqual(preset.Maximum, maximum); });
        return found == AnchorPresets.end() ? std::string_view("Custom") : found->Name;
    }

    void DrawAnchorPresetGlyph(Keire::UiFrame& ui, const Keire::UiItemRect button, const AnchorPreset& preset,
                               const bool selected, const Keire::UiThemeDefinition& theme)
    {
        const Keire::UiItemRect screen{{button.Minimum.X + 8.0F, button.Minimum.Y + 7.0F},
                                       {button.Maximum.X - 8.0F, button.Maximum.Y - 7.0F}};
        ui.DrawRectangle(screen, selected ? theme.Accent : theme.MutedText, selected ? 2.0F : 1.0F, 2.0F);

        const auto point = [&screen](const Keire::Vector2 anchor)
        {
            return Keire::UiPosition{screen.Minimum.X + 3.0F + anchor.X * (screen.Maximum.X - screen.Minimum.X - 6.0F),
                                     screen.Minimum.Y + 3.0F + anchor.Y * (screen.Maximum.Y - screen.Minimum.Y - 6.0F)};
        };
        const auto minimum = point(preset.Minimum);
        const auto maximum = point(preset.Maximum);
        const auto color = selected ? theme.Accent : theme.Text;
        if (ApproximatelyEqual(preset.Minimum, preset.Maximum))
        {
            ui.DrawFilledCircle(minimum, selected ? 3.5F : 3.0F, color);
            return;
        }
        if (std::abs(minimum.X - maximum.X) <= 0.0001F || std::abs(minimum.Y - maximum.Y) <= 0.0001F)
        {
            ui.DrawLine(minimum, maximum, color, selected ? 3.0F : 2.0F);
            ui.DrawFilledCircle(minimum, 2.5F, color);
            ui.DrawFilledCircle(maximum, 2.5F, color);
            return;
        }
        ui.DrawRectangle({minimum, maximum}, color, selected ? 3.0F : 2.0F, 1.0F);
    }

    [[nodiscard]] std::optional<AnchorPresetSelection> DrawAnchorPresetPicker(Keire::UiFrame& ui,
                                                                              const Keire::Vector2 minimum,
                                                                              const Keire::Vector2 maximum,
                                                                              const Keire::UiThemeDefinition& theme)
    {
        std::optional<AnchorPresetSelection> result;
        ui.TextColored(theme.MutedText, "Anchor Preset");
        if (auto combo = ui.BeginCombo("##RectTransformAnchorPreset", AnchorPresetName(minimum, maximum)); combo)
        {
            ui.TextColored(theme.MutedText, "Choose the parent-screen region this element follows.");
            ui.TextColored(theme.MutedText, "Ctrl-click a stretch preset to fit the stretched axes.");
            for (std::size_t index = 0; index < AnchorPresets.size(); ++index)
            {
                const auto& preset = AnchorPresets[index];
                const bool selected =
                    ApproximatelyEqual(minimum, preset.Minimum) && ApproximatelyEqual(maximum, preset.Maximum);
                if (ui.Button("##AnchorPreset" + std::to_string(index), {46.0F, 42.0F}))
                    result = AnchorPresetSelection{preset, ui.ControlDown()};
                const auto state = ui.LastItemState();
                DrawAnchorPresetGlyph(ui, ui.LastItemRect(), preset, selected, theme);
                if (state.Hovered)
                    ui.SetTooltip(preset.Name);
                if (index % 4 != 3)
                    ui.SameLine();
            }
        }
        return result;
    }
} // namespace

namespace KeireEditor
{
    bool InspectorPanel::DrawComponentMenu(Keire::UiFrame& ui, const Keire::Entity& entity,
                                           const Keire::Ref<Keire::Component>& component,
                                           const Keire::ComponentRegistration& registration,
                                           SceneDocument& sceneDocument, const Keire::Ref<Keire::Scene>& scene)
    {
        bool removed = false;
        try
        {
            if (ui.MenuItem("Copy Component"))
                m_ComponentClipboard = ComponentClipboard{registration.Type, registration.Serialize(*component), true};
            const auto copiedRegistration = m_ComponentClipboard && m_ComponentClipboard->WholeComponent
                                                ? scene->Components()->Find(m_ComponentClipboard->Type)
                                                : std::nullopt;
            const bool canPasteComponent =
                copiedRegistration && copiedRegistration->Removable &&
                (copiedRegistration->AllowMultiple || !entity.HasComponent(copiedRegistration->Type));
            if (ui.MenuItem("Paste Component", false, canPasteComponent))
            {
                m_Controller.RecordInspectorUndo("Paste " + copiedRegistration->Name);
                const auto pasted = sceneDocument.AddComponent(entity.Id(), copiedRegistration->Type);
                try
                {
                    sceneDocument.SetComponentValues(entity.Id(), pasted, m_ComponentClipboard->Values);
                }
                catch (...)
                {
                    sceneDocument.RemoveComponent(entity.Id(), pasted);
                    throw;
                }
            }
            ui.Separator();
            if (ui.MenuItem("Copy Values"))
                m_ComponentClipboard = ComponentClipboard{registration.Type, registration.Serialize(*component), false};
            const bool canPasteValues = m_ComponentClipboard && m_ComponentClipboard->Type == registration.Type;
            if (ui.MenuItem("Paste Values", false, canPasteValues))
            {
                m_Controller.RecordInspectorUndo("Paste " + registration.Name + " Values");
                sceneDocument.SetComponentValues(entity.Id(), component, m_ComponentClipboard->Values);
            }
            ui.Separator();
            if (ui.MenuItem("Remove", false, registration.Removable))
            {
                m_Controller.RecordInspectorUndo("Remove " + registration.Name);
                sceneDocument.RemoveComponent(entity.Id(), component);
                removed = true;
            }
        }
        catch (const std::exception& error)
        {
            m_Controller.ReportInspectorAssetError(std::string("Component operation failed: ") + error.what());
        }
        return removed;
    }

    void InspectorPanel::DrawRectTransformAnchorPreset(Keire::UiFrame& ui, const Keire::Entity& entity,
                                                       const Keire::ComponentRegistration& registration,
                                                       SceneDocument& sceneDocument,
                                                       const Keire::UiThemeDefinition& theme)
    {
        const auto rect = entity.GetComponent<Keire::RectTransformComponent>();
        if (!rect)
            return;
        const auto preset = DrawAnchorPresetPicker(ui, rect->AnchorMinimum(), rect->AnchorMaximum(), theme);
        if (!preset)
            return;

        auto anchoredPosition = rect->AnchoredPosition();
        auto sizeDelta = rect->SizeDelta();
        const bool stretchHorizontal = std::abs(preset->Preset.Minimum.X - preset->Preset.Maximum.X) > 0.0001F;
        const bool stretchVertical = std::abs(preset->Preset.Minimum.Y - preset->Preset.Maximum.Y) > 0.0001F;
        m_Controller.RecordInspectorUndo("Change Anchor Preset",
                                         "rect-transform.anchor-preset." + entity.Id().ToString());
        sceneDocument.SetComponentProperty(entity.Id(), registration.Type, "anchorMinimum", preset->Preset.Minimum);
        sceneDocument.SetComponentProperty(entity.Id(), registration.Type, "anchorMaximum", preset->Preset.Maximum);
        if (!preset->FitStretch || (!stretchHorizontal && !stretchVertical))
            return;
        if (stretchHorizontal)
        {
            anchoredPosition.X = 0.0F;
            sizeDelta.X = 0.0F;
        }
        if (stretchVertical)
        {
            anchoredPosition.Y = 0.0F;
            sizeDelta.Y = 0.0F;
        }
        sceneDocument.SetComponentProperty(entity.Id(), registration.Type, "anchoredPosition", anchoredPosition);
        sceneDocument.SetComponentProperty(entity.Id(), registration.Type, "sizeDelta", sizeDelta);
    }
} // namespace KeireEditor
