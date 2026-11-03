# ICD PDF ingestion and review

Import PDF or JSON from ICD Library. The document center can also search previously imported documents without importing again.

## Supported explicit tables

Recognized headers include signal/name/parameter, RT, SA/subaddress, direction, word index/word number, LSB/bit offset, width/bits, scale/resolution, offset/bias, unit, type and word order. Address columns and conversion rules must be explicit. A valid example is:

| Name | RT | SA | Direction | Word index (0-based) | LSB | Width | Scale | Offset | Unit | Type |
|---|---|---|---|---|---|---|---|---|---|---|
| TEMP | 3 | 2 | Rx | 0 | 0 | 16 | 0.01 | 0 | degC | uint16 |

`Word number` means one-based. An unqualified `Word` column is ambiguous unless the document states the indexing base. Bit ranges such as `15:0` identify LSB and width. Multiword mappings require explicit MSW-first/LSW-first order. Numeric expressions support finite decimals, powers of two and simple fractions; arbitrary formulas are never executed.

Types: signed/unsigned integer fields up to 64 bits, IEEE float32/64 and packed BCD. Optional channel 1/2 and bus A/B restrict matching. Scale/offset, signedness, direction and payload bounds are validated in C++ before activation.

## OCR

Embedded text is preferred. Image-only pages render at bounded resolution and use local OCR. Ruled tables undergo cell segmentation before recognition, preserving isolated numeric columns. The worker corrects an averaging bias in the pinned RapidOCR CTC decoder without substituting OCR characters or artificially increasing character probabilities.

Low-confidence but structurally complete rules appear as unchecked candidates. Open the source page, verify the addressing/scale/type, and check accepted candidates before activation. Rows with unresolved fields cannot be selected as mappings. Complete high-confidence profiles activate automatically. Model confidence alone is not proof of engineering correctness.

Limits: PDF ≤100 MiB, ≤500 pages, ≤20000 chunks/document, ≤512 active mappings, bounded raster size and inference batches. Password-protected, corrupt or unsupported documents return an error rather than a fabricated profile.

## Local retrieval

Original PDFs are copied to the per-user document store under SHA-256 names. SQLite retains document/page text chunks and local 384-dimensional MiniLM vectors. Search fuses semantic cosine ranking and FTS5 BM25. Extractive answers show document names, pages and supporting excerpts; no evidence produces an explicit abstention.

An optional local OpenAI-compatible server may be configured by URL and model name in the dialog. Only loopback HTTP endpoints are accepted, redirects are disabled, and answers cannot activate ICD mappings. The server's own CPU/RAM budget and model quality require separate configuration and evaluation.

## Provenance and persistence

The active profile persists in per-user application data. Each capture saves its profile snapshot alongside raw data. Imported rules preserve document hash/name/page, representation, scaling and source constraints. Reopening a recording restores its saved profile when valid.

There is no autonomous retraining or model-directed modification of hardware modes. The import pipeline automates extraction and validated conversion; it does not guarantee that every arbitrary PDF contains a complete, unambiguous ICD.
