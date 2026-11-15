# Au1374 native Core development host

This opt-in Linux host runs the registered `inavi_gn2010` board in libCerfCore.
It uses the same Core and Engine implementations as the frontend. It also
builds the four suites used for 23 hardware, display, SD and audio integration
checks. No firmware, applications, NAND identity bytes or map data are bundled.

Build with the normal libCerfCore dependencies installed:

```sh
cmake -S tools/au13xx_core_probe -B build/au13xx-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/au13xx-core -j4
```

The host requires the original primary BINFS partition, a reconstructed NAND
image and its private 16-byte `.uid` sidecar, and an SD image.
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

The CTest SD suite requires the deterministic 64 MiB FAT16 fixture. For the full
archive, `create_full_sd.py` builds an 8 GiB FAT32 SDHC image using `mkfs.fat`
and `mcopy`, streaming the extracted files through temporary disk storage.
All 2055 supplied files fit, including the 1.7 GB search database. An independent
FAT reader verified every file's SHA256 against the archive. Run the separate
`au1374_core_sdhc_test` with the 8 GiB fixture; it tests negotiation, CSD2/SCR,
addresses above 4 GiB, whole-card erase and overlay restoration. It is not a
general removable-media conformance suite. Original inputs are opened for
reading; guest writes remain in a volatile sector overlay. Snapshots identify
the complete base image and contain only modified sectors and erase intervals.

The host stops gracefully after 45 host seconds and writes a log, a framebuffer
PPM and LCD registers in the working directory. Set `CERF_AU1374_PROBE_TAP=1`
to select iNavi with a calibrated down/up at 50/52 seconds and stop at 75 seconds.
Set `CERF_AU1374_PROBE_ACK_MAP_DIALOG=1` as well to acknowledge the observed
map-version dialog at 62/64 seconds. These timed taps are development controls
for the inspected launcher layout. They do not replace frontend input.

For longer functional runs, `CERF_AU1374_PROBE_CONTROL` names an append-only
text file and `CERF_AU1374_PROBE_SECONDS` sets the host-time deadline (default 900).
Complete control lines are `down X Y`, `up X Y`, `frame PATH`, `cpu`,
`dump HEX_PA HEX_BYTES PATH`, and `stop`. Frame/dump actions pause the guest.
`CERF_AU1374_PROBE_TRACE` accepts space-separated hexadecimal guest PCs for
bounded diagnostic traces. These controls do not change guest code or registers.

With the full SD dataset the original application loads its map, returns 62
matches for the saved Seoul Station search and calculates a 35.5 km route to
`서울역기차역`. `functional_boot_test.py` repeats that sequence in fresh host
processes, checking actual rendered screens with Tesseract 5/Pillow and the
`kor` and `eng` language data. It verifies route comparison, starts guidance,
checks the destination/distance and red route geometry, records stage timings,
and verifies source hashes after the runs. The 64 MiB app-only fixture still
produces the earlier missing-map dialog.
The ALSA-less validation host exercised PCM through the existing silent pacing
backend; audible output has not been verified.

This host remains temporary until the normal frontend reproduces the milestone.
The older Engine-only startup probe is retained as historical diagnostic
scaffolding and must be reconciled during Core acceptance. Independent Core
review and normal frontend acceptance remain outstanding.
