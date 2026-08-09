#include "KeireInternal/FolderDialogInternal.h"

#include <SDL3/SDL.h>

#include <memory>
#include <thread>

namespace Keire::Detail
{
    namespace
    {
        struct FolderDialogRequest final
        {
            WeakRef<FolderDialogState> State;
            std::string DefaultLocation;
            std::string InitialDiagnostic;
            std::thread::id OwnerThread;
            bool AllowZenityFallback = false;
            bool ZenityAttempted = false;
        };

        [[nodiscard]] std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
        }

        [[nodiscard]] std::string Utf8PathString(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        void SDLCALL FolderDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<FolderDialogRequest> request(static_cast<FolderDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;

            if (!files && request->AllowZenityFallback && !request->ZenityAttempted &&
                request->OwnerThread == std::this_thread::get_id())
            {
                request->ZenityAttempted = true;
                request->InitialDiagnostic = LastSdlError();
#if defined(__linux__)
                if (SDL_SetHintWithPriority(SDL_HINT_FILE_DIALOG_DRIVER, "zenity", SDL_HINT_OVERRIDE))
                {
                    const char* defaultLocation =
                        request->DefaultLocation.empty() ? nullptr : request->DefaultLocation.c_str();
                    auto* retryRequest = request.release();
                    SDL_ShowOpenFolderDialog(FolderDialogCompleted, retryRequest, nullptr, defaultLocation, false);
                    SDL_ResetHint(SDL_HINT_FILE_DIALOG_DRIVER);
                    return;
                }
#endif
            }

            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = FolderDialogStatus::Failed;
                state->Error = LastSdlError();
                if (!request->InitialDiagnostic.empty())
                {
                    state->Error = "Desktop portal failed: " + request->InitialDiagnostic +
                                   "; Zenity fallback failed: " + state->Error;
                }
            }
            else if (!files[0])
            {
                state->Status = FolderDialogStatus::Cancelled;
            }
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = FolderDialogStatus::Selected;
            }
        }
    } // namespace

    void ShowNativeFolderDialog(const Ref<FolderDialogState>& state, void* parent,
                                const std::filesystem::path& defaultLocation)
    {
        auto request = std::make_unique<FolderDialogRequest>();
        request->State = WeakRef<FolderDialogState>(state);
        request->DefaultLocation = defaultLocation.empty() ? std::string{} : Utf8PathString(defaultLocation);
        request->OwnerThread = std::this_thread::get_id();
#if defined(__linux__)
        request->AllowZenityFallback = SDL_GetHint(SDL_HINT_FILE_DIALOG_DRIVER) == nullptr;
#endif
        const char* initialLocation = request->DefaultLocation.empty() ? nullptr : request->DefaultLocation.c_str();
        auto* dialogRequest = request.release();
        SDL_ShowOpenFolderDialog(FolderDialogCompleted, dialogRequest, static_cast<SDL_Window*>(parent),
                                 initialLocation, false);
    }
} // namespace Keire::Detail
