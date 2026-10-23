"""Check bundled Windows PE dependencies without developer PATH entries."""
import json
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()

def pe(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset + 4] != b'PE\0\0':
        raise ValueError(str(path))
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    size = struct.unpack_from('<H', data, offset + 20)[0]
    optional = offset + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    imports = struct.unpack_from('<I', data, optional + (112 if magic == 0x20b else 96) + 8)[0]
    sections = []
    for index in range(count):
        pos = optional + size + 40 * index
        virtual_size, address, raw_size, raw_offset = struct.unpack_from('<IIII', data, pos + 8)
        sections.append((address, max(virtual_size, raw_size), raw_offset))
    def mapped(rva):
        for address, length, raw in sections:
            if address <= rva < address + length:
                return raw + rva - address
        return rva
    names = []
    if imports:
        pos = mapped(imports)
        while any(data[pos:pos + 20]):
            name_rva = struct.unpack_from('<I', data, pos + 12)[0]
            name_pos = mapped(name_rva)
            names.append(data[name_pos:data.index(b'\0', name_pos)].decode('ascii'))
            pos += 20
    return machine, names

files = sorted(path for path in root.rglob('*') if path.suffix.lower() in ('.exe', '.dll'))
parsed = {path: pe(path) for path in files}
missing = []
debug = []
for path, (machine, dependencies) in parsed.items():
    for name in dependencies:
        lower = name.lower()
        if lower.endswith('d.dll') and lower.startswith(('qt6', 'msvcp', 'vcruntime', 'ucrtbase')):
            debug.append(f'{path.relative_to(root)} -> {name}')
        candidates = [path.parent / name, root / name]
        if any(candidate.exists() and pe(candidate)[0] == machine for candidate in candidates):
            continue
        if lower.startswith(('api-ms-', 'ext-ms-')):
            continue
        # MSVC runtime must be bundled; other OS DLLs come from Windows itself.
        system = Path('C:/Windows/SysWOW64' if machine == 0x14c else 'C:/Windows/System32') / name
        if not lower.startswith(('msvcp', 'vcruntime', 'concrt')) and system.exists():
            continue
        missing.append(f'{path.relative_to(root)} -> {name}')
result = {'peFiles': len(files), 'lmmsPlugins': len(list((root / 'plugins').glob('*.dll'))),
          'ladspaPlugins': len(list((root / 'plugins/ladspa').glob('*.dll'))),
          'missing': missing, 'debugDependencies': debug}
print(json.dumps(result, indent=2))
assert not missing and not debug, 'Package contains unresolved or debug dependencies.'
assert (root / 'lmms.exe').exists() and (root / 'platforms/qwindows.dll').exists()
assert (root / 'data/locale/zh_CN.qm').exists()
