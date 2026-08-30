#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/ManagedRuntimeSessionResolver.h"

#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/BuildInfo.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/PlatformDirectories.h"
#include "KeireInternal/Scripting/ManagedRuntimePhysics.h"
#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"
#include "KeireInternal/WindowInternal.h"

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

void EditorWorkspaceLayer::RecordManagedProfileSpan(const std::string_view name, const double startMicroseconds,
                                                    const double durationMicroseconds) noexcept
{
    if (const auto profiler = Owner().GetProfiler())
        profiler->RecordSpan(Keire::ProfileCategory::Scripting, name, startMicroseconds, durationMicroseconds);
}

void EditorWorkspaceLayer::SetManagedProfileCounter(const std::string_view name, const double value) noexcept
{
    if (const auto profiler = Owner().GetProfiler())
        profiler->SetCounter(Keire::ProfileCategory::Scripting, name, value);
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

std::uint64_t EditorWorkspaceLayer::BeginManagedSceneLoad(const Keire::AssetId scene,
                                                          const Keire::SceneLoadMode mode) noexcept
{
    try
    {
        if (!m_PlayRuntimeWorld || !scene)
            return 0;
        const auto pending = std::ranges::find_if(m_ManagedSceneOperations,
                                                  [](const ManagedSceneOperation& operation)
                                                  {
                                                      const auto state = operation.Load->State();
                                                      return state == Keire::SceneLoadState::Queued ||
                                                             state == Keire::SceneLoadState::Loading;
                                                  });
        if (pending != m_ManagedSceneOperations.end())
            return 0;
        if (m_ManagedSceneOperations.size() >= 16)
            m_ManagedSceneOperations.erase(m_ManagedSceneOperations.begin());
        const auto operation = m_NextManagedSceneOperation++;
        if (m_NextManagedSceneOperation == 0)
            ++m_NextManagedSceneOperation;
        m_ManagedSceneOperations.push_back({operation, m_PlayRuntimeWorld->Load(scene, mode)});
        return operation;
    }
    catch (...)
    {
        return 0;
    }
}

std::optional<Keire::ManagedSceneLoadStatus>
EditorWorkspaceLayer::ManagedSceneLoad(const std::uint64_t operation) const noexcept
{
    try
    {
        const auto found = std::ranges::find(m_ManagedSceneOperations, operation, &ManagedSceneOperation::Id);
        if (found == m_ManagedSceneOperations.end())
            return std::nullopt;
        const auto state = found->Load->State();
        return Keire::ManagedSceneLoadStatus{found->Load->Asset(),
                                             found->Load->Mode(),
                                             state,
                                             found->Load->Progress(),
                                             found->Load->Diagnostic().Message,
                                             found->Load->Result()};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool EditorWorkspaceLayer::CancelManagedSceneLoad(const std::uint64_t operation) noexcept
{
    const auto found = std::ranges::find(m_ManagedSceneOperations, operation, &ManagedSceneOperation::Id);
    if (found == m_ManagedSceneOperations.end())
        return false;
    const auto state = found->Load->State();
    if (state != Keire::SceneLoadState::Queued && state != Keire::SceneLoadState::Loading)
        return false;
    found->Load->Cancel();
    return true;
}

bool EditorWorkspaceLayer::UnloadManagedScene(const Keire::SceneHandle scene) noexcept
{
    try
    {
        return m_PlayRuntimeWorld && m_PlayRuntimeWorld->Unload(scene);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SetActiveManagedScene(const Keire::SceneHandle scene) noexcept
{
    try
    {
        return m_PlayRuntimeWorld && m_PlayRuntimeWorld->SetActive(scene);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::MakeManagedEntityPersistent(const Keire::ManagedEntityHandle entity) noexcept
{
    try
    {
        const auto scene =
            m_PlayRuntimeWorld ? m_PlayRuntimeWorld->FindWorld(entity.World) : Keire::Ref<Keire::Scene>{};
        const auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
        return target && m_PlayRuntimeWorld->MakePersistent(target);
    }
    catch (...)
    {
        return false;
    }
}

Keire::ManagedSceneHandle EditorWorkspaceLayer::ActiveManagedSceneHandle() const noexcept
{
    if (!m_PlayRuntimeWorld)
        return {};
    const auto active = m_PlayRuntimeWorld->Active();
    return {m_PlayRuntimeWorld->Asset(active), active};
}

Keire::AssetId EditorWorkspaceLayer::ActiveManagedScene() const noexcept { return ActiveManagedSceneHandle().Scene; }

std::vector<Keire::ManagedSceneHandle> EditorWorkspaceLayer::LoadedManagedSceneHandles() const
{
    std::vector<Keire::ManagedSceneHandle> result;
    if (!m_PlayRuntimeWorld)
        return result;
    for (const auto handle : m_PlayRuntimeWorld->LoadedScenes())
        result.push_back({m_PlayRuntimeWorld->Asset(handle), handle});
    return result;
}

std::vector<Keire::AssetId> EditorWorkspaceLayer::LoadedManagedScenes() const
{
    std::vector<Keire::AssetId> result;
    for (const auto scene : LoadedManagedSceneHandles())
        result.push_back(scene.Scene);
    return result;
}

std::vector<std::string> EditorWorkspaceLayer::ManagedEntityTags(const Keire::ManagedEntityHandle entity) const
{
    const auto scene = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->FindWorld(entity.World) : Keire::Ref<Keire::Scene>{};
    const auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
    return target && target.World() == entity.World ? target.Tags() : std::vector<std::string>{};
}

bool EditorWorkspaceLayer::AddManagedEntityTag(const Keire::ManagedEntityHandle entity,
                                               const std::string_view tag) noexcept
{
    try
    {
        const auto scene =
            m_PlayRuntimeWorld ? m_PlayRuntimeWorld->FindWorld(entity.World) : Keire::Ref<Keire::Scene>{};
        auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
        return target && target.World() == entity.World && target.AddTag(std::string(tag));
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::RemoveManagedEntityTag(const Keire::ManagedEntityHandle entity,
                                                  const std::string_view tag) noexcept
{
    try
    {
        const auto scene =
            m_PlayRuntimeWorld ? m_PlayRuntimeWorld->FindWorld(entity.World) : Keire::Ref<Keire::Scene>{};
        auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
        return target && target.World() == entity.World && target.RemoveTag(tag);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ClearManagedEntityTags(const Keire::ManagedEntityHandle entity) noexcept
{
    try
    {
        const auto scene =
            m_PlayRuntimeWorld ? m_PlayRuntimeWorld->FindWorld(entity.World) : Keire::Ref<Keire::Scene>{};
        auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
        if (!target || target.World() != entity.World)
            return false;
        target.SetTags({});
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::vector<Keire::ManagedEntityHandle>
EditorWorkspaceLayer::QueryManagedEntityNamesScoped(const std::string_view name, const Keire::ManagedSceneQuery query,
                                                    const std::size_t maximum) const
{
    const auto entities = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->QueryName(name, query.Scope, query.Scene, maximum)
                                             : std::vector<Keire::Entity>{};
    std::vector<Keire::ManagedEntityHandle> result;
    result.reserve(std::min(entities.size(), maximum));
    for (const auto& entity : entities)
    {
        if (result.size() == maximum)
            break;
        result.push_back({entity.World(), entity.Id().Value()});
    }
    return result;
}

std::vector<Keire::ManagedEntityHandle> EditorWorkspaceLayer::QueryManagedEntityNames(const std::string_view name,
                                                                                      const std::size_t maximum) const
{
    return QueryManagedEntityNamesScoped(name, {}, maximum);
}

std::vector<Keire::ManagedEntityHandle>
EditorWorkspaceLayer::QueryManagedEntityTagsScoped(const std::string_view tag, const Keire::ManagedSceneQuery query,
                                                   const std::size_t maximum) const
{
    const auto entities = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->QueryTag(tag, query.Scope, query.Scene, maximum)
                                             : std::vector<Keire::Entity>{};
    std::vector<Keire::ManagedEntityHandle> result;
    result.reserve(std::min(entities.size(), maximum));
    for (const auto& entity : entities)
    {
        if (result.size() == maximum)
            break;
        result.push_back({entity.World(), entity.Id().Value()});
    }
    return result;
}

std::vector<Keire::ManagedEntityHandle> EditorWorkspaceLayer::QueryManagedEntityTags(const std::string_view tag,
                                                                                     const std::size_t maximum) const
{
    return QueryManagedEntityTagsScoped(tag, {}, maximum);
}

std::vector<Keire::ManagedEntityHandle> EditorWorkspaceLayer::QueryManagedEntityComponentsScoped(
    const Keire::ComponentTypeId component, const Keire::ManagedSceneQuery query, const std::size_t maximum) const
{
    const auto entities = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->Query(component, query.Scope, query.Scene, maximum)
                                             : std::vector<Keire::Entity>{};
    std::vector<Keire::ManagedEntityHandle> result;
    result.reserve(std::min(entities.size(), maximum));
    for (const auto& entity : entities)
    {
        if (result.size() == maximum)
            break;
        result.push_back({entity.World(), entity.Id().Value()});
    }
    return result;
}

std::vector<Keire::ManagedEntityHandle>
EditorWorkspaceLayer::QueryManagedEntityComponents(const Keire::ComponentTypeId component,
                                                   const std::size_t maximum) const
{
    return QueryManagedEntityComponentsScoped(component, {}, maximum);
}

std::optional<Keire::RenderEnvironmentSettings> EditorWorkspaceLayer::ManagedRenderEnvironment() const noexcept
{
    if (m_ManagedRenderEnvironmentOverride)
        return m_ManagedRenderEnvironmentOverride;
    return m_ProjectSettingsDocument ? std::optional(m_ProjectSettingsDocument->Settings()) : std::nullopt;
}

bool EditorWorkspaceLayer::SetManagedRenderEnvironment(Keire::RenderEnvironmentSettings settings) noexcept
{
    try
    {
        if (!m_SceneDocument || !m_SceneDocument->PlaySession())
            return false;
        Keire::ValidateRenderEnvironmentSettings(settings);
        m_ManagedRenderEnvironmentOverride = settings;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ManagedMaterialParameterCollectionReady(const Keire::AssetId collection) noexcept
{
    try
    {
        return m_SceneDocument && m_SceneDocument->PlaySession() &&
               m_ManagedMaterialParameters.Ready(Owner().Assets(), collection);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SetManagedMaterialParameter(const Keire::AssetId collection, const std::string_view name,
                                                       Keire::MaterialPropertyValue value) noexcept
{
    try
    {
        return m_SceneDocument && m_SceneDocument->PlaySession() &&
               m_ManagedMaterialParameters.Set(Owner().Assets(), collection, name, value);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ResetManagedMaterialParameter(const Keire::AssetId collection,
                                                         const std::string_view name) noexcept
{
    try
    {
        return m_SceneDocument && m_SceneDocument->PlaySession() &&
               m_ManagedMaterialParameters.Reset(Owner().Assets(), collection, name);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ClearManagedMaterialParameters(const Keire::AssetId collection) noexcept
{
    try
    {
        return m_SceneDocument && m_SceneDocument->PlaySession() &&
               m_ManagedMaterialParameters.Clear(Owner().Assets(), collection);
    }
    catch (...)
    {
        return false;
    }
}

Keire::ManagedApplicationInfo EditorWorkspaceLayer::ManagedApplication() const
{
    const auto& specification = Owner().Specification();
    const auto& windowing = specification.Windowing;
    const auto identifier =
        windowing.ApplicationIdentifier.empty() ? std::string("keire.project") : windowing.ApplicationIdentifier;
    return {.ProductName =
                windowing.ApplicationName.empty() ? specification.MainWindow.Title : windowing.ApplicationName,
            .Version = windowing.ApplicationVersion.empty() ? std::string(Keire::GetBuildInfo().Version)
                                                            : windowing.ApplicationVersion,
            .Identifier = identifier,
            .PersistentDataPath = Keire::GetPreferenceDirectory() / "Applications" / identifier,
            .IsEditor = true};
}

void EditorWorkspaceLayer::RequestManagedExit(const int) noexcept
{
    try
    {
        RequestStopPlayMode();
    }
    catch (...)
    {
    }
}

double EditorWorkspaceLayer::ManagedTimeScale() const noexcept { return Owner().GetTime().TimeScale(); }

bool EditorWorkspaceLayer::SetManagedTimeScale(const double scale) noexcept
{
    try
    {
        Owner().GetTime().SetTimeScale(scale);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ManagedTimePaused() const noexcept { return Owner().GetTime().Paused(); }

bool EditorWorkspaceLayer::SetManagedTimePaused(const bool paused) noexcept
{
    Owner().GetTime().SetPaused(paused);
    return true;
}

Keire::ManagedScreenState EditorWorkspaceLayer::ManagedScreen() const noexcept
{
    try
    {
        const auto window = Owner().MainWindow();
        if (!window)
            return {};
        const auto logical = window->LogicalSize();
        const auto pixels = window->PixelSize();
        return {.LogicalWidth = logical.Width,
                .LogicalHeight = logical.Height,
                .PixelWidth = pixels.Width,
                .PixelHeight = pixels.Height,
                .DisplayScale = window->DisplayScale(),
                .Mode = window->Mode() == Keire::WindowMode::BorderlessFullscreen
                            ? Keire::ManagedScreenMode::BorderlessFullscreen
                            : Keire::ManagedScreenMode::Windowed,
                .Focused = window->Focused(),
                .Visible = window->Visible(),
                .Minimized = window->Minimized(),
                .VSync = Owner().Specification().Render.PresentMode == Keire::RenderPresentMode::VSync};
    }
    catch (...)
    {
        return {};
    }
}

bool EditorWorkspaceLayer::SetManagedScreen(const std::uint32_t width, const std::uint32_t height,
                                            const Keire::ManagedScreenMode mode) noexcept
{
    if (width < 64 || height < 64 || width > 16384 || height > 16384)
        return false;
    try
    {
        const auto window = Owner().MainWindow();
        if (!window)
            return false;
        const auto previousMode = window->Mode();
        const auto previousSize = window->LogicalSize();
        const auto requestedMode = mode == Keire::ManagedScreenMode::BorderlessFullscreen
                                       ? Keire::WindowMode::BorderlessFullscreen
                                       : Keire::WindowMode::Windowed;
        try
        {
            if (window->Mode() != requestedMode)
                window->SetMode(requestedMode);
            if (requestedMode == Keire::WindowMode::Windowed)
                window->SetSize({width, height});
            return true;
        }
        catch (...)
        {
            try
            {
                if (window->Mode() != previousMode)
                    window->SetMode(previousMode);
                if (previousMode == Keire::WindowMode::Windowed)
                    window->SetSize(previousSize);
            }
            catch (...)
            {
            }
            return false;
        }
    }
    catch (...)
    {
        return false;
    }
}

Keire::Vector2 EditorWorkspaceLayer::ReadManagedInput(const std::string_view action) noexcept
{
    try
    {
        if (!m_GameplayInputContext || !m_SceneDocument->PlaySession() || !m_GameViewportInputActive)
            return {};
        if (!m_GameplayInputMap || !m_GameplayInputContext->MapEnabled(m_GameplayInputMap))
            return {};
        if (!m_ManagedInputCaptureOverride)
            m_ManagedInputCaptureOverride.emplace(m_GameplayInputContext->OverrideUiCapture(m_GameplayInputMap));
        if (action == "Look" && m_SuppressManagedLookFrames > 0)
        {
            --m_SuppressManagedLookFrames;
            return {};
        }
        const auto definition = m_GameplayInputContext->Definition();
        const auto map =
            std::ranges::find(definition.ActionMaps, m_GameplayInputMap, &Keire::InputActionMapDefinition::Id);
        if (map == definition.ActionMaps.end())
            return {};
        const auto found = std::ranges::find(map->Actions, action, &Keire::InputActionDefinition::Name);
        const auto handle =
            found != map->Actions.end() ? m_GameplayInputContext->FindAction(found->Id) : Keire::InputActionHandle{};
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
        if (!m_GameplayInputContext || !m_SceneDocument->PlaySession() || !m_GameViewportInputActive)
            return Keire::ManagedInputState::None;
        if (!m_GameplayInputMap || !m_GameplayInputContext->MapEnabled(m_GameplayInputMap))
            return Keire::ManagedInputState::None;
        if (!m_ManagedInputCaptureOverride)
            m_ManagedInputCaptureOverride.emplace(m_GameplayInputContext->OverrideUiCapture(m_GameplayInputMap));
        const auto definition = m_GameplayInputContext->Definition();
        const auto map =
            std::ranges::find(definition.ActionMaps, m_GameplayInputMap, &Keire::InputActionMapDefinition::Id);
        if (map == definition.ActionMaps.end())
            return Keire::ManagedInputState::None;
        const auto found = std::ranges::find(map->Actions, action, &Keire::InputActionDefinition::Name);
        const auto handle =
            found != map->Actions.end() ? m_GameplayInputContext->FindAction(found->Id) : Keire::InputActionHandle{};
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

std::vector<Keire::ManagedInputDevice> EditorWorkspaceLayer::ManagedInputDevices() const
{
    const auto input = Owner().Input();
    if (!input)
        return {};
    std::vector<Keire::ManagedInputDevice> result;
    for (const auto& device : input->Devices())
    {
        result.push_back({device.Id.Value(), static_cast<Keire::ManagedInputDeviceType>(device.Type), device.Name,
                          device.Connected, device.Paired});
    }
    return result;
}

std::string EditorWorkspaceLayer::ManagedInputControlScheme() const
{
    const auto input = Owner().Input();
    if (!input || !m_EditorInputUser)
        return {};
    const auto users = input->Users();
    const auto found = std::ranges::find(users, m_EditorInputUser, &Keire::InputUserDescriptor::Id);
    return found == users.end() ? std::string{} : found->ControlScheme;
}

bool EditorWorkspaceLayer::SetManagedInputControlScheme(const std::string_view scheme, const bool locked) noexcept
{
    try
    {
        const auto input = Owner().Input();
        return input && m_EditorInputUser && input->SetControlScheme(m_EditorInputUser, std::string(scheme), locked);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::ClearManagedInputControlSchemeLock() noexcept
{
    try
    {
        const auto input = Owner().Input();
        return input && m_EditorInputUser && input->ClearControlSchemeLock(m_EditorInputUser);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SetManagedGamepadRumble(const std::uint32_t device, const float lowFrequency,
                                                   const float highFrequency, const float durationSeconds) noexcept
{
    try
    {
        const auto input = Owner().Input();
        if (!input || !m_EditorInputUser)
            return false;
        const auto users = input->Users();
        const auto user = std::ranges::find(users, m_EditorInputUser, &Keire::InputUserDescriptor::Id);
        const auto id = Keire::InputDeviceId(device);
        if (user == users.end() || std::ranges::find(user->Devices, id) == user->Devices.end())
            return false;
        return input->SetGamepadRumble(id, lowFrequency, highFrequency, Keire::TimeStep::FromSeconds(durationSeconds));
    }
    catch (...)
    {
        return false;
    }
}

std::uint64_t EditorWorkspaceLayer::BeginManagedInputRebind(const Keire::AssetId binding,
                                                            const Keire::ManagedInputRebindOptions options) noexcept
{
    return m_ManagedInputOperations.Begin(Owner().Input(), m_GameplayInputContext, binding, options);
}

std::optional<Keire::ManagedInputRebindSnapshot>
EditorWorkspaceLayer::ManagedInputRebind(const std::uint64_t operation) const noexcept
{
    return m_ManagedInputOperations.Status(operation);
}

bool EditorWorkspaceLayer::ResolveManagedInputRebind(const std::uint64_t operation,
                                                     const Keire::ManagedInputRebindResolution resolution) noexcept
{
    return m_ManagedInputOperations.Resolve(operation, resolution);
}

bool EditorWorkspaceLayer::CancelManagedInputRebind(const std::uint64_t operation) noexcept
{
    return m_ManagedInputOperations.Cancel(operation);
}

bool EditorWorkspaceLayer::SaveManagedInputBindings(const std::string_view profile) noexcept
{
    try
    {
        if (!m_GameplayInputContext)
            return false;
        m_GameplayInputContext->SaveBindingOverrides(profile);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int EditorWorkspaceLayer::LoadManagedInputBindings(const std::string_view profile) noexcept
{
    try
    {
        return m_GameplayInputContext ? static_cast<int>(m_GameplayInputContext->LoadBindingOverrides(profile)) : -1;
    }
    catch (...)
    {
        return -1;
    }
}

bool EditorWorkspaceLayer::ClearManagedInputBindings() noexcept
{
    try
    {
        if (!m_GameplayInputContext)
            return false;
        m_GameplayInputContext->ClearBindingOverrides();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::uint64_t EditorWorkspaceLayer::CreateManagedInputContext(const std::uint64_t generation,
                                                              const Keire::AssetId asset) noexcept
{
    if (!m_SceneDocument->PlaySession())
        return 0;
    return m_ManagedInputContexts.Create(generation, Owner().Input(), m_EditorInputUser, asset);
}

bool EditorWorkspaceLayer::ReleaseManagedInputContext(const std::uint64_t handle) noexcept
{
    return m_ManagedInputContexts.Release(handle);
}

void EditorWorkspaceLayer::ReleaseManagedInputContexts(const std::uint64_t generation) noexcept
{
    m_ManagedInputContexts.ReleaseGeneration(generation);
}

bool EditorWorkspaceLayer::OperateManagedInputContext(const std::uint64_t handle,
                                                      const Keire::ManagedInputContextOperation operation,
                                                      const Keire::AssetId target) noexcept
{
    return m_ManagedInputContexts.Operate(handle, operation, target);
}

std::uint64_t
EditorWorkspaceLayer::BeginManagedInputContextRebind(const std::uint64_t handle, const Keire::AssetId binding,
                                                     const Keire::ManagedInputRebindOptions options) noexcept
{
    const auto context = m_ManagedInputContexts.Context(handle);
    return context && m_SceneDocument->PlaySession()
               ? m_ManagedInputOperations.Begin(Owner().Input(), context, binding, options)
               : 0;
}

Keire::AssetId EditorWorkspaceLayer::FindManagedInputMap(const std::uint64_t handle,
                                                         const std::string_view name) noexcept
{
    return m_ManagedInputContexts.FindMap(handle, name);
}

Keire::AssetId EditorWorkspaceLayer::FindManagedInputAction(const std::uint64_t handle, const Keire::AssetId map,
                                                            const std::string_view name) noexcept
{
    return m_ManagedInputContexts.FindAction(handle, map, name);
}

std::optional<Keire::ManagedInputActionSnapshot>
EditorWorkspaceLayer::ManagedInputAction(const std::uint64_t handle, const Keire::AssetId action) noexcept
{
    if (!m_SceneDocument->PlaySession() || !m_GameViewportInputActive)
        return std::nullopt;
    return m_ManagedInputContexts.Action(handle, action);
}

std::optional<Keire::InputDeviceId>
EditorWorkspaceLayer::CurrentManagedInputDevice(const Keire::InputDeviceType type) noexcept
{
    const auto input = Owner().Input();
    return input && m_SceneDocument->PlaySession() && m_GameViewportInputActive ? input->CurrentDevice(type)
                                                                                : std::nullopt;
}

std::optional<Keire::InputControlSnapshot>
EditorWorkspaceLayer::ManagedInputControl(const Keire::InputDeviceId device, const std::string_view path) noexcept
{
    const auto input = Owner().Input();
    return input && m_SceneDocument->PlaySession() && m_GameViewportInputActive ? input->ReadControl(device, path)
                                                                                : std::nullopt;
}

bool EditorWorkspaceLayer::PlayManagedAudio(const Keire::ManagedAudioPlayback& playback) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(playback.Entity);
        const auto scene = session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        auto sourceEntity = scene ? scene->FindEntity(Keire::EntityId(playback.Entity)) : Keire::Entity{};
        if (!sourceEntity || !presentation || !playback.Clip)
            return false;
        Keire::AudioSourceComponentState candidate{
            .Clip = playback.Clip,
            .Mixer = playback.Mixer,
            .BusId = playback.BusId,
            .Bus = playback.Bus,
            .Gain = playback.Gain,
            .Pitch = playback.Pitch,
            .Priority = playback.Priority,
            .MinimumDistance = playback.MinimumDistance,
            .MaximumDistance = playback.MaximumDistance,
            .Attenuation = playback.Attenuation,
            .Loop = playback.Loop,
            .Spatial = playback.Spatial,
            .PlayOnAwake = true,
        };
        Keire::AudioSourceComponent::ValidateState(candidate);

        auto source = sourceEntity.GetComponent<Keire::AudioSourceComponent>();
        if (!source)
            source = sourceEntity.AddComponent<Keire::AudioSourceComponent>();
        source->ApplyState(std::move(candidate));

        // Commit the component before replacing its voice. A newly added source may not be tracked until the next
        // presentation synchronization, so PlayOnAwake remains the reliable deferred-play fallback.
        (void)presentation->Stop(sourceEntity.Id());
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
        const auto session = ManagedRuntimeSession(entity);
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

bool EditorWorkspaceLayer::PauseManagedAudio(const Keire::AssetId entity, const bool paused) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        return presentation &&
               (paused ? presentation->Pause(Keire::EntityId(entity)) : presentation->Resume(Keire::EntityId(entity)));
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SeekManagedAudio(const Keire::AssetId entity, const float positionSeconds) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        return presentation && presentation->Seek(Keire::EntityId(entity), positionSeconds);
    }
    catch (...)
    {
        return false;
    }
}

Keire::ManagedAudioSourceStatus EditorWorkspaceLayer::ManagedAudioStatus(const Keire::AssetId entity) const noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        if (!presentation)
            return {};
        const auto state = presentation->Playback(Keire::EntityId(entity));
        return {static_cast<Keire::ManagedAudioPlaybackState>(state.State), state.PositionSeconds,
                state.DurationSeconds};
    }
    catch (...)
    {
        return {};
    }
}

bool EditorWorkspaceLayer::PlayManagedVfx(const Keire::AssetId entity, const Keire::AssetId effect,
                                          const bool restart) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->PlayVfx(Keire::EntityId(entity), effect, restart);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::StopManagedVfx(const Keire::AssetId entity) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->StopVfx(Keire::EntityId(entity));
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::PauseManagedVfx(const Keire::AssetId entity, const bool paused) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->PauseVfx(Keire::EntityId(entity), paused);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::IsManagedVfxAlive(const Keire::AssetId entity) const noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->IsVfxAlive(Keire::EntityId(entity));
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SendManagedVfxEvent(const Keire::AssetId entity, const std::string_view eventName,
                                               const std::uint32_t spawnCount) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->SendVfxEvent(Keire::EntityId(entity), eventName, spawnCount);
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::SetManagedVfxParameter(const Keire::AssetId entity,
                                                  const Keire::VfxParameterOverride& value) noexcept
{
    try
    {
        const auto session = ManagedRuntimeSession(entity);
        return session && session->SetVfxParameter(Keire::EntityId(entity), value);
    }
    catch (...)
    {
        return false;
    }
}

Keire::Ref<Keire::SceneRuntimeSession>
EditorWorkspaceLayer::ManagedRuntimeSession(const Keire::AssetId entity) const noexcept
{
    return KeireEditor::ResolveManagedRuntimeSession(
        m_PlayRuntimeWorld, m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{},
        entity);
}

Keire::Ref<Keire::Scene> EditorWorkspaceLayer::ManagedRuntimeScene(const Keire::AssetId entity) const noexcept
{
    const auto session = ManagedRuntimeSession(entity);
    return session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
}

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::ManagedRuntimeAssets() const noexcept { return Owner().Assets(); }

std::optional<Keire::ManagedRaycastHit>
EditorWorkspaceLayer::RaycastManaged(const Keire::ManagedRaycastQuery& query) noexcept
{
    try
    {
        const auto play = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->SessionForWorld(query.World)
                                             : Keire::Ref<Keire::SceneRuntimeSession>{};
        if (!play)
            return std::nullopt;
        const auto hits = play->RayCast({.Origin = query.Origin,
                                         .Direction = query.Direction,
                                         .MaximumDistance = query.MaximumDistance,
                                         .Mask = query.Mask,
                                         .IncludeTriggers = query.IncludeTriggers},
                                        Keire::EntityId(query.IgnoredEntity));
        if (!hits.empty())
        {
            const auto& hit = hits.front();
            return Keire::ManagedRaycastHit{hit.Entity.Value(), hit.Hit.Position, hit.Hit.Normal, hit.Hit.Distance};
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<Keire::ManagedRaycastHit>
EditorWorkspaceLayer::CapsuleCastManaged(const Keire::ManagedCapsuleCastQuery& query) noexcept
{
    const auto play = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->SessionForWorld(query.World)
                                         : Keire::Ref<Keire::SceneRuntimeSession>{};
    return Keire::Detail::QueryManagedCapsule(play, query);
}

std::vector<Keire::AssetId> EditorWorkspaceLayer::OverlapSphereManaged(const Keire::ManagedSphereOverlapQuery& query)
{
    const auto play = m_PlayRuntimeWorld ? m_PlayRuntimeWorld->SessionForWorld(query.World)
                                         : Keire::Ref<Keire::SceneRuntimeSession>{};
    return Keire::Detail::QueryManagedSphereOverlap(play, query);
}

void EditorWorkspaceLayer::ApplyManagedCursorMode() noexcept
{
    try
    {
        const auto windows = Owner().Windows();
        const auto window = Owner().MainWindow();
        if (!windows || !window)
            return;

        const bool playActive = m_SceneDocument && m_SceneDocument->PlaySession() &&
                                m_SceneDocument->PlaySession()->State() != Keire::ScenePlayState::Stopped;
        const bool runtimeInputActive = playActive && m_GameViewportInputActive;
        const auto mode = runtimeInputActive
                              ? (m_ManagedCursorLocked
                                     ? Keire::CursorMode::RelativeLocked
                                     : (m_ManagedCursorVisible ? Keire::CursorMode::Normal : Keire::CursorMode::Hidden))
                              : Keire::CursorMode::Normal;
        const auto previousMode = windows->GetCursorMode(window->Id());
        auto viewport = playActive ? m_GameViewportRect : m_SceneViewportPanel->ViewportRect();
        if (viewport.Size().Width <= 0.0F || viewport.Size().Height <= 0.0F)
            viewport = playActive ? m_SceneViewportPanel->ViewportRect() : m_GameViewportRect;
        const auto centerCursor = [&]
        {
            if (viewport.Size().Width > 0.0F && viewport.Size().Height > 0.0F)
            {
                windows->WarpCursor(window->Id(),
                                    {static_cast<std::int32_t>((viewport.Minimum.X + viewport.Maximum.X) * 0.5F),
                                     static_cast<std::int32_t>((viewport.Minimum.Y + viewport.Maximum.Y) * 0.5F)});
                m_SuppressManagedLookFrames = 2;
            }
        };
        if (mode == Keire::CursorMode::RelativeLocked && previousMode != Keire::CursorMode::RelativeLocked)
            centerCursor();
        windows->SetCursorMode(window->Id(), mode);
    }
    catch (...)
    {
    }
}

void EditorWorkspaceLayer::SetGameViewportInputActive(const bool active) noexcept
{
    if (!active)
        KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
    if (m_GameViewportInputActive == active)
        return;
    m_GameViewportInputActive = active;
    if (!active)
    {
        try
        {
            if (const auto windows = Owner().Windows(); windows && Owner().MainWindow())
                (void)Keire::WindowSystemInternalAccess::SetTextInput(*windows, Owner().MainWindow()->Id(), false);
        }
        catch (...)
        {
        }
    }
    ApplyManagedCursorMode();
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
