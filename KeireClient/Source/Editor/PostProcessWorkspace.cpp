#include "KeireClient/Editor/PostProcessPanel.h"

#include "KeireClientInternal/Editor/PostProcessAuthoring.h"

#include <algorithm>
#include <array>
#include <string>

namespace KeireEditor
{
    void PostProcessPanel::DrawEffectWorkspace(Keire::UiFrame& ui)
    {
        const auto& groups = Detail::PostProcessAuthoringGroups;
        const auto& profile = m_Controller.PostProcessState().Definition();
        constexpr std::size_t customGroup = Detail::PostProcessAuthoringGroups.size();
        std::array<bool, customGroup + 1> matches{};
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            matches[index] = Detail::PostProcessGroupMatches(m_Search, groups[index]);
        }
        matches[customGroup] = Detail::PostProcessSearchMatches(m_Search, "Custom Effects Stack Material Stage Order");
        const auto firstMatch = std::find(matches.begin(), matches.end(), true);
        if (firstMatch == matches.end())
        {
            ui.Spacing();
            ui.TextWrapped("No matching settings. Search by effect, parameter name or unit.");
            return;
        }
        if (m_SelectedGroup >= matches.size() || !matches[m_SelectedGroup])
        {
            m_SelectedGroup = static_cast<std::size_t>(firstMatch - matches.begin());
        }
        const auto name = [&](const std::size_t index)
        { return index == customGroup ? std::string_view("Custom Effects") : groups[index].Name; };
        const auto state = [&](const std::size_t index)
        {
            if (index == customGroup)
            {
                return profile.CustomEffects.Override ? std::string_view("Authored") : std::string_view("Inherited");
            }
            return Detail::PostProcessGroupState(profile, groups[index]);
        };
        const auto drawInspector = [&]
        {
            {
                auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.Text(name(m_SelectedGroup));
            }
            if (m_SelectedGroup == customGroup)
            {
                ui.TextWrapped("Compose your own fullscreen effects in an ordered material stack.");
                ui.Separator();
                DrawCustomEffects(ui);
                return;
            }
            const auto& group = groups[m_SelectedGroup];
            ui.TextWrapped(group.Description);
            ui.Separator();
            if (group.Effect != Keire::PostProcessEffect::Count)
            {
                DrawEffect(ui, group.Effect);
            }
            DrawParameters(ui, static_cast<std::size_t>(group.First), static_cast<std::size_t>(group.Last), group.Name);
            if (group.Effect == Keire::PostProcessEffect::ColorGrading)
            {
                DrawColorLookup(ui);
            }
        };
        const auto available = ui.ContentAvailable();
        const float height = std::max(160.0F, available.Height - 82.0F);
        constexpr float sidebarBreakpoint = 680.0F;
        if (available.Width < sidebarBreakpoint)
        {
            if (auto selector = ui.BeginCombo("Effect", name(m_SelectedGroup)); selector)
            {
                for (std::size_t index = 0; index < matches.size(); ++index)
                {
                    if (matches[index] && ui.Selectable(name(index), m_SelectedGroup == index))
                    {
                        m_SelectedGroup = index;
                    }
                }
            }
            if (auto inspector = ui.BeginChild("PostProcessEffectInspectorCompact", {0.0F, height}, true); inspector)
            {
                drawInspector();
            }
            return;
        }
        Keire::UiTableOptions layout;
        layout.Borders = false;
        layout.Resizable = false;
        layout.RowBackground = false;
        layout.PersistSettings = false;
        if (auto workspace = ui.BeginTable("PostProcessWorkspace", 2, layout); workspace)
        {
            ui.TableSetupColumn("Effects", Keire::UiTableColumnSizing::Fixed, 190.0F);
            ui.TableSetupColumn("Inspector", Keire::UiTableColumnSizing::Stretch);
            (void)ui.TableNextColumn();
            if (auto navigation = ui.BeginChild("PostProcessEffectNavigation", {0.0F, height}, true); navigation)
            {
                ui.Text("EFFECTS");
                ui.Separator();
                for (std::size_t index = 0; index < matches.size(); ++index)
                {
                    if (!matches[index])
                    {
                        continue;
                    }
                    if (ui.Selectable(name(index), m_SelectedGroup == index))
                    {
                        m_SelectedGroup = index;
                    }
                    if (ui.LastItemState().Hovered)
                    {
                        ui.SetTooltip(state(index) == "Inherited"
                                          ? std::string(name(index)) +
                                                " uses the base settings. Edit a value to override it in this profile."
                                          : std::string(name(index)) + " - " + std::string(state(index)));
                    }
                    const auto row = ui.LastItemRect();
                    const auto status = state(index);
                    const std::string_view badge = status == "Inherited"  ? "Base"
                                                   : status == "Enabled"  ? "On"
                                                   : status == "Disabled" ? "Off"
                                                                          : "Edit";
                    const auto textSize = ui.MeasureText(badge);
                    const float badgeWidth = textSize.Width + 12.0F;
                    const Keire::UiItemRect badgeRect{{row.Maximum.X - badgeWidth - 4.0F, row.Minimum.Y},
                                                      {row.Maximum.X - 4.0F, row.Maximum.Y}};
                    ui.DrawFilledRectangle(badgeRect,
                                           status == "Enabled" ? Keire::UiColor{0.12F, 0.30F, 0.24F, 1.0F}
                                                               : Keire::UiColor{0.20F, 0.23F, 0.28F, 1.0F},
                                           4.0F);
                    ui.DrawOverlayText({badgeRect.Minimum.X + 6.0F, badgeRect.Minimum.Y}, {0.80F, 0.85F, 0.92F, 1.0F},
                                       badge, 0.0F, row);
                    ui.Spacing();
                }
            }
            (void)ui.TableNextColumn();
            if (auto inspector = ui.BeginChild("PostProcessEffectInspector", {0.0F, height}, true); inspector)
            {
                drawInspector();
            }
        }
    }
} // namespace KeireEditor
