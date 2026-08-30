"""Dump CELL editor IDs from Fallout 4 plugin files.

FO4 compresses most CELL records, so their EDIDs are invisible to a raw string
search.  Walk the GRUP/record tree, inflate anything with the compressed flag,
and pull the EDID subrecord out.

  python cell_edids.py <substring> [plugin.esm ...]
"""
import struct
import sys
import zlib
from pathlib import Path

COMPRESSED = 0x00040000
DATA = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Data")


def subrecords(data):
    """Yield (type, payload). Handles the XXXX large-size override."""
    pos = 0
    override = None
    end = len(data)
    while pos + 6 <= end:
        stype = data[pos:pos + 4]
        size = struct.unpack_from("<H", data, pos + 4)[0]
        pos += 6
        if stype == b"XXXX":
            override = struct.unpack_from("<I", data, pos)[0]
            pos += size
            continue
        if override is not None:
            size = override
            override = None
        yield stype, data[pos:pos + size]
        pos += size


def walk(buf, pos, end, out, wanted):
    while pos + 24 <= end:
        rtype = buf[pos:pos + 4]
        size = struct.unpack_from("<I", buf, pos + 4)[0]

        if rtype == b"GRUP":
            # size includes the 24-byte header
            walk(buf, pos + 24, min(pos + size, end), out, wanted)
            pos += size
            continue

        flags = struct.unpack_from("<I", buf, pos + 8)[0]
        form_id = struct.unpack_from("<I", buf, pos + 12)[0]
        body = buf[pos + 24:pos + 24 + size]
        pos += 24 + size

        if rtype != b"CELL":
            continue

        if flags & COMPRESSED:
            if len(body) < 4:
                continue
            try:
                body = zlib.decompress(body[4:])
            except zlib.error:
                continue

        for stype, payload in subrecords(body):
            if stype == b"EDID":
                edid = payload.split(b"\0")[0].decode("latin1")
                if wanted in edid.lower():
                    out.append((edid, form_id))
                break


def main():
    wanted = sys.argv[1].lower() if len(sys.argv) > 1 else "fishpacking"
    plugins = sys.argv[2:] or [
        "Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm",
        "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm",
    ]

    total = 0
    for name in plugins:
        path = DATA / name
        if not path.is_file():
            continue
        buf = path.read_bytes()
        out = []
        walk(buf, 0, len(buf), out, wanted)
        if out:
            print(f"--- {name} ---")
            for edid, form_id in sorted(out):
                print(f"  {edid}  ({form_id:08X})")
            total += len(out)

    print(f"\n{total} CELL record(s) matching {wanted!r}")


if __name__ == "__main__":
    main()
