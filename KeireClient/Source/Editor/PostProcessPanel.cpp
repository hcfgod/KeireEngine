#include "KeireClient/Editor/PostProcessPanel.h"
#include "Keire/Assets/RenderingAssets.h"
#include "KeireClientInternal/Editor/PostProcessAuthoring.h"
#include "KeireClientInternal/Editor/PostProcessCreation.h"
#include "KeireClientInternal/Editor/PostProcessDiagnostics.h"
#include "KeireClientInternal/Editor/PostProcessLooks.h"
#include "KeireClientInternal/Editor/PostProcessSearch.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    PostProcessPanel::PostProcessPanel(IPostProcessPanelController& controller) noexcept : m_Controller(controller) {}

    void PostProcessPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.post-processing", "Post Processing", false});
    }

    void PostProcessPanel::Draw(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.PostProcessState();
        m_ContinuousFieldSeen = false;
        if (!document.ContinuousEditActive())
        {
            m_ContinuousField.clear();
        }
        if (!m_Registration.Visible())
        {
            document.CancelContinuousEdit();
            m_ContinuousField.clear();
            m_CancelledField.clear();
            return;
        }
        ui.SetNextWindowSize({840.0F, 680.0F});
        if (auto panel = ui.BeginPanel(m_Registration); panel)
        {
            if (ui.WindowFocused())
            {
                m_Controller.ActivatePostProcessHistory();
            }
            // Reserve feedback space so save/history actions cannot shift the controls under the pointer.
            ui.Text(m_Message.empty() ? std::string_view(" ") : std::string_view(m_Message));
            ui.Separator();
            try
            {
                if (document.ContinuousEditActive() && ui.KeyPressed(Keire::UiKey::Escape))
                {
                    document.CancelContinuousEdit();
                    m_CancelledField = m_ContinuousField;
                    m_ContinuousField.clear();
                    m_Message = "Cancelled the current adjustment.";
                }
                DrawContents(ui);
                if (!m_ContinuousFieldSeen || !ui.WindowFocused())
                {
                    document.CommitContinuousEdit();
                    m_ContinuousField.clear();
                    m_CancelledField.clear();
                }
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_CancelledField = m_ContinuousField;
                try
                {
                    document.CancelContinuousEdit();
                }
                catch (...)
                {
                    // Keep the original action failure visible if preview restoration also fails.
                }
                m_ContinuousField.clear();
            }
        }
        else
        {
            document.CancelContinuousEdit();
            m_ContinuousField.clear();
            m_CancelledField.clear();
        }
    }

    bool PostProcessPanel::TrackNumericEdit(const std::string_view key, const std::string_view label,
                                            const Keire::UiItemState& state)
    {
        auto& document = m_Controller.PostProcessState();
        if (m_CancelledField == key)
        {
            m_ContinuousFieldSeen = true;
            if (!state.Active)
            {
                m_CancelledField.clear();
            }
            return false;
        }
        if (state.Active || state.Activated)
        {
            m_ContinuousFieldSeen = true;
            if (m_ContinuousField != key || !document.ContinuousEditActive())
            {
                document.BeginContinuousEdit(label);
                m_ContinuousField = key;
            }
        }
        m_ContinuousFieldSeen |= m_ContinuousField == key;
        return true;
    }

    void PostProcessPanel::FinishNumericEdit(const std::string_view key, const Keire::UiItemState& state)
    {
        if (m_ContinuousField == key && !state.Active)
        {
            // Include a final sample reported on deactivation in the same command as the drag.
            m_Controller.PostProcessState().CommitContinuousEdit();
            m_ContinuousField.clear();
        }
    }

    void PostProcessPanel::ApplyNumericEdit(const std::string_view label,
                                            const Keire::PostProcessProfileDefinition& replacement)
    {
        auto& document = m_Controller.PostProcessState();
        if (document.ContinuousEditActive())
        {
            (void)document.UpdateContinuousEdit(replacement);
        }
        else
        {
            (void)document.Edit(label, replacement);
        }
        m_Message.clear();
    }

    void PostProcessPanel::DrawEffect(Keire::UiFrame& ui, const Keire::PostProcessEffect effect)
    {
        const auto index = static_cast<std::size_t>(effect);
        auto& document = m_Controller.PostProcessState();
        const auto state = document.Definition().Effects[index];
        const auto key = std::string(Keire::PostProcessEffectKeys()[index]);
        bool enabled = state.Override && state.Value;
        if (ui.Checkbox("Enabled##" + key, enabled))
        {
            (void)document.Edit(enabled ? "Enable " + key : "Disable " + key,
                                Detail::SetPostProcessEffectEnabled(document.Definition(), effect, enabled));
            m_Message.clear();
        }
        // Keep this row present while editing so automatic activation cannot move the active field.
        ui.TextWrapped(m_AdvancedOverrides ? "Unchecked settings inherit from other volumes."
                                           : "Editing a value enables this effect and its settings.");
        if (m_AdvancedOverrides)
        {
            if (ui.Button("Inherit this effect##" + key))
            {
                (void)document.Edit("Inherit " + key, Detail::InheritPostProcessEffect(document.Definition(), effect));
                m_Message = "This effect now inherits its state and settings from other volumes.";
            }
        }
    }

    void PostProcessPanel::DrawParameters(Keire::UiFrame& ui, const std::size_t first, const std::size_t last,
                                          const std::string_view category)
    {
        auto& document = m_Controller.PostProcessState();
        const auto descriptions = Keire::PostProcessParameterDescriptions();
        for (std::size_t index = first; index <= last; ++index)
        {
            const auto& description = descriptions[index];
            if (!Detail::PostProcessParameterMatches(m_Search, category, description.Label, description.Unit))
            {
                continue;
            }
            auto parameter = document.Definition().Parameters[index];
            const auto key = std::string(description.Key);
            const auto label = std::string(description.Label) +
                               (description.Unit.empty() ? "" : " (" + std::string(description.Unit) + ")");
            ui.Separator();
            bool changed = false;
            Keire::UiItemState scalarState;
            if (m_AdvancedOverrides)
            {
                changed = ui.Checkbox("Override##" + key, parameter.Override);
                if (changed)
                {
                    document.CommitContinuousEdit();
                    m_ContinuousField.clear();
                }
                ui.SameLine();
            }
            ui.TextWrapped(label);
            if (auto disabled = ui.BeginDisabled(m_AdvancedOverrides && !parameter.Override); disabled)
            {
                ui.SetNextItemWidth(std::max(1.0F, ui.ContentAvailable().Width - 80.0F));
                const bool valueChanged = ui.DragScalar("##" + key, parameter.Value, description.Step,
                                                        description.Minimum, description.Maximum);
                scalarState = ui.LastItemState();
                if (TrackNumericEdit(key, "Change " + std::string(description.Label), scalarState))
                {
                    changed |= valueChanged;
                }
            }
            ui.SameLine();
            if (ui.Button("Reset##" + key, {68.0F, 0.0F}))
            {
                document.CommitContinuousEdit();
                m_ContinuousField.clear();
                parameter = {!m_AdvancedOverrides, description.Default};
                changed = true;
            }
            if (changed)
            {
                if (m_AdvancedOverrides)
                {
                    auto replacement = document.Definition();
                    replacement.Parameters[index] = parameter;
                    ApplyNumericEdit("Change " + std::string(description.Label), replacement);
                }
                else
                {
                    ApplyNumericEdit("Change " + std::string(description.Label),
                                     Detail::EditPostProcessParameter(document.Definition(),
                                                                      static_cast<Keire::PostProcessParameter>(index),
                                                                      parameter.Value));
                }
                m_Message.clear();
            }
            FinishNumericEdit(key, scalarState);
        }
    }

    void PostProcessPanel::DrawColorLookup(Keire::UiFrame& ui)
    {
        if (!Detail::PostProcessSearchMatches(m_Search, "Color Lookup LUT Texture Weight"))
        {
            return;
        }
        auto& document = m_Controller.PostProcessState();
        const auto database = m_Controller.PostProcessDatabase();
        auto reference = document.Definition().ColorLookup;
        auto weight = document.Definition().ColorLookupWeight;
        ui.Separator();
        ui.Text("Color Lookup (LUT)");
        bool changed = false;
        if (m_AdvancedOverrides)
        {
            changed = ui.Checkbox("Override Lookup Texture", reference.Override);
        }
        if (auto disabled = ui.BeginDisabled(m_AdvancedOverrides && !reference.Override); disabled)
        {
            AssetPickerOptions options;
            options.Label = "Lookup Texture";
            options.ExpectedType = Keire::Texture2DAsset::StaticType();
            if (const auto assets = m_Controller.PostProcessAssets())
            {
                options.ResolveType = [assets](const Keire::AssetId id) { return assets->TryGetType(id); };
            }
            if (database)
            {
                changed |= m_LookupPicker.Draw(ui, database->Records(), reference.Value, options);
            }
            else
            {
                changed |= m_LookupPicker.Draw(ui, {}, reference.Value, options);
            }
            if (!m_LookupPicker.Diagnostic().empty())
            {
                ui.TextWrapped(m_LookupPicker.Diagnostic());
            }
        }
        if (m_AdvancedOverrides)
        {
            changed |= ui.Checkbox("Override Lookup Weight", weight.Override);
        }
        if (changed)
        {
            // Texture selections and override toggles are distinct actions from the preceding weight gesture.
            document.CommitContinuousEdit();
            m_ContinuousField.clear();
        }
        Keire::UiItemState weightState;
        if (auto disabled = ui.BeginDisabled(m_AdvancedOverrides && !weight.Override); disabled)
        {
            const bool valueChanged = ui.DragScalar("Lookup Weight", weight.Value, 0.01, 0.0, 1.0);
            weightState = ui.LastItemState();
            if (TrackNumericEdit("lookupWeight", "Change lookup weight", weightState))
            {
                changed |= valueChanged;
            }
        }
        if (ui.Button(m_AdvancedOverrides ? "Reset Lookup Overrides" : "Clear Lookup"))
        {
            document.CommitContinuousEdit();
            m_ContinuousField.clear();
            reference = {};
            weight = {false, 1.0};
            changed = true;
        }
        if (changed)
        {
            if (m_AdvancedOverrides)
            {
                auto replacement = document.Definition();
                if (replacement.ColorLookup != reference || replacement.ColorLookupWeight != weight)
                {
                    replacement.SchemaVersion = 3;
                    replacement.ColorLookup = reference;
                    replacement.ColorLookupWeight = weight;
                    ApplyNumericEdit("Edit color lookup", replacement);
                }
            }
            else
            {
                if (reference.Value && reference.Value == document.Asset())
                {
                    throw std::invalid_argument("A profile cannot reference itself as a color lookup texture.");
                }
                const auto replacement = Detail::ApplyPostProcessLookup(document.Definition(), reference, weight);
                ApplyNumericEdit("Edit color lookup", replacement);
            }
            m_Message.clear();
        }
        FinishNumericEdit("lookupWeight", weightState);
        ui.TextWrapped(
            m_AdvancedOverrides
                ? "Texture and weight overrides are independent. Reset Lookup Overrides resumes inheritance; "
                  "enable Color Grading separately to apply the lookup."
                : "Choosing a texture applies it automatically. Clear Lookup explicitly suppresses an "
                  "inherited lookup, even when no local texture is selected.");
        ui.Text("Use a horizontal N*N by N strip, with N = 16, 32 or 64.");
        ui.Text("Import as linear Data, without mipmaps; use linear filtering and clamp addressing.");
        ui.Text("Missing or incompatible lookup textures leave the color unchanged until repaired.");
    }

    void PostProcessPanel::DrawCustomEffects(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.PostProcessState();
        auto stack = document.Definition().CustomEffects;
        const auto database = m_Controller.PostProcessDatabase();
        constexpr std::array stageNames{"Before Tone Mapping", "After Tone Mapping", "After UI"};
        bool changed = false;
        if (m_AdvancedOverrides)
        {
            changed = ui.Checkbox("Override Custom Stack", stack.Override);
        }
        ui.Text("Effects run top to bottom within each stage, after the legacy camera effect.");
        ui.Text("The stack switches at 50% volume influence. An empty overridden stack clears inheritance.");
        if (auto disabled = ui.BeginDisabled(m_AdvancedOverrides && !stack.Override); disabled)
        {
            for (std::size_t index = 0; index < stack.Value.size(); ++index)
            {
                auto& effect = stack.Value[index];
                const auto suffix = "##customEffect" + std::to_string(index);
                auto record = database ? database->Find(effect.Material) : std::nullopt;
                if (!record && database)
                {
                    for (const auto& candidate : database->Records())
                    {
                        if (std::ranges::find(candidate.SubAssets, effect.Material) != candidate.SubAssets.end())
                        {
                            record = candidate;
                            break;
                        }
                    }
                }
                ui.Separator();
                changed |= ui.Checkbox("Enabled" + suffix, effect.Enabled);
                ui.SameLine();
                ui.Text(record ? Keire::Detail::PathToUtf8(record->RelativePath)
                               : "Missing material: " + effect.Material.ToString());
                if (database)
                {
                    const auto label = "Material" + suffix;
                    AssetPickerOptions options;
                    options.Label = label;
                    options.ExpectedType = Keire::MaterialAsset::StaticType();
                    options.AllowNone = false;
                    if (const auto assets = m_Controller.PostProcessAssets())
                    {
                        options.ResolveType = [assets](const Keire::AssetId id) { return assets->TryGetType(id); };
                    }
                    changed |= m_CustomMaterialPicker.Draw(ui, database->Records(), effect.Material, options);
                }
                if (auto stages = ui.BeginCombo("Stage" + suffix, stageNames[static_cast<std::size_t>(effect.Stage)]);
                    stages)
                {
                    for (std::size_t stage = 0; stage < stageNames.size(); ++stage)
                    {
                        if (ui.Selectable(stageNames[stage], static_cast<std::size_t>(effect.Stage) == stage))
                        {
                            effect.Stage = static_cast<Keire::PostProcessCustomStage>(stage);
                            changed = true;
                        }
                    }
                }
                if (ui.Button("Remove" + suffix))
                {
                    stack.Value.erase(stack.Value.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    break;
                }
                ui.SameLine();
                if (auto unavailable = ui.BeginDisabled(index == 0); unavailable)
                {
                    if (ui.Button("Move Up" + suffix) && index > 0)
                    {
                        std::swap(stack.Value[index], stack.Value[index - 1]);
                        changed = true;
                        break;
                    }
                }
                ui.SameLine();
                if (auto unavailable = ui.BeginDisabled(index + 1 == stack.Value.size()); unavailable)
                {
                    if (ui.Button("Move Down" + suffix) && index + 1 < stack.Value.size())
                    {
                        std::swap(stack.Value[index], stack.Value[index + 1]);
                        changed = true;
                        break;
                    }
                }
            }
            if (auto full = ui.BeginDisabled(stack.Value.size() >= Keire::MaximumPostProcessCustomEffects); full)
            {
                if (database)
                {
                    Keire::AssetId selected;
                    AssetPickerOptions options;
                    options.Label = "Add Custom Effect";
                    options.EmptyLabel = "Choose a fullscreen material...";
                    options.ExpectedType = Keire::MaterialAsset::StaticType();
                    options.AllowNone = false;
                    if (const auto assets = m_Controller.PostProcessAssets())
                    {
                        options.ResolveType = [assets](const Keire::AssetId id) { return assets->TryGetType(id); };
                    }
                    if (m_CustomMaterialPicker.Draw(ui, database->Records(), selected, options) && selected)
                    {
                        stack.Value.push_back({selected});
                        changed = true;
                    }
                }
            }
        }
        if (changed)
        {
            if (!m_AdvancedOverrides)
            {
                stack.Override = true;
            }
            (void)document.SetCustomEffects(std::move(stack));
            m_Message.clear();
        }
        if (!m_CustomMaterialPicker.Diagnostic().empty())
        {
            ui.TextWrapped(m_CustomMaterialPicker.Diagnostic());
        }
        ui.Text("Use materials targeting Fullscreen. Missing or incompatible effects are skipped with renderer "
                "diagnostics.");
    }

    void PostProcessPanel::DrawContents(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.PostProcessState();
        const auto database = m_Controller.PostProcessDatabase();
        const auto record = database && document.IsOpen() ? database->Find(document.Asset()) : std::nullopt;
        ui.Text("POST PROCESSING");
        ui.Text(record ? Keire::Detail::PathToUtf8(record->RelativePath) + (document.Dirty() ? " *" : "")
                       : "Choose a profile");
        const auto selectedProfile =
            record ? Keire::Detail::PathToUtf8(record->RelativePath) : std::string("Select a profile...");
        if (auto profiles = ui.BeginCombo("Open Profile", selectedProfile); profiles)
        {
            if (database)
            {
                for (const auto& candidate : database->Records())
                {
                    if (candidate.Type == Keire::PostProcessProfileAsset::StaticType() &&
                        ui.Selectable(Keire::Detail::PathToUtf8(candidate.RelativePath),
                                      candidate.Id == document.Asset()))
                    {
                        document.CommitContinuousEdit();
                        m_ContinuousField.clear();
                        m_Controller.OpenPostProcessProfile(candidate.Id);
                        m_Message.clear();
                    }
                }
            }
        }
        if (ui.Button("New Profile..."))
        {
            document.CommitContinuousEdit();
            m_ContinuousField.clear();
            m_CreationMessage.clear();
            ui.OpenPopup("Create Post Processing Profile");
        }
        if (auto creation = ui.BeginPopupModal("Create Post Processing Profile"); creation)
        {
            ui.TextWrapped("Create in the current Asset Browser folder. The .keirepostprocess extension is added.");
            if (ui.InputText("Profile Name", m_NewName))
            {
                m_CreationMessage.clear();
            }
            const auto nameError = Detail::PostProcessProfileNameError(m_NewName);
            const auto diagnostic = !nameError.empty() ? nameError : std::string_view(m_CreationMessage);
            ui.TextWrapped(diagnostic.empty() ? std::string_view(" ") : diagnostic);
            if (auto invalid = ui.BeginDisabled(!nameError.empty()); invalid)
            {
                if (ui.Button("Create") || (nameError.empty() && ui.KeyPressed(Keire::UiKey::Enter)))
                {
                    try
                    {
                        m_Controller.CreatePostProcessProfile(m_NewName);
                        m_Message = "Profile creation requested. The new profile opens after its import completes.";
                        ui.CloseCurrentPopup();
                    }
                    catch (const std::exception& error)
                    {
                        m_CreationMessage = error.what();
                    }
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel") || ui.KeyPressed(Keire::UiKey::Escape))
            {
                ui.CloseCurrentPopup();
            }
        }
        if (!document.IsOpen())
        {
            ui.Text("Assign a profile to a Post Process Volume to affect cameras inside it.");
            return;
        }
        ui.Separator();
        if (ui.Button("Save") || (ui.WindowFocused() && ui.Shortcut({Keire::UiKey::S, true})))
        {
            m_Controller.SavePostProcessProfile();
            m_Message = "Profile saved.";
        }
        ui.SameLine();
        if (ui.Button("Undo"))
        {
            m_Message = document.Undo() ? "Undid the last profile change." : "No profile changes to undo.";
        }
        ui.SameLine();
        if (ui.Button("Redo"))
        {
            m_Message = document.Redo() ? "Restored the next profile change." : "No profile changes to redo.";
        }
        if (ui.ContentAvailable().Width >= 480.0F)
        {
            ui.SameLine();
        }
        if (ui.Button("Reload"))
        {
            m_Controller.ReloadPostProcessProfile();
            m_Message = "Profile reloaded from disk.";
        }
        ui.SameLine();
        if (ui.Button("Discard Changes"))
        {
            document.Discard();
            m_Message = "Restored the last saved profile.";
        }
        if (auto looks = ui.BeginCombo("Color Look", "Apply an editable look..."); looks)
        {
            for (std::size_t index = 0; index < Detail::PostProcessColorLooks.size(); ++index)
            {
                const auto& look = Detail::PostProcessColorLooks[index];
                if (ui.Selectable(look.Name, false))
                {
                    (void)document.Edit("Apply " + std::string(look.Name) + " color look",
                                        Detail::ApplyPostProcessColorLook(document.Definition(), index));
                    m_Message = "Color look applied. Other effects are preserved. Undo restores the previous look.";
                }
            }
        }
        (void)ui.InputTextWithHint("Search Settings", "Find an effect, setting or unit...", m_Search);
        (void)ui.Checkbox("Advanced overrides", m_AdvancedOverrides);
        if (m_AdvancedOverrides)
        {
            ui.TextWrapped(
                "Choose which settings this profile overrides; unchecked settings inherit from other volumes.");
        }
        else
        {
            ui.TextWrapped(
                "Enable an effect once, then adjust its settings. Editing a value activates it automatically.");
        }
        DrawEffectWorkspace(ui);
        ui.Separator();
        ui.TextWrapped("Lens effects use opaque and cutout depth; transparent surfaces do not supply focus depth.");
        const auto lensDiagnostic = Detail::PostProcessLensDiagnostic(document.Definition());
        if (!lensDiagnostic.empty())
        {
            ui.TextWrapped(lensDiagnostic);
        }
        if (!document.Diagnostic().empty())
        {
            ui.TextWrapped(document.Diagnostic());
        }
    }
} // namespace KeireEditor
