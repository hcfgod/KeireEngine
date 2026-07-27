#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"

#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    class SceneRuntimeSession::Impl final
    {
      public:
        Impl(Ref<Scene> scene, Ref<AssetSystem> assets, Ref<AudioSystem> audio)
            : Edit(std::move(scene)), OwnerThread(std::this_thread::get_id())
        {
            if (!Edit || !Edit->IsOpen())
                throw std::invalid_argument("SceneRuntimeSession requires an open edit scene.");
            if (assets)
                Presentation = CreateRef<ScenePresentationRuntime>(std::move(assets), std::move(audio));
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
        Ref<ScenePresentationRuntime> Presentation;
        float PresentationWidth = 1920.0F;
        float PresentationHeight = 1080.0F;
        RuntimeUiInsets SafeArea;
    };

    SceneRuntimeSession::SceneRuntimeSession(Ref<Scene> editScene, Ref<AssetSystem> assets, Ref<AudioSystem> audio)
        : m_Impl(std::make_unique<Impl>(std::move(editScene), std::move(assets), std::move(audio)))
    {
    }

    SceneRuntimeSession::~SceneRuntimeSession() { Stop(); }

    ScenePlayState SceneRuntimeSession::State() const noexcept { return m_Impl->PlayState; }
    Ref<Scene> SceneRuntimeSession::EditScene() const noexcept { return m_Impl->Edit; }
    Ref<Scene> SceneRuntimeSession::RuntimeScene() const noexcept { return m_Impl->Runtime; }
    SceneRuntimeDiagnostic SceneRuntimeSession::Diagnostic() const { return m_Impl->Failure; }
    Ref<ScenePresentationRuntime> SceneRuntimeSession::Presentation() const noexcept { return m_Impl->Presentation; }

    void SceneRuntimeSession::SetPresentationViewport(const float width, const float height,
                                                      const RuntimeUiInsets safeArea)
    {
        m_Impl->RequireOwner("SetPresentationViewport");
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0F || height <= 0.0F)
            throw std::invalid_argument("Scene presentation viewport dimensions must be finite and positive.");
        m_Impl->PresentationWidth = width;
        m_Impl->PresentationHeight = height;
        m_Impl->SafeArea = safeArea;
        if (m_Impl->Presentation && m_Impl->Runtime)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, width, height, true, safeArea);
    }

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
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
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
        {
            m_Impl->Invoke("Update", [&] { m_Impl->Runtime->Update(deltaSeconds); });
            if (m_Impl->Presentation && m_Impl->PlayState != ScenePlayState::Faulted)
                m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth,
                                                  m_Impl->PresentationHeight, true, m_Impl->SafeArea);
        }
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
        if (m_Impl->Presentation)
            m_Impl->Presentation->Clear();
        m_Impl->Runtime = std::move(replacement);
        m_Impl->Failure = {};
        m_Impl->Invoke("Awake/OnEnable", [&] { m_Impl->Runtime->BeginPlay(); });
        if (m_Impl->Presentation)
            m_Impl->Presentation->Synchronize(m_Impl->Runtime, m_Impl->PresentationWidth, m_Impl->PresentationHeight,
                                              true, m_Impl->SafeArea);
    }

    void SceneRuntimeSession::Stop() noexcept
    {
        if (!m_Impl || m_Impl->PlayState == ScenePlayState::Stopped)
            return;
        if (m_Impl->Runtime)
        {
            if (m_Impl->Presentation)
                m_Impl->Presentation->Clear();
            m_Impl->Runtime->EndPlay();
            m_Impl->Runtime->Close();
            m_Impl->Runtime.Reset();
        }
        m_Impl->PlayState = ScenePlayState::Stopped;
        m_Impl->Failure = {};
    }
} // namespace Keire
