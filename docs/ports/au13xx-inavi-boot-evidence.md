# iNavi GN 2010: captured boot evidence

The user supplied the NAND marking and a real-device UART boot transcript on
2026-11-14. This supplements the initial CPU checkpoint. Values below are
observations at their logged stages, not asserted silicon power-on defaults.

## CPU and bootloader handoff

The transcript identifies `iNavi GN 2010`, `Au13xx AA`, PRId `0x800C8001`,
and a CPU frequency of 792 MHz. SYS_CPUPLL is `0x42`. This agrees with the
12 MHz reference and seven-bit PLL multiplier used in the saved Linux clock
implementation: 66 times 12,000,000 is 792,000,000 Hz.

The bootloader reports `Launch 0x8FC7E004`. A native CPU probe starting at that
address executes the primary XIP's four-instruction stub and reaches uncached
`0xAFC82450`. The kernel entry RVA remains useful metadata, but the actual
captured handoff enters through the leading XIP stub.

The log reports `BootArgs are at A00FFC00, SIG = 544f4f42`, followed by a good
signature result. KSEG1 folding places that address at physical `0x000FFC00`.
Only the address and signature are known; the rest of the boot-argument
structure has not been captured or synthesized.

## Native follow-up probe

The probe used the same primary image, memory backing and guarded CP0 storage
as the initial experiment, with two changes from captured evidence: initial
PC `0x8FC7E004` and read-only PRId `0x800C8001`.

| Observation | Guest PC | Trace index |
|---|---|---:|
| Uncached NK entry | `0xAFC82450` | 5 |
| Original kernel.dll entry | `0x8FC8E794` | 1,196,196 |
| PRId read returns `0x800C8001` | `0x8FC8E798` | 1,196,197 |
| Stop: Config0, CP0 register 16, selector 0 | `0x8FC8E7C4` | 1,196,254 |

The PRId boundary is passed. Config0 and Config1 values are still unknown.
The clock values in the UART log are system-controller registers, not those
CP0 configuration registers. The probe remains a bounded CPU experiment;
it models no board peripherals and demonstrates no OS or app boot.
Its temporary source and executable are removed after the experiment.

## Clock and timer observations

| Register | Before PSC0 SPI setup | After setup |
|---|---|---|
| Freqctrl0 | `0x00000000` | `0x00000002` |
| Freqctrl1 | `0x40903409` | `0x40903409` |
| Clksrc | `0x0A0E6287` | `0x0A8E6287` |
| Cpupll | `0x00000042` | `0x00000042` |
| Auxpll | `0x00000014` | `0x00000014` |
| Auxpll2 | `0x0000003F` | `0x0000003F` |

These snapshots follow OEMInit. They must not be copied wholesale into a
power-on reset model or assumed to equal the bootloader handoff state.
`Using RTCTICK tick source` establishes the selected kernel timer source.
The RTC input rate, trim/match settings and interrupt routing are not supplied.

## RAM reservations and display

| Region | Physical base | Size | End, exclusive |
|---|---|---|---|
| MAE | `0x00100000` | `0x01E00000` | `0x01F00000` |
| ITE | `0x01F00000` | `0x00F00000` | `0x02E00000` |
| LCD | `0x02E00000` | `0x00F00000` | `0x03D00000` |

The last endpoint exactly matches the primary ROMHDR's RAMStart
`0x83D00000` after KSEG0 folding. This independently corroborates the boundary
between these reserved buffers and the kernel's RAM arena.

The display driver reports a Samsung 800 by 480 panel at 16 bits per pixel,
and an allocation of `0x001BB000` bytes beginning at physical `0x02E00000`.
The log alone does not establish RGB channel masks, buffer count or scanout
register programming. GPS is opened on COM1 at 9600 baud.

## NAND geometry and translation

The marked part is Samsung `K9F1208U0C-PCB0`. Samsung's K9F1208X0C revision 1.1
datasheet specifies an 8-bit part with:

- 512 data bytes and 16 spare bytes per page.
- 32 pages per erase block, or 16 KiB of data plus 512 spare bytes.
- 4,096 blocks: 64 MiB of data and 2 MiB of spare area.
- 69,206,016 bytes for a complete page-and-spare image.

The existing OpenStore export is 64,225,280 bytes, exactly 3,920 data-block
sizes. Its difference from chip main-area capacity is 176 data-block sizes.
That does not establish a count of bad blocks or how boot/reserved regions
are laid out: it is an exported logical store, not a raw NAND/OOB capture.

The transcript contains `Log2Phys: Logical 0xCAA0 -> Physical 0xDC40`.
This is evidence of nonidentity translation. One pair cannot establish the
full mapping or justify applying a constant offset to the whole image.
The nearby read uses boot-media offset `0x01950000` and length `0x00002000`.
These lines do not establish the secondary XIP's runtime memory placement.

## References

- User-supplied NAND marking and UART transcript, 2026-11-14.
- Primary ROMHDR at Part00 file offset `0x002FF7F8`, RAMStart field.
- [Samsung K9F1208X0C revision 1.1](https://www1.futureelectronics.com/doc/SAMSUNG/K9F1208U0C-PIB0000.pdf),
  printed pages 3 and 8. Saved as
  `references/au13xx/manuals/Samsung_K9F1208U0C_rev1.1.pdf`, SHA-256
  `bf9217e55bdad1d3c1e3eb9462fcdafb02c2a77231f89261c3f3927c3fde226f`.
- Saved Linux `arch/mips/alchemy/common/clock.c`, `alchemy_clk_cpu_recalc`.
- Follow-up native startup log retained with the source checkpoint.
