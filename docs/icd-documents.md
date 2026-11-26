# ICD PDF ingestion and review

Import PDF or JSON from ICD Library. In the document center, select multiple PDFs to name an ICD set and put its files in order. An explicitly marked continuation can reuse the preceding table headings. Each evidence row opens its original file and page.

## Supported explicit tables

Recognized headers include signal/name/parameter, RT, SA/subaddress, direction, word index/word number, LSB/bit offset, width/bits, scale/resolution, offset/bias, unit, type and word order. Address columns and conversion rules must be explicit. A valid example is:

| Name | RT | SA | Direction | Word index (0-based) | LSB | Width | Scale | Offset | Unit | Type |
|---|---|---|---|---|---|---|---|---|---|---|
| TEMP | 3 | 2 | Rx | 0 | 0 | 16 | 0.01 | 0 | degC | uint16 |

`Word number` means one-based. An unqualified `Word` column is ambiguous unless the document states the indexing base. Bit ranges such as `15:0` identify LSB and width. Multiword mappings require explicit MSW-first/LSW-first order. Numeric expressions support finite decimals, powers of two and simple fractions; arbitrary formulas are never executed.

Types: signed/unsigned integer fields up to 64 bits, IEEE float32/64 and packed BCD. Optional channel 1/2 and bus A/B restrict matching. Scale/offset, signedness, direction and payload bounds are validated in C++ before activation.

The review grid supports `source_word` values `payload`, `command1`,
`command2`, `status1` and `status2`. Omit the field for a legacy payload
mapping. Command and status fields must fit within 16 bits. On RT-to-RT
routes, use `peer_rt` and `peer_sa` for the opposite command. Set the main
RT/SA to the receiver for command 1 and status 2, and to the transmitter
for payload, command 2 and status 1. Review a generic “response word” against
the specific ICD layout before selecting a source.

## OCR

Embedded text is preferred. Image-only pages render at bounded resolution and use local OCR. Ruled tables undergo cell segmentation before recognition, preserving isolated numeric columns. The worker corrects an averaging bias in the pinned RapidOCR CTC decoder without substituting OCR characters or artificially increasing character probabilities.

Low-confidence but structurally complete rules appear as unchecked candidates. Open the source page, verify the addressing/scale/type, and check accepted candidates before activation. Rows with unresolved fields cannot be selected as mappings. Complete high-confidence mappings also require explicit publication through Review signals. Model confidence alone is not proof of engineering correctness.

An import supports up to 64 PDFs, 100 MiB, 500 pages, and 20,000 searchable chunks in total. Profiles support up to 512 active mappings. Raster size and inference batches are bounded. Password-protected, corrupt, and unsupported documents return an error.

## Local retrieval

Original PDFs are copied to the per-user document store under SHA-256 names. SQLite retains ICD sets, extracted profiles, page text, and local 384-dimensional MiniLM vectors. Existing indexed JSON profiles remain readable. Search combines semantic cosine ranking and FTS5 BM25. Extractive answers show document names, pages, and supporting excerpts.

The review package includes a local model for explanatory answers. Indexed
questions use text and tables during recording. **Ask after recording**
queues full retrieval and generation. New PDF imports also wait for
recording and saving to finish. The Assistant cites source pages and can
return excerpts when there is insufficient evidence for a generated answer.

## Provenance and persistence

The active profile persists in per-user application data. Each capture saves its profile snapshot alongside raw data. Imported rules preserve document hash/name/page, representation, scaling and source constraints.

There is no autonomous retraining or model-directed modification of hardware modes. The import pipeline automates extraction and validated conversion; it does not guarantee that every arbitrary PDF contains a complete, unambiguous ICD.


## Review signals

Open ICD Library → Review signals to enter signals manually or paste CSV.
For a PDF, import it in AeroTrace Assistant, open Mappings and evidence, then choose Review signals. Check specific
complete mappings to stage a subset. Draft PDF fields open in the same grid. Select a row to see its evidence and open the cited page.

Tables with a separate message header and columns for word number, signal ID, unit, word format, and data type preserve the subsystem, message ID, and signal ID together. A row starting at word zero establishes zero-based numbering for that table. Review displays data words one-based and retains the printed numbers in the source evidence.

Choose **Set table conversion** to confirm missing float word order, assign channels to subsystems, and apply identity scaling where the sending device uses it. Review retains edits when re-importing the same source. If two subsystem definitions would decode the same payload field on the same stream, assign separate channels or remove one group before publishing. Untyped rows remain searchable evidence.

AeroTrace Assistant offers **ICD documents** for source evidence and **Published mappings** for the rules Engineering uses. A published rule can appear beside the extracted evidence when the operator has changed it. Mapping changes are applied separately through review.
Save draft keeps unfinished rows separate from the active profile.

To change a published rule, enter an explicit instruction such as
`Set TEMP scale to 0.02` and select **Propose mapping**. Open it from
**ICD Library → Assistant drafts** to compare the original and proposed
rule, inspect the cited PDF, and try representative hex words with the
native converter. Publish after reviewing the preview. Publication waits
until capture and saving have finished.

Copy CSV header from the grid or use the header below.

```csv
id,rt,sa,direction,word_count,data_word,lsb,width,representation,signed,word_order,scale,offset,unit,channel,bus,source_reference
Example signal,3,12,receive,32,1,0,16,integer,true,msw-first,0.1,0,unitless,1,A,Example ICD revision A
```

Use one-based data words, a zero-based LSB, a decimal point, and explicit
units or `unitless`. Use `0` for unspecified expected count, `any` for
unrestricted channel or bus, and `receive` or `transmit` for direction.
Supported representations are `integer`, `float32`, `float64` and `bcd`.
The sample row illustrates syntax only. Verify real values against the ICD.

Preview pasted hexadecimal words or a selected monitor record.

Flags, enumerated numeric fields, and contiguous mask/right-shift expressions can use the existing integer decoder. Expressions outside the supported grammar stay in review.
Publish profile validates every staged row and saves the profile before use.
On Engineering, double-click a signal to plot it. Use the four panel
selectors to pick other signals. Plot time comes from the selected stream.

For Engineering-32-Word-Example.pdf, select Fill example mappings, preview, publish,
and select Offline simulation in Device Setup. Start recording explicitly.
Demo conversions require example provenance. AeroTrace Assistant can answer message layout and word-range questions
directly from extracted evidence. A chat answer never publishes a profile.


In 0.6.7, Review signals prefills document-backed fields and suggests an ID
from the route and field position when the document gives none. Select
cells in one column and choose **Fill selected cells** to apply a shared
value. Review any unresolved fields against the cited page before publishing.

Use **Offline simulation** for the fixture. From Engineering
or Live Plot, **Export signal report** saves the decoded CSV, plot PNG
and provenance for a chosen signal and channel/bus.


## 0.6.8 offline fixture and hardware preview

Use the installed `Offline-Simulation-ICD.pdf` with **Offline simulation**.
The new PDF has explicit rules for RT 03 and RT 08. Older example
profiles remain simulation-only and work with the compatible RT 03 payload.
For real DDC, select a live or replay message and use **Verify against
selected message** in Review signals. Check the route, word count,
encoding, scaling, and units against the approved hardware ICD.


## Subsystem Workspace (0.6.10)

Mapping, Engineering, Live Plot and the dockable assistant share the selected subsystem and stream. Start with Import ICD, choose a group, then Review subsystem. Use Apply conversion preset to inspect bulk changes before applying; missing fields are filled by default. Expand bit fields/calibration for individual edits. Presets are saved per source/group and manual edits survive re-imports. Changed source definitions need explicit acceptance. Publish subsystem merges reviewed rules with the other groups.

Engineering shows raw words, native decoded values and quality. Select a signal to see its formula/source and choose Plot selected signal. The plot opens one trace; Compare signals restores comparison panels. Limits stay in engineering units and are scoped to the selected signal/stream.

In the assistant, choose PDF evidence or Published mappings above the conversation. Complete table answers paginate in groups of 13, with signal IDs, words, units and linked source pages. Use Next/Previous to inspect every result. Document management and offline model diagnostics expand separately. Expand detaches the dock into a larger window.

Prose answers use local evidence retrieval and the bundled offline AeroTrace Assistant when memory/capture pressure permits. The model cannot apply mappings or supply numeric conversion results. Native preview and explicit publication remain authoritative. See [0.7.0 release details](Release-0.7.0.md) for resource limits and qualification boundaries.
