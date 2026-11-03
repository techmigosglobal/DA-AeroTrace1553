"""Small local ONNX encoder; no model loading from the network."""
from pathlib import Path
import hashlib
import numpy as np
import onnxruntime as ort
ort.disable_telemetry_events()
from tokenizers import Tokenizer

MODEL_HASH = "b941bf19f1f1283680f449fa6a7336bb5600bdcd5f84d10ddc5cd72218a0fd21"
VERSION = "all-MiniLM-L6-v2-int8:" + MODEL_HASH


class Encoder:
    def __init__(self):
        root = Path(__file__).parent / "models"
        model = root / "minilm-int8.onnx"
        if not model.is_file() or not (root / "minilm-tokenizer.json").is_file():
            raise ValueError("Local embedding model is missing; run scripts/setup-documents.ps1")
        with model.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != MODEL_HASH:
                raise ValueError("Embedding model checksum mismatch")
        self.tokenizer = Tokenizer.from_file(str(root / "minilm-tokenizer.json"))
        self.tokenizer.enable_truncation(max_length=256)
        self.tokenizer.enable_padding(pad_id=0, pad_token="[PAD]")
        options = ort.SessionOptions()
        options.intra_op_num_threads = 2
        options.inter_op_num_threads = 1
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        self.session = ort.InferenceSession(str(model), sess_options=options, providers=["CPUExecutionProvider"])
        self.inputs = {entry.name for entry in self.session.get_inputs()}

    def encode(self, texts):
        encoded = self.tokenizer.encode_batch(texts)
        feeds = {"input_ids": np.asarray([item.ids for item in encoded], dtype=np.int64),
                 "attention_mask": np.asarray([item.attention_mask for item in encoded], dtype=np.int64),
                 "token_type_ids": np.asarray([item.type_ids for item in encoded], dtype=np.int64)}
        hidden = self.session.run(None, {key: value for key, value in feeds.items() if key in self.inputs})[0]
        mask = feeds["attention_mask"][..., None]
        pooled = (hidden * mask).sum(axis=1) / np.maximum(mask.sum(axis=1), 1)
        pooled /= np.maximum(np.linalg.norm(pooled, axis=1, keepdims=True), 1e-12)
        return pooled.astype("<f4")
