#pragma once

#include "Keire/Log.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class ConsolePanel final
    {
      public:
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
        };

        Keire::UiPanelRegistration m_Registration;
        std::deque<Message> m_Messages;
        std::vector<Message> m_PausedSnapshot;
        std::string m_Search;
        bool m_Paused = false;
        bool m_ShowInfo = true;
        bool m_ShowWarnings = true;
        bool m_ShowErrors = true;
        bool m_Collapse = true;
    };
} // namespace KeireEditor
