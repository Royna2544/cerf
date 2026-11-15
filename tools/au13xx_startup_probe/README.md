# Au1374 original-ROM startup probe

This opt-in Linux development executable runs the supplied iNavi GN 2010
primary XIP through libCerfEngine. It is an experiment for the Au1300-family
port, not a registered CERF Core board or a frontend release.

The run uses the user's instruction:

> then stop reporting and plug a reasonable value and keep trying okay?

The values below are editable experimental inputs. They are not measured
Au1374 Config register values or verified power-on defaults.

| Input | Default | Basis |
|---|---|---|
| Entry PC | `0x8FC7E004` | Captured bootloader launch |
| PRId | `0x800C8001` | Captured kernel log |
| Status | `0x00400004` | Experimental boot handoff |
| Config0 | `0x80000083` | MIPS32R1, little endian, TLB MMU, Config1 present, K0=3 |
| Config1 | `0x3E63318A` | 32 paired TLB entries, 16 KiB I/D caches, provisional four-way geometry, watch/EJTAG flags, no FPU/MIPS16 |
| CPU rate | 792 MHz | Captured PLL `0x42`, 12 MHz reference |
| POWERCTRL | `2` | Provisional system-bus CPU/4, peripheral CPU/8 |
| CNTRCTRL | `0x120` | Provisional enabled and detected 32.768 kHz source |
| External halfword | `0xFFFF` | Unidentified external window, repeated with correct byte lanes |
| GPIO input levels | Low | Provisional static inputs; PINVAL and level detection use the same level |
| NAND import page | `0x11A0` | Observed first spare read; also `0xDC40 - 0xCAA0` in the captured translation |

SYS frequency registers start from the captured post-OEMInit values:
FREQCTRL0=0, FREQCTRL1=0x40903409, CLKSRC=0x0A0E6287, CPUPLL=0x42,
AUXPLL=0x14, AUXPLL2=0x3F. Using them at handoff is a provisional choice.
Low 256 MiB RAM is zeroed; the first 0x003722E4 Part00 bytes are copied to
PA 0x0FC7E000. No secondary XIP flat copy is made. Boot arguments are zero,
so this run follows the guest's bad-signature fallback, unlike the captured
device boot.

## Build and run

Configure this directory directly with CMake and build its default target,
which includes the startup probe and every regression executable.
`ctest --test-dir <build-directory> --output-on-failure` runs the regressions.
The executable's positional arguments are:

```
au13xx_startup_probe Part00.bin [external16 [Config0 [Config1 [guest_cycle_budget [linear_nand_main_or_raw:path [start_page]]]]]]
```

Unsigned decimal and `0x` values are accepted. The default budget is
1,500,000,000 guest cycles; the maximum accepted budget is 20,000,000,000.
A budget exit is not a successful OS boot. Unknown hardware accesses halt.
Only stdout from this development tool is its experiment log; this does not
change CERF's frontend logging or KernelDebugSink requirements.

## Modeled experiment paths

The host supplies MIPS32R1 CP0 banks, live 1:1 Count ticks, ASID-sensitive
paired TLB translation, TLB instructions and exception entry/return. CP0
latches beyond the exercised startup paths are not a complete Au1374 model.
Count/Compare asserts IP7 at equality and Compare writes acknowledge it.
The GPIC routes the TOY/RTC counter and match edges into the original kernel's
interrupt handlers. Other peripheral interrupt producers are added when modeled.
An Engine idle wait instruction still stops the experiment.

UART0-3 share one model with separate holding FIFOs, receivers and serial
shifters. Guest elapsed cycles drain every completed frame; transmit threshold
and shift-empty status remain distinct. FIFO thresholds, receive timeout,
loopback, modem deltas, autoflow and UART IRQ/DMA request lines are modeled.
No external serial stream is supplied; external modem inputs default to
unasserted levels. GPIC initialization and SYS clock/counter register accesses are
recorded; counter values advance from guest cycles and trim settings. Each counter owns
its fractional progress; unrelated trim/control writes preserve that progress.

The NAND backend supplies Samsung ID EC 76 5A 3F, 131072 pages of 512+16
bytes, three row-address cycles, pointer commands, command-latched random reads, status/read resumption,
program AND behavior and 32-page erases. Sequential block crossings and
command-omitted random block crossings stop with an explicit error. Reads, programs, erases and register
synchronization complete atomically; no guessed operation delay is imposed.
The assumed WP# level permits writes to this volatile backend. All source
files are opened read-only and NAND changes stay in RAM.

An optional logical main-area export is placed linearly at the selected
physical page. Its spare bytes, prefix and tail remain erased. This is not a
recovered FAL map or reconstructed raw NAND image. The guest subsequently
erases this synthetic store and writes fresh FAL metadata. Those writes do
not demonstrate mounting the original logical store.

The `raw:` input instead loads exactly 131072 interleaved 512+16-byte pages.
`synthetic_nand.py` composes the inspected OpenStore MBR and complete Part00,
Part01 and Part02 exports after checking their geometry. Part00 matches the
OpenStore partition exactly; the standalone exFAT exports supply their valid
partition headers and complete contents. The builder assigns provisional linear
sector IDs at page 0x11A0, committed valid FAL metadata and software ECC. It does
not recover the original physical mapping or any original spare bytes.

Ghidra decompilation of the supplied `nandfmd.dll` establishes the eight-byte
metadata layout, scanner state checks and two three-byte ECC codes. The compact
ECC encoder exactly matches execution of the original guest encoder at C02F7D18
for 2048 single-bit basis vectors in each half, 64 random sectors and the four
input headers. The synthetic raw-image run passes flash initialization, runs
the original I2C and SPI initialization. The DMA continuation passes the former
global-register boundary and initializes both SPI DMA channels. The UART
continuation then passes UART0/UART3 module initialization and stops at a USB
control read at PA 0x1402102C in `ohcd.dll`. This is further startup progress,
not a GUI or application boot.

Diagnostic producers cap repeated details per event/register and retain
aggregate counts. Unknown accesses and engine halts remain unsuppressed.

The DMA continuation supplies sixteen descriptor channels, request gating,
standard, literal and compare/branch descriptors, width conversion, byte order,
one- and two-dimensional strides, completion status and the GPIC source 75
level. Transfers use the same RAM and peripheral routes as the CPU. Individual
request bursts complete atomically; a pump yields after 4096 grants with pending
work retained. No measured bus-throughput or operation-latency claim is made.
Requests from peripherals remain inactive until their producer is implemented;
an accessed unknown peripheral still halts. High physical-address extensions and
the newer next-pointer subroutine flag halt explicitly rather than discarding bits.

## Validation and lifecycle

The regression executable exercises NAND ID, 17-bit rows, program/erase,
status-to-read resumption with or without a new address, main/spare random
page reads under a latched command, block-boundary refusal, one-shot B-pointer
reset, two-page main-only import with erased OOB, raw first/last-page main and
spare import, UART elapsed-time FIFO
drainage, nonuniform external byte lanes, guest cycle wrap, synchronous
counter writes, independent fractional counter progress, Random reset on Wired writes, and actual Engine execution
through ASID 1/2/1 at one VA with two different physical programs. Timer cases
exercise Count wrap, Compare acknowledgement, actual IRQ entry and ERET, RTC
edge delivery, masks, pending acknowledgement and match-source priority.
The separate DMA regression checks memory transfers, endian conversion,
decrementing addresses, literal writes, taken/untaken comparisons, both stride
modes, pending IRQ masking/reassertion and level/edge request gating.
The UART regression exercises independent ports, interrupt thresholds, receive
timeout, overflow acknowledgement, break indication, loopback modem deltas,
external CTS flow control, standby phase retention and actual RAM-to-UART-to-RAM
DMA loopback through two channels.

The execution regression rewrites executable RAM through an actual descriptor
and re-executes both cached and uncached aliases. The Engine's public MIPS
bus-master coherence service invalidates the affected native translations;
its standalone regression also covers a write spanning two physical pages.
Armed watchpoints, deferred watch exceptions and reverse-endian user accesses
stop explicitly because these paths are unmodeled. Status feature fields and
WatchHi's declaration of another register pair remain read-only. Mapped physical
addresses retain all PFN bits; translation above the Engine's 32-bit bus limit
halts explicitly. Cause.IV selects the special interrupt vector under both BEV settings.
Clock regressions change CPUPLL, POWERCTRL and UART CLKDIV during active
intervals. DMA regressions retain source and destination burst grants through
FIFO pressure, consume every elapsed throttle request, and distinguish fetch
arbitration from transfer-pool arbitration through two competing UART readers.
GPIO output modes and the set/clear aliases update PINVAL and PINCFG together.
GPIO DMA selectors drive request IDs 28/29 from the selected pin levels.
Level-low IRQ detection agrees with PINVAL before and after repeated input updates.
Receive-break acknowledgement remains pending until a line-status read.
UART bidirectional GE/OD controls and the external TOY/RTC counter source halt
explicitly. BIDIR.GT is retained while GE is zero, when the documented gate is ignored.
Literal payload words are separate from the stride controls. Reserved narrow-width
2D descriptors, 2D decrement, multi-datum decrement and FIFO stride combinations halt. Burstable
external-FIFO addressing remains explicitly unmodeled.

This probe is temporary port scaffolding. Remove this directory once a
registered Core Au1300/iNavi board reproduces these milestones. Transfer
verified behavior into Core's existing CPU, clock, NAND and UART services;
UART TX must then use KernelDebugSink. Retain source and logs together until
that replacement exists. A GUI, shell, driver stack and application boot
have not been established by this experiment.

## References opened

- `references/au13xx/manuals/MIPS32_Vol3_2.00.pdf`: virtual memory and paired
  TLBs (chapter 4), exceptions (chapter 5), CP0 registers (chapter 8), Config
  and Config1 (8.25-8.26). Four-way cache associativity is provisional.
- `references/au13xx/manuals/Samsung_K9F1208U0C_rev1.1.pdf`: geometry (3, 8),
  pointer commands (17), ID table (25), page reads (26), program/erase (30), read status
  and resumption (33), reset (34), printed pages.
- Saved Linux Alchemy `arch/mips/include/asm/mach-au1x00/au1000.h`: Au1300
  peripheral addresses, GPIC registers, NAND ports and SYS counter bits.
- Saved Linux `arch/mips/alchemy/common/clock.c`: PLL multiplier, system bus
  and peripheral clock equations; `clocks-v3.10.c:au1xxx_calc_clock`: Count
  ratio and UART baud equation; `time.c:alchemy_time_init`: RTC trim and
  counter synchronization flags.
- Saved Linux `drivers/tty/serial/8250/8250_core-v3.10.c`: Alchemy register
  layout and UART module enable sequence; `drivers/mtd/nand/au1550nd-v3.10.c`:
  command/address/data ports and controller ready bit.
- Existing CERF Core `src/cpu/r4000/mips_mmu_r4000.cpp`,
  `mips_exception_r4000.cpp` and `src/cpu/mips/mips_cp0_ops.cpp`: local
  architectural integration patterns, especially ASID cache invalidation.
- Original `nk.exe`: GPIC init at 8FC855F0, common ISR at 8FC844C0, RTC
  acknowledgement/tick handler at 8FC843A8 and CPU timer ISR at 8FC84498.
- Original `nandfmd.dll`: page read at C02F1C4C, ECC generation at C02F7D18,
  sector write at C02F25A4 and FAL map scan at C02F5B90. Extracted executable
  images were analyzed with Ghidra; no Shared Source implementation was used.
- Saved Linux `au1xxx_dbdma.h` and `alchemy/common/dbdma.c`: Au1300 request
  IDs, the shared DDMA register/descriptor formats and the Au1300 driver binding.
  `au1000.h` assigns the DDMA interrupt to GPIC source 75.
- `RMI_Au1210_Au1250_Data_Book.pdf`, shared DDMA chapter 4, especially
  registers 4.2, descriptors 4.3 and programming 4.5; these older-family facts
  are cross-checked with the Au1300 Linux binding and original guest accesses.
  Its UART section 9.5 supplies shared FIFO, threshold, timeout, modem and reset
  semantics; the Au1300 bases, pin capabilities and IRQ/request IDs come from
  the Au1300 Linux definitions rather than the older chip's board addresses.
  Printed page 356 defines bidirectional TX gating/open-drain controls; page
  378 defines counter bypass. These unsupported paths are guarded at their writers.
- Original `k.ceddk.dll`: controller init at C0253274, HalInitDmaChannel at
  C0253958, HalStartDMA at C0253E0C and HalStopDMA at C0253E6C.

Linux source was consulted for register facts and equations; its implementation
was not copied. Engine host glue follows CERF's own test-host integration.
