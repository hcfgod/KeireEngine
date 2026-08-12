# Audio Production Workflow And Audit

Review date: 2026-08-09  
Initiative: KE-019, phase 1

This audit separates working contracts from later production work. It also tracks the earlier mixer/reverb defect as a
runtime fix: broad roadmap language is not evidence that a critical audio path works.

## Phase 1 Capability

Audio Sources route by stable mixer and bus IDs while legacy bus names remain compatible. Headless rendering executes
ordered bus effect racks, pre- and post-fader sends, parent routing, mute/solo, block-level ducking, and publishes
bounded peak/RMS/clipping meters. Gain, filters, dynamics, delay, chorus, distortion, and algorithmic reverb are
deterministic offline. Algorithmic reverb preserves the requested dry mix and produces a bounded, repeatable multi-tap
tail. Convolution topology and dependencies are validated, while decoded IR binding remains in the next phase.

Audio Reverb Zones are now runtime-active. The primary listener selects the overlapping zone with the highest priority,
then the strongest blend as a tie-break. Box and sphere boundaries blend outward over `Blend Distance`; the selected
mixer snapshot and reverb-send scale apply together. Leaving the zone restores the immutable base mixer definition.
Invalid or unavailable mixer content never partially replaces the last registered routing snapshot.

The Audio Mixer editor publishes unsaved routing/fader changes transiently for live device preview. Its **Mix Console**
provides channel faders, mute/solo state, and live peak meters, while **Channel Inspector** owns graph routing, ordered
effects, sends, snapshots, and ducking. Effect racks use named, bounded controls with unit-aware presentation instead
of anonymous parameter numbers. The Inspector resolves inherited Default Mixer routing and presents named bus and
snapshot selectors rather than stable-ID text fields. Headless previews and tests exercise the DSP path and meters.

Project Settings owns the playback device, mix sample rate, period size, mono/stereo/5.1/7.1 layout, and resident and
virtual voice budgets. A missing saved device falls back safely to the system default and reports that fallback in the
Profiler. Cooked players receive the portable audio format and voice settings but deliberately exclude the authoring
workstation's hardware device ID.

## Phase 1 Migration

Existing playback, mixer submission, and metering calls remain source-compatible. `AudioSystemStatistics` gained
additive mixer/effect/meter counters; code that names fields needs no change, while positional aggregate initialization
should be replaced with named members. Reverb Zones that previously serialized but had no runtime effect now apply
their referenced mixer snapshot, so projects should verify authored zone priorities, blend distances, and send levels.

## Performance Budgets

These are acceptance targets, not promises about unmeasured hardware:

| Resource | Hard bound or target |
| --- | --- |
| Resident voices | `AudioSystemSpecification::MaximumVoices`; at most 4x that count may exist including virtual voices |
| Meter readings | `MaximumMeterReadings`, default 256; overflow is counted |
| Mixer schema | 256 buses, 128 effects and 64 sends per bus, 64 parameters per effect |
| Recommended active effects | At most 64 across the mixers used by one presentation |
| Device callback | No game callback, asset load, file I/O, or unbounded allocation |
| Audio CPU | Target <= 2 ms p95 and <= 4 ms maximum per game frame on reference hardware |
| Underruns | Zero during a 30-minute content-scale soak and packaged smoke run |

Track `AudioSystemStatistics` for voices, virtualized/audible counts, mixer registrations, bus/effect counts, meter
readings, rendered frames, and underruns. Treat a rising underrun count or sustained budget breach as a release failure.

## Remaining Work

The device path now hosts compiled bus routing, ordered effects, sends, ducking, reverb, and meters with
generation-safe graph replacement. Convolution topology and dependencies are validated, but decoded impulse-response
binding and partitioned convolution state are not yet production-ready. Device hot-plug while the editor is already
running, platform-specific latency tuning, effect-specific visualization, snapshot transition controls, accessibility
review, and long-run Windows/Linux/macOS packaged matrices also remain acceptance work.

Future work must preserve callback-thread constraints: no game callback, asset load, file I/O, locks, or unbounded
allocation. Public API changes require source migration notes and both SDK consumers to pass.

## Validation Scenarios

- Route a looping source through a child bus with an effect and return send; verify output and per-bus meters.
- Render an impulse through algorithmic reverb twice; require identical dry signal and nonzero bounded tail.
- Cross a reverb-zone blend boundary, overlap a higher-priority zone, then exit; require snapshot selection and base
  restoration without recreating the voice.
- Replace a mixer with an invalid revision; require the last valid routing to remain active.
- Run Debug, DebugASan, Release, packaged-player smoke, and a 30-minute underrun soak on every supported platform before
  declaring the device DSP phase production-ready.
