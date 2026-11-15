import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def rejected(builder, inputs, output):
    before = [hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs]
    result = subprocess.run([sys.executable, str(builder), *map(str, inputs), str(output)],
                            capture_output=True, text=True, check=False)
    assert result.returncode != 0, result.stdout
    assert 'Outputs must be distinct new files' in result.stderr, result.stderr
    assert before == [hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs]


def main():
    builder = Path(__file__).with_name('synthetic_nand.py')
    with tempfile.TemporaryDirectory(prefix='cerf-nand-paths-') as directory:
        root = Path(directory)
        inputs = [root / name for name in ('nand.bin.json', 'part00', 'part01', 'part02')]
        for index, path in enumerate(inputs):
            path.write_bytes(bytes([index + 1]) * 4096)
        rejected(builder, inputs, inputs[1])
        output = root / 'nand.bin'
        rejected(builder, inputs, output)
        assert not output.exists()
        output = root / 'new.bin'
        metadata = root / 'new.bin.json'
        metadata.write_bytes(b'existing manifest')
        rejected(builder, inputs, output)
        assert metadata.read_bytes() == b'existing manifest'
        assert not output.exists()
        metadata.unlink()
        metadata.symlink_to(inputs[0])
        rejected(builder, inputs, output)
        assert metadata.is_symlink() and not output.exists()
        metadata.unlink()
        metadata.symlink_to(root / 'missing')
        rejected(builder, inputs, output)
        assert metadata.is_symlink() and not output.exists()
        assert not (root / 'missing').exists()
    print('NAND input and output collision checks passed')


if __name__ == '__main__':
    main()
