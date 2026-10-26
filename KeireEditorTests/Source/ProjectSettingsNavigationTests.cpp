#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireInternal/FileSystem.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

namespace
{
    class SettingsNavigationLayer final : public Keire::Layer, public KeireEditor::IProjectSettingsController
    {
      public:
        SettingsNavigationLayer(bool& checked, const float width, float& renderingHeight)
            : Layer("Project settings navigation"), m_Checked(checked), m_Width(width),
              m_RenderingHeight(renderingHeight), m_Panel(m_Document, *this)
        {
        }

        void ActivateProjectSettingsHistory() noexcept override {}

        KeireEditor::EnvironmentPreviewClock* ProjectEnvironmentPreview() override { return &m_Preview; }

        std::span<const Keire::AssetSourceRecord> ProjectSettingsAssetRecords() const noexcept override { return {}; }

        void RevealProjectSettingsAsset(Keire::AssetId) override { FAIL("Unexpected asset reveal"); }

        std::pair<Keire::AssetId, Keire::AssetId> ProjectDefaultInput() const noexcept override
        {
            ++m_InputReads;
            return {};
        }

        std::vector<Keire::InputActionMapDefinition> ProjectInputActionMaps(Keire::AssetId) const override
        {
            return {};
        }

        void SetProjectDefaultInput(Keire::AssetId, Keire::AssetId) override { FAIL("Navigation changed input"); }

        KeireEditor::ManagedSdkPreference ProjectManagedSdk() const override
        {
            ++m_SdkReads;
            return {};
        }

        void SetProjectManagedSdk(KeireEditor::ManagedSdkPreference) override { FAIL("Navigation changed SDK"); }

        Keire::RenderFeatureCapabilities ProjectRenderFeatureCapabilities() const noexcept override
        {
            ++m_RenderReads;
            return {};
        }

        void ApplyProjectAuthoringSettings(const Keire::ProjectAuthoringSettings&) override
        {
            FAIL("Navigation changed physics");
        }

      protected:
        void OnAttach() override
        {
            m_Root = std::filesystem::temp_directory_path() /
                     ("Keire-SettingsNavigation-" + Keire::AssetId::Generate().ToString());
            m_History = Keire::CreateRef<Keire::UndoService>();
            m_Document.Open(m_Root, {}, m_History->CreateContext({.Name = "Project Settings"}));
            m_CustomExecutable = m_Root / "Test Editor.exe";
            Keire::Detail::WriteTextFileAtomically(m_CustomExecutable, "Test fixture; never launched.");
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetVisible(true);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            constexpr std::array names{"Scripting",       "Input",     "Code Editor",
                                       "Audio / Physics", "Rendering", "Environment"};
            const auto selected = m_Frame >= 18  ? 2
                                  : m_Frame < 3  ? 0
                                  : m_Frame >= 9 ? 0
                                                 : (m_Frame - 2) % static_cast<int>(names.size());
            if (m_Frame >= 3)
            {
                auto& bars = ImGui::GetCurrentContext()->TabBars;
                REQUIRE(bars.GetAliveCount() == 1);
                auto* bar = bars.GetByIndex(0);
                REQUIRE(bar->Tabs.Size == names.size());
                bool found = false;
                for (auto& tab : bar->Tabs)
                {
                    if (std::string_view(ImGui::TabBarGetTabName(bar, &tab)) == names[selected])
                    {
                        bar->NextSelectedTabId = tab.ID;
                        found = true;
                    }
                }
                REQUIRE(found);
            }
            if (m_Frame == 3)
            {
                auto settings = m_Document.Settings();
                settings.Exposure = 2.0F;
                m_Document.Update(settings);
                REQUIRE(m_Document.Dirty());
                REQUIRE(m_Document.UndoContext()->UndoCount() == 0);
            }
            if (m_Frame == 9)
            {
                REQUIRE(m_Document.UndoContext()->Undo());
                REQUIRE(m_Document.Dirty());
            }
            if (m_Frame == 10)
            {
                REQUIRE(m_SaveSettingsId != 0);
                ImGui::ActivateItemByID(m_SaveSettingsId);
            }
            if (m_Frame == 12)
            {
                const auto path = m_Root / "ProjectSettings/Rendering.keiresettings";
                REQUIRE(std::filesystem::remove(path));
                std::filesystem::create_directory(path);
            }
            if (m_Frame == 13)
            {
                ImGui::ActivateItemByID(m_SaveSettingsId);
            }
            if (m_Frame == 16)
            {
                ImGui::ActivateItemByID(m_SaveSettingsId);
            }
            if (m_Frame == 19 || m_Frame == 22)
            {
                ImGui::ClearActiveID();
                auto authoring = m_Document.AuthoringSettings();
                authoring.ExternalEditorId = "custom";
                authoring.ExternalEditorExecutable = m_Frame == 19 ? "first-editor.exe" : "second-editor.exe";
                m_Document.UpdateAuthoring(authoring);
                m_Document.CommitEdit("Change test editor");
            }
            if (m_Frame == 25 || m_Frame == 28)
            {
                ImGui::ClearActiveID();
                if (m_Frame == 25)
                {
                    REQUIRE(m_Document.UndoContext()->Undo());
                }
                else
                {
                    REQUIRE(m_Document.UndoContext()->Redo());
                }
            }
            if (m_Frame == 20 || m_Frame == 23 || m_Frame == 26 || m_Frame == 29)
            {
                REQUIRE(m_EditorExecutableId != 0);
                ImGui::ActivateItemByID(m_EditorExecutableId);
            }
            if (m_Frame == 32)
            {
                ImGui::ActivateItemByID(m_ApplyEditorId);
            }
            if (selected == 2 && (m_ObservedEditorId != m_Document.AuthoringSettings().ExternalEditorId ||
                                  m_ObservedEditorPath != m_Document.AuthoringSettings().ExternalEditorExecutable))
            {
                ++m_EditorDraftRevision;
            }
            if (m_Frame == 34 || m_Frame == 39)
            {
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (std::string_view(window->Name).find("CodeEditorSettings") != std::string_view::npos)
                    {
                        m_EditorExecutableId = EditorControlId(window, "##Editor Executable");
                    }
                }
                ImGui::ActivateItemByID(m_EditorExecutableId);
            }
            if (m_Frame == 38)
            {
                ImGui::ClearActiveID();
                auto settings = m_Document.Settings();
                settings.Exposure = 3.0F;
                m_Document.Update(settings);
                m_Document.CommitEdit("Unrelated render edit");
            }
            m_SdkReads = 0;
            m_InputReads = 0;
            m_RenderReads = 0;
            ui.SetNextWindowPosition({10.0F, 10.0F}, false);
            if (m_Frame > 0)
            {
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (std::string_view(window->Name).find("Project Settings") != std::string_view::npos &&
                        window->ParentWindow == nullptr)
                    {
                        ImGui::SetWindowSize(window, {m_Width, 600.0F}, ImGuiCond_Always);
                    }
                }
            }
            m_Panel.Draw(ui, {});
            if (selected == 2)
            {
                m_ObservedEditorId = m_Document.AuthoringSettings().ExternalEditorId;
                m_ObservedEditorPath = m_Document.AuthoringSettings().ExternalEditorExecutable;
            }
            for (auto* window : ImGui::GetCurrentContext()->Windows)
            {
                if (window->Active &&
                    std::string_view(window->Name).find("CodeEditorSettings") != std::string_view::npos)
                {
                    m_EditorExecutableId = EditorControlId(window, "##Editor Executable");
                    m_ApplyEditorId = EditorControlId(window, "Use Custom Editor");
                    if (m_Frame >= 19)
                    {
                        CHECK(window->DC.CursorMaxPos.x <= window->InnerRect.Max.x);
                    }
                }
            }
            if (m_Frame == 30 || m_Frame == 36)
            {
                auto* input = ImGui::GetInputTextState(m_EditorExecutableId);
                REQUIRE(input != nullptr);
                input->SelectAll();
                const auto text = m_Frame == 30 ? Keire::Detail::PathToUtf8(m_CustomExecutable) : "missing draft.exe";
                ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
            }
            if (m_Frame == 33)
            {
                REQUIRE(m_Document.AuthoringSettings().ExternalEditorExecutable == m_CustomExecutable);
                REQUIRE(m_Document.UndoContext()->Undo());
                CHECK(m_Document.AuthoringSettings().ExternalEditorExecutable == "second-editor.exe");
            }
            if (m_Frame == 35 || m_Frame == 40)
            {
                const auto* input = ImGui::GetInputTextState(m_EditorExecutableId);
                REQUIRE(input != nullptr);
                CHECK(std::string_view(input->TextA.Data) ==
                      (m_Frame == 35 ? "second-editor.exe" : "missing draft.exe"));
                CHECK(m_Document.AuthoringSettings().ExternalEditorExecutable == "second-editor.exe");
            }
            if (m_Frame == 21 || m_Frame == 24 || m_Frame == 27 || m_Frame == 30)
            {
                const auto* input = ImGui::GetInputTextState(m_EditorExecutableId);
                REQUIRE(input != nullptr);
                CHECK(std::string_view(input->TextA.Data) ==
                      (m_Frame == 21 || m_Frame == 27 ? "first-editor.exe" : "second-editor.exe"));
            }
            if (m_Frame >= 3 && m_Frame < 9 && selected >= 4)
            {
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (std::string_view(window->Name).find("Project Settings") != std::string_view::npos &&
                        window->ParentWindow == nullptr)
                    {
                        INFO("Reset footer category: " << names[selected] << ", width: " << m_Width);
                        CHECK(window->DC.CursorMaxPos.y <= window->InnerRect.Max.y);
                    }
                }
            }
            for (auto* window : ImGui::GetCurrentContext()->Windows)
            {
                if (std::string_view(window->Name).find("Project Settings") != std::string_view::npos &&
                    window->ParentWindow == nullptr)
                {
                    m_SaveSettingsId = window->GetID("Save Settings");
                }
            }
            if (m_Frame == 0)
            {
                bool found = false;
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (std::string_view(window->Name).find("Project Settings") != std::string_view::npos &&
                        window->ParentWindow == nullptr)
                    {
                        CHECK(window->Size.x == doctest::Approx(900.0F));
                        CHECK(window->Size.y == doctest::Approx(700.0F));
                        found = true;
                    }
                }
                CHECK(found);
            }
            if (m_Frame >= 3 && m_Frame < 9)
            {
                INFO("Category: " << names[selected] << ", width: " << m_Width);
                CHECK(m_SdkReads == (selected == 0 ? 1 : 0));
                CHECK(m_InputReads == (selected == 1 ? 1 : 0));
                CHECK(m_RenderReads == (selected == 4 ? 1 : 0));
                CHECK_FALSE(m_Document.Dirty());
                CHECK(m_Document.UndoContext()->UndoCount() == 1);
                CHECK(Keire::LoadRenderEnvironmentSettings(m_Root).Exposure == doctest::Approx(2.0F));
                if (selected == 4)
                {
                    for (auto* window : ImGui::GetCurrentContext()->Windows)
                    {
                        if (std::string_view(window->Name).find("RenderingSettings") != std::string_view::npos)
                        {
                            m_RenderingHeight = window->DC.CursorMaxPos.y - window->DC.CursorStartPos.y;
                            REQUIRE(window->ParentWindow != nullptr);
                            CHECK(window->ParentWindow->Scroll.y == 0.0F);
                        }
                    }
                    CHECK(m_RenderingHeight > 0.0F);
                }
            }
            if (m_Frame == 9)
            {
                CHECK(m_Document.Dirty());
                CHECK(Keire::LoadRenderEnvironmentSettings(m_Root).Exposure == doctest::Approx(2.0F));
            }
            if (m_Frame == 11)
            {
                CHECK_FALSE(m_Document.Dirty());
                CHECK(Keire::LoadRenderEnvironmentSettings(m_Root).Exposure == doctest::Approx(1.0F));
                CHECK(m_Document.Settings().Exposure == doctest::Approx(1.0F));
                CHECK(m_Document.UndoContext()->Redo());
                CHECK(m_Document.Settings().Exposure == doctest::Approx(2.0F));
            }
            if (m_Frame == 14 || m_Frame == 15)
            {
                CHECK(m_Document.Dirty());
                bool found = false;
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (window->Active &&
                        std::string_view(window->Name).find("ProjectSettingsErrorDetails") != std::string_view::npos)
                    {
                        found = true;
                        CHECK(window->Size.y == doctest::Approx(100.0F));
                        REQUIRE(window->ParentWindow != nullptr);
                        CHECK(window->Pos.y + window->Size.y <= window->ParentWindow->InnerRect.Max.y);
                        CHECK(window->ParentWindow->Scroll.y == 0.0F);
                    }
                }
                CHECK(found);
                if (m_Frame == 15)
                {
                    REQUIRE(std::filesystem::remove(m_Root / "ProjectSettings/Rendering.keiresettings"));
                }
            }
            if (m_Frame == 17)
            {
                CHECK_FALSE(m_Document.Dirty());
                CHECK(Keire::LoadRenderEnvironmentSettings(m_Root).Exposure == doctest::Approx(2.0F));
            }
            if (m_Frame == 40)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

        void OnDetach() noexcept override
        {
            m_Document.Close();
            m_History->Close();
            std::error_code ignored;
            std::filesystem::remove_all(m_Root, ignored);
        }

      private:
        ImGuiID EditorControlId(ImGuiWindow* window, const char* label) const
        {
            const auto revision = std::to_string(m_EditorDraftRevision);
            return ImHashStr(label, 0, window->GetID(revision.c_str()));
        }

        bool& m_Checked;
        float m_Width;
        float& m_RenderingHeight;
        int m_Frame = 0;
        ImGuiID m_SaveSettingsId = 0;
        ImGuiID m_EditorExecutableId = 0;
        ImGuiID m_ApplyEditorId = 0;
        std::uint64_t m_EditorDraftRevision = 0;
        std::string m_ObservedEditorId;
        std::filesystem::path m_ObservedEditorPath;
        mutable int m_SdkReads = 0;
        mutable int m_InputReads = 0;
        mutable int m_RenderReads = 0;
        std::filesystem::path m_Root;
        std::filesystem::path m_CustomExecutable;
        Keire::Ref<Keire::UndoService> m_History;
        KeireEditor::ProjectSettingsDocument m_Document;
        KeireEditor::EnvironmentPreviewClock m_Preview;
        KeireEditor::ProjectSettingsPanel m_Panel;
    };
} // namespace

TEST_CASE("Project settings categories isolate controls and commit pending edits on navigation")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    std::array<float, 2> renderingHeights{};
    std::size_t widthIndex = 0;
    for (const float width : {320.0F, 1000.0F})
    {
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
            std::make_unique<SettingsNavigationLayer>(checked, width, renderingHeights[widthIndex++]));
        CHECK(application.Run() == 0);
        CHECK(checked);
    }
    // Narrow panels wrap explanations into more lines instead of silently clipping them.
    CHECK(renderingHeights[0] > renderingHeights[1]);
}
