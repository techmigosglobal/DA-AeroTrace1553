"""Read PE import tables without loading/executing binaries. Report non-OS imports absent from package."""
from pathlib import Path
import json
import struct
import sys


def imports(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        return []
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0':
        return []
    count, optional_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    directory = optional+(112 if struct.unpack_from('<H',data,optional)[0] == 0x20b else 96)
    import_rva = struct.unpack_from('<I', data, directory+8)[0]
    if not import_rva:
        return []
    sections = []
    for i in range(count):
        header = optional+optional_size+i*40
        size, virtual, raw_size, raw = struct.unpack_from('<IIII', data, header+8)
        sections.append((virtual, max(size,raw_size), raw))
    def offset(rva):
        for virtual,size,raw in sections:
            if virtual <= rva < virtual+size:
                return raw+rva-virtual
        return rva
    position = offset(import_rva)
    result = []
    for _ in range(4096):
        descriptor = struct.unpack_from('<IIIII',data,position)
        if not any(descriptor):
            break
        name = offset(descriptor[3])
        result.append(data[name:data.index(b'\0',name)].decode('ascii').lower())
        position += 20
    return result


def main():
    root = Path(sys.argv[1]).resolve()
    all_files = list(root.rglob('*'))
    names = {p.name.lower() for p in all_files if p.is_file()}
    system = Path('C:/Windows/System32')
    absent, unresolved, scanned = {}, {}, 0
    for path in all_files:
        if path.suffix.lower() not in {'.exe','.dll','.pyd'}:
            continue
        scanned += 1
        for name in imports(path):
            if name in names or name == 'msvcrt.dll' or name.startswith(('api-ms-','ext-ms-')):
                continue
            if name.startswith(('msvcp','msvcr','vcruntime','concrt','vcomp','libiomp','libomp')):
                absent.setdefault(name, []).append(str(path.relative_to(root)))
            elif not (system/name).exists():
                unresolved.setdefault(name, []).append(str(path.relative_to(root)))
    report = dict(scanned=scanned, missing_redistributable_imports=absent, unresolved_non_system_imports=unresolved,
                  caveat='Static direct imports only. Presence elsewhere in package does not prove loader search paths. Installed workflow tests and actual vendor-driver testing remain necessary.')
    output = Path(sys.argv[2]) if len(sys.argv)>2 else root/'dependency-audit.json'
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))
    if absent or unresolved:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
