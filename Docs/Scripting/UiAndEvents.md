# UI Toolkit And Events From C#

Kéire UI Toolkit is a retained visual-tree system. UI structure, styling, and presentation policy are separate
assets, and a scene references them through one `UIDocument` component. The Editor shell remains ImGui; UI Toolkit is
for game and tool content authored by a project.

## Asset Model

- `.keireui` stores the visual tree. Every element has a stable ID and may have a name, classes, attributes, inline
  properties, bindings, a template reference, a slot, and children.
- `.keirestyle` stores CSS-like selectors and declarations. Type, `#name`, `.class`, descendant, child, compound, and
  pseudo-state selectors participate in a deterministic specificity cascade.
- `.keireuipanel` stores scaling and output policy: screen overlay, camera overlay, render texture, or world surface.

Double-click a `.keireui` file to open the dockable UI Builder. The Builder owns its own hierarchy, control library,
preview, Inspector, stylesheet and selector list, binding view, and source previews. Builder edits use the document
undo stack and are not written until the document is saved.

Add a `UIDocument` component to a scene entity and assign both the visual-tree and panel-settings assets. Component and
panel sorting orders are added together. Later documents draw above earlier documents; pointer input is offered in the
reverse order until handled.

## UI Builder Workflow

Create a UI Document from the Project panel, then assign imported `.keirestyle` and `.keireuipanel` assets as needed.
A `.keireui` document opens with these working authoring surfaces:

- Hierarchy multi-selection, drag-and-drop reparenting, copy/paste with regenerated stable IDs and names, and one
  undoable transaction per accepted edit.
- A control library containing the built-in controls and the custom controls explicitly registered by the active
  last-good managed generation. Click inserts a visible, sized control beside the current non-container selection or
  inside the selected container; drag a control onto the canvas to place and size it directly.
- Inspector editing for names, reusable classes, inline properties, templates, slots, and one-way, two-way, or one-time
  binding declarations.
- Linked stylesheet management plus selector/declaration add, edit, and remove operations. Create a **UI Style Sheet**
  from the Project panel, then link it with the typed searchable asset picker or by dragging the asset onto that picker.
  Style edits have their own undo/redo history and explicit Save/Reload boundary. Select **Styles** for the visual
  three-pane Style Studio; see [Visual Style Studio](../UiStyleStudio.md) for responsive rules, design tokens, source
  drafts, font families, and v1/v2 migration.
- A retained-tree preview with resolution presets or a custom size, landscape/portrait orientation, **Match Game
  View**, DPI/reference scaling, safe-area visualization, zoom/pan, rulers, guides, and live pseudo-state toggles.

New documents open on a 1920x1080 authoring canvas when they do not reference Panel Settings. Clicking a library
control places it near the active parent center; dragging honors the drop point and keeps the complete control inside
the parent content box. Direct canvas moves and resizes write explicit absolute `left`, `top`, `width`, and `height`
properties, while editing those layout properties in the Inspector remains the way to return to flex-flow authoring.
Resize edges and corners use the corresponding `UiCursorShape` through the public `UiFrame::SetCursorShape` boundary;
projects never need to include or call Dear ImGui cursor APIs.

Preview resolution, DPI, safe-area, rulers, guides, zoom, and pseudo-state choices are authoring aids; they do not
silently rewrite the referenced `.keireuipanel`. Edit presentation target, scaling, ordering, camera/texture identity,
and world dimensions on the Panel Settings asset.

While Play Mode is active, an unsaved edit to the open visual tree replaces matching running `UIDocument` instances
through a development-only asset revision. **Save** makes the authored source authoritative. Stopping Play Mode,
switching documents, reloading, or closing the workspace restores the imported baseline when the draft was not saved.
Rejected drafts leave the last-good running document intact and surface an actionable Builder diagnostic.

The Debugger can pick a presented element in Game view and inspect its stable ID, resolved layout/style state, selector
precedence, focus and pointer capture, event capture/target/bubble routes, dirty reasons, UI vertices/batches, atlas
usage, and style/layout/repaint timings when the corresponding runtime provider is available. Missing providers and
stale generations are shown as unavailable or stale instead of fabricated zeroes. Debug snapshots stay local to the
Editor, do not modify source assets, and are not uploaded by UI Builder.

## Viewport Contract

Screen-overlay and camera-overlay documents render in Game view, Play Mode, and packaged players. They are never
painted over the 3D Scene viewport and do not intercept Scene-view input. Selecting a screen-space document focuses its
UI Builder document.

World-surface documents have physical width and height, pixels per unit, depth-test policy, and the owning entity's
transform. They are projected into Scene view as world content. Pointer rays are mapped into panel UV and then layout
coordinates. Render-texture documents target their configured texture rather than a viewport overlay.

## Managed Visual Trees

The managed API is under `Keire.UI`:

```csharp
using Keire.UI;

VisualElement root = new() { Name = "pause-menu" };
root.AddToClassList("menu");

Label title = new("Paused") { Name = "title" };
Button resume = new(() => ResumeGame()) { Name = "resume", Text = "Resume" };
resume.AddToClassList("primary-action");

root.Add(title);
root.Add(resume);

Button? queried = root.Q<Button>("resume");
IReadOnlyList<Button> actions = root.Query<Button>(className: "primary-action").ToList();
```

`VisualElement` owns hierarchy, classes, inline style, enablement, focus metadata, user data, and an inherited data
source. `UQueryBuilder<T>` filters by type, name, and class without exposing native renderer state.

`BindableElement` adds the authoring `BindingPath` used by fields and custom controls. A binding declared in markup or
through `SetBinding` still owns its explicit `OneWay`, `TwoWay`, or `OneTime` mode; the path property is stable authoring
metadata and does not implicitly create an ambient reflection binding.

The built-in controls are `Label`, `Image`, `Button`, `TextField`, `Toggle`, `Slider`, `ProgressBar`, `ScrollView`,
virtualized `ListView` and `TreeView`, `DropdownField`, `Foldout`, `TabView`, `Toolbar`, and `TemplateContainer`.

Scene scripts query the live source-backed tree through the entity's `UIDocument`. Returned handles are checked against
the document generation and become inert after a successful reload or destruction; a failed reload keeps the previous
valid generation alive:

```csharp
UIDocument document = Entity.GetComponent<UIDocument>()!;
RuntimeVisualElement? launch = document.Q("launch");

if (launch?.ClickedThisFrame == true)
{
    launch.Text = "Loading…";
    launch.Interactable = false;
}
```

## Events

Callbacks participate in trickle-down, target, and bubble phases:

```csharp
root.RegisterCallback<ClickEvent>(OnMenuClick, TrickleDown.TrickleDown);
resume.RegisterCallback<ClickEvent>(OnResumeClick);

private static void OnResumeClick(ClickEvent evt)
{
    evt.StopPropagation();
}
```

Pointer, keyboard, focus, submit, and `ChangeEvent<T>` values derive from `EventBase`. A callback may stop propagation,
stop immediate propagation, or prevent the control's default action. Pointer capture and focus are panel-owned and are
released when a document, panel, scene, or device generation is retired.

`PreventDefault` currently suppresses click activation, including the built-in toggle mutation. Focus assignment and
text-field editing are committed by the lower-level input owner before their notification callbacks run, so those
notifications can stop further propagation but cannot roll the already-applied value back in this release.

## Data Binding

Bindings address a property path on the nearest inherited `DataSource`:

```csharp
TextField playerName = new();
playerName.SetBinding(nameof(TextField.Value), new DataBinding
{
    SourcePath = "Profile.DisplayName",
    Mode = BindingMode.TwoWay
});

playerName.DataSource = viewModel;
```

`OneWay` updates the element, `TwoWay` also writes control changes back to the source, and `OneTime` stops observing
after the first successful value transfer. Objects implementing `INotifyPropertyChanged` refresh affected bindings.
Conversion and missing-path failures retain the previous valid target value and identify both the target property and
source path.

Native source-backed documents require an explicit `UiDocumentBindingSource`; authored paths remain visible on each
element and publish `UiBindingSourceUnavailable` until that provider is attached. The runtime never searches ambient
objects or assemblies to guess a data source. A successful document reload retains the provider, while a failed reload
keeps the prior bound document generation.

## Custom Controls

Custom controls use explicit registration. Kéire does not scan every assembly in the ambient load context:

```csharp
using Keire.UI;

[UxmlElement("HealthReadout")]
public sealed class HealthReadout : VisualElement
{
    [UxmlAttribute("caption")]
    public string Caption { get; set; } = "Health";

    [UxmlAttribute("maximum")]
    public float Maximum { get; set; } = 100.0f;
}

UxmlElementRegistry.Register<HealthReadout>();
```

Registration requires a parameterless constructor, stable element and attribute identifiers, and readable/writable
public attributed properties. Registry snapshots are immutable generation views, so failed managed reloads cannot
replace the last-good Builder/Inspector catalog.

## Virtualized Collections

`ListView` and `TreeView` create only the visible range plus bounded overscan:

```csharp
ListView inventory = new()
{
    ItemsSource = items,
    MakeItem = static () => new Label(),
    BindItem = (element, index) =>
        ((Label)element).Text = items[index].DisplayName,
    Overscan = 2
};

inventory.SetViewport(firstVisibleIndex: 120, visibleCount: 18);
```

Rebinding realizes only the bounded range rather than instantiating the complete source collection.

## Styles, Transitions, And Render Targets

Style sheets support inherited variables, responsive media conditions, bounded linear and radial gradients,
asset-backed backgrounds, per-edge borders, per-corner radii, shadow stacks, transforms, masks, nested clipping,
international typography, and pseudo-state transitions. A document may animate at most eight supported properties per
element; durations are finite, clamped to 60 seconds, and advance only through the document's explicit update. An
unchanged document performs no style, layout, or geometry recomputation after warm-up.

A render-texture panel publishes to the logical ID in its `.keireuipanel`. An `Image` consumes that output with the
separate `render-texture` markup attribute:

```xml
<Image id="3c1ed643-5424-4a6e-8689-5bd959c13187"
       name="security-feed"
       render-texture="15236dfa-afaf-437e-a6a7-88f114a671b5"/>
```

Logical render targets are not Asset Database dependencies and cannot be combined with the ordinary `image` asset
attribute. Producers are ordered before same-frame consumers; cycles fail with an actionable diagnostic. The renderer
bounds the target cache, retains one published output plus one writer per possible in-flight frame, and invalidates all
old-device target generations during recovery.

## Runtime And Recovery Ownership

Style, layout, and geometry invalidation propagates only through affected descendants and required ancestors. An
unchanged tree reuses its warm layout. Render submission captures immutable draw data, text geometry, and logical
texture/surface leases into the accepted frame; it never stores borrowed visual-element or scene pointers. Leases are
qualified by frame slot and GPU device generation. Device recovery rebuilds UI GPU resources and retries from immutable
CPU data, so a pre-loss texture or render-target handle cannot be consumed by the recovered frame.

## Current Limits

Imported `.ttf`, `.otf`, and `.ttc` faces and `.keirefont` families use FreeType, HarfBuzz, FriBidi, and libunibreak
for rasterization, shaping, bidirectional ordering, and Unicode line breaking. Font/layout caches are bounded and
generation keyed; the printable-ASCII atlas remains the deterministic fallback when no family is assigned or a face is
unavailable. A font family evaluates its ordered fallback families when the primary face has missing glyphs, choosing
the single face that provides the best complete-command coverage. Per-glyph mixed-face runs, multi-page glyph-atlas
packing, localization-database authoring, color emoji, and platform screen-reader adapters remain follow-up work.
Images use individual immutable frame leases rather than a shared image atlas, so the Debugger reports image-atlas
occupancy as zero.

UI Toolkit authors project content only; the Kéire Editor shell remains on its existing immediate-mode UI. Kéire does
not import Unity UXML/USS files, and the clean break deliberately provides no automatic Canvas/Rect Transform
converter.

The Project panel creates `.keireui` documents, `.keirestyle` style sheets, and `.keirefont` families directly. Style
Studio opens an existing linked `.keirestyle`. Panel Settings assets must still come from a starter/example asset or
another valid project-source workflow; preview controls are not a substitute for persistent Panel Settings authoring.

## Legacy Scene UI

Canvas, Rect Transform, and the old scene UI control component IDs are permanently reserved and are not available for
new authoring. Importing a scene that contains one fails before project state changes with the exact entity name, entity
ID, component name, and component ID. Recreate that UI as a `.keireui` document referenced by `UIDocument`; there is no
automatic converter or permanent dual runtime.

Persistent `KeireEvent` fields and cooperative `Cursor.RequestVisible()` / `Cursor.RequestCapture()` tokens remain
general scripting APIs and may be used by UI Toolkit controllers exactly as they are used by gameplay systems.
