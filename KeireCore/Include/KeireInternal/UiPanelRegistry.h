#pragma once

#include "Keire/UiWorkspace.h"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace Keire::Detail
{
    struct UiPanelRecord final
    {
        std::string Id;
        std::string Title;
        std::string SubmittedName;
        bool Visible = true;
        bool DefaultVisible = true;
        bool FocusRequested = false;
        bool Locked = false;
        bool Maximized = false;
        bool DockRestoreRequested = false;
        std::uint32_t LastDockId = 0;
    };

    struct UiPanelRegistry final
    {
        std::unordered_map<std::string, UiPanelRecord> Panels;
        std::unordered_map<std::string, bool> Visibility;
        bool VisibilityDirty = false;
        bool Alive = true;
        std::thread::id OwnerThread = std::this_thread::get_id();
    };
} // namespace Keire::Detail

namespace Keire
{
    class UiPanelRegistration::Impl final
    {
      public:
        Impl(std::weak_ptr<Detail::UiPanelRegistry> registry, std::string id)
            : Registry(std::move(registry)), Id(std::move(id))
        {
        }

        std::weak_ptr<Detail::UiPanelRegistry> Registry;
        std::string Id;
    };
} // namespace Keire
