#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

void EditorWorkspaceLayer::WriteManagedLog(const Keire::ManagedLogLevel level, const std::string_view message) noexcept
{
    Keire::LogLevel nativeLevel = Keire::LogLevel::Info;
    Keire::UiColor color = m_Theme.Text;
    switch (level)
    {
    case Keire::ManagedLogLevel::Trace:
        nativeLevel = Keire::LogLevel::Trace;
        color = m_Theme.MutedText;
        break;
    case Keire::ManagedLogLevel::Debug:
        nativeLevel = Keire::LogLevel::Debug;
        color = m_Theme.MutedText;
        break;
    case Keire::ManagedLogLevel::Information:
        nativeLevel = Keire::LogLevel::Info;
        color = m_Theme.Text;
        break;
    case Keire::ManagedLogLevel::Warning:
        nativeLevel = Keire::LogLevel::Warn;
        color = m_Theme.Warning;
        break;
    case Keire::ManagedLogLevel::Error:
        nativeLevel = Keire::LogLevel::Error;
        color = m_Theme.Error;
        break;
    case Keire::ManagedLogLevel::Critical:
        nativeLevel = Keire::LogLevel::Critical;
        color = m_Theme.Error;
        break;
    }
    AddConsoleMessage("Script", std::string(message), color, nativeLevel);
}

float EditorWorkspaceLayer::ManagedDeltaTime() const noexcept
{
    return static_cast<float>(Owner().GetTime().DeltaTime().Seconds());
}

float EditorWorkspaceLayer::ManagedFixedDeltaTime() const noexcept
{
    return static_cast<float>(Owner().GetTime().FixedDeltaTime().Seconds());
}

float EditorWorkspaceLayer::ManagedUnscaledDeltaTime() const noexcept
{
    return static_cast<float>(Owner().GetTime().UnscaledDeltaTime().Seconds());
}

double EditorWorkspaceLayer::ManagedElapsedTime() const noexcept
{
    return Owner().GetTime().TimeSinceStartup().Seconds();
}

Keire::Vector2 EditorWorkspaceLayer::ReadManagedInput(const std::string_view action) noexcept
{
    try
    {
        if (!m_GameplayInputContext || !m_SceneDocument->PlaySession())
            return {};
        if (!m_GameplayInputContext->EnableMap("Player"))
            return {};
        if (!m_ManagedInputCaptureOverride)
            m_ManagedInputCaptureOverride.emplace(m_GameplayInputContext->OverrideUiCapture("Player"));
        if (action == "Look" && m_SuppressManagedLookFrames > 0)
        {
            --m_SuppressManagedLookFrames;
            return {};
        }
        const auto handle = m_GameplayInputContext->FindAction("Player", action);
        if (!handle)
            return {};
        const auto value = handle.Value().AsAxis2D();
        return {value.X, value.Y};
    }
    catch (...)
    {
        return {};
    }
}

Keire::ManagedInputState EditorWorkspaceLayer::ReadManagedInputState(const std::string_view action) noexcept
{
    try
    {
        if (!m_GameplayInputContext || !m_SceneDocument->PlaySession())
            return Keire::ManagedInputState::None;
        if (!m_GameplayInputContext->EnableMap("Player"))
            return Keire::ManagedInputState::None;
        if (!m_ManagedInputCaptureOverride)
            m_ManagedInputCaptureOverride.emplace(m_GameplayInputContext->OverrideUiCapture("Player"));
        const auto handle = m_GameplayInputContext->FindAction("Player", action);
        if (!handle)
            return Keire::ManagedInputState::None;
        auto state = Keire::ManagedInputState::None;
        if (handle.Value().Magnitude() >= 0.5F)
            state = state | Keire::ManagedInputState::Held;
        if (handle.WasStartedThisFrame() || handle.WasPerformedThisFrame())
            state = state | Keire::ManagedInputState::Pressed;
        if (handle.WasCanceledThisFrame())
            state = state | Keire::ManagedInputState::Released;
        return state;
    }
    catch (...)
    {
        return Keire::ManagedInputState::None;
    }
}

bool EditorWorkspaceLayer::PlayManagedAudio(const Keire::ManagedAudioPlayback& playback) noexcept
{
    try
    {
        const auto session =
            m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
        const auto scene = session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        auto sourceEntity = scene ? scene->FindEntity(Keire::EntityId(playback.Entity)) : Keire::Entity{};
        if (!sourceEntity || !presentation || !playback.Clip)
            return false;
        auto source = sourceEntity.GetComponent<Keire::AudioSourceComponent>();
        if (!source)
            source = sourceEntity.AddComponent<Keire::AudioSourceComponent>();
        (void)presentation->Stop(sourceEntity.Id());
        source->SetClip(playback.Clip);
        source->SetBus(playback.Bus);
        source->SetGain(playback.Gain);
        source->SetPitch(playback.Pitch);
        source->SetPriority(playback.Priority);
        source->SetLoop(playback.Loop);
        source->SetSpatial(playback.Spatial);
        if (playback.MaximumDistance > source->MinimumDistance())
        {
            source->SetMaximumDistance(playback.MaximumDistance);
            source->SetMinimumDistance(playback.MinimumDistance);
        }
        else
        {
            source->SetMinimumDistance(playback.MinimumDistance);
            source->SetMaximumDistance(playback.MaximumDistance);
        }
        source->SetPlayOnAwake(true);
        (void)presentation->Play(sourceEntity.Id());
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::StopManagedAudio(const Keire::AssetId entity) noexcept
{
    try
    {
        const auto session =
            m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
        const auto scene = session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        const auto sourceEntity = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
        if (!sourceEntity || !presentation)
            return false;
        if (const auto source = sourceEntity.GetComponent<Keire::AudioSourceComponent>())
            source->SetPlayOnAwake(false);
        return presentation->Stop(sourceEntity.Id());
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SetManagedUiText(const Keire::AssetId entity, const std::string_view text) noexcept
{
    try
    {
        const auto scene = m_SceneDocument ? m_SceneDocument->ActiveScene() : Keire::Ref<Keire::Scene>{};
        const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
        const auto component =
            target ? target.GetComponent<Keire::UiTextComponent>() : Keire::Ref<Keire::UiTextComponent>{};
        if (!component)
            return false;
        component->SetText(std::string(text));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ConsumeManagedUiClick(const Keire::AssetId entity) noexcept
{
    try
    {
        const auto session =
            m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        return presentation && presentation->ConsumeClick(Keire::EntityId(entity));
    }
    catch (...)
    {
        return false;
    }
}

void EditorWorkspaceLayer::ResetManagedPhysicsWorld() noexcept
{
    if (m_ManagedPhysicsWorld)
        m_ManagedPhysicsWorld->Close();
    m_ManagedPhysicsWorld.Reset();
    m_ManagedPhysicsScene.Reset();
    m_ManagedPhysicsEntities.clear();
    m_ManagedPhysicsObjectCount = 0;
}

void EditorWorkspaceLayer::RebuildManagedPhysicsWorld()
{
    ResetManagedPhysicsWorld();
    const auto physics = Owner().Physics();
    const auto scene = m_SceneDocument ? m_SceneDocument->ActiveScene() : Keire::Ref<Keire::Scene>{};
    if (!physics || !scene || !scene->IsOpen())
        return;

    m_ManagedPhysicsWorld = physics->CreateWorld();
    m_ManagedPhysicsScene = scene;
    m_ManagedPhysicsObjectCount = scene->ObjectCount();
    for (const auto& entity : scene->Query<Keire::ColliderComponent>())
    {
        const auto collider = entity.GetComponent<Keire::ColliderComponent>();
        const auto transform = entity.GetComponent<Keire::TransformComponent>();
        if (!collider || !transform)
            continue;
        const auto rigidBody = entity.GetComponent<Keire::RigidBodyComponent>();
        const auto scale = transform->LocalScale();
        const Keire::Vector3 absoluteScale{std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z)};
        Keire::PhysicsBodyDefinition definition;
        definition.Motion = rigidBody ? rigidBody->Motion() : Keire::PhysicsMotionType::Static;
        definition.Shape = collider->Shape();
        const auto worldPosition = transform->WorldPosition();
        const auto colliderCenter = collider->Center();
        definition.Position = {
            worldPosition.X + colliderCenter.X,
            worldPosition.Y + colliderCenter.Y,
            worldPosition.Z + colliderCenter.Z,
        };
        definition.Rotation = transform->LocalRotation();
        definition.LinearVelocity = rigidBody ? rigidBody->LinearVelocity() : Keire::Vector3{};
        definition.HalfExtent = {collider->HalfExtent().X * absoluteScale.X, collider->HalfExtent().Y * absoluteScale.Y,
                                 collider->HalfExtent().Z * absoluteScale.Z};
        definition.Radius = collider->Radius() * std::max({absoluteScale.X, absoluteScale.Y, absoluteScale.Z});
        definition.Height = collider->Height() * absoluteScale.Y;
        definition.Mass = rigidBody ? rigidBody->Mass() : 1.0F;
        definition.Layer = collider->Layer();
        definition.Mask = collider->Mask();
        definition.Trigger = collider->Trigger();
        definition.Continuous = rigidBody && rigidBody->Continuous();
        const auto body = m_ManagedPhysicsWorld->CreateBody(definition);
        m_ManagedPhysicsEntities.emplace_back(body, entity.Id().Value());
    }
}

std::optional<Keire::ManagedRaycastHit>
EditorWorkspaceLayer::RaycastManaged(const Keire::ManagedRaycastQuery& query) noexcept
{
    try
    {
        const auto scene = m_SceneDocument ? m_SceneDocument->ActiveScene() : Keire::Ref<Keire::Scene>{};
        if (!scene || !m_SceneDocument->PlaySession())
            return std::nullopt;
        if (!m_ManagedPhysicsWorld || m_ManagedPhysicsScene != scene ||
            m_ManagedPhysicsObjectCount != scene->ObjectCount())
        {
            RebuildManagedPhysicsWorld();
        }
        if (!m_ManagedPhysicsWorld)
            return std::nullopt;
        const auto hits = m_ManagedPhysicsWorld->RayCast({.Origin = query.Origin,
                                                          .Direction = query.Direction,
                                                          .MaximumDistance = query.MaximumDistance,
                                                          .Mask = query.Mask,
                                                          .IncludeTriggers = query.IncludeTriggers});
        for (const auto& hit : hits)
        {
            const auto mapping = std::ranges::find(m_ManagedPhysicsEntities, hit.Body,
                                                   &decltype(m_ManagedPhysicsEntities)::value_type::first);
            if (mapping == m_ManagedPhysicsEntities.end() || mapping->second == query.IgnoredEntity)
                continue;
            return Keire::ManagedRaycastHit{mapping->second, hit.Position, hit.Normal, hit.Distance};
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

void EditorWorkspaceLayer::ApplyManagedCursorMode() noexcept
{
    try
    {
        const auto windows = Owner().Windows();
        const auto window = Owner().MainWindow();
        if (!windows || !window)
            return;

        const auto mode = m_ManagedCursorLocked
                              ? Keire::CursorMode::RelativeLocked
                              : (m_ManagedCursorVisible ? Keire::CursorMode::Normal : Keire::CursorMode::Hidden);
        if (mode == Keire::CursorMode::RelativeLocked &&
            windows->GetCursorMode(window->Id()) != Keire::CursorMode::RelativeLocked)
        {
            const auto viewport = m_SceneViewportPanel->ViewportRect();
            if (viewport.Size().Width > 0.0F && viewport.Size().Height > 0.0F)
            {
                windows->WarpCursor(window->Id(),
                                    {static_cast<std::int32_t>((viewport.Minimum.X + viewport.Maximum.X) * 0.5F),
                                     static_cast<std::int32_t>((viewport.Minimum.Y + viewport.Maximum.Y) * 0.5F)});
                m_SuppressManagedLookFrames = 2;
            }
        }
        windows->SetCursorMode(window->Id(), mode);
    }
    catch (...)
    {
    }
}

void EditorWorkspaceLayer::SetManagedCursorVisible(const bool visible) noexcept
{
    m_ManagedCursorVisible = visible;
    ApplyManagedCursorMode();
}

void EditorWorkspaceLayer::SetManagedCursorLocked(const bool locked) noexcept
{
    m_ManagedCursorLocked = locked;
    ApplyManagedCursorMode();
}

bool EditorWorkspaceLayer::IsManagedCursorVisible() const noexcept { return m_ManagedCursorVisible; }

bool EditorWorkspaceLayer::IsManagedCursorLocked() const noexcept { return m_ManagedCursorLocked; }

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
