# Visual Style Studio And Style Runtime v2

Kéire's Visual Style Studio is the visual-first stylesheet workspace inside UI Builder. It edits `.keirestyle`
assets without forcing designers to author declaration text, while retaining a synchronized source view for precise
or advanced work. UI documents and style sheets keep independent undo histories and are written only by an explicit
**Save**.

## Open The Workspace

Open a `.keireui` document, link a **UI Style Sheet** from the typed asset picker, and select **Styles** in the UI
Builder mode bar. The workspace is arranged around three jobs:

- The left side selects linked sheets, searches and reorders rules, manages design tokens, and creates or duplicates
  selectors.
- The center keeps the live device canvas visible. Resolution, orientation, DPI, safe area, pointer precision,
  navigation mode, reduced-motion preference, and pseudo-state previews can be changed without rewriting the panel
  asset.
- The right side switches between **Properties**, **Computed**, and **Source**. Properties authors declarations;
  Computed explains the winning cascade; Source edits the same lossless draft directly.

The element Inspector and stylesheet rule editor use the same property registry. Every property has one canonical
name, value kind, default, inheritance rule, animation capability, schema requirement, and validation path across
import, runtime, managed bindings, completion, and the visual controls.

## Selectors And Responsive Conditions

The selector builder combines an element type, `#name`, `.classes`, parent relationship, and the `:hover`, `:active`,
`:focus`, `:disabled`, and `:checked` states. Advanced source remains available for compound selector work. Matching
rules are ordered by specificity and source order; the Computed view shows which declarations won and which were
overridden.

Schema v2 adds responsive conditions:

```css
@keire-style 2;

:root {
  --surface: #101827ff;
  --accent: #4f8cffff;
  --space-m: 16px;
}

.toolbar {
  background-color: var(--surface);
  padding: var(--space-m);
}

@media (max-width: 900px) and (orientation: portrait) and (pointer: coarse) {
  .toolbar {
    flex-direction: column;
  }
}
```

Supported conditions cover minimum/maximum width, height, aspect ratio, and DPI; portrait/landscape orientation;
coarse/fine pointer precision; primary navigation mode; and reduced-motion preference. Active responsive rules keep
ordinary selector specificity and source-order semantics.

## Visual Properties And Provenance

Properties are grouped into layout, size, flex, spacing, position, typography, background, border, effects,
transform, clipping, transition, and accessibility sections. Numeric controls keep their units, asset properties use
typed project pickers, keyword properties use constrained choices, and every authored value can be reset to the
cascade. Each row identifies whether its current value is a default, a stylesheet rule, or an inline override.

Design tokens are searchable by name or value, report their current usage count, and show a color editor whenever the
value is a hexadecimal color. The background editor provides visual two-stop linear and radial gradient controls for
angle or center/radius, stop colors, and stop positions. Multi-stop and other advanced gradient expressions remain
editable in Source and are preserved rather than simplified by the visual editor.

Renaming a token across the project is a previewed transaction. **Preview Project Rename** scans only imported UI
documents and style sheets under the project Assets root, shows every affected file, line, column, and source line,
and requires an explicit confirmation. Apply rechecks every baseline byte before writing anything; a file changed
after preview aborts the whole operation. Each replacement candidate is parsed and validated before preview, writes
are atomic, and a partial write failure rolls back files already changed.

Common workflows are available beside the selected element or rule:

- create a class and assign it to the selected element;
- create a matching rule or a hover/active/focus/disabled/checked variant;
- promote a declaration to a `--design-token` and replace the local value with `var(...)`;
- move an inline override into a reusable class rule;
- duplicate and reorder complete rules.

Continuous numeric edits share a property-scoped undo operation. Source, rule, token, and inline edits stay in the
history of the asset they modify.

## Drafts, Source, And Conflicts

Visual and source edits modify the same in-memory draft. Source parsing is debounced; a valid candidate publishes as a
development-only asset revision so UI Builder, Game View, and Play Mode update without writing the source file. An
invalid candidate reports a line and column, disables Save, and leaves the last valid preview active.

Source provides line/column and syntax-token status, property and pseudo-state completion sourced from the runtime
property registry, click-to-insert suggestions, matching-brace locations, property hover documentation, rule
navigation, case-aware find/replace, and deterministic formatting. Tabs are retained as editor input rather than
changing focus. All valid source operations enter the style document's undo history and publish the same live draft
as visual controls.

Comments and untouched formatting are retained when a visual edit can be represented as a local syntax-tree rewrite.
Save compares the source file with the baseline captured when it was opened. If another program changed the file,
Kéire refuses to overwrite it. **Compare External** shows a bounded local-versus-disk comparison and **Reload
External** replaces the draft only when the author chooses it. Reloading, discarding, switching documents, or closing
the workspace removes unpublished revisions and restores the imported baseline.

**Save Draft As...** is available during a conflict and **Save As...** is always available from Source. The target
must be a new `.keirestyle` file inside the project's ordinary, non-linked Assets tree. Save As never overwrites the
externally changed original; it atomically writes the validated draft as a new asset and schedules import.

## v1 Compatibility And v2 Upgrade

Schema v1 assets remain readable and keep their original source. Editing a v1-compatible property does not upgrade
the file. The first v2-only declaration or responsive condition advances the draft to `@keire-style 2;`; the upgrade
is one-way and Kéire never writes a lower schema or importer version.

V2-only styling includes imported font families, per-edge borders, per-corner radii, bounded shadow stacks,
background textures and nine-slice, alpha masks, transforms, transition delays/easing, production text layout, and
responsive rules. Asset references use `asset(<stable-id>)` in source but display project names and paths in visual
pickers.

## Fonts And International Text

Import `.ttf`, `.otf`, or `.ttc` files as UI Font Face assets. A `.keirefont` family maps weights and normal, italic,
or oblique styles to those faces and records ordered fallback families. Reference the family from `font-family`.

Runtime text uses pinned FreeType rasterization, HarfBuzz shaping, FriBidi direction resolution, and libunibreak line
breaking. Layout-cache keys include font generation, UTF-8 text, language, direction, style, and available width.
Glyph atlas and layout caches are bounded and deterministic. Immutable frame leases carry CPU-recoverable font data;
device recovery discards old GPU pages and recreates them for the retried frame without consuming an old-device atlas.
When the selected face cannot cover a code point, shaping splits the bidi run at the exact fallback-face boundary and
uses the first ordered family face that contains that glyph. A single label can therefore produce deterministic
mixed-font runs without being split by the game. Glyphs are packed into up to eight deterministic 1024-by-2048 pages
per face; geometry batches select the exact face/page binding, and every page keeps an immutable frame-slot and device-
generation-qualified lease. Page overflow is an actionable bounded error rather than unbounded atlas growth.

Typography declarations include family, weight, style, size, line height, letter and word spacing, wrapping, maximum
lines, clipping or ellipsis, language, direction, and horizontal/vertical alignment. Color emoji and platform screen
reader adapters remain outside this milestone.

Text shaping selects fallback faces per glyph while preserving contiguous visual-run order. Each face may use up to
eight deterministic atlas pages, while the renderer keeps the total cache within a 32-page working set and refuses an
active frame whose fonts cannot fit that bound. Every glyph references a page-qualified frame and device-generation
lease, so recovery cannot consume stale atlas data.

## Performance Contract

An unchanged visual tree performs no style, layout, or geometry recomputation after warm-up. Token and media changes
recascade only affected elements. Virtualized collections retain their visible-row and bounded-overscan contract.
Style and layout timings, dirty reasons, batch counts, vertices, focus, pointer capture, selector precedence, and atlas
usage remain visible in UI Builder's Debug mode.
