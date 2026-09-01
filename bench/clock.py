#!/usr/bin/env python3
"""Estimate the core clock from the host: SysTick (CLKSOURCE=core) counted over a wall-clock
interval. (DWT_CYCCNT reads stale through the debug AP while the core runs on this setup.)"""
import time, sys, pylink
jl = pylink.JLink(); jl.open(); jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
jl.connect(sys.argv[1] if len(sys.argv) > 1 else "R7KA8M2AF", speed=4000, verbose=False)
if jl.halted():
    jl.restart()
    time.sleep(0.2)
jl.memory_write32(0xE000E014, [0xFFFFFF]); jl.memory_write32(0xE000E018, [0]); jl.memory_write32(0xE000E010, [5])
s0, t0 = jl.memory_read32(0xE000E018, 1)[0], time.perf_counter()
time.sleep(0.5)
s1, t1 = jl.memory_read32(0xE000E018, 1)[0], time.perf_counter()
print(f"{((s0 - s1) & 0xFFFFFF) / (t1 - t0) / 1e6:.2f} MHz (SysTick delta over {t1 - t0:.3f} s; wraps at 16.7M cycles)")
jl.close()
