#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <memory>
#include <span>

namespace KeireEditor
{
    class EditorSmokePlayValidation final
    {
      public:
        explicit EditorSmokePlayValidation(std::filesystem::path output
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                                           ,
                                           bool validateDeviceLoss
#endif
        );
        ~EditorSmokePlayValidation();

        EditorSmokePlayValidation(const EditorSmokePlayValidation&) = delete;
        EditorSmokePlayValidation& operator=(const EditorSmokePlayValidation&) = delete;

        void Update(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                    const Keire::PlayerBuildScenes& buildScenes);
        void ObserveGameView(Keire::Application& application, const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                             const Keire::Ref<Keire::RenderSurface>& surface, Keire::UiItemRect viewport,
                             std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations);

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor
