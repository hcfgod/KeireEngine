#include "Keire/Core.h"
#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/InputActionsDocument.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
    enum class HistoryAction
    {
        Undo,
        Redo,
        Revert
    };

    class InputHistoryLayer final : public Keire::Layer, public KeireEditor::IInputActionsController
    {
      public:
        InputHistoryLayer(const HistoryAction action, bool& checked)
            : Layer("Input history"), m_Action(action), m_Checked(checked), m_Panel(*this)
        {
        }

        KeireEditor::InputActionsDocument& InputActionsState() noexcept override { return m_Document; }
        const Keire::UiThemeDefinition& InputActionsTheme() const noexcept override { return m_Theme; }
        Keire::Ref<Keire::AssetDatabase> InputAssetDatabase() const noexcept override { return {}; }
        Keire::Ref<Keire::InputActionContext> InputActionContext() const noexcept override { return {}; }
        Keire::Ref<Keire::InputSystem> InputSystem() const noexcept override { return {}; }
        void ActivateInputHistory() noexcept override {}
        void SaveInputActionsDocument() override { FAIL("Unexpected Save command"); }
        std::filesystem::path GenerateInputActionsWrapper(std::string_view, std::string_view) override { return {}; }
        void ReloadInputActionsDocument(const Keire::AssetId asset) override
        {
            ++m_Commands;
            m_Document.Open(asset, m_Original, m_History);
        }
        void RecordInputActionsUndo(const std::string_view name) override
        {
            m_Document.RecordApplied(name, m_Document.Definition());
        }
        void UndoInputActions() override
        {
            ++m_Commands;
            REQUIRE(m_Document.Undo());
        }
        void RedoInputActions() override
        {
            ++m_Commands;
            REQUIRE(m_Document.Redo());
        }
        void ReportInputActionsError(std::string message) noexcept override { m_Error = std::move(message); }

      protected:
        void OnAttach() override
        {
            m_Service = Keire::CreateRef<Keire::UndoService>();
            m_History = m_Service->CreateContext({.Name = "Input history"});
            m_Original.Name = "Fixture";
            m_Original.ActionMaps.push_back({.Id = Keire::AssetId::Generate(), .Name = "Player"});
            m_Document.Open(Keire::AssetId::Generate(), m_Original, m_History);
            m_Document.SelectMap(m_Original.ActionMaps.front().Id);
            m_Document.RecordApplied("Rename map", m_Original);
            auto edited = m_Original;
            edited.ActionMaps.front().Name = "Player QA";
            m_Document.ReplaceDefinition(std::move(edited));
            if (m_Action == HistoryAction::Redo)
                REQUIRE(m_Document.Undo());
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetVisible(true);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            auto* context = ImGui::GetCurrentContext();
            if (m_Frame == 1)
            {
                ImGuiWindow* target = nullptr;
                for (auto* window : context->Windows)
                    if (std::string_view(window->Name).starts_with("Input Actions"))
                    {
                        target = window;
                        break;
                    }
                REQUIRE(target != nullptr);
                const auto* button = m_Action == HistoryAction::Undo   ? "##InputActionsUndo"
                                     : m_Action == HistoryAction::Redo ? "##InputActionsRedo"
                                                                       : "##InputActionsRevert";
                const auto id = target->GetID(button);
                context->NavActivateId = id;
                context->NavActivateDownId = id;
                context->NavActivatePressedId = id;
            }
            ui.SetNextWindowSize({1000, 620}, false);
            m_Panel.Draw(ui);
            if (m_Frame >= 1)
            {
                CHECK(m_Commands == 1);
                REQUIRE(m_Document.Definition().ActionMaps.size() == 1);
                CHECK(m_Document.Definition().ActionMaps.front().Name ==
                      (m_Action == HistoryAction::Redo ? "Player QA" : "Player"));
                CHECK(m_Document.Dirty() == (m_Action == HistoryAction::Redo));
                CHECK(m_Error.empty());
            }
            if (++m_Frame == 3)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        HistoryAction m_Action;
        bool& m_Checked;
        KeireEditor::InputActionsPanel m_Panel;
        KeireEditor::InputActionsDocument m_Document;
        Keire::UiThemeDefinition m_Theme;
        Keire::InputActionAssetDefinition m_Original;
        Keire::Ref<Keire::UndoService> m_Service;
        Keire::Ref<Keire::UndoContext> m_History;
        std::string m_Error;
        int m_Frame = 0;
        int m_Commands = 0;
    };
} // namespace

TEST_CASE("Input Actions toolbar undo redo and revert survive the frame commit")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const auto action : {HistoryAction::Undo, HistoryAction::Redo, HistoryAction::Revert})
    {
        INFO("History action: " << static_cast<int>(action));
        bool checked = false;
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Visible = false;
        specification.TargetFrameRate = 0;
        specification.Ui.Mode = Keire::UiMode::Headless;
        specification.Ui.LayoutPath.clear();
        specification.Ui.Workspace.Enabled = true;
        specification.Ui.Workspace.Ephemeral = true;
        Keire::Application application(specification);
        (void)application.PushLayer(std::make_unique<InputHistoryLayer>(action, checked));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
}

TEST_CASE("input actions document owns authoring state and dirty lifecycle")
{
    const auto undoService = Keire::CreateRef<Keire::UndoService>();
    const auto undo = undoService->CreateContext({.Name = "Input document test"});
    const auto root =
        std::filesystem::temp_directory_path() / ("Keire-InputDocument-" + Keire::AssetId::Generate().ToString());
    const auto source = root / "Assets/Input/Test.keireinput";
    std::filesystem::create_directories(source.parent_path());
    KeireEditor::InputActionsDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000002"),
                  Keire::InputActionAsset::DefaultDefinition(), undo, source);
    const auto originalName = document.Definition().Name;
    auto edited = document.Definition();
    document.RecordApplied("Rename input actions", edited);
    edited.Name = "Edited actions";
    document.ReplaceDefinition(std::move(edited));
    CHECK(document.Dirty());
    CHECK(document.Definition().Name == "Edited actions");
    CHECK(document.Definition().ActionMaps.size() > 0);
    CHECK(document.Undo());
    CHECK(document.Definition().Name == originalName);
    CHECK_FALSE(document.Dirty());
    CHECK(document.Redo());
    CHECK(document.Definition().Name == "Edited actions");
    CHECK(document.Dirty());
    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(std::filesystem::is_regular_file(source));
    CHECK(document.Undo());
    CHECK(document.Definition().Name == originalName);
    CHECK(document.Dirty());
    CHECK(document.Redo());
    CHECK(document.Definition().Name == "Edited actions");
    CHECK_FALSE(document.Dirty());
    document.Close();
    CHECK_FALSE(document.Asset());
    CHECK(document.Definition().ActionMaps.empty());
    undo->Close();
    undoService->Close();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
