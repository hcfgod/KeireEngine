#include "Keire/Core.h"
#include "KeireClient/Editor/ExternalEditorProfiles.h"
#include "KeireClientInternal/Editor/ExternalEditorSettings.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/VisualStudioDiscovery.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace
{
    struct EditorSettingsDirectory final
    {
        EditorSettingsDirectory()
            : Root(std::filesystem::temp_directory_path() /
                   ("Keire-ExternalEditorSettings-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Root);
        }

        ~EditorSettingsDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };

    class CustomEditorLayer final : public Keire::Layer
    {
      public:
        CustomEditorLayer(const std::filesystem::path& candidate, const bool finishEditing, const bool valid,
                          bool& checked)
            : Layer("Custom editor controls"), m_Candidate(Keire::Detail::PathToUtf8(candidate)), m_Expected(candidate),
              m_FinishEditing(finishEditing), m_Valid(valid), m_Checked(checked)
        {
            if (!finishEditing)
            {
                m_Draft = m_Candidate;
            }
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({900, 300}, false);
            auto window = ui.BeginWindow("Custom editor fixture");
            if (m_FinishEditing && m_Frame == 1)
            {
                ImGui::SetKeyboardFocusHere();
            }
            if (!m_FinishEditing && m_Frame == 1)
            {
                const auto button = ImGui::GetCurrentWindow()->GetID("Use Custom Editor");
                ImGui::GetCurrentContext()->NavActivateId = button;
                ImGui::GetCurrentContext()->NavActivateDownId = button;
                ImGui::GetCurrentContext()->NavActivatePressedId = button;
            }
            const bool changed = KeireEditor::Detail::DrawCustomExternalEditor(ui, m_Draft, m_Executable, m_Diagnostic);
            if (changed)
            {
                ++m_Applied;
            }
            if (m_FinishEditing && m_Frame == 2)
            {
                ImGui::GetIO().AddInputCharactersUTF8(m_Candidate.c_str());
            }
            if (m_FinishEditing && m_Frame == 4)
            {
                ImGui::SetKeyboardFocusHere();
            }
            (void)ui.Button("Other control");
            if (++m_Frame == 10)
            {
                CHECK(m_Draft == m_Candidate);
                if (m_Valid)
                {
                    CHECK(m_Applied == 1U);
                    CHECK(m_Executable == std::filesystem::absolute(m_Expected).lexically_normal());
                    CHECK(m_Diagnostic.empty());
                }
                else
                {
                    CHECK(m_Applied == 0U);
                    CHECK(m_Executable == std::filesystem::path("previous-editor"));
                    CHECK(m_Diagnostic.find("does not exist") != std::string::npos);
                }
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        std::string m_Candidate;
        std::filesystem::path m_Expected;
        bool m_FinishEditing;
        bool m_Valid;
        bool& m_Checked;
        std::string m_Draft;
        std::filesystem::path m_Executable = "previous-editor";
        std::string m_Diagnostic;
        unsigned int m_Applied = 0;
        int m_Frame = 0;
    };
} // namespace

TEST_CASE("custom IDE controls validate and commit the input before later widgets replace item state")
{
    EditorSettingsDirectory directory;
    const auto existing = directory.Root / "Editor With Spaces.exe";
    Keire::Detail::WriteTextFileAtomically(existing, "test fixture, never launched");
    bool finishEditing = false;
    bool valid = true;
    SUBCASE("leaving an edited executable field applies the value") { finishEditing = true; }
    SUBCASE("explicit apply validates an existing executable") {}
    SUBCASE("explicit apply rejects missing executable without changing the saved value") { valid = false; }
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<CustomEditorLayer>(valid ? existing : directory.Root / "missing.exe",
                                                                    finishEditing, valid, checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}

TEST_CASE("installed Visual Studio versions each have a stable individually selectable editor profile")
{
    const auto profiles = KeireEditor::DiscoverExternalEditorProfiles();
    const auto repeated = KeireEditor::DiscoverExternalEditorProfiles();
    for (const auto& installation : Keire::Detail::DiscoverVisualStudioInstallations())
    {
        if (!Keire::Detail::IsVisualStudioExecutableCompatible(
                installation.Executable, Keire::Detail::ManagedWorkspaceCompatibilityVisualStudioMajor))
        {
            continue;
        }
        const auto executable = std::filesystem::absolute(installation.Executable).lexically_normal();
        const auto found = std::ranges::find(profiles, executable, &KeireEditor::ExternalEditorProfile::Executable);
        REQUIRE(found != profiles.end());
        const auto choice = std::ranges::find_if(
            profiles, [&executable](const auto& profile)
            { return profile.Id.starts_with("visual-studio:") && profile.Executable == executable; });
        REQUIRE(choice != profiles.end());
        const auto& id = choice->Id;
        CHECK(id.size() <= 64U);
        auto settings = Keire::DefaultProjectAuthoringSettings();
        settings.ExternalEditorId = id;
        settings.ExternalEditorExecutable = executable;
        CHECK_NOTHROW(Keire::ValidateProjectAuthoringSettings(settings));
        CHECK(choice->Executable == executable);
        CHECK(choice->Installed);
        const auto same = std::ranges::find(repeated, id, &KeireEditor::ExternalEditorProfile::Id);
        REQUIRE(same != repeated.end());
        CHECK(same->Executable == executable);
        CHECK(same->DisplayName == choice->DisplayName);
        if (installation.Version[0] == 17U)
        {
            CHECK(choice->DisplayName.find("2022") != std::string::npos);
        }
        else if (installation.Version[0] == 18U)
        {
            CHECK(choice->DisplayName.find("2026") != std::string::npos);
        }
    }
    for (auto first = profiles.begin(); first != profiles.end(); ++first)
    {
        CHECK(std::ranges::count(profiles, first->Id, &KeireEditor::ExternalEditorProfile::Id) == 1);
    }
}
