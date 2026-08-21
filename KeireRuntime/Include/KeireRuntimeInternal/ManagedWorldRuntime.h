#pragma once

#include "Keire/Ref.h"
#include "Keire/Scripting/ScriptSystem.h"
#include "KeireInternal/Scripting/ManagedRuntimeApplicationServices.h"
#include "KeireInternal/Scripting/ManagedRuntimeInput.h"

#include <memory>
#include <optional>
#include <vector>

namespace Keire
{
    class Application;
    class AssetSystem;
    class AudioSystem;
    class PhysicsSystem;
    class Scene;
    class ScenePresentationRuntime;
    class SceneRuntimeSession;
    class SceneSystem;
} // namespace Keire

namespace KeireRuntime
{
    struct ManagedWorldActivation final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Runtime;
        Keire::Ref<Keire::Scene> Scene;
        Keire::Ref<Keire::ScenePresentationRuntime> Presentation;
    };

    using ManagedSceneValidator = bool (*)(const Keire::Ref<Keire::Scene>& scene);

    class ManagedWorldRuntime final
    {
      public:
        ManagedWorldRuntime();
        ~ManagedWorldRuntime();

        ManagedWorldRuntime(const ManagedWorldRuntime&) = delete;
        ManagedWorldRuntime& operator=(const ManagedWorldRuntime&) = delete;

        [[nodiscard]] std::uint64_t Begin(const Keire::Ref<Keire::SceneSystem>& scenes, Keire::AssetId scene,
                                          Keire::SceneLoadMode mode, bool allowed) noexcept;
        [[nodiscard]] std::optional<Keire::ManagedSceneLoadStatus> Status(std::uint64_t operation) const noexcept;
        [[nodiscard]] bool Cancel(std::uint64_t operation) noexcept;
        void CancelAll() noexcept;

        [[nodiscard]] std::optional<ManagedWorldActivation>
        Process(const Keire::Ref<Keire::AssetSystem>& assets, const Keire::Ref<Keire::AudioSystem>& audio,
                const Keire::Ref<Keire::PhysicsSystem>& physics, Keire::AssetId defaultMixer, bool deterministic,
                Keire::AssetId& activatingScene, ManagedSceneValidator validator) noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ManagedWorldRuntimeServices : public Keire::Detail::ManagedRuntimeApplicationServices
    {
      public:
        ManagedWorldRuntimeServices(bool editor, Keire::RenderEnvironmentSettings rendering);
        ~ManagedWorldRuntimeServices() override;

        ManagedWorldRuntimeServices(const ManagedWorldRuntimeServices&) = delete;
        ManagedWorldRuntimeServices& operator=(const ManagedWorldRuntimeServices&) = delete;

      protected:
        void BindManagedWorld(Keire::Application& application, Keire::Ref<Keire::SceneRuntimeSession>& runtime,
                              Keire::Ref<Keire::Scene>& scene,
                              Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                              Keire::Ref<Keire::SceneRuntimeSession>& replaySession, bool& replayStarted,
                              Keire::AssetId defaultMixer) noexcept;
        void BindManagedInput(Keire::Ref<Keire::InputActionContext>& context, Keire::InputUserId user) noexcept;
        void UnbindManagedWorld() noexcept;
        [[nodiscard]] bool ProcessManagedSceneTransition(bool deterministic, ManagedSceneValidator validator) noexcept;
        [[nodiscard]] const Keire::RenderEnvironmentSettings& RenderEnvironment() const noexcept;

      private:
        [[nodiscard]] std::uint64_t BeginManagedSceneLoad(Keire::AssetId scene,
                                                          Keire::SceneLoadMode mode) noexcept final;
        [[nodiscard]] std::optional<Keire::ManagedSceneLoadStatus>
        ManagedSceneLoad(std::uint64_t operation) const noexcept final;
        [[nodiscard]] bool CancelManagedSceneLoad(std::uint64_t operation) noexcept final;
        [[nodiscard]] Keire::AssetId ActiveManagedScene() const noexcept final;
        [[nodiscard]] std::vector<Keire::AssetId> LoadedManagedScenes() const final;
        [[nodiscard]] std::optional<Keire::RenderEnvironmentSettings> ManagedRenderEnvironment() const noexcept final;
        [[nodiscard]] bool SetManagedRenderEnvironment(Keire::RenderEnvironmentSettings settings) noexcept final;
        [[nodiscard]] std::vector<Keire::ManagedInputDevice> ManagedInputDevices() const final;
        [[nodiscard]] std::string ManagedInputControlScheme() const final;
        [[nodiscard]] bool SetManagedInputControlScheme(std::string_view scheme, bool locked) noexcept final;
        [[nodiscard]] bool ClearManagedInputControlSchemeLock() noexcept final;
        [[nodiscard]] bool SetManagedGamepadRumble(std::uint32_t device, float lowFrequency, float highFrequency,
                                                   float durationSeconds) noexcept final;
        [[nodiscard]] std::uint64_t BeginManagedInputRebind(Keire::AssetId binding,
                                                            Keire::ManagedInputRebindOptions options) noexcept final;
        [[nodiscard]] std::optional<Keire::ManagedInputRebindSnapshot>
        ManagedInputRebind(std::uint64_t operation) const noexcept final;
        [[nodiscard]] bool ResolveManagedInputRebind(std::uint64_t operation,
                                                     Keire::ManagedInputRebindResolution resolution) noexcept final;
        [[nodiscard]] bool CancelManagedInputRebind(std::uint64_t operation) noexcept final;
        [[nodiscard]] bool SaveManagedInputBindings(std::string_view profile) noexcept final;
        [[nodiscard]] int LoadManagedInputBindings(std::string_view profile) noexcept final;
        [[nodiscard]] bool ClearManagedInputBindings() noexcept final;
        [[nodiscard]] std::optional<Keire::ManagedRaycastHit>
        CapsuleCastManaged(const Keire::ManagedCapsuleCastQuery& query) noexcept final;
        [[nodiscard]] std::vector<Keire::AssetId>
        OverlapSphereManaged(const Keire::ManagedSphereOverlapQuery& query) final;

        ManagedWorldRuntime m_ManagedWorld;
        Keire::Detail::ManagedInputOperationStore m_ManagedInputOperations;
        Keire::RenderEnvironmentSettings m_Rendering;
        Keire::Application* m_Application = nullptr;
        Keire::Ref<Keire::SceneRuntimeSession>* m_Runtime = nullptr;
        Keire::Ref<Keire::Scene>* m_Scene = nullptr;
        Keire::Ref<Keire::ScenePresentationRuntime>* m_Presentation = nullptr;
        Keire::Ref<Keire::SceneRuntimeSession>* m_ReplaySession = nullptr;
        Keire::Ref<Keire::InputActionContext>* m_InputContext = nullptr;
        Keire::InputUserId m_InputUser;
        bool* m_ReplayStarted = nullptr;
        Keire::AssetId m_DefaultMixer;
        Keire::AssetId m_ActivatingScene;
    };
} // namespace KeireRuntime
