# AeroTrace recording and CSV correction — 2 November 2026

## Delivered behavior

- Connect devices discovers and initializes the selected channels without starting acquisition or creating recording files.
- Start recording in Live Monitor begins acquisition and saving. Stop & save finalizes the run and retains the connection. Disconnect saves an active run before releasing the device.
- Application launch, device arrival and configuration changes do not automatically record.
- Installed DDC runtime discovery checks architecture, required exports, driver version evidence and Card Manager assignments. Saved overrides are retained; ambiguous environments require selection. Suggested assignments must match the present physical card.
- Corrected the SDK Windows caller-owned packet-buffer release case returning -68. Other release failures, including SDK-owned buffer failures, remain errors.
- Corrected DDC native clock scaling: ACE_TT_2US means 2000 ns per tick, rather than 100 ns. Original ticks and packet bytes remain preserved.
- Session CSVs contain exactly the 42 named columns in the supplied examples, with per-channel microsecond deltas, HH:MM:SS mmm.uuu time, lowercase hexadecimal words, command descriptions, valid statuses and '-' unused words. The seven unnamed trailing sample fields are omitted as agreed.
- Distinct recordings cannot overwrite an earlier CSV pair. Raw recordings, packet sidecars and integrity manifests remain available.

## Verification

- All 21 Windows CTest tests passed, including the new hardware GUI connection/recording workflow and runtime/assignment rejection checks.
- GUI regression: Connect without acquisition/files; three recording cycles; Stop retains connection; Disconnect saves/releases; reconnect never auto-records; six distinct CSV files retained.
- Physical sustained loopback: SDK BC sample-derived generator on logical 0 / CH1 A, AeroTrace on logical 1 / CH2 A, for 600 seconds. Received 6,041 messages, with no reported hardware loss, overflow, or -68. This receive-stability run preceded the clock conversion correction; do not use its normalized ATD times as a clock reference.
- With corrected clock conversion: three 10-second captures on CH2 received 101 messages each without reopening the connection; reverse direction CH2 A -> CH1 A also received 101 messages in each of three 10-second captures. No reported loss/overflow or SDK errors.
- Independently checked all 12 CSVs from those six runs: 42 fields per row, 606 total messages, expected 10-word 1111/2222/3333/4444 payload, Bus A, correct command description, and exact 100,000 us successive timestamp deltas. Unused counterpart channel files have headers only.
- A further reverse-direction run completed three short cycles (51/50/50 messages), and its generator exited/released successfully.
- Both logical channels opened successfully after all generators stopped. No generator process remained.
- Packaged and actual installed application both passed clean-PATH GUI, PDF runtime, session export, diagnostics ZIP and journal-correlation checks. Installed EXE, Qwt, DDC runtime and manifest hashes matched the verified package. App Paths registration was verified.

## Installed build and evidence on the Windows PC

Project: C:\Users\SES\Documents\AeroTrace1553-0.5.0-Whole-Project

Installed application: C:\Users\SES\AppData\Local\Programs\AeroTrace1553\AeroTrace1553.exe

Installer: dist\installers\AeroTrace1553-RecordingFix-20261102-Setup-x64.exe

Evidence:
- out\build\desktop-local\Testing\Temporary\LastTest.log
- out\build\desktop-local\test-results\hardware-connection\hardware-connection.txt
- out\recordingfix-installed-acceptance\acceptance.json
- out\recordingfix-installed-acceptance\support.zip
- out\investigation-20261102\loopback-ch2-v3-1.atd and its status/events/packet sidecars
- out\investigation-20261102\loopback-clock-cycles-[1-3].atd
- out\investigation-20261102\loopback-reverse-[1-3].atd
- out\investigation-20261102\loopback-csv\ (12 verified CSVs)
- out\investigation-20261102\recordingfix-setup.log

Run from Qt Creator using Desktop Qt 6.11.1 MinGW 64-bit, Release, out/build/desktop-local, target aerotrace_desktop. Use Connect devices, then Start recording in Live Monitor.

## Compatibility and limits

Verified physical combination: Windows 11, Qt 6.11.1 MinGW x64, aceXtreme SDK 3.6.15, acexusb driver 3.6.15.1, BU-67113U. A separate local session-export test build also passed under Qt 6.8.1. Qt 6.12 and other SDK/driver combinations have not been physically qualified. The project retains Qt 6.5 minimum; Qt 6.0 is not supported.

The loopback is passive BC traffic capture; no responding RT was configured, so absent RT status/no-response protocol flags are expected. These tests do not establish long-term driver reliability, every message type, or all SDK ABI combinations.

Windows recorded a 0x10D blue-screen restart during initial setup, followed by stalled opens. After USB reconnection, the sustained and repeated loopback tests passed. The kernel failure's cause was not established; its Windows minidump is C:\Windows\Minidump\110226-3218-01.dmp. Newly written scratch files affected by the restart were restored and source contents compared before rebuilding.
