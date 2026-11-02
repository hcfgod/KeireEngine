#pragma once
#include "Keire/Math/Vector3d.h"

#include "Keire/Math/Math.h"
#include "KeireInternal/Rendering/CloudVolumeViewUniforms.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
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
        std::array<Detail::ResolvedPlanetFrame, MaximumPlanetAtmospheres> PlanetFrames;
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
        // Read-only history from the previous extent. Normalized-UV reprojection does not
        // require matching texture dimensions. Never swap this pair into a current writer.
        CloudTemporalTextures PreviousExtent;
        std::optional<CloudTemporalView> PreviousExtentView;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;

        [[nodiscard]] bool Empty() const noexcept
        {
            return Published.Empty() && PreviousExtent.Empty() && Writers.empty();
        }
    };

    // Transfer only submitted, read-only history. Old writers remain with the old surface's
    // fence retirement; the new surface owns this pair until its next resize or retirement.
    [[nodiscard]] inline bool RetainCloudHistoryForResize(CloudTemporalResources& source,
                                                          CloudTemporalResources& destination) noexcept
    {
        if (&source == &destination || !destination.PreviousExtent.Empty())
        {
            return false;
        }
        if (source.Published.Complete() && source.PublishedView)
        {
            destination.PreviousExtent = std::exchange(source.Published, {});
            destination.PreviousExtentView = std::exchange(source.PublishedView, std::nullopt);
            return true;
        }
        if (source.PreviousExtent.Complete() && source.PreviousExtentView)
        {
            destination.PreviousExtent = std::exchange(source.PreviousExtent, {});
            destination.PreviousExtentView = std::exchange(source.PreviousExtentView, std::nullopt);
            return true;
        }
        return false;
    }

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
