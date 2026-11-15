"""Build an8GiB FAT32 SDHC image from the supplied archive without large RAM buffers."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--mkfs-fat', default='mkfs.fat')
    parser.add_argument('--mcopy', default='mcopy')
    args = parser.parse_args()
    output = args.output.resolve()
    metadata = output.with_suffix('.json')
    if output.exists() or metadata.exists():
        parser.error('Output image and metadata must be new files')
    capacity = 8 * 1024**3
    start = 2048
    files = []
    with tempfile.TemporaryDirectory(prefix='inavi-full-sd-', dir=output.parent) as temp:
        directory = Path(temp)
        with zipfile.ZipFile(args.archive) as archive:
            for info in archive.infolist():
                path = PurePosixPath(info.filename)
                if path.is_absolute() or '..' in path.parts or '\\' in info.filename:
                    raise ValueError(f'Unsafe archive path: {info.filename}')
                target = directory.joinpath(*path.parts)
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                digest = hashlib.sha256()
                with archive.open(info) as source, target.open('xb') as destination:
                    while block := source.read(2 * 1024**2):
                        destination.write(block)
                        digest.update(block)
                files.append({'path': info.filename, 'bytes': info.file_size,
                              'sha256': digest.hexdigest()})
                if len(files) % 100 == 0:
                    print(f'Extracted {len(files)} files', flush=True)
        with output.open('xb') as image:
            image.truncate(capacity)
            mbr = bytearray(512)
            struct.pack_into('<I', mbr, 440, 0x13742010)
            mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 0x0c,
                                       b'\xfe\xff\xff', start, capacity // 512 - start)
            mbr[510:512] = b'\x55\xaa'
            image.write(mbr)
        subprocess.run([args.mkfs_fat, '--invariant', '-F', '32', '-s', '8',
                        '-h', str(start), '-n', 'INAVI', '--offset', str(start),
                        str(output)], check=True)
        env = {**os.environ, 'LC_ALL': 'C.UTF-8'}
        subprocess.run([args.mcopy, '-s', '-m', '-i', f'{output}@@{start * 512}',
                        *map(str, sorted(directory.iterdir())), '::/'], check=True, env=env)
    with metadata.open('x') as stream:
        json.dump({'capacity': capacity, 'partition_start': start, 'filesystem': 'FAT32',
                   'files': files}, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(f'Created {output}: {len(files)} files, {sum(f["bytes"] for f in files)} bytes', flush=True)


if __name__ == '__main__':
    main()
