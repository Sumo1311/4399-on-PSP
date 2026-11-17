from pathlib import Path
import hashlib
import json
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare():
    source = ROOT / 'package/response56'
    target = ROOT / 'package/response56-r24diag'
    assert not target.exists()
    shutil.copytree(source, target)
    original = (target / 'EBOOT.PBP').read_bytes()
    data = bytearray(original)
    assert data[:4] == b'\0PBP'
    offsets = struct.unpack_from('<8I', data, 8)
    base = offsets[0]
    magic, version, keys, values, count = struct.unpack_from('<5I', data, base)
    assert magic == 0x46535000
    found = False
    for i in range(count):
        at = base + 20 + i * 16
        key, kind, size, capacity, value = struct.unpack_from('<HHIII', data, at)
        begin = base + keys + key
        name = data[begin:data.index(0, begin)].decode()
        if name != 'TITLE':
            continue
        label = b'Dad n Me R24 Diag\0'
        assert kind == 0x204 and len(label) <= capacity
        begin = base + values + value
        data[begin:begin + capacity] = label + bytes(capacity - len(label))
        struct.pack_into('<I', data, at + 4, len(label))
        found = True
    assert found and len(data) == len(original)
    assert data[offsets[1]:] == original[offsets[1]:]
    elf = sha(data[offsets[6]:offsets[7]])
    assert elf == sha(original[offsets[6]:offsets[7]])
    (target / 'EBOOT.PBP').write_bytes(data)
    (target / 'SHA256SUMS').write_text(''.join(sha(f.read_bytes()) + '  ' + f.name + '\n'
        for f in sorted(target.iterdir()) if f.is_file() and f.name not in ['SHA256SUMS', 'build-identity.json']))
    identity = json.loads((target / 'build-identity.json').read_text())
    identity['resources'] = {f.name: sha(f.read_bytes()) for f in target.iterdir()
                             if f.is_file() and f.name != 'build-identity.json'}
    identity['version'] = 'DadResponse-response56-r24diag'
    identity['scope'] = 'R24 identical runtime executable with SFO title-only packaging variant for manual physical diagnostics'
    identity['title_patch'] = {'title': 'Dad n Me R24 Diag', 'normal_eboot_sha256': sha(original),
                               'data_psp_unchanged_sha256': elf, 'script': 'work-r24/prepare_diag_package.py'}
    (target / 'build-identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    report = {'status': 'prepared', 'normal_runtime_data_psp_sha256': elf,
              'r24_diag_eboot_sha256': sha(data), 'normal_eboot_sha256': sha(original),
              'all_sections_after_sfo_exact': True}
    (ROOT / 'work-r24/diagnostic-package-check.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    prepare()
