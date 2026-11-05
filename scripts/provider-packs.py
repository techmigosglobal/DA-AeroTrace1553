"""Create, inspect and stage private DDC provider packs without vendor installers.

Only software verification receipts produced after a real build/test/inspection
are accepted. Physical qualification and redistribution are separate evidence.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
from datetime import datetime, timezone

SCHEMA = 'aerotrace.provider-pack.v1'
HEX = re.compile(r'^[a-fA-F0-9]{64}$')
ID = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_file(root, relative):
    if not isinstance(relative, str) or not relative or '\\' in relative or ':' in relative:
        raise ValueError('Pack paths must be slash-separated relative paths')
    parts = PurePosixPath(relative)
    if parts.is_absolute() or any(p in ('..', '.') for p in relative.split('/')):
        raise ValueError('Pack path escapes or is noncanonical')
    candidate = Path(root).joinpath(*parts.parts)
    if candidate.is_symlink() or not candidate.resolve().is_relative_to(Path(root).resolve()):
        raise ValueError('Pack path leaves its root')
    if not candidate.is_file():
        raise ValueError(f'Missing pack file: {relative}')
    return candidate


def x64(path):
    with Path(path).open('rb') as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:2] != b'MZ':
            raise ValueError(f'Not a PE file: {path}')
        stream.seek(struct.unpack_from('<I', header, 60)[0])
        if stream.read(6) != b'PE\0\0\x64\x86':
            raise ValueError(f'Expected x64 PE file: {path}')


def validate(root):
    root = Path(root)
    pack = read_json(root / 'provider-pack.json')
    if pack.get('schema') != SCHEMA or not ID.fullmatch(str(pack.get('id', ''))):
        raise ValueError('Invalid provider-pack identity/schema')
    if pack.get('architecture') != 'x64' or pack.get('provider', {}).get('abi') != 1:
        raise ValueError('Pack requires x64 and provider ABI 1')
    if not pack.get('sdk', {}).get('identity') or not HEX.fullmatch(pack['sdk'].get('header_sha256', '')):
        raise ValueError('SDK identity/header fingerprint missing')
    for field in ('provider', 'runtime'):
        entry = pack[field]
        if not HEX.fullmatch(entry.get('sha256', '')):
            raise ValueError(f'Invalid {field} fingerprint')
        path = safe_file(root, entry.get('path'))
        if digest(path) != entry['sha256'].lower():
            raise ValueError(f'{field} fingerprint mismatch')
        x64(path)
    if not pack['runtime'].get('file_version'):
        raise ValueError('Runtime file version missing')
    receipts = pack.get('receipts', {})
    if any(receipts.get(key) is not True for key in ('compiled', 'contract_tested', 'runtime_inspected')):
        raise ValueError('Actual software verification receipts required')
    matrix = pack.get('compatibility', {}).get('matrix')
    if not isinstance(matrix, list):
        raise ValueError('Explicit finite compatibility matrix required')
    for row in matrix:
        if row.get('physical_qualified') is not True or not row.get('receipt'):
            raise ValueError('Qualification row requires actual physical receipt')
        if not isinstance(row.get('windows_min_build'), int) or not isinstance(row.get('windows_max_build'), int):
            raise ValueError('Qualification row requires a finite OS build range')
        if row['windows_min_build'] > row['windows_max_build']:
            raise ValueError('Invalid qualification OS build range')
        for field in ('driver_version', 'hardware_id'):
            if not row.get(field) or '*' in row[field] or '?' in row[field]:
                raise ValueError('Qualification identifiers must be exact')
    if 'license' in pack:
        license_entry = pack['license']
        if digest(safe_file(root, license_entry['path'])) != license_entry['sha256'].lower():
            raise ValueError('License fingerprint mismatch')
    return pack


def create(args):
    descriptor = read_json(args.descriptor)
    receipt = read_json(args.software_receipt)
    if descriptor.get('schema') != 'aerotrace.provider.v1' or descriptor.get('architecture') != 'x64':
        raise ValueError('Unsupported provider descriptor')
    identity = descriptor['provider']
    if not ID.fullmatch(identity):
        raise ValueError('Invalid provider ID')
    provider_hash, runtime_hash = digest(args.provider), digest(args.runtime)
    if descriptor['runtime_sha256'].lower() != runtime_hash:
        raise ValueError('Descriptor was built for a different runtime')
    if (receipt.get('schema') != 'aerotrace.provider-software-verification.v1'
            or receipt.get('status') != 'PASS'
            or receipt.get('provider_sha256', '').lower() != provider_hash
            or receipt.get('runtime_sha256', '').lower() != runtime_hash
            or any(receipt.get(k) is not True for k in ('compiled', 'contract_tested', 'runtime_inspected'))):
        raise ValueError('Verification receipt does not cover these binaries')
    destination = Path(args.destination)
    if destination.exists():
        raise ValueError('Use a fresh destination; installed packs are never overwritten')
    x64(args.provider); x64(args.runtime)
    destination.mkdir(parents=True)
    (destination / 'runtime').mkdir()
    shutil.copy2(args.provider, destination / Path(args.provider).name)
    shutil.copy2(args.runtime, destination / 'runtime' / 'emacepl.dll')
    shutil.copy2(args.descriptor, destination / (identity + '.json'))
    shutil.copy2(args.software_receipt, destination / 'software-verification.json')
    shutil.copy2(args.license, destination / 'DDC-SDK-LICENSE.txt')
    pack = {
        'schema': SCHEMA, 'abi': 1, 'id': identity, 'architecture': 'x64',
        'sdk': {'identity': descriptor['sdk'], 'version': args.sdk_version,
                'header_sha256': descriptor['header_sha256']},
        'provider': {'path': Path(args.provider).name, 'abi': 1, 'sha256': provider_hash},
        'runtime': {'path': 'runtime/emacepl.dll', 'sha256': runtime_hash,
                    'file_version': args.runtime_version},
        'receipts': {k: receipt[k] for k in ('compiled', 'contract_tested', 'runtime_inspected')},
        'compatibility': {'qualification': 'NOT RUN', 'matrix': []},
        'redistribution': {'receipt': None}, 'qualification': {'receipt': None},
        'license': {'path': 'DDC-SDK-LICENSE.txt', 'sha256': digest(args.license)},
        'created_utc': datetime.now(timezone.utc).isoformat()
    }
    (destination / 'provider-pack.json').write_text(json.dumps(pack, indent=2)+'\n', encoding='utf-8')
    validate(destination)
    print(json.dumps({'pack': str(destination.resolve()), 'id': identity,
                      'software_verification': 'PASS', 'physical_qualification': 'NOT RUN'}))


def stage(args):
    source = Path(args.source)
    pack = validate(source)
    destination = Path(args.destination) / pack['id']
    if destination.exists():
        raise ValueError('Pack already exists; remove/replace through an explicit update workflow')
    if any(p.is_symlink() for p in source.rglob('*')):
        raise ValueError('Symlink entries are not admitted')
    shutil.copytree(source, destination)
    validate(destination)
    print(json.dumps({'installed_pack': str(destination.resolve()), 'restart_application': True}))


def release_gate(args):
    root = Path(args.package_root)
    manifests = sorted((root / 'providers' / 'packs').glob('*/provider-pack.json'))
    reasons = []
    packs = []
    if not manifests:
        reasons.append('No verified private SDK/provider packs are included')
    for path in manifests:
        pack = validate(path.parent)
        packs.append(pack['id'])
        if not pack['compatibility']['matrix']:
            reasons.append(f'{pack["id"]}: physical compatibility matrix has no qualified rows')
        entry = pack.get('redistribution', {}).get('receipt')
        try:
            permission = read_json(safe_file(path.parent, entry))
            if (permission.get('status') != 'APPROVED' or not permission.get('issuer')
                    or permission.get('runtime_sha256', '').lower() != pack['runtime']['sha256']
                    or not permission.get('authorization_reference')):
                raise ValueError('Receipt does not authorize this runtime')
        except (TypeError, KeyError, ValueError, OSError):
            reasons.append(f'{pack["id"]}: vendor redistribution authorization receipt missing or invalid')
    prereq = root / 'ddc-prerequisites.json'
    config = read_json(prereq) if prereq.exists() else {}
    if config.get('schema') != 'aerotrace.ddc-prerequisites.v1':
        reasons.append('Approved DDC prerequisite manifest missing')
    packages = config.get('driver_packages', [])
    def verified_driver(package):
        try:
            if package.get('qualified') is not True or package.get('architecture') != 'x64':
                return False
            entries = package['files']
            if not entries or package['inf'] not in entries or package['catalog'] not in entries:
                return False
            for relative, expected in entries.items():
                if not HEX.fullmatch(expected) or digest(safe_file(root, relative)) != expected.lower():
                    return False
            rights = read_json(safe_file(root, package['rights_receipt']))
            qualified = read_json(safe_file(root, package['qualification_receipt']))
            signature = read_json(safe_file(root, package['signature_receipt']))
            if (rights.get('schema') != 'aerotrace.vendor-redistribution.v1'
                    or rights.get('status') != 'APPROVED' or not rights.get('issuer')
                    or not rights.get('authorization_reference') or rights.get('package_id') != package['id']
                    or rights.get('files') != entries):
                return False
            if (signature.get('schema') != 'aerotrace.driver-signature-verification.v1'
                    or signature.get('status') != 'PASS' or signature.get('package_id') != package['id']
                    or signature.get('files') != entries or not signature.get('verifier') or not signature.get('signer')):
                return False
            if (qualified.get('schema') != 'aerotrace.driver-qualification.v1'
                    or qualified.get('status') != 'PASS' or qualified.get('package_id') != package['id']
                    or qualified.get('driver_version') != package['driver_version']
                    or qualified.get('rtpro_coexistence') is not True
                    or qualified.get('four_stream_capture') is not True
                    or qualified.get('reconnect') is not True or not qualified.get('evidence_reference')):
                return False
            if (qualified.get('windows_min_build') != package['windows_min_build']
                    or qualified.get('windows_max_build') != package['windows_max_build']
                    or not isinstance(package['windows_max_build'], int)
                    or qualified.get('hardware_ids') != package['hardware_ids']
                    or not package['hardware_ids']):
                return False
            return not any('*' in x or '?' in x for x in package['hardware_ids'])
        except (TypeError, KeyError, ValueError, OSError):
            return False
    if not any(p.get('driver_version') == '4.10.2.1' and verified_driver(p) for p in packages):
        reasons.append('Clean-PC 4.10.2.1 signed driver baseline lacks rights/physical acceptance receipts')
    manager = config.get('card_manager', {})
    def verified_manager():
        try:
            recipe = manager['recipe'] or {}
            if recipe.get('mode') != 'private-executable' or recipe.get('path') != 'vendor/ddc/card-manager/ddccm.exe':
                return False
            entries = manager['files']
            if recipe['path'] not in entries:
                return False
            for relative, expected in entries.items():
                if not HEX.fullmatch(expected) or digest(safe_file(root, relative)) != expected.lower():
                    return False
            for field, schema, status in (
                    ('redistribution_receipt', 'aerotrace.vendor-redistribution.v1', 'APPROVED'),
                    ('qualification_receipt', 'aerotrace.card-manager-qualification.v1', 'PASS'),
                    ('dependency_verification_receipt', 'aerotrace.component-dependencies.v1', 'PASS')):
                receipt = read_json(safe_file(root, manager[field]))
                if receipt.get('schema') != schema or receipt.get('status') != status or receipt.get('files') != entries:
                    return False
                if field == 'redistribution_receipt' and (not receipt.get('issuer') or not receipt.get('authorization_reference')):
                    return False
                if field != 'redistribution_receipt' and not receipt.get('evidence_reference'):
                    return False
            x64(safe_file(root, recipe['path']))
            return True
        except (TypeError, KeyError, ValueError, OSError):
            return False
    if not verified_manager():
        reasons.append('Missing-PC Card Manager redistribution/dependency/qualification recipe is unavailable')
    result = {'schema': 'aerotrace.complete-bundle-gate.v1',
              'status': 'BLOCKED' if reasons else 'PASS', 'mode': args.mode,
              'packs': packs, 'reasons': reasons, 'checked_utc': datetime.now(timezone.utc).isoformat()}
    (root / 'release-gate.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    approval = root / 'complete-bundle-approved.json'
    if reasons and approval.exists():
        approval.unlink()  # A stale successful marker cannot authorize a later blocked tree.
    if not reasons and args.mode == 'complete':
        (root / 'complete-bundle-approved.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 2 if reasons and args.mode == 'complete' else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create')
    for name in ('descriptor', 'provider', 'runtime', 'license', 'software-receipt', 'destination', 'sdk-version', 'runtime-version'):
        p.add_argument('--'+name, required=True)
    p.set_defaults(handler=create)
    p = sub.add_parser('validate'); p.add_argument('root')
    p.set_defaults(handler=lambda a: print(json.dumps(validate(a.root), indent=2)))
    p = sub.add_parser('stage'); p.add_argument('--source', required=True); p.add_argument('--destination', required=True)
    p.set_defaults(handler=stage)
    p = sub.add_parser('release-gate'); p.add_argument('--package-root', required=True)
    p.add_argument('--mode', choices=['review', 'complete'], default='review'); p.set_defaults(handler=release_gate)
    args = parser.parse_args()
    try:
        return args.handler(args) or 0
    except (ValueError, KeyError, TypeError, OSError, json.JSONDecodeError) as exc:
        print(f'BLOCKED: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
