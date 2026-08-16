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
