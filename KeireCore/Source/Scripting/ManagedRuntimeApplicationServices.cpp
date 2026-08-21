#include "KeireInternal/Scripting/ManagedRuntimeApplicationServices.h"

#include "Keire/Application.h"
#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

#include <map>

namespace Keire::Detail
{
    class ManagedRuntimeApplicationServices::RuntimeAssets final
    {
      public:
        struct Entry final
        {
            std::uint64_t Generation = 0;
            AssetHandle<Asset> Handle;
        };

        [[nodiscard]] std::uint64_t Load(Application* application, const std::uint64_t generation, const AssetId id,
                                         const AssetTypeId type, const AssetPriority priority) noexcept
        {
            constexpr std::size_t MaximumHandles = 4096;
            if (!application || generation == 0 || !id || !type || priority > AssetPriority::Background ||
                m_Handles.size() >= MaximumHandles)
                return 0;
            const auto assets = application->Assets();
            if (!assets || !assets->IsOpen())
                return 0;
            try
            {
                std::uint64_t token = m_Next++;
                if (token == 0)
                    token = m_Next++;
                if (token == 0 || m_Handles.contains(token))
                    return 0;
                m_Handles.emplace(token, Entry{generation, assets->Load(id, type, priority)});
                return token;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] std::optional<ManagedRuntimeAssetStatus> Status(const std::uint64_t token) const noexcept
        {
            const auto found = m_Handles.find(token);
            if (found == m_Handles.end())
                return std::nullopt;
            try
            {
                const auto& handle = found->second.Handle;
                return ManagedRuntimeAssetStatus{handle.State(), handle.UsingFallback(), handle.Revision(),
                                                 handle.Diagnostic()};
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] bool Release(const std::uint64_t token) noexcept { return m_Handles.erase(token) != 0; }

        void ReleaseGeneration(const std::uint64_t generation) noexcept
        {
            std::erase_if(m_Handles, [generation](const auto& entry) { return entry.second.Generation == generation; });
        }

        void Clear() noexcept { m_Handles.clear(); }

      private:
        std::map<std::uint64_t, Entry> m_Handles;
        std::uint64_t m_Next = 1;
    };

    ManagedRuntimeApplicationServices::ManagedRuntimeApplicationServices(const bool editor)
        : m_RuntimeAssets(std::make_unique<RuntimeAssets>()), m_Editor(editor)
    {
    }

    ManagedRuntimeApplicationServices::~ManagedRuntimeApplicationServices() = default;

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

    std::uint64_t ManagedRuntimeApplicationServices::BeginManagedRuntimeAssetLoad(const std::uint64_t generation,
                                                                                  const AssetId id,
                                                                                  const AssetTypeId type,
                                                                                  const AssetPriority priority) noexcept
    {
        return m_RuntimeAssets->Load(m_Application, generation, id, type, priority);
    }

    std::optional<ManagedRuntimeAssetStatus>
    ManagedRuntimeApplicationServices::ManagedRuntimeAsset(const std::uint64_t handle) const noexcept
    {
        return m_RuntimeAssets->Status(handle);
    }

    bool ManagedRuntimeApplicationServices::ReleaseManagedRuntimeAsset(const std::uint64_t handle) noexcept
    {
        return m_RuntimeAssets->Release(handle);
    }

    void ManagedRuntimeApplicationServices::ReleaseManagedRuntimeAssets(const std::uint64_t generation) noexcept
    {
        m_RuntimeAssets->ReleaseGeneration(generation);
    }

    void ManagedRuntimeApplicationServices::BindManagedApplication(Application& application) noexcept
    {
        m_Application = &application;
    }

    void ManagedRuntimeApplicationServices::UnbindManagedApplication() noexcept
    {
        m_RuntimeAssets->Clear();
        m_Application = nullptr;
    }
} // namespace Keire::Detail
