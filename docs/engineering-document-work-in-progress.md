# Engineering document conversion — work in progress

The document worker retains existing PDFium/pdfplumber, RapidOCR and OpenCV processing. PDF ingestion no longer automatically activates extracted mappings: operators must explicitly select rules and activate them. The local example header and word ranges can be captured as review-only evidence, retaining message ID as metadata and excluding reserved words. These draft ranges are not conversion mappings; representation, order, names and scaling require review.

Approved C++ signals may now specify an expected data word count (1–32; 0 means unspecified). Older JSON profiles without the field remain unspecified. Decoding checks the decoded command count and captured payload length. The Qt Designer ICD editor exposes this field.

This work is incomplete. Draft-range editing and publication, actual sample-derived digital/scanned PDF regression fixtures, optional Tesseract comparison, SI-unit handling, expanded plots/table, revision-aware replay, engineering PDF reports, release packaging and physical verification remain outstanding. The 0.6.4 installer predates these changes. See the user-approved plan for full scope.
