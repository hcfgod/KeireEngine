# UI Workspace Guide

`UiWorkspace` is Kéire's application-owned editor workspace service. It provides stable panel registration, factory
docking, named layouts, semantic themes, portable profile files, and user-local persistence without exposing Dear
ImGui, SDL, or JSON through the public API.

Use the workspace for editor-style applications that need multiple persistent panels. A simpler client may continue
using `UiSpecification::LayoutPath` for one legacy backend layout file, but the two persistence modes are mutually
exclusive.

`UiFrame::WindowFocused()` includes child windows and popup descendants, preserving the existing panel-focus
contract. The additive `WindowFocused(false)` overload excludes popup descendants while retaining ordinary child
windows. Modal keyboard handlers can use it to defer Enter/Escape to a child selector. Require focus on both the
previous and current frame when dismissing or submitting a parent modal, so a selector's closing key does not also
activate the newly refocused parent.

## Settings Sections

With input trickling enabled, a pointer move followed immediately by a press is processed across frames:
the movement establishes hover first, then the queued press and release retain their order. This lets tabs
and overlapping controls respond to a single quick click, including on a slow-rendering frame. It adds
one frame before the press when movement and pressing arrive in the same event batch. Focus loss still
cancels input immediately; explicitly disabling input trickling retains immediate processing.

For HDR radiance authoring, use `UiFrame::ColorEditHdrRgb` with `std::array<float, 3>` to provide an RGB picker
without an opacity field. Values may exceed one and must be finite; invalid edits leave the caller's array unchanged.
The existing `ColorEditHdr` retains its RGBA controls. Display/theme colors still use the normalized `ColorEdit`.

Use `UiFrame::BeginSection(label)` for collapsible settings groups. A single click on the heading or unused row
space toggles the group; the returned scope balances indentation and IDs when expanded. Reuse a stable label or
`##` identity suffix to preserve expansion state. This does not select an object or change settings values.
`BeginTreeNode` reserves label clicks for the caller's selection handling; the arrow or a double-click toggles expansion.

## Responsive Checkbox Rows

The existing `Checkbox(label, value)` starts a normal new item row. For a compact toolbar, opt into continuing the
previous row only when the complete checkbox and its visible label fit:

```cpp
bool maximizeOnPlay = false;
bool showFps = false;
(void)ui.Checkbox("Maximize On Play", maximizeOnPlay);
(void)ui.Checkbox("FPS", showFps, {.ContinueRow = true});
```

`ContinueRow` uses the active font, theme frame padding, item spacing, and current window/child/table content boundary.
When the next control does not fit, it keeps normal next-line placement; it neither truncates the label nor changes
the value. A `##` identity suffix does not contribute visible width. Like any single control, a label wider than the
entire content region still requires a wider region or a shorter label. Use this after a preceding item; an earlier
`SameLine()` request does not prevent overflow wrapping. No object-size changes are required for existing clients.

## Tooltip Readability

`UiFrame::SetTooltip` wraps long help text at a font-relative preferred width, capped by the current viewport's usable
width after theme padding and safe-area margins. Existing immediate and delayed hover behavior is unchanged. This
keeps long explanations readable on narrow windows without requiring each panel to insert manual line breaks.

## Enable The Workspace

Configure the workspace before constructing the application runtime:

```cpp
Keire::ApplicationSpecification specification;
specification.Ui.Mode = Keire::UiMode::Rendered;
specification.Ui.Workspace.Enabled = true;
specification.Ui.Workspace.BuildFactoryLayout = [](Keire::UiLayoutBuilder& layout)
{
    const auto left = layout.Split(layout.Root(), Keire::UiDockDirection::Left, 0.20F);
    const auto bottom = layout.Split(left.Far, Keire::UiDockDirection::Down, 0.25F);

    layout.Dock("editor.hierarchy", left.Near);
    layout.Dock("editor.console", bottom.Near);
    layout.Dock("editor.scene", bottom.Far);
};
```

`BuildFactoryLayout` is a backend-independent recipe. `Root()` returns the initial dock region. `Split()` consumes one
live region and returns `Near` in the requested direction plus `Far` for the remaining space. Ratios must be finite and
between `0.05` and `0.95`. A consumed region cannot be split or docked again.

The callback is invoked when the immutable Default layout is first needed or explicitly reset. It must be deterministic
and must not depend on frame-local state.

Set `Workspace.Ephemeral` for smoke runs that must not read or write user preferences. Set `DirectoryOverride` only for
deterministic tests or tools that deliberately own an alternate profile directory.

## Register And Draw Panels

Register panels during layer attachment and retain each move-only registration for as long as the panel exists:

```cpp
class EditorLayer final : public Keire::Layer
{
  public:
    EditorLayer() : Layer("EditorLayer") {}

  protected:
    void OnAttach() override
    {
        m_Inspector = Owner().GetUiWorkspace().RegisterPanel(
            {"editor.inspector", "Inspector", true});
    }

    void OnUi(Keire::UiFrame& ui) override
    {
        if (auto panel = ui.BeginPanel(m_Inspector); panel)
        {
            ui.Text("Nothing selected");
        }
    }

  private:
    Keire::UiPanelRegistration m_Inspector;
};
```

The panel ID is persisted and must remain stable across releases. It is not a display label, localization key, pointer,
or generated value. Changing it intentionally creates a new persistence identity. The title may change without losing
the saved panel state.

Destroying or replacing the registration unregisters the panel. Unknown panel visibility entries remain in a layout so
temporarily unavailable plug-ins or feature panels can recover their state when registered again. Registration,
visibility changes, workspace queries, and profile mutations are UI-owner-thread operations.

Use `SetVisible()` for Window-menu toggles. `BeginPanel()` owns the close button and records visibility changes. Do not
also submit the same panel through `BeginWindow()`.

Every registered panel receives a common lock control. `SetLocked()` and `Locked()` expose the same owner-thread state
to custom panels. A locked view cannot be moved, resized, or collapsed; selection-driven panels should also retain
their current document, entity, or asset context until unlocked. Lock state is intentionally session-local so stale
objects are never restored across project launches.

## Layout Workflow

### Reveal A Tab From Another View

Panel visibility, panel focus, and the selected tab are separate states. When an action such as **Edit Payload**
targets controls in another tab, first select the target object, make its panel visible with `SetVisible(true)`, and
request panel focus with `RequestFocus()` if it is in another panel. Retain a one-time tab-selection request on the
owning controller; consume it when drawing the destination tab, not when the panel is hidden or collapsed.

Use `UiFrame::BeginTabItem(label, open, select)` with `select` true for that request. Submit false on later frames so
the user can navigate away. The existing `BeginTabItem(label, open)` overload is unchanged and does not force
selection. Keep each tab's scope inside its own block: close the first tab scope before opening the next one.
Only the visible tab should draw its contents. This is navigation, not an asset edit or an undo command.

The VFX graph's **Edit Payload** action uses this mechanism to select its module and reveal **Runtime Modules**.
It does not convert the effect or change its execution source.

### Save And Restore Layouts

`Layouts()` returns snapshots suitable for menus. Each entry provides its strong ID, name, built-in status, active
status, and whether Default has live modifications.

- Selecting a layout calls `LoadLayout(id)` and applies it at a safe frame boundary.
- `SaveLayoutAs(name)` creates and activates a named custom layout from the current live workspace.
- Custom layouts may be renamed or deleted. Built-in layouts are immutable.
- `ResetFactoryLayout()` selects Default, restores default panel visibility, and reapplies the factory dock recipe.
- Live docking and panel visibility changes autosave. There is intentionally no manual "save current layout" step.
- Dock split ratios remain stable when the host transitions between fullscreen, maximized, and windowed sizes. The
  workspace proportionally rescales the dock tree before rendering instead of allowing the central panel to absorb
  the entire size change.
- `.keirelayout` files provide explicit portable import and export. Native dialog helpers are available only in
  rendered mode; path-based methods also work in headless tools and tests.

Profile names are exact and case-sensitive. They must contain 1–64 UTF-8 bytes, cannot begin or end with an ASCII
space, and cannot contain control characters or path separators.

## Theme Workflow

Editable field backgrounds use the theme's raised-panel surface instead of the child-panel surface. Hovering uses
the hover accent; active editing retains the selection color. Custom themes should distinguish these surfaces so
numeric fields remain discoverable without moving the pointer over them.

Kéire Dark, Kéire Light, and Classic are immutable built-in themes. Discover them and all custom themes through
`Themes()`; do not persist numeric IDs outside the workspace catalog.

The supported theme contract is `UiThemeDefinition`. It contains semantic colors for surfaces, text, interaction
states, selection, and status feedback, plus spacing, borders, and rounding. The private runtime maps those tokens to
the backend style. Backend color indices are intentionally not public.

A professional theme editor should use this transaction:

1. Read the selected theme with `ThemeDefinition(id)` into a working copy.
2. Update the working copy locally and call `PreviewTheme()` for live feedback.
3. Persist a custom theme with `UpdateTheme()` or create one with `SaveThemeAs()`.
4. Call `CancelThemePreview()` when edits are discarded.
5. Before switching themes or closing a dirty editor, present Save, Save As, Discard, and Cancel as appropriate.

`ApplyTheme(id)` changes the active saved theme; it does not implicitly save a preview. Built-in themes cannot be
updated, renamed, or deleted. Portable custom themes use the `.keiretheme` extension.

All color components must be finite values in `0..1`. Padding and spacing accept `0..32`, rounding accepts `0..24`,
and border sizes accept `0..4`.

## Storage And Recovery

Normal workspaces live below the platform directory returned by
`SDL_GetPrefPath(ProjectName, ProjectName)/Editor/Workspace`:

```text
Workspace/
|-- catalog.json
|-- session.keirelayout
|-- Layouts/
|   `-- <layout-id>.keirelayout
`-- Themes/
    `-- <theme-id>.keiretheme
```

`catalog.json` records profile identities and the active selections. `session.keirelayout` restores the most recent
live workspace, including unsaved modifications to Default. Custom profile files are versioned portable documents.

Documents are limited to 1 MiB. Parsing rejects duplicate or unknown fields, invalid types and values, unsafe names,
and unsupported schema identities. Writes use a temporary document and recoverable backup replacement. Do not edit
the user-local files while the application is running; use import and export for deliberate external changes.

Recoverable startup problems fall back to built-ins and appear through `ConsumeNotice()`. Menu layers should drain and
display these notices. Direct API misuse and explicit file-operation failures throw exceptions so the caller can keep
the current profile active and report the diagnostic.

Native file dialogs are asynchronous. Their callback only copies the result into a synchronized mailbox; imports and
exports execute later on the UI owner thread. A late callback after shutdown is inert.

## Troubleshooting

### A panel does not return to its saved location

Confirm that registration happens before the first UI frame and that the panel uses the same stable ID in registration,
the factory recipe, and every release. Reset Default after intentionally changing the factory recipe.

### Panels become narrow after leaving fullscreen

Current workspace layouts are normalized to the host work area whenever its size changes, including when a saved
layout was captured at a different resolution. If an older session already contains a collapsed panel, reset Default
once to restore the factory ratios; subsequent fullscreen and windowed transitions preserve those proportions.

### A preset does not switch after editing a theme

The editor must resolve the dirty working copy first. KéireClient prompts for Save, Save As, Discard, or Cancel and
defers that modal until the preset combo or menu has closed. Custom clients should use the same safe-boundary pattern.

### Native import or export fails in a test

Headless mode intentionally has no native dialog owner. Call the path-based import or export function with a temporary
directory instead.

### Application startup rejects the UI specification

Clear `UiSpecification::LayoutPath` when enabling `UiSpecification::Workspace`. Only one layout persistence owner may
be active.

## Deliberate Boundaries

The workspace does not expose Dear ImGui types, renderer resources, SDL events, native handles, or JSON objects. It
does not enable multi-viewports, create an input-routing policy, serialize engine scenes, discover assets, or define a
plug-in system. Those systems require their own ownership and lifecycle designs.


Text padding applies to the control content rectangle used for alignment and wrapping. Container padding spaces its children; control padding spaces its own text. Insets scale with the panel, automatic sizing includes them, and rotating an inset label retains the original element pivot. If padding consumes the entire content rectangle, no text is emitted.

### Small runtime UI text

UI font atlases include three filtered smaller levels with glyph padding, reducing missing strokes when a panel is
scaled down. This applies to the embedded fallback font and imported font faces. Extremely small text still loses
readability because the display has too few pixels: validate menus at their smallest supported viewport, use a
comfortable physical text size, and provide text scaling. Do not use a tiny Game view as the final legibility target.

## Pending grid authoring validation

The next source batch adds Show grid, Grid opacity, Grid fade distance (metres), and Reset grid to the Scene toolbar
settings popup. Preferences are stored in local SceneTools state version 5; older versions use a visible grid with
0.5 opacity and a 200 m fade distance. Grid lines and axes fade together over the outer half of that distance.
These controls affect the editor overlay, not scene lighting or packaged content. Zero opacity skips the grid draw.
The updated shader compiles as DXIL, SPIR-V and MSL. The October 28 combined DevDist build and staging passed.
In the maximized editor, Show grid removed both lines and axes, and Reset grid restored visibility and defaults.
Persistence/migration coverage has been extended; linked persistence tests, slider appearance checks and restart
acceptance remain pending.

Grid fade validation: the shared shader visibility function is exercised by
`Scripts/Tests/GridAppearanceProbe.hlsl` through `test-atmosphere-medium-gpu.py --suite grid`.
D3D12 and Vulkan each passed 384,000 opacity/range samples; DXIL, SPIR-V and MSL compilation passed.
These numerical checks cover opacity clamping and near/midpoint/far fading, not native viewport appearance.
