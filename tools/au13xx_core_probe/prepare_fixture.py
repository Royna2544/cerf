import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description='Configure private inputs for the Au1374 Core probe')
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('rom', type=Path)
    parser.add_argument('nand', type=Path)
    parser.add_argument('sd', type=Path)
    args = parser.parse_args()
    rom, nand, sd = (path.resolve(strict=True) for path in (args.rom, args.nand, args.sd))
    uid = Path(str(nand) + '.uid')
    if not uid.is_file() or uid.stat().st_size != 16:
        parser.error('NAND requires a private16-byte .uid sidecar')
    if nand.stat().st_size != 4096 * 32 * 528:
        parser.error('NAND geometry must be4096 blocks of32 pages of512+16 bytes')
    if sd.stat().st_size != 64 * 1024 * 1024:
        parser.error('The SD test fixture must be the deterministic64MiB FAT16 image')
    config = {
        'board': {'id': 'inavi_gn2010'},
        'rom': {'primary': str(rom)},
        'storage': {'nand': str(nand)},
        'additional_packages': {'sd_cards': [{
            'file': str(sd), 'name': 'INAVI supplied files', 'insert_on_launch': True,
        }]},
    }
    directory = args.build_dir / 'devices/au1374'
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / 'cerf.json'
    with target.open('x') as output:
        json.dump(config, output, indent=2)
        output.write('\n')
    print(target)


if __name__ == '__main__':
    main()
