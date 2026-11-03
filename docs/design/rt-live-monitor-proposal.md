# Live Monitor RT layout — approved side-by-side design

The [interactive local prototype](rt-live-monitor-proposal.html) uses illustrative data. It does not access hardware or change application behavior. The user approved the side-by-side layout on 2 November 2026. Native implementation uses fixed RT rows, independent A/B values, expandable routes, display-only pause and an available chronological view. The HTML/SVG/PNG remain design artifacts; native screenshots and test evidence are recorded separately.

## Row identity and update behavior

- External / CH1 and Internal / CH2 have independent panes, side by side at wide sizes and stacked at narrower sizes. Both capture A and B when enabled.
- One parent row per `(session, channel, RT address)`. RT 03 updates RT 03's row, RT 02 updates RT 02's row. Default numeric RT ordering stays fixed as packets arrive. Selection and expanded state persist.
- Parent rows show independent A and B latest card times and counts, the latest message summary, and response evidence. A traffic must not overwrite B's latest observation or vice versa. Parent response state describes the latest relevant transaction; expanded details retain per-bus status.
- Expand for children keyed by `(channel, RT, SA/mode code, direction, bus, message class)`. A separate chronological raw view remains available; grouping is a display mode and never alters recording.
- Incoming RT-to-RT transactions update participation of both addressed endpoints, while payload inspection uses the interpreted transmitter command. Keep unique message counts separate from participant counts. Broadcast is a separate group, never a responding RT.
- Unknown or malformed command words go to an “Unclassified” group with raw evidence. Never invent an RT address. Preserve all RT addresses 00–30; broadcast uses 31.

## Controls, states and accessibility

- A+B/A/B display controls, RT filtering, pause display, layout selection and expand/collapse. In the prototype, “Receive RT 03 packet” explicitly demonstrates a single-row update.
- Disconnected, connected/silent, recording, stopped, stale, no response, protocol error, incomplete recording and lost-device states must be distinct. RT staleness uses an explicit configured timeout; a quiet B bus is not automatically a failure.
- Frozen display still records. Show display scope, recording totals and hardware loss separately. A row disappearing due to a display filter never resets counters.
- Keyboard: Up/Down selects parent rows; Right/Left expands/collapses; Enter inspects a message. Preserve focus during updates. Native Qt accessible names and text states supplement color; no screen-reader announcement per packet. Reduced-motion preference disables update highlights.
- Display resolution follows the captured timebase. Never merge independent channel clocks without calibration. Last-seen age may use host monotonic time and must be labeled separately from card time.

## Native implementation after approval

Use the existing Qt Widgets/Designer shell and a Qt tree model with bounded RT/message summaries, stable internal IDs and batched owner-thread model updates. Publish RT aggregate snapshots before display thinning. Keep original records and provenance available through indexed disk inspection. Use the existing expandable detail/inspector workflow and accepted neutral palette.

Acceptance: many updates for RT 03 leave its row count and position unchanged; RT 02 updates independently; identical RTs on different channels remain separate; simultaneous A/B retain separate values and counts; row selection survives updates; malformed/broadcast/RT-to-RT cases remain truthful; display filtering and pause never change capture totals.

## Verified native result

The [implemented Qt screen](rt-live-monitor-implemented.png) shows simulated four-stream traffic. Software tests cover stable RT rows, independent buses/channels, expandable routes, retained aggregates through display thinning, display-only pause, exact-message inspection and chronological mode. Physical qualification remains open. Automatic narrow-window stacking, configurable RT staleness, reduced-motion update highlighting and full disk-index navigation from every aggregate are further design work; this proposal is not evidence that those details are already implemented.
