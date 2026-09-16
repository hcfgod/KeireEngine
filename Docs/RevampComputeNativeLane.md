# Compute native implementation evidence

This lane adds an independent, application-thread-owned `ComputeDevice`. It creates actual SDL GPU devices using the
explicit D3D12, Vulkan, or Metal backend and consumes verified cooked `ProgramArtifact` compute binaries. It does not
compile source internally. Unsupported backends, absent binaries, graphics targets, textures, resource arrays, register
gaps, and unsupported uniform layouts raise errors before dispatch.

## New files owned by this lane

- `KeireCore/Include/Keire/Rendering/Compute.h`
- `KeireCore/Source/Rendering/Compute.cpp`
- `KeireTests/Source/Rendering/ComputeTests.cpp`
- `KeireTests/Source/Rendering/ApplicationComputeTests.cpp`
- `Docs/RevampComputeNativeLane.md`

Existing files changed for application ownership: `KeireCore/Include/Keire/Application.h` and
`KeireCore/Source/Application.cpp`.

These files are additions after the supplied seed. The header was first created in the isolated worktree and transferred
by the lead after the user directed work to the canonical checkout. Implementation, tests, and this evidence file were
created directly in `C:/Users/keith/Desktop/KéireEngine`. Application files were extended in place and formatted under the
repository formatting requirement. No historical seed features were reimplemented here.

## Contracts implemented

Public identities carry private device identity leases and monotonically assigned slot values. Foreign devices, default
identities, stale buffers/pipelines, released submissions, and operations after shutdown are rejected. The leases retain
identity only; native resources remain device-owned. All operations require the construction thread except the atomic
`IsOpen` query. Destruction is a documented owner-thread operation and performs nonthrowing final cleanup.

Buffers are four-byte-aligned, nonempty, at most 128 MiB, and support actual upload/download through transfer buffers.
Each buffer/pipeline/submission registry is bounded to 4096 entries. Read-only buffer registers use space 0; writable
registers use space 1. Each class has consecutive unique slots from zero with a maximum of 16. Structured buffer sizes
must be multiples of the reflected stride. Uniforms require one block at b0/space2 with its exact byte size in
`StrideBytes`, nonzero, divisible by 16, and at most 64 KiB; unknown or mismatched sizes reject.

Direct dispatch validates dimensions in 1..65535. Indirect dispatch requires an indirect-capable buffer, aligned in-bounds
12-byte arguments, and synchronously reads back GPU-produced arguments to validate the same dimensions before executing
the native indirect command. This conservative validation adds a synchronization point. Writable aliasing within a pass,
including aliasing with its indirect arguments, is rejected. Separate ordered command submissions and SDL compute/copy
pass transitions provide intra-device dependencies; resources are never cycled during upload or writable binding.

Dispatch returns a real GPU fence identity. `IsComplete` polls it; `Wait` waits and retires its native fence while retaining
the completed identity; `ReleaseSubmission` waits and erases the identity. Buffer and pipeline destruction use SDL deferred
native release. Shutdown waits for device idle, releases outstanding fences/resources, and destroys the device. Repeated
shutdown is safe. Any native creation, mapping, command acquisition, pass creation, submission, or wait failure makes the
device inert before throwing. Native storage is retained until stack guards unwind and explicit shutdown or destruction
reclaims it. Recovery requires a fresh device and new resources; automatic reload or recreation is not claimed.

`Application::Compute()` lazily owns a reference-counted compute device selected from the live renderer backend. It
rejects access outside an active rendered application or from another thread. The application waits for compute work
before UI and non-UI presentation and shuts the service down before renderer/window teardown on normal and exceptional
exit. References retained by clients are inert after shutdown. This is an interim application-owned independent queue
and coarse completion barrier, not shared graphics resources or final frame-graph scheduling.

## Checks performed by this lane

- Repository `AGENTS.md` read before implementation.
- `clang-format -i` and `clang-format --dry-run --Werror` on all six changed C++ files passed.
- One lightweight `clang++ -std=c++20 -fsyntax-only -DKEIRE_STATIC -I KeireCore/Include -I Vendor/SDL/include
  -I Vendor/glm KeireCore/Source/Rendering/Compute.cpp` passed before the subsequent fail-closed and indirect-validation
  edits. It is not evidence of a final build.
- Focused tests added for lazy pre-device input rejection, inert/idempotent shutdown, unknown backend values, and
  cross-thread rejection with unchanged state. Application tests also cover before/after Run, disabled/headless rendering,
  and wrong-thread rejection. Execution is coordinated by the lead with the shared build lock.

## Open acceptance gates

The lead owns compiler-to-GPU execution tests, managed integration, umbrella-header registration, package impact, and final
build evidence. This lane has not run Debug, Release, AddressSanitizer, Linux, macOS, or packaged consumer validation.

The independent GPU device is backend infrastructure. It does not share buffers with `RenderSystem`, participate in the
engine frame graph beyond its coarse application presentation barrier, expose storage textures/samplers, automatically recover after device loss, reload
programs in place, or implement graphics indirect draws. Those integration gates remain open. No editor creation menu or
material picker was changed by this lane.

## Proposed integration documentation

README: A standalone compute device can consume cooked compute programs, allocate explicitly owned buffers, dispatch
buffer kernels, and inspect completion/readback. It currently uses an independent GPU queue/device; graphics resource
sharing and frame-graph integration remain unavailable.

Architecture: Compute resources and submission fences belong to an explicit device owner. Private identity leases prevent
cross-device/stale access without exposing native handles. Native failure makes the service inert and shutdown performs
final retirement; reconstruction requires a new device.

Changelog: Added an opaque native buffer-compute API with cooked backend pipelines, validated direct/indirect dispatch,
fence completion, readback, owner-thread enforcement, and explicit resource/shutdown lifetimes.

## September 15 continuation: reload and asynchronous readback

`ComputeDevice::ReloadPipeline` replaces the native pipeline behind an existing identity only after the new cooked
artifact has passed validation and backend creation. Pending work retains the old native pipeline through SDL deferred
release. Invalid artifacts leave the previous pipeline usable. Native creation failure still makes the device inert;
this is not automatic device-loss recovery.

`RequestReadback` submits a buffer snapshot and returns an ordinary completion identity without waiting on a GPU fence.
`GetReadback` waits and copies the snapshot; repeated retrieval remains valid until `ReleaseSubmission`. The transfer
buffer belongs to the request, so destroying or updating the source after submission does not invalidate the snapshot.
Shutdown releases pending transfers after the device idle barrier. Requests share the 4096-submission bound and retain
at most 256 MiB of snapshot storage per device. There is no cancellation claim for already submitted GPU work.

Managed `ComputePipeline.Reload`, `ComputeBuffer.RequestReadback`, and `ComputeSubmission.GetReadback` preserve these
contracts. Managed resource wrappers enforce owner-thread/disposal checks, and readback wrappers can outlive the source
buffer. New-work operations remain forbidden in a preparing managed reload candidate. Existing synchronous buffer
upload and direct dispatch now reject alignment, allocation-bound, and dispatch-bound violations before native calls.

Focused no-device tests cover new operation identity/shutdown/thread checks and malformed managed command payloads.
The opt-in backend test now checks failed-reload last-good behavior, replacement while a submission remains live, and
readback persistence across upload and source destruction. Execution evidence will be added after the coordinated build.

Still open: storage textures/samplers and their verified compiler reflection, shared graphics resources/frame-graph
scheduling, automatic device recreation, and actual Vulkan/Metal host runs. Texture support requires public opaque
texture identities, format/mip/access validation, SDL texture transfer ownership, managed commands, and SPIR-V image
reflection; adding only dispatch bindings would not close this contract.
