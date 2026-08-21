#include "KeireRuntimeInternal/ManagedWorldRuntime.h"

#include "Keire/Application.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "KeireInternal/Scripting/ManagedRuntimePhysics.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireRuntime
{
    namespace
    {
        struct ManagedSceneTransition final
        {
            std::uint64_t Operation = 0;
            Keire::Ref<Keire::SceneLoadOperation> Load;
            std::string Diagnostic;
            bool Activated = false;
            bool ActivationFailed = false;
        };

        constexpr std::size_t MaximumManagedSceneTransitions = 16;
    } // namespace

    class ManagedWorldRuntime::Impl final
    {
      public:
        std::vector<ManagedSceneTransition> Transitions;
        std::uint64_t NextOperation = 1;
    };

    ManagedWorldRuntime::ManagedWorldRuntime() : m_Impl(std::make_unique<Impl>()) {}

    ManagedWorldRuntime::~ManagedWorldRuntime() { CancelAll(); }

    std::uint64_t ManagedWorldRuntime::Begin(const Keire::Ref<Keire::SceneSystem>& scenes, const Keire::AssetId scene,
                                             const Keire::SceneLoadMode mode, const bool allowed) noexcept
    {
        try
        {
            if (!allowed || !scenes || !scene || mode != Keire::SceneLoadMode::Single)
                return 0;
            const auto pending = std::ranges::find_if(m_Impl->Transitions,
                                                      [](const ManagedSceneTransition& transition)
                                                      {
                                                          const auto state = transition.Load->State();
                                                          return !transition.Activated &&
                                                                 !transition.ActivationFailed &&
                                                                 state != Keire::SceneLoadState::Failed &&
                                                                 state != Keire::SceneLoadState::Cancelled;
                                                      });
            if (pending != m_Impl->Transitions.end())
                return 0;
            if (m_Impl->Transitions.size() >= MaximumManagedSceneTransitions)
            {
                const auto reclaim = std::ranges::find_if(m_Impl->Transitions,
                                                          [](const ManagedSceneTransition& transition)
                                                          {
                                                              const auto state = transition.Load->State();
                                                              return transition.Activated ||
                                                                     transition.ActivationFailed ||
                                                                     state == Keire::SceneLoadState::Failed ||
                                                                     state == Keire::SceneLoadState::Cancelled;
                                                          });
                if (reclaim == m_Impl->Transitions.end())
                    return 0;
                m_Impl->Transitions.erase(reclaim);
            }
            const auto operation = m_Impl->NextOperation++;
            if (m_Impl->NextOperation == 0)
                ++m_Impl->NextOperation;
            m_Impl->Transitions.push_back({operation, scenes->Load(scene, mode)});
            return operation;
        }
        catch (...)
        {
            return 0;
        }
    }

    std::optional<Keire::ManagedSceneLoadStatus>
    ManagedWorldRuntime::Status(const std::uint64_t operation) const noexcept
    {
        try
        {
            const auto found = std::ranges::find(m_Impl->Transitions, operation, &ManagedSceneTransition::Operation);
            if (found == m_Impl->Transitions.end())
                return std::nullopt;
            auto state = found->Load->State();
            std::string diagnostic;
            float progress = 0.0F;
            if (found->ActivationFailed)
            {
                state = Keire::SceneLoadState::Failed;
                diagnostic = found->Diagnostic;
                progress = 1.0F;
            }
            else if (state == Keire::SceneLoadState::Ready && !found->Activated)
            {
                state = Keire::SceneLoadState::Loading;
                progress = 0.9F;
            }
            else
            {
                progress = state == Keire::SceneLoadState::Queued
                               ? 0.0F
                               : (state == Keire::SceneLoadState::Loading ? 0.5F : 1.0F);
                if (state == Keire::SceneLoadState::Failed)
                    diagnostic = found->Load->Diagnostic().Message;
            }
            return Keire::ManagedSceneLoadStatus{found->Load->Asset(), found->Load->Mode(), state, progress,
                                                 std::move(diagnostic)};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ManagedWorldRuntime::Cancel(const std::uint64_t operation) noexcept
    {
        const auto found = std::ranges::find(m_Impl->Transitions, operation, &ManagedSceneTransition::Operation);
        if (found == m_Impl->Transitions.end() || found->Activated || found->ActivationFailed)
            return false;
        const auto state = found->Load->State();
        if (state != Keire::SceneLoadState::Queued && state != Keire::SceneLoadState::Loading)
            return false;
        found->Load->Cancel();
        return true;
    }

    void ManagedWorldRuntime::CancelAll() noexcept
    {
        for (const auto& transition : m_Impl->Transitions)
            transition.Load->Cancel();
        m_Impl->Transitions.clear();
    }

    std::optional<ManagedWorldActivation> ManagedWorldRuntime::Process(
        const Keire::Ref<Keire::AssetSystem>& assets, const Keire::Ref<Keire::AudioSystem>& audio,
        const Keire::Ref<Keire::PhysicsSystem>& physics, const Keire::AssetId defaultMixer, const bool deterministic,
        Keire::AssetId& activatingScene, const ManagedSceneValidator validator) noexcept
    {
        const auto found = std::ranges::find_if(m_Impl->Transitions,
                                                [](const ManagedSceneTransition& transition)
                                                {
                                                    return !transition.Activated && !transition.ActivationFailed &&
                                                           transition.Load->State() == Keire::SceneLoadState::Ready;
                                                });
        if (found == m_Impl->Transitions.end())
            return std::nullopt;
        struct ActivationScope final
        {
            explicit ActivationScope(Keire::AssetId& current, const Keire::AssetId scene) noexcept : Current(current)
            {
                Current = scene;
            }
            ~ActivationScope() { Current = {}; }
            Keire::AssetId& Current;
        } activationScope(activatingScene, found->Load->Asset());
        try
        {
            auto replacement =
                Keire::CreateRef<Keire::SceneRuntimeSession>(found->Load->Result(), assets, audio, physics);
            if (const auto presentation = replacement->Presentation())
                presentation->SetDefaultMixer(defaultMixer);
            replacement->SetDeterministicSimulation(deterministic);
            replacement->Play();
            if (replacement->State() == Keire::ScenePlayState::Faulted)
                throw std::runtime_error("Scene Play failed: " + replacement->Diagnostic().Message);
            const auto scene = replacement->RuntimeScene();
            if (!validator || !validator(scene))
                throw std::runtime_error("The loaded scene has no active camera.");
            found->Activated = true;
            return ManagedWorldActivation{std::move(replacement), scene, replacement->Presentation()};
        }
        catch (const std::exception& error)
        {
            found->Diagnostic = error.what();
            found->ActivationFailed = true;
        }
        catch (...)
        {
            found->Diagnostic = "The loaded scene failed during runtime activation.";
            found->ActivationFailed = true;
        }
        return std::nullopt;
    }

    ManagedWorldRuntimeServices::ManagedWorldRuntimeServices(const bool editor,
                                                             Keire::RenderEnvironmentSettings rendering)
        : ManagedRuntimeApplicationServices(editor), m_Rendering(std::move(rendering))
    {
    }

    ManagedWorldRuntimeServices::~ManagedWorldRuntimeServices() { UnbindManagedWorld(); }

    void ManagedWorldRuntimeServices::BindManagedWorld(Keire::Application& application,
                                                       Keire::Ref<Keire::SceneRuntimeSession>& runtime,
                                                       Keire::Ref<Keire::Scene>& scene,
                                                       Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                                       Keire::Ref<Keire::SceneRuntimeSession>& replaySession,
                                                       bool& replayStarted, const Keire::AssetId defaultMixer) noexcept
    {
        m_Application = &application;
        m_Runtime = &runtime;
        m_Scene = &scene;
        m_Presentation = &presentation;
        m_ReplaySession = &replaySession;
        m_ReplayStarted = &replayStarted;
        m_DefaultMixer = defaultMixer;
    }

    void ManagedWorldRuntimeServices::BindManagedInput(Keire::Ref<Keire::InputActionContext>& context,
                                                       const Keire::InputUserId user) noexcept
    {
        m_InputContext = &context;
        m_InputUser = user;
    }

    void ManagedWorldRuntimeServices::UnbindManagedWorld() noexcept
    {
        m_ManagedWorld.CancelAll();
        m_MaterialParameters.Close();
        m_ManagedInputOperations.CancelAll();
        m_Application = nullptr;
        m_Runtime = nullptr;
        m_Scene = nullptr;
        m_Presentation = nullptr;
        m_ReplaySession = nullptr;
        m_InputContext = nullptr;
        m_InputUser = {};
        m_ReplayStarted = nullptr;
        m_DefaultMixer = {};
        m_ActivatingScene = {};
    }

    bool ManagedWorldRuntimeServices::ProcessManagedSceneTransition(const bool deterministic,
                                                                    const ManagedSceneValidator validator) noexcept
    {
        if (!m_Application || !m_Runtime || !m_Scene || !m_Presentation || !m_ReplaySession)
            return false;
        auto activation =
            m_ManagedWorld.Process(m_Application->Assets(), m_Application->Audio(), m_Application->Physics(),
                                   m_DefaultMixer, deterministic, m_ActivatingScene, validator);
        if (!activation)
            return false;
        const auto previous = std::move(*m_Runtime);
        const auto previousPresentation = std::move(*m_Presentation);
        *m_Runtime = std::move(activation->Runtime);
        *m_Scene = std::move(activation->Scene);
        *m_Presentation = std::move(activation->Presentation);
        *m_ReplaySession = *m_Runtime;
        if (previousPresentation)
            previousPresentation->Clear();
        if (previous)
            previous->Stop();
        return true;
    }

    const Keire::RenderEnvironmentSettings& ManagedWorldRuntimeServices::RenderEnvironment() const noexcept
    {
        return m_Rendering;
    }

    std::map<std::string, Keire::MaterialPropertyValue, std::less<>> ManagedWorldRuntimeServices::MaterialParameters()
    {
        return m_MaterialParameters.Snapshot();
    }

    std::uint64_t ManagedWorldRuntimeServices::BeginManagedSceneLoad(const Keire::AssetId scene,
                                                                     const Keire::SceneLoadMode mode) noexcept
    {
        const bool allowed = m_Application && m_Runtime && *m_Runtime && m_ReplayStarted && !*m_ReplayStarted;
        return m_ManagedWorld.Begin(allowed ? m_Application->Scenes() : Keire::Ref<Keire::SceneSystem>{}, scene, mode,
                                    allowed);
    }

    std::optional<Keire::ManagedSceneLoadStatus>
    ManagedWorldRuntimeServices::ManagedSceneLoad(const std::uint64_t operation) const noexcept
    {
        return m_ManagedWorld.Status(operation);
    }

    bool ManagedWorldRuntimeServices::CancelManagedSceneLoad(const std::uint64_t operation) noexcept
    {
        return m_ManagedWorld.Cancel(operation);
    }

    Keire::AssetId ManagedWorldRuntimeServices::ActiveManagedScene() const noexcept
    {
        return m_ActivatingScene ? m_ActivatingScene : (m_Scene && *m_Scene ? (*m_Scene)->Asset() : Keire::AssetId{});
    }

    std::vector<Keire::AssetId> ManagedWorldRuntimeServices::LoadedManagedScenes() const
    {
        const auto active = ActiveManagedScene();
        return active ? std::vector{active} : std::vector<Keire::AssetId>{};
    }

    std::vector<std::string>
    ManagedWorldRuntimeServices::ManagedEntityTags(const Keire::ManagedEntityHandle entity) const
    {
        const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
        const auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
        return target && target.World() == entity.World ? target.Tags() : std::vector<std::string>{};
    }

    bool ManagedWorldRuntimeServices::AddManagedEntityTag(const Keire::ManagedEntityHandle entity,
                                                          const std::string_view tag) noexcept
    {
        try
        {
            const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
            auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
            return target && target.World() == entity.World && target.AddTag(std::string(tag));
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::RemoveManagedEntityTag(const Keire::ManagedEntityHandle entity,
                                                             const std::string_view tag) noexcept
    {
        try
        {
            const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
            auto target = scene ? scene->FindEntity(Keire::EntityId(entity.Entity)) : Keire::Entity{};
            return target && target.World() == entity.World && target.RemoveTag(tag);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::ClearManagedEntityTags(const Keire::ManagedEntityHandle entity) noexcept
    {
        try
        {
            const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
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
    ManagedWorldRuntimeServices::QueryManagedEntityNames(const std::string_view name, const std::size_t maximum) const
    {
        const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
        const auto entities = scene ? scene->QueryName(name) : std::vector<Keire::Entity>{};
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
    ManagedWorldRuntimeServices::QueryManagedEntityTags(const std::string_view tag, const std::size_t maximum) const
    {
        const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
        const auto entities = scene ? scene->QueryTag(tag) : std::vector<Keire::Entity>{};
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
    ManagedWorldRuntimeServices::QueryManagedEntityComponents(const Keire::ComponentTypeId component,
                                                              const std::size_t maximum) const
    {
        const auto scene = m_Scene && *m_Scene ? *m_Scene : Keire::Ref<Keire::Scene>{};
        const auto entities = scene ? scene->Query(component) : std::vector<Keire::Entity>{};
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

    std::optional<Keire::RenderEnvironmentSettings>
    ManagedWorldRuntimeServices::ManagedRenderEnvironment() const noexcept
    {
        return m_Rendering;
    }

    bool ManagedWorldRuntimeServices::SetManagedRenderEnvironment(Keire::RenderEnvironmentSettings settings) noexcept
    {
        try
        {
            Keire::ValidateRenderEnvironmentSettings(settings);
            m_Rendering = std::move(settings);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::ManagedMaterialParameterCollectionReady(const Keire::AssetId collection) noexcept
    {
        try
        {
            return m_Application && m_MaterialParameters.Ready(m_Application->Assets(), collection);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::SetManagedMaterialParameter(const Keire::AssetId collection,
                                                                  const std::string_view name,
                                                                  Keire::MaterialPropertyValue value) noexcept
    {
        try
        {
            return m_Application &&
                   m_MaterialParameters.Set(m_Application->Assets(), collection, name, std::move(value));
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::ResetManagedMaterialParameter(const Keire::AssetId collection,
                                                                    const std::string_view name) noexcept
    {
        try
        {
            return m_Application && m_MaterialParameters.Reset(m_Application->Assets(), collection, name);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::ClearManagedMaterialParameters(const Keire::AssetId collection) noexcept
    {
        try
        {
            return m_Application && m_MaterialParameters.Clear(m_Application->Assets(), collection);
        }
        catch (...)
        {
            return false;
        }
    }

    std::vector<Keire::ManagedInputDevice> ManagedWorldRuntimeServices::ManagedInputDevices() const
    {
        if (!m_Application || !m_Application->Input())
            return {};
        std::vector<Keire::ManagedInputDevice> result;
        for (const auto& device : m_Application->Input()->Devices())
        {
            result.push_back({device.Id.Value(), static_cast<Keire::ManagedInputDeviceType>(device.Type), device.Name,
                              device.Connected, device.Paired});
        }
        return result;
    }

    std::string ManagedWorldRuntimeServices::ManagedInputControlScheme() const
    {
        if (!m_Application || !m_Application->Input() || !m_InputUser)
            return {};
        const auto users = m_Application->Input()->Users();
        const auto found = std::ranges::find(users, m_InputUser, &Keire::InputUserDescriptor::Id);
        return found == users.end() ? std::string{} : found->ControlScheme;
    }

    bool ManagedWorldRuntimeServices::SetManagedInputControlScheme(const std::string_view scheme,
                                                                   const bool locked) noexcept
    {
        try
        {
            return m_Application && m_Application->Input() && m_InputUser &&
                   m_Application->Input()->SetControlScheme(m_InputUser, std::string(scheme), locked);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::ClearManagedInputControlSchemeLock() noexcept
    {
        try
        {
            return m_Application && m_Application->Input() && m_InputUser &&
                   m_Application->Input()->ClearControlSchemeLock(m_InputUser);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedWorldRuntimeServices::SetManagedGamepadRumble(const std::uint32_t device, const float lowFrequency,
                                                              const float highFrequency,
                                                              const float durationSeconds) noexcept
    {
        try
        {
            if (!m_Application || !m_Application->Input() || !m_InputUser)
                return false;
            const auto input = m_Application->Input();
            const auto users = input->Users();
            const auto user = std::ranges::find(users, m_InputUser, &Keire::InputUserDescriptor::Id);
            const auto id = Keire::InputDeviceId(device);
            if (user == users.end() || std::ranges::find(user->Devices, id) == user->Devices.end())
                return false;
            return input->SetGamepadRumble(id, lowFrequency, highFrequency,
                                           Keire::TimeStep::FromSeconds(durationSeconds));
        }
        catch (...)
        {
            return false;
        }
    }

    std::uint64_t
    ManagedWorldRuntimeServices::BeginManagedInputRebind(const Keire::AssetId binding,
                                                         const Keire::ManagedInputRebindOptions options) noexcept
    {
        return m_ManagedInputOperations.Begin(
            m_Application ? m_Application->Input() : Keire::Ref<Keire::InputSystem>{},
            m_InputContext ? *m_InputContext : Keire::Ref<Keire::InputActionContext>{}, binding, options);
    }

    std::optional<Keire::ManagedInputRebindSnapshot>
    ManagedWorldRuntimeServices::ManagedInputRebind(const std::uint64_t operation) const noexcept
    {
        return m_ManagedInputOperations.Status(operation);
    }

    bool ManagedWorldRuntimeServices::ResolveManagedInputRebind(
        const std::uint64_t operation, const Keire::ManagedInputRebindResolution resolution) noexcept
    {
        return m_ManagedInputOperations.Resolve(operation, resolution);
    }

    bool ManagedWorldRuntimeServices::CancelManagedInputRebind(const std::uint64_t operation) noexcept
    {
        return m_ManagedInputOperations.Cancel(operation);
    }

    bool ManagedWorldRuntimeServices::SaveManagedInputBindings(const std::string_view profile) noexcept
    {
        try
        {
            if (!m_InputContext || !*m_InputContext)
                return false;
            (*m_InputContext)->SaveBindingOverrides(profile);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    int ManagedWorldRuntimeServices::LoadManagedInputBindings(const std::string_view profile) noexcept
    {
        try
        {
            return m_InputContext && *m_InputContext
                       ? static_cast<int>((*m_InputContext)->LoadBindingOverrides(profile))
                       : -1;
        }
        catch (...)
        {
            return -1;
        }
    }

    bool ManagedWorldRuntimeServices::ClearManagedInputBindings() noexcept
    {
        try
        {
            if (!m_InputContext || !*m_InputContext)
                return false;
            (*m_InputContext)->ClearBindingOverrides();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<Keire::ManagedRaycastHit>
    ManagedWorldRuntimeServices::CapsuleCastManaged(const Keire::ManagedCapsuleCastQuery& query) noexcept
    {
        return Keire::Detail::QueryManagedCapsule(m_Runtime ? *m_Runtime : Keire::Ref<Keire::SceneRuntimeSession>{},
                                                  query);
    }

    std::vector<Keire::AssetId>
    ManagedWorldRuntimeServices::OverlapSphereManaged(const Keire::ManagedSphereOverlapQuery& query)
    {
        return Keire::Detail::QueryManagedSphereOverlap(
            m_Runtime ? *m_Runtime : Keire::Ref<Keire::SceneRuntimeSession>{}, query);
    }
} // namespace KeireRuntime
