#include "KeireClient/Editor/ConsolePanel.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

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
