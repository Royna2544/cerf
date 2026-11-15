import argparse
import hashlib
import json
import struct
from pathlib import Path


def parity_masks():
    result = []
    for address_bit in range(8):
        for value in range(2):
            mask = bytes(255 if (i >> address_bit) & 1 == value else 0 for i in range(256))
            byte = 1 if address_bit < 4 else 0
            bit = 2 * (address_bit % 4) + value
            result.append((int.from_bytes(mask, 'little'), byte, bit))
    for column_bit in range(3):
        for value in range(2):
            column = sum(1 << i for i in range(8) if (i >> column_bit) & 1 == value)
            result.append((int.from_bytes(bytes([column]) * 256, 'little'), 2,
                           2 + 2 * column_bit + value))
    return result


MASKS = parity_masks()


def ecc256(data):
    if len(data) != 256:
        raise ValueError('ECC input must be 256 bytes')
    word = int.from_bytes(data, 'little')
    result = bytearray(3)
    for mask, byte, bit in MASKS:
        result[byte] |= ((word & mask).bit_count() & 1) << bit
    return bytes(result)


def compose(store_path, partition_paths):
    store = bytearray(store_path.read_bytes())
    if len(store) != 125440 * 512 or store[510:512] != b'\x55\xaa':
        raise ValueError('OpenStore geometry or MBR signature differs from the inspected export')
    partitions = []
    for index, path in enumerate(partition_paths):
        entry = 446 + 16 * index
        kind = store[entry + 4]
        start, sectors = struct.unpack_from('<II', store, entry + 8)
        data = path.read_bytes()
        if len(data) != sectors * 512 or start + sectors > len(store) // 512:
            raise ValueError(f'Partition {index} does not match the MBR geometry')
        if index == 0:
            if kind != 0x21 or store[start * 512:(start + sectors) * 512] != data:
                raise ValueError('Part00 differs from the BINFS region in OpenStore')
        else:
            partition_offset, volume_length = struct.unpack_from('<QQ', data, 64)
            if (data[3:11] != b'EXFAT   ' or data[510:512] != b'\x55\xaa' or
                    partition_offset != 0 or volume_length != sectors or data[108:110] != b'\x09\x00'):
                raise ValueError(f'Partition {index} is not the inspected partition-relative exFAT export')
        partitions.append({'index': index, 'type': kind, 'start_lba': start, 'sectors': sectors,
                           'replaced_bytes': sum(a != b for a, b in
                                                 zip(store[start * 512:(start + sectors) * 512], data))})
        store[start * 512:(start + sectors) * 512] = data
    return store, partitions


def validate_outputs(inputs, outputs):
    resolved_inputs = {path.resolve() for path in inputs}
    resolved_outputs = [path.resolve() for path in outputs]
    if len(set(resolved_outputs)) != len(outputs):
        raise ValueError('Outputs must be distinct new files, separate from every input')
    for path, resolved in zip(outputs, resolved_outputs):
        if resolved in resolved_inputs or path.exists() or path.is_symlink():
            raise ValueError('Outputs must be distinct new files, separate from every input')


def main():
    parser = argparse.ArgumentParser(description='Build experimental NAND; no original OOB is recovered')
    parser.add_argument('store', type=Path)
    parser.add_argument('part00', type=Path)
    parser.add_argument('part01', type=Path)
    parser.add_argument('part02', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    inputs = [args.store, args.part00, args.part01, args.part02]
    manifest_path = args.output.with_suffix(args.output.suffix + '.json')
    validate_outputs(inputs, [args.output, manifest_path])
    store, partitions = compose(args.store, inputs[1:])
    image = bytearray(b'\xff') * (131072 * 528)
    start_page = 0x11a0
    for logical in range(len(store) // 512):
        data = store[logical * 512:(logical + 1) * 512]
        offset = (start_page + logical) * 528
        image[offset:offset + 512] = data
        image[offset + 512:offset + 520] = struct.pack('<IBBH', logical, 255, 255, 0xfff9)
        image[offset + 520:offset + 526] = ecc256(data[:256]) + ecc256(data[256:])
    manifest = {
        'synthetic': True,
        'original_physical_mapping_recovered': False,
        'start_page': start_page,
        'metadata_state': '0xfff9: committed, valid; original nandfmd.dll FAL scanner c02f5b90',
        'ecc': 'two 256-byte uncomplemented parity codes; nandfmd.dll c02f7d18',
        'inputs_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        'partitions': partitions,
        'logical_bytes': len(store),
        'raw_bytes': len(image),
        'raw_sha256': hashlib.sha256(image).hexdigest(),
    }
    with args.output.open('xb') as output, manifest_path.open('x') as metadata:
        output.write(image)
        metadata.write(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
