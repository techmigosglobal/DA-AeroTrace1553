# Task workspaces and reviewed ICD automation

Open **More tools → Tasks and automation** or press **Ctrl+Shift+T**. The existing capture, conversion, plotting and replay services remain responsible for application operations. Retrieval supplies evidence; chat never evaluates code or calls arbitrary application functions.

## Saved workspace

Use **Apply simulation and checks** to commit edits, then **Save workspace**. A `.aerotask.json` contains the conversion profile and document provenance, capture source, enabled channel/bus streams, logical devices, runtime path, simulation settings, check rules, monitor filters, selected plot signal/source, added plot traces, column visibility and sidebar layout.

Opening a workspace validates it before applying it. Capture must be stopped and stays stopped; device connection is explicitly held until the operator starts it. Existing recordings and source PDFs are not embedded or changed. Added plot traces can be cleared from the workspace tab. Workspaces without an ICD can also be restored; engineering analysis requires a conversion profile.

## Offline simulation

Select **Use offline simulator** and **Generate traffic from the approved ICD mappings**, choose waveform, rate, amplitude, engineering center, frequency and seed, then apply and start capture. Sine, square, ramp, constant and seeded noise are supported. Drop/flag intervals permit repeatable fault scenarios. The original demo generator remains available when ICD generation is unchecked.

The generator routes messages to enabled streams and packs every applicable mapping into a message. It converts requested engineering values back to integer, signed integer, float32, float64 or BCD payload fields with the approved scale, offset and word order. Finite payload limits cause quantization/saturation. Overlapping fields are rejected for generation, even though intentional aliases remain valid for decoding. The same waveform parameters are used in each signal's own engineering units. Rate means total records per second, not per signal. This is software simulation and does not exercise a physical bus or BC/RT transmission.

## Automatic checks

Add one row per signal in **Signal checks**. Blank bounds disable those bounds; a zero missing timeout disables its timer. Configure limits in engineering units. Hysteresis controls when an out-of-range event recovers. Missing checks run independently per matching channel and bus, start when that stream is first observed, and count only successfully decoded values. Quiet-stream timers continue while live capture waits for traffic. Offline checks use recorded timestamps, including gaps between consecutive matching records; they do not extrapolate beyond the recording's final timestamp.

Protocol flag transitions, status-word non-address bits and backward card-clock jumps are also recorded. Status bits are reported as observations, not all classified as hardware faults. Clock resets restart the affected stream's timers. Checks observe traffic without modifying raw recording or issuing hardware commands.

**Events** shows the most recent 512 transitions with display filters. The full live event log is `<recording>.checks.jsonl`; the Events CSV exports the retained recent events. Batch JSON keeps up to 10,000 event details per input and explicitly reports truncation and total count. A stream that has never produced traffic is not classified as missing by the signal timer; device connection state remains a separate concern.

## Batch analysis and reporting

Choose up to 100 `.atd` / `.atd2` recordings and an output directory. Stop capture before starting a batch. Each input is limited to 32 MiB because the current ATD2 reader loads one complete recording. Inputs are processed sequentially using a private snapshot. Engineering CSV output is emitted in bounded chunks rather than building an unbounded export string.

A uniquely named output folder contains:

- `report.html`: readable per-recording results and signal min/max/mean, valid and invalid sample counts.
- `batch-report.json`: all input outcomes, CRC/recovery results, profile and input/output SHA-256 hashes, applied checks and detailed events.
- One engineering CSV and one detailed JSON report per successfully analyzed input.

The explicitly selected current profile is applied uniformly to all inputs. Historical sidecar profiles and profile activation timelines are not automatically substituted. A CRC-valid file proves valid stored records, not physical acquisition completeness. Recovered/truncated recordings are labeled incomplete. Missing files and other input errors remain in the overall report; they do not erase successful outputs. Cancel stops at the next bounded operation and preserves already completed reports. Card times beyond 24 hours are retained without wrapping to the start of the day.

## Concise ICD chat and reviewed actions

Open **ICD Assistant** with **Ctrl+J**, select/index the PDF and ask a question. Direct mapping answers give units, scale or a compact conversion definition. Other answers show short relevant evidence snippets with source pages; a configured local model is instructed to answer in at most 80 words. The UI displays the latest two exchanges while retaining the document's conversation history. PDF facts and operator overrides remain distinct: asking about a PDF reports its indexed definition.

To clarify conversion or plotting, enter one explicit action and select **Preview action**:

- `Set TEMP scale to 0.01`
- `TEMP offset is -40`
- `Set TEMP word index to 0` (always zero-based)
- `Set TEMP width to 16`, `Set TEMP lsb to 0`
- `TEMP is signed`
- `Set TEMP bus to B`, `Set TEMP channel to 2`
- `Set TEMP direction to receive`
- `Set TEMP type to float32` (the field must already have width 32 and LSB 0)
- `Set TEMP word order to LSW first`
- `Set TEMP unit to degC`
- `Plot TEMP`

The preview shows the old/new field and resulting conversion formula. **Apply reviewed action** checks that the active profile still matches the preview's starting profile and the signal belongs to the selected ICD. Conversion edits require capture to be stopped. Ambiguous or compound requests are rejected with examples; they do not become partial actions. Plot selection can be applied during live capture.

Accepted clarifications become the active C++ conversion profile used by subsequent capture, engineering values, export and plots. They carry an `operator_note`; the original PDF is untouched. Before/after action audits are saved under the application's local `ReviewedActions` directory. A later PDF reimport can restore its original definition; save a task workspace to retain the reviewed setup.

## Application key gate: prepared, enrollment pending

This development build leaves key enforcement disabled. Windows did not enumerate a Yubico device or PIV smart-card certificate during the read-only checks on 29 October 2026. No key, PIN, certificate or device configuration was changed.

The optional Windows gate supports an enrolled RSA/ECDSA PIV signing certificate through the **Microsoft Smart Card Key Storage Provider**. A release build pins its certificate's DER SHA-256 using the CMake cache value `AEROTRACE_KEY_CERT_SHA256`. This policy cannot be turned off in Preferences or a saved workspace. Protected builds reject the application's test command switches.

Verification signs a fresh random challenge and verifies it against the pinned public certificate. Software key providers are rejected. Once unlocked, verification runs every five seconds; an unsuccessful check locks the application, stops capture/replay and closes document/task dialogs. A proof that has not refreshed expires after ten seconds. Reconnecting requires explicit verification and does not restart capture. Windows handles PIN prompts; the application does not store a PIN. Key touch/PIN policies must permit the chosen periodic verification behavior.

Enrollment requires identifying the actual key, confirming PIV support, selecting or generating an on-card key without overwriting existing credentials, installing its public certificate, and pinning the fingerprint in a dedicated release build. Physical key removal/reinsertion, PIN failure/cancellation, certificate expiry and provider behavior remain unverified until a compatible device is visible. This gate is AeroTrace's own application protection and does not provide or impersonate a Sentinel entitlement for RTPro.

API references: [Yubico PIV platform support](https://developers.yubico.com/PIV/Introduction/Platform_support.html), [Windows private-key acquisition](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptacquirecertificateprivatekey), [hardware signature operation](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptsignhash), [public-key import](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptimportpublickeyinfoex2).

## Validation

`scripts/build.ps1 -Test` runs the existing application, hardware-adapter contract, OCR/retrieval, live conversion, plotting and stress checks alongside the new checks/simulation and GUI automation tests. The automation tests exercise workspace restoration and invalid-input rejection, preview immutability, reviewed conversion, stale-preview rejection, plot selection, batch partial failures/cancellation, and export of card times beyond 24 hours. PDF workflow tests exercise the real Python worker → preview → approved C++ profile path. Synthetic tests do not qualify physical hardware operation or physical key enforcement.
