#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    class SnapshotController final : public KeireEditor::IAudioMixerPanelController
    {
      public:
        SnapshotController()
        {
            auto definition = Keire::AudioMixerAsset::DefaultDefinition();
            definition.Snapshots.push_back({.Id = Keire::AssetId::Generate(),
                                            .Name = "Quiet",
                                            .Parameters = {{.Type = Keire::AudioMixerSnapshotParameterType::BusGain,
                                                            .Target = definition.MasterBus,
                                                            .Value = 0.25F},
                                                           {.Type = Keire::AudioMixerSnapshotParameterType::BusMute,
                                                            .Target = definition.MasterBus,
                                                            .Value = 1.0F}}});
            Document.Open(Keire::AssetId::Generate(), std::move(definition), 1);
        }

        KeireEditor::AudioMixerDocument& AudioMixerState() noexcept override { return Document; }

        const Keire::UiThemeDefinition& AudioMixerTheme() const noexcept override { return Theme; }

        Keire::Ref<Keire::AssetDatabase> AudioMixerDatabase() const noexcept override { return {}; }

        std::string_view AudioMixerPreviewDiagnostic() const noexcept override { return {}; }

        Keire::AudioMeterSnapshot AudioMixerMeters() const noexcept override { return {}; }

        void ActivateAudioMixerHistory() noexcept override {}

        void SaveAudioMixerDocument() override { Document.Save(); }

        void DiscardAudioMixerDocument() override { Document.Discard(); }

        void ReloadAudioMixerDocument(Keire::AssetId) override {}

        void UndoAudioMixerEdit() override { (void)Document.Undo(); }

        void RedoAudioMixerEdit() override { (void)Document.Redo(); }

        void StopAudioMixerPreview() noexcept override {}

        void ReportAudioMixerError(std::string message) noexcept override { Error = std::move(message); }

        Keire::AudioMixerDefinition Preview;
        Keire::UiThemeDefinition Theme;
        std::string Error;
        KeireEditor::AudioMixerDocument Document{
            {.Preview = [&](Keire::AssetId, const Keire::AudioMixerDefinition& value) { Preview = value; },
             .Persist = [](Keire::AssetId, std::span<const std::byte>) {}}};
    };

    class SnapshotLayer final : public Keire::Layer
    {
      public:
        explicit SnapshotLayer(bool& checked) : Layer("Snapshot audition controls"), m_Checked(checked) {}

      protected:
        void OnAttach() override
        {
            m_Panel.Attach(Owner().GetUiWorkspace());
            m_Panel.Registration().SetVisible(true);
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            auto* context = ImGui::GetCurrentContext();
            if (m_Frame == 1)
            {
                auto* panel = ImGui::FindWindowByName("Audio Mixer###editor.audio-mixer");
                REQUIRE(panel != nullptr);
                auto* tabs = context->TabBars.GetByKey(panel->GetID("AudioMixerTabs"));
                REQUIRE(tabs != nullptr);
                tabs->NextSelectedTabId = ImHashStr("Snapshots", 0, tabs->ID);
            }
            if (m_Frame >= 3 && m_Frame <= 6)
            {
                ImGuiWindow* details = nullptr;
                for (auto* window : context->Windows)
                {
                    if (std::string_view(window->Name).find("/MixerSnapshotDetails_") != std::string_view::npos)
                    {
                        details = window;
                    }
                }
                REQUIRE(details != nullptr);
                ImGuiID action = 0;
                if (m_Frame == 3)
                {
                    action = details->GetID("Audition Snapshot");
                }
                else if (m_Frame == 4)
                {
                    action = details->GetID("Restore Authored Mix");
                }
                else
                {
                    action = ImHashStr("Remove Parameter", 0, ImHashStr("0", 0, details->ID));
                }
                context->NavActivateId = action;
                context->NavActivateDownId = action;
                context->NavActivatePressedId = action;
            }
            m_Panel.Draw(ui);
            CHECK(m_Controller.Error.empty());
            if (m_Frame == 3)
            {
                CHECK(m_Controller.Preview.Buses.front().Gain == doctest::Approx(0.25F));
                CHECK(m_Controller.Preview.Buses.front().Mute);
                CHECK_FALSE(m_Controller.Document.Dirty());
                CHECK(m_Controller.Document.Definition().Snapshots.back().Parameters.size() == 2);
            }
            if (m_Frame == 4)
            {
                CHECK(m_Controller.Preview == m_Controller.Document.Definition());
                CHECK_FALSE(m_Controller.Document.Dirty());
            }
            if (m_Frame == 5)
            {
                const auto& parameters = m_Controller.Document.Definition().Snapshots.back().Parameters;
                REQUIRE(parameters.size() == 1);
                CHECK(parameters.front().Type == Keire::AudioMixerSnapshotParameterType::BusMute);
                CHECK(m_Controller.Document.Dirty());
            }
            if (m_Frame == 6)
            {
                CHECK(m_Controller.Document.Definition().Snapshots.back().Parameters.empty());
                m_Checked = true;
                Owner().RequestExit();
            }
            ++m_Frame;
        }

      private:
        bool& m_Checked;
        int m_Frame = 0;
        SnapshotController m_Controller;
        KeireEditor::AudioMixerPanel m_Panel{m_Controller};
    };
} // namespace

TEST_CASE("Audio Mixer snapshot controls audition restore and remove successive parameter rows")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool checked = false;
    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.TargetFrameRate = 0;
    specification.Ui.Mode = Keire::UiMode::Headless;
    specification.Ui.LayoutPath.clear();
    specification.Ui.Workspace.Enabled = true;
    specification.Ui.Workspace.Ephemeral = true;
    Keire::Application application(specification);
    (void)application.PushLayer(std::make_unique<SnapshotLayer>(checked));
    CHECK(application.Run() == 0);
    CHECK(checked);
}
