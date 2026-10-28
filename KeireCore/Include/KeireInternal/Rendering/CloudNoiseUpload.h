#pragma once

#include "KeireInternal/Rendering/CloudNoiseVolume.h"

struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace Keire::RenderBackend
{
    /// Submits a complete linear RGBA8 3D upload. The caller owns the returned texture.
    /// Failures release partial resources. Submit subsequent sampling commands on the same device.
    [[nodiscard]] SDL_GPUTexture* UploadCloudNoiseVolume(SDL_GPUDevice* device, const CloudNoiseVolume& volume);
} // namespace Keire::RenderBackend
