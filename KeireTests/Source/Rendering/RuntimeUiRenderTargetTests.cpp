#include "Keire/Core.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderFramePacketInternal.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"
#include "KeireInternal/Rendering/RuntimeUiGeometryInternal.h"
#include "KeireInternal/Rendering/RuntimeUiRenderTargetInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>

namespace
{
    void UseRuntimeUiTargetDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    enum class SubmissionCase
    {
        LegacyOverlay,
        FilteredRoot,
        CameraOverlay,
        WorldSurface,
        WorldSurfaceWithoutDepth,
        RenderTexture
    };

    struct SubmissionProbe final
    {
        std::size_t PendingCommands = 0;
        std::size_t ExpectedCommands = 0;
        std::string Diagnostic;
        Keire::RenderStatistics Statistics;
    };

    [[nodiscard]] std::size_t CommandsWithin(const Keire::RuntimeUiTree& tree, const Keire::RuntimeUiElementId root)
    {
        std::size_t result = 0;
        for (const auto& command : tree.DrawCommands())
        {
            auto element = command.Element;
            while (element && element != root)
            {
                const auto state = tree.State(element);
                element = state ? state->Parent : Keire::RuntimeUiElementId{};
            }
            if (element == root)
                ++result;
        }
        return result;
    }

    class RuntimeUiTargetLayer final : public Keire::Layer
    {
      public:
        RuntimeUiTargetLayer(SubmissionProbe& probe, const SubmissionCase submissionCase)
            : Layer("runtime-ui-target"), m_Probe(probe), m_Case(submissionCase)
        {
        }

      protected:
        void OnAttach() override
        {
            m_View = Owner().Renderer()->CreateView({.Name = "runtime-ui-target",
                                                     .Width = 64,
                                                     .Height = 64,
                                                     .Depth = m_Case != SubmissionCase::WorldSurfaceWithoutDepth});
            m_Tree = Keire::CreateRef<Keire::RuntimeUiTree>();
            m_First = m_Tree->Create(Keire::RuntimeUiElementType::Panel);
            const auto firstChild = m_Tree->Create(Keire::RuntimeUiElementType::Text, m_First);
            m_Second = m_Tree->Create(Keire::RuntimeUiElementType::Panel);
            Keire::RuntimeUiStyle firstStyle;
            firstStyle.Width = 24.0F;
            firstStyle.Height = 24.0F;
            firstStyle.Background = {1.0F, 0.0F, 0.0F, 1.0F};
            REQUIRE(m_Tree->SetStyle(m_First, firstStyle));
            Keire::RuntimeUiStyle childStyle;
            childStyle.Width = 12.0F;
            childStyle.Height = 12.0F;
            REQUIRE(m_Tree->SetStyle(firstChild, childStyle));
            REQUIRE(m_Tree->SetContent(firstChild, {.Text = "A"}));
            auto secondStyle = firstStyle;
            secondStyle.X = 32.0F;
            secondStyle.Background = {0.0F, 1.0F, 0.0F, 1.0F};
            REQUIRE(m_Tree->SetStyle(m_Second, secondStyle));
            m_Tree->Layout(64.0F, 64.0F);
        }

        void OnUpdate(const Keire::Time&) override
        {
            auto renderer = Owner().Renderer();
            m_Probe.ExpectedCommands = m_Case == SubmissionCase::LegacyOverlay ? m_Tree->DrawCommands().size()
                                                                               : CommandsWithin(*m_Tree, m_First);
            if (m_Case == SubmissionCase::LegacyOverlay)
            {
                renderer->SubmitRuntimeUi(m_Tree);
            }
            else
            {
                Keire::RuntimeUiRenderSubmission submission;
                submission.Tree = m_Tree;
                submission.Root = m_First;
                submission.View = m_View;
                submission.Viewport = {64.0F, 64.0F};
                if (m_Case == SubmissionCase::FilteredRoot)
                    submission.Target = Keire::RuntimeUiRenderTarget::ScreenOverlay;
                else if (m_Case == SubmissionCase::CameraOverlay)
                    submission.Target = Keire::RuntimeUiRenderTarget::CameraOverlay;
                else if (m_Case == SubmissionCase::WorldSurface || m_Case == SubmissionCase::WorldSurfaceWithoutDepth)
                    submission.Target = Keire::RuntimeUiRenderTarget::WorldSurface;
                else
                {
                    submission.Target = Keire::RuntimeUiRenderTarget::RenderTexture;
                    submission.RenderTexture = Keire::AssetId::Generate();
                }
                try
                {
                    renderer->SubmitRuntimeUiTarget(std::move(submission));
                }
                catch (const std::logic_error& error)
                {
                    m_Probe.Diagnostic = error.what();
                }
            }
            m_Probe.PendingCommands = Keire::RenderSystemInternalAccess::RuntimeUiCommandCount(*renderer);
            Owner().RequestExit();
        }

        void OnDetach() noexcept override
        {
            try
            {
                Owner().Renderer()->Flush();
                m_Probe.Statistics = Owner().Renderer()->Statistics();
            }
            catch (...)
            {
            }
        }

      private:
        SubmissionProbe& m_Probe;
        SubmissionCase m_Case;
        Keire::Ref<Keire::RenderView> m_View;
        Keire::Ref<Keire::RuntimeUiTree> m_Tree;
        Keire::RuntimeUiElementId m_First;
        Keire::RuntimeUiElementId m_Second;
    };

    [[nodiscard]] Keire::ApplicationSpecification TargetSpecification()
    {
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "runtime-ui-target";
        specification.MainWindow.Visible = false;
        specification.SuspendWhenMainWindowMinimized = false;
        specification.ManageLogging = false;
        specification.Render.Mode = Keire::RenderMode::Headless;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        return specification;
    }

    [[nodiscard]] SubmissionProbe RunSubmissionCase(const SubmissionCase submissionCase)
    {
        SubmissionProbe probe;
        Keire::Application application(TargetSpecification());
        (void)application.PushLayer(std::make_unique<RuntimeUiTargetLayer>(probe, submissionCase));
        CHECK(application.Run() == 0);
        return probe;
    }
} // namespace

TEST_CASE("Runtime UI target submissions preserve the legacy overlay wrapper and filter selected roots")
{
    UseRuntimeUiTargetDummyVideoDriver();
    const auto legacy = RunSubmissionCase(SubmissionCase::LegacyOverlay);
    CHECK(legacy.PendingCommands == legacy.ExpectedCommands);
    CHECK(legacy.Statistics.AcceptedFrames == 1U);

    const auto filtered = RunSubmissionCase(SubmissionCase::FilteredRoot);
    CHECK(filtered.PendingCommands == filtered.ExpectedCommands);
    CHECK(filtered.ExpectedCommands < legacy.ExpectedCommands);
    CHECK(filtered.Statistics.AcceptedFrames == 1U);
}

TEST_CASE("Runtime UI render-texture targets enter a logical offscreen packet and never the overlay")
{
    UseRuntimeUiTargetDummyVideoDriver();
    const auto probe = RunSubmissionCase(SubmissionCase::RenderTexture);
    CHECK(probe.PendingCommands == probe.ExpectedCommands);
    CHECK(probe.Diagnostic.empty());
    CHECK(probe.Statistics.AcceptedFrames == 1U);
}

TEST_CASE("Camera-overlay runtime UI is captured against an explicit render-view surface")
{
    UseRuntimeUiTargetDummyVideoDriver();
    const auto probe = RunSubmissionCase(SubmissionCase::CameraOverlay);
    CHECK(probe.PendingCommands == probe.ExpectedCommands);
    CHECK(probe.Diagnostic.empty());
    CHECK(probe.Statistics.AcceptedFrames == 1U);
}

TEST_CASE("World runtime UI packet ownership is rebound only to an explicit frame slot and device generation")
{
    Keire::RenderBackend::RenderFramePacket frame;
    frame.DeviceGeneration = 7U;
    frame.FrameSlot = 2U;
    Keire::RenderBackend::CapturedRuntimeUiWorldPanel panel;
    panel.Surface.Id = 1U;
    panel.Surface.Epoch = 3U;
    panel.Surface.Lifetime = std::make_shared<const Keire::RenderBackend::RenderSurfaceEpochLease>(1U, 3U);
    panel.Viewport = {640.0F, 360.0F};
    panel.LayoutScale = 1.0F;
    frame.RuntimeUiWorldPanels.push_back(panel);
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiWorldPanelOwnershipValid(frame.RuntimeUiWorldPanels.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiWorldPanels(frame);
    CHECK(Keire::RenderBackend::RuntimeUiWorldPanelOwnershipValid(frame.RuntimeUiWorldPanels.front(), frame));

    frame.DeviceGeneration = 8U;
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiWorldPanelOwnershipValid(frame.RuntimeUiWorldPanels.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiWorldPanels(frame);
    CHECK(Keire::RenderBackend::RuntimeUiWorldPanelOwnershipValid(frame.RuntimeUiWorldPanels.front(), frame));
    CHECK(frame.RuntimeUiWorldPanels.front().Viewport == (Keire::Vector2{640.0F, 360.0F}));
    CHECK(frame.RuntimeUiWorldPanels.front().LayoutScale == doctest::Approx(1.0F));

    Keire::RenderBackend::RenderFramePacket resizedFrame;
    resizedFrame.DeviceGeneration = 8U;
    resizedFrame.FrameSlot = 0U;
    panel.Viewport = {1440.0F, 900.0F};
    panel.LayoutScale = 2.3717082F;
    resizedFrame.RuntimeUiWorldPanels.push_back(panel);
    Keire::RenderBackend::QualifyRuntimeUiWorldPanels(resizedFrame);
    CHECK(Keire::RenderBackend::RuntimeUiWorldPanelOwnershipValid(resizedFrame.RuntimeUiWorldPanels.front(),
                                                                  resizedFrame));
    CHECK(resizedFrame.RuntimeUiWorldPanels.front().Viewport == (Keire::Vector2{1440.0F, 900.0F}));
    CHECK(resizedFrame.RuntimeUiWorldPanels.front().LayoutScale == doctest::Approx(2.3717082F));
    CHECK(frame.RuntimeUiWorldPanels.front().Viewport == (Keire::Vector2{640.0F, 360.0F}));
}

TEST_CASE("World runtime UI geometry retains authored physical size across viewport scaling")
{
    Keire::RenderBackend::CapturedRuntimeUiWorldPanel initial;
    initial.Commands.push_back({.Type = Keire::RuntimeUiDrawType::Quad,
                                .Rect = {0.0F, 0.0F, 400.0F, 200.0F},
                                .ClipRect = {0.0F, 0.0F, 400.0F, 200.0F}});
    initial.Viewport = {640.0F, 360.0F};
    initial.ReferenceResolution = {400.0F, 200.0F};
    initial.Pivot = {0.5F, 0.5F};
    initial.WorldUnitsPerPixel = {0.005F, 0.005F};
    initial.LayoutScale = 1.0F;
    const auto initialGeometry = Keire::RenderBackend::BuildRuntimeUiWorldGeometry(initial, 1024U, 1024U);

    auto maximized = initial;
    maximized.Viewport = {1440.0F, 900.0F};
    maximized.LayoutScale = 2.3717082F;
    maximized.Commands.front().Rect = {0.0F, 0.0F, 400.0F * maximized.LayoutScale, 200.0F * maximized.LayoutScale};
    maximized.Commands.front().ClipRect = maximized.Commands.front().Rect;
    const auto maximizedGeometry = Keire::RenderBackend::BuildRuntimeUiWorldGeometry(maximized, 1024U, 1024U);

    REQUIRE(initialGeometry.Vertices.size() == maximizedGeometry.Vertices.size());
    REQUIRE_FALSE(initialGeometry.Vertices.empty());
    for (std::size_t index = 0; index < initialGeometry.Vertices.size(); ++index)
    {
        CHECK(maximizedGeometry.Vertices[index].Position.X ==
              doctest::Approx(initialGeometry.Vertices[index].Position.X));
        CHECK(maximizedGeometry.Vertices[index].Position.Y ==
              doctest::Approx(initialGeometry.Vertices[index].Position.Y));
        CHECK(maximizedGeometry.Vertices[index].Position.Z ==
              doctest::Approx(initialGeometry.Vertices[index].Position.Z));
    }
}

TEST_CASE("Camera-overlay runtime UI packets are invalidated across frame slots and device generations")
{
    Keire::RenderBackend::RenderFramePacket frame;
    frame.DeviceGeneration = 11U;
    frame.FrameSlot = 1U;
    Keire::RenderBackend::CapturedRuntimeUiCameraPanel panel;
    panel.Surface.Id = 5U;
    panel.Surface.Epoch = 2U;
    panel.Surface.Lifetime = std::make_shared<const Keire::RenderBackend::RenderSurfaceEpochLease>(5U, 2U);
    frame.RuntimeUiCameraPanels.push_back(panel);
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiCameraPanelOwnershipValid(frame.RuntimeUiCameraPanels.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiCameraPanels(frame);
    CHECK(Keire::RenderBackend::RuntimeUiCameraPanelOwnershipValid(frame.RuntimeUiCameraPanels.front(), frame));

    frame.DeviceGeneration = 12U;
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiCameraPanelOwnershipValid(frame.RuntimeUiCameraPanels.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiCameraPanels(frame);
    CHECK(Keire::RenderBackend::RuntimeUiCameraPanelOwnershipValid(frame.RuntimeUiCameraPanels.front(), frame));
}

TEST_CASE("Runtime UI image batching preserves command order and only coalesces adjacent texture runs")
{
    const auto firstTexture = Keire::AssetId::Generate();
    const auto secondTexture = Keire::AssetId::Generate();
    const std::vector<Keire::RuntimeUiDrawCommand> commands{
        {.Type = Keire::RuntimeUiDrawType::Quad},
        {.Type = Keire::RuntimeUiDrawType::PushClip},
        {.Type = Keire::RuntimeUiDrawType::Image, .Asset = firstTexture},
        {.Type = Keire::RuntimeUiDrawType::Image, .Asset = firstTexture},
        {.Type = Keire::RuntimeUiDrawType::Text, .Asset = secondTexture, .Text = "font asset is not an image"},
        {.Type = Keire::RuntimeUiDrawType::Image, .Asset = secondTexture},
        {.Type = Keire::RuntimeUiDrawType::Image, .Asset = firstTexture},
    };

    const auto runs = Keire::RenderBackend::BuildRuntimeUiTextureRuns(commands);
    REQUIRE(runs.size() == 5U);
    CHECK_FALSE(runs[0].Asset);
    CHECK(runs[0].FirstCommand == 0U);
    CHECK(runs[0].CommandCount == 1U);
    CHECK(runs[1].Asset == firstTexture);
    CHECK(runs[1].FirstCommand == 2U);
    CHECK(runs[1].CommandCount == 2U);
    CHECK(runs[2].Asset == Keire::RenderBackend::RuntimeUiFallbackFontId);
    CHECK(runs[2].FirstCommand == 4U);
    CHECK(runs[3].Asset == secondTexture);
    CHECK(runs[3].FirstCommand == 5U);
    CHECK(runs[4].Asset == firstTexture);
    CHECK(runs[4].FirstCommand == 6U);
}

TEST_CASE("Runtime UI image leases are frame-owned and invalidated across device recovery generations")
{
    const auto image = Keire::AssetId::Generate();
    Keire::RenderBackend::RenderFramePacket frame;
    frame.DeviceGeneration = 4U;
    frame.FrameSlot = 1U;
    frame.RuntimeUiImageLeases.push_back({.Asset = image});

    const auto* lease = Keire::RenderBackend::FindRuntimeUiImageLease(frame, image);
    REQUIRE(lease);
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiImageLeaseOwnershipValid(*lease, frame));
    Keire::RenderBackend::QualifyRuntimeUiImageLeases(frame);
    CHECK(Keire::RenderBackend::RuntimeUiImageLeaseOwnershipValid(*lease, frame));

    frame.DeviceGeneration = 5U;
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiImageLeaseOwnershipValid(*lease, frame));
    Keire::RenderBackend::QualifyRuntimeUiImageLeases(frame);
    CHECK(Keire::RenderBackend::RuntimeUiImageLeaseOwnershipValid(*lease, frame));
    CHECK(Keire::RenderBackend::FindRuntimeUiImageLease(frame, Keire::AssetId::Generate()) == nullptr);
}

TEST_CASE("Runtime UI render-texture packets are frame-owned and requalified after device recovery")
{
    Keire::RenderBackend::RenderFramePacket frame;
    frame.DeviceGeneration = 3U;
    frame.FrameSlot = 1U;
    frame.RuntimeUiRenderTextures.push_back(
        {.Target = Keire::AssetId::Generate(), .ReferenceResolution = {512.0F, 256.0F}});
    CHECK_FALSE(
        Keire::RenderBackend::RuntimeUiRenderTextureOwnershipValid(frame.RuntimeUiRenderTextures.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiRenderTextures(frame);
    CHECK(Keire::RenderBackend::RuntimeUiRenderTextureOwnershipValid(frame.RuntimeUiRenderTextures.front(), frame));

    frame.DeviceGeneration = 4U;
    CHECK_FALSE(
        Keire::RenderBackend::RuntimeUiRenderTextureOwnershipValid(frame.RuntimeUiRenderTextures.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiRenderTextures(frame);
    CHECK(Keire::RenderBackend::RuntimeUiRenderTextureOwnershipValid(frame.RuntimeUiRenderTextures.front(), frame));
}

TEST_CASE("Runtime UI render-texture sampling uses a placeholder while its producer is streaming")
{
    using enum Keire::RenderBackend::RuntimeUiRenderTextureBindingKind;

    CHECK(Keire::RenderBackend::ResolveRuntimeUiRenderTextureBinding(false, false, false, false) == Placeholder);
    CHECK(Keire::RenderBackend::ResolveRuntimeUiRenderTextureBinding(true, false, true, true) == Placeholder);
    CHECK(Keire::RenderBackend::ResolveRuntimeUiRenderTextureBinding(true, true, false, true) == Placeholder);
    CHECK(Keire::RenderBackend::ResolveRuntimeUiRenderTextureBinding(true, true, true, false) == Placeholder);
    CHECK(Keire::RenderBackend::ResolveRuntimeUiRenderTextureBinding(true, true, true, true) == Published);
}

TEST_CASE("Runtime UI render textures order producers before same-frame image consumers and reject cycles")
{
    const auto first = Keire::AssetId::Generate();
    const auto second = Keire::AssetId::Generate();
    Keire::RenderBackend::RenderFramePacket frame;
    frame.RuntimeUiRenderTextures.push_back(
        {.Commands = {{.Type = Keire::RuntimeUiDrawType::Image, .RenderTexture = second}}, .Target = first});
    frame.RuntimeUiRenderTextures.push_back({.Target = second});
    CHECK(Keire::RenderBackend::BuildRuntimeUiRenderTextureOrder(frame) == std::vector<Keire::AssetId>{second, first});

    frame.RuntimeUiRenderTextures.back().Commands.push_back(
        {.Type = Keire::RuntimeUiDrawType::Image, .RenderTexture = first});
    const auto buildCycle = [&frame] { (void)Keire::RenderBackend::BuildRuntimeUiRenderTextureOrder(frame); };
    CHECK_THROWS_WITH_AS(buildCycle(),
                         "Runtime UI RenderTexture targets contain a same-frame sampling dependency cycle.",
                         std::logic_error);
}

TEST_CASE("Runtime UI logical render textures remain distinct from immutable image asset leases")
{
    const auto image = Keire::AssetId::Generate();
    const auto logicalTarget = Keire::AssetId::Generate();
    const Keire::RuntimeUiDrawCommand assetCommand{.Type = Keire::RuntimeUiDrawType::Image, .Asset = image};
    const Keire::RuntimeUiDrawCommand logicalCommand{.Type = Keire::RuntimeUiDrawType::Image,
                                                     .RenderTexture = logicalTarget};

    CHECK(Keire::RenderBackend::RuntimeUiTextureAsset(assetCommand) == image);
    CHECK(Keire::RenderBackend::RuntimeUiTextureAsset(logicalCommand) == logicalTarget);
}

TEST_CASE("Runtime UI renderer lowers linear and radial gradients deterministically")
{
    Keire::RuntimeUiGradient linear;
    linear.Kind = Keire::RuntimeUiGradientKind::Linear;
    linear.LinearAngleDegrees = 90.0F;
    linear.StopCount = 2;
    linear.Stops[0] = {0.0F, {1.0F, 0.0F, 0.0F, 1.0F}};
    linear.Stops[1] = {1.0F, {0.0F, 0.0F, 1.0F, 0.5F}};
    const auto left = Keire::RenderBackend::EvaluateRuntimeUiGradient(linear, {0.0F, 0.5F});
    const auto right = Keire::RenderBackend::EvaluateRuntimeUiGradient(linear, {1.0F, 0.5F});
    CHECK(left.Red == doctest::Approx(1.0F));
    CHECK(left.Blue == doctest::Approx(0.0F));
    CHECK(right.Red == doctest::Approx(0.0F));
    CHECK(right.Blue == doctest::Approx(1.0F));
    CHECK(right.Alpha == doctest::Approx(0.5F));

    auto radial = linear;
    radial.Kind = Keire::RuntimeUiGradientKind::Radial;
    radial.RadialCenter = {0.5F, 0.5F};
    radial.RadialRadius = 0.5F;
    const auto center = Keire::RenderBackend::EvaluateRuntimeUiGradient(radial, {0.5F, 0.5F});
    const auto edge = Keire::RenderBackend::EvaluateRuntimeUiGradient(radial, {1.0F, 0.5F});
    CHECK(center == linear.Stops[0].ColorValue);
    CHECK(edge == linear.Stops[1].ColorValue);
}

TEST_CASE("Runtime UI renderer applies rounded alpha clipping to solid gradient and image geometry")
{
    Keire::RuntimeUiGradient gradient;
    gradient.Kind = Keire::RuntimeUiGradientKind::Linear;
    gradient.StopCount = 2;
    gradient.Stops[0] = {0.0F, {1.0F, 0.0F, 0.0F, 1.0F}};
    gradient.Stops[1] = {1.0F, {0.0F, 0.0F, 1.0F, 1.0F}};
    const Keire::RuntimeUiDrawCommand quad{.Type = Keire::RuntimeUiDrawType::Quad,
                                           .Rect = {0.0F, 0.0F, 100.0F, 50.0F},
                                           .ClipRect = {0.0F, 0.0F, 100.0F, 50.0F},
                                           .ColorValue = {1.0F, 1.0F, 1.0F, 1.0F},
                                           .BackgroundGradient = gradient,
                                           .CornerRadius = 12.0F};
    const auto quadGeometry = Keire::RenderBackend::BuildRuntimeUiGeometry(std::span(&quad, 1U));
    CHECK(quadGeometry.Vertices.size() > 6U);
    CHECK(
        std::ranges::any_of(quadGeometry.Vertices, [](const auto& vertex) { return vertex.ColorValue.Alpha < 0.5F; }));
    CHECK(std::ranges::any_of(quadGeometry.Vertices,
                              [](const auto& vertex) { return vertex.ColorValue.Alpha >= 0.99F; }));

    const auto texture = Keire::AssetId::Generate();
    const Keire::RuntimeUiDrawCommand image{.Type = Keire::RuntimeUiDrawType::Image,
                                            .Rect = {0.0F, 0.0F, 64.0F, 64.0F},
                                            .ClipRect = {0.0F, 0.0F, 64.0F, 64.0F},
                                            .ColorValue = {1.0F, 1.0F, 1.0F, 1.0F},
                                            .Asset = texture,
                                            .CornerRadius = 8.0F};
    const auto imageGeometry = Keire::RenderBackend::BuildRuntimeUiGeometry(std::span(&image, 1U));
    REQUIRE(imageGeometry.Batches.size() == 1U);
    CHECK(imageGeometry.Batches.front().Asset == texture);
    CHECK(imageGeometry.Vertices.size() > 6U);
    CHECK(std::ranges::all_of(
        imageGeometry.Vertices, [](const auto& vertex)
        { return vertex.UV.X >= 0.0F && vertex.UV.X <= 1.0F && vertex.UV.Y >= 0.0F && vertex.UV.Y <= 1.0F; }));
}

TEST_CASE("Runtime UI fallback glyph atlas is bounded reused and frame-generation owned")
{
    const auto& firstAtlas = Keire::RenderBackend::RuntimeUiFallbackGlyphAtlas();
    const auto& secondAtlas = Keire::RenderBackend::RuntimeUiFallbackGlyphAtlas();
    REQUIRE(firstAtlas);
    CHECK(firstAtlas == secondAtlas);
    CHECK(firstAtlas->Width >= 256U);
    CHECK(firstAtlas->Height >= 128U);
    CHECK(firstAtlas->Glyphs.size() == 95U);
    CHECK(firstAtlas->Pixels.size() ==
          static_cast<std::size_t>(firstAtlas->Width) * static_cast<std::size_t>(firstAtlas->Height) * 4U);
    CHECK(Keire::RenderBackend::RuntimeUiFallbackGlyph(static_cast<std::uint8_t>('A')).Advance > 0.0F);
    CHECK(Keire::RenderBackend::RuntimeUiFontBindingId(Keire::AssetId::Generate()) ==
          Keire::RenderBackend::RuntimeUiFallbackFontId);
    CHECK(&Keire::RenderBackend::RuntimeUiFallbackGlyph(0xffU) ==
          &Keire::RenderBackend::RuntimeUiFallbackGlyph(static_cast<std::uint8_t>('?')));

    Keire::RenderBackend::RenderFramePacket frame;
    frame.DeviceGeneration = 8U;
    frame.FrameSlot = 2U;
    frame.RuntimeUiFontLeases.push_back({Keire::RenderBackend::RuntimeUiFallbackFontId, firstAtlas});
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiFontLeaseOwnershipValid(frame.RuntimeUiFontLeases.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiFontLeases(frame);
    CHECK(Keire::RenderBackend::RuntimeUiFontLeaseOwnershipValid(frame.RuntimeUiFontLeases.front(), frame));
    frame.DeviceGeneration = 9U;
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiFontLeaseOwnershipValid(frame.RuntimeUiFontLeases.front(), frame));
    Keire::RenderBackend::QualifyRuntimeUiFontLeases(frame);
    CHECK(Keire::RenderBackend::RuntimeUiFontLeaseOwnershipValid(frame.RuntimeUiFontLeases.front(), frame));
}

TEST_CASE("Runtime UI text batches use deterministic font identities and nested clips")
{
    const auto requestedFont = Keire::AssetId::Generate();
    const std::vector<Keire::RuntimeUiDrawCommand> commands{
        {.Type = Keire::RuntimeUiDrawType::PushClip,
         .Rect = {10.0F, 10.0F, 20.0F, 20.0F},
         .ClipRect = {10.0F, 10.0F, 20.0F, 20.0F}},
        {.Type = Keire::RuntimeUiDrawType::Text,
         .Rect = {0.0F, 0.0F, 64.0F, 32.0F},
         .ClipRect = {10.0F, 10.0F, 20.0F, 20.0F},
         .Asset = requestedFont,
         .Text = "Atlas",
         .FontSize = 12.0F},
        {.Type = Keire::RuntimeUiDrawType::PopClip},
    };
    const auto runs = Keire::RenderBackend::BuildRuntimeUiTextureRuns(commands);
    REQUIRE(runs.size() == 1U);
    CHECK(requestedFont != Keire::RenderBackend::RuntimeUiFallbackFontId);
    CHECK(runs.front().Asset == Keire::RenderBackend::RuntimeUiFallbackFontId);
    const auto geometry = Keire::RenderBackend::BuildRuntimeUiGeometry(commands);
    CHECK_FALSE(geometry.Vertices.empty());
    REQUIRE(geometry.Batches.size() == 1U);
    CHECK(geometry.Batches.front().ClipRect == commands[1].ClipRect);
    CHECK(std::ranges::all_of(geometry.Vertices,
                              [](const auto& vertex)
                              {
                                  return vertex.Position.X >= 10.0F && vertex.Position.X <= 30.0F &&
                                         vertex.Position.Y >= 10.0F && vertex.Position.Y <= 30.0F;
                              }));

    Keire::RenderBackend::RuntimeUiFontAtlasCacheEntry cache;
    cache.Texture = reinterpret_cast<SDL_GPUTexture*>(std::uintptr_t{1});
    cache.DeviceGeneration = 4U;
    CHECK(Keire::RenderBackend::RuntimeUiFontAtlasCacheValid(cache, 4U));
    CHECK_FALSE(Keire::RenderBackend::RuntimeUiFontAtlasCacheValid(cache, 5U));
}

TEST_CASE("Runtime UI mixed-font geometry preserves contiguous face and atlas-page ordering")
{
    const auto firstPage = Keire::AssetId::Generate();
    const auto secondPage = Keire::AssetId::Generate();
    Keire::RuntimeUiDrawCommand text;
    text.Type = Keire::RuntimeUiDrawType::Text;
    text.Rect = {0.0F, 0.0F, 100.0F, 32.0F};
    text.ClipRect = text.Rect;
    text.Text = "ABA";
    text.FontSize = 16.0F;
    text.PreparedFontBinding = firstPage;
    text.PreparedTextWidth = 30.0F;
    text.PreparedTextHeight = 16.0F;
    text.PreparedTextLines = {{0U, 3U, 30.0F}};
    text.PreparedTextGlyphs = {
        {.FontBinding = firstPage,
         .UvMinimum = {0.0F, 0.0F},
         .UvMaximum = {0.1F, 0.1F},
         .Position = {0.0F, 0.0F},
         .Size = {8.0F, 12.0F}},
        {.FontBinding = secondPage,
         .UvMinimum = {0.1F, 0.0F},
         .UvMaximum = {0.2F, 0.1F},
         .Position = {10.0F, 0.0F},
         .Size = {8.0F, 12.0F}},
        {.FontBinding = firstPage,
         .UvMinimum = {0.2F, 0.0F},
         .UvMaximum = {0.3F, 0.1F},
         .Position = {20.0F, 0.0F},
         .Size = {8.0F, 12.0F}},
    };

    const auto geometry = Keire::RenderBackend::BuildRuntimeUiGeometry(std::span(&text, 1U));
    REQUIRE(geometry.Batches.size() == 3U);
    CHECK(geometry.Batches[0].Asset == firstPage);
    CHECK(geometry.Batches[1].Asset == secondPage);
    CHECK(geometry.Batches[2].Asset == firstPage);
    CHECK(geometry.Batches[0].VertexCount == 6U);
    CHECK(geometry.Batches[1].VertexCount == 6U);
    CHECK(geometry.Batches[2].VertexCount == 6U);
}

TEST_CASE("Runtime UI nested clips split GPU scissor batches without changing texture order")
{
    const auto texture = Keire::AssetId::Generate();
    const std::vector<Keire::RuntimeUiDrawCommand> commands{{.Type = Keire::RuntimeUiDrawType::Image,
                                                             .Rect = {0.0F, 0.0F, 64.0F, 64.0F},
                                                             .ClipRect = {0.0F, 0.0F, 64.0F, 64.0F},
                                                             .ColorValue = {1.0F, 1.0F, 1.0F, 1.0F},
                                                             .Asset = texture},
                                                            {.Type = Keire::RuntimeUiDrawType::PushClip,
                                                             .Rect = {8.0F, 8.0F, 16.0F, 16.0F},
                                                             .ClipRect = {8.0F, 8.0F, 16.0F, 16.0F}},
                                                            {.Type = Keire::RuntimeUiDrawType::Image,
                                                             .Rect = {0.0F, 0.0F, 64.0F, 64.0F},
                                                             .ClipRect = {8.0F, 8.0F, 16.0F, 16.0F},
                                                             .ColorValue = {1.0F, 1.0F, 1.0F, 1.0F},
                                                             .Asset = texture},
                                                            {.Type = Keire::RuntimeUiDrawType::PopClip,
                                                             .Rect = {8.0F, 8.0F, 16.0F, 16.0F},
                                                             .ClipRect = {8.0F, 8.0F, 16.0F, 16.0F}},
                                                            {.Type = Keire::RuntimeUiDrawType::Image,
                                                             .Rect = {0.0F, 0.0F, 64.0F, 64.0F},
                                                             .ClipRect = {0.0F, 0.0F, 64.0F, 64.0F},
                                                             .ColorValue = {1.0F, 1.0F, 1.0F, 1.0F},
                                                             .Asset = texture}};

    const auto runs = Keire::RenderBackend::BuildRuntimeUiTextureRuns(commands);
    REQUIRE(runs.size() == 3U);
    CHECK(runs[0].ClipRect == commands[0].ClipRect);
    CHECK(runs[1].ClipRect == commands[2].ClipRect);
    CHECK(runs[2].ClipRect == commands[4].ClipRect);
    const auto geometry = Keire::RenderBackend::BuildRuntimeUiGeometry(commands);
    REQUIRE(geometry.Batches.size() == 3U);
    CHECK(geometry.Batches[0].ClipRect == commands[0].ClipRect);
    CHECK(geometry.Batches[1].ClipRect == commands[2].ClipRect);
    CHECK(geometry.Batches[2].ClipRect == commands[4].ClipRect);
}

TEST_CASE("Runtime UI renderer statistics are bounded value snapshots")
{
    Keire::RenderBackend::RuntimeUiGeometry geometry;
    geometry.Vertices.resize(12U);
    geometry.Batches.resize(2U);
    Keire::RuntimeUiRendererStatistics accumulated;
    Keire::RenderBackend::AccumulateRuntimeUiGeometryStatistics(accumulated, geometry);
    Keire::RenderBackend::AccumulateRuntimeUiGeometryStatistics(accumulated, geometry);
    CHECK(accumulated.RenderedVertices == 24U);
    CHECK(accumulated.DrawBatches == 4U);
    CHECK(accumulated.GlyphAtlasEntries == 0U);
    CHECK(accumulated.ImageAtlasEntries == 0U);

    accumulated.GlyphAtlasEntries = Keire::RenderBackend::RuntimeUiFallbackGlyphAtlas()->Glyphs.size();
    accumulated.GlyphAtlasBytes = Keire::RenderBackend::RuntimeUiFallbackGlyphAtlas()->Pixels.size();
    accumulated.RepaintCpuMilliseconds = 0.5F;
    Keire::RenderStatistics renderer;
    renderer.RuntimeUiRenderer = accumulated;
    const auto published = renderer;
    renderer.RuntimeUiRenderer = {};
    CHECK(published.RuntimeUiRenderer.RenderedVertices == 24U);
    CHECK(published.RuntimeUiRenderer.DrawBatches == 4U);
    CHECK(published.RuntimeUiRenderer.GlyphAtlasEntries == 95U);
    CHECK(published.RuntimeUiRenderer.GlyphAtlasBytes ==
          Keire::RenderBackend::RuntimeUiFallbackGlyphAtlas()->Pixels.size());
    CHECK(published.RuntimeUiRenderer.ImageAtlasEntries == 0U);
    CHECK(published.RuntimeUiRenderer.ImageAtlasBytes == 0U);
    CHECK(published.RuntimeUiRenderer.RepaintCpuMilliseconds == doctest::Approx(0.5F));
    CHECK(published.RuntimeUiRenderer.UploadBufferPoolSize == 0U);
    CHECK(published.RuntimeUiRenderer.UploadBufferReallocations == 0U);
}

TEST_CASE("World runtime UI submissions are root-filtered and accepted into bounded headless frame packets")
{
    UseRuntimeUiTargetDummyVideoDriver();
    const auto probe = RunSubmissionCase(SubmissionCase::WorldSurface);
    CHECK(probe.PendingCommands == probe.ExpectedCommands);
    CHECK(probe.Statistics.AcceptedFrames == 1U);
    CHECK(probe.Statistics.RetiredFrames == 1U);

    const auto missingDepth = RunSubmissionCase(SubmissionCase::WorldSurfaceWithoutDepth);
    CHECK(missingDepth.Diagnostic == "Depth-tested world-surface runtime UI requires a depth-enabled surface.");
}
