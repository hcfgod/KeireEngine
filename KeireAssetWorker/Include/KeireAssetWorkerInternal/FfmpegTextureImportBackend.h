#pragma once

#include "KeireInternal/Assets/TextureImportBackend.h"

namespace Keire::Detail
{
    [[nodiscard]] TextureDecodeBackend CreateFfmpegTextureImportBackend();
} // namespace Keire::Detail
