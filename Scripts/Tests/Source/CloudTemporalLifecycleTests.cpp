// Standalone executable only: these SDL doubles must never enter the engine or its regular test runner.
#include "KeireInternal/Rendering/CloudTemporalResources.h"

#include <SDL3/SDL_gpu.h>
#include <doctest/doctest.h>

#include <cstddef>
#include <stdexcept>
#include <unordered_set>
#include <utility>

struct SDL_GPUDevice
{
};

struct SDL_GPUTexture
{
    SDL_GPUTextureCreateInfo Description;
};

namespace
{
    using namespace Keire::RenderBackend;

    struct TestState
    {
        int Calls = 0;
        int FailAt = 0;
        std::unordered_set<SDL_GPUTexture*> Live;
    } state;

    struct Fixture
    {
        SDL_GPUDevice Device;
        CloudTemporalResources Resources;

        Fixture() { state = {}; }

        ~Fixture()
        {
            ReleaseCloudTemporalResources(&Device, Resources);
            CHECK(state.Live.empty());
        }

        void Ensure(const std::uint32_t width = 1919, const std::uint32_t height = 1079, const std::uint32_t frames = 3)
        {
            EnsureCloudTemporalResources(&Device, Resources, width, height, frames);
        }
    };
} // namespace

extern "C"
{
    const char* SDLCALL SDL_GetError() { return "injected history allocation failure"; }

    SDL_GPUTexture* SDLCALL SDL_CreateGPUTexture(SDL_GPUDevice*, const SDL_GPUTextureCreateInfo* description)
    {
        ++state.Calls;
        if (state.Calls == state.FailAt)
        {
            return nullptr;
        }
        auto* texture = new SDL_GPUTexture{*description};
        state.Live.insert(texture);
        return texture;
    }

    void SDLCALL SDL_ReleaseGPUTexture(SDL_GPUDevice*, SDL_GPUTexture* texture)
    {
        const auto erased = state.Live.erase(texture);
        CHECK(erased == 1U);
        if (erased == 1U)
        {
            delete texture;
        }
    }
}

TEST_CASE("Cloud history allocation is lazy, correctly typed and reusable")
{
    Fixture fixture;
    CHECK(fixture.Resources.Empty());
    fixture.Ensure();
    CHECK(fixture.Resources.Width == 960U);
    CHECK(fixture.Resources.Height == 540U);
    REQUIRE(fixture.Resources.Writers.size() == 3U);
    CHECK(state.Live.size() == 14U);
    CHECK_FALSE(fixture.Resources.PublishedView);
    CHECK(fixture.Resources.Published.RadianceOpacity->Description.format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    CHECK(fixture.Resources.Published.CloudAndSceneDepth->Description.format == SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT);
    for (const auto* texture : state.Live)
    {
        CHECK(texture->Description.width == 960U);
        CHECK(texture->Description.height == 540U);
        CHECK(texture->Description.sample_count == SDL_GPU_SAMPLECOUNT_1);
        CHECK(texture->Description.usage == (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER));
    }
    const auto calls = state.Calls;
    fixture.Ensure();
    CHECK(state.Calls == calls);
    ReleaseCloudTemporalResources(&fixture.Device, fixture.Resources);
    ReleaseCloudTemporalResources(&fixture.Device, fixture.Resources);
    CHECK(fixture.Resources.Empty());
    CHECK(state.Live.empty());
}

TEST_CASE("Every partial cloud history allocation rolls back and can retry")
{
    for (int failure = 1; failure <= 14; ++failure)
    {
        Fixture fixture;
        state.FailAt = failure;
        CHECK_THROWS_AS(fixture.Ensure(), std::runtime_error);
        CHECK(fixture.Resources.Empty());
        CHECK(state.Live.empty());
        state.FailAt = 0;
        fixture.Ensure(1, 1, 1);
        CHECK(fixture.Resources.Width == 1U);
        CHECK(fixture.Resources.Height == 1U);
        CHECK(state.Live.size() == 6U);
    }
}

TEST_CASE("Failed cloud history resize preserves the published resources and metadata")
{
    for (int failure = 1; failure <= 14; ++failure)
    {
        Fixture fixture;
        fixture.Ensure(1280, 720, 1);
        fixture.Resources.PublishedView.emplace();
        fixture.Resources.PublishedView->FrameId = 42;
        const auto original = fixture.Resources.Published;
        state.FailAt = state.Calls + failure;
        CHECK_THROWS_AS(fixture.Ensure(), std::runtime_error);
        CHECK(fixture.Resources.Published.RadianceOpacity == original.RadianceOpacity);
        CHECK(fixture.Resources.Published.CloudAndSceneDepth == original.CloudAndSceneDepth);
        CHECK(fixture.Resources.Width == 640U);
        REQUIRE(fixture.Resources.PublishedView);
        CHECK(fixture.Resources.PublishedView->FrameId == 42U);
        CHECK(state.Live.size() == 6U);
        state.FailAt = 0;
        fixture.Ensure();
        CHECK_FALSE(fixture.Resources.PublishedView);
        CHECK(fixture.Resources.Width == 960U);
        CHECK(state.Live.size() == 14U);
    }
}

TEST_CASE("Cloud history publication swaps only the submitted writer and rejects abandoned work")
{
    Fixture fixture;
    fixture.Ensure();
    auto& writer = fixture.Resources.Writers[1];
    const auto oldPublished = fixture.Resources.Published;
    const auto submitted = writer.History;
    const auto otherWriter = fixture.Resources.Writers[0].History;
    writer.RecordedView.emplace();
    writer.RecordedView->FrameId = 10;
    DiscardCloudTemporalRecording(fixture.Resources, 1);
    CHECK_FALSE(writer.RecordedView);
    CHECK_FALSE(fixture.Resources.PublishedView);
    CHECK(fixture.Resources.Published.RadianceOpacity == oldPublished.RadianceOpacity);

    writer.RecordedView.emplace();
    writer.RecordedView->FrameId = 11;
    PublishCloudTemporalHistory(fixture.Resources, 1);
    REQUIRE(fixture.Resources.PublishedView);
    CHECK(fixture.Resources.PublishedView->FrameId == 11U);
    CHECK_FALSE(writer.RecordedView);
    CHECK(fixture.Resources.Published.RadianceOpacity == submitted.RadianceOpacity);
    CHECK(fixture.Resources.Published.CloudAndSceneDepth == submitted.CloudAndSceneDepth);
    CHECK(writer.History.RadianceOpacity == oldPublished.RadianceOpacity);
    CHECK(fixture.Resources.Writers[0].History.RadianceOpacity == otherWriter.RadianceOpacity);

    PublishCloudTemporalHistory(fixture.Resources, 0);
    CHECK_FALSE(fixture.Resources.PublishedView);
    CHECK(fixture.Resources.Published.RadianceOpacity == submitted.RadianceOpacity);
    DiscardCloudTemporalRecording(fixture.Resources, 99);
    PublishCloudTemporalHistory(fixture.Resources, 99);
    CHECK_FALSE(fixture.Resources.PublishedView);
}

TEST_CASE("Invalid cloud history allocation requests do not touch GPU resources")
{
    Fixture fixture;
    CHECK_THROWS_AS(fixture.Ensure(0, 1, 1), std::invalid_argument);
    CHECK_THROWS_AS(fixture.Ensure(1, 0, 1), std::invalid_argument);
    CHECK_THROWS_AS(fixture.Ensure(1, 1, 0), std::invalid_argument);
    CHECK_THROWS_AS(fixture.Ensure(1, 1, 4), std::invalid_argument);
    CHECK_THROWS_AS(EnsureCloudTemporalResources(nullptr, fixture.Resources, 1, 1, 1), std::invalid_argument);
    CHECK(state.Calls == 0);
}

TEST_CASE("Surface retirement transfers cloud history through exchange and deferred release")
{
    Fixture fixture;
    fixture.Ensure();
    const auto published = fixture.Resources.Published;
    fixture.Resources.PublishedView.emplace();
    fixture.Resources.PublishedView->FrameId = 42;

    // Match SurfaceResources' explicit ownership transfer into its deferred retirement queue.
    auto retired = std::exchange(fixture.Resources, {});
    CHECK(fixture.Resources.Empty());
    CHECK_FALSE(fixture.Resources.PublishedView);
    REQUIRE(retired.PublishedView);
    CHECK(retired.PublishedView->FrameId == 42U);
    CHECK(retired.Published.RadianceOpacity == published.RadianceOpacity);
    std::vector<CloudTemporalResources> pendingRetired;
    pendingRetired.push_back(retired);
    retired = {};

    fixture.Ensure(640, 480, 1);
    CHECK(state.Live.size() == 20U);
    ReleaseCloudTemporalResources(&fixture.Device, pendingRetired.front());
    CHECK(state.Live.size() == 6U);
    CHECK(pendingRetired.front().Empty());
    CHECK(fixture.Resources.Published.Complete());
}
