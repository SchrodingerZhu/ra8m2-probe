#!/usr/bin/env python3
"""hsearch results.csv -> Markdown."""
import sys
from collections import defaultdict

rows, meta = [], []
for line in open(sys.argv[1]):
    line = line.strip()
    if line.startswith("#"):
        meta.append(line)
    elif line.startswith("R,"):
        _, impl, op, n, total, ops, mem = line.split(",")
        rows.append((impl, op, int(n), int(total), int(ops), int(mem)))

data = defaultdict(dict)   # (op, n) -> impl -> (cyc/op, mem)
impls, ops_order, sizes = [], [], []
for impl, op, n, total, ops, mem in rows:
    data[(op, n)][impl] = (total / ops, mem)
    if impl not in impls: impls.append(impl)
    if op not in ops_order: ops_order.append(op)
    if n not in sizes and op == "insert": sizes.append(n)

print("# llvm-libc hsearch on Cortex-M85 (RA8M2)\n")
for m in meta: print(f"    {m}")
print("\nValues are **cycles per operation** (best of 3 passes for finds).\n")

for op in ops_order:
    ns = sorted({n for (o, n) in data if o == op})
    print(f"## {op}\n")
    print("| impl | " + " | ".join(f"n={n}" for n in ns) + " |")
    print("|" + "---|" * (len(ns) + 1))
    for impl in impls:
        cells = []
        for n in ns:
            v = data.get((op, n), {}).get(impl)
            cells.append("-" if v is None else f"{v[0]:.0f}")
        if any(c != "-" for c in cells):
            print(f"| {impl} | " + " | ".join(cells) + " |")
    print()

# memory table
print("## peak heap during presized insert (bytes)\n")
ns = sorted({n for (o, n) in data if o == "insert"})
print("| impl | " + " | ".join(f"n={n}" for n in ns) + " |")
print("|" + "---|" * (len(ns) + 1))
for impl in impls:
    cells = []
    for n in ns:
        v = data.get(("insert", n), {}).get(impl)
        cells.append("-" if v is None or v[1] == 0 else str(v[1]))
    if any(c != "-" for c in cells):
        print(f"| {impl} | " + " | ".join(cells) + " |")
