"""Complete a fresh CMake install with app-local DLLs and an audited ZIP.

Run in the foreground after cmake --install; this script does not build or
download dependencies. Windows system DLLs remain supplied by Windows.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zipfile


def pe(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3C)[0]
    if data[offset : offset + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE: {path}')
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    size = struct.unpack_from('<H', data, offset + 20)[0]
    optional = offset + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    sections = []
    for index in range(count):
        entry = optional + size + 40 * index
        virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, entry + 8)
        sections.append((address, max(virtual_size, raw_size), raw))

    def file_offset(rva):
        for address, length, raw in sections:
            if address <= rva < address + length:
                return raw + rva - address
        return rva

    imports = []
    rva = struct.unpack_from('<I', data, directories + 8)[0]
    if rva:
        entry = file_offset(rva)
        while any(data[entry : entry + 20]):
            name = file_offset(struct.unpack_from('<I', data, entry + 12)[0])
            imports.append(data[name : data.index(b'\0', name)].decode('ascii').lower())
            entry += 20
    delayed = struct.unpack_from('<I', data, directories + 13 * 8)[0]
    if delayed:
        entry = file_offset(delayed)
        base = struct.unpack_from(
            '<Q' if magic == 0x20B else '<I', data, optional + (24 if magic == 0x20B else 28)
        )[0]
        while any(data[entry : entry + 32]):
            flags, name = struct.unpack_from('<II', data, entry)
            name = file_offset(name if flags & 1 else name - base)
            imports.append(data[name : data.index(b'\0', name)].decode('ascii').lower())
            entry += 32
    return machine, imports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--vcpkg', type=Path, required=True)
    parser.add_argument('--qt', type=Path, required=True)
    parser.add_argument('--plugins', type=Path, required=True)
    parser.add_argument('--crt', type=Path, required=True)
    parser.add_argument('--sdk-license', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve(strict=True)
    if not (stage / 'lmms.exe').is_file() or args.archive.exists():
        raise ValueError('Require a fresh install and a new archive path')
    helper32 = next(stage.rglob('RemoteVstHost32.exe')).parent
    runtime_dirs = {0x8664: [args.vcpkg / 'bin', args.qt / 'bin', args.plugins], 0x14C: []}
    for machine, arch in ((0x8664, 'x64'), (0x14C, 'x86')):
        runtime_dirs[machine] += sorted((args.crt / arch).glob('Microsoft.VC*.CRT'))
    system_root = Path('C:/Windows')
    debug = re.compile(
        r'^(?:qt6.*d|(?:vcruntime|msvcp|concrt)\d+(?:_\d+)?d(?:_[^.]*)?|ucrtbased)\.dll$', re.I
    )
    # This Windows Qt build imports the OS ICU shim's unversioned functions.
    # CMake's generic resolver can instead copy an incompatible SDK ICU DLL.
    # Windows supplies the shim; do not shadow it with that SDK DLL/data file.
    for name in ('icuuc.dll', 'icudt78.dll'):
        for binary in stage.rglob(name):
            assert binary.resolve().is_relative_to(stage)
            binary.unlink()
    # Some older find modules cache an import library from debug/lib under the
    # same DLL name. Deploy the matching Release DLL before checking imports.
    for binary in stage.rglob('*.dll'):
        machine = pe(binary)[0]
        candidate = next(
            (
                directory / binary.name
                for directory in runtime_dirs.get(machine, [])
                if (directory / binary.name).is_file()
            ),
            None,
        )
        if candidate:
            if pe(candidate)[0] != machine:
                raise ValueError(f'Wrong Release DLL architecture: {candidate}')
            shutil.copy2(candidate, binary)
    pending = [
        p
        for p in stage.rglob('*')
        if p.suffix.lower() in ('.exe', '.dll') and not debug.match(p.name)
    ]
    visited = set()
    manifest = []
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        machine, imports = pe(binary)
        if machine not in runtime_dirs:
            raise ValueError(f'Unsupported machine {machine:x}: {binary}')
        destination = (
            binary.parent
            if binary.suffix.lower() == '.exe'
            else (stage if machine == 0x8664 else helper32)
        )
        directories = [
            binary.parent,
            destination,
            stage if machine == 0x8664 else helper32,
        ] + runtime_dirs[machine]
        for name in imports:
            if debug.match(name):
                raise ValueError(f'Release binary imports Debug DLL {name}: {binary}')
            if name.startswith(('api-ms-', 'ext-ms-')):
                continue
            if name == 'icuuc.dll':
                if not (
                    system_root / ('System32' if machine == 0x8664 else 'SysWOW64') / name
                ).is_file():
                    raise ValueError('Windows ICU support missing')
                continue
            found = next(
                (directory / name for directory in directories if (directory / name).is_file()),
                None,
            )
            if found:
                if pe(found)[0] != machine:
                    raise ValueError(f'Wrong architecture for {name}: {found}')
                target = destination / found.name
                if target != found:
                    shutil.copy2(found, target)
                    print(f'Deploy {found.name} ({machine:x})')
                pending.append(target)
            else:
                system = system_root / ('System32' if machine == 0x8664 else 'SysWOW64') / name
                if not system.is_file():
                    raise ValueError(f'Missing DLL {name}: {binary}')
        manifest.append(
            {
                'path': binary.relative_to(stage).as_posix(),
                'machine': f'{machine:04x}',
                'imports': imports,
                'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            }
        )
    # CMake's existing install rule also copies Debug CRTs. They are not needed
    # by this Release closure. Only remove files inside this fresh staging root.
    for binary in stage.rglob('*.dll'):
        if debug.match(binary.name):
            assert binary.resolve().is_relative_to(stage) and binary not in visited
            binary.unlink()
    for filename, machine in (
        ('RemoteVstHost64.exe', 0x8664),
        ('RemoteVstPlugin64.exe', 0x8664),
        ('RemoteVstHost32.exe', 0x14C),
        ('RemoteVstPlugin32.exe', 0x14C),
    ):
        paths = list(stage.rglob(filename))
        if len(paths) != 1 or pe(paths[0])[0] != machine:
            raise ValueError(f'Missing or incorrect helper: {filename}')
    prohibited = [
        p
        for p in stage.rglob('*')
        if p.suffix.lower() in ('.vst3', '.pdb', '.lib')
        or p.name in ('WaveShellInventory.exe', 'vst3sdk')
    ]
    if prohibited:
        raise ValueError(f'Test/SDK/commercial files in package: {prohibited}')
    licenses = stage / 'licenses'
    licenses.mkdir(exist_ok=True)
    shutil.copy2(args.sdk_license, licenses / 'VST3-SDK-LICENSE.txt')
    qt_licenses = args.qt.parent.parent / 'Licenses'
    if not (qt_licenses / 'LICENSE').is_file():
        raise ValueError('Qt license directory missing')
    for source in qt_licenses.iterdir():
        if source.is_file():
            (licenses / 'Qt').mkdir(exist_ok=True)
            shutil.copy2(source, licenses / 'Qt' / source.name)
    for source in (args.vcpkg / 'share').glob('*/copyright'):
        target = licenses / 'vcpkg' / source.parent.name
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target / 'copyright')
    (stage / 'windows-runtime-manifest.json').write_text(
        json.dumps(
            {
                'version': args.version,
                'binaries': sorted(manifest, key=lambda item: item['path']),
                'manual_skips': [
                    'listening',
                    'commercial activation/certification',
                    'DPI/focus/UI review',
                ],
                'scope': 'VeSTige Instrument / VstEffect Effect; classified discovery only; S6 removed',
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    args.archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.archive, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                archive.write(path, f'{stage.name}/{path.relative_to(stage).as_posix()}')
    digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    args.archive.with_suffix('.zip.sha256').write_text(
        f'{digest}  {args.archive.name}\n', encoding='utf-8'
    )
    print(
        f'PASS {len(manifest)} PE binaries, dependency closure, dual-ABI helpers; {args.archive}; SHA256 {digest}'
    )


if __name__ == '__main__':
    main()
