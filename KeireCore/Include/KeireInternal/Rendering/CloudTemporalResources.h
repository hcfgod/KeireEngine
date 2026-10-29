#pragma once
#include "Keire/Math/Vector3d.h"

#include "Keire/Math/Math.h"
#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

#include <cstdint>
#include <optional>
#include <vector>

struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace Keire::RenderBackend
{
    enum class CloudTemporalPass : std::uint8_t
    {
        Trace,
        Resolve,
        Composite
    };

    struct CloudTemporalTextures final
    {
        SDL_GPUTexture* RadianceOpacity = nullptr;
        SDL_GPUTexture* CloudAndSceneDepth = nullptr;

        [[nodiscard]] bool Complete() const noexcept { return RadianceOpacity && CloudAndSceneDepth; }

        [[nodiscard]] bool Empty() const noexcept { return !RadianceOpacity && !CloudAndSceneDepth; }
    };

    struct CloudTemporalView final
    {
        CloudVolumeViewUniforms Volume;
        Matrix4 RotationViewProjection;
        Matrix4 UnjitteredProjection;
        std::uint64_t FrameId = 0;
        Vector3d WorldOrigin;
        std::optional<Detail::ResolvedPlanetFrame> PlanetFrame;
    };

    struct CloudTemporalWriter final
    {
        CloudTemporalTextures Current;
        CloudTemporalTextures History;
        std::optional<CloudTemporalView> RecordedView;
    };

    // One published pair and independent trace/history pairs for each accepted frame slot.
    // Ownership follows SurfaceResources retirement; only the render thread accesses these handles.
    struct CloudTemporalResources final
    {
        CloudTemporalTextures Published;
        std::vector<CloudTemporalWriter> Writers;
        std::optional<CloudTemporalView> PublishedView;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;

        [[nodiscard]] bool Empty() const noexcept { return Published.Empty() && Writers.empty(); }
    };

    // Lazy, half-resolution allocation. A failed replacement retains all previous textures and metadata.
    void EnsureCloudTemporalResources(SDL_GPUDevice* device, CloudTemporalResources& resources,
                                      std::uint32_t renderWidth, std::uint32_t renderHeight,
                                      std::uint32_t frameSlotCount);

    void ReleaseCloudTemporalResources(SDL_GPUDevice* device, CloudTemporalResources& resources) noexcept;

    // Begin/abandon a recording without changing the last successfully submitted history.
    void DiscardCloudTemporalRecording(CloudTemporalResources& resources, std::uint32_t frameSlot) noexcept;

    // Call only after successful command-buffer submission. A frame without cloud recording invalidates reuse.
    void PublishCloudTemporalHistory(CloudTemporalResources& resources, std::uint32_t frameSlot) noexcept;
} // namespace Keire::RenderBackend
