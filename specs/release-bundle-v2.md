# Local website release bundles

`python tools/tinyrt.py release <app> --development-key` produces an offline
website v2 ZIP and a matching unpacked directory. Set `TINYRT_CC` and
`TINYRT_RUNNER`, or pass `--cc` and `--runner` with local executables.
It does not download tools, upload files or publish to a server.

## Inputs and versions

Keep the existing app.json contract. Add listing.json, cover.png, README.md,
CHANGELOG.md and LICENSES.md. The three Markdown files must be nonempty UTF-8
without BOM, use LF and be at most 64 KiB each. Cover is a single-frame RGB/RGBA
210x210 PNG at most 64 KiB; app.json must point to it. Listing follows
[listing.schema.json](listing.schema.json); unknown fields, duplicate keys and
nonstandard JSON numbers are errors. The handoff's "four Markdown files" is a typo.

app.json.version is the sole positive uint32 application release version.
ZIP names, signed package metadata and release.json.version use that integer.
listing.json rejects display_version. Application Hall does not display versions.
Increment for distributed application updates, not repeated local builds.
Schema 2, ABI 1 and package format 1 are independent protocol versions.
See [version-management.md](version-management.md) for compatibility and identity.

Screenshot entries require exactly one of at_ms and file; caption is optional.
1-6 entries appear in listing order as screenshots/01.png ... 06.png. at_ms is
WAMR virtual time, bounded by --preview-ms (default/hard maximum 60000 ms).
Optional events.json uses the existing run event format, within that duration.
Preview ends at the latest screenshot or input event; device-only listings still
run WAMR at least at time zero. Device PNGs must already be single-frame 466x466,
at most 1 MiB; never stretched. Screenshots are re-encoded as metadata-free RGBA
PNGs; preview images retain the existing round-screen alpha mask.

## Pipeline and output

1. Check inputs, local tools, signing/channel rules and destination ownership.
2. Run <app>/tests/run_native.py --cc <compiler> if supplied. Shipped examples
   use SDK tests/<example>/run_native.py; otherwise native is not-run.
3. Reuse SDK build and real WAMR preview; failures stop the release.
4. Sign, authenticate the envelope and read identity/policy/AOT facts back.
5. Extract actual Wasm function imports, sort/deduplicate and reject unknown
   imports or missing permissions. Runner loading/execution complements envelope
   validation; local checks do not replace all device/core compatibility checks.
6. Generate manifest/report, archive and self-inspect, then commit local output.

Default destination is <app>/release/<app_id>-v<app.json.version>-<variant>.zip
and its same-named unpacked directory. --output changes the destination directory. Fixed entry order:

```text
release.json
package.trpkg
cover.png
README.md
CHANGELOG.md
LICENSES.md
screenshots/01.png ... screenshots/06.png
build/report.json
```

No enclosing folder or directory entries. Producer uses ZIP_STORED with fixed
regular-file attributes, no extras/comments/encryption/ZIP64. Limits: ZIP 12 MiB,
expanded 10 MiB, package 2 MiB, manifest 32 KiB, report 256 KiB. Each file's hash
and size use final bytes. Report always accompanies successful output, though
website Schema permits report=null. Steps use name/result and optional string
detail. Device verification and rights come solely from developer declarations.

A failed rebuild preserves the previous complete deliverable. Unknown output,
modified unpacked files or linked destinations are refused. The ZIP commits
last. Failure writes a sanitized stage report to <app>/build/release-report.json
when safe; it never emits a partial ZIP. Inputs are checked for changes during
the build. Source, keys, scripts and firmware never enter the archive.

## Reproducibility and signing

Same inputs, keys and toolchain/environment produce the same bytes. JSON uses
UTF-8 without BOM, LF, two spaces and fixed key order. built_at uses --built-at,
then SOURCE_DATE_EPOCH, then SDK commit time; never current wall-clock time.
ZIP uses that UTC timestamp, clamped to the DOS year range and two-second
resolution. Variable timings and absolute preview paths do not enter reports.
build.command is a logical invocation with <app>/<key> placeholders for local
paths; compiler version is recorded separately. SDK HEAD is recorded. Source
archives need --sdk-revision plus SOURCE_DATE_EPOCH or --built-at.

Default variant is wasm-aot. --wamrc <local compiler> uses the
existing pinned development AOT workflow with Wasm fallback. Production AOT
remains in controlled release_compile.py. This project's deliverable is the AOT
ZIP; standalone Wasm ZIPs are interpreter diagnostics, not product deliverables.
Private-key Wasm diagnostics use --variant wasm --key <key>
--key-id <id>. --channel defaults to development. Public development scalar
(also supplied via PEM) requires key ID 1, demo. namespace and development channel.

Self-check covers ZIP whitelist/duplicates/types/limits/CRC, v2 Schema, hashes,
PNGs, signature, metadata/imports/variant and signed-cover consistency. Website
account authorization, key registry, channel approval and rights review remain
server decisions. Local success does not assert online acceptance.

V2 adds release_notes, changelog, build and screenshot captions. Other semantics
and error-code names remain in developer-publishing-v1.md. Failure diagnostics
identify the local stage and cause; native/preview passed refers only to work
actually performed in this invocation.
