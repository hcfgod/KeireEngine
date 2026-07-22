#pragma once

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
        void Add(std::string category, std::string message, Keire::UiColor color, std::uint64_t frame);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct Message
        {
            std::string Category;
            std::string Text;
            Keire::UiColor Color;
            std::uint64_t Frame = 0;
        };

        Keire::UiPanelRegistration m_Registration;
        std::deque<Message> m_Messages;
        std::vector<Message> m_PausedSnapshot;
        std::string m_Search;
        bool m_Paused = false;
    };
} // namespace KeireEditor
