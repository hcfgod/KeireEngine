#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Ref.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Keire
{
    class Application;
    class RenderSurface;
    class RenderSystem;
    class SceneRuntimeWorld;
    class ApplicationCommandLineArguments;
} // namespace Keire

namespace KeireRuntime
{
    void ParseRuntimeAdditiveValidationOption(const Keire::ApplicationCommandLineArguments& arguments,
                                              std::size_t& index, std::filesystem::path& output);
    [[nodiscard]] std::vector<Keire::AssetId> ParseRuntimeValidationScenes(const nlohmann::json& manifest,
                                                                           Keire::AssetId startupScene);
    [[nodiscard]] std::vector<Keire::AssetId>
    SelectRuntimeValidationScenes(const std::filesystem::path& output, std::span<const Keire::AssetId> buildScenes);

    class RuntimeAdditiveValidation final
    {
      public:
        RuntimeAdditiveValidation(std::filesystem::path output, std::vector<Keire::AssetId> buildScenes
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                  ,
                                  bool validateDeviceLoss
#endif
        );
        ~RuntimeAdditiveValidation();

        RuntimeAdditiveValidation(const RuntimeAdditiveValidation&) = delete;
        RuntimeAdditiveValidation& operator=(const RuntimeAdditiveValidation&) = delete;

        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool Complete() const noexcept;
        void Update(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world, float width,
                    float height);
        void ObserveSubmission(Keire::Application& application, const Keire::Ref<Keire::RenderSystem>& renderer,
                               const Keire::Ref<Keire::RenderSurface>& surface);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        void FinalizeDeviceLossShutdown(Keire::RenderSystem& renderer) noexcept;
#endif

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireRuntime
