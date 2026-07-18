# Asset Runtime

## Contract

`AssetSystem` is an application-owned asynchronous content service. Enable it through
`ApplicationSpecification::Assets` and obtain the active service through `Application::Assets()`. The default mode is
`Disabled`, so applications opt into either development catalogs or cooked-only distribution catalogs deliberately.

An `AssetHandle<T>` is a small reference-counted view over shared load state. Repeated requests for the same `AssetId`
return the same state and payload revision. A valid handle's `Get()` always returns the registered typed fallback:
`BinaryAsset` and `TextAsset` use empty values. `TryGetLoaded()` distinguishes loaded data from a fallback, while
`Require()` turns `Failed` and `Cancelled` into `AssetLoadError`. `Require()` never blocks the asset-system owner thread;
callers there should observe state or completion events instead.

States progress through `Queued`, `Loading`, and `Ready`, or end in `Failed`/`Cancelled`. Reloads use `Reloading` and
commit a new immutable payload only at an application frame boundary. A failed reload retains the last known-good
payload and revision, returns to `Ready`, and records a diagnostic. Initial failures retain the typed fallback and become
`Failed`.

## Threading And Completion

Loads are admitted into a bounded five-priority queue and decoded by 1–16 workers (auto-selected when zero). Workers
perform pack I/O, Zstandard decompression, SHA-256 verification, and decoder execution. They never dispatch engine
events or replace a handle payload. `PumpCompletions()` commits results on the construction thread and synchronously
dispatches `AssetLoadedEvent` or `AssetLoadFailedEvent`; `Application` calls it after queued events and before layer
updates.

Catalog mounting, unmounting, reload requests, completion pumping, and eviction are owner-thread operations. `Load()`
is synchronized and may be requested by other threads. `Close()` stops admission, cancels queued work, joins workers,
and leaves retained handles safely inspectable.

## Mounts And Integrity

Each mount supplies a versioned catalog and one or more `.keirepak` files. Catalog paths are confined and relative,
payload ranges are validated before activation, and dependency references must resolve without cycles. Higher-priority
catalogs replace lower-priority entries only when `AllowOverrides` is explicit. This supports base content plus patches
without silent identity collisions.

Before decode, every payload is bounded by `MaximumAssetBytes`, decompressed with the pinned Zstandard build, and
verified against its catalog SHA-256. Decoder registrations require a non-null fallback whose type exactly matches the
registration. Binary and UTF-8 text decoders are built in; renderer, texture, audio, model, and scene asset types remain
future subsystem work.

The default queue capacity is 4096, resident cache budget is 512 MiB, maximum individual asset size is 1 GiB, and
development catalog path is `Library/AssetCache/Runtime/catalog.json`. `EvictUnused()` only removes ready entries whose
handle state has no external owner, so live handles never lose data.
