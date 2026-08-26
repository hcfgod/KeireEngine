#pragma once

#include "Keire/Window.h"

struct SDL_Window;

namespace Keire::Detail
{
    [[nodiscard]] Ref<OpenFileDialogOperation> ShowNativeOpenFileDialog(
        SDL_Window* parent, const OpenFileDialogSpecification& specification);
    [[nodiscard]] Ref<SaveFileDialogOperation> ShowNativeSaveFileDialog(
        SDL_Window* parent, const SaveFileDialogSpecification& specification);
} // namespace Keire::Detail
