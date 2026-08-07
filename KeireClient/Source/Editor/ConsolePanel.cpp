#include "KeireClient/Editor/ConsolePanel.h"

#include "KeireInternal/LogInternal.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::UiColor LogColor(const Keire::LogLevel level,
                                              const Keire::UiThemeDefinition& theme) noexcept
        {
            if (level >= Keire::LogLevel::Error)
                return theme.Error;
            if (level == Keire::LogLevel::Warn)
                return theme.Warning;
            if (level == Keire::LogLevel::Trace || level == Keire::LogLevel::Debug)
                return theme.MutedText;
            return theme.Text;
        }

        void SplitCategory(const Keire::Detail::RetainedLogRecord& record, std::string& category, std::string& message)
        {
            category = record.Channel == Keire::Detail::LogChannel::Core ? "Core" : "Client";
            message = record.Message;
            if (!message.starts_with('['))
                return;
            const auto close = message.find("] ");
            if (close == std::string::npos || close <= 1 || close > 64)
                return;
            category = message.substr(1, close - 1);
            message.erase(0, close + 2);
        }
    } // namespace

    void ConsolePanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.console", "Console"});
    }

    void ConsolePanel::Add(std::string category, std::string message, const Keire::UiColor color,
                           const std::uint64_t frame, const Keire::LogLevel level)
    {
        constexpr std::size_t maximumMessages = 500;
        m_Messages.push_back({std::move(category), std::move(message), color, frame, level, m_NextSerial++});
        while (m_Messages.size() > maximumMessages)
            m_Messages.pop_front();
    }

    void ConsolePanel::LogAndCapture(std::string category, std::string message, const Keire::UiColor color,
                                     const std::uint64_t frame, const Keire::UiThemeDefinition& theme,
                                     const Keire::LogLevel level) noexcept
    {
        try
        {
            Keire::Log::GetClientLogger().Write(level, '[' + category + "] " + message);
            CaptureEngineLogs(frame, theme);
            return;
        }
        catch (...)
        {
            std::fprintf(stderr, "[%s] %s\n", category.c_str(), message.c_str());
        }
        try
        {
            Add(std::move(category), std::move(message), color, frame, level);
        }
        catch (...)
        {
            std::fputs("Editor Console could not retain a log entry.\n", stderr);
        }
    }

    void ConsolePanel::CaptureEngineLogs(const std::uint64_t frame, const Keire::UiThemeDefinition& theme) noexcept
    {
        std::vector<Keire::Detail::RetainedLogRecord> records;
        try
        {
            records = Keire::Detail::LogInternalAccess::ReadRecordsSince(m_LogSequence);
        }
        catch (...)
        {
            return;
        }
        for (const auto& record : records)
        {
            m_LogSequence = record.Sequence;
            try
            {
                std::string category;
                std::string message;
                SplitCategory(record, category, message);
                Add(std::move(category), std::move(message), LogColor(record.Level, theme), frame, record.Level);
            }
            catch (...)
            {
            }
        }
    }

    void ConsolePanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (auto console = ui.BeginPanel(m_Registration); console)
        {
            const auto countLevel = [this](const Keire::LogLevel level)
            { return std::ranges::count(m_Messages, level, &Message::Level); };
            if (ui.Checkbox(("Info " + std::to_string(countLevel(Keire::LogLevel::Info))).c_str(), m_ShowInfo))
            {
            }
            ui.SameLine();
            if (ui.Checkbox(("Warnings " + std::to_string(countLevel(Keire::LogLevel::Warn))).c_str(), m_ShowWarnings))
            {
            }
            ui.SameLine();
            if (ui.Checkbox(("Errors " + std::to_string(countLevel(Keire::LogLevel::Error))).c_str(), m_ShowErrors))
            {
            }
            ui.SameLine();
            (void)ui.Checkbox("Collapse", m_Collapse);
            (void)ui.InputTextWithHint("##ConsoleSearch", "Search Console", m_Search);
            ui.SameLine();
            if (ui.Checkbox("Pause", m_Paused))
            {
                if (m_Paused)
                    m_PausedSnapshot.assign(m_Messages.begin(), m_Messages.end());
                else
                    m_PausedSnapshot.clear();
            }
            ui.SameLine();
            if (ui.Button("Clear"))
            {
                m_Messages.clear();
                m_PausedSnapshot.clear();
                m_SelectedSerial = 0;
            }
            if (m_Messages.empty())
            {
                ui.TextColored(theme.Success, "Ready");
                return;
            }
            const auto drawEntries = [&](const auto& entries)
            {
                std::string previousCategory;
                std::string previousText;
                for (const auto& entry : entries)
                {
                    if (!m_Search.empty() && entry.Category.find(m_Search) == std::string::npos &&
                        entry.Text.find(m_Search) == std::string::npos)
                        continue;
                    if ((entry.Level == Keire::LogLevel::Info && !m_ShowInfo) ||
                        (entry.Level == Keire::LogLevel::Warn && !m_ShowWarnings) ||
                        (entry.Level >= Keire::LogLevel::Error && !m_ShowErrors))
                        continue;
                    if (m_Collapse && entry.Category == previousCategory && entry.Text == previousText)
                        continue;
                    const auto formatted =
                        "[" + std::to_string(entry.Frame) + "] [" + entry.Category + "] " + entry.Text;
                    auto id = ui.PushId(std::to_string(entry.Serial));
                    if (ui.Selectable(formatted, m_SelectedSerial == entry.Serial))
                        m_SelectedSerial = entry.Serial;
                    const auto copy = [&]
                    {
                        try
                        {
                            if (m_CopyText)
                                m_CopyText(formatted);
                        }
                        catch (...)
                        {
                        }
                    };
                    if (ui.LastItemState().DoubleClicked)
                        copy();
                    if (auto context = ui.BeginItemContextMenu("ConsoleEntry"); context)
                        if (ui.MenuItem("Copy"))
                            copy();
                    previousCategory = entry.Category;
                    previousText = entry.Text;
                }
            };
            if (m_Paused)
                drawEntries(m_PausedSnapshot);
            else
                drawEntries(m_Messages);
        }
    }
} // namespace KeireEditor
