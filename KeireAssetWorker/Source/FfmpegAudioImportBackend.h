#pragma once

#include "KeireInternal/Audio/AudioImportBackend.h"

namespace Keire::Detail
{
    [[nodiscard]] AudioTranscodeBackend CreateFfmpegAudioImportBackend();
} // namespace Keire::Detail
