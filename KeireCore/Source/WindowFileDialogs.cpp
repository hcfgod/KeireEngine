#include "KeireInternal/WindowFileDialogsInternal.h"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        class OpenFileDialogState final : public RefCounted
        {
          public:
            mutable std::mutex Mutex;
            OpenFileDialogStatus Status = OpenFileDialogStatus::Pending;
            std::filesystem::path Path;
            std::string Error;
        };

        class SaveFileDialogState final : public RefCounted
        {
          public:
            mutable std::mutex Mutex;
            SaveFileDialogStatus Status = SaveFileDialogStatus::Pending;
            std::filesystem::path Path;
            std::string Error;
        };
    } // namespace Detail

    namespace
    {
        struct OpenFileDialogRequest final
        {
            WeakRef<Detail::OpenFileDialogState> State;
        };

        struct SaveFileDialogRequest final
        {
            WeakRef<Detail::SaveFileDialogState> State;
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

        void SDLCALL OpenFileDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<OpenFileDialogRequest> request(static_cast<OpenFileDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;
            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = OpenFileDialogStatus::Failed;
                state->Error = LastSdlError();
            }
            else if (!files[0])
            {
                state->Status = OpenFileDialogStatus::Cancelled;
            }
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = OpenFileDialogStatus::Selected;
            }
        }

        void SDLCALL SaveFileDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<SaveFileDialogRequest> request(static_cast<SaveFileDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;
            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = SaveFileDialogStatus::Failed;
                state->Error = LastSdlError();
            }
            else if (!files[0])
            {
                state->Status = SaveFileDialogStatus::Cancelled;
            }
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = SaveFileDialogStatus::Selected;
            }
        }
    } // namespace

    OpenFileDialogOperation::OpenFileDialogOperation(Ref<Detail::OpenFileDialogState> state) : m_State(std::move(state))
    {
    }

    OpenFileDialogOperation::~OpenFileDialogOperation() = default;

    OpenFileDialogStatus OpenFileDialogOperation::Status() const noexcept
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Status;
    }

    std::filesystem::path OpenFileDialogOperation::SelectedPath() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Path;
    }

    std::string OpenFileDialogOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Error;
    }

    class SaveFileDialogOperation::Impl final
    {
      public:
        explicit Impl(Ref<Detail::SaveFileDialogState> state) : State(std::move(state)) {}
        Ref<Detail::SaveFileDialogState> State;
    };

    SaveFileDialogOperation::SaveFileDialogOperation(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    SaveFileDialogOperation::~SaveFileDialogOperation() = default;

    SaveFileDialogStatus SaveFileDialogOperation::Status() const noexcept
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Status;
    }

    std::filesystem::path SaveFileDialogOperation::SelectedPath() const
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Path;
    }

    std::string SaveFileDialogOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Error;
    }

    Ref<OpenFileDialogOperation> Detail::ShowNativeOpenFileDialog(
        SDL_Window* parent, const OpenFileDialogSpecification& specification)
    {
        if (specification.Title.empty() || specification.Title.size() > 256 || specification.FilterName.size() > 128 ||
            specification.Extension.size() > 32 || specification.Extension.find('*') != std::string::npos ||
            specification.Extension.find('.') != std::string::npos)
        {
            throw std::invalid_argument("Open file dialog specification is invalid.");
        }
        auto state = CreateRef<OpenFileDialogState>();
        auto operation = CreateRef<OpenFileDialogOperation>(state);
        auto request = std::make_unique<OpenFileDialogRequest>();
        request->State = state;
        const auto location =
            specification.DefaultLocation.empty() ? std::string{} : Utf8PathString(specification.DefaultLocation);
        SDL_DialogFileFilter filter{specification.FilterName.c_str(), specification.Extension.c_str()};
        const SDL_DialogFileFilter* filters = specification.Extension.empty() ? nullptr : &filter;
        const int filterCount = filters ? 1 : 0;
        SDL_ShowOpenFileDialog(OpenFileDialogCompleted, request.release(), parent, filters, filterCount,
                               location.empty() ? nullptr : location.c_str(), false);
        return operation;
    }

    Ref<SaveFileDialogOperation> Detail::ShowNativeSaveFileDialog(
        SDL_Window* parent, const SaveFileDialogSpecification& specification)
    {
        if (specification.Title.empty() || specification.Title.size() > 256 || specification.DefaultName.size() > 256 ||
            specification.Extension.size() > 32 || specification.Extension.find('*') != std::string::npos ||
            specification.Extension.find('.') != std::string::npos)
        {
            throw std::invalid_argument("Save file dialog specification is invalid.");
        }
        auto state = CreateRef<SaveFileDialogState>();
        auto operation = CreateRef<SaveFileDialogOperation>(std::make_unique<SaveFileDialogOperation::Impl>(state));
        auto request = std::make_unique<SaveFileDialogRequest>();
        request->State = state;
        const auto location =
            specification.DefaultLocation.empty() ? std::string{} : Utf8PathString(specification.DefaultLocation);
        const auto defaultPath = specification.DefaultName.empty()
                                     ? location
                                     : Utf8PathString(specification.DefaultLocation / specification.DefaultName);
        SDL_DialogFileFilter filter{specification.FilterName.c_str(), specification.Extension.c_str()};
        const SDL_DialogFileFilter* filters = specification.Extension.empty() ? nullptr : &filter;
        const int filterCount = filters ? 1 : 0;
        SDL_ShowSaveFileDialog(SaveFileDialogCompleted, request.release(), parent, filters, filterCount,
                               defaultPath.empty() ? nullptr : defaultPath.c_str());
        return operation;
    }
} // namespace Keire
