#include "Keire/Core.h"

#include "KeireInternal/Audio/AudioImportBackend.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
    class TemporaryPresentationProject final
    {
      public:
        TemporaryPresentationProject()
            : Root(std::filesystem::temp_directory_path() /
                   ("krp-" + Keire::AssetId::Generate().ToString().substr(0, 8)))
        {
            std::filesystem::create_directories(Root / "Assets");
        }

        ~TemporaryPresentationProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        void Write(const std::filesystem::path& relative, const std::string_view content) const
        {
            const auto path = Root / "Assets" / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            REQUIRE(stream.good());
        }

        std::filesystem::path Root;
    };
} // namespace

TEST_CASE("retained runtime UI lays out, draws, hit-tests, and emits clicks")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto canvas = tree->Create(Keire::RuntimeUiElementType::Canvas);
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button, canvas);

    Keire::RuntimeUiStyle canvasStyle;
    canvasStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    canvasStyle.Width = 800.0F;
    canvasStyle.Height = 600.0F;
    REQUIRE(tree->SetStyle(canvas, canvasStyle));

    Keire::RuntimeUiStyle buttonStyle;
    buttonStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    buttonStyle.X = 20.0F;
    buttonStyle.Y = 30.0F;
    buttonStyle.Width = 180.0F;
    buttonStyle.Height = 48.0F;
    buttonStyle.Background = {0.10F, 0.45F, 0.95F, 1.0F};
    REQUIRE(tree->SetStyle(button, buttonStyle));
    REQUIRE(tree->SetInteractable(button, true));
    REQUIRE(tree->SetContent(button, {.Text = "Launch"}));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(800.0F, 600.0F, {}, settings);
    CHECK_FALSE(tree->DrawCommands().empty());
    CHECK(tree->HitTest(40.0F, 50.0F) == button);

    tree->PointerMove(40.0F, 50.0F);
    tree->PointerButton(40.0F, 50.0F, Keire::RuntimeUiPointerButton::Primary, true);
    tree->PointerButton(40.0F, 50.0F, Keire::RuntimeUiPointerButton::Primary, false);
    Keire::RuntimeUiEvent event;
    bool clicked = false;
    while (tree->PollEvent(event))
        clicked = clicked || (event.Type == Keire::RuntimeUiEventType::Click && event.Target == button);
    CHECK(clicked);
}

TEST_CASE("retained runtime UI resolves anchors pivots size deltas and local scale against its parent")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto canvas = tree->Create(Keire::RuntimeUiElementType::Canvas);
    const auto panel = tree->Create(Keire::RuntimeUiElementType::Panel, canvas);

    Keire::RuntimeUiStyle canvasStyle;
    canvasStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    canvasStyle.Width = 800.0F;
    canvasStyle.Height = 600.0F;
    REQUIRE(tree->SetStyle(canvas, canvasStyle));

    Keire::RuntimeUiStyle panelStyle;
    panelStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    panelStyle.UseAnchors = true;
    panelStyle.AnchorMinimum = {0.25F, 0.25F};
    panelStyle.AnchorMaximum = {0.75F, 0.75F};
    panelStyle.Pivot = {0.5F, 0.5F};
    panelStyle.AnchoredPosition = {10.0F, 5.0F};
    panelStyle.SizeDelta = {-20.0F, -40.0F};
    panelStyle.LocalScale = {1.0F, 1.0F};
    REQUIRE(tree->SetStyle(panel, panelStyle));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(800.0F, 600.0F, {}, settings);
    const auto state = tree->State(panel);
    REQUIRE(state);
    CHECK(state->Rect.X == doctest::Approx(220.0F));
    CHECK(state->Rect.Y == doctest::Approx(175.0F));
    CHECK(state->Rect.Width == doctest::Approx(380.0F));
    CHECK(state->Rect.Height == doctest::Approx(260.0F));
}

TEST_CASE("retained vertical and grid layouts place children without fighting Rect Transform anchors")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto vertical = tree->Create(Keire::RuntimeUiElementType::VerticalLayout);
    const auto first = tree->Create(Keire::RuntimeUiElementType::Button, vertical);
    const auto second = tree->Create(Keire::RuntimeUiElementType::Button, vertical);

    Keire::RuntimeUiStyle layout;
    layout.Position = Keire::RuntimeUiPositionMode::Absolute;
    layout.Width = 300.0F;
    layout.Height = 220.0F;
    layout.Padding = {10.0F, 10.0F, 10.0F, 10.0F};
    layout.Gap = 8.0F;
    layout.ControlChildWidth = true;
    layout.ForceExpandWidth = true;
    REQUIRE(tree->SetStyle(vertical, layout));

    Keire::RuntimeUiStyle child;
    child.Position = Keire::RuntimeUiPositionMode::Flow;
    child.Width = 120.0F;
    child.Height = 40.0F;
    REQUIRE(tree->SetStyle(first, child));
    REQUIRE(tree->SetStyle(second, child));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(300.0F, 220.0F, {}, settings);
    const auto firstState = tree->State(first);
    const auto secondState = tree->State(second);
    REQUIRE(firstState);
    REQUIRE(secondState);
    CHECK(firstState->Rect.X == doctest::Approx(10.0F));
    CHECK(firstState->Rect.Width == doctest::Approx(280.0F));
    CHECK(firstState->Rect.Y == doctest::Approx(10.0F));
    CHECK(secondState->Rect.Y == doctest::Approx(58.0F));

    REQUIRE(tree->SetType(vertical, Keire::RuntimeUiElementType::GridLayout));
    layout.GridCellSize = {100.0F, 40.0F};
    layout.Gap = 10.0F;
    REQUIRE(tree->SetStyle(vertical, layout));
    const auto third = tree->Create(Keire::RuntimeUiElementType::Button, vertical);
    REQUIRE(tree->SetStyle(third, child));
    tree->Layout(300.0F, 220.0F, {}, settings);
    const auto thirdState = tree->State(third);
    REQUIRE(thirdState);
    CHECK(thirdState->Rect.X == doctest::Approx(10.0F));
    CHECK(thirdState->Rect.Y == doctest::Approx(60.0F));
}

TEST_CASE("retained buttons render their hover pressed and disabled visual states")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button);
    Keire::RuntimeUiStyle style;
    style.Position = Keire::RuntimeUiPositionMode::Absolute;
    style.Width = 160.0F;
    style.Height = 48.0F;
    style.Background = {0.1F, 0.2F, 0.3F, 1.0F};
    style.HoverBackground = {0.2F, 0.4F, 0.6F, 1.0F};
    style.PressedBackground = {0.05F, 0.1F, 0.2F, 1.0F};
    style.DisabledBackground = {0.15F, 0.15F, 0.15F, 0.5F};
    REQUIRE(tree->SetStyle(button, style));
    REQUIRE(tree->SetInteractable(button, true));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(320.0F, 180.0F, {}, settings);
    tree->PointerMove(20.0F, 20.0F);
    tree->Layout(320.0F, 180.0F, {}, settings);
    const auto hovered = tree->DrawCommands();
    REQUIRE_FALSE(hovered.empty());
    CHECK(hovered.front().ColorValue.Red == doctest::Approx(style.HoverBackground.Red));

    tree->PointerButton(20.0F, 20.0F, Keire::RuntimeUiPointerButton::Primary, true);
    tree->Layout(320.0F, 180.0F, {}, settings);
    CHECK(tree->DrawCommands().front().ColorValue.Red == doctest::Approx(style.PressedBackground.Red));

    REQUIRE(tree->SetEnabled(button, false));
    tree->Layout(320.0F, 180.0F, {}, settings);
    CHECK(tree->DrawCommands().front().ColorValue.Red == doctest::Approx(style.DisabledBackground.Red));
}

TEST_CASE("audio clip assets round-trip deterministic PCM payloads")
{
    Keire::AudioClipData source;
    source.SampleRate = 48'000;
    source.Channels = 2;
    source.Samples = {0.0F, 0.0F, 0.25F, -0.25F, 0.5F, -0.5F};

    const auto encoded = Keire::AudioClipAsset::Encode(source);
    const auto decoded = Keire::AudioClipAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->FrameCount() == 3);
    CHECK(decoded->Clip()->SampleRate == source.SampleRate);
    CHECK(decoded->Clip()->Channels == source.Channels);
    CHECK(decoded->Clip()->Samples == source.Samples);
}

TEST_CASE("audio clip assets preserve encoded streaming payloads without PCM expansion")
{
    Keire::AudioClipData source;
    source.SampleRate = 48'000;
    source.Channels = 2;
    source.Frames = 48'000ULL * 60ULL * 30ULL;
    source.Streaming = true;
    source.EncodedSource = {
        std::byte{0x52}, std::byte{0x49}, std::byte{0x46}, std::byte{0x46},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    };

    const auto encoded = Keire::AudioClipAsset::Encode(source);
    const auto decoded = Keire::AudioClipAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->FrameCount() == source.Frames);
    CHECK(decoded->Clip()->Streaming);
    CHECK(decoded->Clip()->Samples.empty());
    CHECK(decoded->Clip()->EncodedSource == source.EncodedSource);
}

TEST_CASE("audio importer uses its private backend only after the native fast path rejects a source")
{
    bool backendInvoked = false;
    auto importer = Keire::Detail::CreateAudioClipAssetImporter(
        [&](const Keire::AssetImportContext&, const std::span<const std::byte>)
        {
            backendInvoked = true;
            Keire::Detail::AudioTranscodeResult result;
            result.EncodedAudio = {
                std::byte{0x52}, std::byte{0x49}, std::byte{0x46}, std::byte{0x46}, std::byte{0x26}, std::byte{0x00},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x57}, std::byte{0x41}, std::byte{0x56}, std::byte{0x45},
                std::byte{0x66}, std::byte{0x6d}, std::byte{0x74}, std::byte{0x20}, std::byte{0x10}, std::byte{0x00},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
                std::byte{0x40}, std::byte{0x1f}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3e},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00},
                std::byte{0x64}, std::byte{0x61}, std::byte{0x74}, std::byte{0x61}, std::byte{0x02}, std::byte{0x00},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
            };
            result.SampleRate = 8'000;
            result.Channels = 1;
            result.Frames = 1;
            result.SourceCodec = "test";
            result.SourceContainer = "test";
            result.RuntimeEncoding = "PCM WAV";
            return result;
        });
    const std::array invalidSource{std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
    const auto output = importer.ContextualImport(Keire::AssetImportContext{}, invalidSource);

    REQUIRE(backendInvoked);
    REQUIRE(output.Diagnostics.size() == 1);
    CHECK(output.Diagnostics.front().Message.find("PCM WAV") != std::string::npos);
    const auto transcodeMode = std::ranges::find(importer.ImportOptions, std::string("transcodeMode"),
                                                 &Keire::AssetImportOptionDescriptor::Key);
    REQUIRE(transcodeMode != importer.ImportOptions.end());
    CHECK(std::get<std::string>(transcodeMode->DefaultValue) == "fast");
    const auto decoded = Keire::AudioClipAsset::Decode(output.Bytes);
    REQUIRE(decoded);
    CHECK(decoded->Clip()->SampleRate == 8'000);
    CHECK(decoded->Clip()->Channels == 1);
    CHECK(decoded->FrameCount() == 1);
}

TEST_CASE("scene runtime presentation ignores unchanged viewport assignments")
{
    auto assets = Keire::CreateRef<Keire::AssetSystem>(Keire::AssetSystemSpecification{});
    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("Viewport"));
    auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets, Keire::Ref<Keire::AudioSystem>{});

    session->Play();
    const auto presentation = session->Presentation();
    REQUIRE(presentation);
    CHECK(presentation->Statistics().SynchronizationCount == 1);

    session->SetPresentationViewport(1920.0F, 1080.0F);
    CHECK(presentation->Statistics().SynchronizationCount == 1);

    session->SetPresentationViewport(1280.0F, 720.0F);
    CHECK(presentation->Statistics().SynchronizationCount == 2);
    session->SetPresentationViewport(1280.0F, 720.0F);
    CHECK(presentation->Statistics().SynchronizationCount == 2);
}

TEST_CASE("scene presentation clear discards UI events deferred during filtered consumption")
{
    auto assets = Keire::CreateRef<Keire::AssetSystem>(Keire::AssetSystemSpecification{});
    auto presentation = Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, Keire::Ref<Keire::AudioSystem>{});
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Presentation UI"));
    auto canvas = scene->CreateEntity("Canvas");
    const auto canvasComponent = canvas.AddComponent<Keire::CanvasComponent>();
    REQUIRE(canvasComponent);
    canvasComponent->SetScaleMode(Keire::CanvasScaleMode::ConstantPixels);

    auto first = scene->CreateEntity("First", canvas);
    const auto firstRect = first.AddComponent<Keire::RectTransformComponent>();
    REQUIRE(firstRect);
    firstRect->SetAnchorMinimum({});
    firstRect->SetAnchorMaximum({});
    firstRect->SetPivot({});
    firstRect->SetAnchoredPosition({10.0F, 10.0F});
    firstRect->SetSizeDelta({100.0F, 50.0F});
    REQUIRE(first.AddComponent<Keire::UiButtonComponent>());

    auto second = scene->CreateEntity("Second", canvas);
    const auto secondRect = second.AddComponent<Keire::RectTransformComponent>();
    REQUIRE(secondRect);
    secondRect->SetAnchorMinimum({});
    secondRect->SetAnchorMaximum({});
    secondRect->SetPivot({});
    secondRect->SetAnchoredPosition({140.0F, 10.0F});
    secondRect->SetSizeDelta({100.0F, 50.0F});
    REQUIRE(second.AddComponent<Keire::UiButtonComponent>());

    presentation->Synchronize(scene, 320.0F, 180.0F, false);
    presentation->PointerMove(25.0F, 25.0F);
    presentation->PointerButton(25.0F, 25.0F, Keire::RuntimeUiPointerButton::Primary, true);
    presentation->PointerButton(25.0F, 25.0F, Keire::RuntimeUiPointerButton::Primary, false);
    CHECK_FALSE(presentation->ConsumeClick(second.Id()));

    presentation->Clear();
    Keire::RuntimeUiEvent event;
    CHECK_FALSE(presentation->PollUiEvent(event));

    scene->Close();
    assets->Close();
}

TEST_CASE("scene presentation treats automatic and manual audio playback as edge-triggered requests")
{
    TemporaryPresentationProject project;
    project.Write("OneShot.testaudio", "test");
    const Keire::AssetId masterBus(0x50524553454e5441ULL, 1);
    const Keire::AssetId effectsBus(0x50524553454e5441ULL, 2);
    Keire::AudioMixerDefinition mixerDefinition{
        .MasterBus = masterBus,
        .Buses =
            {
                {.Id = masterBus, .Name = "Master", .Gain = 1.0F},
                {.Id = effectsBus, .Name = "Effects", .Parent = masterBus, .Gain = 0.5F},
            },
    };
    const auto mixerBytes = Keire::AudioMixerAsset::Encode(mixerDefinition);
    project.Write("Runtime.keiremixer", {reinterpret_cast<const char*>(mixerBytes.data()), mixerBytes.size()});

    Keire::AssetImporterRegistration importer;
    importer.Name = "Test.AudioClip";
    importer.Type = Keire::AudioClipAsset::StaticType();
    importer.Extensions = {".testaudio"};
    importer.Import = [](const std::span<const std::byte>)
    {
        Keire::AudioClipData clip;
        clip.SampleRate = 48'000;
        clip.Channels = 1;
        clip.Samples.assign(64, 0.25F);
        return Keire::AudioClipAsset::Encode(clip);
    };
    Keire::AssetDatabaseSpecification databaseSpecification;
    databaseSpecification.ProjectRoot = project.Root;
    databaseSpecification.Importers.push_back(std::move(importer));
    databaseSpecification.Importers.push_back(Keire::CreateAudioMixerAssetImporter());
    auto database = Keire::CreateRef<Keire::AssetDatabase>(std::move(databaseSpecification));
    const auto imported = database->ImportAll();
    const auto record = database->Find("OneShot.testaudio");
    const auto mixerRecord = database->Find("Runtime.keiremixer");
    REQUIRE(record);
    REQUIRE(mixerRecord);

    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.DevelopmentCatalog = imported.CatalogPath;
    assetSpecification.WorkerCount = 1;
    assetSpecification.Decoders.push_back(Keire::CreateAudioClipAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateAudioMixerAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    Keire::AudioSystemSpecification audioSpecification;
    audioSpecification.Mode = Keire::AudioMode::Headless;
    auto audio = Keire::CreateRef<Keire::AudioSystem>(audioSpecification);
    auto presentation = Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, audio);
    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                Keire::SceneAsset::EmptyDefinition("Presentation Audio"));
    auto sourceEntity = scene->CreateEntity("One shot");
    const auto source = sourceEntity.AddComponent<Keire::AudioSourceComponent>();
    REQUIRE(source);
    source->SetClip(record->Id);
    source->SetMixer(mixerRecord->Id);
    source->SetBus("Stale legacy bus");
    source->SetBusId(effectsBus);
    source->SetLoop(true);
    source->SetPlayOnAwake(true);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((presentation->Statistics().ActiveAudioSources == 0 || presentation->Statistics().PendingAudioAssets != 0) &&
           std::chrono::steady_clock::now() < deadline)
    {
        presentation->Synchronize(scene, 320.0F, 180.0F, true);
        (void)assets->PumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(presentation->Statistics().ActiveAudioSources == 1);
    CHECK(presentation->Statistics().PendingAudioAssets == 0);

    const auto routed = audio->RenderVoicesOffline(1);
    REQUIRE(routed.size() == 2);
    const auto centerPanGain = std::sqrt(0.5F);
    const auto initialRoutedSample = 0.25F * 0.5F * centerPanGain;
    CHECK(routed[0] == doctest::Approx(initialRoutedSample));
    CHECK(routed[1] == doctest::Approx(initialRoutedSample));

    mixerDefinition.Buses[1].Gain = 0.25F;
    const auto revisedMixerBytes = Keire::AudioMixerAsset::Encode(mixerDefinition);
    project.Write("Runtime.keiremixer",
                  {reinterpret_cast<const char*>(revisedMixerBytes.data()), revisedMixerBytes.size()});
    const auto revisedImport = database->ImportAll();
    CHECK(revisedImport.Imported >= 1);
    REQUIRE(assets->Unmount(revisedImport.CatalogPath));
    assets->Mount({revisedImport.CatalogPath, 0, true});
    REQUIRE(assets->Reload(mixerRecord->Id));
    const auto revisedRoutedSample = 0.25F * 0.25F * centerPanGain;
    bool observedRevisedMixer = false;
    const auto reloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!observedRevisedMixer && std::chrono::steady_clock::now() < reloadDeadline)
    {
        (void)assets->PumpCompletions();
        presentation->Synchronize(scene, 320.0F, 180.0F, true);
        const auto revised = audio->RenderVoicesOffline(1);
        observedRevisedMixer = revised.size() == 2 && revised[0] == doctest::Approx(revisedRoutedSample) &&
                               revised[1] == doctest::Approx(revisedRoutedSample);
        if (!observedRevisedMixer)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(observedRevisedMixer);

    project.Write("Runtime.keiremixer", "{ malformed");
    CHECK_THROWS_WITH_AS((void)database->ImportAll(), doctest::Contains("Audio mixer source is malformed"),
                         std::invalid_argument);
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    const auto afterRejectedSource = audio->RenderVoicesOffline(1);
    REQUIRE(afterRejectedSource.size() == 2);
    CHECK(afterRejectedSource[0] == doctest::Approx(revisedRoutedSample));
    CHECK(afterRejectedSource[1] == doctest::Approx(revisedRoutedSample));

    source->SetLoop(false);
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    REQUIRE(presentation->Seek(sourceEntity.Id(), 0.0F));
    (void)audio->RenderVoicesOffline(128);
    REQUIRE(audio->Voices().empty());
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    CHECK(audio->Voices().empty());
    CHECK(presentation->Statistics().ActiveAudioSources == 0);

    REQUIRE(presentation->Play(sourceEntity.Id()));
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    REQUIRE(audio->Voices().size() == 1);
    CHECK(presentation->Playback(sourceEntity.Id()).State == Keire::AudioSourcePlaybackState::Playing);
    REQUIRE(presentation->Pause(sourceEntity.Id()));
    CHECK(presentation->Playback(sourceEntity.Id()).State == Keire::AudioSourcePlaybackState::Paused);
    (void)audio->RenderVoicesOffline(2);
    CHECK(audio->Voices().front().Frame == 0);
    REQUIRE(presentation->Seek(sourceEntity.Id(), 2.0F / 48'000.0F));
    CHECK(audio->Voices().front().Frame == 2);
    REQUIRE(presentation->Resume(sourceEntity.Id()));
    CHECK(presentation->Playback(sourceEntity.Id()).State == Keire::AudioSourcePlaybackState::Playing);
    REQUIRE(presentation->Stop(sourceEntity.Id()));
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    CHECK(audio->Voices().empty());

    presentation->Synchronize(scene, 320.0F, 180.0F, false);
    presentation->Synchronize(scene, 320.0F, 180.0F, true);
    CHECK(audio->Voices().size() == 1);

    const auto mixerRouting = audio->Voices().front().MixerRouting;
    REQUIRE(mixerRouting);
    presentation->Clear();
    CHECK_FALSE(audio->UpdateMixer(mixerRouting, mixerDefinition));
    scene->Close();
    assets->Close();
    audio->Close();
}
