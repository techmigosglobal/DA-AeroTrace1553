import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('packs', Path(__file__).with_name('provider-packs.py'))
packs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packs)


class PackBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        pe = bytearray(128)
        pe[:2] = b'MZ'; struct.pack_into('<I', pe, 60, 80); pe[80:86] = b'PE\0\0\x64\x86'
        (self.root/'provider.dll').write_bytes(pe)
        (self.root/'runtime.dll').write_bytes(pe)
        self.manifest = {'schema': packs.SCHEMA, 'id': 'test-sdk', 'architecture': 'x64',
            'sdk': {'identity': 'test-only', 'header_sha256': '1'*64},
            'provider': {'abi': 1, 'path': 'provider.dll', 'sha256': packs.digest(self.root/'provider.dll')},
            'runtime': {'path': 'runtime.dll', 'sha256': packs.digest(self.root/'runtime.dll'), 'file_version': 'test-only'},
            'receipts': {'compiled': True, 'contract_tested': True, 'runtime_inspected': True},
            'compatibility': {'matrix': []}}
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        (self.root/'provider-pack.json').write_text(json.dumps(self.manifest), encoding='utf-8')

    def test_manual_only_pack_is_valid_without_physical_claim(self):
        self.assertEqual(packs.validate(self.root)['compatibility']['matrix'], [])

    def test_modified_binary_rejected(self):
        (self.root/'runtime.dll').write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'fingerprint mismatch'): packs.validate(self.root)

    def test_parent_path_rejected_even_when_target_exists(self):
        self.manifest['provider']['path'] = '../provider.dll'; self.save()
        with self.assertRaisesRegex(ValueError, 'escapes'): packs.validate(self.root)

    def test_absolute_or_ads_paths_rejected(self):
        for path in ['C:/Windows/a.dll', '/Windows/a.dll', 'provider.dll:stream', 'a/../provider.dll']:
            with self.subTest(path=path), self.assertRaises(ValueError): packs.safe_file(self.root, path)

    def test_string_receipt_is_not_boolean_evidence(self):
        self.manifest['receipts']['contract_tested'] = 'true'; self.save()
        with self.assertRaisesRegex(ValueError, 'verification receipts'): packs.validate(self.root)

    def test_wildcard_cannot_qualify_hardware(self):
        self.manifest['compatibility']['matrix'] = [{'physical_qualified': True, 'receipt': 'fixture',
            'windows_min_build': 17763, 'windows_max_build': 26100,
            'driver_version': '*', 'hardware_id': 'USB\\VID_0000&PID_0000'}]; self.save()
        with self.assertRaisesRegex(ValueError, 'exact'): packs.validate(self.root)

    def test_complete_release_stays_blocked_without_external_receipts(self):
        import argparse
        destination = self.root/'deployment/providers/packs/test-sdk'
        import shutil
        shutil.copytree(self.root, destination, ignore=shutil.ignore_patterns('deployment'))
        args = argparse.Namespace(package_root=str(self.root/'deployment'), mode='complete')
        self.assertEqual(packs.release_gate(args), 2)
        gate = packs.read_json(self.root/'deployment/release-gate.json')
        self.assertEqual(gate['status'], 'BLOCKED')
        self.assertFalse((self.root/'deployment/complete-bundle-approved.json').exists())


if __name__ == '__main__': unittest.main()
