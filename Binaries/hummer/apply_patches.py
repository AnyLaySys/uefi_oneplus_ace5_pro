#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'patches.json').read_text())
for entry in manifest['patches']:
    source = root / entry['file']
    data = bytearray(source.read_bytes())
    if hashlib.sha256(data).hexdigest() != entry['source_sha256']:
        raise SystemExit(f'Unsupported original binary: {source}')
    for edit in entry['edits']:
        offset = int(edit['offset'], 16)
        before, after = bytes.fromhex(edit['before']), bytes.fromhex(edit['after'])
        if len(before) != len(after) or data[offset:offset + len(before)] != before:
            raise SystemExit(f'Unexpected patch bytes: {source}:{offset:#x}')
        data[offset:offset + len(before)] = after
    if 'append' in entry:
        addition = entry['append']
        payload = bytes.fromhex(addition['data'])
        if len(payload) > addition['size']:
            raise SystemExit(f'Invalid appended section size: {source}')
        data.extend(payload + bytes(addition['size'] - len(payload)))
    if hashlib.sha256(data).hexdigest() != entry['patched_sha256']:
        raise SystemExit(f'Patched hash mismatch: {source}')
    destination = source.with_name(source.stem + '.patched.efi')
    destination.write_bytes(data)
    print(f'Verified {destination.relative_to(root)}')
