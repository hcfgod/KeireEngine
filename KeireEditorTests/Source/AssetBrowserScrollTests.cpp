#include "Keire/Core.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>

TEST_CASE("Asset browser content identities isolate projects and folders with literal ImGui markers")
{
    using KeireEditor::AssetBrowserContentPaneId;
    const auto root = std::filesystem::path("Project") / "Assets";
    CHECK(AssetBrowserContentPaneId(root, "A") != AssetBrowserContentPaneId(root, "B"));
    CHECK(AssetBrowserContentPaneId(root, "A") != AssetBrowserContentPaneId("Other/Assets", "A"));
    CHECK(AssetBrowserContentPaneId(root, "A###same") != AssetBrowserContentPaneId(root, "B###same"));
    CHECK(AssetBrowserContentPaneId(root, "A###same").find("###") == std::string::npos);
    CHECK(AssetBrowserContentPaneId(root, "A/../B") == AssetBrowserContentPaneId(root, "B"));
}

TEST_CASE("Asset browser folder navigation restores its own scroll and explicit reveal takes priority")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    int checkedFrames = 0;
    class FolderScrollLayer final : public Keire::Layer
    {
      public:
        explicit FolderScrollLayer(int& checked) : Layer("Asset folder scroll"), m_Checked(checked) {}

      protected:
        void OnAttach() override
        {
            m_Panel = Owner().GetUiWorkspace().RegisterPanel({"test.asset.scroll", "Asset scroll"});
            m_Panel.SetVisible(true);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            const auto root = m_Frame == 5 ? "Other/Assets" : "Project/Assets";
            const auto folder = m_Frame == 2 || m_Frame == 3 ? "B" : "A";
            ui.SetNextWindowSize({600.0F, 300.0F}, false);
            if (auto panel = ui.BeginPanel(m_Panel); panel)
            {
                if (auto child = ui.BeginChild(KeireEditor::AssetBrowserContentPaneId(root, folder), {0, 220}); child)
                {
                    if (m_Frame == 1 || m_Frame == 4)
                    {
                        CHECK(ImGui::GetScrollY() == doctest::Approx(300.0F));
                        ++m_Checked;
                    }
                    if (m_Frame == 2 || m_Frame == 5)
                    {
                        CHECK(ImGui::GetScrollY() == doctest::Approx(0.0F));
                        ++m_Checked;
                    }
                    if (m_Frame == 3)
                    {
                        CHECK(ImGui::GetScrollY() == doctest::Approx(200.0F));
                        ++m_Checked;
                    }
                    if (m_Frame == 7)
                    {
                        CHECK(ImGui::GetScrollY() > 300.0F);
                        ++m_Checked;
                    }
                    for (int row = 0; row < 100; ++row)
                    {
                        ui.Text("Asset " + std::to_string(row));
                        if (m_Frame == 6 && row == 80)
                            ui.ScrollLastItemIntoView();
                    }
                    if (m_Frame == 0)
                        ImGui::SetScrollY(300.0F);
                    if (m_Frame == 2)
                        ImGui::SetScrollY(200.0F);
                }
            }
            if (++m_Frame == 8)
                Owner().RequestExit();
        }

      private:
        Keire::UiPanelRegistration m_Panel;
        int& m_Checked;
        int m_Frame = 0;
    };
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    specification.Ui.Workspace.Enabled = true;
    specification.Ui.Workspace.Ephemeral = true;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<FolderScrollLayer>(checkedFrames));
    CHECK(application.Run() == 0);
    CHECK(checkedFrames == 6);
}
