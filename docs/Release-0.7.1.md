# AeroTrace 1553 0.7.1 bench candidate

0.7.1 adds manual command definitions and live engineering charts. The bench
installer is for controlled evaluation. See the accompanying verification
receipt for software, physical card, and upgrade test results.

## Manual engineering workflow

1. Open **ICD Library → Manual mappings**. Add a command and enter the subsystem,
   RT, subaddress, direction at the RT and expected payload length. Select a
   channel or bus only if the definition is restricted to that stream.
2. Add the fields you need. Use one-based payload word numbers and a zero-based
   bit offset. For a multiword field, select the order documented by the sender.
   Enter the unit and optional scale and offset.
3. Enter representative hexadecimal words and an independently expected
   engineering value. Add an absolute tolerance when the reference is rounded.
   **Check examples** displays the result from the live decoder.
4. Check the subsystems and commands to activate. **Publish selected commands**
   validates the batch while capture is idle. Use **Save drafts** to resume later.
5. Start recording. Select a raw message and choose **Live Engineering** to
   open the matching command and stream. Each field has its own scale and shares
   the selected stream’s time window. Scroll to see additional plots.

An example command with 32 words can map word 3 to unsigned temperature
(raw `0064`, scale 0.5, offset −10, expected 40 °C), word 4 to a signed
integer (raw `FFFE`, expected −2), and words 5–6 to MSW-first float32
(raw `4148 0000`, expected 12.5). These are synthetic examples.

## Offline PDF Assistant

Attach PDFs and ask questions in the separate Assistant. Indexed table
answers and text excerpts link to their source pages. Scans use local OCR.
Manual mapping publication uses its own editor. The package omits generative
and semantic embedding models and keeps existing indexed text and chat history.

## Software verification

All 40 configured regression checks passed. The 32-word fixture verified the
three example conversions through the simulator and the DDC SDK fixture.
The final 75-second four-stream run recorded and checkpointed 3,770,509
messages at 49,993 messages/s with all 32 chart histories active. The ATD and
paired CSVs matched exactly. GUI heartbeat was 33 ms at p95 and 160 ms maximum.
The earlier runs identified a conversion backlog and long GUI paint delays.
The final build prepares field encodings once per batch and bounds monitor
repaints while live charts are open.

See the [native suite](../out/test-results/071-20261122-211243/native.xml)
and [capture test](../out/test-results/071-20261122-211243/capture/performance.json).
The physical BU-67113U conversion check remains pending.

## Windows x64 bench installer

`AeroTrace1553-0.7.1-Bench-Setup-x64.exe` is 126,350,896 bytes. Its
SHA-256 is `DD527C4F65E623C6A8419060E6380C8A6C5856A3300A011E399DC22853DFAD86`.
The package passed 18 document-worker tests plus the manual workflow, PDF
Assistant, and startup checks at 100% and 150% scaling. Actual installation
and upgrade tests remain pending.
[Installer verification receipt](../dist/installers/AeroTrace1553-0.7.1-Bench-Setup-x64.exe.receipt.json)
