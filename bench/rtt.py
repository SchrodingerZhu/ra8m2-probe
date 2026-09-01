#!/usr/bin/env python3
"""Stream SEGGER RTT channel 0 from the target using libjlinkarm (pylink).

Usage: rtt.py [--device R7KA8M2AF] [--addr 0x22000008] [--elf idreg.elf]
              [--reset] [--sn SERIAL] [--timeout SECS]
Ctrl-C to stop. Any byte typed on stdin is forwarded to down-channel 0.
"""
import argparse, os, re, select, subprocess, sys, time
import pylink

ap = argparse.ArgumentParser()
ap.add_argument("--device", default=os.environ.get("DEVICE", "R7KA8M2AF"))
ap.add_argument("--addr", help="RTT control block address (hex)")
ap.add_argument("--elf", default="idreg.elf", help="ELF to look up _SEGGER_RTT in")
ap.add_argument("--sn", type=int, default=None)
ap.add_argument("--speed", type=int, default=4000)
ap.add_argument("--reset", action="store_true", help="reset+run target before reading")
ap.add_argument("--timeout", type=float, default=0, help="exit after N s of silence (0=never)")
a = ap.parse_args()

addr = None
if a.addr:
    addr = int(a.addr, 16)
elif os.path.exists(a.elf):
    nm = subprocess.run(["llvm-nm", a.elf], capture_output=True, text=True).stdout
    m = re.search(r"^([0-9a-fA-F]+) [BbDd] _SEGGER_RTT$", nm, re.M)
    if m:
        addr = int(m.group(1), 16)

jl = pylink.JLink()
jl.open(serial_no=a.sn)
jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
jl.connect(a.device, speed=a.speed, verbose=False)
if a.reset:
    jl.reset(halt=False)
jl.rtt_start(block_address=addr)
print(f"# RTT: {a.device} via J-Link S/N {jl.serial_number}, block @ "
      f"{hex(addr) if addr else 'auto-search'}", file=sys.stderr)

# wait for the control block to be detected
for _ in range(50):
    try:
        if jl.rtt_get_num_up_buffers() > 0:
            break
    except pylink.errors.JLinkRTTException:
        pass
    time.sleep(0.1)
else:
    sys.exit("RTT control block not found")

last = time.monotonic()
try:
    while True:
        data = jl.rtt_read(0, 4096)
        if data:
            sys.stdout.write(bytes(data).decode("utf-8", "replace"))
            sys.stdout.flush()
            last = time.monotonic()
        elif a.timeout and time.monotonic() - last > a.timeout:
            break
        else:
            time.sleep(0.02)
        if sys.stdin.isatty() and select.select([sys.stdin], [], [], 0)[0]:
            jl.rtt_write(0, list(os.read(0, 64)))
except KeyboardInterrupt:
    pass
finally:
    jl.rtt_stop()
    jl.close()
