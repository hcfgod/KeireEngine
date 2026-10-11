# Windows installer performance

The Editor and Hub installers use non-solid zlib compression. This favors faster extraction at the cost of a larger
download. Setup suppresses individual extraction rows for the bundled SDK's thousands of files, while retaining the
progress bar and phase messages. The install worker streams transaction phase durations to the details list.

## Measurements

Run the extraction benchmark with no other build or disk-heavy tests active:

```powershell
./Scripts/Tests/measure-installer-extraction-windows.ps1 `
    -SourceDirectory ./Build/Distributions/keire-editor-windows-x86_64-Dist/bin/Managed/Dotnet `
    -ExtractionRoot C:/Temp/ki-bench
```

The script writes results under `Build/Validation/InstallerExtraction`, retains its isolated extraction directories,
and checks every extracted file against the source SHA-256. It does not register or replace any installed product.
Use a short writable extraction root: the benchmark rejects paths exceeding the Win32 MAX_PATH limit instead of
accepting incomplete extraction. Run multiple times, reversing `-Compression zlib,lzma`, to assess cache and disk
variance. Extraction duration excludes the subsequent hash comparison.

On October 7, 2026, two isolated comparisons using the locked .NET 10.0.302 SDK from clean Editor build
`58164ed2` produced these results on the development Windows machine. The second comparison reversed extraction
order and reused the same benchmark archives:

| Compression | Benchmark EXE bytes | First comparison seconds | Reverse-order seconds | Files verified per run |
| --- | ---: | ---: | ---: | ---: |
| LZMA | 190,733,400 | 36.62 | 47.21 | 5,611 |
| zlib | 240,295,849 | 15.19 | 18.48 | 5,611 |

This measures SDK extraction, not total installation time, and is not a performance guarantee for other machines.
It trades roughly 50 MB of additional download size for roughly 21–29 seconds saved in these samples. Download bandwidth,
antivirus scanning, storage, and other running applications affect the overall experience.

On October 11, 2026, the restored checkout's staged Editor (executable SHA-256 prefix `F77DF3E6`) was measured again
with the Editor closed and no active builds. The reverse-order comparison reused the same benchmark archives.
Every extracted file passed its SHA-256 comparison in all four extractions.

| Compression | Benchmark EXE bytes | First comparison seconds | Reverse-order seconds | Files verified per run |
| --- | ---: | ---: | ---: | ---: |
| LZMA | 190,733,295 | 27.58 | 25.81 | 5,611 |
| zlib | 240,295,858 | 10.06 | 8.80 | 5,611 |

These samples saved approximately 17 seconds of SDK extraction at the cost of approximately 50 MB of download size.
They do not measure the full installer, transaction verification, or setup-window responsiveness, and do not establish
a cold-cache guarantee. No installed product or registration was changed by the benchmark.

## Interpreting transaction timings

Setup reports extraction `elapsed_ms`. The worker reports each command's total elapsed milliseconds and durable
transaction phase elapsed/delta milliseconds. The staged phase includes preparation and source validation; subsequent
phases separate backup moves, activation, registration, verification, and commit. Report these details with the
product, fresh-install versus upgrade scenario, and destination drive when investigating slow setup.

Full payload verification, owned-file boundaries, installed executable verification, and rollback remain enabled.
Do not remove mutation-boundary checks merely because a file was checked earlier: files can change between operations.
SDK reuse and direct staging need an explicit transaction design and recovery tests before adoption. Live phase output
does not by itself prove the Windows setup window remains responsive throughout every operation; validate that visually.

## Remaining measurements and SDK reuse

The measured Editor package contains 6,536 declared files totaling 1,747,644,771 bytes. Its bundled SDK contributes
5,611 files and 798,277,746 bytes. Measure a fresh installation and a same-package update separately: updates also
verify the existing installation, move its owned files into backup, and clean up that backup after commit.
Debug-worker fixture timings are diagnostic only and must not be presented as shipping installer timings.

SDK reuse is not implemented. A safe implementation needs an explicit journal inventory of unchanged files retained
in place, matched by path, size, and hash against both receipts. Recovery must distinguish retained files from newly
activated files, preserve unknown user content, and handle interruption at every durable phase. Test changed or missing
SDK files, an SDK version change, locked files, rollback, and subsequent uninstall before enabling reuse. Avoid hard
links between a rollback backup and writable installed files: a later write could alter both copies.

Skipping SDK extraction is a separate packaging change. The complete expected SDK must still be available and
verified when no reusable installation exists, including offline installation. Do not infer that retaining unchanged
installed files automatically eliminates extraction or download cost.
