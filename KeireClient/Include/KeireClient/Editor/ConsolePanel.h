#pragma once

#include "Keire/Log.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class ConsolePanel final
    {
      public:
        using CopyTextCallback = std::function<void(std::string_view)>;

        explicit ConsolePanel(CopyTextCallback copyText = {}) : m_CopyText(std::move(copyText)) {}
        void Attach(Keire::UiWorkspace& workspace);
        void Add(std::string category, std::string message, Keire::UiColor color, std::uint64_t frame,
                 Keire::LogLevel level = Keire::LogLevel::Info);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct Message
        {
            std::string Category;
            std::string Text;
            Keire::UiColor Color;
            std::uint64_t Frame = 0;
            Keire::LogLevel Level = Keire::LogLevel::Info;
            std::uint64_t Serial = 0;
        };

        CopyTextCallback m_CopyText;
        Keire::UiPanelRegistration m_Registration;
        std::deque<Message> m_Messages;
        std::vector<Message> m_PausedSnapshot;
        std::string m_Search;
        std::uint64_t m_NextSerial = 1;
        std::uint64_t m_SelectedSerial = 0;
        bool m_Paused = false;
        bool m_ShowInfo = true;
        bool m_ShowWarnings = true;
        bool m_ShowErrors = true;
        bool m_Collapse = true;
    };
} // namespace KeireEditor
