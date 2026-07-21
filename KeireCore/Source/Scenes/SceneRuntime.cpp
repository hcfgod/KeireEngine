#include "Keire/Scenes/Scene.h"

#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    class SceneRuntimeSession::Impl final
    {
      public:
        explicit Impl(Ref<Scene> scene) : Edit(std::move(scene)), OwnerThread(std::this_thread::get_id())
        {
            if (!Edit || !Edit->IsOpen())
                throw std::invalid_argument("SceneRuntimeSession requires an open edit scene.");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("SceneRuntimeSession::") + operation +
                                       " must run on the owner thread.");
        }

        template <typename Callback> void Invoke(const char* callback, Callback&& operation)
        {
            try
            {
                std::forward<Callback>(operation)();
            }
            catch (const std::exception& exception)
            {
                PlayState = ScenePlayState::Faulted;
                Failure = {callback, exception.what()};
            }
            catch (...)
            {
                PlayState = ScenePlayState::Faulted;
                Failure = {callback, "Component callback threw a non-standard exception."};
            }
        }

        Ref<Scene> Edit;
        Ref<Scene> Runtime;
        std::thread::id OwnerThread;
        ScenePlayState PlayState = ScenePlayState::Stopped;
        SceneRuntimeDiagnostic Failure;
    };

    SceneRuntimeSession::SceneRuntimeSession(Ref<Scene> editScene)
        : m_Impl(std::make_unique<Impl>(std::move(editScene)))
    {
    }

    SceneRuntimeSession::~SceneRuntimeSession() { Stop(); }

    ScenePlayState SceneRuntimeSession::State() const noexcept { return m_Impl->PlayState; }
    Ref<Scene> SceneRuntimeSession::EditScene() const noexcept { return m_Impl->Edit; }
    Ref<Scene> SceneRuntimeSession::RuntimeScene() const noexcept { return m_Impl->Runtime; }
    SceneRuntimeDiagnostic SceneRuntimeSession::Diagnostic() const { return m_Impl->Failure; }

    void SceneRuntimeSession::Play()
    {
        m_Impl->RequireOwner("Play");
        if (m_Impl->PlayState != ScenePlayState::Stopped)
            return;
        m_Impl->Failure = {};
        m_Impl->Runtime = CreateRef<Scene>(m_Impl->Edit->Asset(), m_Impl->Edit->Snapshot(), m_Impl->Edit->Components());
        m_Impl->Runtime->MarkSaved();
        m_Impl->PlayState = ScenePlayState::Playing;
        m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
    }

    void SceneRuntimeSession::Pause(const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        if (m_Impl->PlayState == ScenePlayState::Playing && paused)
            m_Impl->PlayState = ScenePlayState::Paused;
        else if (m_Impl->PlayState == ScenePlayState::Paused && !paused)
            m_Impl->PlayState = ScenePlayState::Playing;
    }

    void SceneRuntimeSession::TogglePause() { Pause(m_Impl->PlayState != ScenePlayState::Paused); }

    bool SceneRuntimeSession::Step(const float fixedDeltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (m_Impl->PlayState != ScenePlayState::Paused)
            return false;
        if (fixedDeltaSeconds <= 0.0F)
            throw std::invalid_argument("Scene step delta must be positive.");
        m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(fixedDeltaSeconds); });
        return m_Impl->PlayState != ScenePlayState::Faulted;
    }

    void SceneRuntimeSession::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        if (m_Impl->PlayState == ScenePlayState::Playing)
            m_Impl->Invoke("FixedUpdate", [&] { m_Impl->Runtime->FixedUpdate(deltaSeconds); });
    }

    void SceneRuntimeSession::Update(const float deltaSeconds)
    {
        m_Impl->RequireOwner("Update");
        if (m_Impl->PlayState == ScenePlayState::Playing)
            m_Impl->Invoke("Update", [&] { m_Impl->Runtime->Update(deltaSeconds); });
    }

    void SceneRuntimeSession::ReplaceRuntime(SceneDefinition definition)
    {
        m_Impl->RequireOwner("ReplaceRuntime");
        if (m_Impl->PlayState == ScenePlayState::Stopped || !m_Impl->Runtime)
            throw std::logic_error("SceneRuntimeSession::ReplaceRuntime requires an active Play session.");
        auto replacement = CreateRef<Scene>(m_Impl->Edit->Asset(), std::move(definition), m_Impl->Edit->Components());
        replacement->MarkSaved();
        m_Impl->Runtime->EndPlay();
        m_Impl->Runtime->Close();
        m_Impl->Runtime = std::move(replacement);
        m_Impl->Failure = {};
        m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
    }

    void SceneRuntimeSession::Stop() noexcept
    {
        if (!m_Impl || m_Impl->PlayState == ScenePlayState::Stopped)
            return;
        if (m_Impl->Runtime)
        {
            m_Impl->Runtime->EndPlay();
            m_Impl->Runtime->Close();
            m_Impl->Runtime.Reset();
        }
        m_Impl->PlayState = ScenePlayState::Stopped;
        m_Impl->Failure = {};
    }
} // namespace Keire
