# Demonstration ICD input kit

Use **AeroTrace-Demo-ICD.pdf** for the normal PDF/OCR/retrieval-to-conversion workflow. The JSON file contains the same nine rules for a direct-import comparison. The image-only `AeroTrace-Demo-ICD-scanned.pdf` exercises OCR; uncertain OCR fields require review. These are synthetic demonstration inputs, not approved equipment ICDs.

1. Open the application. In Device Setup select **Simulator – test traffic**, then explicitly start capture. The simulator periodically publishes the ICD payload on all four channel/bus paths.
2. Drop the digital PDF onto the workspace or use Ctrl+I. Complete validated rules activate automatically. Ask **“TEMP temperature engineering unit”** in the document dialog to inspect a cited answer.
3. Open Engineering. Choose External CH1/A, External CH1/B, Internal CH2/A or Internal CH2/B. The table shows all nine fields, raw hexadecimal words, converted values, units, card time and quality. The eight cards are a summary; READY is the ninth table row.
4. Select a signal and choose **Plot selected signal**, or double-click its row. The signal selector also exposes every active ICD mapping, including READY. The x-axis uses the selected stream's card time.
5. In Live Monitor, Columns → Individual data words exposes Word 0–31. Double-click a word to plot its matching engineering field. An unmapped numeric column plots its raw numeric value. One Plot button also works for the selected cell.
6. Export CSV or stop and open a saved session. Capture logs and original raw recordings remain separate from the bounded display window.

For the worked payload `03E8 1A2B 0B22 00F2 8313 01A2 04DD 0008`, expected values are:

| Field | Engineering value |
|---|---|
| TEMP | 10 degC |
| PRESSURE | 66.99 kPa |
| VOLTAGE | 28.5 V |
| VIBRATION | 2.42 g |
| RPM | 3355.5 RPM |
| CURRENT | 4.18 A |
| PITCH | 12.45 deg |
| STATUS | 8 bits |
| READY | 1 state; word 7, bit 3 |

The running simulator varies most words; the worked values describe this exact payload, not every live row. Rules use zero-based word indices, RT 3, SA 2 and BC-to-RT direction. The CSV contains machine-readable expected values. PDF ingestion creates a local evidence index and validated conversion profile; it does not retrain model weights.

Regenerate with the project's document Python runtime and `scripts/generate-demo-icd.py examples/demo-icd`. `scripts/verify-demo-icd.py` checks the actual digital PDF against the JSON and exercises cited retrieval. The Release CTest `gui_demo_icd_live` exercises import during capture, all nine engineering rows and a Channel 2/Bus B plot of the ninth mapping.
