#pragma once

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace KeireRuntime
{
    class RuntimeRenderBenchmark final
    {
      public:
        static constexpr std::uint32_t WarmupFrames = 300;
        static constexpr std::uint32_t MeasuredFrames = 2000;

        [[nodiscard]] static constexpr bool SupportsConfiguration(const std::string_view configuration) noexcept
        {
            return configuration == "Release" || configuration == "DevDist" || configuration == "Dist";
        }

        RuntimeRenderBenchmark(std::filesystem::path output, Keire::RenderPresentMode presentMode);
        ~RuntimeRenderBenchmark();

        RuntimeRenderBenchmark(const RuntimeRenderBenchmark&) = delete;
        RuntimeRenderBenchmark& operator=(const RuntimeRenderBenchmark&) = delete;

        [[nodiscard]] bool Enabled() const noexcept;
        void Update(Keire::Application& application, const Keire::Ref<Keire::RenderSystem>& renderer);

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireRuntime
