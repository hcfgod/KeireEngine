#include "KeireClient/Editor/ConsolePanel.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

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
