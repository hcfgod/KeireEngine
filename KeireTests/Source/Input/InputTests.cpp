#include "KeireTests/TestSupport.h"

#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
    void UseDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    struct InputProject final
    {
        InputProject() : Root(KeireTests::MakeTestDirectory("InputTests"))
        {
            std::filesystem::create_directories(Root / "Assets");
            Keire::AssetDatabaseSpecification specification{.ProjectRoot = Root};
            specification.Importers.push_back(Keire::CreateInputActionAssetImporter());
            Database = Keire::CreateRef<Keire::AssetDatabase>(std::move(specification));
            const auto bytes = Keire::InputActionAsset::Encode(Keire::InputActionAsset::DefaultDefinition());
            Asset = Database->CreateAsset("TestInput.keireinput", Keire::CreateInputActionAssetImporter(), bytes);
            Catalog = Database->ImportAll().CatalogPath;
        }

        ~InputProject()
        {
            Database.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
        Keire::Ref<Keire::AssetDatabase> Database;
        Keire::AssetId Asset;
        std::filesystem::path Catalog;
    };

    struct InputProbeResult
    {
        bool SawMove = false;
        bool SawCancel = false;
        bool MapEnabled = false;
        bool EventPushed = false;
        float LastY = 0.0F;
        Keire::InputActionPhase LastPhase = Keire::InputActionPhase::Disabled;
        bool ExclusivePairing = false;
        bool Rebound = false;
        bool CaptureOverrideActive = false;
        std::size_t ReloadedOverrides = 0;
        int PerformedCallbacks = 0;
    };

    class InputProbeLayer final : public Keire::Layer
    {
      public:
        InputProbeLayer(const Keire::AssetId asset, std::shared_ptr<InputProbeResult> result)
            : Layer("InputProbe"), m_Asset(asset), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto input = Owner().Input();
            REQUIRE(input);
            const auto user = input->CreateUser("Test Player");
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(1)));
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(2)));
            const auto second = input->CreateUser("Second Player");
            m_Result->ExclusivePairing = !input->PairDevice(second, Keire::InputDeviceId(1));
            REQUIRE(input->SetControlScheme(user, "KeyboardMouse"));
            REQUIRE(input->ClearControlSchemeLock(user));
            REQUIRE(input->RemoveUser(second));
            m_Context = input->CreateActionContext(m_Asset, user);
            m_RebindContext = input->CreateActionContext(Keire::InputActionAsset::DefaultDefinition(), user);
            CHECK(m_RebindContext->User() == user);
            CHECK_FALSE(m_RebindContext->Asset());
        }

        void OnUpdate(const Keire::Time&) override
        {
            ++m_Update;
            if (!m_Move)
            {
                if (!m_Context->EnableMap("Player"))
                    return;
                m_Result->MapEnabled = true;
                m_CaptureOverride =
                    m_Context->OverrideUiCapture(Keire::InputActionAsset::DefaultDefinition().ActionMaps.front().Id);
                m_Result->CaptureOverrideActive = m_CaptureOverride.Active();
                CHECK_THROWS_AS((void)m_Context->OverrideUiCapture(
                                    Keire::InputActionAsset::DefaultDefinition().ActionMaps.front().Id),
                                std::logic_error);
                m_Move = m_Context->FindAction("Player", "Move");
                if (!m_Move)
                    return;
                m_Subscription = m_Context->Subscribe(m_Move.Id(),
                                                      [this](const Keire::InputActionEvent& event)
                                                      {
                                                          if (event.Phase == Keire::InputActionPhase::Performed)
                                                              ++m_Result->PerformedCallbacks;
                                                      });
                PushKey(true);
                m_Result->EventPushed = true;
                return;
            }

            m_Result->LastY = m_Move.Value().AsAxis2D().Y;
            m_Result->LastPhase = m_Move.Phase();

            if (!m_Result->SawMove && m_Move.Value().AsAxis2D().Y > 0.9F)
            {
                m_Result->SawMove = m_Move.WasPerformedThisFrame();
                PushKey(false);
                return;
            }
            if (!m_AwaitingRebindRelease && m_Result->SawMove && m_Move.WasCanceledThisFrame())
            {
                m_Result->SawCancel = true;
                const auto binding = Keire::AssetId::Parse("e3afdb73-a3ec-43a7-96bb-871fe3007209");
                m_Rebind = Owner().Input()->BeginInteractiveRebind(m_RebindContext, binding);
                PushKey(true, SDL_SCANCODE_A);
                return;
            }
            if (m_Rebind && m_Rebind->Status() == Keire::RebindStatus::Candidate)
            {
                CHECK(m_Rebind->CandidatePath() == "<Keyboard>/a");
                m_Rebind->Apply(Keire::RebindConflictResolution::KeepBoth);
                m_Result->Rebound = m_Rebind->Status() == Keire::RebindStatus::Completed;
                m_RebindContext->SaveBindingOverrides("TestProfile");
                m_RebindContext->ClearBindingOverrides();
                m_Result->ReloadedOverrides = m_RebindContext->LoadBindingOverrides("TestProfile");
                PushKey(false, SDL_SCANCODE_A);
                m_AwaitingRebindRelease = true;
                return;
            }
            if (m_AwaitingRebindRelease && m_Move.WasCanceledThisFrame())
            {
                Owner().RequestExit();
                return;
            }
            if (m_Update > 600)
                Owner().RequestExit(2);
        }

      private:
        static void PushKey(const bool down, const SDL_Scancode scancode = SDL_SCANCODE_W)
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.timestamp = SDL_GetTicksNS();
            event.key.scancode = scancode;
            event.key.down = down;
            REQUIRE(SDL_PushEvent(&event));
        }

        Keire::AssetId m_Asset;
        std::shared_ptr<InputProbeResult> m_Result;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::Ref<Keire::InputActionContext> m_RebindContext;
        Keire::InputActionHandle m_Move;
        Keire::InputActionSubscription m_Subscription;
        Keire::InputCaptureOverride m_CaptureOverride;
        Keire::Ref<Keire::InteractiveRebindOperation> m_Rebind;
        bool m_AwaitingRebindRelease = false;
        int m_Update = 0;
    };

    class InputProbeApplication final : public Keire::Application
    {
      public:
        InputProbeApplication(Keire::ApplicationSpecification specification, const Keire::AssetId asset,
                              std::shared_ptr<InputProbeResult> result)
            : Application(std::move(specification)), m_Asset(asset), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<InputProbeLayer>(m_Asset, m_Result)); }

      private:
        Keire::AssetId m_Asset;
        std::shared_ptr<InputProbeResult> m_Result;
    };

    struct RelativeInputResult final
    {
        Keire::InputValue First;
        Keire::InputValue Second;
    };

    class RelativeInputLayer final : public Keire::Layer
    {
      public:
        explicit RelativeInputLayer(std::shared_ptr<RelativeInputResult> result)
            : Layer("RelativeInput"), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            auto definition = Keire::InputActionAsset::DefaultDefinition();
            auto player = std::ranges::find(definition.ActionMaps, "Player", &Keire::InputActionMapDefinition::Name);
            REQUIRE(player != definition.ActionMaps.end());
            player->Actions.clear();
            player->Bindings.clear();
            m_Action = Keire::AssetId::Parse("bf8d1589-63c8-4db9-9a82-f4ca9cb167e7");
            player->Actions.push_back(
                {m_Action, "Look", Keire::InputActionType::PassThrough, Keire::InputValueType::Axis2D});
            player->Bindings.push_back({.Id = Keire::AssetId::Parse("bda05628-fc40-451a-a425-615defa8dc89"),
                                        .Action = m_Action,
                                        .Name = "Mouse delta",
                                        .Path = "<Mouse>/delta"});
            const auto input = Owner().Input();
            const auto user = input->CreateUser("Relative input");
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(2)));
            m_Context = input->CreateActionContext(std::move(definition), user);
            REQUIRE(m_Context->EnableMap("Player"));
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame++ == 0)
            {
                PushMotion(2.0F, -3.0F);
                PushMotion(4.0F, 1.0F);
                return;
            }
            const auto first = Owner().Input()->CaptureFixedTick(10);
            const auto second = Owner().Input()->CaptureFixedTick(11);
            const auto firstAction = std::ranges::find(first.Actions, m_Action, &Keire::FixedTickInputAction::Action);
            const auto secondAction = std::ranges::find(second.Actions, m_Action, &Keire::FixedTickInputAction::Action);
            REQUIRE(firstAction != first.Actions.end());
            REQUIRE(secondAction != second.Actions.end());
            m_Result->First = firstAction->Value;
            m_Result->Second = secondAction->Value;
            Owner().RequestExit();
        }

      private:
        static void PushMotion(const float x, const float y)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.timestamp = SDL_GetTicksNS();
            event.motion.xrel = x;
            event.motion.yrel = y;
            REQUIRE(SDL_PushEvent(&event));
        }

        std::shared_ptr<RelativeInputResult> m_Result;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::AssetId m_Action;
        std::uint32_t m_Frame = 0;
    };

    class RelativeInputApplication final : public Keire::Application
    {
      public:
        RelativeInputApplication(Keire::ApplicationSpecification specification,
                                 std::shared_ptr<RelativeInputResult> result)
            : Application(std::move(specification)), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<RelativeInputLayer>(m_Result)); }

      private:
        std::shared_ptr<RelativeInputResult> m_Result;
    };

    struct MouseTapResult final
    {
        bool Started = false;
        bool Performed = false;
        bool Canceled = false;
        bool Held = true;
    };

    class MouseTapLayer final : public Keire::Layer
    {
      public:
        explicit MouseTapLayer(std::shared_ptr<MouseTapResult> result) : Layer("MouseTap"), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto input = Owner().Input();
            REQUIRE(input);
            const auto user = input->CreateUser("Mouse tap");
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(2)));
            m_Context = input->CreateActionContext(Keire::InputActionAsset::DefaultDefinition(), user);
            REQUIRE(m_Context->EnableMap("Player"));
            m_Fire = m_Context->FindAction("Player", "Fire");
            REQUIRE(m_Fire);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame++ == 0)
            {
                PushButton(true);
                PushButton(false);
                return;
            }
            m_Result->Started = m_Fire.WasStartedThisFrame();
            m_Result->Performed = m_Fire.WasPerformedThisFrame();
            m_Result->Canceled = m_Fire.WasCanceledThisFrame();
            m_Result->Held = m_Fire.Value().AsBoolean();
            Owner().RequestExit();
        }

      private:
        static void PushButton(const bool down)
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.timestamp = SDL_GetTicksNS();
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = down;
            REQUIRE(SDL_PushEvent(&event));
        }

        std::shared_ptr<MouseTapResult> m_Result;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::InputActionHandle m_Fire;
        std::uint32_t m_Frame = 0;
    };

    class MouseTapApplication final : public Keire::Application
    {
      public:
        MouseTapApplication(Keire::ApplicationSpecification specification, std::shared_ptr<MouseTapResult> result)
            : Application(std::move(specification)), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<MouseTapLayer>(m_Result)); }

      private:
        std::shared_ptr<MouseTapResult> m_Result;
    };

    struct MixedDeviceInputResult final
    {
        bool BeganWithGamepadScheme = false;
        bool SwitchedToKeyboardMouse = false;
        bool FireHeld = false;
        bool FirePerformed = false;
    };

    class MixedDeviceInputLayer final : public Keire::Layer
    {
      public:
        explicit MixedDeviceInputLayer(std::shared_ptr<MixedDeviceInputResult> result)
            : Layer("MixedDeviceInput"), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto input = Owner().Input();
            REQUIRE(input);
            m_User = input->CreateUser("Standalone player");
            REQUIRE(input->PairDevice(m_User, Keire::InputDeviceId(1)));
            REQUIRE(input->PairDevice(m_User, Keire::InputDeviceId(2)));
            REQUIRE(input->SetControlScheme(m_User, "Gamepad", false));
            m_Context = input->CreateActionContext(Keire::InputActionAsset::DefaultDefinition(), m_User);
            REQUIRE(m_Context->EnableMap("Player"));
            m_Fire = m_Context->FindAction("Player", "Fire");
            REQUIRE(m_Fire);

            const auto users = input->Users();
            const auto user = std::ranges::find(users, m_User, &Keire::InputUserDescriptor::Id);
            REQUIRE(user != users.end());
            m_Result->BeganWithGamepadScheme = user->ControlScheme == "Gamepad";
            PushButton(true);
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto input = Owner().Input();
            m_Result->FireHeld = m_Fire.Value().AsBoolean();
            m_Result->FirePerformed = m_Fire.WasPerformedThisFrame();
            const auto users = input->Users();
            const auto user = std::ranges::find(users, m_User, &Keire::InputUserDescriptor::Id);
            REQUIRE(user != users.end());
            m_Result->SwitchedToKeyboardMouse = user->ControlScheme == "KeyboardMouse";
            PushButton(false);
            Owner().RequestExit();
        }

      private:
        static void PushButton(const bool down)
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.timestamp = SDL_GetTicksNS();
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = down;
            REQUIRE(SDL_PushEvent(&event));
        }

        std::shared_ptr<MixedDeviceInputResult> m_Result;
        Keire::InputUserId m_User;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::InputActionHandle m_Fire;
    };

    class MixedDeviceInputApplication final : public Keire::Application
    {
      public:
        MixedDeviceInputApplication(Keire::ApplicationSpecification specification,
                                    std::shared_ptr<MixedDeviceInputResult> result)
            : Application(std::move(specification)), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<MixedDeviceInputLayer>(m_Result)); }

      private:
        std::shared_ptr<MixedDeviceInputResult> m_Result;
    };
} // namespace

TEST_CASE("Input action assets validate and serialize canonically")
{
    const auto definition = Keire::InputActionAsset::DefaultDefinition();
    CHECK_NOTHROW(Keire::InputActionAsset::Validate(definition));
    const auto first = Keire::InputActionAsset::Encode(definition);
    const auto decoded = Keire::InputActionAsset::Decode(first);
    REQUIRE(decoded);
    CHECK(decoded->Definition().Name == "DefaultInput");
    CHECK(decoded->FindMap("Player") != nullptr);
    CHECK(decoded->FindMap("UI") != nullptr);
    CHECK(Keire::InputActionAsset::Encode(decoded->Definition()) == first);

    auto invalid = definition;
    invalid.ActionMaps[1].Id = invalid.ActionMaps[0].Id;
    CHECK_THROWS_AS(Keire::InputActionAsset::Validate(invalid), std::invalid_argument);
    invalid = definition;
    invalid.ActionMaps[0].Bindings[1].Path = "keyboard/w";
    CHECK_THROWS_AS(Keire::InputActionAsset::Validate(invalid), std::invalid_argument);

    const auto fallback = Keire::CreateRef<Keire::InputActionAsset>();
    CHECK(fallback->Definition().ActionMaps.empty());
    CHECK(fallback->ResidentBytes() == 0);
}

TEST_CASE("Input importer creates typed deterministic assets")
{
    InputProject project;
    const auto record = project.Database->Find(project.Asset);
    REQUIRE(record);
    CHECK(record->Type == Keire::InputActionAsset::StaticType());
    CHECK(record->Importer == "Keire.InputActions");
    CHECK_NOTHROW(Keire::AssetCooker::Validate(project.Catalog));
    const auto cached = project.Database->ImportAll();
    CHECK(cached.Imported == 0);
    CHECK(cached.CacheHits == 1);
}

TEST_CASE("Application input processes one immutable action snapshot per outer frame")
{
    UseDummyVideoDriver();
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Input test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Input.BindingOverrideDirectory = project.Root / "Preferences/Input";
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<InputProbeResult>();
    InputProbeApplication application(std::move(specification), project.Asset, result);
    CHECK(application.Run() == 0);
    CHECK(result->SawMove);
    CHECK(result->SawCancel);
    CHECK(result->PerformedCallbacks == 2);
    CHECK(result->MapEnabled);
    CHECK(result->EventPushed);
    CHECK(result->LastY == doctest::Approx(0.0F));
    CHECK(result->LastPhase == Keire::InputActionPhase::Canceled);
    CHECK(result->ExclusivePairing);
    CHECK(result->Rebound);
    CHECK(result->ReloadedOverrides == 1);
    CHECK(result->CaptureOverrideActive);
}

TEST_CASE("Input preserves mouse button taps that begin and end within one action snapshot")
{
    UseDummyVideoDriver();
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Mouse tap input test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<MouseTapResult>();
    MouseTapApplication application(std::move(specification), result);
    CHECK(application.Run() == 0);
    CHECK(result->Started);
    CHECK(result->Performed);
    CHECK(result->Canceled);
    CHECK_FALSE(result->Held);
}

TEST_CASE("Input switches a mixed-device standalone user to the active mouse control scheme")
{
    UseDummyVideoDriver();
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Mixed-device input test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<MixedDeviceInputResult>();
    MixedDeviceInputApplication application(std::move(specification), result);
    CHECK(application.Run() == 0);
    CHECK(result->BeganWithGamepadScheme);
    CHECK(result->SwitchedToKeyboardMouse);
    CHECK(result->FireHeld);
    CHECK(result->FirePerformed);
}

TEST_CASE("Application rejects enabled input without assets")
{
    UseDummyVideoDriver();
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.ManageLogging = false;
    Keire::Application application(specification);
    CHECK_THROWS_AS((void)application.Run(), std::invalid_argument);
}

TEST_CASE("Fixed tick input accumulates relative deltas and consumes them once")
{
    UseDummyVideoDriver();
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.Timing.FixedDeltaTime = Keire::TimeStep::FromSeconds(10.0);
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;
    auto result = std::make_shared<RelativeInputResult>();
    RelativeInputApplication application(std::move(specification), result);
    CHECK(application.Run() == 0);
    CHECK(result->First.X == doctest::Approx(6.0F));
    CHECK(result->First.Y == doctest::Approx(-2.0F));
    CHECK(result->Second.X == doctest::Approx(0.0F));
    CHECK(result->Second.Y == doctest::Approx(0.0F));
}
