# Card time, DDC integration and diagnostic evidence

## Time semantics

The display format is **HH:MM:SS.mmm.uuu**: hours, minutes, seconds, milliseconds and microseconds. Hours continue beyond 24 rather than jumping to zero at midnight. Plot ticks use two lines for the same fields; cursor readouts use the complete format and cursor differences use microseconds. Raw nanoseconds and device ticks remain available for analysis.

The adapter explicitly configures relative Chapter 10 time and preserves the hardware counter instead of substituting GUI refresh time or PC wall time. In relative packets, the intra-packet counter occupies 48 bits in an eight-byte field; the high 16 bits are unused. The current DDC adapter requests ACE_TT_2US and records native ticks with a resolution of 2,000 ns. Do not apply a generic 100 ns Chapter 10 assumption to these DDC packets. Original ticks and packet bytes are retained; storage/display precision does not establish hardware accuracy.

Engineering fields in one message share its original DDC message time tag. No separate word-level acquisition times are invented.

Recognized 48-bit rollover extends the presentation/recording timeline while retaining the original tick and packet bytes. Unexpected backward movement remains a diagnostic event; it is not clamped, smoothed or replaced. Source-specific plot histories restart at a backward discontinuity. Unsupported absolute/secondary packet time formats fail explicitly with the original packet preserved.

Engineering and signal plots select one channel/bus stream. They do not combine independent device clocks into an apparently synchronized timeline. Actual bus idle periods, capture losses, resets or an unsynchronized external clock cannot be removed by software. Recorded intervals and backward/repeated timestamps are reported. The largest interval is not proof of a lost message: the protocol/ICD must specify an expected rate before that conclusion is valid. Physical synchronization and timebase accuracy remain part of the user's hardware test.

The combined monitor preserves reception order; independent device times may differ. It does not sort unrelated clocks into a fabricated global timeline. Rollover extension uses the nearest counter epoch and accepts a delayed pre-wrap message without counting a second wrap. Inferring epochs assumes observations are less than half a counter cycle apart; long outages or a hardware reset still need source-context diagnosis.

## DDC API integration

The Windows x64 Release adapter loads the selected compatible aceXtreme runtime and resolves vendor declarations from `stdemace.h`. It chooses legacy MT or MTI from the hardware capability evidence. MTI uses `aceGetLibVersion`, `aceErrorStr`, `aceInitialize`, `aceSetTimeTagRes`, `aceMTIConfigure`, `aceMTIStart`, `aceMTIGetCh10DataPkt`, `aceMTIFreeCh10DataPkt`, `aceMTIDecodeRawMsg`, `aceMtiGetMetrics`, `aceMTIStop` and `aceFree`. Packet reads are nonblocking; the single capture owner writes original packets and decoded records, performs conversions and publishes bounded batches. No BC/RT transmit API is loaded.

Both selected logical devices configure before either starts. Configuration, API results, cancellation, faults and shutdown are logged. Required exports are checked before capture. Tests use a separate fake SDK, never the deployed runtime, and cover partial configuration failure, loss fail-stop, unused time bits, rollover and unsupported time format handling.

Driver presence, binding and Card Manager assignments must be checked on the test machine. The supplied November 2 SES evidence records successful SDK 3.6.15/BU-67113U A-path loopback; older development-host -80 results are historical. See [current status](current-status.md).

## Logs for analysis

| File/location | Contents |
|---|---|
| `Sessions/<capture>.atd.events.jsonl` | UTC epoch milliseconds and host monotonic timestamps; capture configuration; loaded runtime; per-device phases; DDC initialization/configuration/start/stop/free results; profile revisions; one-second health; final/error events. |
| Health records | Recorded count, display skips, actual hardware loss/overflow, queue depth/high-water, maximum batch duration, conversion/invalid counts, and four separate stream clock summaries. |
| Stream clock summaries | Count, first/last nanoseconds, backward and repeated timestamps, largest forward interval. Repeated timestamps can be valid at the selected hardware resolution. |
| `Sessions/<capture>.atd.status.txt` | Final capture integrity, errors, counts and per-stream timing summary. |
| `<capture>.atd`, `.ch1.ch10`, `.ch2.ch10` | Decoded binary recording and original hardware packets. Display thinning does not rewrite these into a falsely continuous recording. |
| `.profile.json`, `.icd.jsonl` | Validated rule snapshot and changes tied to their first raw-record boundary. |
| `%LOCALAPPDATA%/AeroTrace/AeroTrace1553/Logs/application-<run>-<segment>.jsonl` | Structured UTC/elapsed/run/PID/thread/sequence events, severity and Qt category/file/function/line; document and application actions. Segments rotate near 8 MiB, with a bounded writer queue. See [logging](logging.md). |

Run **Collect Diagnostics.cmd** from the project, or the packaged `Collect Diagnostics.cmd`. It creates a local Diagnostics folder containing a read-only device/driver/runtime inventory, application logs and recent capture diagnostics. It does not install drivers, initialize buses or upload information. Capture logs remain per session; retention and large-recording rotation remain operator/release qualification work.
