# CSV and monitor comparison

Use the record `#` and recording session to compare values. CSVs cover the complete session; live filters and the retained-row limit do not restrict them.

The supplied 0.4.0 files are `run1_ext.csv` and `run1_int.csv` in the legacy Sessions/Exports folder. Both match their original 305-record CRC-valid ATD capture with no differences in timestamp, channel/bus, command/status, flags or payload words. Evidence: `out/csv-0.4.0-comparison.json`. Historical live-screen contents were not available to independently verify.

New complete-session CSVs add readable time, RT, SA, direction, word count, quality and session identity while preserving the original raw fields. Explicit 0x-prefixed hex columns prevent spreadsheet numeric inference from changing leading zeros or treating hexadecimal E notation as a decimal exponent. Regression checks compare retained live records with exported CSVs by record identity, including their decoded command fields and raw words. Another test includes 0000, 0001, 0010, 00FF, 1E10 and FFFF.

The default simulator also now updates the status RT address when its command address is overridden for the demo route. This avoids inconsistent command/status addresses within generated records; hardware decoding is unchanged.

A second source-selection issue was corrected: opening an older replay and then returning to Live monitor must not redirect the monitor's CSV save to that replay. Live monitor now exports its own last capture; the Replay page exports its replay; Sessions passes its selected recording explicitly without replacing live-capture identity. The capture ID is visible beside the output settings. A two-session regression uses different record IDs/words to verify both paths.

Empty recordings retain their replay source identity; they never display or export unrelated live data as replay contents.
