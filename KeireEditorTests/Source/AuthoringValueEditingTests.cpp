#include "Keire/Core.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClientInternal/Editor/AuthoringValueEditing.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

TEST_CASE("Authoring lifetime key edits survive VFX document undo redo save and reopen")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Lifetime authoring"});
    std::vector<std::byte> persisted;
    KeireEditor::VfxEffectDocument document({.Persist = [&](Keire::AssetId, const std::span<const std::byte> bytes)
                                             { persisted.assign(bytes.begin(), bytes.end()); }});
    const auto original = Keire::VfxEffectAsset::DefaultDefinition();
    const auto asset = Keire::AssetId::Generate();
    document.Open(asset, Keire::VfxEffectAsset::Encode(original), 1, undo);
    for (const bool gradient : {false, true})
    {
        const auto& modules = document.Definition().Modules;
        const auto found = std::ranges::find_if(
            modules,
            [gradient](const Keire::VfxModuleDefinition& module)
            {
                return gradient ? std::holds_alternative<Keire::VfxColorOverLifetimeModule>(module.Payload)
                                : std::holds_alternative<Keire::VfxSizeOverLifetimeModule>(module.Payload);
            });
        REQUIRE(found != modules.end());
        REQUIRE(document.EditModule(found->Id,
                                    [gradient](Keire::VfxModuleDefinition& module)
                                    {
                                        if (gradient)
                                        {
                                            auto& value =
                                                std::get<Keire::VfxColorOverLifetimeModule>(module.Payload).Color;
                                            REQUIRE(KeireEditor::Detail::InsertAuthoringGradientKey(value));
                                            value.SetInterpolation(Keire::GradientInterpolation::Constant);
                                        }
                                        else
                                        {
                                            auto& value =
                                                std::get<Keire::VfxSizeOverLifetimeModule>(module.Payload).Size;
                                            REQUIRE(KeireEditor::Detail::InsertAuthoringCurveKey(value));
                                            std::vector<Keire::CurveKey> keys(value.Keys().begin(), value.Keys().end());
                                            keys.front().Interpolation = Keire::CurveInterpolation::Cubic;
                                            keys.front().OutTangent = 0.5F;
                                            value.SetKeys(std::move(keys));
                                        }
                                    }));
    }
    const auto edited = document.Definition();
    REQUIRE(document.Dirty());
    REQUIRE(document.Undo());
    REQUIRE(document.Undo());
    CHECK(document.Definition() == original);
    REQUIRE(document.Redo());
    REQUIRE(document.Redo());
    CHECK(document.Definition() == edited);
    document.Save();
    CHECK_FALSE(document.Dirty());
    REQUIRE_FALSE(persisted.empty());
    CHECK(Keire::VfxEffectAsset::Decode(persisted)->Definition() == edited);
    document.Open(asset, persisted, 2, undo);
    CHECK(document.Definition() == edited);
    CHECK_FALSE(document.Dirty());
}

namespace
{
    class AuthoringKeyLayer final : public Keire::Layer
    {
      public:
        AuthoringKeyLayer(const bool gradient, const bool interpolation, bool& checked)
            : Layer("Authoring key controls"), m_Gradient(gradient), m_Interpolation(interpolation), m_Checked(checked)
        {
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({640, 600}, false);
            auto window = ui.BeginWindow("Authoring keys fixture");
            if (m_Interpolation)
            {
                ActivateInterpolation(ui);
            }
            else if (m_Frame >= 1 && m_Frame <= 5)
            {
                auto field = ui.PushId("Value##primary");
                if (m_Frame == 1 || m_Frame == 5)
                {
                    Activate(ImGui::GetCurrentWindow()->GetID("Add key"));
                }
                else
                {
                    auto row = ui.PushId(m_Frame == 2 ? "1" : "0");
                    Activate(ImGui::GetCurrentWindow()->GetID("Remove key"));
                }
            }

            const bool changed = Draw(ui, "Value##primary", m_Curve, m_Color);
            auto untouchedCurve = Keire::Curve1D::Linear(5, 7);
            const auto originalCurve = untouchedCurve;
            auto untouchedColor = Keire::ColorGradient::Constant({0, 1, 0, 1});
            const auto originalColor = untouchedColor;
            CHECK_FALSE(Draw(ui, "Value##secondary", untouchedCurve, untouchedColor));
            CHECK(untouchedCurve == originalCurve);
            CHECK(untouchedColor == originalColor);

            if (m_Interpolation)
            {
                CHECK(changed == (m_Frame == 2));
                if (m_Frame >= 2)
                {
                    CHECK(m_Curve.Keys().front().Interpolation ==
                          (m_Gradient ? Keire::CurveInterpolation::Linear : Keire::CurveInterpolation::Cubic));
                    CHECK(m_Color.Interpolation() ==
                          (m_Gradient ? Keire::GradientInterpolation::Constant : Keire::GradientInterpolation::Linear));
                }
            }
            else
            {
                CHECK(changed == (m_Frame >= 1 && m_Frame <= 5));
                const std::size_t expected[] = {2, 3, 2, 1, 0, 1, 1};
                CHECK((m_Gradient ? m_Color.Keys().size() : m_Curve.Keys().size()) == expected[m_Frame]);
            }
            if (++m_Frame == (m_Interpolation ? 4 : 7))
            {
                m_Checked = true;
                Owner().RequestExit();
            }
        }

      private:
        static void Activate(const ImGuiID id)
        {
            auto* context = ImGui::GetCurrentContext();
            context->NavActivateId = id;
            context->NavActivateDownId = id;
            context->NavActivatePressedId = id;
        }

        void ActivateInterpolation(Keire::UiFrame& ui)
        {
            if (m_Frame == 1)
            {
                auto field = ui.PushId("Value##primary");
                if (m_Gradient)
                {
                    Activate(ImGui::GetCurrentWindow()->GetID("Interpolation"));
                }
                else
                {
                    auto row = ui.PushId("0");
                    Activate(ImGui::GetCurrentWindow()->GetID("Interpolation"));
                }
            }
            else if (m_Frame == 2)
            {
                auto* context = ImGui::GetCurrentContext();
                REQUIRE(context->OpenPopupStack.Size == 1);
                auto* popup = context->OpenPopupStack.back().Window;
                REQUIRE(popup != nullptr);
                Activate(popup->GetID(m_Gradient ? "Constant" : "Cubic"));
            }
        }

        bool Draw(Keire::UiFrame& ui, const std::string_view label, Keire::Curve1D& curve, Keire::ColorGradient& color)
        {
            return m_Gradient ? KeireEditor::AuthoringValueEditors::Gradient(ui, label, color)
                              : KeireEditor::AuthoringValueEditors::Curve(ui, label, curve);
        }

        bool m_Gradient;
        bool m_Interpolation;
        bool& m_Checked;
        int m_Frame = 0;
        Keire::Curve1D m_Curve = Keire::Curve1D::Linear(2, 6);
        Keire::ColorGradient m_Color{{{0, {1, 0, 0, 1}}, {1, {0, 0, 1, 0}}}};
    };
} // namespace

TEST_CASE("Authoring key controls add remove and select interpolation without changing neighboring fields")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool gradient : {false, true})
    {
        for (const bool interpolation : {false, true})
        {
            INFO("Gradient: " << gradient << ", interpolation: " << interpolation);
            bool checked = false;
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            Keire::Application application(specification);
            (void)application.PushLayer(std::make_unique<AuthoringKeyLayer>(gradient, interpolation, checked));
            CHECK(application.Run() == 0);
            CHECK(checked);
        }
    }
}
