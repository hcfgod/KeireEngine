#include "KeireTests/TestSupport.h"

#include "KeireInternal/FileSystem.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Ui/UiToolkit.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct TemporaryDirectory final
    {
        explicit TemporaryDirectory(const std::string& name) : Path(KeireTests::MakeTestDirectory(name))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    void UseDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    [[nodiscard]] std::string JsonString(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        result.push_back('"');
        for (const char character : value)
        {
            switch (character)
            {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(character);
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    [[nodiscard]] std::string ManagedReference(const Keire::AssetId id)
    {
        return "{\"Id\":{\"High\":" + std::to_string(id.High()) + ",\"Low\":" + std::to_string(id.Low()) + "}}";
    }

    struct SceneProbe final
    {
        Keire::AssetId First;
        Keire::AssetId Second;
        bool SingleReady = false;
        bool AdditiveReady = false;
        bool ActiveChanged = false;
        bool FailedLoadPreservedActive = false;
        int ActiveChangeEvents = 0;
    };

    class SceneProbeLayer final : public Keire::Layer
    {
      public:
        explicit SceneProbeLayer(std::shared_ptr<SceneProbe> probe)
            : Keire::Layer("SceneProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override
        {
            Listen<Keire::ActiveSceneChangedEvent>(
                [this](const auto&)
                {
                    ++m_Probe->ActiveChangeEvents;
                    return Keire::EventFlow::Continue;
                });
            m_First = Owner().Scenes()->Load(m_Probe->First);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (!m_Probe->SingleReady && m_First->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->SingleReady = Owner().Scenes()->Active()->Asset() == m_Probe->First;
                m_Second = Owner().Scenes()->Load(m_Probe->Second, Keire::SceneLoadMode::Additive);
                return;
            }
            if (m_Second && m_Second->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->AdditiveReady = Owner().Scenes()->LoadedScenes().size() == 2;
                REQUIRE(Owner().Scenes()->SetActive(m_Probe->Second));
                m_Second.Reset();
                return;
            }
            if (m_Probe->AdditiveReady && !m_Missing && Owner().Scenes()->Active()->Asset() == m_Probe->Second)
            {
                m_Probe->ActiveChanged = true;
                m_Missing = Owner().Scenes()->Load(Keire::AssetId::Generate());
                return;
            }
            if (m_Missing && m_Missing->State() == Keire::SceneLoadState::Failed)
            {
                m_Probe->FailedLoadPreservedActive = Owner().Scenes()->Active()->Asset() == m_Probe->Second &&
                                                     Owner().Scenes()->LoadedScenes().size() == 2;
                Owner().RequestExit();
            }
        }

      private:
        std::shared_ptr<SceneProbe> m_Probe;
        Keire::Ref<Keire::SceneLoadOperation> m_First;
        Keire::Ref<Keire::SceneLoadOperation> m_Second;
        Keire::Ref<Keire::SceneLoadOperation> m_Missing;
    };

    class SceneProbeApplication final : public Keire::Application
    {
      public:
        SceneProbeApplication(Keire::ApplicationSpecification specification, std::shared_ptr<SceneProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<SceneProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<SceneProbe> m_Probe;
    };

    enum class SceneCommitProbeMode : std::uint8_t
    {
        ThrowOnUnloaded,
        ThrowOnDeferredUnload,
        ThrowOnActiveChanged,
        CancelOnLoaded
    };

    struct SceneCommitProbe final
    {
        Keire::AssetId First;
        Keire::AssetId Second;
        SceneCommitProbeMode Mode = SceneCommitProbeMode::ThrowOnUnloaded;
        bool ListenerObservedCommittedState = false;
        bool WorkerObservedReady = false;
        bool Completed = false;
        bool UnexpectedTerminalState = false;
        bool TimedOut = false;
    };

    class SceneCommitProbeLayer final : public Keire::Layer
    {
      public:
        explicit SceneCommitProbeLayer(std::shared_ptr<SceneCommitProbe> probe)
            : Keire::Layer("SceneCommitProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override
        {
            Listen<Keire::SceneUnloadedEvent>(
                [this](const Keire::SceneUnloadedEvent& event)
                {
                    if (m_Probe->Mode == SceneCommitProbeMode::ThrowOnDeferredUnload && event.Scene == m_Probe->First)
                    {
                        return ObserveDeferredUnloadAndThrow();
                    }
                    if (m_Probe->Mode == SceneCommitProbeMode::ThrowOnUnloaded && event.Scene == m_Probe->First)
                        return ObserveCommitAndThrow();
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::ActiveSceneChangedEvent>(
                [this](const Keire::ActiveSceneChangedEvent& event)
                {
                    if (m_Probe->Mode == SceneCommitProbeMode::ThrowOnActiveChanged && event.Current == m_Probe->Second)
                    {
                        return ObserveCommitAndThrow();
                    }
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::SceneLoadedEvent>(
                [this](const Keire::SceneLoadedEvent& event)
                {
                    if (m_Probe->Mode != SceneCommitProbeMode::CancelOnLoaded || event.Scene != m_Probe->Second)
                        return Keire::EventFlow::Continue;
                    std::thread worker(
                        [this]
                        {
                            m_Second->Cancel();
                            m_Probe->WorkerObservedReady = m_Second->State() == Keire::SceneLoadState::Ready;
                        });
                    worker.join();
                    return Keire::EventFlow::Continue;
                });
            m_First = Owner().Scenes()->Load(m_Probe->First);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (++m_Frames > 2048)
            {
                m_Probe->TimedOut = true;
                Owner().RequestExit(1);
                return;
            }
            const auto unexpectedlyTerminal = [](const Keire::Ref<Keire::SceneLoadOperation>& operation)
            {
                return operation && (operation->State() == Keire::SceneLoadState::Failed ||
                                     operation->State() == Keire::SceneLoadState::Cancelled);
            };
            if (unexpectedlyTerminal(m_First) || unexpectedlyTerminal(m_Second))
            {
                m_Probe->UnexpectedTerminalState = true;
                Owner().RequestExit(2);
                return;
            }
            if (!m_Second && m_First->State() == Keire::SceneLoadState::Ready)
            {
                m_FirstScene = m_First->Result();
                const auto mode = m_Probe->Mode == SceneCommitProbeMode::ThrowOnDeferredUnload
                                      ? Keire::SceneLoadMode::Additive
                                      : Keire::SceneLoadMode::Single;
                m_Second = Owner().Scenes()->Load(m_Probe->Second, mode);
                return;
            }
            if (m_Second && m_Second->State() == Keire::SceneLoadState::Ready)
            {
                if (m_Probe->Mode == SceneCommitProbeMode::ThrowOnDeferredUnload)
                {
                    if (!m_UnloadQueued)
                    {
                        m_UnloadQueued = Owner().Scenes()->Unload(m_Probe->First);
                    }
                    return;
                }
                const auto loaded = Owner().Scenes()->LoadedScenes();
                m_Probe->Completed = m_Probe->WorkerObservedReady && m_Second->Result() && loaded.size() == 1 &&
                                     loaded.front() == m_Second->Result() &&
                                     Owner().Scenes()->Active() == m_Second->Result();
                Owner().RequestExit();
            }
        }

      private:
        [[noreturn]] Keire::EventFlow ObserveCommitAndThrow()
        {
            const auto loaded = Owner().Scenes()->LoadedScenes();
            m_Probe->ListenerObservedCommittedState =
                m_Second && m_Second->State() == Keire::SceneLoadState::Ready && m_Second->Result() &&
                loaded.size() == 1 && loaded.front() == m_Second->Result() &&
                Owner().Scenes()->Active() == m_Second->Result() && m_FirstScene && !m_FirstScene->IsOpen();
            throw std::runtime_error("expected scene lifecycle listener failure");
        }

        [[noreturn]] Keire::EventFlow ObserveDeferredUnloadAndThrow()
        {
            const auto loaded = Owner().Scenes()->LoadedScenes();
            m_Probe->ListenerObservedCommittedState =
                m_UnloadQueued && m_First && m_First->State() == Keire::SceneLoadState::Ready && m_FirstScene &&
                !m_FirstScene->IsOpen() && m_Second && m_Second->State() == Keire::SceneLoadState::Ready &&
                m_Second->Result() && loaded.size() == 1 && loaded.front() == m_Second->Result() &&
                Owner().Scenes()->Active() == m_Second->Result();
            throw std::runtime_error("expected scene lifecycle listener failure");
        }

        std::shared_ptr<SceneCommitProbe> m_Probe;
        Keire::Ref<Keire::SceneLoadOperation> m_First;
        Keire::Ref<Keire::SceneLoadOperation> m_Second;
        Keire::Ref<Keire::Scene> m_FirstScene;
        std::size_t m_Frames = 0;
        bool m_UnloadQueued = false;
    };

    class SceneCommitProbeApplication final : public Keire::Application
    {
      public:
        SceneCommitProbeApplication(Keire::ApplicationSpecification specification,
                                    std::shared_ptr<SceneCommitProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<SceneCommitProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<SceneCommitProbe> m_Probe;
    };

    struct SceneReentrancyProbe final
    {
        Keire::AssetId First;
        Keire::AssetId Second;
        Keire::AssetId Third;
        bool LoadsQueuedDuringCallback = false;
        bool UnloadQueuedDuringCallback = false;
        bool UnloadListenerObservedCommittedState = false;
        bool ReentrantUnloadWasDeferred = false;
        bool Completed = false;
        bool UnexpectedTerminalState = false;
        bool TimedOut = false;
    };

    class SceneReentrancyProbeLayer final : public Keire::Layer
    {
      public:
        explicit SceneReentrancyProbeLayer(std::shared_ptr<SceneReentrancyProbe> probe)
            : Keire::Layer("SceneReentrancyProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override
        {
            Listen<Keire::SceneLoadedEvent>(
                [this](const Keire::SceneLoadedEvent& event)
                {
                    if (event.Scene != m_Probe->First || m_Second)
                        return Keire::EventFlow::Continue;
                    m_Second = Owner().Scenes()->Load(m_Probe->Second, Keire::SceneLoadMode::Additive);
                    m_Third = Owner().Scenes()->Load(m_Probe->Third, Keire::SceneLoadMode::Additive);
                    m_Probe->LoadsQueuedDuringCallback = m_Second->State() == Keire::SceneLoadState::Queued &&
                                                         m_Third->State() == Keire::SceneLoadState::Queued &&
                                                         Owner().Scenes()->LoadedScenes().size() == 1;
                    return Keire::EventFlow::Continue;
                });
            Listen<Keire::SceneUnloadedEvent>(
                [this](const Keire::SceneUnloadedEvent& event)
                {
                    if (event.Scene != m_Probe->First)
                        return Keire::EventFlow::Continue;
                    const auto active = Owner().Scenes()->Active();
                    m_Probe->UnloadListenerObservedCommittedState =
                        active && active->Asset() == m_Probe->Second && !Owner().Scenes()->Find(m_Probe->First);
                    m_Probe->UnloadQueuedDuringCallback = Owner().Scenes()->Unload(m_Probe->Second);
                    return Keire::EventFlow::Continue;
                });
            m_First = Owner().Scenes()->Load(m_Probe->First);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (++m_Frames > 2048)
            {
                m_Probe->TimedOut = true;
                Owner().RequestExit(1);
                return;
            }
            const auto unexpectedlyTerminal = [](const Keire::Ref<Keire::SceneLoadOperation>& operation)
            {
                return operation && (operation->State() == Keire::SceneLoadState::Failed ||
                                     operation->State() == Keire::SceneLoadState::Cancelled);
            };
            if (unexpectedlyTerminal(m_First) || unexpectedlyTerminal(m_Second) || unexpectedlyTerminal(m_Third))
            {
                m_Probe->UnexpectedTerminalState = true;
                Owner().RequestExit(2);
                return;
            }
            if (!m_FirstUnloadQueued && m_Second && m_Third && m_Second->State() == Keire::SceneLoadState::Ready &&
                m_Third->State() == Keire::SceneLoadState::Ready)
            {
                const auto loaded = Owner().Scenes()->LoadedScenes();
                if (loaded.size() == 3)
                    m_FirstUnloadQueued = Owner().Scenes()->Unload(m_Probe->First);
                return;
            }
            if (!m_FirstUnloadQueued || !m_Probe->UnloadQueuedDuringCallback)
                return;
            if (!Owner().Scenes()->Find(m_Probe->First) && Owner().Scenes()->Find(m_Probe->Second))
            {
                m_Probe->ReentrantUnloadWasDeferred = true;
                return;
            }
            if (!Owner().Scenes()->Find(m_Probe->First) && !Owner().Scenes()->Find(m_Probe->Second))
            {
                const auto active = Owner().Scenes()->Active();
                m_Probe->Completed =
                    active && active->Asset() == m_Probe->Third && Owner().Scenes()->LoadedScenes().size() == 1;
                Owner().RequestExit(m_Probe->Completed ? 0 : 3);
            }
        }

      private:
        std::shared_ptr<SceneReentrancyProbe> m_Probe;
        Keire::Ref<Keire::SceneLoadOperation> m_First;
        Keire::Ref<Keire::SceneLoadOperation> m_Second;
        Keire::Ref<Keire::SceneLoadOperation> m_Third;
        std::size_t m_Frames = 0;
        bool m_FirstUnloadQueued = false;
    };

    class SceneReentrancyProbeApplication final : public Keire::Application
    {
      public:
        SceneReentrancyProbeApplication(Keire::ApplicationSpecification specification,
                                        std::shared_ptr<SceneReentrancyProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<SceneReentrancyProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<SceneReentrancyProbe> m_Probe;
    };

    struct SceneLoadingCancellationProbe final
    {
        Keire::AssetId Scene;
        std::shared_ptr<std::atomic_bool> ReleaseDecoder;
        bool ObservedLoading = false;
        bool WorkerObservedCancelled = false;
        bool NoSceneWasCommitted = false;
        bool UnexpectedTerminalState = false;
        bool TimedOut = false;
    };

    class SceneLoadingCancellationProbeLayer final : public Keire::Layer
    {
      public:
        explicit SceneLoadingCancellationProbeLayer(std::shared_ptr<SceneLoadingCancellationProbe> probe)
            : Keire::Layer("SceneLoadingCancellationProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override { m_Operation = Owner().Scenes()->Load(m_Probe->Scene); }

        void OnDetach() noexcept override { ReleaseDecoder(); }

        void OnUpdate(const Keire::Time&) override
        {
            if (++m_Frames > 2048)
            {
                m_Probe->TimedOut = true;
                ReleaseDecoder();
                Owner().RequestExit(1);
                return;
            }
            const auto state = m_Operation->State();
            if (state == Keire::SceneLoadState::Loading)
            {
                m_Probe->ObservedLoading = true;
                std::thread worker(
                    [this]
                    {
                        m_Operation->Cancel();
                        m_Probe->WorkerObservedCancelled = m_Operation->State() == Keire::SceneLoadState::Cancelled;
                    });
                worker.join();
                ReleaseDecoder();
                m_Probe->NoSceneWasCommitted = !Owner().Scenes()->Active() && Owner().Scenes()->LoadedScenes().empty();
                Owner().RequestExit(m_Probe->WorkerObservedCancelled && m_Probe->NoSceneWasCommitted ? 0 : 2);
                return;
            }
            if (state == Keire::SceneLoadState::Ready || state == Keire::SceneLoadState::Failed ||
                state == Keire::SceneLoadState::Cancelled)
            {
                m_Probe->UnexpectedTerminalState = true;
                ReleaseDecoder();
                Owner().RequestExit(3);
            }
        }

      private:
        void ReleaseDecoder() const noexcept
        {
            m_Probe->ReleaseDecoder->store(true, std::memory_order_release);
            m_Probe->ReleaseDecoder->notify_all();
        }

        std::shared_ptr<SceneLoadingCancellationProbe> m_Probe;
        Keire::Ref<Keire::SceneLoadOperation> m_Operation;
        std::size_t m_Frames = 0;
    };

    class SceneLoadingCancellationProbeApplication final : public Keire::Application
    {
      public:
        SceneLoadingCancellationProbeApplication(Keire::ApplicationSpecification specification,
                                                 std::shared_ptr<SceneLoadingCancellationProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<SceneLoadingCancellationProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<SceneLoadingCancellationProbe> m_Probe;
    };

    struct RuntimeWorldProbe final
    {
        Keire::AssetId First;
        Keire::AssetId Additive;
        Keire::AssetId Rejected;
        Keire::SceneHandle FirstHandle;
        Keire::SceneHandle AdditiveHandle;
        bool AdditiveActivated = false;
        bool FirstUnloaded = false;
        bool RejectedWithoutReplacingActive = false;
    };

    [[nodiscard]] bool ValidateRuntimeWorldScene(const Keire::Ref<Keire::Scene>& scene)
    {
        return scene && scene->Name() != "Rejected";
    }

    class RuntimeWorldProbeLayer final : public Keire::Layer
    {
      public:
        explicit RuntimeWorldProbeLayer(std::shared_ptr<RuntimeWorldProbe> probe)
            : Keire::Layer("RuntimeWorldProbe"), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnAttach() override
        {
            m_World = Keire::CreateRef<Keire::SceneRuntimeWorld>(
                Keire::SceneRuntimeWorldSpecification{.Scenes = Owner().Scenes(), .Assets = Owner().Assets()});
            m_First = m_World->Load(m_Probe->First);
        }

        void OnDetach() noexcept override
        {
            if (m_World)
                m_World->Close();
            m_World.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            m_World->Process(&ValidateRuntimeWorldScene);
            if (m_Stage == Stage::First && m_First->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->FirstHandle = m_First->Result();
                m_Additive = m_World->Load(m_Probe->Additive, Keire::SceneLoadMode::Additive);
                m_Stage = Stage::Additive;
                return;
            }
            if (m_Stage == Stage::Additive && m_Additive->State() == Keire::SceneLoadState::Ready)
            {
                m_Probe->AdditiveHandle = m_Additive->Result();
                m_Probe->AdditiveActivated =
                    m_Probe->FirstHandle != m_Probe->AdditiveHandle && m_World->Active() == m_Probe->FirstHandle &&
                    m_World->LoadedScenes().size() == 2 && m_World->Find(m_Probe->AdditiveHandle)->Name() == "Additive";
                (void)m_World->SetActive(m_Probe->AdditiveHandle);
                m_Stage = Stage::ActivateAdditive;
                return;
            }
            if (m_Stage == Stage::ActivateAdditive && m_World->Active() == m_Probe->AdditiveHandle)
            {
                (void)m_World->Unload(m_Probe->FirstHandle);
                m_Stage = Stage::UnloadFirst;
                return;
            }
            if (m_Stage == Stage::UnloadFirst && !m_World->IsLoaded(m_Probe->FirstHandle))
            {
                m_Probe->FirstUnloaded =
                    m_World->Active() == m_Probe->AdditiveHandle &&
                    m_World->LoadedScenes() == std::vector<Keire::SceneHandle>{m_Probe->AdditiveHandle};
                m_Rejected = m_World->Load(m_Probe->Rejected);
                m_Stage = Stage::Rejected;
                return;
            }
            if (m_Stage == Stage::Rejected && m_Rejected->State() == Keire::SceneLoadState::Failed)
            {
                m_Probe->RejectedWithoutReplacingActive = m_World->Active() == m_Probe->AdditiveHandle &&
                                                          m_World->IsLoaded(m_Probe->AdditiveHandle) &&
                                                          m_World->LoadedScenes().size() == 1;
                Owner().RequestExit();
            }
        }

      private:
        enum class Stage : std::uint8_t
        {
            First,
            Additive,
            ActivateAdditive,
            UnloadFirst,
            Rejected
        };

        std::shared_ptr<RuntimeWorldProbe> m_Probe;
        Keire::Ref<Keire::SceneRuntimeWorld> m_World;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> m_First;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> m_Additive;
        Keire::Ref<Keire::SceneRuntimeLoadOperation> m_Rejected;
        Stage m_Stage = Stage::First;
    };

    class RuntimeWorldProbeApplication final : public Keire::Application
    {
      public:
        RuntimeWorldProbeApplication(Keire::ApplicationSpecification specification,
                                     std::shared_ptr<RuntimeWorldProbe> probe)
            : Keire::Application(std::move(specification)), m_Probe(std::move(probe))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<RuntimeWorldProbeLayer>(m_Probe)); }

      private:
        std::shared_ptr<RuntimeWorldProbe> m_Probe;
    };
} // namespace

TEST_CASE("Projects create isolated starter assets and hold exclusive editor locks")
{
    TemporaryDirectory directory("ProjectTests");
    const auto created = Keire::Project::Create({directory.Path, "Game", Keire::ProjectTemplate::Starter});
    REQUIRE(created);
    CHECK(created->Descriptor().Name == "Game");
    CHECK(created->Descriptor().SchemaVersion == Keire::CurrentProjectSchemaVersion);
    CHECK_FALSE(created->Descriptor().CreatedAt.empty());
    CHECK(created->Descriptor().LastSavedWithEngineVersion == created->Descriptor().CreatedWithEngineVersion);
    REQUIRE(created->Descriptor().Template);
    CHECK(created->Descriptor().Template->Id == "keire.3d-starter");
    CHECK(created->Descriptor().DefaultInput);
    CHECK(created->Descriptor().StartupScene);
    CHECK(std::filesystem::exists(created->Root() / "Assets/Input/DefaultInput.keireinput"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Scenes/SampleScene.keirescene"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Shaders/DefaultUnlit.hlsl"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Shaders/DefaultUnlit.keireshader"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Materials/DefaultUnlit.keirematerial"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/Starter.keirestyle"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/StarterHud.keireui"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/StarterCard.keireui"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/StarterBindingExample.keireui"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/ScreenOverlay.keireuipanel"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/WorldTerminal.keireui"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/WorldSurface.keireuipanel"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/StarterRenderTexture.keireui"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/UI/StarterRenderTexture.keireuipanel"));
    CHECK(std::filesystem::exists(created->Root() / "Assets/Scripts/Runtime/StarterUi.cs"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Project.keireproject"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Rendering.keiresettings"));
    CHECK(std::filesystem::exists(created->Root() / "ProjectSettings/Scripting.keiresettings"));
    const auto scriptingSettings = KeireTests::ReadFile(created->Root() / "ProjectSettings/Scripting.keiresettings");
    CHECK(scriptingSettings.find(R"("sdkSelection": "bundled")") != std::string::npos);
    const auto starterSceneSource = KeireTests::ReadFile(created->Root() / "Assets/Scenes/SampleScene.keirescene");
    const std::vector<std::byte> starterSceneBytes(
        reinterpret_cast<const std::byte*>(starterSceneSource.data()),
        reinterpret_cast<const std::byte*>(starterSceneSource.data() + starterSceneSource.size()));
    const auto starterScene = Keire::SceneAsset::Decode(starterSceneBytes);
    REQUIRE(starterScene);
    CHECK(starterScene->Definition().SchemaVersion == Keire::CurrentSceneSchemaVersion);
    CHECK(std::ranges::all_of(starterScene->Definition().Objects, [](const Keire::SceneObjectDefinition& object)
                              { return object.Layer < Keire::EntityLayerCount; }));
    const auto starterLight =
        std::ranges::find(starterScene->Definition().Objects, "Directional Light", &Keire::SceneObjectDefinition::Name);
    REQUIRE(starterLight != starterScene->Definition().Objects.end());
    const auto starterLightTransform =
        Keire::Math::ComposeTransform({}, starterLight->Transform.Rotation, {1.0F, 1.0F, 1.0F});
    const auto starterLightDirection = Keire::Math::TransformDirection(starterLightTransform, {0.0F, 0.0F, 1.0F});
    CHECK(starterLightDirection.Y < -0.5F);
    const auto starterUi = std::ranges::find(starterScene->Definition().Objects, "Starter UI Document",
                                             &Keire::SceneObjectDefinition::Name);
    REQUIRE(starterUi != starterScene->Definition().Objects.end());
    CHECK(std::ranges::count(starterUi->Components, Keire::UiDocumentComponent::StaticType(),
                             &Keire::SceneComponentDefinition::Type) == 1);
    CHECK(std::ranges::count(starterUi->Components,
                             Keire::ComponentTypeId(Keire::AssetId::Parse("b1b2d001-1000-4000-8000-000000000001")),
                             &Keire::SceneComponentDefinition::Type) == 1);
    const auto starterWorldUi =
        std::ranges::find(starterScene->Definition().Objects, "Starter World UI", &Keire::SceneObjectDefinition::Name);
    REQUIRE(starterWorldUi != starterScene->Definition().Objects.end());
    CHECK(starterWorldUi->Transform.Position == Keire::Vector3{0.0F, 2.0F, 3.0F});
    CHECK(std::ranges::count(starterWorldUi->Components, Keire::UiDocumentComponent::StaticType(),
                             &Keire::SceneComponentDefinition::Type) == 1);
    CHECK(std::ranges::count(starterWorldUi->Components,
                             Keire::ComponentTypeId(Keire::AssetId::Parse("b1b2d001-1000-4000-8000-000000000002")),
                             &Keire::SceneComponentDefinition::Type) == 1);
    const auto starterRenderTextureUi = std::ranges::find(
        starterScene->Definition().Objects, "Starter UI Render Target", &Keire::SceneObjectDefinition::Name);
    REQUIRE(starterRenderTextureUi != starterScene->Definition().Objects.end());
    CHECK(std::ranges::count(starterRenderTextureUi->Components, Keire::UiDocumentComponent::StaticType(),
                             &Keire::SceneComponentDefinition::Type) == 1);
    auto rendering = Keire::LoadRenderEnvironmentSettings(created->Root());
    CHECK(rendering.AmbientIntensity == doctest::Approx(0.75F));
    rendering.AmbientColor = {0.1F, 0.2F, 0.3F, 1.0F};
    rendering.AmbientIntensity = 1.5F;
    rendering.Exposure = 1.25F;
    rendering.DirectionalShadowDistance = 250.0F;
    rendering.DirectionalShadowCascadeCount = 3;
    rendering.DirectionalShadowResolution = 4096;
    rendering.DirectionalShadowSplitLambda = 0.8F;
    CHECK_NOTHROW(Keire::ValidateRenderEnvironmentSettings(rendering));
    Keire::SaveRenderEnvironmentSettings(created->Root(), rendering);
    CHECK(Keire::LoadRenderEnvironmentSettings(created->Root()) == rendering);
    for (int revision = 1; revision <= 16; ++revision)
    {
        rendering.AmbientIntensity = static_cast<float>(revision) * 0.25F;
        Keire::SaveRenderEnvironmentSettings(created->Root(), rendering);
        CHECK(Keire::LoadRenderEnvironmentSettings(created->Root()) == rendering);
    }
    rendering.Exposure = 0.0F;
    CHECK_THROWS_AS(Keire::ValidateRenderEnvironmentSettings(rendering), std::invalid_argument);
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(created->Root(), rendering), std::invalid_argument);
    rendering.Exposure = 1.0F;
    rendering.DirectionalShadowResolution = 3000;
    CHECK_THROWS_AS(Keire::SaveRenderEnvironmentSettings(created->Root(), rendering), std::invalid_argument);
    CHECK(Keire::Project::Inspect(created->Root()) == Keire::ProjectStatus::Ready);

    auto exclusive = Keire::Project::Open(created->Root(), Keire::ProjectOpenMode::Exclusive);
    CHECK(Keire::Project::IsLocked(created->Root()));
    CHECK_THROWS_AS((void)Keire::Project::Open(created->Root(), Keire::ProjectOpenMode::Exclusive), std::runtime_error);

    auto descriptor = exclusive->Descriptor();
    descriptor.Name = "Game Renamed";
    exclusive->Save(descriptor);
    CHECK(exclusive->Descriptor().Name == "Game Renamed");

    const auto registryPath = directory.Path / "Registry/projects.json";
    auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
    registry->RecordOpened(*exclusive, "editor-stable-1");
    REQUIRE(registry->Entries().size() == 1);
    CHECK(registry->Entries().front().Status == Keire::ProjectStatus::InUse);
    CHECK(registry->Entries().front().PreferredEditorInstallation == "editor-stable-1");
    exclusive.Reset();
    registry->Refresh();
    CHECK(registry->Entries().front().Status == Keire::ProjectStatus::Ready);
    CHECK(registry->Entries().front().PreferredEditorInstallation == "editor-stable-1");
    CHECK(registry->SetPinned(descriptor.Id, true));
    CHECK(registry->Entries().front().Pinned);
    CHECK(registry->Remove(descriptor.Id));
    CHECK(registry->Entries().empty());

    CHECK_THROWS_AS((void)Keire::Project::Create({directory.Path, "../Unsafe", Keire::ProjectTemplate::Empty}),
                    std::invalid_argument);
    const auto corruptRegistry = directory.Path / "Registry/corrupt.json";
    std::filesystem::create_directories(corruptRegistry.parent_path());
    {
        std::ofstream output(corruptRegistry);
        output << "not json";
    }
    const auto recoveredRegistry = Keire::CreateRef<Keire::ProjectRegistry>(corruptRegistry);
    CHECK(recoveredRegistry->Entries().empty());
}

TEST_CASE("Project registry preserves UTF-8 paths across save and reload")
{
    TemporaryDirectory directory("ProjectUtf8Tests");
    const auto unicodeParent = directory.Path / Keire::Detail::PathFromUtf8("Kéire Projects");
    std::filesystem::create_directories(unicodeParent);
    const auto project = Keire::Project::Create({unicodeParent, "Sandbox", Keire::ProjectTemplate::Empty});
    const auto registryPath = directory.Path / "Registry/projects.json";

    {
        const auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
        registry->RecordOpened(*project);
    }

    const auto reloaded = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
    REQUIRE(reloaded->Entries().size() == 1);
    CHECK(reloaded->Entries().front().Root == project->Root());
    CHECK(reloaded->Entries().front().Status == Keire::ProjectStatus::Ready);
    CHECK_FALSE(reloaded->Entries().front().LastSavedWithEngineVersion.empty());
    const auto persisted = KeireTests::ReadFile(registryPath);
    CHECK(persisted.find(R"("schemaVersion": 2)") != std::string::npos);
    CHECK(persisted.find(R"("cachedMetadata")") != std::string::npos);
    CHECK(persisted.find(R"("projectSchemaVersion": 4)") != std::string::npos);
    CHECK(persisted.find(R"("added":)") != std::string::npos);
}

TEST_CASE("Project registry can load cached metadata without touching project folders")
{
    TemporaryDirectory directory("ProjectCachedRegistryTests");
    auto project = Keire::Project::Create({directory.Path, "Cached", Keire::ProjectTemplate::Empty});
    const auto root = project->Root();
    const auto registryPath = directory.Path / "Registry/projects.json";
    {
        const auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
        registry->RecordOpened(*project);
    }
    project.Reset();
    std::filesystem::remove_all(root);

    const auto cached =
        Keire::CreateRef<Keire::ProjectRegistry>(registryPath, Keire::ProjectRegistryLoadMode::CachedMetadata);
    REQUIRE(cached->Entries().size() == 1);
    CHECK(cached->Entries().front().Status == Keire::ProjectStatus::Ready);

    const auto refreshed = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
    REQUIRE(refreshed->Entries().size() == 1);
    CHECK(refreshed->Entries().front().Status == Keire::ProjectStatus::Missing);
}

TEST_CASE("Project registry preserves cached upgrade availability")
{
    TemporaryDirectory directory("ProjectCachedUpgradeTests");
    const auto project = Keire::Project::Create({directory.Path, "Upgradeable", Keire::ProjectTemplate::Empty});
    const auto registryPath = directory.Path / "Registry/projects.json";
    {
        const auto registry = Keire::CreateRef<Keire::ProjectRegistry>(registryPath);
        registry->RecordOpened(*project);
    }

    auto source = KeireTests::ReadFile(registryPath);
    const auto ready = source.find(R"("status": "ready")");
    REQUIRE(ready != std::string::npos);
    source.replace(ready, std::string_view(R"("status": "ready")").size(), R"("status": "upgradeAvailable")");
    {
        std::ofstream output(registryPath, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output << source;
    }

    const auto cached =
        Keire::CreateRef<Keire::ProjectRegistry>(registryPath, Keire::ProjectRegistryLoadMode::CachedMetadata);
    REQUIRE(cached->Entries().size() == 1);
    CHECK(cached->Entries().front().Status == Keire::ProjectStatus::UpgradeAvailable);
    REQUIRE(cached->SetPinned(project->Descriptor().Id, true));

    const auto reloaded =
        Keire::CreateRef<Keire::ProjectRegistry>(registryPath, Keire::ProjectRegistryLoadMode::CachedMetadata);
    REQUIRE(reloaded->Entries().size() == 1);
    CHECK(reloaded->Entries().front().Status == Keire::ProjectStatus::UpgradeAvailable);
}

TEST_CASE("Version-neutral project inspection preserves common metadata from newer schemas")
{
    TemporaryDirectory directory("ProjectFutureSchemaTests");
    auto project = Keire::Project::Create({directory.Path, "Future", Keire::ProjectTemplate::Empty});
    const auto id = project->Descriptor().Id;
    const auto root = project->Root();
    project.Reset();

    const auto marker = root / "ProjectSettings/Project.keireproject";
    auto source = KeireTests::ReadFile(marker);
    const auto schema = source.find(R"("schemaVersion": 4)");
    REQUIRE(schema != std::string::npos);
    source.replace(schema, std::string_view(R"("schemaVersion": 4)").size(), R"("schemaVersion": 99)");
    {
        std::ofstream output(marker, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output << source;
    }

    const auto inspection = Keire::Project::InspectMetadata(root);
    CHECK(inspection.Status == Keire::ProjectStatus::UnsupportedSchema);
    CHECK(inspection.SchemaVersion == 99);
    CHECK(inspection.Id == id);
    CHECK(inspection.Name == "Future");
    CHECK_FALSE(inspection.LastSavedWithEngineVersion.empty());
    CHECK_THROWS_AS((void)Keire::Project::Open(root), std::runtime_error);
}

TEST_CASE("Scene assets and mutable scenes preserve validated hierarchy ordering")
{
    const auto definition = Keire::SceneAsset::SampleDefinition();
    const auto encoded = Keire::SceneAsset::Encode(definition);
    const auto decoded = Keire::SceneAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(Keire::SceneAsset::Encode(decoded->Definition()) == encoded);

    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), definition);
    const auto root = scene->CreateObject("Root");
    REQUIRE(root);
    const auto child = scene->CreateObject("Child", root.Id());
    REQUIRE(child);
    CHECK(scene->ObjectCount() == 5);
    CHECK_THROWS_AS((void)scene->ReparentObject(root.Id(), child.Id()), std::invalid_argument);
    CHECK(scene->RenameObject(child.Id(), "Renamed"));
    const auto duplicate = scene->DuplicateObject(root.Id());
    REQUIRE(duplicate);
    CHECK(scene->ObjectCount() == 7);
    const auto duplicateChildren =
        std::ranges::count(scene->Objects(), duplicate.Id(), &Keire::SceneObjectDefinition::Parent);
    CHECK(duplicateChildren == 1);
    CHECK(scene->DestroyObject(duplicate.Id()));
    REQUIRE(child.Snapshot());
    CHECK(child.Snapshot()->Name == "Renamed");
    CHECK(scene->DestroyObject(root.Id()));
    CHECK_FALSE(child);
    CHECK(scene->ObjectCount() == 3);
    std::atomic_bool rejectedOffThread = false;
    std::jthread worker(
        [&]
        {
            try
            {
                (void)scene->CreateObject("Wrong Thread");
            }
            catch (const std::logic_error&)
            {
                rejectedOffThread = true;
            }
        });
    worker.join();
    CHECK(rejectedOffThread);
    scene->Close();
    CHECK_FALSE(root);
}

TEST_CASE("legacy mesh renderer migration preserves every material slot")
{
    const auto baseMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    const auto detailMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000002");
    auto definition = Keire::SceneAsset::EmptyDefinition("Multi Material Migration");
    definition.Objects.push_back({.Id = Keire::AssetId::Parse("20000000-0000-4000-8000-000000000001"),
                                  .Name = "Multi Material Mesh",
                                  .Components = {{Keire::MeshRendererComponent::StaticType(), 3, true,
                                                  "{\"mesh\":\"4b454952-4543-5542-454d-455348000001\",\"material\":\"" +
                                                      baseMaterial.ToString() + "\",\"material.1\":\"" +
                                                      detailMaterial.ToString() + "\",\"visible\":true}"}}});

    const auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), std::move(definition));
    const auto entity = scene->FindEntity(Keire::EntityId::Parse("20000000-0000-4000-8000-000000000001"));
    const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
    REQUIRE(renderer);
    REQUIRE(renderer->Materials().size() == 2);
    CHECK(renderer->Material(0) == baseMaterial);
    CHECK(renderer->Material(1) == detailMaterial);

    const auto objects = scene->Objects();
    REQUIRE(objects.size() == 1);
    const auto serialized = std::ranges::find(objects.front().Components, Keire::MeshRendererComponent::StaticType(),
                                              &Keire::SceneComponentDefinition::Type);
    REQUIRE(serialized != objects.front().Components.end());
    CHECK(serialized->SchemaVersion == 4);
    CHECK(serialized->Data.find("\"material.1\":\"" + detailMaterial.ToString() + "\"") != std::string::npos);
}

TEST_CASE("Scene import discovers deterministic authored and managed asset dependencies")
{
    const auto graph = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000001");
    const auto skeleton = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000002");
    const auto skinnedMesh = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000003");
    const auto avatarMask = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000004");
    const auto collisionMesh = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000005");
    const auto physicsMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000006");
    const auto clip = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000007");
    const auto mixer = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000008");
    const auto effect = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000009");
    const auto managedDirect = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000a");
    const auto managedArray = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000b");
    const auto overrideAsset = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000c");
    const auto addedEffect = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000d");
    const auto prefab = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000e");
    const auto ignoredEntity = Keire::AssetId::Parse("10000000-0000-4000-8000-00000000000f");
    const auto projectedManagedAsset = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000010");
    const auto mesh = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000011");
    const auto baseMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000012");
    const auto detailMaterial = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000013");

    const std::string managedState =
        "{\"Version\":1,\"Fields\":["
        "{\"StableId\":\"20000000-0000-4000-8000-000000000001\",\"Name\":\"Definition\","
        "\"Type\":\"Keire.AssetReference`1[[Game.Definition, Game]]\",\"Aliases\":[],\"Value\":" +
        ManagedReference(managedDirect) +
        "},"
        "{\"StableId\":\"20000000-0000-4000-8000-000000000002\",\"Name\":\"Effects\","
        "\"Type\":\"Keire.AssetReference`1[[Keire.VfxEffect, Keire.Managed]][]\",\"Aliases\":[],\"Value\":[" +
        ManagedReference(managedArray) + "," + ManagedReference(effect) +
        "]},"
        "{\"StableId\":\"20000000-0000-4000-8000-000000000003\",\"Name\":\"Target\","
        "\"Type\":\"Keire.Entity\",\"Aliases\":[],\"Value\":" +
        ManagedReference(ignoredEntity) +
        "},{\"StableId\":\"\",\"Name\":\"projectedAsset\",\"Type\":\"\",\"Aliases\":[],\"Value\":" +
        ManagedReference(projectedManagedAsset) +
        "},{\"StableId\":\"\",\"Name\":\"projectedEntity\",\"Type\":\"\",\"Aliases\":[],\"Value\":" +
        ManagedReference(ignoredEntity) + "}]}";

    auto definition = Keire::SceneAsset::EmptyDefinition("Authored Dependencies");
    Keire::SceneObjectDefinition object;
    object.Id = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000001");
    object.Name = "Authoring";
    object.Components = {
        {Keire::MeshRendererComponent::StaticType(), 3, true,
         "{\"mesh\":" + JsonString(mesh.ToString()) + ",\"material\":" + JsonString(baseMaterial.ToString()) +
             ",\"material.1\":" + JsonString(detailMaterial.ToString()) + "}"},
        {Keire::AnimatorComponent::StaticType(), 1, true,
         "{\"graph\":" + JsonString(graph.ToString()) + ",\"skeleton\":" + JsonString(skeleton.ToString()) +
             ",\"skinnedMesh\":" + JsonString(skinnedMesh.ToString()) + ",\"avatarMasks\":[" +
             JsonString(avatarMask.ToString()) + "," + JsonString(avatarMask.ToString()) + "]}"},
        {Keire::ColliderComponent::StaticType(), 2, true,
         "{\"collisionMesh\":" + JsonString(collisionMesh.ToString()) +
             ",\"physicsMaterial\":" + JsonString(physicsMaterial.ToString()) + "}"},
        {Keire::AudioSourceComponent::StaticType(), 2, true,
         "{\"clip\":" + JsonString(clip.ToString()) + ",\"mixer\":" + JsonString(mixer.ToString()) + "}"},
        {Keire::VfxEmitterComponent::StaticType(), 1, true, "{\"effect\":" + JsonString(effect.ToString()) + "}"},
        {Keire::ComponentTypeId::Parse("40000000-0000-4000-8000-000000000001"), 1, true,
         "{\"managedState\":" + JsonString(managedState) +
             ",\"projectedAsset\":" + JsonString(projectedManagedAsset.ToString()) +
             ",\"projectedEntity\":" + JsonString(ignoredEntity.ToString()) + "}"},
    };
    definition.Objects.push_back(object);
    definition.PrefabInstances.push_back(
        {.Prefab = prefab,
         .Root = object.Id,
         .Objects = {{Keire::AssetId::Parse("50000000-0000-4000-8000-000000000001"), object.Id}}});
    definition.PrefabOverrides.push_back({.Kind = Keire::PrefabOverrideKind::SetComponentProperty,
                                          .Object = object.Id,
                                          .Component = Keire::VfxEmitterComponent::StaticType(),
                                          .Property = "effect",
                                          .Value = overrideAsset});
    definition.PrefabOverrides.push_back(
        {.Kind = Keire::PrefabOverrideKind::AddComponent,
         .Object = object.Id,
         .AddedComponent = Keire::SceneComponentDefinition{Keire::VfxEmitterComponent::StaticType(), 1, true,
                                                           "{\"effect\":" + JsonString(addedEffect.ToString()) + "}"}});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    Keire::AssetImportContext context;
    context.ResolveAssetSource =
        [projectedManagedAsset](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset != projectedManagedAsset)
            return std::nullopt;
        return Keire::AssetImportSource{projectedManagedAsset,
                                        Keire::AssetTypeId::Parse("11000000-0000-4000-8000-000000000001"),
                                        "Audio/Confirm.wav"};
    };
    const auto first = importer.ContextualImport(context, Keire::SceneAsset::Encode(definition));
    const auto second = importer.ContextualImport(context, Keire::SceneAsset::Encode(definition));
    auto expected = std::vector{graph,           skeleton,      skinnedMesh, avatarMask, collisionMesh,
                                physicsMaterial, clip,          mixer,       effect,     managedDirect,
                                managedArray,    overrideAsset, addedEffect, prefab,     projectedManagedAsset};
    expected.insert(expected.end(), {mesh, baseMaterial, detailMaterial});
    std::ranges::sort(expected);
    CHECK(first.AssetDependencies == expected);
    CHECK(second.AssetDependencies == expected);
    CHECK(std::ranges::find(first.AssetDependencies, ignoredEntity) == first.AssetDependencies.end());
}

TEST_CASE("Scene reimport retains UI Document visual-tree and panel dependencies")
{
    const auto visualTree = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000020");
    const auto panelSettings = Keire::AssetId::Parse("10000000-0000-4000-8000-000000000021");
    auto definition = Keire::SceneAsset::EmptyDefinition("UI Document Dependencies");
    definition.Objects.push_back(
        {.Id = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000020"),
         .Name = "HUD",
         .Components = {{Keire::UiDocumentComponent::StaticType(), 1, true,
                         "{\"visualTree\":" + JsonString(visualTree.ToString()) + ",\"panelSettings\":" +
                             JsonString(panelSettings.ToString()) + ",\"sortingOrder\":0,\"receivesInput\":true}"}}});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    auto expected = std::vector{visualTree, panelSettings};
    std::ranges::sort(expected);
    const auto first = importer.ContextualImport({}, Keire::SceneAsset::Encode(definition));
    const auto second = importer.ContextualImport({}, Keire::SceneAsset::Encode(definition));
    CHECK(first.AssetDependencies == expected);
    CHECK(second.AssetDependencies == expected);
}

TEST_CASE("A rooted UI Document scene cook retains complete toolkit dependency closure and every panel target")
{
    TemporaryDirectory directory("SceneUiToolkitDependencyCook");
    std::filesystem::create_directories(directory.Path / "Assets");

    Keire::AssetImporterRegistration leafImporter;
    leafImporter.Name = "Test.UiResource";
    leafImporter.Type = Keire::AssetTypeId::Parse("51000000-0000-4000-8000-000000000001");
    leafImporter.Extensions = {".uiresource"};
    leafImporter.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };
    const auto visualTreeImporter = Keire::CreateUiVisualTreeAssetImporter();
    const auto styleImporter = Keire::CreateUiStyleSheetAssetImporter();
    const auto panelImporter = Keire::CreateUiPanelSettingsAssetImporter();
    Keire::AssetDatabaseSpecification specification{.ProjectRoot = directory.Path};
    specification.Importers = {leafImporter, visualTreeImporter, styleImporter, panelImporter,
                               Keire::CreateSceneAssetImporter()};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));

    const std::string leafSource = "ui resource";
    const auto image = database->CreateAsset("UI/HudImage.uiresource", leafImporter,
                                             std::as_bytes(std::span(leafSource.data(), leafSource.size())));
    const auto font = database->CreateAsset("UI/HudFont.uiresource", leafImporter,
                                            std::as_bytes(std::span(leafSource.data(), leafSource.size())));
    const auto unrelated = database->CreateAsset("UI/Unrelated.uiresource", leafImporter,
                                                 std::as_bytes(std::span(leafSource.data(), leafSource.size())));
    const std::string styleSource = "@keire-style 1;\n\n.hud { width: 100%; height: 100%; }\n";
    const auto style = database->CreateAsset("UI/Hud.keirestyle", styleImporter,
                                             std::as_bytes(std::span(styleSource.data(), styleSource.size())));

    const std::string templateSource = "<ui schemaVersion=\"1\" name=\"HudTemplate\">\n"
                                       "  <Label id=\"52000000-0000-4000-8000-000000000001\" name=\"caption\" font=\"" +
                                       font.ToString() + "\" text=\"Status\"/>\n</ui>\n";
    const auto templateTree =
        database->CreateAsset("UI/HudTemplate.keireui", visualTreeImporter,
                              std::as_bytes(std::span(templateSource.data(), templateSource.size())));
    const std::string documentSource =
        "<ui schemaVersion=\"1\" name=\"Hud\">\n  <style src=\"" + style.ToString() +
        "\"/>\n  <VisualElement id=\"52000000-0000-4000-8000-000000000002\" name=\"root\" class=\"hud\" "
        "image=\"" +
        image.ToString() + "\">\n    <TemplateContainer id=\"52000000-0000-4000-8000-000000000003\" template=\"" +
        templateTree.ToString() + "\"/>\n  </VisualElement>\n</ui>\n";
    const auto visualTree = database->CreateAsset(
        "UI/Hud.keireui", visualTreeImporter, std::as_bytes(std::span(documentSource.data(), documentSource.size())));

    const auto createPanel = [&](const std::string_view name, const Keire::UiPanelSettingsDefinition& definition)
    {
        const auto source = Keire::UiPanelSettingsAsset::Encode(definition);
        return database->CreateAsset(std::filesystem::path("UI") / (std::string(name) + ".keireuipanel"), panelImporter,
                                     source);
    };
    Keire::UiPanelSettingsDefinition screen;
    screen.Target = Keire::UiPanelTarget::ScreenOverlay;
    Keire::UiPanelSettingsDefinition camera;
    camera.Target = Keire::UiPanelTarget::CameraOverlay;
    camera.Camera = Keire::AssetId::Parse("53000000-0000-4000-8000-000000000001");
    Keire::UiPanelSettingsDefinition renderTexture;
    renderTexture.Target = Keire::UiPanelTarget::RenderTexture;
    renderTexture.RenderTexture = Keire::AssetId::Parse("53000000-0000-4000-8000-000000000002");
    Keire::UiPanelSettingsDefinition world;
    world.Target = Keire::UiPanelTarget::WorldSurface;
    world.WorldWidth = 2.0F;
    world.WorldHeight = 1.0F;
    const std::array panels{createPanel("Screen", screen), createPanel("Camera", camera),
                            createPanel("RenderTexture", renderTexture), createPanel("World", world)};

    auto sceneDefinition = Keire::SceneAsset::EmptyDefinition("UI Toolkit Cook Root");
    for (std::size_t index = 0; index < panels.size(); ++index)
    {
        sceneDefinition.Objects.push_back(
            {.Id = Keire::AssetId(0x5400000000004000ULL, index + 1),
             .Name = "UI Document " + std::to_string(index),
             .Components = {{Keire::UiDocumentComponent::StaticType(), 1, true,
                             "{\"visualTree\":" + JsonString(visualTree.ToString()) +
                                 ",\"panelSettings\":" + JsonString(panels[index].ToString()) +
                                 ",\"sortingOrder\":" + std::to_string(index) + ",\"receivesInput\":true}"}}});
    }
    const auto sceneSource = Keire::SceneAsset::Encode(sceneDefinition);
    const auto scene =
        database->CreateAsset("Scenes/UiToolkit.keirescene", Keire::CreateSceneAssetImporter(), sceneSource);

    Keire::AssetBuildProfile profile;
    profile.Strict = true;
    profile.Roots = {scene};
    const auto first = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookA");
    const auto second = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookB");
    CHECK(first.AssetCount == 10);
    CHECK(KeireTests::ReadFile(first.CatalogPath) == KeireTests::ReadFile(second.CatalogPath));
    const auto catalog = KeireTests::ReadFile(first.CatalogPath);
    const std::array closure{scene, visualTree, templateTree, style,     image,
                             font,  panels[0],  panels[1],    panels[2], panels[3]};
    for (const auto dependency : closure)
    {
        CAPTURE(dependency.ToString());
        CHECK(catalog.find(dependency.ToString()) != std::string::npos);
    }
    CHECK(catalog.find(unrelated.ToString()) == std::string::npos);
}

TEST_CASE("Scene import rejects retired Canvas UI with an exact entity and component diagnostic")
{
    auto definition = Keire::SceneAsset::EmptyDefinition("Retired UI");
    const auto entity = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000099");
    const auto retiredCanvas = Keire::ComponentTypeId::Parse("4b454952-4555-4943-414e-564153000001");
    definition.Objects.push_back({.Id = entity, .Name = "Pause Menu", .Components = {{retiredCanvas, 1, true, "{}"}}});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    const Keire::AssetImportContext context;
    CHECK_THROWS_WITH_AS(importer.ContextualImport(context, Keire::SceneAsset::Encode(definition)),
                         "Scene entity 'Pause Menu' (30000000-0000-4000-8000-000000000099) contains retired legacy "
                         "component 'Canvas' (4b454952-4555-4943-414e-564153000001). Recreate this UI as a UI "
                         "Document that references .keireui and .keireuipanel assets.",
                         std::invalid_argument);
}

TEST_CASE("Scene import rejects retired UI introduced by prefab instance overrides")
{
    auto definition = Keire::SceneAsset::EmptyDefinition("Retired Prefab UI");
    const auto entity = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000098");
    const auto source = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000097");
    const auto retiredButton = Keire::ComponentTypeId::Parse("4b454952-4555-4942-5554-544f4e000001");
    definition.Objects.push_back({.Id = entity, .Name = "Prefab Pause Menu"});
    definition.PrefabInstances.push_back(
        {.Prefab = Keire::AssetId::Parse("30000000-0000-4000-8000-000000000096"),
         .Root = entity,
         .Objects = {{source, entity}},
         .Overrides = {{.Kind = Keire::PrefabOverrideKind::AddComponent,
                        .Object = entity,
                        .AddedComponent = Keire::SceneComponentDefinition{retiredButton, 1, true, "{}"}}}});

    const auto importer = Keire::CreateSceneAssetImporter();
    REQUIRE(importer.ContextualImport);
    const Keire::AssetImportContext context;
    CHECK_THROWS_WITH_AS(importer.ContextualImport(context, Keire::SceneAsset::Encode(definition)),
                         "Scene entity 'Prefab Pause Menu' (30000000-0000-4000-8000-000000000098) contains retired "
                         "legacy component 'UI Button' (4b454952-4555-4942-5554-544f4e000001). Recreate this UI as a "
                         "UI Document that references .keireui and .keireuipanel assets.",
                         std::invalid_argument);
}

TEST_CASE("A rooted scene cook includes managed data dependency closure")
{
    TemporaryDirectory directory("SceneManagedDependencyCook");
    std::filesystem::create_directories(directory.Path / "Assets");

    const auto leafType = Keire::AssetTypeId::Parse("60000000-0000-4000-8000-000000000001");
    Keire::AssetImporterRegistration leafImporter;
    leafImporter.Name = "Test.ManagedDependencyLeaf";
    leafImporter.Type = leafType;
    leafImporter.Extensions = {".leaf"};
    leafImporter.Import = [](const std::span<const std::byte> bytes)
    { return std::vector<std::byte>(bytes.begin(), bytes.end()); };

    Keire::AssetDatabaseSpecification specification{.ProjectRoot = directory.Path};
    specification.Importers = {leafImporter, Keire::CreateManagedDataAssetImporter(),
                               Keire::CreateSceneAssetImporter()};
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
    const std::string leafSource = "managed dependency";
    const auto leaf = database->CreateAsset("Data/Dependency.leaf", leafImporter,
                                            std::as_bytes(std::span(leafSource.data(), leafSource.size())));
    const auto unrelated = database->CreateAsset("Data/Unrelated.leaf", leafImporter,
                                                 std::as_bytes(std::span(leafSource.data(), leafSource.size())));

    Keire::ManagedDataDefinition managedDefinition;
    managedDefinition.ManagedType = Keire::ManagedTypeId::Parse("70000000-0000-4000-8000-000000000001");
    managedDefinition.ManagedTypeName = "Game.SceneSettings";
    managedDefinition.Dependencies = {{.Asset = leaf, .AssetType = leafType}};
    const auto managedBytes = Keire::ManagedDataAsset::Encode(managedDefinition);
    const auto managed =
        database->CreateAsset("Data/SceneSettings.keiredata", Keire::CreateManagedDataAssetImporter(), managedBytes);

    const std::string state =
        "{\"Version\":1,\"Fields\":["
        "{\"StableId\":\"80000000-0000-4000-8000-000000000001\",\"Name\":\"Settings\","
        "\"Type\":\"Keire.AssetReference`1[[Game.SceneSettings, Game]]\",\"Aliases\":[],\"Value\":" +
        ManagedReference(managed) + "}]}";
    auto sceneDefinition = Keire::SceneAsset::EmptyDefinition("Managed Root");
    sceneDefinition.Objects.push_back(
        {.Id = Keire::AssetId::Parse("90000000-0000-4000-8000-000000000001"),
         .Name = "Root",
         .Components = {{Keire::ComponentTypeId::Parse("90000000-0000-4000-8000-000000000002"), 1, true,
                         "{\"managedState\":" + JsonString(state) + "}"}}});
    const auto sceneBytes = Keire::SceneAsset::Encode(sceneDefinition);
    const auto scene =
        database->CreateAsset("Scenes/ManagedRoot.keirescene", Keire::CreateSceneAssetImporter(), sceneBytes);

    Keire::AssetBuildProfile profile;
    profile.Roots = {scene};
    const auto first = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookA");
    const auto second = Keire::AssetCooker::Cook(*database, profile, directory.Path / "CookB");
    CHECK(first.AssetCount == 3);
    CHECK(KeireTests::ReadFile(first.CatalogPath) == KeireTests::ReadFile(second.CatalogPath));
    const auto catalog = KeireTests::ReadFile(first.CatalogPath);
    CHECK(catalog.find(scene.ToString()) != std::string::npos);
    CHECK(catalog.find(managed.ToString()) != std::string::npos);
    CHECK(catalog.find(leaf.ToString()) != std::string::npos);
    CHECK(catalog.find(unrelated.ToString()) == std::string::npos);
}

TEST_CASE("Scene entity moves preserve hierarchy order and reject invalid insertion targets")
{
    Keire::SceneDefinition definition;
    definition.Name = "Ordering";
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), std::move(definition));
    const auto first = scene->CreateEntity("First");
    const auto second = scene->CreateEntity("Second");
    const auto third = scene->CreateEntity("Third");
    const auto child = scene->CreateEntity("Child", first);

    scene->MoveEntity(third.Id(), {}, first.Id());
    auto objects = scene->Objects();
    REQUIRE(objects.size() == 4);
    CHECK(objects[0].Id == third.Id().Value());
    CHECK(objects[1].Id == first.Id().Value());
    CHECK(objects[2].Id == child.Id().Value());
    CHECK(objects[3].Id == second.Id().Value());

    scene->MoveEntity(second.Id(), first.Id(), child.Id());
    objects = scene->Objects();
    REQUIRE(objects.size() == 4);
    CHECK(objects[0].Id == third.Id().Value());
    CHECK(objects[1].Id == first.Id().Value());
    CHECK(objects[2].Id == second.Id().Value());
    CHECK(objects[3].Id == child.Id().Value());
    CHECK(objects[2].Parent == first.Id().Value());

    scene->MoveEntity(second.Id());
    objects = scene->Objects();
    CHECK(objects.back().Id == second.Id().Value());
    CHECK_FALSE(objects.back().Parent);
    CHECK_THROWS_AS(scene->MoveEntity(first.Id(), child.Id()), std::invalid_argument);
    CHECK_THROWS_AS(scene->MoveEntity(third.Id(), {}, child.Id()), std::invalid_argument);
}

TEST_CASE("Application scene system activates single and additive scene loads at frame boundaries")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneSystemTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    auto firstDefinition = Keire::SceneAsset::EmptyDefinition("First");
    auto secondDefinition = Keire::SceneAsset::EmptyDefinition("Second");
    const auto first = database->CreateAsset("First.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(firstDefinition));
    const auto second = database->CreateAsset("Second.keirescene", Keire::CreateSceneAssetImporter(),
                                              Keire::SceneAsset::Encode(secondDefinition));
    const auto catalog = database->ImportAll().CatalogPath;

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<SceneProbe>();
    probe->First = first;
    probe->Second = second;
    SceneProbeApplication application(std::move(specification), probe);
    CHECK(application.Run() == 0);
    CHECK(probe->SingleReady);
    CHECK(probe->AdditiveReady);
    CHECK(probe->ActiveChanged);
    CHECK(probe->FailedLoadPreservedActive);
    CHECK(probe->ActiveChangeEvents == 2);
}

TEST_CASE("Scene load commits remain terminal when lifecycle listeners throw or cancel")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneCommitTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto first = database->CreateAsset("First.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("First")));
    const auto second = database->CreateAsset("Second.keirescene", Keire::CreateSceneAssetImporter(),
                                              Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Second")));
    const auto catalog = database->ImportAll().CatalogPath;

    SceneCommitProbeMode mode = SceneCommitProbeMode::ThrowOnUnloaded;
    SUBCASE("unloaded listener throws") { mode = SceneCommitProbeMode::ThrowOnUnloaded; }
    SUBCASE("deferred-unload listener throws after active state commits")
    {
        mode = SceneCommitProbeMode::ThrowOnDeferredUnload;
    }
    SUBCASE("active-scene listener throws") { mode = SceneCommitProbeMode::ThrowOnActiveChanged; }
    SUBCASE("loaded listener cancels from a worker") { mode = SceneCommitProbeMode::CancelOnLoaded; }

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<SceneCommitProbe>();
    probe->First = first;
    probe->Second = second;
    probe->Mode = mode;
    SceneCommitProbeApplication application(std::move(specification), probe);

    if (mode == SceneCommitProbeMode::CancelOnLoaded)
    {
        CHECK(application.Run() == 0);
        CHECK(probe->WorkerObservedReady);
        CHECK(probe->Completed);
    }
    else
    {
        CHECK_THROWS_WITH_AS((void)application.Run(), "expected scene lifecycle listener failure", std::runtime_error);
        CHECK(probe->ListenerObservedCommittedState);
    }
    CHECK_FALSE(probe->UnexpectedTerminalState);
    CHECK_FALSE(probe->TimedOut);
}

TEST_CASE("Scene lifecycle listeners may queue loads and unloads without invalidating frame traversal")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneReentrancyTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto first = database->CreateAsset("First.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("First")));
    const auto second = database->CreateAsset("Second.keirescene", Keire::CreateSceneAssetImporter(),
                                              Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Second")));
    const auto third = database->CreateAsset("Third.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Third")));
    const auto catalog = database->ImportAll().CatalogPath;

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<SceneReentrancyProbe>();
    probe->First = first;
    probe->Second = second;
    probe->Third = third;
    SceneReentrancyProbeApplication application(std::move(specification), probe);

    CHECK(application.Run() == 0);
    CHECK(probe->LoadsQueuedDuringCallback);
    CHECK(probe->UnloadQueuedDuringCallback);
    CHECK(probe->UnloadListenerObservedCommittedState);
    CHECK(probe->ReentrantUnloadWasDeferred);
    CHECK(probe->Completed);
    CHECK_FALSE(probe->UnexpectedTerminalState);
    CHECK_FALSE(probe->TimedOut);
}

TEST_CASE("Worker cancellation wins while a scene load is still decoding")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneLoadingCancellationTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto scene = database->CreateAsset("Delayed.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Delayed")));
    const auto catalog = database->ImportAll().CatalogPath;

    auto releaseDecoder = std::make_shared<std::atomic_bool>(false);
    auto decoder = Keire::CreateSceneAssetDecoder();
    const auto decode = decoder.Decode;
    decoder.Decode = [releaseDecoder, decode](const std::span<const std::byte> bytes)
    {
        releaseDecoder->wait(false, std::memory_order_acquire);
        return decode(bytes);
    };

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Assets.Decoders.push_back(std::move(decoder));
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<SceneLoadingCancellationProbe>();
    probe->Scene = scene;
    probe->ReleaseDecoder = releaseDecoder;
    SceneLoadingCancellationProbeApplication application(std::move(specification), probe);

    CHECK(application.Run() == 0);
    CHECK(probe->ObservedLoading);
    CHECK(probe->WorkerObservedCancelled);
    CHECK(probe->NoSceneWasCommitted);
    CHECK_FALSE(probe->UnexpectedTerminalState);
    CHECK_FALSE(probe->TimedOut);
}

TEST_CASE("Runtime scene activation failure preserves the previous playable scene transactionally")
{
    UseDummyVideoDriver();
    TemporaryDirectory directory("SceneRuntimeWorldFailureTests");
    std::filesystem::create_directories(directory.Path / "Assets");
    Keire::AssetDatabaseSpecification databaseSpecification{.ProjectRoot = directory.Path};
    databaseSpecification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto first = database->CreateAsset("First.keirescene", Keire::CreateSceneAssetImporter(),
                                             Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("First")));
    const auto additive =
        database->CreateAsset("Additive.keirescene", Keire::CreateSceneAssetImporter(),
                              Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Additive")));
    const auto rejected =
        database->CreateAsset("Rejected.keirescene", Keire::CreateSceneAssetImporter(),
                              Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Rejected")));
    const auto catalog = database->ImportAll().CatalogPath;

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = catalog;
    specification.Scenes.Mode = Keire::SceneMode::Enabled;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto probe = std::make_shared<RuntimeWorldProbe>();
    probe->First = first;
    probe->Additive = additive;
    probe->Rejected = rejected;
    RuntimeWorldProbeApplication application(std::move(specification), probe);
    CHECK(application.Run() == 0);
    CHECK(probe->FirstHandle);
    CHECK(probe->AdditiveHandle);
    CHECK(probe->AdditiveActivated);
    CHECK(probe->FirstUnloaded);
    CHECK(probe->RejectedWithoutReplacingActive);
}

TEST_CASE("Newer scene importers upgrade older metadata revisions but reject future revisions")
{
    TemporaryDirectory directory("SceneImporterUpgradeTests");
    const auto sourceDirectory = directory.Path / "Assets";
    std::filesystem::create_directories(sourceDirectory);
    const auto source = sourceDirectory / "Legacy.keirescene";
    const auto metadata = sourceDirectory / "Legacy.keirescene.keiremeta";
    const auto sourceBytes = Keire::SceneAsset::Encode(Keire::SceneAsset::EmptyDefinition("Legacy"));
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(sourceBytes.data()),
                     static_cast<std::streamsize>(sourceBytes.size()));
        REQUIRE(stream.good());
    }
    const auto writeMetadata = [&](const std::uint32_t importerVersion)
    {
        std::ofstream stream(metadata, std::ios::binary | std::ios::trunc);
        stream << "{\n"
                  "  \"schemaVersion\": 1,\n"
                  "  \"id\": \"11111111-1111-4111-8111-111111111111\",\n"
                  "  \"type\": \"4b454952-4553-4345-4e45-415353455401\",\n"
                  "  \"importer\": \"Keire.Scene\",\n"
                  "  \"importerVersion\": "
               << importerVersion << ",\n  \"dependencies\": [],\n  \"subAssets\": []\n}\n";
        REQUIRE(stream.good());
    };
    writeMetadata(2);
    Keire::AssetDatabaseSpecification specification{.ProjectRoot = directory.Path};
    specification.Importers.push_back(Keire::CreateSceneAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(specification);
    CHECK_NOTHROW((void)database->ImportAll());

    writeMetadata(Keire::CreateSceneAssetImporter().Version + 1);
    database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
    CHECK_THROWS_WITH_AS((void)database->ImportAll(),
                         "No compatible importer is registered for asset: Legacy.keirescene", std::runtime_error);
}

TEST_CASE("Sandbox Shader and Material Graph gallery decodes every progressive material pairing")
{
    const auto source = KeireTests::ReadFile(std::filesystem::current_path() /
                                             "Samples/KeireSandbox/Assets/Scenes/SandboxShowcase.keirescene");
    REQUIRE_FALSE(source.empty());
    const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(source.data()),
                                       reinterpret_cast<const std::byte*>(source.data() + source.size()));
    const auto scene = Keire::SceneAsset::Decode(bytes);
    REQUIRE(scene);
    CHECK(scene->Definition().SchemaVersion == Keire::CurrentSceneSchemaVersion);

    constexpr std::array examples{
        std::pair{"01 - Studio Paint", "77c1e51e-6397-5983-b80b-e82587b2edaa"},
        std::pair{"02 - Tiled Ceramic", "295ace3d-32b9-5c8d-b2d5-f518a4af3f6c"},
        std::pair{"03 - Neon Pulse", "8b3aebee-37f7-5e6d-a096-617a4893e5b9"},
        std::pair{"04 - Procedural Cutout", "0e47b16e-4304-5c11-a66c-c716daf7f6be"},
        std::pair{"05 - Automotive Clear Coat", "78b21fbf-d81b-511f-9d6e-78df263d3652"},
        std::pair{"06 - Brushed Alloy", "3e20c25e-1348-5f09-acf2-f7fef06ca51f"},
        std::pair{"07 - Frosted Glass", "5dd2203b-4b66-53d7-a2bc-6208cd7c24c0"},
        std::pair{"08 - World-Aligned Stone", "fd8d1359-edbc-50a0-bfca-260b1686062b"},
        std::pair{"09 - Energy Dissolve", "ef1d0b19-beeb-5073-85cb-2183b5681437"},
        std::pair{"10 - Hologram Scanlines", "f6d03ea8-c948-527b-a601-a0794fa938c7"},
        std::pair{"11 - Vertex Wave", "c216fa42-86b0-568a-b021-f84dfad59a94"},
        std::pair{"12 - Iridescent Shield", "ccdad064-d9e2-5862-9fac-ff9763c347ac"},
    };
    for (const auto& [name, material] : examples)
    {
        const auto entity = std::ranges::find(scene->Definition().Objects, name, &Keire::SceneObjectDefinition::Name);
        REQUIRE(entity != scene->Definition().Objects.end());
        const auto renderer = std::ranges::find(entity->Components, Keire::MeshRendererComponent::StaticType(),
                                                &Keire::SceneComponentDefinition::Type);
        REQUIRE(renderer != entity->Components.end());
        CHECK(renderer->SchemaVersion == 3);
        CHECK(renderer->Data.find(material) != std::string::npos);
    }

    const auto plinth =
        std::ranges::find(scene->Definition().Objects, "Showcase Plinth", &Keire::SceneObjectDefinition::Name);
    REQUIRE(plinth != scene->Definition().Objects.end());
    const auto plinthRenderer = std::ranges::find(plinth->Components, Keire::MeshRendererComponent::StaticType(),
                                                  &Keire::SceneComponentDefinition::Type);
    REQUIRE(plinthRenderer != plinth->Components.end());
    CHECK(plinthRenderer->Data.find("d22ab141-adbb-53e9-a556-f07c5baf89be") != std::string::npos);
    CHECK(std::filesystem::exists(
        std::filesystem::current_path() /
        "Samples/KeireSandbox/Assets/Examples/MaterialLab/Materials/ShowcasePlinth.keirematerial"));

    const auto showcaseScript = Keire::ComponentTypeId::Parse("73616e64-626f-4078-8000-000000000060");
    std::size_t scriptedObjects = 0;
    std::size_t vfxObjects = 0;
    for (const auto& object : scene->Definition().Objects)
    {
        scriptedObjects += static_cast<std::size_t>(
            std::ranges::count(object.Components, showcaseScript, &Keire::SceneComponentDefinition::Type));
        vfxObjects += static_cast<std::size_t>(std::ranges::count(
            object.Components, Keire::VfxEmitterComponent::StaticType(), &Keire::SceneComponentDefinition::Type));
    }
    CHECK(scriptedObjects == examples.size());
    CHECK(vfxObjects == 4);
}
