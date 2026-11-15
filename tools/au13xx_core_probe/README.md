# Au1374 native Core development host

This opt-in Linux host runs the registered `inavi_gn2010` board in libCerfCore.
It uses the same Core and Engine implementations as the frontend. It also
builds the four suites used for23 hardware, display, SD and audio integration
checks. No firmware, applications, NAND identity bytes or map data are bundled.

Build with the normal libCerfCore dependencies installed:

```sh
cmake -S tools/au13xx_core_probe -B build/au13xx-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/au13xx-core -j4
```

The host requires the original primary BINFS partition, a reconstructed NAND
image and its private16-byte `.uid` sidecar, and the64MiB FAT16 SD fixture.
The reconstruction tool at `../au13xx_startup_probe/synthetic_nand.py` consumes
the four inspected logical exports; its synthetic OOB is not an original raw
NAND dump. `create_sd.py INAVI.zip output.img` preserves every archive file
and writes SHA256 metadata. It refuses existing outputs.

Configure the inputs beside the executables, then run from the build directory:

```sh
python3 tools/au13xx_core_probe/prepare_fixture.py build/au13xx-core \
    /path/to/dump_NAND_Part00.bin /path/to/synthetic-nand.bin /path/to/inavi-sd.img
cd build/au13xx-core
ctest --output-on-failure
./au1374_core_host --board-id=inavi_gn2010 --disable-network
```

The SD tests require the deterministic image geometry. The fixture can contain
the supplied app archive or a complete original SD backup within that capacity.
It is not a general removable-media conformance suite. The original inputs
are opened for reading; guest writes stay in volatile emulated storage.

The host stops gracefully after45 host seconds and writes a log, a framebuffer
PPM and LCD registers in the working directory. Set `CERF_AU1374_PROBE_TAP=1`
to select iNavi with a calibrated down/up at50/52 seconds and stop at75 seconds.
Set `CERF_AU1374_PROBE_ACK_MAP_DIALOG=1` as well to acknowledge the observed
map-version dialog at62/64 seconds. These timed taps are development controls
for the inspected launcher layout. They do not replace frontend input.

The original firmware reaches its800x480 launcher and runs the supplied iNavi
executable to its splash screen. The supplied archive lacks `mapinfo.bin`, so
the app shows its map-version dialog and returns to the launcher after OK.
The ALSA-less validation host exercised PCM through the existing silent pacing
backend; audible output has not been verified.

This host remains temporary until the normal frontend reproduces the milestone.
The older Engine-only startup probe is retained as historical diagnostic
scaffolding and must be reconciled during Core acceptance. Independent Core
review and normal frontend acceptance remain outstanding.
