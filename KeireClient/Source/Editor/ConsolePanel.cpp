#include "KeireClient/Editor/ConsolePanel.h"

#include "KeireInternal/LogInternal.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <sstream>
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
            return theme.AccentHovered;
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

    void ConsoleSelection::Select(const std::span<const std::uint64_t> visibleOrder, const std::uint64_t serial,
                                  const bool range, const bool additive)
    {
        const auto selectedEntry = std::ranges::find(visibleOrder, serial);
        if (selectedEntry == visibleOrder.end())
            return;
        const auto anchorEntry = std::ranges::find(visibleOrder, m_Anchor);
        if (range && anchorEntry != visibleOrder.end())
        {
            if (!additive)
                m_Selected.clear();
            const auto first = std::min(anchorEntry, selectedEntry);
            const auto last = std::max(anchorEntry, selectedEntry);
            for (auto entry = first; entry != last + 1; ++entry)
                if (!Contains(*entry))
                    m_Selected.push_back(*entry);
            return;
        }

        m_Anchor = serial;
        const auto existing = std::ranges::find(m_Selected, serial);
        if (additive)
        {
            if (existing == m_Selected.end())
                m_Selected.push_back(serial);
            else
                m_Selected.erase(existing);
            return;
        }
        m_Selected.assign(1, serial);
    }

    void ConsoleSelection::Retain(const std::span<const std::uint64_t> available)
    {
        std::erase_if(m_Selected, [available](const auto serial)
                      { return std::ranges::find(available, serial) == available.end(); });
        if (std::ranges::find(available, m_Anchor) == available.end())
            m_Anchor = 0;
    }

    void ConsoleSelection::Clear() noexcept
    {
        m_Selected.clear();
        m_Anchor = 0;
    }

    bool ConsoleSelection::Contains(const std::uint64_t serial) const noexcept
    {
        return std::ranges::find(m_Selected, serial) != m_Selected.end();
    }

    void ConsolePanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.console", "Console", false});
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
            const auto countInfo = std::ranges::count_if(m_Messages, [](const Message& message)
                                                         { return message.Level < Keire::LogLevel::Warn; });
            const auto countWarnings = std::ranges::count(m_Messages, Keire::LogLevel::Warn, &Message::Level);
            const auto countErrors = std::ranges::count_if(m_Messages, [](const Message& message)
                                                           { return message.Level >= Keire::LogLevel::Error; });
            if (ui.Checkbox("Info " + std::to_string(countInfo), m_ShowInfo))
            {
            }
            ui.SameLine();
            if (ui.Checkbox("Warnings " + std::to_string(countWarnings), m_ShowWarnings))
            {
            }
            ui.SameLine();
            if (ui.Checkbox("Errors " + std::to_string(countErrors), m_ShowErrors))
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
                m_Selection.Clear();
            }
            if (m_Messages.empty())
            {
                ui.TextColored(theme.Success, "Ready");
                return;
            }
            const auto drawEntries = [&](const auto& entries)
            {
                std::vector<const Message*> visibleEntries;
                std::vector<std::uint64_t> available;
                available.reserve(entries.size());
                for (const auto& entry : entries)
                    available.push_back(entry.Serial);
                m_Selection.Retain(available);
                std::string previousCategory;
                std::string previousText;
                for (const auto& entry : entries)
                {
                    if (!m_Search.empty() && entry.Category.find(m_Search) == std::string::npos &&
                        entry.Text.find(m_Search) == std::string::npos)
                        continue;
                    if ((entry.Level < Keire::LogLevel::Warn && !m_ShowInfo) ||
                        (entry.Level == Keire::LogLevel::Warn && !m_ShowWarnings) ||
                        (entry.Level >= Keire::LogLevel::Error && !m_ShowErrors))
                        continue;
                    if (m_Collapse && entry.Category == previousCategory && entry.Text == previousText)
                        continue;
                    visibleEntries.push_back(&entry);
                    previousCategory = entry.Category;
                    previousText = entry.Text;
                }

                std::vector<std::uint64_t> visibleOrder;
                visibleOrder.reserve(visibleEntries.size());
                for (const auto* entry : visibleEntries)
                    visibleOrder.push_back(entry->Serial);

                const auto copySelection = [&]
                {
                    std::ostringstream copied;
                    bool first = true;
                    for (const auto& entry : entries)
                    {
                        if (!m_Selection.Contains(entry.Serial))
                            continue;
                        if (!first)
                            copied << '\n';
                        first = false;
                        copied << '[' << entry.Frame << "] [" << entry.Category << "] " << entry.Text;
                    }
                    try
                    {
                        if (!first && m_CopyText)
                            m_CopyText(copied.str());
                    }
                    catch (...)
                    {
                    }
                };

                for (const auto* entry : visibleEntries)
                {
                    const auto formatted =
                        "[" + std::to_string(entry->Frame) + "] [" + entry->Category + "] " + entry->Text;
                    auto id = ui.PushId(std::to_string(entry->Serial));
                    auto textColor = ui.PushStyleColor(Keire::UiStyleColorRole::Text, entry->Color);
                    if (ui.Selectable(formatted, m_Selection.Contains(entry->Serial)))
                        m_Selection.Select(visibleOrder, entry->Serial, ui.ShiftDown(), ui.ControlDown());
                    if (ui.LastItemState().DoubleClicked)
                        copySelection();
                    if (auto context = ui.BeginItemContextMenu("ConsoleEntry"); context)
                    {
                        if (!m_Selection.Contains(entry->Serial))
                            m_Selection.Select(visibleOrder, entry->Serial, false, false);
                        if (ui.MenuItem("Copy"))
                            copySelection();
                    }
                }
                if (ui.Shortcut({.Key = Keire::UiKey::C, .Primary = true}))
                    copySelection();
            };
            if (m_Paused)
                drawEntries(m_PausedSnapshot);
            else
                drawEntries(m_Messages);
        }
    }
} // namespace KeireEditor
