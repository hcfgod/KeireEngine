# Kéire SDL timestamp extension

`0001-gpu-timestamp-queries.patch` extends the SDL revision pinned in `Config/Dependencies.lock`. Repository dependency
launchers apply it to an isolated cached source copy; do not apply it to the tracked `Vendor/SDL` checkout. The patch
is an explicitly maintained Kéire extension, not an upstream SDL API. Its original SDL license notices are preserved.

The opaque `SDL_GPUTimestampQuery` holds two native timestamp slots. Check `SDL_GPUSupportsTimestampQueries`, create a
query, then record `SDL_WriteGPUTimestamp` with slot 0 and slot 1 outside GPU passes, submitting in that order. Read
`SDL_GetGPUTimestampQueryResult` with a fence from the final marker's submission or a later same-device submission.
It returns false without changing the output if that fence is incomplete or the query result is unavailable.

Operations on a query must be externally serialized. Reuse starts with slot 0 only after every command buffer
referencing the previous recording has completed or been canceled. `SDL_ReleaseGPUTimestampQuery` has the same
completion requirement; it never waits implicitly. Retain the owning device throughout. Kéire enforces these rules
with its bounded accepted-frame slots and final submission fence. Device-loss policy applies to query objects just
as it does to other native resources.

D3D12 uses query heaps and queue-frequency conversion; Vulkan uses query pools, queue valid bits, and timestamp
period; Metal uses capability-gated timestamp counters and paired CPU/GPU clock calibration. Unsupported Metal
sampling tiers and Vulkan queues report unavailable. Neither fence-polling duration nor CPU submission latency is
reported as GPU execution time.

See [performance measurement and acceptance](../../Docs/PerformanceGates.md). The extension and renderer integration
require native backend/lifecycle validation and controlled Release captures before a production gate can be claimed.
