#include "KeireInternal/Project/StarterProjectUiInternal.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Ui/UiToolkit.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::span<const std::byte> Bytes(const std::string_view value) noexcept
        {
            return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
        }
    } // namespace

    StarterProjectUiAssets CreateStarterProjectUiAssets(AssetDatabase& database)
    {
        constexpr std::string_view style = R"(@keire-style 1;

.starter-hud {
  width: 100%;
  height: 100%;
  padding: 40px;
  flex-direction: column;
  align-items: start;
  justify-content: end;
}

.starter-card {
  width: 420px;
  padding: 20px;
  gap: 8px;
  flex-direction: column;
  align-items: center;
  background-color: #101827e8;
  border-color: #4f6580ff;
  border-width: 1px;
  border-radius: 14px;
}

.starter-title {
  width: 100%;
  height: 40px;
  color: #f3f8ffff;
  font-size: 32px;
  text-align: center;
  vertical-align: center;
}

.starter-copy {
  width: 100%;
  height: 32px;
  color: #a7bdd8ff;
  font-size: 18px;
  text-align: center;
  vertical-align: center;
}

.starter-actions {
  width: 100%;
  height: 48px;
  gap: 8px;
  flex-direction: row;
  justify-content: center;
}

Button.primary {
  width: 180px;
  height: 48px;
  background-color: #245f9eff;
  color: #ffffffff;
  font-size: 18px;
  border-radius: 8px;
  text-align: center;
  vertical-align: center;
}

Button.primary:hover {
  background-color: #3384d6ff;
}

.starter-render-target {
  width: 1024px;
  height: 1024px;
  padding: 72px;
  gap: 24px;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background-color: #102a46ff;
  border-color: #67a7ffff;
  border-width: 12px;
  border-radius: 48px;
}

.starter-render-preview {
  width: 320px;
  height: 160px;
  border-color: #67a7ffff;
  border-width: 3px;
  border-radius: 12px;
}
)";
        const auto styleAsset =
            database.CreateAsset("UI/Starter.keirestyle", CreateUiStyleSheetAssetImporter(), Bytes(style));

        const auto cardTemplate = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Starter Card Template">
  <style src=")") + styleAsset.ToString() +
                                  R"("/>
  <VisualElement id="b1b2c3d4-1000-4000-8000-000000000201" name="card" class="starter-card">
    <Slot id="b1b2c3d4-1000-4000-8000-000000000202" name="header">
      <Label id="b1b2c3d4-1000-4000-8000-000000000203" name="fallback-title" class="starter-title" text="Starter"/>
    </Slot>
    <Slot id="b1b2c3d4-1000-4000-8000-000000000204" name="content">
      <Label id="b1b2c3d4-1000-4000-8000-000000000205" name="fallback-copy" class="starter-copy" text="Ready"/>
    </Slot>
    <Slot id="b1b2c3d4-1000-4000-8000-000000000206" name="actions"/>
  </VisualElement>
</ui>
)";
        const auto cardTemplateAsset =
            database.CreateAsset("UI/StarterCard.keireui", CreateUiVisualTreeAssetImporter(), Bytes(cardTemplate));

        const auto document = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Starter HUD">
  <style src=")") + styleAsset.ToString() +
                              R"("/>
  <VisualElement id="b1b2c3d4-1000-4000-8000-000000000101" name="starter-hud" class="starter-hud">
    <TemplateContainer id="b1b2c3d4-1000-4000-8000-000000000102" name="starter-card" template=")" +
                              cardTemplateAsset.ToString() + R"(">
      <Label id="b1b2c3d4-1000-4000-8000-000000000103" name="starter-title" slot="header" class="starter-title" text="Kéire Starter"/>
      <Label id="b1b2c3d4-1000-4000-8000-000000000104" name="starter-copy" slot="content" class="starter-copy" text="Double-click this document to open UI Builder."/>
      <VisualElement id="b1b2c3d4-1000-4000-8000-000000000105" name="starter-actions" slot="actions" class="starter-actions">
        <Button id="b1b2c3d4-1000-4000-8000-000000000106" name="continue" class="primary" text="Continue"/>
      </VisualElement>
    </TemplateContainer>
  </VisualElement>
</ui>
)";
        const auto documentAsset =
            database.CreateAsset("UI/StarterHud.keireui", CreateUiVisualTreeAssetImporter(), Bytes(document));

        const auto bindingExample = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Starter Binding Example">
  <style src=")") + styleAsset.ToString() +
                                    R"("/>
  <VisualElement id="b1b2c3d4-1000-4000-8000-000000000301" name="settings" class="starter-card">
    <Label id="b1b2c3d4-1000-4000-8000-000000000302" name="player-name" class="starter-title" bind:text="Player.DisplayName"/>
    <TextField id="b1b2c3d4-1000-4000-8000-000000000303" name="display-name" bind-two-way:value="Player.DisplayName"/>
    <StarterStatus id="b1b2c3d4-1000-4000-8000-000000000304" name="status" bind:value="Session.Progress"/>
    <ListView id="b1b2c3d4-1000-4000-8000-000000000305" name="recent-events" bind:items-source="Session.RecentEvents"/>
  </VisualElement>
</ui>
)";
        (void)database.CreateAsset("UI/StarterBindingExample.keireui", CreateUiVisualTreeAssetImporter(),
                                   Bytes(bindingExample));

        const auto panel = Json{{"schemaVersion", 1},
                                {"target", "ScreenOverlay"},
                                {"scaleMode", "ScaleWithViewport"},
                                {"referenceWidth", 1920.0F},
                                {"referenceHeight", 1080.0F},
                                {"matchWidthOrHeight", 0.5F},
                                {"sortingOrder", 0},
                                {"camera", AssetId{}.ToString()},
                                {"renderTexture", AssetId{}.ToString()},
                                {"respectSafeArea", true},
                                {"worldWidth", 1.92F},
                                {"worldHeight", 1.08F},
                                {"pixelsPerUnit", 1000.0F},
                                {"depthTest", false}}
                               .dump(2) +
                           '\n';
        const auto panelAsset =
            database.CreateAsset("UI/ScreenOverlay.keireuipanel", CreateUiPanelSettingsAssetImporter(), Bytes(panel));

        const auto worldDocument = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Starter World Terminal">
  <style src=")") + styleAsset.ToString() +
                                   R"("/>
  <VisualElement id="b1b2c3d4-1000-4000-8000-000000000401" name="world-terminal" class="starter-card">
    <Label id="b1b2c3d4-1000-4000-8000-000000000402" name="world-title" class="starter-title" text="World UI"/>
    <Label id="b1b2c3d4-1000-4000-8000-000000000403" name="world-copy" class="starter-copy" text="Depth-tested and ray interactive"/>
    <Image id="b1b2c3d4-1000-4000-8000-000000000405" name="render-target-preview" class="starter-render-preview" render-texture="b1b2c3d4-1000-4000-8000-0000000000ff" accessibility-label="Live starter UI render target"/>
    <Button id="b1b2c3d4-1000-4000-8000-000000000404" name="world-action" class="primary" text="Activate"/>
  </VisualElement>
</ui>
)";
        const auto worldDocumentAsset =
            database.CreateAsset("UI/WorldTerminal.keireui", CreateUiVisualTreeAssetImporter(), Bytes(worldDocument));
        const auto worldPanel = Json{{"schemaVersion", 1},
                                     {"target", "WorldSurface"},
                                     {"scaleMode", "ScaleWithViewport"},
                                     {"referenceWidth", 960.0F},
                                     {"referenceHeight", 540.0F},
                                     {"matchWidthOrHeight", 0.5F},
                                     {"sortingOrder", 0},
                                     {"camera", AssetId{}.ToString()},
                                     {"renderTexture", AssetId{}.ToString()},
                                     {"respectSafeArea", false},
                                     {"worldWidth", 1.92F},
                                     {"worldHeight", 1.08F},
                                     {"pixelsPerUnit", 500.0F},
                                     {"depthTest", true}}
                                    .dump(2) +
                                '\n';
        const auto worldPanelAsset = database.CreateAsset("UI/WorldSurface.keireuipanel",
                                                          CreateUiPanelSettingsAssetImporter(), Bytes(worldPanel));

        const auto renderTextureDocument = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Starter Render Texture Producer">
  <style src=")") + styleAsset.ToString() +
                                           R"("/>
  <VisualElement id="b1b2c3d4-1000-4000-8000-000000000501" name="render-target" class="starter-render-target">
    <Label id="b1b2c3d4-1000-4000-8000-000000000502" name="render-target-title" class="starter-title" text="Live UI Render Target"/>
    <ProgressBar id="b1b2c3d4-1000-4000-8000-000000000503" name="render-target-progress" minimum="0" maximum="1" value="0.75"/>
    <Label id="b1b2c3d4-1000-4000-8000-000000000504" name="render-target-copy" class="starter-copy" text="Sampled by the world panel"/>
  </VisualElement>
</ui>
)";
        const auto renderTextureDocumentAsset = database.CreateAsset(
            "UI/StarterRenderTexture.keireui", CreateUiVisualTreeAssetImporter(), Bytes(renderTextureDocument));
        const auto renderTexturePanel = Json{{"schemaVersion", 1},
                                             {"target", "RenderTexture"},
                                             {"scaleMode", "ScaleWithViewport"},
                                             {"referenceWidth", 1024.0F},
                                             {"referenceHeight", 1024.0F},
                                             {"matchWidthOrHeight", 0.5F},
                                             {"sortingOrder", -10},
                                             {"camera", AssetId{}.ToString()},
                                             {"renderTexture", "b1b2c3d4-1000-4000-8000-0000000000ff"},
                                             {"respectSafeArea", false},
                                             {"worldWidth", 1.0F},
                                             {"worldHeight", 1.0F},
                                             {"pixelsPerUnit", 1024.0F},
                                             {"depthTest", false}}
                                            .dump(2) +
                                        '\n';
        const auto renderTexturePanelAsset = database.CreateAsset(
            "UI/StarterRenderTexture.keireuipanel", CreateUiPanelSettingsAssetImporter(), Bytes(renderTexturePanel));
        return {.VisualTree = documentAsset,
                .PanelSettings = panelAsset,
                .WorldVisualTree = worldDocumentAsset,
                .WorldPanelSettings = worldPanelAsset,
                .RenderTextureVisualTree = renderTextureDocumentAsset,
                .RenderTexturePanelSettings = renderTexturePanelAsset};
    }

    void WriteStarterProjectUiScript(const std::filesystem::path& projectRoot)
    {
        constexpr std::string_view source = R"(using Keire;
using Keire.UI;

namespace Game;

[UxmlElement("StarterStatus")]
public sealed class StarterStatus : VisualElement
{
    [UxmlAttribute("value")]
    public float Value { get; set; }
}

[StableComponentId("b1b2d001-1000-4000-8000-000000000001")]
public sealed class StarterUiController : Behaviour
{
    private UIDocument? _document;
    private RuntimeVisualElement? _continue;

    protected override void OnEnable()
    {
        UxmlElementRegistry.Register<StarterStatus>();
        Resolve();
    }

    protected override void OnDisable() => _continue = null;
    protected override void OnBeforeReload() => _continue = null;
    protected override void OnAfterReload() => OnEnable();

    protected override void Update()
    {
        if (_continue is not { IsAlive: true })
            Resolve();
        if (_continue?.ClickedThisFrame != true)
            return;

        _continue.Text = "Ready";
        _continue.Interactable = false;
        Debug.Log("Starter UI Document button clicked.");
    }

    private void Resolve()
    {
        _document ??= Entity.GetComponent<UIDocument>();
        _continue = _document?.Q("continue");
    }
}

[StableComponentId("b1b2d001-1000-4000-8000-000000000002")]
public sealed class StarterWorldUiController : Behaviour
{
    private UIDocument? _document;
    private RuntimeVisualElement? _action;

    protected override void OnEnable() => Resolve();
    protected override void OnDisable() => _action = null;
    protected override void OnBeforeReload() => _action = null;
    protected override void OnAfterReload() => Resolve();

    protected override void Update()
    {
        if (_action is not { IsAlive: true })
            Resolve();
        if (_action?.ClickedThisFrame == true)
            _action.Text = _action.Text == "Activate" ? "Active" : "Activate";
    }

    private void Resolve()
    {
        _document ??= Entity.GetComponent<UIDocument>();
        _action = _document?.Q("world-action");
    }
}
)";
        WriteTextFileAtomically(projectRoot / "Assets/Scripts/Runtime/StarterUi.cs", source);
    }
} // namespace Keire::Detail
