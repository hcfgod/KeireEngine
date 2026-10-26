# Audio Production Workflow And Audit

Review date: 2026-08-09  
Initiative: KE-019, phase 1

Editor authoring follow-up: 2026-10-25 (development changes; acceptance is tracked separately).

This audit separates working contracts from later production work. It also tracks the earlier mixer/reverb defect as a
runtime fix: broad roadmap language is not evidence that a critical audio path works.

## Phase 1 Capability

Audio Sources route by stable mixer and bus IDs while legacy bus names remain compatible. Headless rendering executes
ordered bus effect racks, pre- and post-fader sends, parent routing, mute/solo, block-level ducking, and publishes
bounded peak/RMS/clipping meters. Gain, filters, dynamics, delay, chorus, distortion, and algorithmic reverb are
deterministic offline. Algorithmic reverb preserves the requested dry mix and produces a bounded, repeatable multi-tap
tail. Scene presentation now loads each convolution dependency as an `AudioClipAsset`, binds resident PCM to the mixer
registration, resamples it to the mix format, and retains that decoded data for both headless and device DSP. The
Equalizer preserves its legacy output-gain parameter at index zero and adds low/mid/high dB bands with authored
crossovers.

Audio Reverb Zones are now runtime-active. The primary listener selects the overlapping zone with the highest priority,
then the strongest blend as a tie-break. Box and sphere boundaries blend outward over `Blend Distance`; the selected
mixer snapshot and reverb-send scale apply together. Leaving the zone restores the immutable base mixer definition.
Invalid or unavailable mixer content never partially replaces the last registered routing snapshot.

The Audio Mixer editor publishes unsaved routing/fader changes transiently for live device preview. Its **Mix Console**
provides channel faders, mute/solo state, and live peak meters. **Routing** shows the bus hierarchy and connection graph;
**Channel Inspector** edits the selected bus output, ordered effects, and sends. **Snapshots** and **Ducking** have
their own tabs. Effect racks use named, bounded controls with unit-aware presentation instead
of anonymous parameter numbers. The Inspector resolves inherited Default Mixer routing and presents named bus and
snapshot selectors rather than stable-ID text fields. Headless previews and tests exercise the DSP path and meters.

Project Settings owns the playback device, mix sample rate, period size, mono/stereo/5.1/7.1 layout, and resident and
virtual voice budgets. A missing saved device falls back safely to the system default and reports that fallback in the
Profiler. Cooked players receive the portable audio format and voice settings but deliberately exclude the authoring
workstation's hardware device ID.

## Snapshot Audition In The Editor

In **Snapshots**, select a snapshot and set **Preview Weight** from zero (the current authored draft) to one (the full
snapshot). Choose **Audition Snapshot** to publish that blend for live preview. Change the weight and audition again
to compare positions in the blend. Every audition starts from the draft, not the last audition, so repeated previews
do not accumulate gain changes. Mute, solo, and bypass overrides switch at weight 0.5 rather than interpolating.

**Restore Authored Mix** returns preview to the current draft, including any unsaved authoring changes. Audition does
not change dirty state, source bytes, revision, or undo history. Save persists authored values, never the auditioned
blend. Successful content edits and Undo/Redo publish their authored state again. A failed audition attempts to restore
the draft and reports the original failure even if restoration also fails; do not assume that a failed preview changed
the device. This is manual blend audition, not a timed transition editor or an audio playback source. Route a playing
source through this mixer to hear it; a snapshot alone does not start a voice.

Live preview resolves impulse-response resources before publishing a candidate mix. If publication or device submission
fails, the Editor attempts to restore the last successfully previewed mix; the document also attempts to restore its
authored draft and keeps the original error. Closing the panel restores the latest readable saved source. If the source
has become unreadable or its new impulse responses are unavailable, the Editor uses its retained saved baseline and
decoded resources instead. A failed restore retains the session for retry; reopen and close the mixer to retry it.
Saving prepares the retained baseline before writing, then advances it only after a successful atomic source write.
These safeguards do not guarantee device recovery after a hardware failure or make audition a timed transition.

## Phase 1 Migration

Existing playback, mixer submission, and metering calls remain source-compatible. Initial registration of a mixer with
convolution effects must use the `AudioMixerImpulseResponses` overload; scene presentation and cooked-player flows do
this automatically, and later overloads without a map reuse the already-bound resources. `AudioSystemStatistics` gained
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
| Convolution IR | At most 5 seconds and 4,194,304 output-channel frames per effect and per mixer aggregate; 8,388,608 active processor channel frames per audio system, with offline and device instances counted separately |
| Recommended active effects | At most 64 across the mixers used by one presentation |
| Device callback | No game callback, asset load, file I/O, or unbounded allocation |
| Audio CPU | Target <= 2 ms p95 and <= 4 ms maximum per game frame on reference hardware |
| Underruns | Zero during a 30-minute content-scale soak and packaged smoke run |

Track `AudioSystemStatistics` for voices, virtualized/audible counts, mixer registrations, bus/effect counts, meter
readings, rendered frames, and underruns. Treat a rising underrun count or sustained budget breach as a release failure.

## Remaining Work

The device path now hosts compiled bus routing, ordered effects, sends, ducking, reverb, and meters with
generation-safe graph replacement. Decoded convolution is active and block-continuous. Responses through 128 frames
use a fixed bounded direct path; longer responses use allocation-free uniform partitioned convolution with a
sample-rate-scaled block and one-block wet-path latency. EQ coefficients and gains are compiled before processing, and
inverted crossover values are normalized into low-to-high order. Non-uniform partition tuning remains performance work.
Parameter-only mixer updates, including continuously blended Reverb Zone weights, retain filter, delay, reverb, and
convolution state. The owner thread publishes bounded atomic control snapshots to native effects and send volumes;
topology, bypass, ducking, or impulse-resource changes deliberately rebuild the affected routing.
Device hot-plug while the editor is already running,
platform-specific latency tuning, effect-specific visualization, timed snapshot transition controls, accessibility review,
and long-run Windows/Linux/macOS packaged matrices also remain acceptance work.

Future work must preserve callback-thread constraints: no game callback, asset load, file I/O, locks, or unbounded
allocation. Public API changes require source migration notes and both SDK consumers to pass.

## Validation Scenarios

- Route a looping source through a child bus with an effect and return send; verify output and per-bus meters.
- Render an impulse through algorithmic reverb twice; require identical dry signal and nonzero bounded tail.
- Bind a mono impulse-response asset with a different source rate, render it through headless and block-streamed
  convolution, and require the same resampled nonzero tail.
- Cross a reverb-zone blend boundary, overlap a higher-priority zone, then exit; require snapshot selection and base
  restoration without recreating the voice.
- Replace a mixer with an invalid revision; require the last valid routing to remain active.
- Run Debug, DebugASan, Release, packaged-player smoke, and a 30-minute underrun soak on every supported platform before
  declaring the device DSP phase production-ready.
