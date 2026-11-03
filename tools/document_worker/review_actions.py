"""Explicit, review-only commands. No generated code or arbitrary application calls."""
import copy
import math
import re


def propose(question, profile, document_hash):
    text = question.strip().rstrip(".")
    signals = profile.get("signals", [])
    if not isinstance(signals, list) or not 1 <= len(signals) <= 512:
        raise ValueError("Activate a validated ICD profile first")
    # Full-match grammar deliberately rejects unsupported clauses instead of silently ignoring them.
    selected = next((s for s in signals if re.fullmatch(r"(?:please )?(?:plot|graph|show a plot of)\s+"+re.escape(s["id"]), text, re.I)), None)
    if selected:
        if selected.get("source_hash") != document_hash:
            raise ValueError("The signal is not from the selected ICD")
        return dict(kind="plot", signal=selected["id"], summary=f'Plot {selected["id"]} in {selected["unit"]}.')
    fields = {"scale": "scale", "offset": "offset", "word index": "word", "lsb": "lsb", "width": "width", "rt": "rt", "sa": "sa", "channel": "channel"}
    for signal in signals:
        identifier = re.escape(signal["id"])
        match = re.fullmatch(r"(?:please )?(?:set\s+"+identifier+r"\s+|(?:for\s+)?"+identifier+r",?\s+)(scale|offset|word index|lsb|width|rt|sa|channel)\s*(?:to|is|=)?\s*([-+]?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)", text, re.I)
        signed = re.fullmatch(r"(?:set\s+)?"+identifier+r"\s+(?:is\s+)?(signed|unsigned)", text, re.I)
        choice = re.fullmatch(r"(?:set\s+)?"+identifier+r"\s+(bus|type|direction|word order|unit)\s+(?:to\s+|is\s+|=\s*)?([\w°/%.-]+(?:\s+first)?)", text, re.I)
        if not match and not signed and not choice:
            continue
        if signal.get("source_hash") != document_hash:
            raise ValueError("The signal is not from the selected ICD")
        proposal = copy.deepcopy(profile)
        updated = next(s for s in proposal["signals"] if s["id"] == signal["id"])
        if choice:
            field, word = choice[1].lower(), choice[2]
            choices = {"bus": ("bus", {"a": 0, "b": 1, "either": -1}), "type": ("numeric_type", {k: k for k in ("integer", "float32", "float64", "bcd")}), "direction": ("transmit", {"transmit": True, "receive": False}), "word order": ("lsw_first", {"lsw first": True, "msw first": False})}
            if field == "unit":
                if len(word) > 32:
                    raise ValueError("Unit label is too long")
                key, value = "unit", word
            else:
                key, allowed = choices[field]
                if word.lower() not in allowed:
                    raise ValueError(f"Unsupported {field}; choose "+", ".join(allowed))
                value = allowed[word.lower()]
        elif signed:
            key, value = "signed", signed[1].lower() == "signed"
        else:
            key, value = fields[match[1].lower()], float(match[2])
            if not math.isfinite(value):
                raise ValueError("Use a finite number")
            if key not in {"scale", "offset"}:
                if not value.is_integer():
                    raise ValueError("The index/address must be an integer")
                value = int(value)
                limits = {"word": (0, 31), "lsb": (0, 15), "width": (1, 64), "rt": (0, 31), "sa": (0, 31), "channel": (0, 2)}
                lo, hi = limits[key]
                if not lo <= value <= hi:
                    raise ValueError(f"{key} must be {lo}–{hi}; word indices are zero-based")
        updated[key] = value
        updated["operator_note"] = text
        if updated["lsb"]+updated["width"] > 64 or updated["word"]+(updated["lsb"]+updated["width"]+15)//16 > 32:
            raise ValueError("Bit field exceeds payload")
        numeric = updated.get("numeric_type", "integer")
        if (numeric == "float32" and (updated["width"] != 32 or updated["lsb"])) or (numeric == "float64" and (updated["width"] != 64 or updated["lsb"])) or (numeric == "bcd" and updated["width"] % 4):
            raise ValueError("Field width and encoding disagree")
        unit = updated["unit"]
        summary = (f'{signal["id"]}: {key} {signal.get(key)} → {value}.\n'
                   f'RT {updated["rt"]} / SA {updated["sa"]}; word index {updated["word"]}, LSB {updated["lsb"]}, {updated["width"]} bits.\n'
                   f'Engineering = extracted value × {updated["scale"]} + {updated["offset"]} {unit}.\n'
                   'Operator clarification; verify against the cited ICD before applying.')
        return dict(kind="mapping", signal=signal["id"], profile=proposal, summary=summary)
    raise ValueError('Use one explicit action: "Plot TEMP", "Set TEMP scale to 0.01", "TEMP offset is -40", or "Set TEMP word index to 0". Ambiguous instructions need clarification.')
