#pragma once

#include "KeireInternal/Scripting/ManagedRuntimeRenderingServices.h"

namespace Keire
{
    class Application;
}

namespace Keire::Detail
{
    class ManagedRuntimeApplicationServices : public ManagedRuntimeSceneServices
    {
      public:
        explicit ManagedRuntimeApplicationServices(bool editor) noexcept : m_Editor(editor) {}
        ~ManagedRuntimeApplicationServices() override = default;

        [[nodiscard]] ManagedApplicationInfo ManagedApplication() const override;
        void RequestManagedExit(int exitCode) noexcept override;
        [[nodiscard]] double ManagedTimeScale() const noexcept override;
        [[nodiscard]] bool SetManagedTimeScale(double scale) noexcept override;
        [[nodiscard]] bool ManagedTimePaused() const noexcept override;
        [[nodiscard]] bool SetManagedTimePaused(bool paused) noexcept override;
        [[nodiscard]] ManagedScreenState ManagedScreen() const noexcept override;
        [[nodiscard]] bool SetManagedScreen(std::uint32_t width, std::uint32_t height,
                                            ManagedScreenMode mode) noexcept override;

      protected:
        void BindManagedApplication(Application& application) noexcept;
        void UnbindManagedApplication() noexcept;

      private:
        Application* m_Application = nullptr;
        bool m_Editor = false;
    };
} // namespace Keire::Detail
