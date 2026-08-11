# Runtime Lifecycle

Kéire uses explicit application-owned services and one construction-thread owner. Understanding the lifecycle is
required before adding layers, event producers, windows, time behavior, or editor UI.

## Application States

An `Application` begins in `Constructed`, transitions to `Running` exactly once through `Run()`, and finishes in
`Stopped`. Calling `Run()` more than once is an error. Destroying an application while it is still running terminates
the process because service teardown cannot be made deterministic from an arbitrary destructor context.

The application construction thread owns `Run()`, immediate event dispatch and subscription, window operations, layer
mutations, and UI workspace operations. `RequestExit()` and owned queued-event enqueue are the deliberate cross-thread
entry points.

## Startup Order

Startup is transactional inside the top-level exception boundary:

1. Open and exclusively lock an editor project when requested, rebasing all project-local service paths.
2. Initialize managed logging when requested.
3. Create the event bus.
4. Create the optional asset system with the event bus as its completion sink.
5. Create optional Scenes after Assets.
6. Create the application-owned time service.
7. Create the window system and primary window.
8. Create optional RenderSystem after Windowing and claim the primary window when rendered.
9. Create optional Input after Assets and Windowing.
10. Create the optional UI system and workspace after Input, bridging presentation through RenderSystem.
11. Connect the layer event listener.
12. Activate the layer stack, call client `OnInitialize()`, and apply pending attachment operations.

If any step fails, shutdown runs for the resources that were acquired. The original exception is rethrown after cleanup.

## Frame Order

```mermaid
flowchart TD
    Start["Sample frame time"] --> PendingA["Apply pending layer changes"]
    PendingA --> Poll["Poll and translate window events"]
    Poll --> Queued["Dispatch queued event snapshot"]
    Queued --> Assets["Commit asset completions and events"]
    Assets --> Scenes["Commit scene load/unload/activation"]
    Scenes --> Input["Publish one input snapshot"]
    Input --> Fixed{"Fixed steps available?"}
    Fixed -->|Yes| FixedUpdate["LayerStack fixed update"]
    FixedUpdate --> Fixed
    Fixed -->|No| Update["LayerStack variable update"]
    Update --> UI{"UI enabled and frame active?"}
    UI -->|Yes| BeginUI["Begin UI frame and root dockspace"]
    BeginUI --> LayerUI["LayerStack UI traversal"]
    LayerUI --> EndUI["Resize targets; scene, grid, UI, present"]
    UI -->|No| PendingB["Apply pending layer changes"]
    EndUI --> PendingB
    PendingB --> Pace["Optional frame pacing"]
```

Window events are pumped before simulation. Queued events drain a fixed snapshot, so events enqueued during that drain
wait for the next frame. Fixed simulation consumes scaled time before variable update. UI runs after simulation and is
skipped when the application is suspended by a minimized primary window.

Minimized suspension is sampled with the time advance at the outer-frame boundary. If a minimize event arrives while
that frame is being pumped, the fixed and variable work already produced for the frame completes, rendering is skipped,
and suspension begins on the next frame. This guarantees that no pending fixed tick can be abandoned across a minimize
transition. A restore event similarly resumes simulation at the following frame boundary.

An editor `SceneRuntimeSession` receives fixed and variable updates from its owning layer. Playing dispatches component
lifecycle callbacks against a private scene clone. Paused sessions receive neither update phase unless Step explicitly
requests one fixed tick. Faulted sessions retain their diagnostic until Stop and never mutate the authored scene.

Exit requests are checked between phases. A request does not force callbacks already on the stack to unwind, but later
frame phases are skipped as soon as the loop reaches a safe check.

## Layer Ownership And Traversal

The application creates `UndoService` before layers attach. A layer may create document contexts in `OnAttach`, but all
history mutation remains owner-thread-affine. `LayerStack::Deactivate` gives panels an opportunity to close those
contexts before the application closes the service and continues subsystem teardown.

`LayerStack` owns every layer through `std::unique_ptr`. Layers attach exactly once after activation and detach in
reverse order. The stack maintains a layer partition followed by an overlay partition.

- Fixed update, variable update, and UI traverse bottom-to-top.
- Events traverse overlays and layers top-to-bottom.
- Handled events stop propagation.
- Automatic layer subscriptions disconnect before detachment.
- `OnDetach()` is `noexcept` and cannot create new automatic subscriptions.

Traversal depth is RAII-managed. Push and remove requests made during attach, detach, event, fixed-update, update, or UI
callbacks are queued until the next application safe boundary. Nested callbacks do not flush the queue early.

## Events

Immediate dispatch is synchronous and construction-thread-affine. Typed and generic listeners share priority and
registration order. Subscription removal during dispatch creates an inactive tombstone without invalidating traversal,
and nested dispatch remains deterministic.

Worker producers use the bounded owned queue. Enqueue either transfers the event into the queue or rejects it without
blocking when capacity is exhausted. Closing the bus makes retained references and subscription tokens inert.

Window events originate in the private SDL boundary, are translated into Kéire value types, and then enter the same
event bus. Unhandled quit and primary-window close requests become application exit requests.

## Time

`Time` is application-owned. Every frame records raw, clamped unscaled, scaled, smoothed, and elapsed values. Scaled
time feeds the fixed-step accumulator; pause and minimized suspension stop scaled simulation without erasing real time.

The fixed-step cap prevents a slow frame from running unbounded simulation work. Excess whole ticks are recorded as
dropped time, while the fractional remainder remains available for interpolation. Changes to clamping, scaling,
smoothing, tick caps, or remainder behavior are observable and require deterministic tests. A scheduler that rejects
already-produced work can explicitly discard pending fixed steps; discarding drains only the pending count and never
advances committed fixed time, fixed tick count, or dropped-time accounting.

## UI

`UiSystem` owns the Dear ImGui context while application-owned `RenderSystem` exclusively owns SDL_GPU and presentation.
`UiFrame` is valid only during
`Layer::OnUi()` on the owner thread. Its move-only scopes balance backend begin/end operations during normal returns and
exception unwinding; a scope cannot escape its frame or close out of nesting order.

The root dockspace is submitted before layer UI. `UiWorkspace` applies queued layout and theme changes at safe frame
boundaries and autosaves after rendering. Raw SDL events, GPU resources, backend types, and native handles never cross
the public UI boundary.

## Shutdown Order

Shutdown is deterministic and idempotent at service boundaries:

1. Deactivate the layer stack and detach layers in reverse order.
2. Call client `OnShutdown()` if initialization completed.
3. Disconnect the application layer-event listener.
4. Close the undo service after document panels have released their contexts.
5. Shut down UI event forwarding, workspace, renderer bridge, and context.
6. Close the render system, wait for its final fences, and release its window claim/device.
7. Close Input and its native-event registration/gamepad handles.
8. Close Scenes and invalidate loaded mutable scene instances.
9. Close the event bus.
10. Release the primary window and shut down the window system and tray.
11. Close the asset system and join its workers.
12. Release the project lock, time/event services, and managed logging.

Cleanup that is required to be `noexcept` contains secondary failures. A failure from application work remains the
exception observed by the caller rather than being replaced by a teardown diagnostic.

## Extension Checklist

Before adding a runtime subsystem, define:

- its explicit owner and construction thread;
- the startup point after every dependency is available;
- the frame phase in which it runs;
- safe behavior during nested callbacks and deferred mutation;
- the cross-thread operations, if any;
- reverse-order shutdown and partial-initialization rollback;
- behavior of retained handles after the service closes;
- deterministic, failure, Release, and sanitizer tests.
