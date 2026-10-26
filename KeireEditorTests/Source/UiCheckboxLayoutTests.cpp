#include "Keire/Core.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace
{
    class CheckboxLayoutLayer final : public Keire::Layer
    {
      public:
        explicit CheckboxLayoutLayer(bool& checked) : Layer("Responsive checkbox layout"), m_Checked(checked) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            constexpr std::array widths{240.0F, 360.0F, 544.0F, 900.0F, 320.0F, 544.0F, 900.0F};
            const auto variant = m_Frame == 0 ? 0U : (m_Frame - 1U) % widths.size();
            const bool large = variant >= 4U;
            ui.SetNextWindowPosition({10.0F, 10.0F}, false);
            ui.SetNextWindowSize({widths[variant], 500.0F}, false);
            auto window = ui.BeginWindow("Responsive checkbox fixture");
            if (!window)
            {
                Owner().RequestExit();
                return;
            }
            ImGui::PushFont(nullptr, large ? 24.0F : 16.0F);
            {
                const auto padding =
                    ui.PushStyleVariable(Keire::UiStyleVariable::FramePadding,
                                         large ? Keire::UiSize{12.0F, 10.0F} : Keire::UiSize{6.0F, 4.0F});
                const auto spacing =
                    ui.PushStyleVariable(Keire::UiStyleVariable::ItemSpacing,
                                         large ? Keire::UiSize{20.0F, 10.0F} : Keire::UiSize{8.0F, 4.0F});
                DrawToolbar(ui, variant);
                CheckBoundary(ui, true);
                CheckBoundary(ui, false);
                if (auto child = ui.BeginChild("Nested content", {0.0F, 130.0F}, true); child)
                {
                    CheckBoundary(ui, true);
                    CheckBoundary(ui, false);
                }
                if (auto table = ui.BeginTable("Columns", 2U, {.PersistSettings = false}); table)
                {
                    ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    CheckBoundary(ui, true);
                    CheckBoundary(ui, false);
                    (void)ui.TableNextColumn();
                    CheckBoundary(ui, true);
                    CheckBoundary(ui, false);
                }
            }
            ImGui::PopFont();
            if (++m_Frame == widths.size() * 2U + 1U)
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        void DrawToolbar(Keire::UiFrame& ui, const std::size_t variant)
        {
            const float selectorWidth = ui.MeasureText("16:10").Width + ui.MeasureText("MMMM").Width;
            ui.SetNextItemWidth(selectorWidth);
            if (auto combo = ui.BeginCombo("Preview", "16:9"); combo)
            {
                (void)ui.Selectable("Free");
            }
            const auto comboRect = ui.LastItemRect();
            auto previous = comboRect;
            constexpr std::array labels{std::string_view("Maximize On Play"), std::string_view("FPS"),
                                        std::string_view("GPU Bounds")};
            bool wrapped = false;
            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                if (m_Frame == 2U && index == 1U)
                {
                    auto* context = ImGui::GetCurrentContext();
                    context->NavActivateId = ImGui::GetID("FPS");
                    context->NavActivateDownId = context->NavActivateId;
                    context->NavActivatePressedId = context->NavActivateId;
                }
                const bool changed = ui.Checkbox(labels[index], m_Values[index], {.ContinueRow = true});
                const auto rectangle = ui.LastItemRect();
                if (m_Frame > 0)
                {
                    INFO("Variant: " << variant << ", control: " << labels[index]);
                    const auto* current = ImGui::GetCurrentWindow();
                    CHECK(rectangle.Minimum.X >= current->WorkRect.Min.x);
                    CHECK(rectangle.Maximum.X <= current->WorkRect.Max.x + 0.01F);
                    const bool sameRow = rectangle.Minimum.Y == previous.Minimum.Y;
                    if (sameRow)
                    {
                        CHECK(rectangle.Minimum.X >= previous.Maximum.X + ImGui::GetStyle().ItemSpacing.x);
                    }
                    else
                    {
                        CHECK(rectangle.Minimum.Y >= previous.Maximum.Y);
                        wrapped = true;
                    }
                    CHECK(changed == (m_Frame == 2U && index == 1U));
                    CHECK(m_Values[index] == (m_Frame >= 2U && index == 1U));
                }
                previous = rectangle;
            }
            if (m_Frame > 0)
            {
                if (variant == 0U)
                {
                    CHECK(wrapped);
                }
                if (variant == 3U || variant == 6U)
                {
                    CHECK_FALSE(wrapped);
                }
                // Both the existing overload and an explicit default preserve normal next-line placement.
                bool legacy = false;
                CHECK_FALSE(ui.Checkbox("Legacy layout", legacy));
                CHECK(ui.LastItemRect().Minimum.Y >= previous.Maximum.Y);
                const auto legacyRect = ui.LastItemRect();
                CHECK_FALSE(ui.Checkbox("Explicit normal layout", legacy, {}));
                CHECK(ui.LastItemRect().Minimum.Y >= legacyRect.Maximum.Y);
            }
        }

        void CheckBoundary(Keire::UiFrame& ui, const bool exactFit)
        {
            const auto id = ui.PushId(exactFit ? "Exact" : "Overflow");
            // A hidden label contributes only its square; the long identifier must not force a wrap.
            const float controlWidth = ImGui::GetFrameHeight();
            const float firstWidth =
                ui.ContentAvailable().Width - ImGui::GetStyle().ItemSpacing.x - controlWidth + (exactFit ? 0.0F : 1.0F);
            (void)ui.Button("First", {firstWidth, controlWidth});
            const auto first = ui.LastItemRect();
            if (!exactFit)
            {
                // Responsive placement also repairs a preceding unconditional SameLine request.
                ui.SameLine();
            }
            bool value = false;
            CHECK_FALSE(ui.Checkbox("##A long hidden checkbox identity", value, {.ContinueRow = true}));
            if (m_Frame > 0)
            {
                const auto second = ui.LastItemRect();
                CHECK((second.Minimum.Y == first.Minimum.Y) == exactFit);
                CHECK(second.Maximum.X <= ImGui::GetCurrentWindow()->WorkRect.Max.x + 0.01F);
            }
        }

        bool& m_Checked;
        std::size_t m_Frame = 0;
        std::array<bool, 3> m_Values{};
    };
} // namespace

TEST_CASE("Responsive checkboxes fit themed Game toolbar rows without losing interaction or legacy layout")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.MainWindow.Width = 1100;
    specification.MainWindow.Height = 700;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<CheckboxLayoutLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
