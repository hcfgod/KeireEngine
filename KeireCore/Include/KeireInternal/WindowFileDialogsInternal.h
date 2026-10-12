#pragma once

#include "Keire/Window.h"
#include "KeireInternal/FileSystem.h"

struct SDL_Window;

namespace Keire::Detail
{
    [[nodiscard]] inline std::string SaveFileDialogDefaultPath(const SaveFileDialogSpecification& specification)
    {
        return PathToUtf8(specification.DefaultName.empty()
                              ? specification.DefaultLocation
                              : specification.DefaultLocation / PathFromUtf8(specification.DefaultName));
    }

    [[nodiscard]] Ref<OpenFileDialogOperation>
    ShowNativeOpenFileDialog(SDL_Window* parent, const OpenFileDialogSpecification& specification);
    [[nodiscard]] Ref<SaveFileDialogOperation>
    ShowNativeSaveFileDialog(SDL_Window* parent, const SaveFileDialogSpecification& specification);
} // namespace Keire::Detail
