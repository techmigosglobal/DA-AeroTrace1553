# English OCR model

`en_PP-OCRv4_rec_mobile.onnx` is the RapidAI/PaddleOCR English recognition model. It is loaded locally; importing documents never downloads models.

Source: https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/rec/en_PP-OCRv4_rec_mobile.onnx

SHA-256: `e8770c967605983d1570cdf5352041dfb68fa0c21664f49f47b155abd3e0e318`

Model reference: https://github.com/RapidAI/RapidOCR/blob/main/python/rapidocr/default_models.yaml

RapidOCR and PaddleOCR use Apache-2.0. Retain their license and model attribution when distributing the runtime.

## Semantic retrieval

`minilm-int8.onnx` and `minilm-tokenizer.json` are from the official `sentence-transformers/all-MiniLM-L6-v2` repository, revision `321b1453a093b6ed637ddb765600dbdd288610c8` (Apache-2.0).

- Encoder SHA-256: `b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21`
- Tokenizer SHA-256: `be50c3628f2bf5bb5e3a7f17b1f74611b2561a3a27eeab05e5aa30f411572037`
- Model: https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/tree/321b1453a093b6ed637ddb765600dbdd288610c8/onnx

The worker runs the quantized encoder locally with two CPU threads and combines cosine ranking with SQLite FTS5 BM25. These vectors retrieve evidence; they never invent or activate engineering mappings.
