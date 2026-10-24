#include "Keire/Core.h"
#include "KeireClient/Editor/ConsolePanel.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

TEST_CASE("Console parses managed diagnostic source coordinates without confusing Windows drive letters")
{
    const auto windows = KeireEditor::Detail::ParseConsoleSourceLocation("D:/My Project/Game.cs:39:27: CS1061: error");
    REQUIRE(windows);
    CHECK(windows->Path == "D:/My Project/Game.cs");
    CHECK(windows->Line == 39);
    CHECK(windows->Column == 27);
    const auto unixPath = KeireEditor::Detail::ParseConsoleSourceLocation("/tmp/Game.cs:2:1: error");
    REQUIRE(unixPath);
    CHECK(unixPath->Line == 2);
    CHECK_FALSE(KeireEditor::Detail::ParseConsoleSourceLocation("Ready"));
    CHECK_FALSE(KeireEditor::Detail::ParseConsoleSourceLocation("Game.cs:0:1: error"));
    CHECK_FALSE(KeireEditor::Detail::ParseConsoleSourceLocation("Game.cs:1:4294967296: error"));
    CHECK_FALSE(KeireEditor::Detail::ParseConsoleSourceLocation("Game.cs:1:2x: error"));
}

namespace
{
    class LogScope final
    {
      public:
        LogScope()
            : m_Directory(
                  std::filesystem::temp_directory_path() /
                  ("Keire-ConsolePanel-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            Keire::Log::Shutdown();
            std::filesystem::remove_all(m_Directory);
            Keire::LogConfig config;
            config.LogDirectory = m_Directory.string();
            config.EnableConsole = false;
            Keire::Log::Initialize(config);
        }

        ~LogScope() noexcept
        {
            Keire::Log::Shutdown();
            std::error_code error;
            std::filesystem::remove_all(m_Directory, error);
        }

      private:
        std::filesystem::path m_Directory;
    };
} // namespace

TEST_CASE("Compact Console keeps log rows visible and restores details when enlarged")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    int checkedFrames = 0;
    class ConsoleLayoutLayer final : public Keire::Layer
    {
      public:
        explicit ConsoleLayoutLayer(int& checkedFrames) : Layer("Console layout"), m_CheckedFrames(checkedFrames) {}

      protected:
        void OnAttach() override
        {
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetVisible(true);
            m_Panel.Add("Validation", "A log row must remain readable.", {1, 1, 1, 1}, 1);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            const bool compact = m_Frame < 2;
            ui.SetNextWindowSize({640.0F, compact ? 140.0F : 440.0F}, false);
            m_Panel.Draw(ui, {});
            if (m_Frame == 1 || m_Frame == 3)
            {
                ImGuiWindow* logs = nullptr;
                ImGuiWindow* details = nullptr;
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                {
                    if (!window->Active)
                        continue;
                    const std::string_view name(window->Name);
                    if (name.find("/ConsoleLogs_") != std::string_view::npos)
                        logs = window;
                    if (name.find("/ConsoleDetails_") != std::string_view::npos)
                        details = window;
                }
                REQUIRE(logs != nullptr);
                CHECK(logs->InnerRect.GetHeight() >= ImGui::GetFontSize());
                if (compact)
                    CHECK(details == nullptr);
                else
                {
                    REQUIRE(details != nullptr);
                    CHECK(details->InnerRect.GetHeight() >= ImGui::GetFontSize());
                }
                ++m_CheckedFrames;
            }
            if (++m_Frame == 4)
                Owner().RequestExit();
        }

      private:
        KeireEditor::ConsolePanel m_Panel;
        int& m_CheckedFrames;
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
    (void)application.PushLayer(std::make_unique<ConsoleLayoutLayer>(checkedFrames));
    CHECK(application.Run() == 0);
    CHECK(checkedFrames == 2);
}

TEST_CASE("Editor Console captures native Core and Client records exactly once")
{
    LogScope logs;
    KeireEditor::ConsolePanel panel;
    const Keire::UiThemeDefinition theme;

    panel.CaptureEngineLogs(1, theme);
    CHECK(panel.MessageCount() == 1);

    KEIRE_CORE_WARN("renderer warning routed to editor");
    KEIRE_CLIENT_ERROR("[Managed] script failure routed to editor");
    panel.CaptureEngineLogs(2, theme);
    CHECK(panel.MessageCount() == 3);

    panel.CaptureEngineLogs(3, theme);
    CHECK(panel.MessageCount() == 3);
}

TEST_CASE("Editor Console collapse projection tracks repetitions without merging presentation differences")
{
    const Keire::UiColor error{1.0F, 0.0F, 0.0F, 1.0F};
    const Keire::UiColor warning{1.0F, 0.5F, 0.0F, 1.0F};
    const std::vector<KeireEditor::Detail::ConsoleProjectionEntry> entries{
        {"Managed Build", "CS1525: Invalid expression term", error, Keire::LogLevel::Error},
        {"Managed Build", "CS1525: Invalid expression term", error, Keire::LogLevel::Error},
        {"Managed Build", "CS1525: Invalid expression term", warning, Keire::LogLevel::Error},
        {"Managed Build", "CS1525: Invalid expression term", error, Keire::LogLevel::Warn},
        {"Managed Runtime", "CS1525: Invalid expression term", error, Keire::LogLevel::Error},
        {"Managed Runtime", "Different text", error, Keire::LogLevel::Error},
    };

    const auto collapsed = KeireEditor::Detail::ProjectConsoleEntries(entries, true);
    REQUIRE(collapsed.size() == 5);
    CHECK(collapsed[0].SourceIndex == 0);
    CHECK(collapsed[0].Repetitions == 2);
    CHECK(collapsed[1].SourceIndex == 2);
    CHECK(collapsed[1].Repetitions == 1);
    CHECK(collapsed[2].SourceIndex == 3);
    CHECK(collapsed[3].SourceIndex == 4);
    CHECK(collapsed[4].SourceIndex == 5);

    const auto expanded = KeireEditor::Detail::ProjectConsoleEntries(entries, false);
    REQUIRE(expanded.size() == entries.size());
    CHECK(std::ranges::all_of(expanded, [](const auto& entry) { return entry.Repetitions == 1; }));
}

TEST_CASE("Editor Console detail formatting preserves complete messages and summarizes multiple selection")
{
    const auto formatted = KeireEditor::Detail::FormatConsoleEntry(
        42, "Managed Build", "D:/A/Very/Long/Project/Path/Player.cs:12:7: CS1525: Invalid expression term ';'");
    CHECK(formatted == "[42] [Managed Build] "
                       "D:/A/Very/Long/Project/Path/Player.cs:12:7: CS1525: Invalid expression term ';'");
    CHECK(KeireEditor::Detail::FormatConsoleSelectionSummary(3) == "3 messages selected.");
}

TEST_CASE("Editor Console selection supports range additive and retained message behavior")
{
    KeireEditor::ConsoleSelection selection;
    const std::vector<std::uint64_t> visible{10, 20, 30, 40, 50};

    selection.Select(visible, 20, false, false);
    selection.Select(visible, 40, true, false);
    CHECK(std::ranges::equal(selection.Selected(), std::vector<std::uint64_t>{20, 30, 40}));

    selection.Select(visible, 50, false, true);
    CHECK(selection.Contains(50));
    selection.Select(visible, 30, false, true);
    CHECK_FALSE(selection.Contains(30));

    const std::vector<std::uint64_t> retained{10, 20, 40};
    selection.Retain(retained);
    CHECK(std::ranges::equal(selection.Selected(), std::vector<std::uint64_t>{20, 40}));

    selection.Clear();
    CHECK(selection.Selected().empty());
}

TEST_CASE("Editor Console highlights keep severity messages readable during hover selection and press")
{
    const auto luminance = [](const Keire::UiColor color)
    {
        const auto linear = [](const float channel)
        { return channel <= 0.04045F ? channel / 12.92F : std::pow((channel + 0.055F) / 1.055F, 2.4F); };
        return 0.2126F * linear(color.Red) + 0.7152F * linear(color.Green) + 0.0722F * linear(color.Blue);
    };
    const Keire::UiThemeDefinition theme;
    for (const auto text : {theme.AccentHovered, theme.Warning, theme.Error, theme.MutedText})
    {
        const auto highlights = KeireEditor::Detail::ConsoleHighlights(theme.Panel, text);
        CHECK(highlights.Selected != highlights.Hovered);
        CHECK(highlights.Active != highlights.Selected);
        for (const auto background : {highlights.Selected, highlights.Hovered, highlights.Active})
        {
            CHECK(background.Alpha == 1.0F);
            const float light = luminance(text);
            const float dark = luminance(background);
            CHECK((light + 0.05F) / (dark + 0.05F) >= 3.0F);
        }
    }
    // Light themes must tint toward dark text, rather than inheriting a dark-theme-only constant.
    const Keire::UiColor darkText{0.05F, 0.05F, 0.05F, 1.0F};
    const auto light = KeireEditor::Detail::ConsoleHighlights({0.95F, 0.95F, 0.95F, 1.0F}, darkText);
    for (const auto background : {light.Selected, light.Hovered, light.Active})
        CHECK((luminance(background) + 0.05F) / (luminance(darkText) + 0.05F) >= 3.0F);
}
