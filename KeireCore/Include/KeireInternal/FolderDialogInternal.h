#pragma once

#include "Keire/Window.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace Keire::Detail
{
    class FolderDialogState final : public RefCounted
    {
      public:
        mutable std::mutex Mutex;
        FolderDialogStatus Status = FolderDialogStatus::Pending;
        std::filesystem::path Path;
        std::string Error;
    };

    void ShowNativeFolderDialog(const Ref<FolderDialogState>& state, void* parent,
                                const std::filesystem::path& defaultLocation);
} // namespace Keire::Detail
