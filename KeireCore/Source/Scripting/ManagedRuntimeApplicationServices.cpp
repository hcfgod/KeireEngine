#include "KeireInternal/Scripting/ManagedRuntimeApplicationServices.h"

#include "Keire/Application.h"
#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

namespace Keire::Detail
{
    ManagedApplicationInfo ManagedRuntimeApplicationServices::ManagedApplication() const
    {
        if (!m_Application)
            return {};
        const auto& specification = m_Application->Specification();
        const auto& windowing = specification.Windowing;
        const auto identifier =
            windowing.ApplicationIdentifier.empty() ? std::string("keire.project") : windowing.ApplicationIdentifier;
        return {.ProductName =
                    windowing.ApplicationName.empty() ? specification.MainWindow.Title : windowing.ApplicationName,
                .Version = windowing.ApplicationVersion.empty() ? std::string(GetBuildInfo().Version)
                                                                : windowing.ApplicationVersion,
                .Identifier = identifier,
                .PersistentDataPath = GetPreferenceDirectory() / "Applications" / identifier,
                .IsEditor = m_Editor};
    }

    void ManagedRuntimeApplicationServices::RequestManagedExit(const int exitCode) noexcept
    {
        if (m_Application)
            m_Application->RequestExit(exitCode);
    }

    double ManagedRuntimeApplicationServices::ManagedTimeScale() const noexcept
    {
        return m_Application ? m_Application->GetTime().TimeScale() : 1.0;
    }

    bool ManagedRuntimeApplicationServices::SetManagedTimeScale(const double scale) noexcept
    {
        if (!m_Application)
            return false;
        try
        {
            m_Application->GetTime().SetTimeScale(scale);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ManagedRuntimeApplicationServices::ManagedTimePaused() const noexcept
    {
        return m_Application && m_Application->GetTime().Paused();
    }

    bool ManagedRuntimeApplicationServices::SetManagedTimePaused(const bool paused) noexcept
    {
        if (!m_Application)
            return false;
        m_Application->GetTime().SetPaused(paused);
        return true;
    }

    ManagedScreenState ManagedRuntimeApplicationServices::ManagedScreen() const noexcept
    {
        try
        {
            const auto window = m_Application ? m_Application->MainWindow() : Ref<Window>{};
            if (!window)
                return {};
            const auto logical = window->LogicalSize();
            const auto pixels = window->PixelSize();
            return {.LogicalWidth = logical.Width,
                    .LogicalHeight = logical.Height,
                    .PixelWidth = pixels.Width,
                    .PixelHeight = pixels.Height,
                    .DisplayScale = window->DisplayScale(),
                    .Mode = window->Mode() == WindowMode::BorderlessFullscreen ? ManagedScreenMode::BorderlessFullscreen
                                                                               : ManagedScreenMode::Windowed,
                    .Focused = window->Focused(),
                    .Visible = window->Visible(),
                    .Minimized = window->Minimized(),
                    .VSync = m_Application->Specification().Render.PresentMode == RenderPresentMode::VSync};
        }
        catch (...)
        {
            return {};
        }
    }

    bool ManagedRuntimeApplicationServices::SetManagedScreen(const std::uint32_t width, const std::uint32_t height,
                                                             const ManagedScreenMode mode) noexcept
    {
        if (!m_Application || width < 64 || height < 64 || width > 16384 || height > 16384)
            return false;
        try
        {
            const auto window = m_Application->MainWindow();
            if (!window)
                return false;
            const auto previousMode = window->Mode();
            const auto previousSize = window->LogicalSize();
            const auto requestedMode = mode == ManagedScreenMode::BorderlessFullscreen
                                           ? WindowMode::BorderlessFullscreen
                                           : WindowMode::Windowed;
            try
            {
                if (window->Mode() != requestedMode)
                    window->SetMode(requestedMode);
                if (requestedMode == WindowMode::Windowed)
                    window->SetSize({width, height});
                return true;
            }
            catch (...)
            {
                try
                {
                    if (window->Mode() != previousMode)
                        window->SetMode(previousMode);
                    if (previousMode == WindowMode::Windowed)
                        window->SetSize(previousSize);
                }
                catch (...)
                {
                }
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
    }

    void ManagedRuntimeApplicationServices::BindManagedApplication(Application& application) noexcept
    {
        m_Application = &application;
    }

    void ManagedRuntimeApplicationServices::UnbindManagedApplication() noexcept { m_Application = nullptr; }
} // namespace Keire::Detail
