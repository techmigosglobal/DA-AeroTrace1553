"""Validate and stage a software-tested x86 helper pack; never runs hardware."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys

spec = importlib.util.spec_from_file_location('provider_packs', Path(__file__).with_name('provider-packs.py'))
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)

def validate(root):
    root = Path(root)
    m = p.read_json(root/'helper-pack.json')
    if m.get('schema') != 'aerotrace.ddc-helper-pack.v1' or m.get('architecture') != 'x86' or m.get('protocol') != 1 or m.get('fixture'):
        raise ValueError('Unsupported or test-only helper pack')
    if not p.ID.fullmatch(m.get('id', '')):
        raise ValueError('Invalid helper pack identity')
    for key in ('helper', 'runtime'):
        entry = m[key]
        path = p.safe_file(root, entry['path'])
        if not p.HEX.fullmatch(entry['sha256']) or p.digest(path) != entry['sha256'].lower():
            raise ValueError('Helper/runtime fingerprint mismatch')
        with path.open('rb') as f:
            header = f.read(64)
            if len(header) != 64 or header[:2] != b'MZ': raise ValueError('Invalid helper PE')
            f.seek(struct.unpack_from('<I', header, 60)[0])
            if f.read(6) != b'PE\0\0\x4c\x01': raise ValueError('Helper requires x86 PE')
    receipt = p.read_json(root/'software-verification.json')
    if receipt.get('status') != 'PASS' or not all(receipt.get(k) is True for k in ('compiled','wire_contract','fixture_integration','runtime_inspected')):
        raise ValueError('Missing successful helper software verification')
    for key in ('helper','runtime'):
        if receipt.get(key+'_sha256') != m[key]['sha256']: raise ValueError('Stale helper receipt')
    if receipt.get('header_sha256') != m['sdk']['header_sha256']: raise ValueError('Stale header receipt')
    if not m.get('receive_modes') or set(m['receive_modes']) - {'Auto','MTI','LegacyMT'}: raise ValueError('Invalid modes')
    if not isinstance(m.get('compatibility',{}).get('matrix'),list): raise ValueError('Missing qualification matrix')
    return m

if __name__ == '__main__':
    try:
        root = Path(sys.argv[1]); m = validate(root)
        if len(sys.argv)>2:
            target = Path(sys.argv[2])/m['id']
            target.mkdir(parents=True, exist_ok=False)
            for name in ('helper-pack.json','software-verification.json','DDC-SDK-LICENSE.txt',m['helper']['path'],m['runtime']['path']):
                source = p.safe_file(root,name); out = target/name; out.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,out)
            validate(target)
        print('Helper pack software verification passed; physical qualification remains separate')
    except (ValueError,KeyError,OSError,IndexError) as e:
        print(str(e),file=sys.stderr);sys.exit(1)
