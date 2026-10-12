#include "KeireClient/Editor/AssetPicker.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <memory>
#include <string>

namespace
{
    class PickerInteractionLayer final : public Keire::Layer
    {
      public:
        PickerInteractionLayer(const bool initiallyAssigned, const bool selectNone, bool& checked)
            : Layer("Asset picker interaction"), m_SelectNone(selectNone), m_Checked(checked)
        {
            auto& source = m_Records.front();
            source.Id = Keire::AssetId::Generate();
            source.Type = Keire::MaterialGraphAsset::StaticType();
            source.RelativePath = "Materials/Test.keirematerial";
            source.SubAssets = {m_Compiled};
            m_Value = initiallyAssigned ? m_Compiled : Keire::AssetId{};
            m_Original = m_Value;
            m_Options.Label = "Material";
            m_Options.ExpectedType = Keire::MaterialAsset::StaticType();
            m_Options.ResolveType = [this](const Keire::AssetId id) -> std::optional<Keire::AssetTypeId>
            { return id == m_Compiled ? std::optional{Keire::MaterialAsset::StaticType()} : std::nullopt; };
        }

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({640, 480}, false);
            auto window = ui.BeginWindow("Picker fixture");
            auto* context = ImGui::GetCurrentContext();
            if (m_Frame == 1)
            {
                auto id = ui.PushId(m_Options.Label);
                Activate(ImGui::GetCurrentWindow()->GetID("##Asset"));
            }
            if (m_Frame == 2)
            {
                REQUIRE(context->OpenPopupStack.Size == 1);
                auto* popup = context->OpenPopupStack.back().Window;
                REQUIRE(popup != nullptr);
                const auto label = m_SelectNone
                                       ? std::string("None")
                                       : "Materials/Test.keirematerial / Compiled Material##" + m_Compiled.ToString();
                Activate(popup->GetID(label.c_str()));
            }
            const bool changed = m_Picker.Draw(ui, m_Records, m_Value, m_Options);
            if (m_Frame < 2)
                CHECK_FALSE(changed);
            else if (m_Frame == 2)
            {
                const auto expected = m_SelectNone ? Keire::AssetId{} : m_Compiled;
                CHECK(m_Value == expected);
                CHECK(changed == (expected != m_Original));
                CHECK(context->OpenPopupStack.empty());
                CHECK(m_Picker.Diagnostic().empty());
            }
            else
            {
                CHECK_FALSE(changed);
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        static void Activate(const ImGuiID id)
        {
            auto* context = ImGui::GetCurrentContext();
            context->NavActivateId = id;
            context->NavActivateDownId = id;
            context->NavActivatePressedId = id;
        }

        KeireEditor::AssetPicker m_Picker;
        KeireEditor::AssetPickerOptions m_Options;
        std::array<Keire::AssetSourceRecord, 1> m_Records;
        Keire::AssetId m_Compiled = Keire::AssetId::Generate();
        Keire::AssetId m_Value;
        Keire::AssetId m_Original;
        bool m_SelectNone;
        bool& m_Checked;
        int m_Frame = 0;
    };
} // namespace

TEST_CASE("Asset picker selection reports only changed assignments and closes unchanged selections")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    for (const bool initiallyAssigned : {false, true})
        for (const bool selectNone : {false, true})
        {
            INFO("Initially assigned: " << initiallyAssigned << ", select None: " << selectNone);
            bool checked = false;
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            Keire::Application application(specification);
            (void)application.PushLayer(
                std::make_unique<PickerInteractionLayer>(initiallyAssigned, selectNone, checked));
            CHECK(application.Run() == 0);
            CHECK(checked);
        }
}
