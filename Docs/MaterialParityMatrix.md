# Material Ecosystem Parity Matrix

Review date: 2026-08-10
Comparison model: Unreal-inspired material workflow, adapted to Kéire's renderer-neutral asset and runtime boundaries

This matrix is the acceptance authority for the Material Ecosystem initiative. `Complete` means the capability has an
implemented engine/editor path and focused evidence. `Partial` means the shipped subset rejects unsupported behavior
instead of silently degrading. `Planned` is not a supported feature. Counts are useful for planning, but production
scenarios and executable validation decide milestone acceptance.

## Capability Matrix

| ID | Capability | Status | Priority | Evidence or remaining gate |
| --- | --- | --- | --- | --- |
| ME-AS-001 | Distinct Shader Graph asset | Complete | P0 | `.keireshadergraph`, importer, editor, generated variants, and tests. |
| ME-AS-002 | Distinct Material Graph asset | Complete | P0 | `.keirematerialgraph`, Material Output composition, importer, editor, and tests. |
| ME-AS-003 | Direct Material asset | Complete | P0 | Inspector-authored `.keirematerial` remains assignable and shader-backed. |
| ME-AS-004 | Material Instance asset | Complete | P0 | Versioned inheritance, static/dynamic overrides, bounded ancestry, and tests. |
| ME-AS-005 | Material Function asset | Complete | P0 | `.keirematerialfunction`, typed body, importer/decoder, editor create/open/save. |
| ME-AS-006 | Shader Function asset | Complete | P1 | `.keireshaderfunction`, typed body, importer/decoder, editor create/open/save. |
| ME-AS-007 | Material Layer asset | Complete | P0 | `.keiremateriallayer` with Material Attributes output. |
| ME-AS-008 | Material Layer Blend asset | Complete | P0 | `.keirematerialblend` with Bottom, Top, and Alpha interface. |
| ME-AS-009 | Material Parameter Collection asset | Complete | P0 | `.keirematerialcollection`, deterministic source/cooked data, importer/decoder. |
| ME-AS-010 | Safe rename, duplicate, move, and trash | Complete | P0 | Generic stable-ID asset operations cover every new extension. |
| ME-AS-011 | Deterministic source serialization | Complete | P0 | Canonical JSON and byte-equality tests for functions and collections. |
| ME-AS-012 | Bounded source decoding | Complete | P0 | Explicit size, count, text, schema, identifier, and finite-value limits. |
| ME-AS-013 | Future-schema rejection | Complete | P0 | Shader schema 3 and reusable schema 1 reject unsupported future data. |
| ME-AS-014 | Import dependency extraction | Complete | P0 | Function calls publish sorted unique asset dependencies. |
| ME-AS-015 | Dependent reimport after function change | Complete | P0 | Asset dependency graph invalidates Shader/Material Graph imports. |
| ME-AS-016 | Transactional generated shader publication | Complete | P0 | Staged replacement and rollback retain last-good variants. |
| ME-AS-017 | Function metadata preservation | Complete | P1 | Reusable document saves preserve description, category, priority, and library visibility. |
| ME-AS-018 | Function metadata editing UI | Planned | P1 | Add dedicated Library metadata controls and undo coverage. |
| ME-AS-019 | Redirectors after asset moves | Planned | P1 | Requires explicit redirector asset and cook policy. |
| ME-AS-020 | Source-control-aware checkout/status | Planned | P2 | Requires provider-neutral source-control boundary. |
| ME-GR-001 | Typed function call node | Complete | P0 | Interface derives from stable Parameter and Output pins. |
| ME-GR-002 | Calls in Shader Graph | Complete | P0 | Search palette lists compatible project functions and layers. |
| ME-GR-003 | Calls in Material Graph | Complete | P0 | Material expressions use the same call contract. |
| ME-GR-004 | Deterministic recursive expansion | Complete | P0 | Derived IDs make repeated expansion byte-identical. |
| ME-GR-005 | Missing function diagnostics | Complete | P0 | Expansion rejects unresolved assets before shader publication. |
| ME-GR-006 | Function recursion rejection | Complete | P0 | Dependency stack rejects direct and indirect cycles. |
| ME-GR-007 | Expansion depth budget | Complete | P0 | Configurable depth cap defaults to 32. |
| ME-GR-008 | Stale call-interface rejection | Complete | P0 | Typed pin mapping fails recoverably when an interface no longer matches. |
| ME-GR-009 | Function input defaults | Complete | P0 | Unconnected call inputs substitute the authored Parameter default. |
| ME-GR-010 | Multiple function outputs | Complete | P1 | Output node inputs become independent typed call outputs. |
| ME-GR-011 | Material Attributes function IO | Complete | P0 | Structured attributes are valid function and layer interfaces. |
| ME-GR-012 | BSDF function IO | Complete | P1 | Structured BSDF parameters are valid in reusable bodies. |
| ME-GR-013 | Function preview in parent graph | Partial | P1 | Expanded shader preview works; reusable-body standalone preview is intentionally absent. |
| ME-GR-014 | Promote selection to function | Planned | P1 | Needs transactional graph extraction and cable rewiring. |
| ME-GR-015 | Inline function command | Planned | P2 | Needs editor placement policy for expanded nodes. |
| ME-GR-016 | Function-library favorites/recent | Partial | P2 | Base node palette has recent/common; dynamic asset calls do not yet persist recents. |
| ME-GR-017 | Reroute node | Complete | P1 | Typed pass-through node compiles and previews. |
| ME-GR-018 | If node | Complete | P0 | Thresholded three-way selection with typed results. |
| ME-GR-019 | Compare node | Complete | P0 | Equal, greater, and less scalar outputs. |
| ME-GR-020 | Boolean logic nodes | Complete | P0 | And, Or, and Not lower to portable scalar truth values. |
| ME-GR-021 | Unit conversion nodes | Complete | P1 | Degrees/Radians conversion compiles and previews. |
| ME-GR-022 | General exponent/log nodes | Complete | P1 | Natural exponential/logarithm with contained domains. |
| ME-GR-023 | Hyperbolic math nodes | Complete | P2 | Sinh, cosh, and tanh use portable HLSL intrinsics. |
| ME-GR-024 | Scale and Bias node | Complete | P1 | Typed input with scalar scale and bias. |
| ME-GR-025 | Node comments | Planned | P1 | Requires serialized comment regions and canvas interaction. |
| ME-GR-026 | Named reroute declarations/usages | Planned | P2 | Requires cross-canvas name resolution. |
| ME-GR-027 | Collapsible graph regions | Planned | P2 | Requires persistent region ownership and navigation. |
| ME-GR-028 | Graph bookmarks | Planned | P2 | Requires workspace-scoped navigation state. |
| ME-GR-029 | Copy/paste across assets | Planned | P1 | Requires collision-safe stable-ID remapping and dependency transfer. |
| ME-GR-030 | Diff/merge visualization | Planned | P1 | Requires semantic graph diff independent of JSON ordering. |
| ME-MA-001 | Material Output node | Complete | P0 | Owns surface contract and remains distinct from Shader Output. |
| ME-MA-002 | Shader Graph template selection | Complete | P0 | Material Graph composes expressions through an explicit template. |
| ME-MA-003 | Raw Shader compatibility workflow | Complete | P1 | Direct/compatibility material values remain supported. |
| ME-MA-004 | Dynamic exposed parameters | Complete | P0 | Parameter nodes become instance-editable material properties. |
| ME-MA-005 | Static switch parameters | Complete | P0 | Bounded deterministic shader variants and instance overrides. |
| ME-MA-006 | Material Attributes make/break/blend | Complete | P0 | Structured layer composition compiles through the normal graph path. |
| ME-MA-007 | Standard Surface BSDF | Complete | P0 | Portable lit surface closure. |
| ME-MA-008 | Clear Coat BSDF | Complete | P0 | Weight and roughness modify a typed BSDF. |
| ME-MA-009 | Sheen BSDF | Complete | P1 | Color, weight, and roughness modify a typed BSDF. |
| ME-MA-010 | Subsurface BSDF | Complete | P1 | Color and weight publish bounded subsurface attributes. |
| ME-MA-011 | Transmission BSDF | Complete | P1 | Weight, IOR, refraction, and thickness publish typed attributes. |
| ME-MA-012 | Surface Stack multi-closure evaluation | Partial | P0 | Layered attributes and BSDF modifiers ship; multi-lobe energy evaluation remains. |
| ME-MA-013 | Material Layer call workflow | Complete | P0 | Layer functions appear in both graph palettes. |
| ME-MA-014 | Layer Blend call workflow | Complete | P0 | Blend function exposes Bottom, Top, and Alpha. |
| ME-MA-015 | Per-instance layer selection | Planned | P1 | Requires layer-stack instance schema and static permutation policy. |
| ME-MA-016 | Material quality levels | Planned | P1 | Requires project quality policy and variant pruning. |
| ME-MA-017 | Material parameter associations | Planned | P1 | Required for layer/function parameter identity across instances. |
| ME-MA-018 | Parameter groups and sort priority | Complete | P1 | Stable metadata drives editor ordering. |
| ME-MA-019 | Parameter ranges and steps | Complete | P1 | Optional finite metadata is reflected and validated. |
| ME-MA-020 | Parameter rename stability | Complete | P0 | Stable property IDs preserve compatibility bindings. |
| ME-RT-001 | Immutable runtime MaterialAsset | Complete | P0 | All authoring workflows converge before RenderSystem. |
| ME-RT-002 | Persistent Material Instance resolution | Complete | P0 | Bounded parent resolution publishes one runtime material. |
| ME-RT-003 | Dynamic Material Instance snapshots | Complete | P0 | Thread-safe typed overrides, resets, revisions, and close. |
| ME-RT-004 | Dynamic instance render upload | Planned | P0 | Needs renderer-owned coalesced upload and lifetime fencing. |
| ME-RT-005 | Parameter Collection runtime snapshots | Complete | P0 | Stable-ID values, typed mutation, reset, revision, and close. |
| ME-RT-006 | Renderer-wide collection buffer | Planned | P0 | Needs renderer ABI, dirty-range upload, and per-frame binding. |
| ME-RT-007 | World-scoped collection state | Planned | P0 | Needs scene/runtime ownership and Play Mode isolation. |
| ME-RT-008 | Collection access nodes | Planned | P0 | Must bind stable collection/parameter IDs through the renderer ABI. |
| ME-RT-009 | Managed collection API | Planned | P1 | Requires generated C# value contracts and reload-safe handles. |
| ME-RT-010 | Managed dynamic instance API | Planned | P1 | Requires lifetime-safe managed wrappers and render handoff. |
| ME-RT-011 | Material render proxies | Planned | P0 | Needed for scalable immutable-parent plus override binding. |
| ME-RT-012 | Uniform-expression cache | Planned | P0 | Requires dependency-aware per-world cache invalidation. |
| ME-RT-013 | Pipeline state sorting | Complete | P0 | Opaque/masked/transparent submissions preserve deterministic policy. |
| ME-RT-014 | Transparent sorting | Complete | P0 | Back-to-front view-depth order is tested. |
| ME-RT-015 | Material hot reload | Complete | P0 | Development assets update without replacing last-good on failure. |
| ME-ED-001 | Separate Shader and Material Graph documents | Complete | P0 | Double-click routing and docked panels remain distinct. |
| ME-ED-002 | Reusable graph document mode | Complete | P0 | Purpose-aware validation and save do not generate standalone shaders. |
| ME-ED-003 | Search-first node palette | Complete | P0 | Toolbar/context search share the stable catalog. |
| ME-ED-004 | Function/layer asset palette | Complete | P0 | Project assets appear under Functions & Layers. |
| ME-ED-005 | Material Parameter Collection Inspector | Complete | P0 | Add/edit/remove, explicit save/revert, type/default controls. |
| ME-ED-006 | Live Shader Graph preview | Complete | P0 | Last-good sphere/plane/cube/custom mesh preview. |
| ME-ED-007 | Material live scene preview | Complete | P0 | Draft runtime material is published to assigned scene objects. |
| ME-ED-008 | Reusable body preview | Planned | P1 | Needs caller-supplied preview values and output visualization. |
| ME-ED-009 | Per-node preview tiles | Planned | P1 | Needs selective compilation and bounded thumbnail scheduling. |
| ME-ED-010 | Node/pin diagnostic focus | Partial | P1 | IDs are reported; canvas focus/navigation remains. |
| ME-ED-011 | Undo/redo graph topology | Complete | P0 | Bounded document contexts cover nodes, cables, values, and metadata. |
| ME-ED-012 | Collection undo/redo | Planned | P1 | Current editor uses explicit save/revert. |
| ME-ED-013 | Last-good compile recovery | Complete | P0 | Failed drafts do not replace preview or runtime shader variants. |
| ME-ED-014 | Background generation checks | Complete | P0 | Superseded compilation results are discarded. |
| ME-ED-015 | Compile statistics | Complete | P1 | Active nodes, textures, ALU estimate, and variants. |
| ME-ED-016 | Platform compiler diagnostics | Complete | P0 | Importer diagnostics retain generated-line context. |
| ME-ED-017 | Shader complexity visualization | Planned | P1 | Needs renderer instrumentation and viewport overlay. |
| ME-ED-018 | Material analyzer | Planned | P0 | Needs enforceable instruction/resource/variant budget policy. |
| ME-ED-019 | Usage/reference viewer | Planned | P1 | Requires asset dependency query UI. |
| ME-ED-020 | Batch recompile dashboard | Planned | P1 | Requires cancellable asset-operation aggregation. |
| ME-RS-001 | Texture2D parameters and sampling | Complete | P0 | Reflected resource ABI and texture semantics. |
| ME-RS-002 | Explicit sampler states | Planned | P0 | Requires portable sampler asset/value contract. |
| ME-RS-003 | Texture arrays/cubes/3D textures | Planned | P0 | Requires asset types, reflection, preview, and backend coverage. |
| ME-RS-004 | Structured/byte-address buffers | Planned | P1 | Requires security and size budgets in authoring/runtime. |
| ME-RS-005 | Virtual textures | Planned | P2 | Requires streaming system and feedback integration. |
| ME-RS-006 | Decal output | Complete | P1 | Dedicated template/output contract. |
| ME-RS-007 | Fullscreen output | Complete | P1 | Dedicated template/output contract. |
| ME-RS-008 | Hair output | Complete | P1 | Dedicated template/output contract. |
| ME-RS-009 | Eye output | Complete | P1 | Dedicated template/output contract. |
| ME-RS-010 | Transparent output | Complete | P0 | Premultiplied output and renderer state. |
| ME-RS-011 | Additional blend modes | Complete | P0 | Additive, modulate, premultiplied alpha-composite, and alpha-holdout use explicit color/alpha factors, transparent sorting, and disabled depth writes with focused policy and batching tests. |
| ME-RS-012 | Two-sided shading controls | Complete | P0 | Material surface state is serialized and renderer-consumed. |
| ME-RS-013 | World-position offset | Complete | P0 | Vertex-stage validation and generated ABI. |
| ME-RS-014 | Pixel-depth offset | Complete | P1 | Fragment depth output and stage validation. |
| ME-RS-015 | Tessellation/displacement pipeline | Planned | P2 | Depends on renderer backend strategy. |
| ME-RS-016 | Ray tracing material path | Planned | P2 | Depends on explicit cross-platform RT milestone. |
| ME-RS-017 | Path-tracing material path | Planned | P2 | Depends on offline renderer milestone. |
| ME-RS-018 | Mobile material tier | Planned | P0 | Requires explicit resource/instruction and feature policy. |
| ME-RS-019 | Platform-specific variant pruning | Partial | P0 | Target formats cook selectively; feature-level pruning remains. |
| ME-RS-020 | Stable renderer ABI versioning | Complete | P0 | Generator and vertex-layout versions are embedded and tested. |
| ME-PF-001 | Node/connection hard limits | Complete | P0 | 1,024 nodes and 4,096 connections by default. |
| ME-PF-002 | Texture resource hard limits | Complete | P0 | Portable material texture budget is validated. |
| ME-PF-003 | Keyword/variant hard limits | Complete | P0 | Deterministic maximum and explicit diagnostic. |
| ME-PF-004 | Compile cancellation/supersession | Complete | P0 | Generation-checked background jobs. |
| ME-PF-005 | Derived-data shader cache | Complete | P0 | Asset import cache and immutable inputs. |
| ME-PF-006 | Reference-scene CPU budget | Planned | P0 | Define hardware tiers and measured thresholds. |
| ME-PF-007 | Reference-scene GPU budget | Planned | P0 | Define hardware tiers and measured thresholds. |
| ME-PF-008 | Shader compile-time gate | Planned | P0 | Add cold/warm percentile validation. |
| ME-PF-009 | Variant explosion dashboard | Planned | P0 | Needs project-wide ownership and package impact reporting. |
| ME-PF-010 | Runtime material memory attribution | Planned | P1 | Needs profiler counters by parent/instance/collection. |
| ME-QA-001 | Core deterministic tests | Complete | P0 | Function, layer, collection, compile, and instance tests. |
| ME-QA-002 | Editor routing tests | Complete | P0 | New extensions route to graph or Inspector workflows. |
| ME-QA-003 | Windows debug build | Complete | P0 | Required build is run for this slice. |
| ME-QA-004 | Windows Release validation | Complete | P0 | The clean 0.3.2 Dist Editor package passed the complete Core/editor/Hub, consumer, cook, smoke, and inventory gate. |
| ME-QA-005 | Windows ASan validation | Partial | P0 | Reusable-graph import, collection-state, and dynamic-instance tests pass under ASan; the complete release-candidate gate remains. |
| ME-QA-006 | Linux build/test | Complete | P0 | The exact 0.3.2 commit passed the Rocky Linux package suites and packaged Vulkan/WSLg smoke; DEB/RPM acceptance covered Ubuntu, Debian, Rocky, Fedora, and openSUSE. |
| ME-QA-007 | macOS build/test | Planned | P0 | Required before cross-platform release claim. |
| ME-QA-008 | Sandbox function/layer examples | Planned | P1 | Add progressive reusable graph examples and scene objects. |
| ME-QA-009 | Automated screenshot/golden UI coverage | Planned | P2 | Must remain resilient to backend/font differences. |
| ME-QA-010 | Package/SDK consumer validation | Complete | P0 | Windows and Linux 0.3.2 package gates passed the direct and CMake SDK consumers after the public API additions. |

## Production Scenario Gates

1. **Layered hero material:** a Material Graph calls reusable functions and layers, exposes instance parameters,
   previews and hot reloads, survives a broken dependency, and packages the same renderer contract on every target.
2. **Large material-instance family:** one parent drives at least 10,000 persistent/dynamic instances without shader
   duplication, unbounded uploads, or unstable draw ordering.
3. **World-driven collection:** gameplay updates weather/time/wind collection values once per world and all dependent
   materials observe one frame-consistent revision with bounded uploads.
4. **Safe project upgrade:** historical Shader/Material Graphs open without source mutation; explicit save upgrades
   deterministically; future schemas and broken functions retain the last-good runtime assets.
5. **Cross-platform cook:** Windows, Linux, and macOS packages include only target formats and the complete transitive
   function/layer/material dependency closure.

The current slice closes the reusable-asset and deterministic-compiler foundation. It does not claim full Unreal
Engine parity: the `Partial` and `Planned` rows above remain release-visible work, especially renderer-backed
collections, dynamic-instance render proxies, resource breadth, performance gates, and cross-platform release proof.
