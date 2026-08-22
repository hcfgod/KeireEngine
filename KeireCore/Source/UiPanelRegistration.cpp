#include "Keire/UiWorkspace.h"

#include "KeireInternal/UiPanelRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    UiPanelRegistration::UiPanelRegistration() noexcept = default;
    UiPanelRegistration::UiPanelRegistration(std::unique_ptr<Impl> implementation) noexcept
        : m_Impl(std::move(implementation))
    {
    }
    UiPanelRegistration::UiPanelRegistration(UiPanelRegistration&& other) noexcept = default;
    UiPanelRegistration& UiPanelRegistration::operator=(UiPanelRegistration&& other) noexcept
    {
        if (this != &other)
        {
            if (m_Impl)
            {
                if (const auto registry = m_Impl->Registry.lock(); registry && registry->Alive)
                    registry->Panels.erase(m_Impl->Id);
            }
            m_Impl = std::move(other.m_Impl);
        }
        return *this;
    }
    UiPanelRegistration::~UiPanelRegistration()
    {
        if (!m_Impl)
            return;
        if (const auto registry = m_Impl->Registry.lock(); registry && registry->Alive)
            registry->Panels.erase(m_Impl->Id);
    }

    UiPanelRegistration::operator bool() const noexcept
    {
        if (!m_Impl)
            return false;
        const auto registry = m_Impl->Registry.lock();
        return registry && registry->Alive && registry->Panels.contains(m_Impl->Id);
    }

    std::string_view UiPanelRegistration::Id() const noexcept { return m_Impl ? m_Impl->Id : std::string_view{}; }

    std::string_view UiPanelRegistration::Title() const noexcept
    {
        if (!m_Impl)
            return {};
        if (const auto registry = m_Impl->Registry.lock();
            registry && registry->Alive && registry->Panels.contains(m_Impl->Id))
            return registry->Panels.at(m_Impl->Id).Title;
        return {};
    }

    bool UiPanelRegistration::Visible() const noexcept
    {
        if (!m_Impl)
            return false;
        if (const auto registry = m_Impl->Registry.lock();
            registry && registry->Alive && registry->Panels.contains(m_Impl->Id))
            return registry->Panels.at(m_Impl->Id).Visible;
        return false;
    }

    bool UiPanelRegistration::Locked() const noexcept
    {
        if (!m_Impl)
            return false;
        if (const auto registry = m_Impl->Registry.lock();
            registry && registry->Alive && registry->Panels.contains(m_Impl->Id))
            return registry->Panels.at(m_Impl->Id).Locked;
        return false;
    }

    bool UiPanelRegistration::Maximized() const noexcept
    {
        if (!m_Impl)
            return false;
        if (const auto registry = m_Impl->Registry.lock();
            registry && registry->Alive && registry->Panels.contains(m_Impl->Id))
            return registry->Panels.at(m_Impl->Id).Maximized;
        return false;
    }

    void UiPanelRegistration::SetVisible(const bool visible)
    {
        if (!m_Impl)
            throw std::logic_error("The UI panel registration is empty.");
        const auto registry = m_Impl->Registry.lock();
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        if (std::this_thread::get_id() != registry->OwnerThread)
            throw std::logic_error("UiPanelRegistration::SetVisible must run on the UI owner thread.");
        auto& panel = registry->Panels.at(m_Impl->Id);
        if (panel.Visible != visible)
        {
            panel.Visible = visible;
            registry->Visibility[panel.Id] = visible;
            registry->VisibilityDirty = true;
        }
    }

    void UiPanelRegistration::SetLocked(const bool locked)
    {
        if (!m_Impl)
            throw std::logic_error("The UI panel registration is empty.");
        const auto registry = m_Impl->Registry.lock();
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        if (std::this_thread::get_id() != registry->OwnerThread)
            throw std::logic_error("UiPanelRegistration::SetLocked must run on the UI owner thread.");
        registry->Panels.at(m_Impl->Id).Locked = locked;
    }

    void UiPanelRegistration::SetMaximized(const bool maximized)
    {
        if (!m_Impl)
            throw std::logic_error("The UI panel registration is empty.");
        const auto registry = m_Impl->Registry.lock();
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        if (std::this_thread::get_id() != registry->OwnerThread)
            throw std::logic_error("UiPanelRegistration::SetMaximized must run on the UI owner thread.");
        auto& panel = registry->Panels.at(m_Impl->Id);
        if (panel.Maximized == maximized)
            return;
        panel.Maximized = maximized;
        panel.DockRestoreRequested = !maximized;
        if (maximized)
        {
            panel.Visible = true;
            panel.FocusRequested = true;
        }
    }

    void UiPanelRegistration::RequestFocus()
    {
        if (!m_Impl)
            throw std::logic_error("The UI panel registration is empty.");
        const auto registry = m_Impl->Registry.lock();
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        if (std::this_thread::get_id() != registry->OwnerThread)
            throw std::logic_error("UiPanelRegistration::RequestFocus must run on the UI owner thread.");
        auto& panel = registry->Panels.at(m_Impl->Id);
        panel.Visible = true;
        panel.FocusRequested = true;
        registry->Visibility[panel.Id] = true;
        registry->VisibilityDirty = true;
    }

    const std::string& UiPanelRegistration::SubmittedName() const
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        return registry->Panels.at(m_Impl->Id).SubmittedName;
    }

    bool* UiPanelRegistration::VisibilityAddress()
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            throw std::logic_error("The UI panel registration is no longer active.");
        return &registry->Panels.at(m_Impl->Id).Visible;
    }

    bool UiPanelRegistration::ConsumeFocusRequest()
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            return false;
        auto& panel = registry->Panels.at(m_Impl->Id);
        return std::exchange(panel.FocusRequested, false);
    }

    bool UiPanelRegistration::PrepareWindow()
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            return false;
        auto& panel = registry->Panels.at(m_Impl->Id);
        if (panel.Maximized)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            if (const auto* dockspace = ImGui::DockBuilderGetNode(ImHashStr("Keire.RootDockSpace")))
            {
                ImGui::SetNextWindowPos(dockspace->Pos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(dockspace->Size, ImGuiCond_Always);
            }
        }
        else if (std::exchange(panel.DockRestoreRequested, false) && panel.LastDockId != 0)
            ImGui::SetNextWindowDockID(panel.LastDockId, ImGuiCond_Always);
        return panel.Maximized;
    }

    void UiPanelRegistration::NotifyWindowSubmitted() noexcept
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            return;
        auto& panel = registry->Panels.at(m_Impl->Id);
        if (!panel.Maximized && ImGui::GetWindowDockID() != 0)
            panel.LastDockId = ImGui::GetWindowDockID();
    }

    void UiPanelRegistration::NotifyVisibilityChanged(const bool previous)
    {
        const auto registry = m_Impl ? m_Impl->Registry.lock() : nullptr;
        if (!registry || !registry->Alive || !registry->Panels.contains(m_Impl->Id))
            return;
        const auto& panel = registry->Panels.at(m_Impl->Id);
        if (panel.Visible != previous)
        {
            registry->Visibility[panel.Id] = panel.Visible;
            registry->VisibilityDirty = true;
        }
    }

} // namespace Keire
