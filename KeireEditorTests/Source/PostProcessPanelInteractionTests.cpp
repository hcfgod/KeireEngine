#include "Keire/Core.h"
#include "KeireClient/Editor/PostProcessPanel.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
    enum class GestureAction
    {
        Undo,
        Hide,
        Collapse,
        ClearLookup
    };

    enum class CreationAction
    {
        None,
        Cancel,
        Escape,
        Submit,
        Reject
    };

    class PostProcessInteractionLayer final : public Keire::Layer, public KeireEditor::IPostProcessPanelController
    {
      public:
        PostProcessInteractionLayer(const GestureAction gestureAction, bool& checked,
                                    const CreationAction creationDialog = CreationAction::None)
            : Layer("Post process interaction"), m_GestureAction(gestureAction), m_Checked(checked),
              m_CreationDialog(creationDialog), m_Panel(*this)
        {
        }

        KeireEditor::PostProcessDocument& PostProcessState() noexcept override { return m_Document; }

        Keire::Ref<Keire::AssetDatabase> PostProcessDatabase() const noexcept override { return {}; }

        void ActivatePostProcessHistory() noexcept override {}

        void OpenPostProcessProfile(Keire::AssetId) override { FAIL("Unexpected profile selection"); }

        void CreatePostProcessProfile(const std::string_view name) override
        {
            const bool expectsCreation =
                m_CreationDialog == CreationAction::Submit || m_CreationDialog == CreationAction::Reject;
            REQUIRE(expectsCreation);
            CHECK(name == "Post Processing Profile");
            ++m_CreationRequests;
            if (m_CreationDialog == CreationAction::Reject)
            {
                throw std::runtime_error("A profile with this name already exists.");
            }
        }

        void SavePostProcessProfile() override { FAIL("Unexpected profile save"); }

        void ReloadPostProcessProfile() override { FAIL("Unexpected profile reload"); }

      protected:
        void OnAttach() override
        {
            m_Service = Keire::CreateRef<Keire::UndoService>();
            m_History = m_Service->CreateContext({.Name = "Post process interaction"});
            m_Document.Open(Keire::AssetId(27, 1), Keire::PostProcessProfileAsset::Encode({}), 1, m_History);
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetVisible(true);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            if (m_GestureAction == GestureAction::ClearLookup)
            {
                DrawLookupClearInteraction(ui);
                return;
            }
            if (m_CreationDialog != CreationAction::None)
            {
                DrawCreationInteraction(ui);
                return;
            }
            if (m_Frame == 1)
            {
                m_Document.BeginContinuousEdit("Exposure gesture");
                auto changed = m_Document.Definition();
                changed.Parameters[0] = {true, 1.0};
                REQUIRE(m_Document.UpdateContinuousEdit(changed));
                if (m_GestureAction == GestureAction::Hide)
                {
                    m_Panel.Registration().SetVisible(false);
                }
                else
                {
                    auto* context = ImGui::GetCurrentContext();
                    ImGuiWindow* target = nullptr;
                    for (auto* window : context->Windows)
                    {
                        if (std::string_view(window->Name).starts_with("Post Processing"))
                        {
                            target = window;
                            break;
                        }
                    }
                    REQUIRE(target != nullptr);
                    if (m_GestureAction == GestureAction::Collapse)
                    {
                        ImGui::SetWindowCollapsed(target->Name, true);
                    }
                    else
                    {
                        const auto id = target->GetID("Undo");
                        context->NavActivateId = id;
                        context->NavActivateDownId = id;
                        context->NavActivatePressedId = id;
                    }
                }
            }
            m_Panel.Draw(ui);
            if (m_Frame == 1)
            {
                CHECK_FALSE(m_Document.ContinuousEditActive());
                CHECK_FALSE(m_Document.Dirty());
                CHECK(m_Document.Definition() == Keire::PostProcessProfileDefinition{});
                CHECK(m_History->UndoCount() == 0);
                CHECK(m_History->RedoCount() == (m_GestureAction == GestureAction::Undo ? 1 : 0));
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        void DrawLookupClearInteraction(Keire::UiFrame& ui)
        {
            auto* context = ImGui::GetCurrentContext();
            const auto findWindow = [context](const std::string_view fragment)
            {
                for (auto* window : context->Windows)
                {
                    if (std::string_view(window->Name).find(fragment) != std::string_view::npos)
                    {
                        return window;
                    }
                }
                return static_cast<ImGuiWindow*>(nullptr);
            };
            ImGuiWindow* target = nullptr;
            const char* label = nullptr;
            if (m_Frame == 1)
            {
                target = findWindow("PostProcessEffectNavigation");
                label = "Color";
            }
            if (m_Frame == 2)
            {
                target = findWindow("PostProcessEffectInspector");
                REQUIRE(target != nullptr);
                ImGui::SetScrollY(target, target->ScrollMax.y);
            }
            if (m_Frame == 3)
            {
                target = findWindow("PostProcessEffectInspector");
                label = "Clear Lookup";
            }
            if (label)
            {
                REQUIRE(target != nullptr);
                const auto id = target->GetID(label);
                context->NavActivateId = id;
                context->NavActivateDownId = id;
                context->NavActivatePressedId = id;
            }
            m_Panel.Draw(ui);
            if (m_Frame == 3)
            {
                CHECK(m_Document.Definition().ColorLookup.Override);
                CHECK_FALSE(m_Document.Definition().ColorLookup.Value);
                CHECK_FALSE(m_Document.Definition()
                                .Effects[static_cast<std::size_t>(Keire::PostProcessEffect::ColorGrading)]
                                .Override);
                CHECK(m_History->UndoCount() == 1);
                CHECK(m_Document.Dirty());
                REQUIRE(m_Document.Undo());
                CHECK(m_Document.Definition() == Keire::PostProcessProfileDefinition{});
                CHECK_FALSE(m_Document.Dirty());
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

        void DrawCreationInteraction(Keire::UiFrame& ui)
        {
            auto* context = ImGui::GetCurrentContext();
            ImGuiWindow* target = nullptr;
            const char* label = nullptr;
            if (m_Frame == 1)
            {
                for (auto* window : context->Windows)
                {
                    if (std::string_view(window->Name).starts_with("Post Processing"))
                    {
                        target = window;
                        break;
                    }
                }
                label = "New Profile...";
            }
            if (m_Frame == 2)
            {
                REQUIRE(context->OpenPopupStack.Size == 1);
                target = context->OpenPopupStack.back().Window;
                if (m_CreationDialog == CreationAction::Escape)
                {
                    // Input queued during OnUi is consumed by the next ImGui frame.
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
                }
                else
                {
                    label = m_CreationDialog == CreationAction::Cancel ? "Cancel" : "Create";
                }
            }
            if (m_Frame == 3 && m_CreationDialog == CreationAction::Reject)
            {
                REQUIRE(context->OpenPopupStack.Size == 1);
                target = context->OpenPopupStack.back().Window;
                label = "Cancel";
            }
            if (label)
            {
                REQUIRE(target != nullptr);
                const auto id = target->GetID(label);
                context->NavActivateId = id;
                context->NavActivateDownId = id;
                context->NavActivatePressedId = id;
            }
            m_Panel.Draw(ui);
            if (m_Frame == 1)
            {
                CHECK(context->OpenPopupStack.Size == 1);
            }
            if (m_Frame == 2)
            {
                const bool shouldClose =
                    m_CreationDialog == CreationAction::Cancel || m_CreationDialog == CreationAction::Submit;
                CHECK(context->OpenPopupStack.empty() == shouldClose);
                const bool requestedCreation =
                    m_CreationDialog == CreationAction::Submit || m_CreationDialog == CreationAction::Reject;
                CHECK(m_CreationRequests == (requestedCreation ? 1 : 0));
            }
            if (m_Frame == 3)
            {
                if (m_CreationDialog == CreationAction::Escape)
                {
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
                }
                CHECK(context->OpenPopupStack.empty());
                const bool requestedCreation =
                    m_CreationDialog == CreationAction::Submit || m_CreationDialog == CreationAction::Reject;
                CHECK(m_CreationRequests == (requestedCreation ? 1 : 0));
                CHECK_FALSE(m_Document.Dirty());
                CHECK(m_History->UndoCount() == 0);
                CHECK(m_History->RedoCount() == 0);
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

        GestureAction m_GestureAction;
        bool& m_Checked;
        CreationAction m_CreationDialog;
        unsigned m_CreationRequests = 0;
        unsigned m_Frame = 0;
        Keire::Ref<Keire::UndoService> m_Service;
        Keire::Ref<Keire::UndoContext> m_History;
        KeireEditor::PostProcessDocument m_Document{{.Persist = [](Keire::AssetId, std::span<const std::byte>) {}}};
        KeireEditor::PostProcessPanel m_Panel;
    };
} // namespace

TEST_CASE("post process panel hide and collapse cancel a gesture while Undo finishes then undoes it")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const auto action : {GestureAction::Undo, GestureAction::Hide, GestureAction::Collapse})
    {
        CAPTURE(static_cast<unsigned>(action));
        bool checked = false;
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        specification.Ui.Workspace.Enabled = true;
        specification.Ui.Workspace.Ephemeral = true;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<PostProcessInteractionLayer>(action, checked));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}

TEST_CASE("post process creation modal submits once closes on success and preserves cancellation after failure")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const auto action :
         {CreationAction::Cancel, CreationAction::Escape, CreationAction::Submit, CreationAction::Reject})
    {
        CAPTURE(static_cast<unsigned>(action));
        bool checked = false;
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        specification.Ui.Workspace.Enabled = true;
        specification.Ui.Workspace.Ephemeral = true;
        Keire::Application application(specification);
        (void)application.PushLayer(
            std::make_unique<PostProcessInteractionLayer>(GestureAction::Undo, checked, action));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}

TEST_CASE("post process Clear Lookup authors null override without a local texture or database")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    specification.Ui.Workspace.Enabled = true;
    specification.Ui.Workspace.Ephemeral = true;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<PostProcessInteractionLayer>(GestureAction::ClearLookup, checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
