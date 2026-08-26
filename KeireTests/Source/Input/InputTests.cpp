#include "KeireTests/TestSupport.h"

#include "Keire/Core.h"
#include "KeireInternal/Scripting/ManagedRuntimeInput.h"

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
        bool RawKeyHeld = false;
        bool RawKeyPressed = false;
        bool ReenablePreservedPhase = false;
        Keire::InputDeviceId CurrentKeyboard;
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
                REQUIRE(m_Context->EnableMap("Player"));
                m_Result->ReenablePreservedPhase = m_Move.Phase() == Keire::InputActionPhase::Performed;
                m_Result->SawMove = m_Move.WasPerformedThisFrame();
                const auto raw = Owner().Input()->ReadControl(Keire::InputDeviceId(1), "<Keyboard>/w");
                REQUIRE(raw);
                m_Result->RawKeyHeld = raw->IsPressed();
                m_Result->RawKeyPressed = raw->Pressed;
                m_Result->CurrentKeyboard =
                    Owner().Input()->CurrentDevice(Keire::InputDeviceType::Keyboard).value_or(Keire::InputDeviceId{});
                PushKey(false);
                return;
            }
            if (!m_AwaitingRebindRelease && m_Result->SawMove && m_Move.WasCanceledThisFrame())
            {
                m_Result->SawCancel = true;
                const auto binding = Keire::AssetId::Parse("e3afdb73-a3ec-43a7-96bb-871fe3007209");
                m_ManagedRebind = m_ManagedRebinds.Begin(Owner().Input(), m_RebindContext, binding, {});
                REQUIRE(m_ManagedRebind != 0);
                PushKey(true, SDL_SCANCODE_A);
                return;
            }
            const auto rebind = m_ManagedRebinds.Status(m_ManagedRebind);
            if (rebind && rebind->Status == Keire::ManagedInputRebindStatus::Candidate)
            {
                CHECK(rebind->CandidatePath == "<Keyboard>/a");
                REQUIRE(m_ManagedRebinds.Resolve(m_ManagedRebind, Keire::ManagedInputRebindResolution::KeepBoth));
                const auto completed = m_ManagedRebinds.Status(m_ManagedRebind);
                m_Result->Rebound = completed && completed->Status == Keire::ManagedInputRebindStatus::Completed;
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
        Keire::Detail::ManagedInputOperationStore m_ManagedRebinds;
        std::uint64_t m_ManagedRebind = 0;
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

    struct GamepadRumbleResult final
    {
        Keire::InputDeviceId Device;
        bool RumbleAccepted = false;
        bool StopAccepted = false;
        bool KeyboardRejected = false;
    };

    struct VirtualGamepad final
    {
        VirtualGamepad()
        {
            Initialized = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
            if (!Initialized)
            {
                Diagnostic = SDL_GetError();
                return;
            }
            SDL_VirtualJoystickDesc specification;
            SDL_INIT_INTERFACE(&specification);
            specification.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            specification.vendor_id = 0x045E;
            specification.product_id = 0x028E;
            specification.naxes = SDL_GAMEPAD_AXIS_COUNT;
            specification.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            specification.button_mask = (1U << SDL_GAMEPAD_BUTTON_SOUTH) | (1U << SDL_GAMEPAD_BUTTON_START) |
                                        (1U << SDL_GAMEPAD_BUTTON_LEFT_STICK) | (1U << SDL_GAMEPAD_BUTTON_RIGHT_STICK) |
                                        (1U << SDL_GAMEPAD_BUTTON_DPAD_UP) | (1U << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                                        (1U << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1U << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            specification.axis_mask = (1U << SDL_GAMEPAD_AXIS_LEFTX) | (1U << SDL_GAMEPAD_AXIS_LEFTY) |
                                      (1U << SDL_GAMEPAD_AXIS_RIGHTX) | (1U << SDL_GAMEPAD_AXIS_RIGHTY);
            specification.name = "Kéire Virtual Gamepad";
            specification.userdata = this;
            specification.Rumble = RecordRumble;
            Device = SDL_AttachVirtualJoystick(&specification);
            if (Device == 0)
                Diagnostic = SDL_GetError();
            else
            {
                Joystick = SDL_OpenJoystick(Device);
                if (!Joystick)
                    Diagnostic = SDL_GetError();
            }
        }

        ~VirtualGamepad()
        {
            if (Joystick)
                SDL_CloseJoystick(Joystick);
            if (Device != 0)
                (void)SDL_DetachVirtualJoystick(Device);
            if (Initialized)
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }

        VirtualGamepad(const VirtualGamepad&) = delete;
        VirtualGamepad& operator=(const VirtualGamepad&) = delete;

        static bool SDLCALL RecordRumble(void* context, const Uint16 low, const Uint16 high)
        {
            auto& probe = *static_cast<VirtualGamepad*>(context);
            if (probe.RumbleCalls == 0)
            {
                probe.FirstLowFrequency = low;
                probe.FirstHighFrequency = high;
            }
            ++probe.RumbleCalls;
            probe.LowFrequency = low;
            probe.HighFrequency = high;
            return true;
        }

        SDL_JoystickID Device = 0;
        SDL_Joystick* Joystick = nullptr;
        bool Initialized = false;
        std::string Diagnostic;
        std::uint32_t RumbleCalls = 0;
        std::uint16_t FirstLowFrequency = 0;
        std::uint16_t FirstHighFrequency = 0;
        std::uint16_t LowFrequency = 0;
        std::uint16_t HighFrequency = 0;
    };

    class GamepadRumbleLayer final : public Keire::Layer
    {
      public:
        explicit GamepadRumbleLayer(std::shared_ptr<GamepadRumbleResult> result)
            : Layer("GamepadRumble"), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto input = Owner().Input();
            REQUIRE(input);
            const auto devices = input->Devices();
            const auto gamepad =
                std::ranges::find_if(devices, [](const Keire::InputDeviceDescriptor& device)
                                     { return device.Type == Keire::InputDeviceType::Gamepad && device.Connected; });
            m_Result->KeyboardRejected =
                !input->SetGamepadRumble(Keire::InputDeviceId(1), 0.25F, 0.75F, Keire::TimeStep::FromSeconds(0.125));
            const auto validationDevice = gamepad == devices.end() ? Keire::InputDeviceId(1) : gamepad->Id;
            CHECK_THROWS_AS(
                (void)input->SetGamepadRumble(validationDevice, -0.1F, 0.5F, Keire::TimeStep::FromSeconds(1.0)),
                std::invalid_argument);
            CHECK_THROWS_AS(
                (void)input->SetGamepadRumble(validationDevice, 0.1F, 0.5F, Keire::TimeStep::FromSeconds(61.0)),
                std::invalid_argument);
            if (gamepad == devices.end())
            {
                Owner().RequestExit();
                return;
            }
            CHECK(gamepad->Name == "Kéire Virtual Gamepad");
            const auto user = input->CreateUser("Rumble player");
            REQUIRE(input->PairDevice(user, gamepad->Id));
            m_Result->Device = gamepad->Id;
            m_Result->RumbleAccepted =
                input->SetGamepadRumble(gamepad->Id, 0.25F, 0.75F, Keire::TimeStep::FromSeconds(0.125));
            m_Result->StopAccepted =
                input->SetGamepadRumble(gamepad->Id, 0.0F, 0.0F, Keire::TimeStep::FromSeconds(0.0));
            Owner().RequestExit();
        }

      private:
        std::shared_ptr<GamepadRumbleResult> m_Result;
    };

    class GamepadRumbleApplication final : public Keire::Application
    {
      public:
        GamepadRumbleApplication(Keire::ApplicationSpecification specification,
                                 std::shared_ptr<GamepadRumbleResult> result)
            : Application(std::move(specification)), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<GamepadRumbleLayer>(m_Result)); }

      private:
        std::shared_ptr<GamepadRumbleResult> m_Result;
    };

    struct GamepadInputResult final
    {
        bool CombinedAxes = false;
        bool OpposingDpadCanceled = false;
        bool RemainingDpadDirectionRestored = false;
        bool StickButtonWorked = false;
    };

    class GamepadInputLayer final : public Keire::Layer
    {
      public:
        GamepadInputLayer(const SDL_JoystickID device, std::shared_ptr<GamepadInputResult> result)
            : Layer("GamepadInput"), m_Device(device), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto input = Owner().Input();
            const auto devices = input->Devices();
            const auto gamepad =
                std::ranges::find_if(devices, [](const Keire::InputDeviceDescriptor& device)
                                     { return device.Type == Keire::InputDeviceType::Gamepad && device.Connected; });
            REQUIRE(gamepad != devices.end());
            const auto user = input->CreateUser("Gamepad input");
            REQUIRE(input->PairDevice(user, gamepad->Id));

            auto definition = Keire::InputActionAsset::DefaultDefinition();
            definition.Name = "GamepadInput";
            definition.ActionMaps.resize(1);
            auto& map = definition.ActionMaps.front();
            map.Actions = {
                {m_StickAction, "Stick", Keire::InputActionType::Value, Keire::InputValueType::Axis2D},
                {m_DpadAction, "Dpad", Keire::InputActionType::Value, Keire::InputValueType::Axis2D},
                {m_ClickAction, "StickClick", Keire::InputActionType::Button, Keire::InputValueType::Boolean},
            };
            map.Bindings = {
                {.Id = Keire::AssetId::Parse("3086c5ae-b065-4d9e-bcf1-cf022498f401"),
                 .Action = m_StickAction,
                 .Path = "<Gamepad>/leftStick"},
                {.Id = Keire::AssetId::Parse("572766aa-4b81-4e8b-ac41-40b817d7f402"),
                 .Action = m_DpadAction,
                 .Path = "<Gamepad>/dpad"},
                {.Id = Keire::AssetId::Parse("18cd817e-9f31-4cd6-a7c5-aacdb0bb6403"),
                 .Action = m_ClickAction,
                 .Path = "<Gamepad>/leftStickPress"},
            };
            const auto mapId = map.Id;
            m_Context = input->CreateActionContext(std::move(definition), user);
            REQUIRE(m_Context->EnableMap(mapId));
            m_Stick = m_Context->FindAction(m_StickAction);
            m_Dpad = m_Context->FindAction(m_DpadAction);
            m_Click = m_Context->FindAction(m_ClickAction);
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame++ == 0)
            {
                PushAxis(SDL_GAMEPAD_AXIS_LEFTX, 16384);
                PushAxis(SDL_GAMEPAD_AXIS_LEFTY, -8192);
                PushButton(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
                PushButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
                PushButton(SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
                return;
            }
            if (m_Frame == 2)
            {
                const auto stick = m_Stick.Value().AsAxis2D();
                const auto dpad = m_Dpad.Value().AsAxis2D();
                m_Result->CombinedAxes =
                    stick.X == doctest::Approx(16384.0F / 32767.0F) && stick.Y == doctest::Approx(8192.0F / 32767.0F);
                m_Result->OpposingDpadCanceled = dpad.X == doctest::Approx(0.0F) && dpad.Y == doctest::Approx(0.0F);
                m_Result->StickButtonWorked = m_Click.Value().AsBoolean() && m_Click.WasPerformedThisFrame();
                PushButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
                PushButton(SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
                return;
            }
            m_Result->RemainingDpadDirectionRestored = m_Dpad.Value().AsAxis2D().Y > 0.9F;
            Owner().RequestExit();
        }

      private:
        void PushAxis(const SDL_GamepadAxis axis, const std::int16_t value) const
        {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            event.gaxis.timestamp = SDL_GetTicksNS();
            event.gaxis.which = m_Device;
            event.gaxis.axis = static_cast<std::uint8_t>(axis);
            event.gaxis.value = value;
            REQUIRE(SDL_PushEvent(&event));
        }

        void PushButton(const SDL_GamepadButton button, const bool down) const
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
            event.gbutton.timestamp = SDL_GetTicksNS();
            event.gbutton.which = m_Device;
            event.gbutton.button = static_cast<std::uint8_t>(button);
            event.gbutton.down = down;
            REQUIRE(SDL_PushEvent(&event));
        }

        SDL_JoystickID m_Device = 0;
        std::shared_ptr<GamepadInputResult> m_Result;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::InputActionHandle m_Stick;
        Keire::InputActionHandle m_Dpad;
        Keire::InputActionHandle m_Click;
        Keire::AssetId m_StickAction = Keire::AssetId::Parse("942b94c3-aa3d-475d-bd0a-6967dcc7f401");
        Keire::AssetId m_DpadAction = Keire::AssetId::Parse("dc22428d-ae41-4ba5-98ff-f13dc520a402");
        Keire::AssetId m_ClickAction = Keire::AssetId::Parse("af31e0a1-8baa-40ae-8d41-88806e173403");
        std::uint32_t m_Frame = 0;
    };

    class GamepadInputApplication final : public Keire::Application
    {
      public:
        GamepadInputApplication(Keire::ApplicationSpecification specification, const SDL_JoystickID device,
                                std::shared_ptr<GamepadInputResult> result)
            : Application(std::move(specification)), m_Device(device), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<GamepadInputLayer>(m_Device, m_Result)); }

      private:
        SDL_JoystickID m_Device = 0;
        std::shared_ptr<GamepadInputResult> m_Result;
    };

    struct InputLifecycleResult final
    {
        bool AxisCompositeWorked = false;
        bool WheelPulseWorked = false;
        bool ValueHoldWaited = false;
        bool FocusReleaseWorked = false;
        bool InvalidDefinitionRejected = false;
        bool InvalidDeviceRejected = false;
        bool ClosedObjectsInert = false;
    };

    class InputLifecycleLayer final : public Keire::Layer
    {
      public:
        explicit InputLifecycleLayer(std::shared_ptr<InputLifecycleResult> result)
            : Layer("InputLifecycle"), m_Result(std::move(result))
        {
        }

      protected:
        void OnAttach() override
        {
            auto definition = Definition();
            const auto input = Owner().Input();
            REQUIRE(input);
            const auto user = input->CreateUser("Lifecycle input");
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(1)));
            REQUIRE(input->PairDevice(user, Keire::InputDeviceId(2)));
            m_Context = input->CreateActionContext(definition, user);
            REQUIRE(m_Context->EnableMap(m_Map));
            m_Axis = m_Context->FindAction(m_AxisAction);
            m_Wheel = m_Context->FindAction(m_WheelAction);
            m_Hold = m_Context->FindAction(m_HoldAction);
            REQUIRE(m_Axis);
            REQUIRE(m_Wheel);
            REQUIRE(m_Hold);
            m_Subscription = m_Context->Subscribe(m_AxisAction, [](const Keire::InputActionEvent&) {});
            m_Capture = m_Context->OverrideUiCapture(m_Map);

            definition.ActionMaps.front().Bindings.erase(definition.ActionMaps.front().Bindings.begin() + 2);
            try
            {
                (void)input->CreateActionContext(std::move(definition), user);
            }
            catch (const std::invalid_argument&)
            {
                m_Result->InvalidDefinitionRejected = true;
            }
            m_Result->InvalidDeviceRejected =
                !input->CurrentDevice(static_cast<Keire::InputDeviceType>(255)).has_value();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Frame++ == 0)
            {
                PushKey(SDL_SCANCODE_D, true);
                PushKey(SDL_SCANCODE_W, true);
                SDL_Event wheel{};
                wheel.type = SDL_EVENT_MOUSE_WHEEL;
                wheel.wheel.timestamp = SDL_GetTicksNS();
                wheel.wheel.y = 1.0F;
                REQUIRE(SDL_PushEvent(&wheel));
                return;
            }
            if (m_Frame == 2)
            {
                m_Result->AxisCompositeWorked =
                    m_Axis.Value().AsAxis1D() > 0.9F && m_Axis.Phase() == Keire::InputActionPhase::Performed;
                m_Result->WheelPulseWorked = m_Wheel.WasStartedThisFrame() && m_Wheel.WasPerformedThisFrame() &&
                                             m_Wheel.WasCanceledThisFrame() && !m_Wheel.Value().AsBoolean();
                m_Result->ValueHoldWaited = m_Hold.WasStartedThisFrame() && !m_Hold.WasPerformedThisFrame() &&
                                            m_Hold.Phase() == Keire::InputActionPhase::Started;
                REQUIRE(m_Context->EnableMap(m_Map));
                CHECK(m_Axis.Phase() == Keire::InputActionPhase::Performed);
                SDL_Event focus{};
                focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                focus.common.timestamp = SDL_GetTicksNS();
                REQUIRE(SDL_PushEvent(&focus));
                return;
            }

            const auto raw = Owner().Input()->ReadControl(Keire::InputDeviceId(1), "<Keyboard>/d");
            m_Result->FocusReleaseWorked = raw && raw->Released && m_Axis.WasCanceledThisFrame();
            Owner().Input()->Close();
            m_Result->ClosedObjectsInert = !m_Axis && m_Axis.Phase() == Keire::InputActionPhase::Disabled &&
                                           m_Axis.Value().Magnitude() == 0.0F && !m_Context->MapEnabled(m_Map) &&
                                           !m_Context->ActionEnabled(m_AxisAction) && !m_Subscription.Connected() &&
                                           !m_Capture.Active();
            Owner().RequestExit();
        }

      private:
        [[nodiscard]] Keire::InputActionAssetDefinition Definition()
        {
            auto definition = Keire::InputActionAsset::DefaultDefinition();
            definition.Name = "LifecycleInput";
            definition.ActionMaps.resize(1);
            auto& map = definition.ActionMaps.front();
            map.Id = m_Map;
            map.Name = "Gameplay";
            map.Actions = {
                {m_AxisAction, "Throttle", Keire::InputActionType::Value, Keire::InputValueType::Axis1D},
                {m_WheelAction, "Next", Keire::InputActionType::Button, Keire::InputValueType::Boolean},
                {m_HoldAction,
                 "Charge",
                 Keire::InputActionType::Value,
                 Keire::InputValueType::Axis1D,
                 {{"Hold", {{"duration", 10.0}}}}},
            };
            map.Bindings = {
                {.Id = Keire::AssetId::Parse("2da89924-2915-4afc-85e9-d0604bb5c301"),
                 .Action = m_AxisAction,
                 .Name = "Keyboard axis",
                 .Composite = "Axis1D"},
                {.Id = Keire::AssetId::Parse("8b6adf81-cb27-4103-b9ac-411ba5d1c302"),
                 .Action = m_AxisAction,
                 .Path = "<Keyboard>/a",
                 .CompositePart = "Negative"},
                {.Id = Keire::AssetId::Parse("eece7df3-08ba-4c5d-9e61-24e2b8d68303"),
                 .Action = m_AxisAction,
                 .Path = "<Keyboard>/d",
                 .CompositePart = "Positive"},
                {.Id = Keire::AssetId::Parse("d716bbfc-217f-481f-aad1-aa65ddb40304"),
                 .Action = m_WheelAction,
                 .Path = "<Mouse>/wheelUp"},
                {.Id = Keire::AssetId::Parse("f711a1a9-80d8-450f-bcf8-736c54402305"),
                 .Action = m_HoldAction,
                 .Path = "<Keyboard>/w"},
            };
            return definition;
        }

        static void PushKey(const SDL_Scancode scancode, const bool down)
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.timestamp = SDL_GetTicksNS();
            event.key.scancode = scancode;
            event.key.down = down;
            REQUIRE(SDL_PushEvent(&event));
        }

        std::shared_ptr<InputLifecycleResult> m_Result;
        Keire::Ref<Keire::InputActionContext> m_Context;
        Keire::InputActionHandle m_Axis;
        Keire::InputActionHandle m_Wheel;
        Keire::InputActionHandle m_Hold;
        Keire::InputActionSubscription m_Subscription;
        Keire::InputCaptureOverride m_Capture;
        Keire::AssetId m_Map = Keire::AssetId::Parse("86cf4892-273f-42e9-a5e4-82450e9d6301");
        Keire::AssetId m_AxisAction = Keire::AssetId::Parse("030b25de-ec1e-4e19-ac45-75a85a25c301");
        Keire::AssetId m_WheelAction = Keire::AssetId::Parse("fba5499e-6577-469e-b73b-c19e45d82302");
        Keire::AssetId m_HoldAction = Keire::AssetId::Parse("174768da-d0f0-4670-b00b-a2953784a303");
        std::uint32_t m_Frame = 0;
    };

    class InputLifecycleApplication final : public Keire::Application
    {
      public:
        InputLifecycleApplication(Keire::ApplicationSpecification specification,
                                  std::shared_ptr<InputLifecycleResult> result)
            : Application(std::move(specification)), m_Result(std::move(result))
        {
        }

      protected:
        void OnInitialize() override { (void)PushLayer(std::make_unique<InputLifecycleLayer>(m_Result)); }

      private:
        std::shared_ptr<InputLifecycleResult> m_Result;
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
    invalid = definition;
    invalid.ActionMaps[0].Actions[0].Interactions = {{"Tap", {{"pressPoint", 1.1}}}};
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
    CHECK(result->RawKeyHeld);
    CHECK(result->RawKeyPressed);
    CHECK(result->ReenablePreservedPhase);
    CHECK(result->CurrentKeyboard == Keire::InputDeviceId(1));
}

TEST_CASE("Input lifecycle preserves map state and exposes complete authored controls")
{
    UseDummyVideoDriver();
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Input lifecycle test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<InputLifecycleResult>();
    InputLifecycleApplication application(std::move(specification), result);
    CHECK(application.Run() == 0);
    CHECK(result->AxisCompositeWorked);
    CHECK(result->WheelPulseWorked);
    CHECK(result->ValueHoldWaited);
    CHECK(result->FocusReleaseWorked);
    CHECK(result->InvalidDefinitionRejected);
    CHECK(result->InvalidDeviceRejected);
    CHECK(result->ClosedObjectsInert);
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

TEST_CASE("Input gamepad rumble validates requests and reaches the owned native device")
{
    UseDummyVideoDriver();
    VirtualGamepad gamepad;
    REQUIRE_MESSAGE(gamepad.Initialized, gamepad.Diagnostic);
    REQUIRE_MESSAGE(gamepad.Device != 0, gamepad.Diagnostic);
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Gamepad rumble test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<GamepadRumbleResult>();
    GamepadRumbleApplication application(std::move(specification), result);
    CHECK(application.Run() == 0);
    CHECK(result->KeyboardRejected);
    CHECK(result->Device);
    CHECK(result->RumbleAccepted);
    CHECK(result->StopAccepted);
    CHECK(gamepad.RumbleCalls == 2);
    CHECK(gamepad.FirstLowFrequency == 16384);
    CHECK(gamepad.FirstHighFrequency == 49151);
    CHECK(gamepad.LowFrequency == 0);
    CHECK(gamepad.HighFrequency == 0);
}

TEST_CASE("Input combines same-frame gamepad axes and preserves opposing D-pad state")
{
    UseDummyVideoDriver();
    VirtualGamepad gamepad;
    REQUIRE_MESSAGE(gamepad.Initialized, gamepad.Diagnostic);
    REQUIRE_MESSAGE(gamepad.Device != 0, gamepad.Diagnostic);
    REQUIRE_MESSAGE(gamepad.Joystick != nullptr, gamepad.Diagnostic);
    InputProject project;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Title = "Gamepad input test";
    specification.MainWindow.Visible = false;
    specification.Assets.Mode = Keire::AssetMode::Development;
    specification.Assets.DevelopmentCatalog = project.Catalog;
    specification.Input.Mode = Keire::InputMode::Enabled;
    specification.Input.AutoJoin = false;
    specification.Ui.Mode = Keire::UiMode::Disabled;
    specification.ManageLogging = false;
    specification.TargetFrameRate = 240;

    auto result = std::make_shared<GamepadInputResult>();
    GamepadInputApplication application(std::move(specification), gamepad.Device, result);
    CHECK(application.Run() == 0);
    CHECK(result->CombinedAxes);
    CHECK(result->OpposingDpadCanceled);
    CHECK(result->RemainingDpadDirectionRestored);
    CHECK(result->StickButtonWorked);
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
