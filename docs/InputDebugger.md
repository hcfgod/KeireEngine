# Input Debugger

The dockable Input Debugger proves the action pipeline using Kéire APIs; KeireClient contains no Dear ImGui calls or SDL
input ownership.

Opening a project attaches its default `.keireinput` asset to the editor user. Open **Window > Input Debugger** and choose
**Start Input Test**. The operation enables every map transactionally, installs RAII action subscriptions, and temporarily
bypasses UI keyboard/pointer suppression only for those test maps. Stop, panel teardown, or an initialization failure
removes subscriptions, capture overrides, and enabled maps.

Each Started, Performed, and Canceled action is recorded in Console with:

- map/action and phase;
- processed scalar/2D value;
- user and device IDs;
- selected or automatic control scheme;
- interaction duration and monotonic input timestamp.

The debugger also lists connected/paired devices and local users. Console keeps a bounded 10,000-entry history, supports
search and clear, and Pause freezes the visible snapshot without dropping new events.

Input Test Mode is an editor diagnostic, not gameplay routing. Runtime code should create its own `InputActionContext`,
enable only the required maps, and keep `InputCaptureOverride` scoped to deliberate tools such as rebinding or diagnostics.

