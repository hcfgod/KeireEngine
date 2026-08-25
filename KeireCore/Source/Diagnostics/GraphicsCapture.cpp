#include "KeireInternal/Diagnostics/GraphicsCaptureInternal.h"

#include <bit>
#include <cstdint>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace Keire::Internal
{
    namespace
    {
        // RenderDoc guarantees the 1.0.x layout remains the prefix of later APIs. Version 1.1.2 is the first API with
        // the current capture terminology. Opaque members preserve that published prefix without exposing SDK types.
        inline constexpr int RenderDocApiVersion112 = 10102;
        using OpaqueRenderDocFunction = void (*)();
        using RenderDocTriggerCapture = void (*)();
        using RenderDocIsFrameCapturing = std::uint32_t (*)();
        using RenderDocGetApi = int (*)(int, void**);

        struct RenderDocApi112Prefix
        {
            OpaqueRenderDocFunction GetApiVersion = nullptr;
            OpaqueRenderDocFunction SetCaptureOptionU32 = nullptr;
            OpaqueRenderDocFunction SetCaptureOptionF32 = nullptr;
            OpaqueRenderDocFunction GetCaptureOptionU32 = nullptr;
            OpaqueRenderDocFunction GetCaptureOptionF32 = nullptr;
            OpaqueRenderDocFunction SetFocusToggleKeys = nullptr;
            OpaqueRenderDocFunction SetCaptureKeys = nullptr;
            OpaqueRenderDocFunction GetOverlayBits = nullptr;
            OpaqueRenderDocFunction MaskOverlayBits = nullptr;
            OpaqueRenderDocFunction RemoveHooks = nullptr;
            OpaqueRenderDocFunction UnloadCrashHandler = nullptr;
            OpaqueRenderDocFunction SetCaptureFilePathTemplate = nullptr;
            OpaqueRenderDocFunction GetCaptureFilePathTemplate = nullptr;
            OpaqueRenderDocFunction GetNumCaptures = nullptr;
            OpaqueRenderDocFunction GetCapture = nullptr;
            RenderDocTriggerCapture TriggerCapture = nullptr;
            OpaqueRenderDocFunction IsTargetControlConnected = nullptr;
            OpaqueRenderDocFunction LaunchReplayUi = nullptr;
            OpaqueRenderDocFunction SetActiveWindow = nullptr;
            OpaqueRenderDocFunction StartFrameCapture = nullptr;
            RenderDocIsFrameCapturing IsFrameCapturing = nullptr;
        };

        struct ResolvedRenderDocApi
        {
            RenderDocApi112Prefix* Api = nullptr;
            bool Injected = false;
        };

        template <typename Destination, typename Source>
        [[nodiscard]] Destination FunctionPointer(const Source source) noexcept
        {
            static_assert(sizeof(Destination) == sizeof(Source));
            return std::bit_cast<Destination>(source);
        }

        [[nodiscard]] RenderDocGetApi FindInjectedRenderDocEntryPoint() noexcept
        {
#if defined(_WIN32)
            const auto module = GetModuleHandleW(L"renderdoc.dll");
            if (!module)
                return nullptr;
            return FunctionPointer<RenderDocGetApi>(GetProcAddress(module, "RENDERDOC_GetAPI"));
#elif defined(__linux__)
            // RTLD_DEFAULT searches the process' existing global objects and cannot load RenderDoc.
            return FunctionPointer<RenderDocGetApi>(dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI"));
#else
            return nullptr;
#endif
        }

        [[nodiscard]] ResolvedRenderDocApi ResolveInjectedRenderDocApi() noexcept
        {
#if defined(_WIN32)
            const bool injected = GetModuleHandleW(L"renderdoc.dll") != nullptr;
#elif defined(__linux__)
            const bool injected = dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI") != nullptr;
#else
            constexpr bool injected = false;
#endif
            const auto getApi = FindInjectedRenderDocEntryPoint();
            if (!getApi)
                return {.Injected = injected};

            void* opaqueApi = nullptr;
            if (getApi(RenderDocApiVersion112, &opaqueApi) != 1 || !opaqueApi)
                return {.Injected = true};
            return {.Api = static_cast<RenderDocApi112Prefix*>(opaqueApi), .Injected = true};
        }

        [[nodiscard]] std::mutex& RenderDocApiMutex() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }
    } // namespace

    GraphicsCaptureStatus QueryGraphicsCaptureStatus() noexcept
    {
        const std::scoped_lock lock(RenderDocApiMutex());
        const auto resolved = ResolveInjectedRenderDocApi();
        if (!resolved.Api || !resolved.Api->TriggerCapture || !resolved.Api->IsFrameCapturing)
        {
            return {.Provider = resolved.Injected ? GraphicsCaptureProvider::RenderDoc : GraphicsCaptureProvider::None,
                    .State = GraphicsCaptureState::Unavailable};
        }
        return {.Provider = GraphicsCaptureProvider::RenderDoc,
                .State = resolved.Api->IsFrameCapturing() != 0U ? GraphicsCaptureState::Capturing
                                                                : GraphicsCaptureState::Ready};
    }

    GraphicsCaptureRequestResult QueueGraphicsCaptureNextFrame() noexcept
    {
        const std::scoped_lock lock(RenderDocApiMutex());
        const auto resolved = ResolveInjectedRenderDocApi();
        if (!resolved.Api || !resolved.Api->TriggerCapture || !resolved.Api->IsFrameCapturing)
            return GraphicsCaptureRequestResult::Unavailable;
        if (resolved.Api->IsFrameCapturing() != 0U)
            return GraphicsCaptureRequestResult::CaptureAlreadyActive;

        resolved.Api->TriggerCapture();
        return GraphicsCaptureRequestResult::Queued;
    }
} // namespace Keire::Internal
