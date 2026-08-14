#!/usr/bin/env python3
"""
Generate ShaderDB entries from pipeline trace output.

Usage:
  python scripts/generate_shaderdb.py <trace_dir> <output_dir> [--runtime PreNG]

Workflow:
  1. Enable bTracePipeline=true in CommunityShaders.ini
  2. Launch FO4 with F4SE, visit scenes with lights
  3. Find ShaderDump output under Data/F4SE/Plugins/CommunityShaders/ShaderDump/
  4. Run: python scripts/generate_shaderdb.py <ShaderDump_dir> <output_dir>
  5. Copy the output to package/CommunityShaders/.../ShaderDB/
"""

import os
import sys
import re
import shutil
from pathlib import Path


def parse_shader_txt(txt_path):
    """Parse a ShaderDB .txt metadata file, return dict."""
    meta = {}
    with open(txt_path) as f:
        in_section = False
        for line in f:
            line = line.strip()
            if not line or line.startswith(';') or line.startswith('#'):
                continue
            if line.startswith('['):
                in_section = 'Hunt' in line or 'Dump' in line or 'shaderDump' in line.lower()
                continue
            if line.startswith('/') or line.startswith(']'):
                in_section = False
                continue
            if not in_section:
                continue
            if '=' in line:
                k, v = line.split('=', 1)
                meta[k.strip()] = v.strip()
    return meta


def check_uses_light_cb(asm_path):
    """Check if the shader assembly references a light constant buffer (CB slot 2)."""
    try:
        with open(asm_path, errors='ignore') as f:
            content = f.read()
        # Look for cb2 references in D3D assembly
        if 'cb2[' in content or 'cb2 ' in content:
            return True
        # Also check for common light buffer patterns
        if 'dcl_constantbuffer' in content and 'cb2' in content:
            return True
    except:
        pass
    return False


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    trace_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    runtime = sys.argv[3] if len(sys.argv) > 3 else "Common"

    if not trace_dir.exists():
        print(f"Error: trace directory not found: {trace_dir}")
        sys.exit(1)

    # ShaderDump directory structure: <Runtime>/Pixel/<uid>/
    pixel_dir = trace_dir / runtime / "Pixel"
    if not pixel_dir.exists():
        # Try without runtime prefix
        pixel_dir = trace_dir / "Pixel"
    if not pixel_dir.exists():
        print(f"Error: no Pixel directory found in {trace_dir}")
        sys.exit(1)

    count = 0
    for uid_dir in sorted(pixel_dir.iterdir()):
        if not uid_dir.is_dir():
            continue

        uid = uid_dir.name
        txt_file = uid_dir / f"{uid}.txt"
        asm_file = uid_dir / f"{uid}.asm"

        if not txt_file.exists():
            continue

        meta = parse_shader_txt(txt_file)
        asm_hash = meta.get('asmHash', '0')
        stage_type = meta.get('type', 'PS')

        # Check if this PS uses light CB
        uses_light = False
        if asm_file.exists():
            uses_light = check_uses_light_cb(asm_file)

        if not uses_light:
            continue

        # Create ShaderDB entry
        out_dir = output_dir / runtime / "Pixel" / uid
        out_dir.mkdir(parents=True, exist_ok=True)

        out_txt = out_dir / f"{uid}.txt"
        with open(out_txt, 'w') as f:
            f.write("[pixelHunt]\n")
            f.write(f"uid={uid}\n")
            f.write(f"type={stage_type}\n")
            f.write(f"asmHash={asm_hash}\n")
            # Use 0x prefix if not already present
            if not asm_hash.startswith('0x'):
                f.write(f"asmHash=0x{asm_hash}\n")
            f.write("; Uncomment and adjust path to enable replacement:\n")
            f.write(";shader=Data\\Shaders\\LightLimitFix\\BSLightingLLFConsumerPS.hlsl\n")
            f.write("active=false\n")
            f.write("priority=0\n")

        print(f"  [{runtime}/Pixel/{uid}] uses light CB (asmHash={asm_hash})")
        count += 1

    print(f"\nGenerated {count} ShaderDB entries in {output_dir}")
    print(f"  To activate: edit each .txt, uncomment 'shader=', set 'active=true'")
    print(f"  To deploy: copy {output_dir / runtime} to Data/F4SE/Plugins/CommunityShaders/ShaderDB/")


if __name__ == '__main__':
    main()
