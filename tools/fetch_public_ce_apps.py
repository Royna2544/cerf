"""Reuse public upstream CE helper binaries for a fork's host-only CI build.

These auxiliary guest tools are copied unchanged. The Windows frontend, Core
and Engine are compiled from this checkout; no private CE toolchain is needed.
"""

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import urllib.request
import zipfile

URL = ('https://github.com/gweslab/cerf/releases/download/8.1.3.669/'
       'CERF-8.1.3-cb265c9-Release-Win32.zip')
SHA256 = '6ed5c8ae8f1337e3f88399db58d5900613f9333a1e7424207e69b3fbe708197b'


def extract(archive, output):
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    if digest.hexdigest() != SHA256:
        raise ValueError('Public CE helper archive SHA256 mismatch')
    output.mkdir(parents=True, exist_ok=False)
    count = 0
    with zipfile.ZipFile(archive) as source:
        for info in source.infolist():
            path = PurePosixPath(info.filename)
            if not path.parts or path.parts[0] != 'ce_apps' or info.is_dir():
                continue
            if path.is_absolute() or '..' in path.parts or '\\' in info.filename:
                raise ValueError(f'Unsafe CE helper path: {info.filename}')
            target = output.joinpath(*path.parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(info) as reader, target.open('xb') as writer:
                shutil.copyfileobj(reader, writer)
            count += 1
    if count != 30:
        raise ValueError(f'Expected30 pinned upstream CE helper files, got{count}')
    print(f'Prepared {count} public CE helpers from upstream8.1.3.669; SHA256={SHA256}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    archive = args.archive or Path('ci/downloads/ce-helpers-upstream.zip')
    if args.archive is None and not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(URL, headers={'User-Agent': 'CERF-fork-build'})
        with urllib.request.urlopen(request, timeout=120) as response, archive.open('xb') as writer:
            shutil.copyfileobj(response, writer)
    extract(archive, args.output)


if __name__ == '__main__':
    main()
