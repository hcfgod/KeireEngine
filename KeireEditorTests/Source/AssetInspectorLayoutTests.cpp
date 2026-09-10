#include "Keire/Core.h"
#include "KeireClient/Editor/AssetInspectorFileActions.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <string>

namespace
{
    class InspectorLayoutLayer final : public Keire::Layer
    {
      public:
        explicit InspectorLayoutLayer(bool& drawn) : Layer("InspectorLayout"), m_Drawn(drawn) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            for (const float width : std::array{150.0F, 220.0F, 360.0F})
            {
                const auto label = "Inspector " + std::to_string(width);
                ui.SetNextWindowSize({width, 350.0F}, false);
                if (auto window = ui.BeginWindow(label); window)
                {
                    std::string name = "Long scene Caf\xc3\xa9.keirescene";
                    const auto bounds = ui.ContentRect();
                    CHECK(KeireEditor::DrawAssetInspectorFileActions(ui, name) ==
                          KeireEditor::AssetInspectorFileAction::None);
                    const auto trash = ui.LastItemRect();
                    CHECK(trash.Minimum.X >= bounds.Minimum.X);
                    CHECK(trash.Maximum.X <= bounds.Maximum.X);
                    CHECK(trash.Maximum.X - trash.Minimum.X >= ui.MeasureText("Move to Trash").Width);
                    CHECK(name == "Long scene Caf\xc3\xa9.keirescene");
                    m_Drawn = true;
                }
            }
            Owner().RequestExit();
        }

      private:
        bool& m_Drawn;
    };

    class InspectorLayoutApplication final : public Keire::Application
    {
      public:
        explicit InspectorLayoutApplication(bool& drawn) : Application(Specification())
        {
            (void)PushLayer(std::make_unique<InspectorLayoutLayer>(drawn));
        }

      private:
        static Keire::ApplicationSpecification Specification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Title = "Inspector layout regression";
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            return specification;
        }
    };
} // namespace

TEST_CASE("Asset Inspector file actions remain inside narrow panels")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool drawn = false;
    {
        InspectorLayoutApplication application(drawn);
        CHECK(application.Run() == 0);
    }
    CHECK(drawn);
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    }
    SDL_Quit();
}
