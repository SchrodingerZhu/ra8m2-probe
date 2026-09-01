# ra8m2-probe

Bare-metal probe firmware + Nix dev shell for the Renesas **RA8M2** (Arm **Cortex-M85**),
built with the LLVM **Arm Toolchain for Embedded (ATfE)** and driven over the on-board
SEGGER J-Link OB. The firmware reads every CPU feature-identification register, prints
the raw value, every bit-field (hex / binary / meaning) and a consolidated feature
summary over SEGGER RTT.

Tested on an EK-RA8M2 (`R7KA8M2AF`). Full output: [`fw/dump.txt`](fw/dump.txt).

## What the silicon reports

| Register | Raw | Decoded |
|---|---|---|
| CPUID | `0x411FD231` | **Cortex-M85 r1p1**, Arm Ltd |
| ID_PFR0 / ID_PFR1 | `0x20000030` / `0x00000230` | T32 Thumb-2; **RAS** extension; **TrustZone-M + NS FP/MVE state handling** (Armv8.1-M) |
| ID_DFR0 | `0x10200000` | Armv8.1-M debug; **Unprivileged Debug Extension**; PerfMon = 0 |
| ID_MMFR0 | `0x00111040` | PMSAv8 MPU, TCM, ACTLR, two shareability levels |
| ID_ISAR0–4 | `0x01103110` `0x02212000` `0x20232232` `0x01111131` `0x01310132` | CLZ, bitfield, CBZ, SDIV/UDIV, MOVW/MOVT, LDRD/STRD, PLD/PLI/PLDW, UMAAL, RBIT, **DSP/SIMD**, TBB/TBH, LDREX/STREX (+B/H), unprivileged loads/stores, DMB/DSB/ISB |
| ID_ISAR5 | `0x00400000` | **PACBTI[23:20] = 0x4 → Pointer Authentication + Branch Target Identification implemented** |
| MVFR0 / MVFR1 / MVFR2 | `0x10110221` / `0x12100211` / `0x00000040` | SP + DP FPU, 16 D-regs, VDIV/VSQRT, all rounding modes, FtZ/DN, **FMA**, **FP16**, **MVE integer + float (Helium)**, VSEL/VMAXNM/VRINT* |
| CLIDR / CTR | `0x09200003` / `0x8303C003` | separate L1 I/D, 32-byte lines, PIPT |
| CCSIDR (I / D) | `0xF01FE009` / `0xF00FE019` | **16 KiB I-cache 2-way (256 sets)**, **16 KiB D-cache 4-way (128 sets)**, WA/RA/WB/WT |
| MPU_TYPE (S / NS) | `0x00000800` / `0x00000800` | **8 secure + 8 non-secure MPU regions** |
| SAU_TYPE | `0x00000008` | **8 SAU regions** (SAU disabled at reset) |
| ICTR | `0x00000002` | up to **96 NVIC interrupt lines** |
| PMU_TYPE | `0x00A05F08` | **8 × 32-bit event counters + cycle counter** (ID_DFR0.PerfMon reads 0 regardless) |
| DWT_CTRL | `0x80000001` | 8 comparators, CYCCNT, profiling counters, trace sampling |
| FP_CTRL | `0x10000081` | FPB v2, 8 breakpoint comparators, 0 literal |
| DHCSR / DAUTHSTATUS | `0x05110000` / `0x00000000` | secure debug enabled (S_SDE) |
| ITM / ETM DEVARCH | `0x47701A01` / `0x47754A13` | ITM present; ETM present (ETMv4.x, TRCIDR0 `0x280006E1`, TRCIDR1 `0x4100F454`) |
| CoreSight ROM | entries `0xFFF0F003` `0xFFF02003` `0xFFF03003` `0xFFF01003` | NVIC/SCS, DWT, FPB, ITM (+ ETM, PMU, CTI in second-level table) |
| AIRCR / CCR / FPCCR | `0xFA050000` / `0x00000201` / `0xC0000004` | little-endian; BusFault/HardFault/NMI secure; lazy + automatic FP state preservation |
| OFS0–3 (ConfigROM) | `0xFFFFFFFF` `0xFFFFFFEF` `0xFFFFFFFF` `0xFFFFFFFF` | factory-default Renesas option bytes |
| OTP_CFG0/1 | `0xFFFFFFFF` / `0xFFFFFFFF` | unprogrammed |

Feature summary as printed by the firmware:

```
 core            : Cortex-M85 r1p1 (Arm)
 architecture    : Armv8-M Mainline + v8.1-M extensions
 security state  : Secure
 TrustZone-M     : yes (8 SAU regions)
 MPU (S) / (NS)  : 8 regions / 8 regions
 FPU             : single + double precision, 16 D-regs, FMA, FP16
 MVE (Helium)    : integer + float
 DSP extension   : yes
 PACBTI          : yes
 RAS extension   : yes
 PMU             : 8 x 32-bit event counters + cycle counter (per PMU_TYPE)
 Unpriv debug ext: yes
 I-cache / D-cache: 16 KiB / 16 KiB, line 32 B
 TCM             : yes (ITCM @0x0, DTCM @0x20000000)
 NVIC lines      : up to 96
 DWT / FPB       : 8 comparators + CYCCNT / 8 breakpoints
 ITM / ETM       : present / present
```

All values match the QEMU `cortex-m85` model, except ID_ISAR5 which QEMU zeroes
because it does not emulate PACBTI.

## Layout

```
flake.nix        nix develop -> ATfE clang/lld 22.1, J-Link 9.52 CLI, pyocd, OpenOCD, probe-rs, lldb
fw/link.ld       1 MiB code flash @0x02000000, SRAM @0x22000000 (Renesas.RA_DFP 6.5.1)
fw/startup.c     vector table, CPACR/FPU enable, .data/.bss init, fault-skipping HardFault handler
fw/rtt.c         minimal SEGGER-RTT-compatible control block (ID assembled at run time)
fw/main.c        register table + per-field decoders + summary
fw/rtt.py        RTT reader via libjlinkarm (pylink) with explicit control-block address
fw/Makefile      make / make flash / make rtt
fw/dump.txt      captured output from the EK-RA8M2
```

## Usage

```sh
nix develop                 # or: nix develop -c <cmd>
make -C fw                  # -> idreg.elf / .bin / .hex
make -C fw flash            # JLinkExe: reset, load, run    (DEVICE=R7KA8M2AF, JLINK_SN=... to override)
make -C fw rtt              # stream RTT; Ctrl-C to stop, any key re-runs the dump
```

Non-root probe access on NixOS (the flake does not install udev rules):

```nix
users.users.<you>.extraGroups = [ "dialout" ];
services.udev.extraRules = ''
  SUBSYSTEM=="usb", ATTRS{idVendor}=="1366", MODE="0660", GROUP="dialout", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
  SUBSYSTEM=="tty", ATTRS{idVendor}=="1366", MODE="0660", GROUP="dialout", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
'';
```

## Notes / gotchas

* Reads use a HardFault handler that skips the faulting load and flags it, so optional or
  secure-only registers cannot hang the probe.
* J-Link's RTT auto-search does not cover RA8M2 SRAM (`0x22000000`), and `JLinkRTTLogger`
  cannot read it even with `-RTTAddress` (it skips the device-specific connect). `rtt.py`
  passes the `_SEGGER_RTT` address from `llvm-nm` to `libjlinkarm` directly.
* Never background `JLinkExe` with a tty on stdin — it stops on SIGTTIN at its prompt.
* The J-Link OB firmware (2022) warns it does not handle the M85 I/D-cache; this firmware
  leaves caches disabled.
* `segger-jlink` in nixpkgs pulls an insecure Qt4 for its GUI tools; the flake permits it
  (`permittedInsecurePackages`) since only the CLI is used.
