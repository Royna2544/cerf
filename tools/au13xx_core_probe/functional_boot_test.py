"""Exercise original iNavi map, Seoul Station search and route from clean boots.

Requires Pillow, Tesseract5 and kor+eng traineddata. No guest code, registry,
GPS position or map files are patched. Each run starts a new host process with
fresh volatile NAND/SD overlays. Screen checks inspect the actual Core renderer.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageOps


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        while block := source.read(4 * 1024**2):
            digest.update(block)
    return digest.hexdigest()


class Run:
    def __init__(self, args, index):
        self.args = args
        self.directory = args.output / f'run-{index:02d}'
        self.directory.mkdir(parents=True, exist_ok=False)
        self.control = self.directory / 'control.txt'
        self.control.touch()
        self.events = []
        self.sequence = 0
        self.started = time.monotonic()
        self.process = None

    def note(self, stage, **details):
        event = {'stage': stage, 'seconds': round(time.monotonic() - self.started, 2), **details}
        self.events.append(event)
        (self.directory / 'events.json').write_text(json.dumps(self.events, indent=2) + '\n')
        print(self.directory.name, event, flush=True)

    def command(self, line):
        with self.control.open('a') as stream:
            stream.write(line + '\n')

    def alive(self):
        if self.process.poll() is not None:
            raise RuntimeError(f'Guest host exited with {self.process.returncode}')

    def frame(self):
        self.alive()
        self.sequence += 1
        target = self.directory / f'frame-{self.sequence:04d}.ppm'
        self.command(f'frame {target}')
        end = time.monotonic() + 15
        while time.monotonic() < end:
            self.alive()
            try:
                image = Image.open(target)
                image.load()
                if image.size != (800, 480):
                    raise RuntimeError(f'Unexpected panel size {image.size}')
                return image
            except (FileNotFoundError, OSError):
                time.sleep(0.1)
        raise TimeoutError('Native host did not finish a frame capture')

    def text(self, image, box, invert=False):
        area = image.crop(box)
        if invert:
            area = ImageOps.invert(area.convert('L'))
        area = area.resize((area.width * 3, area.height * 3))
        if invert:
            area = ImageOps.expand(area, 20, 255)
        data = io.BytesIO()
        area.save(data, format='PNG')
        result = subprocess.run([self.args.tesseract, 'stdin', 'stdout', '--psm', '6',
                                 '-l', 'kor+eng', '--tessdata-dir', str(self.args.tessdata)],
                                input=data.getvalue(), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=True)
        return result.stdout.decode().strip()

    def tap(self, x, y):
        self.command(f'down {x} {y}')
        time.sleep(2)
        self.command(f'up {x} {y}')
        self.note('tap', x=x, y=y)

    def wait_screen(self, stage, test, seconds, retry=None):
        end = time.monotonic() + seconds
        next_retry = time.monotonic() + 25
        while time.monotonic() < end:
            image = self.frame()
            details = test(image)
            if details is not None:
                image.save(self.directory / f'{stage}.png')
                self.note(stage, **details)
                return image
            if retry is not None and time.monotonic() >= next_retry:
                retry()
                next_retry = time.monotonic() + 25
            time.sleep(5)
        image.save(self.directory / f'{stage}-timeout.png')
        raise TimeoutError(f'Guest did not reach {stage}')

    def exercise(self):
        host = self.directory / 'au1374_core_host'
        shutil.copy2(self.args.host, host)
        for name in ['db.json', 'huds.json']:
            shutil.copy2(self.args.host.parent / name, self.directory / name)
        subprocess.run([sys.executable, str(Path(__file__).with_name('prepare_fixture.py')),
                        str(self.directory), str(self.args.rom), str(self.args.nand),
                        str(self.args.sd)], check=True, stdout=subprocess.DEVNULL)
        env = {**os.environ, 'CERF_AU1374_PROBE_CONTROL': str(self.control),
               'CERF_AU1374_PROBE_SECONDS': '900'}
        for name in ['CERF_AU1374_PROBE_TRACE', 'CERF_AU1374_PROBE_TAP',
                     'CERF_AU1374_PROBE_ACK_MAP_DIALOG']:
            env.pop(name, None)
        with (self.directory / 'console.log').open('wb') as output:
            self.process = subprocess.Popen([str(host), '--board-id=inavi_gn2010',
                                             '--disable-network'], cwd=self.directory,
                                            env=env, stdout=output, stderr=subprocess.STDOUT)
            self.note('cold-boot')
            try:
                self.wait_screen('map', self.map_screen, 360, self.launcher_tap)
                self.tap(765, 82)
                self.wait_screen('search', self.search_screen, 120, lambda: self.tap(765, 82))
                self.tap(750, 96)
                self.wait_screen('search-results', self.results_screen, 120)
                self.tap(698, 450)
                calculated = self.wait_screen('route-calculated', self.calculated_screen, 180)
                if self.route_screen(calculated) is None:
                    self.tap(700, 450)
                self.wait_screen('route', self.route_screen, 90)
            finally:
                if self.process.poll() is None:
                    self.command('stop')
                    try:
                        self.process.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait()
            if self.process.returncode != 0:
                raise RuntimeError(f'Native host exit={self.process.returncode}')
        self.note('passed', host_exit=0)

    def launcher_tap(self):
        image = self.frame()
        red, blue = image.getpixel((100, 160)), image.getpixel((280, 310))
        if red[0] > 100 and red[1] < 70 and blue[2] > blue[0] * 1.2:
            self.tap(100, 160)

    def map_screen(self, image):
        text = self.text(image, (739, 185, 790, 233), invert=True)
        if '통계' in text and ('OFF' in text or '0FF' in text):
            return {'panel': [800, 480], 'text': text}
        return None

    def search_screen(self, image):
        text = self.text(image, (280, 0, 470, 35))
        if '퀵검색' in re.sub(r'\s+', '', text):
            return {'text': text}
        return None

    def results_screen(self, image):
        text = self.text(image, (105, 0, 618, 64))
        if '검색결과(서울역)' in re.sub(r'\s+', '', text):
            return {'query': '서울역', 'text': text}
        return None

    def route_screen(self, image):
        text = self.text(image, (320, 425, 715, 480))
        compact = re.sub(r'\s+', '', text)
        red = self.red_pixels(image)
        if '35.5' in text and '서울역기차역' in compact and red > 3000:
            return {'destination': '서울역기차역', 'distance_km': 35.5,
                    'route_red_pixels': red, 'text': text}
        return None

    @staticmethod
    def red_pixels(image):
        data = image.crop((0, 50, 730, 425)).convert('RGB').tobytes()
        return sum(r > 170 and g < 100 and b < 70
                   for r, g, b in zip(data[0::3], data[1::3], data[2::3]))

    def calculated_screen(self, image):
        navigation = self.route_screen(image)
        if navigation is not None:
            return {**navigation, 'view': 'navigation'}
        summary = self.text(image, (0, 52, 166, 133))
        button = self.text(image, (606, 434, 793, 476))
        red = self.red_pixels(image)
        if '35.5' in summary and '안내시작' in re.sub(r'\s+', '', button) and red > 3000:
            return {'view': 'comparison', 'distance_km': 35.5, 'route_red_pixels': red,
                    'summary': summary, 'button': button}
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['host', 'rom', 'nand', 'sd', 'tessdata', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runs', type=int, default=2)
    parser.add_argument('--tesseract', default='tesseract')
    args = parser.parse_args()
    for name in ['host', 'rom', 'nand', 'sd', 'tessdata']:
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    inputs = [args.rom, args.nand, Path(str(args.nand) + '.uid'), args.sd]
    hashes = {str(path): sha256(path) for path in inputs}
    runs = []
    result = {'input_sha256': hashes, 'runs': runs, 'passed': False}
    try:
        for index in range(1, args.runs + 1):
            run = Run(args, index)
            run.exercise()
            runs.append(run.events)
        after = {str(path): sha256(path) for path in inputs}
        if after != hashes:
            raise RuntimeError('A read-only source input changed')
        result['inputs_unchanged'] = True
        result['passed'] = True
    finally:
        (args.output / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
