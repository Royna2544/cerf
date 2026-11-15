from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import math
import re
import struct
import zipfile

parser = argparse.ArgumentParser(description='Create the experimental64MiB FAT16 SD image')
parser.add_argument('archive', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
OUTPUT = args.output
if OUTPUT.exists() or OUTPUT.with_suffix('.json').exists():
    parser.error('Output image and metadata must be new files')
if args.archive.resolve() in (OUTPUT.resolve(), OUTPUT.with_suffix('.json').resolve()):
    parser.error('Output image and metadata must differ from the input')
SECTOR = 512
SECTORS = 131072
START = 2048
SPC = 4
CLUSTER = SECTOR * SPC
PART_SECTORS = SECTORS - START
FAT_SECTORS = 1
while True:
    clusters = (PART_SECTORS - 1 - 32 - 2 * FAT_SECTORS) // SPC
    required = math.ceil((clusters + 2) * 2 / SECTOR)
    if required == FAT_SECTORS:
        break
    FAT_SECTORS = required
assert 4085 <= clusters < 65525
DATA_SECTOR = START + 1 + 2 * FAT_SECTORS + 32


def node(name, parent, directory=True):
    return dict(name=name, parent=parent, children={} if directory else None,
                content=b'', cluster=0, count=0, alias=None)


tree = node('', None)
with zipfile.ZipFile(args.archive) as archive:
    for info in archive.infolist():
        path = PurePosixPath(info.filename)
        assert not path.is_absolute() and '..' not in path.parts
        parent = tree
        for index, part in enumerate(path.parts):
            directory = index + 1 < len(path.parts) or info.is_dir()
            if part not in parent['children']:
                parent['children'][part] = node(part, parent, directory)
            parent = parent['children'][part]
        if not info.is_dir():
            parent['content'] = archive.read(info)


def descendants(parent):
    for child in parent['children'].values():
        yield child
        if child['children'] is not None:
            yield from descendants(child)


def assign_aliases(parent):
    used = set()
    for child in parent['children'].values():
        base, dot, extension = child['name'].rpartition('.')
        if not dot:
            base, extension = child['name'], ''
        base = re.sub(r'[^A-Z0-9_$~!#%&\-{}()@\x27`]', '', base.upper()) or 'FILE'
        extension = re.sub(r'[^A-Z0-9]', '', extension.upper())[:3]
        candidate = base[:8].ljust(8) + extension.ljust(3)
        serial = 1
        while len(base) > 8 or candidate in used:
            suffix = '~' + str(serial)
            candidate = (base[:8 - len(suffix)] + suffix).ljust(8) + extension.ljust(3)
            if candidate not in used:
                break
            serial += 1
        used.add(candidate)
        child['alias'] = candidate.encode('ascii')
        if child['children'] is not None:
            assign_aliases(child)


assign_aliases(tree)
next_cluster = 2
for item in descendants(tree):
    if item['children'] is not None:
        entries = 3 + sum(1 + math.ceil((len(c['name'].encode('utf-16le')) // 2 + 1) / 13)
                          for c in item['children'].values())
        length = entries * 32
    else:
        length = len(item['content'])
    item['count'] = math.ceil(length / CLUSTER)
    if item['count']:
        item['cluster'] = next_cluster
        next_cluster += item['count']
assert next_cluster < clusters + 2


def short_entry(alias, attribute, cluster, size):
    entry = bytearray(32)
    entry[:11] = alias
    entry[11] = attribute
    date = ((2026 - 1980) << 9) | (11 << 5) | 15
    struct.pack_into('<HHHHHHHI', entry, 14, 0, date, date, 0, 0, date, cluster, size)
    return entry


def entries(child):
    alias = child['alias']
    checksum = 0
    for value in alias:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 255
    name = list(struct.unpack('<' + str(len(child['name'].encode('utf-16le')) // 2) + 'H',
                              child['name'].encode('utf-16le'))) + [0]
    name += [0xffff] * ((-len(name)) % 13)
    output = bytearray()
    for number in reversed(range(len(name) // 13)):
        entry = bytearray(32)
        entry[0] = number + 1 | (0x40 if number + 1 == len(name) // 13 else 0)
        entry[11] = 0x0f
        entry[13] = checksum
        part = struct.pack('<13H', *name[number * 13:(number + 1) * 13])
        entry[1:11], entry[14:26], entry[28:32] = part[:10], part[10:22], part[22:]
        output += entry
    attribute = 0x10 if child['children'] is not None else 0x20
    output += short_entry(alias, attribute, child['cluster'],
                          0 if child['children'] is not None else len(child['content']))
    return output


def directory_data(parent):
    output = bytearray()
    if parent is not tree:
        output += short_entry(b'.          ', 0x10, parent['cluster'], 0)
        output += short_entry(b'..         ', 0x10, parent['parent']['cluster'], 0)
    for child in parent['children'].values():
        output += entries(child)
    return output + bytes(32)


image = bytearray(SECTORS * SECTOR)
image[446:462] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 0x0e,
                             b'\xfe\xff\xff', START, PART_SECTORS)
image[510:512] = b'\x55\xaa'
boot = bytearray(512)
boot[:11] = b'\xeb\x3c\x90CERFSD  '
struct.pack_into('<HBHBHHBHHHII', boot, 11, SECTOR, SPC, 1, 2, 512, 0,
                 0xf8, FAT_SECTORS, 63, 255, START, PART_SECTORS)
struct.pack_into('<BBBI11s8s', boot, 36, 0x80, 0, 0x29, 0xce002544, b'INAVI SD   ', b'FAT16   ')
boot[510:512] = b'\x55\xaa'
image[START * SECTOR:(START + 1) * SECTOR] = boot
fat = bytearray(FAT_SECTORS * SECTOR)
struct.pack_into('<HH', fat, 0, 0xfff8, 0xffff)
files = {}
for item in descendants(tree):
    first = item['cluster']
    for index in range(item['count']):
        struct.pack_into('<H', fat, (first + index) * 2,
                         first + index + 1 if index + 1 < item['count'] else 0xffff)
    content = directory_data(item) if item['children'] is not None else item['content']
    if content:
        offset = (DATA_SECTOR + (first - 2) * SPC) * SECTOR
        image[offset:offset + len(content)] = content
    if item['children'] is None:
        parts = [item['name']]
        parent = item['parent']
        while parent is not tree:
            parts.insert(0, parent['name'])
            parent = parent['parent']
        files['/'.join(parts)] = hashlib.sha256(content).hexdigest()
for index in range(2):
    offset = (START + 1 + index * FAT_SECTORS) * SECTOR
    image[offset:offset + len(fat)] = fat
directory = directory_data(tree)
assert len(directory) <= 32 * SECTOR
offset = (START + 1 + 2 * FAT_SECTORS) * SECTOR
image[offset:offset + len(directory)] = directory
OUTPUT.write_bytes(image)
metadata = dict(image=str(OUTPUT), bytes=len(image), sha256=hashlib.sha256(image).hexdigest(),
                source_sha256=hashlib.sha256((args.archive).read_bytes()).hexdigest(),
                partition_start=START, fat_sectors=FAT_SECTORS, data_sector=DATA_SECTOR, files=files)
OUTPUT.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps({key: value for key, value in metadata.items() if key != 'files'}))
