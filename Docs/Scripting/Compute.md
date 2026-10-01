# GPU Compute From C#

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Runtime reference](RuntimeReference.md)

Managed compute exposes an independent compute device, storage buffers, pipelines, and explicit submissions.
It is a host-integrated workflow: pipeline creation consumes the opaque program key registered by native
`ScriptSystem.RegisterComputeProgram`. A Shader Graph asset reference or Asset ID is not that key. Do not invent
numeric keys or assume creating an ordinary script imports/registers a compute program automatically.

## Workflow And Prerequisites

1. Compile/package the compute program through the native shader/program workflow.
2. Have the native host register the program and provide its returned key to the managed integration.
3. Select a supported backend: `D3D12`, `Vulkan`, or `Metal`. Backend availability depends on platform, drivers, and
   packaged native support; the enum is not an availability probe.
4. Create one device on the thread that will own all operations and disposal.
5. Create buffers and a pipeline, upload aligned bytes, dispatch the intended group counts, and retain submissions
   while observing completion/readback.
6. Release submissions, buffers, and pipelines, then the device, on that same thread.

For compilation and native resource details, see the [compute compiler workflow](../RevampComputeCompilerLane.md),
[native compute workflow](../RevampComputeNativeLane.md), and
[ComputeDevice declarations](RuntimeReference.md#keirecomputedevice).

## Dispatch And Read Back

The compile-checked helper [ComputeExample.cs](Examples/ComputeExample.cs) is called by a host integration on its
owning thread. It expects a program with writable storage binding slot `0`, no uniforms, one workgroup sufficient
for its algorithm, and a 16-byte output layout. Change those assumptions to match the program's actual contract.

```csharp
using ComputeDevice device = new(backend);
using ComputeBuffer output = device.CreateBuffer(16);
using ComputePipeline pipeline = device.CreatePipeline(programKey);
output.Upload(new byte[16]);

using ComputeSubmission dispatch = device.Dispatch(
    pipeline, new[] { new ComputeBufferBinding(0, output, Writable: true) }, x: 1);
dispatch.Wait();
using ComputeSubmission readback = output.RequestReadback();
byte[] bytes = readback.GetReadback();
```

`Dispatch` dimensions count workgroups, not individual threads. The shader's workgroup size determines invocation
count. `GetReadback` waits for the requested snapshot and may be called again until the submission is disposed.
`Wait` and readback can block: this compact helper is appropriate for validation or controlled host work, not a
per-frame latency-sensitive callback. To avoid a deliberate wait, retain the submission, poll `IsComplete` on the
creation thread in later frames, then retrieve and release it.

Do not move the resources into `Jobs.Run`: the compute device enforces creation-thread ownership. Coroutine yielding
on that same thread is a possible polling mechanism, with explicit release on disable/reload and no use after teardown.

## Arguments And Bounds

| Operation | Required contract |
| --- | --- |
| `CreateBuffer(size)` | Positive size, multiple of four, at most 128 MiB |
| Indirect buffer | `indirect: true`, at least 12 bytes |
| `Upload(bytes, offset)` | Nonempty byte count and offset aligned to four bytes; range contained in the buffer |
| `RequestReadback(offset, size)` | Positive resolved size; aligned contained range; zero size means remaining bytes |
| `CreatePipeline` / `Reload` | Nonzero registered program key; valid compiled variant |
| `Dispatch(x, y, z)` | Each dimension in `1..65535`; at most 1024 bindings |
| Buffer binding | Resource belongs to the same device; `(Slot, Writable)` is unique within the dispatch |
| `DispatchIndirect` | Indirect buffer from the same device; aligned offset with room for three `uint` values |

The GPU program contract determines read/write layouts, uniforms, and valid slot usage. Managed validation does not
infer your algorithm or synthesize missing bindings. Use an explicit byte layout that agrees with the compiled program.

## Reload And Failure

`ComputePipeline.Reload(programKey, variant)` asks the native boundary to replace its program. Check host registration
and compiled variants before requesting it. Managed script generation reload and pipeline program replacement are
different lifecycles: a Behaviour that owns compute resources must release or deliberately recreate them in its
reload callbacks, just as it does for other runtime resources.

Wrong-thread calls, disposed resources, cross-device buffers, invalid ranges, and invalid arguments throw before
successful submission. An unavailable native compute boundary throws `InvalidOperationException`. Use scoped
ownership or a clear controller release path so setup failure cannot leak resources acquired earlier.
