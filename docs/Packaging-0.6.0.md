# V0.6.0 installer validation

4 November 2026. **PASS**: local offline installer compilation, version/checksum checks, native dependency audit and staged application workflows. Clean-PC install/uninstall and physical qualification remain **NOT RUN**.

## Delivered artifact

- File: dist/installers/AeroTrace1553-0.6.0-Setup-x64.exe
- Size: 163,002,761 bytes (155.45 MiB).
- SHA-256: **147E90C437B640CA3900E1781A628D135543C02500210F11319298D0A735C55C**. The adjacent .exe.sha256 file identifies the same artifact.
- Application product version: 0.6.0. Installer numeric file version: 0.6.0.0.
- Signature: **unsigned** (NotSigned); this is a software-qualified hardware-test delivery.
- Payload source commit: **33a9b229496817447b54a327b665a39fc0e38791**.
- Installer recipe/build-check commit: **82545a03b1c275173cf4711e65a1eb9da9d41de4**. Later repository changes only add this final evidence.
- Compiler: Inno Setup 7.1.1.

The installer includes the private Qt/Qwt runtime, isolated document Python/OCR/ONNX/retrieval runtime, docs/examples and the two production providers. The selected default vendor runtime is included for the licensed local delivery. It does not install or replace DDC drivers. No separate operator Python/Qt/compiler setup or online model download is required. Dependency filenames are hidden during the short installer flow.

The source repository contains implementation and sanitized receipts. Licensed SDK packages, the bundled vendor runtime, local installer binaries, recordings and machine-specific support evidence remain outside Git and were not uploaded as public release assets.

## Acceptance

| Check | Result | Evidence |
| --- | --- | --- |
| Native version and sidecar identity | PASS | out/release-0.6.0-installer-identity.json |
| Package checksums | PASS | All 4,051 manifest entries matched; out/release-0.6.0-manifest-check.json |
| Native import audit | PASS | 134 binaries scanned; no missing redistributable or unresolved non-system imports |
| Packaged provider selection | PASS | Both provider DLLs inspected against their matching actual runtimes, without card initialization |
| Clean-PATH staged workflows | PASS | out/release-0.6.0-staged-acceptance/acceptance.json |
| Automation | PASS | Workspace/batch/checks, reviewed mappings/plots, cancellation and partial-failure behavior |
| Sessions and pages | PASS | Recording, stop/drain, paired CSV, indexed replay, filtering, overlapping replay requests and live/export identity |
| Bundled documents | PASS | Demo PDF yields nine validated mappings; retrieval/follow-up, persisted chat, worker conversion, Channel 2/Bus B history, Qwt plots and capture concurrent with document work |
| Logging/support | PASS | Application start/exit journal, capture/run correlation and support ZIP |
| Native icon | PASS | Extracted from packaged executable |
| Clean-PC install/uninstall | NOT RUN | Staging exercises payloads on this host; the existing installed application was preserved |
| Physical capture/reconnect/endurance | NOT RUN | No connected DDC card; follow SES-Bench-Checklist.md |

The staged run removed developer tool paths and Python environment overrides, used packaged Qt plugins, and selected the packaged provider/runtime roots. The deployment marker disables the source-build provider directory. A separate regression confirms that removing bundled providers causes a provider-required error rather than developer fallback. The native import audit checks direct imports and cannot establish arbitrary machine loader/driver compatibility by itself.

The first compiled installer failed the final numeric file-version check: its product text said 0.6.0 while its numeric file version remained 0.0.0.0. The superseded artifact was retained outside delivery. The recipe now sets explicit [file-version](https://jrsoftware.org/ishelp/topic_setup_versioninfoversion.htm) and [product-version](https://jrsoftware.org/ishelp/topic_setup_versioninfoproductversion.htm) resources, and build-installer.ps1 rejects a mismatch before writing the delivery checksum. The accepted artifact above passed the corrected assertion.

[Machine-readable packaging receipt](Packaging-0.6.0.json) includes provider DLL identities and hashes of local evidence. [Functional qualification](Release-0.6.0.md) documents the 29-test desktop baseline, release deployment checks, 14 sanitizer tests, four five-minute fuzz targets and 30,026,073-record four-stream soak, including limitations and earlier failed attempts. Session SQLite, DuckDB/Parquet and standalone scientific-analysis jobs remain deferred by the explicit release-scope decision.
