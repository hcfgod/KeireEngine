#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Keire
{
    class AssetSystem;
    class Scene;
    class Window;
    class WindowSystem;

    enum class RenderMode : std::uint8_t
    {
        Automatic,
        Disabled,
        Headless,
        Rendered
    };

    enum class RenderPresentMode : std::uint8_t
    {
        VSync,
        Mailbox,
        Immediate
    };

    enum class RenderSampleCount : std::uint8_t
    {
        One = 1,
        Two = 2,
        Four = 4,
        Eight = 8
    };

    struct RenderSpecification
    {
        RenderMode Mode = RenderMode::Automatic;
        RenderPresentMode PresentMode = RenderPresentMode::VSync;
        Color SwapchainClearColor{0.08F, 0.09F, 0.11F, 1.0F};
        RenderSampleCount PreferredSampleCount = RenderSampleCount::Four;
        std::uint32_t MaximumFramesInFlight = 3;
        bool EnableGpuValidation = false;
    };

    struct RenderSurfaceSpecification
    {
        std::string Name = "Render Surface";
        std::uint32_t Width = 1;
        std::uint32_t Height = 1;
        Color ClearColor{0.08F, 0.09F, 0.11F, 1.0F};
        RenderSampleCount SampleCount = RenderSampleCount::Four;
        bool Depth = true;
    };

    class KEIRE_API RenderSurface final : public RefCounted
    {
      public:
        class Impl;
        ~RenderSurface() override;

        [[nodiscard]] std::string Name() const;
        [[nodiscard]] std::uint32_t Width() const noexcept;
        [[nodiscard]] std::uint32_t Height() const noexcept;
        [[nodiscard]] RenderSampleCount SampleCount() const noexcept;
        [[nodiscard]] Color ClearColor() const noexcept;
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        [[nodiscard]] bool Available() const noexcept;

        void RequestSize(std::uint32_t width, std::uint32_t height);
        void SetClearColor(Color color);

      private:
        friend class RenderSystem;
        friend class RenderSystemInternalAccess;
        friend class UiFrame;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit RenderSurface(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct RenderCamera
    {
        Matrix4 View;
        Matrix4 Projection;
        Color ClearColor{0.08F, 0.09F, 0.11F, 1.0F};
    };

    struct RenderEnvironmentSettings
    {
        std::uint32_t SchemaVersion = 1;
        Color AmbientColor{0.20F, 0.22F, 0.26F, 1.0F};
        float AmbientIntensity = 0.75F;
        float Exposure = 1.0F;

        auto operator<=>(const RenderEnvironmentSettings&) const noexcept = default;
    };

    [[nodiscard]] KEIRE_API RenderEnvironmentSettings
    LoadRenderEnvironmentSettings(const std::filesystem::path& projectRoot);
    KEIRE_API void SaveRenderEnvironmentSettings(const std::filesystem::path& projectRoot,
                                                 const RenderEnvironmentSettings& settings);

    class KEIRE_API RenderView final : public RefCounted
    {
      public:
        class Impl;
        ~RenderView() override;

        [[nodiscard]] Ref<RenderSurface> Surface() const noexcept;
        [[nodiscard]] RenderCamera Camera() const noexcept;
        void SetCamera(RenderCamera camera);

      private:
        friend class RenderSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit RenderView(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct SceneRenderRequest
    {
        Ref<Scene> Scene;
        Ref<RenderView> View;
        bool DrawGrid = false;
        RenderEnvironmentSettings Environment;
    };

    struct RenderStatistics
    {
        std::uint64_t Frame = 0;
        std::uint32_t Passes = 0;
        std::uint32_t Surfaces = 0;
        std::uint32_t DrawCalls = 0;
        std::uint32_t Triangles = 0;
    };

    class KEIRE_API RenderSystem final : public RefCounted
    {
      public:
        ~RenderSystem() override;

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;

        [[nodiscard]] Ref<RenderSurface> CreateSurface(RenderSurfaceSpecification specification = {});
        [[nodiscard]] Ref<RenderView> CreateView(RenderSurfaceSpecification specification = {});
        void Submit(SceneRenderRequest request);

        [[nodiscard]] RenderMode Mode() const noexcept;
        [[nodiscard]] RenderStatistics Statistics() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        friend class Application;
        friend class RenderSystemInternalAccess;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        RenderSystem(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                     Ref<AssetSystem> assets);

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
