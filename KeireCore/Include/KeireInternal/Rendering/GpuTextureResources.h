#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstdint>

struct SDL_GPUTexture;
struct SDL_GPUSampler;

namespace Keire::RenderBackend
{
    struct GpuTextureResources final
    {
        SDL_GPUTexture* Texture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;
        bool HdrEncoded = false;
        TextureEnvironmentLayout EnvironmentLayout = TextureEnvironmentLayout::Auto;
        std::array<Vector4, 9> DiffuseIrradiance{};
        std::uint32_t MipLevels = 1;
        bool HasDiffuseIrradiance = false;
        std::uint64_t EstimatedBytes = 0;

        [[nodiscard]] bool Empty() const noexcept { return !Texture && !Sampler; }
    };
} // namespace Keire::RenderBackend
