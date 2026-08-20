#include "KeireInternal/Scripting/ManagedRuntimeFoundation.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace Keire::Detail
{
    namespace
    {
        enum class ApplicationText : std::uint8_t
        {
            ProductName,
            Version,
            Identifier,
            PersistentDataPath
        };

        struct NativeScreenState
        {
            std::uint32_t LogicalWidth = 0;
            std::uint32_t LogicalHeight = 0;
            std::uint32_t PixelWidth = 0;
            std::uint32_t PixelHeight = 0;
            float DisplayScale = 1.0F;
            std::uint8_t Mode = 0;
            std::uint8_t Focused = 0;
            std::uint8_t Visible = 0;
            std::uint8_t Minimized = 0;
            std::uint8_t VSync = 1;
        };
        static_assert(sizeof(NativeScreenState) == 28);

        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            const auto encoded = path.u8string();
            return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
        }

        [[nodiscard]] int CopyText(const std::string_view text, std::uint8_t* destination, const int capacity) noexcept
        {
            if (capacity < 0 || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return -1;
            const auto size = static_cast<int>(text.size());
            if (!destination || capacity == 0)
                return size;
            if (capacity < size)
                return -1;
            if (size > 0)
                std::memcpy(destination, text.data(), text.size());
            return size;
        }

        [[nodiscard]] int GetApplicationText(const std::uint8_t field, std::uint8_t* destination,
                                             const int capacity) noexcept
        {
            if (!ActiveServices || field > static_cast<std::uint8_t>(ApplicationText::PersistentDataPath))
                return CopyText({}, destination, capacity);
            try
            {
                const auto info = ActiveServices->ManagedApplication();
                switch (static_cast<ApplicationText>(field))
                {
                case ApplicationText::ProductName:
                    return CopyText(info.ProductName, destination, capacity);
                case ApplicationText::Version:
                    return CopyText(info.Version, destination, capacity);
                case ApplicationText::Identifier:
                    return CopyText(info.Identifier, destination, capacity);
                case ApplicationText::PersistentDataPath:
                    return CopyText(PathText(info.PersistentDataPath), destination, capacity);
                }
            }
            catch (...)
            {
            }
            return CopyText({}, destination, capacity);
        }

        [[nodiscard]] std::uint8_t IsEditor() noexcept
        {
            try
            {
                return ActiveServices && ActiveServices->ManagedApplication().IsEditor ? 1 : 0;
            }
            catch (...)
            {
                return 0;
            }
        }

        void RequestExit(const int exitCode) noexcept
        {
            if (ActiveServices)
                ActiveServices->RequestManagedExit(exitCode);
        }

        [[nodiscard]] double GetTimeScale() noexcept
        {
            return ActiveServices ? ActiveServices->ManagedTimeScale() : 1.0;
        }

        [[nodiscard]] std::uint8_t SetTimeScale(const double scale) noexcept
        {
            return ActiveServices && std::isfinite(scale) && ActiveServices->SetManagedTimeScale(scale) ? 1 : 0;
        }

        [[nodiscard]] std::uint8_t IsTimePaused() noexcept
        {
            return ActiveServices && ActiveServices->ManagedTimePaused() ? 1 : 0;
        }

        [[nodiscard]] std::uint8_t SetTimePaused(const std::uint8_t paused) noexcept
        {
            return ActiveServices && ActiveServices->SetManagedTimePaused(paused != 0) ? 1 : 0;
        }

        [[nodiscard]] std::uint8_t GetScreenState(NativeScreenState* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            const auto state = ActiveServices->ManagedScreen();
            destination->LogicalWidth = state.LogicalWidth;
            destination->LogicalHeight = state.LogicalHeight;
            destination->PixelWidth = state.PixelWidth;
            destination->PixelHeight = state.PixelHeight;
            destination->DisplayScale = state.DisplayScale;
            destination->Mode = static_cast<std::uint8_t>(state.Mode);
            destination->Focused = state.Focused ? 1 : 0;
            destination->Visible = state.Visible ? 1 : 0;
            destination->Minimized = state.Minimized ? 1 : 0;
            destination->VSync = state.VSync ? 1 : 0;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetScreen(const std::uint32_t width, const std::uint32_t height,
                                             const std::uint8_t mode) noexcept
        {
            if (!ActiveServices || mode > static_cast<std::uint8_t>(ManagedScreenMode::BorderlessFullscreen))
                return 0;
            return ActiveServices->SetManagedScreen(width, height, static_cast<ManagedScreenMode>(mode)) ? 1 : 0;
        }
    } // namespace

    ManagedRuntimeFoundationScope::ManagedRuntimeFoundationScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeFoundationScope::~ManagedRuntimeFoundationScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeFoundation(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeFoundation", "GetApplicationTextIcall",
                                 reinterpret_cast<void*>(&GetApplicationText));
        assembly.AddInternalCall("Keire.NativeFoundation", "IsEditorIcall", reinterpret_cast<void*>(&IsEditor));
        assembly.AddInternalCall("Keire.NativeFoundation", "RequestExitIcall", reinterpret_cast<void*>(&RequestExit));
        assembly.AddInternalCall("Keire.NativeFoundation", "GetTimeScaleIcall", reinterpret_cast<void*>(&GetTimeScale));
        assembly.AddInternalCall("Keire.NativeFoundation", "SetTimeScaleIcall", reinterpret_cast<void*>(&SetTimeScale));
        assembly.AddInternalCall("Keire.NativeFoundation", "IsTimePausedIcall", reinterpret_cast<void*>(&IsTimePaused));
        assembly.AddInternalCall("Keire.NativeFoundation", "SetTimePausedIcall",
                                 reinterpret_cast<void*>(&SetTimePaused));
        assembly.AddInternalCall("Keire.NativeFoundation", "GetScreenStateIcall",
                                 reinterpret_cast<void*>(&GetScreenState));
        assembly.AddInternalCall("Keire.NativeFoundation", "SetScreenIcall", reinterpret_cast<void*>(&SetScreen));
    }
} // namespace Keire::Detail
