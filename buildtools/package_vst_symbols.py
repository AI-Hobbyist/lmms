"""Add matching Release LMMS and VST symbols to a replacement ZIP."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
from uuid import UUID
import zipfile


HELPERS = {
    "lmms.exe": "Release/lmms.pdb",
    "plugins/32/RemoteVstPlugin32.exe": "plugins/32/RemoteVstPlugin32.pdb",
    "plugins/RemoteVstPlugin64.exe": "plugins/RemoteVstPlugin64.pdb",
    "plugins/32/RemoteVstHost32.exe": "vst-host/x86/Release/RemoteVstHost.pdb",
    "plugins/RemoteVstHost64.exe": "vst-host/x64/Release/RemoteVstHost.pdb",
}


def executable_identity(data, machine):
    pe, = struct.unpack_from("<I", data, 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    actual_machine, count = struct.unpack_from("<HH", data, pe + 4)
    if actual_machine != machine:
        raise ValueError("unexpected PE architecture")
    optional_size, = struct.unpack_from("<H", data, pe + 20)
    optional = pe + 24
    directories = optional + (96 if machine == 0x14C else 112)
    debug_rva, debug_size = struct.unpack_from("<II", data, directories + 6 * 8)
    for index in range(count):
        section = optional + optional_size + index * 40
        _, rva, size, raw = struct.unpack_from("<IIII", data, section + 8)
        if not (rva <= debug_rva and debug_rva + debug_size <= rva + size):
            continue
        start = raw + debug_rva - rva
        for entry in range(start, start + debug_size, 28):
            kind, size, _, offset = struct.unpack_from("<IIII", data, entry + 12)
            record = data[offset:offset + size]
            if kind == 2 and record.startswith(b"RSDS") and len(record) >= 25:
                guid = UUID(bytes_le=record[4:20])
                age, = struct.unpack_from("<I", record, 20)
                name = Path(record[24:].split(b"\0", 1)[0].decode("utf-8")).name
                return guid, age, name
    raise ValueError("no RSDS CodeView record")


def pdb_identity(data):
    if not data.startswith(b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"):
        raise ValueError("invalid linker PDB signature")
    page_size, _, _, directory_size, _, page_map = struct.unpack_from("<6I", data, 32)
    page_count = (directory_size + page_size - 1) // page_size
    pages = struct.unpack_from(f"<{page_count}I", data, page_map * page_size)
    directory = b"".join(data[page * page_size:(page + 1) * page_size] for page in pages)
    count, = struct.unpack_from("<I", directory)
    lengths = struct.unpack_from(f"<{count}I", directory, 4)
    if count < 2 or lengths[1] < 28 or lengths[1] == 0xFFFFFFFF:
        raise ValueError("missing PDB info stream")
    first_stream_pages = 0 if lengths[0] == 0xFFFFFFFF else (lengths[0] + page_size - 1) // page_size
    info_page, = struct.unpack_from("<I", directory, 4 * (1 + count + first_stream_pages))
    info = info_page * page_size
    age, = struct.unpack_from("<I", data, info + 8)
    return UUID(bytes_le=data[info + 12:info + 28]), age


def sha256(data):
    return hashlib.sha256(data).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args()
    if not args.archive.is_file():
        parser.error("the replacement ZIP must already exist")
    payload = {}
    records = []
    for executable, pdb_path in HELPERS.items():
        pdb = args.build / pdb_path
        exe = args.build / "Release" / executable
        try:
            exe_data = exe.read_bytes()
            pdb_data = pdb.read_bytes()
            machine = 0x14C if executable.startswith("plugins/32/") else 0x8664
            guid, age, name = executable_identity(exe_data, machine)
            if (guid, age) != pdb_identity(pdb_data) or name.casefold() != pdb.name.casefold():
                raise ValueError(f"PDB does not match {pdb}")
        except (OSError, ValueError, struct.error) as error:
            parser.exit(1, f"Cannot package Release symbols for {exe}: {error}\n")
        pdb_entry = Path(executable).with_name(pdb.name).as_posix()
        directory = "symbols/LMMS/" if executable == "lmms.exe" else "symbols/VST/"
        payload[directory + executable] = exe_data
        payload[directory + pdb_entry] = pdb_data
        records.append({
            "ArchiveDirectory": directory,
            "Executable": executable,
            "ExecutableSHA256": sha256(exe_data),
            "PDB": pdb_entry,
            "PDBSHA256": sha256(pdb_data),
            "GUID": str(guid),
            "Age": age,
        })

    prefix = "symbols/VST/"
    with zipfile.ZipFile(args.archive, "a", zipfile.ZIP_DEFLATED) as archive:
        if any(name.startswith((prefix, "symbols/LMMS/")) for name in archive.namelist()):
            raise ValueError(f"Release symbols already present: {args.archive}")
        for path, data in payload.items():
            archive.writestr(path, data)
        archive.writestr(prefix + "symbols-manifest.json", json.dumps(records, indent=2) + "\n")
    with zipfile.ZipFile(args.archive) as archive:
        if archive.testzip() is not None:
            raise ValueError(f"Invalid symbols archive: {args.archive}")
    print(f"Verified {len(records)} Release LMMS and VST executable/PDB pairs: {args.archive}")


if __name__ == "__main__":
    main()
