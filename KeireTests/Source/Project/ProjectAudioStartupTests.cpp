#include "Keire/Application.h"
#include "Keire/Project/ProjectAuthoringSettings.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"
#include "KeireTests/TestSupport.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    class AudioStartupFixture final
    {
      public:
        AudioStartupFixture() : Root(KeireTests::MakeTestDirectory("project-audio-startup"))
        {
            std::filesystem::create_directories(Root);
            const auto project = Keire::Project::Create({Root, "AudioStartup", Keire::ProjectTemplate::Empty});
            ProjectRoot = project->Root();
        }

        ~AudioStartupFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
        std::filesystem::path ProjectRoot;
    };

    class AudioStartupApplication final : public Keire::Application
    {
      public:
        using Application::Application;

      protected:
        void OnInitialize() override { RequestExit(); }
    };
} // namespace

TEST_CASE("Project audio startup applies project preferences only when explicitly enabled")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    AudioStartupFixture fixture;
    auto authoring = Keire::DefaultProjectAuthoringSettings();
    authoring.Audio.MaximumVoices = 32;
    authoring.Audio.MaximumVirtualVoices = 64;
    authoring.Audio.MixSampleRate = 44100;
    authoring.Audio.PeriodFrames = 512;
    authoring.Audio.OutputLayout = Keire::AudioChannelLayout::Mono;
    authoring.Audio.PlaybackDeviceId = "0123abcd";
    Keire::SaveProjectAuthoringSettings(fixture.ProjectRoot, authoring);

    Keire::ApplicationSpecification specification;
    specification.MainWindow.Visible = false;
    specification.ManageLogging = false;
    specification.Projects.Mode = Keire::ProjectMode::Editor;
    specification.Projects.Root = fixture.ProjectRoot;
    specification.Assets.Mode = Keire::AssetMode::Development;
    // Audio stays disabled: validate startup configuration without requiring a physical device.
    const auto original = specification.Audio;
    CHECK_FALSE(specification.LoadProjectAudioSettings);
    bool expectProjectSettings = false;

    SUBCASE("explicit client settings remain unchanged by default") {}
    SUBCASE("opt in loads all six fields after opening")
    {
        specification.LoadProjectAudioSettings = true;
        expectProjectSettings = true;
    }
    SUBCASE("interrupted save is recovered before project audio is read")
    {
        specification.LoadProjectAudioSettings = true;
        expectProjectSettings = true;
        const auto settingsPath = fixture.ProjectRoot / "ProjectSettings/Authoring.keiresettings";
        const auto originalText = Keire::Detail::ReadTextFile(settingsPath, 1024U * 1024U);
        const std::string replacement = "{";
        const auto digest = [](const std::string_view value)
        {
            return Keire::Detail::DigestToString(
                Keire::Detail::Sha256(std::as_bytes(std::span(value.data(), value.size()))));
        };
        const auto directory = fixture.ProjectRoot / "Library/ProjectSettingsSave/active";
        std::filesystem::create_directories(directory);
        Keire::Detail::WriteTextFileAtomically(directory / "0.backup", originalText);
        const nlohmann::json journal{{"schemaVersion", 1},
                                     {"state", "prepared"},
                                     {"files",
                                      {{{"destination", "ProjectSettings/Authoring.keiresettings"},
                                        {"original", digest(originalText)},
                                        {"replacement", digest(replacement)}}}}};
        Keire::Detail::WriteTextFileAtomically(directory / "journal.json", journal.dump());
        Keire::Detail::WriteTextFileAtomically(settingsPath, replacement);
        REQUIRE(Keire::Detail::HasPendingProjectSettingsSave(fixture.ProjectRoot));
    }
    SUBCASE("malformed authoring retains the supplied specification")
    {
        specification.LoadProjectAudioSettings = true;
        Keire::Detail::WriteTextFileAtomically(fixture.ProjectRoot / "ProjectSettings/Authoring.keiresettings", "{");
    }
    SUBCASE("opt in without an editor project retains the supplied specification")
    {
        specification.LoadProjectAudioSettings = true;
        specification.Projects.Mode = Keire::ProjectMode::Disabled;
    }

    AudioStartupApplication application(specification);
    REQUIRE(application.Run() == 0);
    CHECK_FALSE(Keire::Detail::HasPendingProjectSettingsSave(fixture.ProjectRoot));
    const auto& actual = application.Specification().Audio;
    CHECK(actual.MaximumVoices == (expectProjectSettings ? authoring.Audio.MaximumVoices : original.MaximumVoices));
    CHECK(actual.MaximumVirtualVoices ==
          (expectProjectSettings ? authoring.Audio.MaximumVirtualVoices : original.MaximumVirtualVoices));
    CHECK(actual.MixSampleRate == (expectProjectSettings ? authoring.Audio.MixSampleRate : original.MixSampleRate));
    CHECK(actual.PeriodFrames == (expectProjectSettings ? authoring.Audio.PeriodFrames : original.PeriodFrames));
    CHECK(actual.OutputLayout == (expectProjectSettings ? authoring.Audio.OutputLayout : original.OutputLayout));
    CHECK(actual.PlaybackDeviceId ==
          (expectProjectSettings ? authoring.Audio.PlaybackDeviceId : original.PlaybackDeviceId));
    CHECK(actual.Mode == original.Mode);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}
