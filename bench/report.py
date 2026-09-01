#!/usr/bin/env python3
"""Turn results.csv from the bench firmware into Markdown tables.

For every (phase, func, alignment) a table of cycles/byte (min of REPS) per
implementation, plus a speed-up column vs picolibc and vs llvm-libc."""
import csv, sys
from collections import defaultdict

PHASES = {0: "caches OFF, buffers in SRAM (0x2200_0000)",
          1: "I+D cache ON, buffers in SRAM",
          2: "I+D cache ON, buffers in DTCM (0x2000_0000)"}
SHOW_SIZES = [1, 4, 8, 16, 32, 64, 128, 256, 1024, 4096, 16384]

rows = []
meta = []
fails = []
with open(sys.argv[1]) as f:
    for line in f:
        line = line.rstrip("\n")
        if line.startswith("#"):
            meta.append(line)
        elif line.startswith("V,"):
            fails.append(line)
        elif line.startswith("R,"):
            _, ph, fn, im, n, da, sa, mn, md = line.split(",")
            rows.append((int(ph), fn, im, int(n), int(da), int(sa), int(mn), int(md)))

# index: (phase, func, da, sa) -> impl -> n -> min
data = defaultdict(lambda: defaultdict(dict))
impl_order = defaultdict(list)
for ph, fn, im, n, da, sa, mn, md in rows:
    data[(ph, fn, da, sa)][im][n] = mn
    if im not in impl_order[fn]:
        impl_order[fn].append(im)

print("# Cortex-M85 (RA8M2) string-function benchmark\n")
for m in meta:
    print(f"    {m}")
print()
if fails:
    print(f"**{len(fails)} verification failures:**\n")
    for v in fails:
        print(f"    {v}")
    print()
else:
    print("All implementations passed verification against the byte-per-byte reference on every size/alignment.\n")

print("Values are **cycles** for the whole call (min of REPS, call overhead subtracted); "
      "the bracketed number is cycles/byte. Speed-ups in the last columns are for n=1024 (or the "
      "largest size present) against picolibc and llvm-libc.\n")

funcs = ["memcpy", "memmove_fwd", "memmove_bwd", "memset", "memcmp", "bcmp", "strlen", "memchr"]

# ---- headline: phase 1 (caches on, SRAM), aligned, n = 1024 and n = 16 -----
print("## Headline (I+D cache on, SRAM, aligned): cycles at n=1024 [cycles/byte] and n=16\n")
allimpls = []
for fn in funcs:
    for im in impl_order[fn]:
        if im not in allimpls:
            allimpls.append(im)
print("| func | " + " | ".join(allimpls) + " | best vs llvm-libc |")
print("|" + "---|" * (len(allimpls) + 2))
for fn in funcs:
    key = (1, fn, 0, 0)
    if key not in data:
        continue
    impls = data[key]
    cells = []
    best = None
    for im in allimpls:
        c = impls.get(im, {}).get(1024); s = impls.get(im, {}).get(16)
        if c is None:
            cells.append("-")
        else:
            cells.append(f"{c} [{c / 1024:.2f}] / {s}")
            if best is None or c < best[1]:
                best = (im, c)
    llvm = impls.get("llvm-libc", {}).get(1024)
    print(f"| {fn} | " + " | ".join(cells) + (f" | {best[0]} {llvm / best[1]:.1f}x |" if best and llvm else " | - |"))
print()

for ph in sorted(PHASES):
    print(f"\n## Phase {ph}: {PHASES[ph]}\n")
    for fn in funcs:
        keys = sorted(k for k in data if k[0] == ph and k[1] == fn)
        for key in keys:
            _, _, da, sa = key
            impls = data[key]
            sizes = sorted({n for im in impls.values() for n in im})
            show = [n for n in SHOW_SIZES if n in sizes]
            big = 1024 if 1024 in sizes else sizes[-1]
            align = f"dst+{da}" + (f", src+{sa}" if fn in ("memcpy", "memcmp", "bcmp") else "")
            print(f"### {fn}  ({align})\n")
            hdr = "| impl | " + " | ".join(str(n) for n in show) + " | vs picolibc | vs llvm-libc |"
            print(hdr)
            print("|" + "---|" * (len(show) + 3))
            pico = impls.get("picolibc", {}).get(big)
            llvm = impls.get("llvm-libc", {}).get(big)
            for im in impl_order[fn]:
                if im not in impls:
                    continue
                cells = []
                for n in show:
                    c = impls[im].get(n)
                    cells.append("-" if c is None else f"{c} ({c / n:.2f})")
                mine = impls[im].get(big)
                sp = "-" if not (pico and mine) else f"{pico / mine:.2f}x"
                sl = "-" if not (llvm and mine) else f"{llvm / mine:.2f}x"
                print(f"| {im} | " + " | ".join(cells) + f" | {sp} | {sl} |")
            print()
