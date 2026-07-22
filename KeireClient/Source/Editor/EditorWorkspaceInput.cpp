#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/InputActionsDocument.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

void EditorWorkspaceLayer::BeginInputTest()
{
    if (!m_InputContext || m_InputActionsDocument->Definition().ActionMaps.empty())
        throw std::logic_error("Open an imported input action asset before starting Input Test Mode.");
    EndInputTest();
    std::vector<Keire::InputActionSubscription> subscriptions;
    std::vector<Keire::InputCaptureOverride> captureOverrides;
    try
    {
        for (const auto& map : m_InputActionsDocument->Definition().ActionMaps)
        {
            if (!m_InputContext->EnableMap(map.Id))
                throw std::runtime_error("Input action context is still loading; try again next frame.");
            captureOverrides.push_back(m_InputContext->OverrideUiCapture(map.Id));
            for (const auto& action : map.Actions)
            {
                subscriptions.push_back(m_InputContext->Subscribe(
                    action.Id,
                    [this, actionId = action.Id, valueType = action.ValueType, mapName = map.Name,
                     actionName = action.Name](const Keire::InputActionEvent& event)
                    {
                        try
                        {
                            constexpr float inputEpsilon = 0.01F;
                            constexpr std::uint64_t coalescingWindowNanoseconds = 50'000'000;
                            constexpr std::size_t maximumHistoryEntries = 2048;
                            const auto magnitude =
                                std::sqrt(event.Value.X * event.Value.X + event.Value.Y * event.Value.Y);
                            const bool canceled = event.Phase == Keire::InputActionPhase::Canceled;
                            if (canceled && !m_InputRecordReleases)
                                return;
                            if (valueType != Keire::InputValueType::Boolean && magnitude <= inputEpsilon)
                                return;
                            const auto phase = event.Phase == Keire::InputActionPhase::Started     ? "Started"
                                               : event.Phase == Keire::InputActionPhase::Performed ? "Performed"
                                               : event.Phase == Keire::InputActionPhase::Canceled  ? "Canceled"
                                                                                                   : "Waiting";
                            if (event.Phase == Keire::InputActionPhase::Waiting)
                                return;
                            std::string scheme = "Automatic";
                            if (const auto input = Owner().Input())
                            {
                                const auto users = input->Users();
                                const auto user = std::ranges::find(users, event.User, &Keire::InputUserDescriptor::Id);
                                if (user != users.end() && !user->ControlScheme.empty())
                                    scheme = user->ControlScheme;
                            }
                            std::ostringstream message;
                            message << mapName << '/' << actionName << ' ' << phase << " value=[" << event.Value.X
                                    << ", " << event.Value.Y << "] user=" << event.User.Value()
                                    << " device=" << event.Device.Value() << " scheme=" << scheme
                                    << " duration=" << event.DurationSeconds
                                    << "s timestamp=" << event.TimestampNanoseconds << "ns";
                            if (!m_InputHistory.empty())
                            {
                                auto& previous = m_InputHistory.back();
                                if (previous.Action == actionId && previous.Phase == phase &&
                                    event.TimestampNanoseconds >= previous.TimestampNanoseconds &&
                                    event.TimestampNanoseconds - previous.TimestampNanoseconds <=
                                        coalescingWindowNanoseconds)
                                {
                                    previous.Value = event.Value;
                                    previous.User = event.User;
                                    previous.Device = event.Device;
                                    previous.TimestampNanoseconds = event.TimestampNanoseconds;
                                    ++previous.Repetitions;
                                    if (m_InputForwardToConsole)
                                        AddConsoleMessage("Input", message.str(), m_Theme.Accent);
                                    return;
                                }
                            }
                            if (m_InputHistory.size() == maximumHistoryEntries)
                                m_InputHistory.pop_front();
                            m_InputHistory.push_back({actionId, mapName, actionName, phase, event.Value, event.User,
                                                      event.Device, event.TimestampNanoseconds});
                            if (m_InputForwardToConsole)
                                AddConsoleMessage("Input", message.str(), m_Theme.Accent);
                        }
                        catch (...)
                        {
                            ReportError("Input", "Input debugger event processing failed with an unknown error.");
                        }
                    }));
            }
        }
    }
    catch (...)
    {
        m_InputContext->DisableAll();
        throw;
    }
    m_InputSubscriptions = std::move(subscriptions);
    m_InputCaptureOverrides = std::move(captureOverrides);
    m_InputTesting = true;
    m_InputDebuggerMessage =
        "Input Test Mode is active. Events stay in debugger history unless Console forwarding is enabled.";
}

void EditorWorkspaceLayer::EndInputTest() noexcept
{
    m_InputSubscriptions.clear();
    m_InputCaptureOverrides.clear();
    if (m_InputContext)
        m_InputContext->DisableAll();
    m_InputTesting = false;
}

void EditorWorkspaceLayer::DrawInputDebugger(Keire::UiFrame& ui)
{
    if (auto debugger = ui.BeginPanel(m_InputDebugger); debugger)
    {
        ui.TextColored(m_Theme.Accent, "INPUT DEBUGGER");
        ui.Separator();
        if (!m_InputActionsDocument->Asset())
        {
            ui.Text("No input action asset is attached.");
            if (const auto project = Owner().GetProject(); project && project->Descriptor().DefaultInput)
            {
                if (ui.Button("Attach Project Default Input"))
                {
                    try
                    {
                        OpenInputActions(project->Descriptor().DefaultInput);
                    }
                    catch (const std::exception& error)
                    {
                        m_InputDebuggerMessage = error.what();
                        ReportError("Input", m_InputDebuggerMessage);
                    }
                }
            }
            return;
        }
        ui.Text(m_InputActionsDocument->Definition().Name);
        if (!m_InputTesting)
        {
            if (ui.Button("Start Input Test"))
            {
                try
                {
                    BeginInputTest();
                }
                catch (const std::exception& error)
                {
                    m_InputDebuggerMessage = error.what();
                    ReportError("Input", m_InputDebuggerMessage);
                }
            }
        }
        else if (ui.Button("Stop Input Test"))
            EndInputTest();
        ui.SameLine();
        if (ui.Button("Clear History"))
            m_InputHistory.clear();
        (void)ui.Checkbox("Forward to Console", m_InputForwardToConsole);
        ui.SameLine();
        (void)ui.Checkbox("Record releases", m_InputRecordReleases);
        if (!m_InputDebuggerMessage.empty())
            ui.TextColored(m_Theme.MutedText, m_InputDebuggerMessage);
        if (const auto input = Owner().Input())
        {
            ui.Separator();
            ui.Text("DEVICES");
            for (const auto& device : input->Devices())
            {
                ui.Text(device.Name + "  id=" + std::to_string(device.Id.Value()) +
                        (device.Connected ? "  connected" : "  disconnected") +
                        (device.Paired ? "  paired" : "  unpaired"));
            }
            ui.Text("USERS");
            for (const auto& user : input->Users())
                ui.Text(user.Name + "  scheme=" + (user.ControlScheme.empty() ? "Automatic" : user.ControlScheme));
        }
        ui.Separator();
        ui.Text("EVENT HISTORY");
        if (m_InputHistory.empty())
        {
            ui.TextColored(m_Theme.MutedText, "Press a bound control to record an event. Idle input is filtered.");
        }
        else
        {
            for (auto entry = m_InputHistory.rbegin(); entry != m_InputHistory.rend(); ++entry)
            {
                std::ostringstream text;
                text << entry->Map << '/' << entry->Name << "  " << entry->Phase << "  [" << entry->Value.X << ", "
                     << entry->Value.Y << "]  user=" << entry->User.Value() << " device=" << entry->Device.Value();
                if (entry->Repetitions > 1)
                    text << "  x" << entry->Repetitions;
                ui.Text(text.str());
            }
        }
    }
}

KeireEditor::InputActionsDocument& EditorWorkspaceLayer::InputActionsState() noexcept
{
    return *m_InputActionsDocument;
}

const Keire::UiThemeDefinition& EditorWorkspaceLayer::InputActionsTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::InputAssetDatabase() const noexcept { return m_AssetDatabase; }

Keire::Ref<Keire::InputActionContext> EditorWorkspaceLayer::InputActionContext() const noexcept
{
    return m_InputContext;
}

Keire::Ref<Keire::InputSystem> EditorWorkspaceLayer::InputSystem() const noexcept { return Owner().Input(); }

void EditorWorkspaceLayer::ActivateInputHistory() noexcept
{
    m_ActiveUndoContext = m_InputActionsDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveInputActionsDocument() { SaveInputActions(); }

void EditorWorkspaceLayer::ReloadInputActionsDocument(const Keire::AssetId asset) { OpenInputActions(asset); }

void EditorWorkspaceLayer::RecordInputActionsUndo(const std::string_view name) { RecordInputUndo(name); }

void EditorWorkspaceLayer::UndoInputActions() { UndoInputEdit(); }

void EditorWorkspaceLayer::RedoInputActions() { RedoInputEdit(); }

void EditorWorkspaceLayer::ReportInputActionsError(std::string message) noexcept
{
    ReportError("Input", std::move(message));
}
