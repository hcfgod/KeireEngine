#include "KeireClient/Editor/AudioMixerPanel.h"

#include "KeireClient/Editor/AudioMixerDocument.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        struct EffectTypeEntry
        {
            Keire::AudioGraphNodeType Type;
            std::string_view Name;
        };

        constexpr std::array EffectTypes{
            EffectTypeEntry{Keire::AudioGraphNodeType::Gain, "Gain"},
            EffectTypeEntry{Keire::AudioGraphNodeType::LowPass, "Low Pass"},
            EffectTypeEntry{Keire::AudioGraphNodeType::HighPass, "High Pass"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Equalizer, "Equalizer"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Compressor, "Compressor"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Limiter, "Limiter"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Gate, "Gate"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Delay, "Delay"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Chorus, "Chorus"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Distortion, "Distortion"},
            EffectTypeEntry{Keire::AudioGraphNodeType::AlgorithmicReverb, "Algorithmic Reverb"},
            EffectTypeEntry{Keire::AudioGraphNodeType::ConvolutionReverb, "Convolution Reverb"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Meter, "Meter"},
            EffectTypeEntry{Keire::AudioGraphNodeType::Capture, "Capture"},
        };

        constexpr std::array SnapshotTypes{
            std::pair{Keire::AudioMixerSnapshotParameterType::BusGain, std::string_view("Bus Gain")},
            std::pair{Keire::AudioMixerSnapshotParameterType::BusMute, std::string_view("Bus Mute")},
            std::pair{Keire::AudioMixerSnapshotParameterType::BusSolo, std::string_view("Bus Solo")},
            std::pair{Keire::AudioMixerSnapshotParameterType::SendGain, std::string_view("Send Gain")},
            std::pair{Keire::AudioMixerSnapshotParameterType::EffectBypass, std::string_view("Effect Bypass")},
            std::pair{Keire::AudioMixerSnapshotParameterType::EffectParameter, std::string_view("Effect Parameter")},
        };

        [[nodiscard]] Keire::AudioMixerBusDefinition& RequireBus(Keire::AudioMixerDefinition& definition,
                                                                 const Keire::AssetId bus)
        {
            const auto found = std::ranges::find(definition.Buses, bus, &Keire::AudioMixerBusDefinition::Id);
            if (found == definition.Buses.end())
                throw std::invalid_argument("Audio Mixer bus is unavailable.");
            return *found;
        }

        [[nodiscard]] Keire::AudioMixerEffectDefinition& RequireEffect(Keire::AudioMixerDefinition& definition,
                                                                       const Keire::AssetId effect)
        {
            for (auto& bus : definition.Buses)
            {
                const auto found = std::ranges::find(bus.Effects, effect, &Keire::AudioMixerEffectDefinition::Id);
                if (found != bus.Effects.end())
                    return *found;
            }
            throw std::invalid_argument("Audio Mixer effect is unavailable.");
        }

        [[nodiscard]] Keire::AudioMixerSendDefinition& RequireSend(Keire::AudioMixerDefinition& definition,
                                                                   const Keire::AssetId send)
        {
            for (auto& bus : definition.Buses)
            {
                const auto found = std::ranges::find(bus.Sends, send, &Keire::AudioMixerSendDefinition::Id);
                if (found != bus.Sends.end())
                    return *found;
            }
            throw std::invalid_argument("Audio Mixer send is unavailable.");
        }

        [[nodiscard]] Keire::AudioMixerSnapshotDefinition& RequireSnapshot(Keire::AudioMixerDefinition& definition,
                                                                           const Keire::AssetId snapshot)
        {
            const auto found =
                std::ranges::find(definition.Snapshots, snapshot, &Keire::AudioMixerSnapshotDefinition::Id);
            if (found == definition.Snapshots.end())
                throw std::invalid_argument("Audio Mixer snapshot is unavailable.");
            return *found;
        }

        [[nodiscard]] Keire::AudioMixerDuckingDefinition& RequireDucking(Keire::AudioMixerDefinition& definition,
                                                                         const Keire::AssetId ducking)
        {
            const auto found = std::ranges::find(definition.Ducking, ducking, &Keire::AudioMixerDuckingDefinition::Id);
            if (found == definition.Ducking.end())
                throw std::invalid_argument("Audio Mixer ducking rule is unavailable.");
            return *found;
        }

        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueName(const Range& values, std::string base, Projection projection)
        {
            std::string candidate = base;
            for (std::size_t suffix = 2; std::ranges::any_of(values, [&](const auto& value)
                                                             { return std::invoke(projection, value) == candidate; });
                 ++suffix)
            {
                candidate = base + " " + std::to_string(suffix);
            }
            return candidate;
        }

        [[nodiscard]] std::size_t BusDepth(const Keire::AudioMixerDefinition& definition,
                                           const Keire::AudioMixerBusDefinition& bus)
        {
            std::size_t depth = 0;
            auto parent = bus.Parent;
            while (parent)
            {
                const auto found = std::ranges::find(definition.Buses, parent, &Keire::AudioMixerBusDefinition::Id);
                if (found == definition.Buses.end())
                    break;
                ++depth;
                parent = found->Parent;
            }
            return depth;
        }

        [[nodiscard]] std::string_view EffectTypeName(const Keire::AudioGraphNodeType type)
        {
            const auto found = std::ranges::find(EffectTypes, type, &EffectTypeEntry::Type);
            return found == EffectTypes.end() ? "Unsupported" : found->Name;
        }

        [[nodiscard]] std::string_view SnapshotTypeName(const Keire::AudioMixerSnapshotParameterType type)
        {
            const auto found =
                std::ranges::find_if(SnapshotTypes, [type](const auto& entry) { return entry.first == type; });
            return found == SnapshotTypes.end() ? "Unsupported" : found->second;
        }

        [[nodiscard]] const Keire::AudioMixerBusDefinition* FindBus(const Keire::AudioMixerDefinition& definition,
                                                                    const Keire::AssetId bus)
        {
            const auto found = std::ranges::find(definition.Buses, bus, &Keire::AudioMixerBusDefinition::Id);
            return found == definition.Buses.end() ? nullptr : std::addressof(*found);
        }
    } // namespace

    void AudioMixerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.audio-mixer", "Audio Mixer", false});
    }

    bool AudioMixerPanel::ApplyEdit(const std::string_view name,
                                    const std::function<void(Keire::AudioMixerDefinition&)>& operation)
    {
        try
        {
            const bool changed = m_Controller.AudioMixerState().Edit(name, operation);
            if (changed)
                m_Message = std::string(name) + ".";
            return changed;
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
            m_Controller.ReportAudioMixerError(m_Message);
            return false;
        }
    }

    void AudioMixerPanel::Draw(Keire::UiFrame& ui)
    {
        if (!m_Registration.Visible())
        {
            if (m_WasVisible)
                StopTransientPreview();
            return;
        }
        auto panel = ui.BeginPanel(m_Registration);
        if (!m_Registration.Visible())
        {
            StopTransientPreview();
            return;
        }
        m_WasVisible = true;
        if (!panel)
            return;

        auto& document = m_Controller.AudioMixerState();
        const auto& theme = m_Controller.AudioMixerTheme();
        if (ui.WindowFocused())
            m_Controller.ActivateAudioMixerHistory();
        if (!document.IsOpen())
        {
            ui.TextColored(theme.Accent, "AUDIO MIXER");
            ui.Separator();
            ui.Text("No Audio Mixer asset is open.");
            ui.TextColored(theme.MutedText, "Create or double-click a .keiremixer asset in the Project panel.");
            return;
        }

        const auto database = m_Controller.AudioMixerDatabase();
        const auto record = database ? database->Find(document.Asset()) : std::nullopt;
        ui.TextColored(theme.Accent, "AUDIO MIXER");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (document.Dirty() ? " *" : "") : "Missing asset");
        ui.Separator();
        if (ui.Shortcut({Keire::UiKey::S, true}) && document.Dirty())
        {
            try
            {
                m_Controller.SaveAudioMixerDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        }
        if (ui.Button("Save"))
        {
            try
            {
                m_Controller.SaveAudioMixerDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.Dirty()); disabled)
        {
            if (ui.Button("Discard"))
            {
                try
                {
                    m_Controller.DiscardAudioMixerDocument();
                }
                catch (const std::exception& error)
                {
                    m_Message = error.what();
                    m_Controller.ReportAudioMixerError(m_Message);
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Reload"))
        {
            try
            {
                m_Controller.ReloadAudioMixerDocument(document.Asset());
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        }
        ui.SameLine();
        const auto undo = document.UndoContext();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
                m_Controller.UndoAudioMixerEdit();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
                m_Controller.RedoAudioMixerEdit();
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::ValidateAudioMixer(document.Definition());
                m_Message = "Validation passed.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        }
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        if (const auto preview = m_Controller.AudioMixerPreviewDiagnostic(); !preview.empty())
            ui.TextColored(theme.Warning, preview);
        ui.Separator();

        if (auto tabs = ui.BeginTabBar("AudioMixerTabs"); tabs)
        {
            if (auto routing = ui.BeginTabItem("Routing"); routing)
                DrawRouting(ui);
            if (auto bus = ui.BeginTabItem("Selected Bus"); bus)
                DrawSelectedBus(ui);
            if (auto snapshots = ui.BeginTabItem("Snapshots"); snapshots)
                DrawSnapshots(ui);
            if (auto ducking = ui.BeginTabItem("Ducking"); ducking)
                DrawDucking(ui);
        }
    }

    void AudioMixerPanel::DrawRouting(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.AudioMixerState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.AudioMixerTheme();
        if (auto hierarchy = ui.BeginChild("MixerBusHierarchy", {230.0F, 0.0F}, true); hierarchy)
        {
            ui.TextColored(theme.Accent, "BUS HIERARCHY");
            for (const auto& bus : definition.Buses)
            {
                const auto label = std::string(BusDepth(definition, bus) * 2, ' ') + bus.Name;
                if (ui.Selectable(label, document.SelectedBus() == bus.Id))
                    document.SelectBus(bus.Id);
            }
            ui.Separator();
            if (ui.Button("+ Bus"))
            {
                const auto parent = document.SelectedBus().value_or(definition.MasterBus);
                Keire::AudioMixerBusDefinition added{
                    .Id = Keire::AssetId::Generate(),
                    .Name = UniqueName(definition.Buses, "Bus", &Keire::AudioMixerBusDefinition::Name),
                    .Parent = parent,
                };
                const auto id = added.Id;
                if (document.AddBus(std::move(added)))
                    document.SelectBus(id);
            }
            const bool removable = document.SelectedBus() && *document.SelectedBus() != definition.MasterBus;
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!removable); disabled)
            {
                if (ui.Button("Remove") && document.SelectedBus())
                    (void)document.RemoveBus(*document.SelectedBus());
            }
        }
        ui.SameLine();
        if (auto canvas = ui.BeginChild("MixerRoutingCanvas", {}, true); canvas)
        {
            if (ui.Button("Frame All"))
                document.FocusRouting(ui.ContentAvailable());
            ui.SameLine();
            ui.TextColored(theme.MutedText, "Solid routes are outputs; Pre/Post routes are sends.");
            (void)document.DrawRouting(ui, "AudioMixerRouting");
        }
    }

    void AudioMixerPanel::DrawSelectedBus(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.AudioMixerState();
        const auto& definition = document.Definition();
        const auto selected = document.SelectedBus().value_or(definition.MasterBus);
        const auto found = FindBus(definition, selected);
        if (!found)
        {
            ui.Text("Select a bus in the Routing tab.");
            return;
        }
        const auto bus = *found;
        const auto& theme = m_Controller.AudioMixerTheme();
        const auto run = [&](const std::string_view name, const auto& operation)
        {
            try
            {
                if (operation())
                    m_Message = std::string(name) + ".";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        };

        ui.TextColored(theme.Accent, bus.Name);
        ui.TextColored(theme.MutedText, "Stable ID: " + bus.Id.ToString());
        std::string name = bus.Name;
        if (auto disabled = ui.BeginDisabled(bus.Id == definition.MasterBus); disabled)
        {
            if (ui.InputText("Name", name))
                (void)ApplyEdit("Rename Audio Mixer bus",
                                [id = bus.Id, name = std::move(name)](Keire::AudioMixerDefinition& candidate) mutable
                                { RequireBus(candidate, id).Name = std::move(name); });
        }
        if (bus.Id != definition.MasterBus)
        {
            const auto parent = FindBus(definition, bus.Parent);
            if (auto combo = ui.BeginCombo("Output", parent ? parent->Name : "Missing Bus"); combo)
            {
                for (const auto& candidate : definition.Buses)
                {
                    if (candidate.Id == bus.Id)
                        continue;
                    if (ui.Selectable(candidate.Name, candidate.Id == bus.Parent))
                    {
                        (void)ApplyEdit("Route Audio Mixer bus",
                                        [id = bus.Id, parentId = candidate.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireBus(mixer, id).Parent = parentId; });
                    }
                }
            }
        }
        double gain = bus.Gain;
        if (ui.DragScalar("Fader (linear)", gain, 0.01, 0.0, 16.0))
            (void)ApplyEdit("Edit Audio Mixer fader",
                            [id = bus.Id, value = static_cast<float>(gain)](Keire::AudioMixerDefinition& mixer)
                            { RequireBus(mixer, id).Gain = value; });
        bool mute = bus.Mute;
        if (ui.Checkbox("Mute", mute))
            (void)ApplyEdit("Edit Audio Mixer mute", [id = bus.Id, mute](Keire::AudioMixerDefinition& mixer)
                            { RequireBus(mixer, id).Mute = mute; });
        ui.SameLine();
        bool solo = bus.Solo;
        if (ui.Checkbox("Solo", solo))
            (void)ApplyEdit("Edit Audio Mixer solo", [id = bus.Id, solo](Keire::AudioMixerDefinition& mixer)
                            { RequireBus(mixer, id).Solo = solo; });

        ui.Separator();
        ui.TextColored(theme.Accent, "ORDERED EFFECT RACK");
        for (std::size_t index = 0; index < bus.Effects.size(); ++index)
        {
            const auto effect = bus.Effects[index];
            auto id = ui.PushId(effect.Id.ToString());
            if (auto tree = ui.BeginTreeNode(std::to_string(index + 1) + ". " + effect.Name); tree)
            {
                std::string effectName = effect.Name;
                if (ui.InputText("Name", effectName))
                    (void)ApplyEdit("Rename Audio Mixer effect",
                                    [effectId = effect.Id,
                                     nameValue = std::move(effectName)](Keire::AudioMixerDefinition& mixer) mutable
                                    { RequireEffect(mixer, effectId).Name = std::move(nameValue); });
                bool bypassed = effect.Bypassed;
                if (ui.Checkbox("Bypass", bypassed))
                    (void)ApplyEdit("Bypass Audio Mixer effect",
                                    [effectId = effect.Id, bypassed](Keire::AudioMixerDefinition& mixer)
                                    { RequireEffect(mixer, effectId).Bypassed = bypassed; });
                if (auto typeCombo = ui.BeginCombo("Type", EffectTypeName(effect.Type)); typeCombo)
                {
                    for (const auto& type : EffectTypes)
                    {
                        if (ui.Selectable(type.Name, effect.Type == type.Type))
                        {
                            if (type.Type == Keire::AudioGraphNodeType::ConvolutionReverb && !effect.ImpulseResponse)
                            {
                                m_Message =
                                    "Convolution Reverb requires an impulse-response AudioClip stable ID first.";
                            }
                            else
                            {
                                (void)ApplyEdit(
                                    "Change Audio Mixer effect type",
                                    [effectId = effect.Id, replacement = type.Type](Keire::AudioMixerDefinition& mixer)
                                    {
                                        auto& edited = RequireEffect(mixer, effectId);
                                        edited.Type = replacement;
                                        if (replacement != Keire::AudioGraphNodeType::ConvolutionReverb)
                                            edited.ImpulseResponse = {};
                                    });
                            }
                        }
                    }
                }
                if (effect.Type == Keire::AudioGraphNodeType::ConvolutionReverb)
                {
                    auto impulse = effect.ImpulseResponse.ToString();
                    if (ui.InputText("Impulse Response ID", impulse))
                    {
                        try
                        {
                            const auto parsed = Keire::AssetId::Parse(impulse);
                            (void)ApplyEdit("Assign convolution impulse response",
                                            [effectId = effect.Id, parsed](Keire::AudioMixerDefinition& mixer)
                                            { RequireEffect(mixer, effectId).ImpulseResponse = parsed; });
                        }
                        catch (const std::exception& error)
                        {
                            m_Message = error.what();
                        }
                    }
                }
                ui.TextColored(theme.MutedText, "PARAMETERS");
                for (std::size_t parameter = 0; parameter < effect.Parameters.size(); ++parameter)
                {
                    double value = effect.Parameters[parameter];
                    if (ui.DragScalar("Parameter " + std::to_string(parameter), value, 0.01))
                        (void)ApplyEdit("Edit Audio Mixer effect parameter",
                                        [effectId = effect.Id, parameter,
                                         value = static_cast<float>(value)](Keire::AudioMixerDefinition& mixer)
                                        { RequireEffect(mixer, effectId).Parameters.at(parameter) = value; });
                }
                if (effect.Parameters.size() < 64 && ui.Button("+ Parameter"))
                    (void)ApplyEdit("Add Audio Mixer effect parameter",
                                    [effectId = effect.Id](Keire::AudioMixerDefinition& mixer)
                                    { RequireEffect(mixer, effectId).Parameters.push_back(0.0F); });
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(effect.Parameters.empty()); disabled)
                {
                    if (ui.Button("- Parameter"))
                        (void)ApplyEdit(
                            "Remove Audio Mixer effect parameter",
                            [effectId = effect.Id](Keire::AudioMixerDefinition& mixer)
                            {
                                auto& edited = RequireEffect(mixer, effectId);
                                const auto removed = static_cast<std::uint32_t>(edited.Parameters.size() - 1);
                                edited.Parameters.pop_back();
                                for (auto& snapshot : mixer.Snapshots)
                                {
                                    std::erase_if(
                                        snapshot.Parameters,
                                        [effectId,
                                         removed](const Keire::AudioMixerSnapshotParameterDefinition& parameter)
                                        {
                                            return parameter.Type ==
                                                       Keire::AudioMixerSnapshotParameterType::EffectParameter &&
                                                   parameter.Target == effectId && parameter.Parameter == removed;
                                        });
                                }
                            });
                }
                if (index > 0 && ui.Button("Move Up"))
                    run("Reordered Audio Mixer effect",
                        [&] { return document.MoveEffect(bus.Id, effect.Id, index - 1); });
                if (index > 0)
                    ui.SameLine();
                if (index + 1 < bus.Effects.size() && ui.Button("Move Down"))
                    run("Reordered Audio Mixer effect",
                        [&] { return document.MoveEffect(bus.Id, effect.Id, index + 1); });
                if (index + 1 < bus.Effects.size())
                    ui.SameLine();
                if (ui.Button("Remove Effect"))
                    run("Removed Audio Mixer effect", [&] { return document.RemoveEffect(effect.Id); });
            }
        }
        if (ui.Button("+ Effect"))
        {
            Keire::AudioMixerEffectDefinition effect{
                .Id = Keire::AssetId::Generate(),
                .Name = UniqueName(bus.Effects, "Gain", &Keire::AudioMixerEffectDefinition::Name),
                .Type = Keire::AudioGraphNodeType::Gain,
                .Parameters = {1.0F},
            };
            run("Added Audio Mixer effect", [&] { return document.AddEffect(bus.Id, std::move(effect)); });
        }

        ui.Separator();
        ui.TextColored(theme.Accent, "SENDS");
        for (const auto& send : bus.Sends)
        {
            auto id = ui.PushId(send.Id.ToString());
            const auto destination = FindBus(definition, send.DestinationBus);
            if (auto tree = ui.BeginTreeNode(destination ? destination->Name : "Missing Bus"); tree)
            {
                if (auto destinationCombo =
                        ui.BeginCombo("Destination", destination ? destination->Name : "Missing Bus");
                    destinationCombo)
                {
                    for (const auto& candidate : definition.Buses)
                    {
                        if (candidate.Id == bus.Id)
                            continue;
                        if (ui.Selectable(candidate.Name, candidate.Id == send.DestinationBus))
                            (void)ApplyEdit("Route Audio Mixer send", [sendId = send.Id, destinationId = candidate.Id](
                                                                          Keire::AudioMixerDefinition& mixer)
                                            { RequireSend(mixer, sendId).DestinationBus = destinationId; });
                    }
                }
                const auto stageName = send.Stage == Keire::AudioMixerSendStage::PreFader ? "Pre Fader" : "Post Fader";
                if (auto stageCombo = ui.BeginCombo("Stage", stageName); stageCombo)
                {
                    if (ui.Selectable("Pre Fader", send.Stage == Keire::AudioMixerSendStage::PreFader))
                        (void)ApplyEdit("Edit Audio Mixer send stage",
                                        [sendId = send.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireSend(mixer, sendId).Stage = Keire::AudioMixerSendStage::PreFader; });
                    if (ui.Selectable("Post Fader", send.Stage == Keire::AudioMixerSendStage::PostFader))
                        (void)ApplyEdit("Edit Audio Mixer send stage",
                                        [sendId = send.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireSend(mixer, sendId).Stage = Keire::AudioMixerSendStage::PostFader; });
                }
                double sendGain = send.Gain;
                if (ui.DragScalar("Gain", sendGain, 0.01, 0.0, 16.0))
                    (void)ApplyEdit(
                        "Edit Audio Mixer send gain",
                        [sendId = send.Id, value = static_cast<float>(sendGain)](Keire::AudioMixerDefinition& mixer)
                        { RequireSend(mixer, sendId).Gain = value; });
                if (ui.Button("Remove Send"))
                    run("Removed Audio Mixer send", [&] { return document.RemoveSend(send.Id); });
            }
        }
        const auto sendTarget =
            std::ranges::find_if(definition.Buses, [&](const auto& candidate) { return candidate.Id != bus.Id; });
        if (auto disabled = ui.BeginDisabled(bus.Id == definition.MasterBus || sendTarget == definition.Buses.end());
            disabled)
        {
            if (ui.Button("+ Send"))
            {
                Keire::AudioMixerSendDefinition send{
                    .Id = Keire::AssetId::Generate(),
                    .DestinationBus = definition.MasterBus,
                    .Stage = Keire::AudioMixerSendStage::PostFader,
                    .Gain = 1.0F,
                };
                run("Added Audio Mixer send", [&] { return document.AddSend(bus.Id, std::move(send)); });
            }
        }
    }

    void AudioMixerPanel::DrawSnapshots(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.AudioMixerState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.AudioMixerTheme();
        if (!m_SelectedSnapshot ||
            std::ranges::find(definition.Snapshots, m_SelectedSnapshot, &Keire::AudioMixerSnapshotDefinition::Id) ==
                definition.Snapshots.end())
        {
            m_SelectedSnapshot = definition.Snapshots.empty() ? Keire::AssetId{} : definition.Snapshots.front().Id;
        }
        const auto run = [&](const std::string_view name, const auto& operation)
        {
            try
            {
                if (operation())
                    m_Message = std::string(name) + ".";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        };

        if (auto list = ui.BeginChild("MixerSnapshotList", {230.0F, 0.0F}, true); list)
        {
            ui.TextColored(theme.Accent, "SNAPSHOTS");
            for (const auto& snapshot : definition.Snapshots)
                if (ui.Selectable(snapshot.Name, snapshot.Id == m_SelectedSnapshot))
                    m_SelectedSnapshot = snapshot.Id;
            ui.Separator();
            if (ui.Button("+ Snapshot"))
            {
                Keire::AudioMixerSnapshotDefinition snapshot{
                    .Id = Keire::AssetId::Generate(),
                    .Name = UniqueName(definition.Snapshots, "Snapshot", &Keire::AudioMixerSnapshotDefinition::Name),
                };
                const auto id = snapshot.Id;
                run("Added Audio Mixer snapshot", [&] { return document.AddSnapshot(std::move(snapshot)); });
                m_SelectedSnapshot = id;
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!m_SelectedSnapshot); disabled)
            {
                if (ui.Button("Remove"))
                {
                    const auto removed = m_SelectedSnapshot;
                    run("Removed Audio Mixer snapshot", [&] { return document.RemoveSnapshot(removed); });
                    m_SelectedSnapshot = {};
                }
            }
        }
        ui.SameLine();
        if (auto details = ui.BeginChild("MixerSnapshotDetails", {}, true); details)
        {
            const auto current =
                std::ranges::find(definition.Snapshots, m_SelectedSnapshot, &Keire::AudioMixerSnapshotDefinition::Id);
            if (current == definition.Snapshots.end())
            {
                ui.Text("Select or create a snapshot.");
                return;
            }
            const auto snapshot = *current;
            std::string name = snapshot.Name;
            if (ui.InputText("Name", name))
                (void)ApplyEdit("Rename Audio Mixer snapshot",
                                [id = snapshot.Id, name = std::move(name)](Keire::AudioMixerDefinition& mixer) mutable
                                { RequireSnapshot(mixer, id).Name = std::move(name); });
            ui.TextColored(theme.MutedText, "Stable ID: " + snapshot.Id.ToString());
            ui.Separator();

            for (std::size_t index = 0; index < snapshot.Parameters.size(); ++index)
            {
                const auto parameter = snapshot.Parameters[index];
                auto id = ui.PushId(std::to_string(index));
                ui.TextColored(theme.Accent, SnapshotTypeName(parameter.Type));
                if (auto typeCombo = ui.BeginCombo("Type", SnapshotTypeName(parameter.Type)); typeCombo)
                {
                    for (const auto& [type, label] : SnapshotTypes)
                    {
                        if (!ui.Selectable(label, parameter.Type == type))
                            continue;
                        Keire::AssetId target;
                        std::uint32_t parameterIndex = 0;
                        float value = 0.0F;
                        if (type == Keire::AudioMixerSnapshotParameterType::BusGain ||
                            type == Keire::AudioMixerSnapshotParameterType::BusMute ||
                            type == Keire::AudioMixerSnapshotParameterType::BusSolo)
                        {
                            target = definition.MasterBus;
                            value = type == Keire::AudioMixerSnapshotParameterType::BusGain ? 1.0F : 0.0F;
                        }
                        else if (type == Keire::AudioMixerSnapshotParameterType::SendGain)
                        {
                            for (const auto& bus : definition.Buses)
                                if (!bus.Sends.empty())
                                {
                                    target = bus.Sends.front().Id;
                                    break;
                                }
                            value = 1.0F;
                        }
                        else
                        {
                            for (const auto& bus : definition.Buses)
                            {
                                const auto effect =
                                    type == Keire::AudioMixerSnapshotParameterType::EffectParameter
                                        ? std::ranges::find_if(bus.Effects, [](const auto& candidate)
                                                               { return !candidate.Parameters.empty(); })
                                        : bus.Effects.begin();
                                if (effect != bus.Effects.end())
                                {
                                    target = effect->Id;
                                    break;
                                }
                            }
                        }
                        if (!target)
                        {
                            m_Message = "Add a compatible send or effect before using that snapshot parameter.";
                            continue;
                        }
                        (void)ApplyEdit("Change Audio Mixer snapshot parameter",
                                        [snapshotId = snapshot.Id, index, type, target, parameterIndex,
                                         value](Keire::AudioMixerDefinition& mixer)
                                        {
                                            auto& edited = RequireSnapshot(mixer, snapshotId).Parameters.at(index);
                                            edited.Type = type;
                                            edited.Target = target;
                                            edited.Parameter = parameterIndex;
                                            edited.Value = value;
                                        });
                    }
                }

                const bool busTarget = parameter.Type == Keire::AudioMixerSnapshotParameterType::BusGain ||
                                       parameter.Type == Keire::AudioMixerSnapshotParameterType::BusMute ||
                                       parameter.Type == Keire::AudioMixerSnapshotParameterType::BusSolo;
                if (busTarget)
                {
                    const auto bus = FindBus(definition, parameter.Target);
                    if (auto targetCombo = ui.BeginCombo("Target", bus ? bus->Name : "Missing Bus"); targetCombo)
                    {
                        for (const auto& candidate : definition.Buses)
                            if (ui.Selectable(candidate.Name, candidate.Id == parameter.Target))
                                (void)ApplyEdit(
                                    "Retarget Audio Mixer snapshot parameter",
                                    [snapshotId = snapshot.Id, index,
                                     target = candidate.Id](Keire::AudioMixerDefinition& mixer)
                                    { RequireSnapshot(mixer, snapshotId).Parameters.at(index).Target = target; });
                    }
                }
                else if (parameter.Type == Keire::AudioMixerSnapshotParameterType::SendGain)
                {
                    std::string targetName = "Missing Send";
                    for (const auto& bus : definition.Buses)
                    {
                        const auto found =
                            std::ranges::find(bus.Sends, parameter.Target, &Keire::AudioMixerSendDefinition::Id);
                        if (found != bus.Sends.end())
                        {
                            const auto destination = FindBus(definition, found->DestinationBus);
                            targetName = bus.Name + " -> " + (destination ? destination->Name : "Missing");
                            break;
                        }
                    }
                    if (auto targetCombo = ui.BeginCombo("Target", targetName); targetCombo)
                    {
                        for (const auto& bus : definition.Buses)
                            for (const auto& send : bus.Sends)
                            {
                                const auto destination = FindBus(definition, send.DestinationBus);
                                const auto label = bus.Name + " -> " + (destination ? destination->Name : "Missing");
                                if (ui.Selectable(label, send.Id == parameter.Target))
                                    (void)ApplyEdit(
                                        "Retarget Audio Mixer snapshot parameter",
                                        [snapshotId = snapshot.Id, index,
                                         target = send.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireSnapshot(mixer, snapshotId).Parameters.at(index).Target = target; });
                            }
                    }
                }
                else
                {
                    std::string targetName = "Missing Effect";
                    const Keire::AudioMixerEffectDefinition* targetEffect = nullptr;
                    for (const auto& bus : definition.Buses)
                    {
                        const auto found =
                            std::ranges::find(bus.Effects, parameter.Target, &Keire::AudioMixerEffectDefinition::Id);
                        if (found != bus.Effects.end())
                        {
                            targetName = bus.Name + " / " + found->Name;
                            targetEffect = std::addressof(*found);
                            break;
                        }
                    }
                    if (auto targetCombo = ui.BeginCombo("Target", targetName); targetCombo)
                    {
                        for (const auto& bus : definition.Buses)
                            for (const auto& effect : bus.Effects)
                            {
                                if (parameter.Type == Keire::AudioMixerSnapshotParameterType::EffectParameter &&
                                    effect.Parameters.empty())
                                    continue;
                                if (ui.Selectable(bus.Name + " / " + effect.Name, effect.Id == parameter.Target))
                                    (void)ApplyEdit("Retarget Audio Mixer snapshot parameter",
                                                    [snapshotId = snapshot.Id, index,
                                                     target = effect.Id](Keire::AudioMixerDefinition& mixer)
                                                    {
                                                        auto& edited =
                                                            RequireSnapshot(mixer, snapshotId).Parameters.at(index);
                                                        edited.Target = target;
                                                        edited.Parameter = 0;
                                                    });
                            }
                    }
                    if (parameter.Type == Keire::AudioMixerSnapshotParameterType::EffectParameter && targetEffect &&
                        !targetEffect->Parameters.empty())
                    {
                        std::int64_t parameterIndex = parameter.Parameter;
                        if (ui.DragInteger("Parameter", parameterIndex, 1.0, 0,
                                           static_cast<std::int64_t>(targetEffect->Parameters.size() - 1)))
                            (void)ApplyEdit("Edit Audio Mixer snapshot parameter index",
                                            [snapshotId = snapshot.Id, index,
                                             parameterIndex = static_cast<std::uint32_t>(parameterIndex)](
                                                Keire::AudioMixerDefinition& mixer)
                                            {
                                                RequireSnapshot(mixer, snapshotId).Parameters.at(index).Parameter =
                                                    parameterIndex;
                                            });
                    }
                }

                if (parameter.Type == Keire::AudioMixerSnapshotParameterType::BusMute ||
                    parameter.Type == Keire::AudioMixerSnapshotParameterType::BusSolo ||
                    parameter.Type == Keire::AudioMixerSnapshotParameterType::EffectBypass)
                {
                    bool value = parameter.Value != 0.0F;
                    if (ui.Checkbox("Value", value))
                        (void)ApplyEdit(
                            "Edit Audio Mixer snapshot value",
                            [snapshotId = snapshot.Id, index, value](Keire::AudioMixerDefinition& mixer)
                            { RequireSnapshot(mixer, snapshotId).Parameters.at(index).Value = value ? 1.0F : 0.0F; });
                }
                else
                {
                    double value = parameter.Value;
                    if (ui.DragScalar("Value", value, 0.01))
                        (void)ApplyEdit("Edit Audio Mixer snapshot value",
                                        [snapshotId = snapshot.Id, index,
                                         value = static_cast<float>(value)](Keire::AudioMixerDefinition& mixer)
                                        { RequireSnapshot(mixer, snapshotId).Parameters.at(index).Value = value; });
                }
                if (ui.Button("Remove Parameter"))
                    (void)ApplyEdit("Remove Audio Mixer snapshot parameter",
                                    [snapshotId = snapshot.Id, index](Keire::AudioMixerDefinition& mixer)
                                    {
                                        auto& parameters = RequireSnapshot(mixer, snapshotId).Parameters;
                                        parameters.erase(parameters.begin() + static_cast<std::ptrdiff_t>(index));
                                    });
                ui.Separator();
            }
            if (ui.Button("+ Bus Gain"))
            {
                const auto target = std::ranges::find_if(
                    definition.Buses,
                    [&](const Keire::AudioMixerBusDefinition& bus)
                    {
                        return std::ranges::none_of(snapshot.Parameters,
                                                    [&](const Keire::AudioMixerSnapshotParameterDefinition& parameter)
                                                    {
                                                        return parameter.Type ==
                                                                   Keire::AudioMixerSnapshotParameterType::BusGain &&
                                                               parameter.Target == bus.Id;
                                                    });
                    });
                if (target == definition.Buses.end())
                    m_Message = "Every bus already has a gain entry in this snapshot.";
                else
                    (void)ApplyEdit("Add Audio Mixer snapshot parameter",
                                    [snapshotId = snapshot.Id, target = target->Id](Keire::AudioMixerDefinition& mixer)
                                    {
                                        RequireSnapshot(mixer, snapshotId)
                                            .Parameters.push_back(
                                                {.Type = Keire::AudioMixerSnapshotParameterType::BusGain,
                                                 .Target = target,
                                                 .Value = 1.0F});
                                    });
            }
        }
    }

    void AudioMixerPanel::DrawDucking(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.AudioMixerState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.AudioMixerTheme();
        if (!m_SelectedDucking ||
            std::ranges::find(definition.Ducking, m_SelectedDucking, &Keire::AudioMixerDuckingDefinition::Id) ==
                definition.Ducking.end())
        {
            m_SelectedDucking = definition.Ducking.empty() ? Keire::AssetId{} : definition.Ducking.front().Id;
        }
        const auto run = [&](const std::string_view name, const auto& operation)
        {
            try
            {
                if (operation())
                    m_Message = std::string(name) + ".";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportAudioMixerError(m_Message);
            }
        };

        if (auto list = ui.BeginChild("MixerDuckingList", {230.0F, 0.0F}, true); list)
        {
            ui.TextColored(theme.Accent, "SIDECHAIN DUCKING");
            for (const auto& ducking : definition.Ducking)
                if (ui.Selectable(ducking.Name, ducking.Id == m_SelectedDucking))
                    m_SelectedDucking = ducking.Id;
            ui.Separator();
            if (auto disabled = ui.BeginDisabled(definition.Buses.size() < 2); disabled)
            {
                if (ui.Button("+ Rule"))
                {
                    Keire::AudioMixerDuckingDefinition ducking{
                        .Id = Keire::AssetId::Generate(),
                        .Name = UniqueName(definition.Ducking, "Ducking", &Keire::AudioMixerDuckingDefinition::Name),
                        .SidechainBus = definition.Buses[1].Id,
                        .TargetBus = definition.MasterBus,
                    };
                    const auto id = ducking.Id;
                    run("Added Audio Mixer ducking", [&] { return document.AddDucking(std::move(ducking)); });
                    m_SelectedDucking = id;
                }
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!m_SelectedDucking); disabled)
            {
                if (ui.Button("Remove"))
                {
                    const auto removed = m_SelectedDucking;
                    run("Removed Audio Mixer ducking", [&] { return document.RemoveDucking(removed); });
                    m_SelectedDucking = {};
                }
            }
        }
        ui.SameLine();
        if (auto details = ui.BeginChild("MixerDuckingDetails", {}, true); details)
        {
            const auto found =
                std::ranges::find(definition.Ducking, m_SelectedDucking, &Keire::AudioMixerDuckingDefinition::Id);
            if (found == definition.Ducking.end())
            {
                ui.Text("Select or create a ducking rule.");
                return;
            }
            const auto ducking = *found;
            std::string name = ducking.Name;
            if (ui.InputText("Name", name))
                (void)ApplyEdit("Rename Audio Mixer ducking",
                                [id = ducking.Id, name = std::move(name)](Keire::AudioMixerDefinition& mixer) mutable
                                { RequireDucking(mixer, id).Name = std::move(name); });
            const auto sidechain = FindBus(definition, ducking.SidechainBus);
            if (auto combo = ui.BeginCombo("Sidechain", sidechain ? sidechain->Name : "Missing Bus"); combo)
            {
                for (const auto& bus : definition.Buses)
                {
                    if (bus.Id == ducking.TargetBus)
                        continue;
                    if (ui.Selectable(bus.Name, bus.Id == ducking.SidechainBus))
                        (void)ApplyEdit("Edit Audio Mixer ducking sidechain",
                                        [id = ducking.Id, busId = bus.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireDucking(mixer, id).SidechainBus = busId; });
                }
            }
            const auto target = FindBus(definition, ducking.TargetBus);
            if (auto combo = ui.BeginCombo("Target", target ? target->Name : "Missing Bus"); combo)
            {
                for (const auto& bus : definition.Buses)
                {
                    if (bus.Id == ducking.SidechainBus)
                        continue;
                    if (ui.Selectable(bus.Name, bus.Id == ducking.TargetBus))
                        (void)ApplyEdit("Edit Audio Mixer ducking target",
                                        [id = ducking.Id, busId = bus.Id](Keire::AudioMixerDefinition& mixer)
                                        { RequireDucking(mixer, id).TargetBus = busId; });
                }
            }
            const auto scalar = [&](const std::string_view label, const std::string_view undoName, const float current,
                                    const double minimum, const double maximum, auto member)
            {
                double value = current;
                if (ui.DragScalar(label, value, 0.01, minimum, maximum))
                    (void)ApplyEdit(undoName, [id = ducking.Id, value = static_cast<float>(value),
                                               member](Keire::AudioMixerDefinition& mixer)
                                    { RequireDucking(mixer, id).*member = value; });
            };
            scalar("Threshold (dB)", "Edit Audio Mixer ducking threshold", ducking.ThresholdDb, -96.0, 0.0,
                   &Keire::AudioMixerDuckingDefinition::ThresholdDb);
            scalar("Ratio", "Edit Audio Mixer ducking ratio", ducking.Ratio, 1.0, 100.0,
                   &Keire::AudioMixerDuckingDefinition::Ratio);
            scalar("Attack (s)", "Edit Audio Mixer ducking attack", ducking.AttackSeconds, 0.0, 10.0,
                   &Keire::AudioMixerDuckingDefinition::AttackSeconds);
            scalar("Hold (s)", "Edit Audio Mixer ducking hold", ducking.HoldSeconds, 0.0, 10.0,
                   &Keire::AudioMixerDuckingDefinition::HoldSeconds);
            scalar("Release (s)", "Edit Audio Mixer ducking release", ducking.ReleaseSeconds, 0.0, 30.0,
                   &Keire::AudioMixerDuckingDefinition::ReleaseSeconds);
            scalar("Maximum Attenuation (dB)", "Edit Audio Mixer ducking attenuation", ducking.MaximumAttenuationDb,
                   0.0, 96.0, &Keire::AudioMixerDuckingDefinition::MaximumAttenuationDb);
        }
    }

    void AudioMixerPanel::ResetTransientState() noexcept
    {
        m_SelectedSnapshot = {};
        m_SelectedDucking = {};
        m_Message.clear();
    }

    void AudioMixerPanel::StopTransientPreview() noexcept
    {
        m_Controller.StopAudioMixerPreview();
        m_WasVisible = false;
    }
} // namespace KeireEditor
