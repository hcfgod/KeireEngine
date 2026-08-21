#pragma once

#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

#include <memory>

namespace Keire
{
    class Application;
}

namespace Keire::Detail
{
    class ManagedRuntimeApplicationServices : public ManagedRuntimeSceneServices
    {
      public:
        explicit ManagedRuntimeApplicationServices(bool editor);
        ~ManagedRuntimeApplicationServices() override;

        [[nodiscard]] ManagedApplicationInfo ManagedApplication() const override;
        void RequestManagedExit(int exitCode) noexcept override;
        [[nodiscard]] double ManagedTimeScale() const noexcept override;
        [[nodiscard]] bool SetManagedTimeScale(double scale) noexcept override;
        [[nodiscard]] bool ManagedTimePaused() const noexcept override;
        [[nodiscard]] bool SetManagedTimePaused(bool paused) noexcept override;
        [[nodiscard]] ManagedScreenState ManagedScreen() const noexcept override;
        [[nodiscard]] bool SetManagedScreen(std::uint32_t width, std::uint32_t height,
                                            ManagedScreenMode mode) noexcept override;
        [[nodiscard]] std::uint64_t BeginManagedRuntimeAssetLoad(std::uint64_t generation, AssetId id, AssetTypeId type,
                                                                 AssetPriority priority) noexcept override;
        [[nodiscard]] std::optional<ManagedRuntimeAssetStatus>
        ManagedRuntimeAsset(std::uint64_t handle) const noexcept override;
        [[nodiscard]] bool ReleaseManagedRuntimeAsset(std::uint64_t handle) noexcept override;
        void ReleaseManagedRuntimeAssets(std::uint64_t generation) noexcept override;

      protected:
        void BindManagedApplication(Application& application) noexcept;
        void UnbindManagedApplication() noexcept;

      private:
        class RuntimeAssets;

        Application* m_Application = nullptr;
        std::unique_ptr<RuntimeAssets> m_RuntimeAssets;
        bool m_Editor = false;
    };
} // namespace Keire::Detail
