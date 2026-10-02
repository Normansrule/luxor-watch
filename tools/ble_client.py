#!/usr/bin/env python3
"""Paired local BLE test client; no cloud, no automatic OS notification bridge.
Python >=3.10; pip install -r requirements-tools.txt
Enable the watch BLE session first. Enter the watch PIN in your OS pairing prompt.
"""
import argparse
import asyncio
import struct
import time
from bleak import BleakClient, BleakScanner

TELEMETRY = "e75a0010-2b3e-474e-8738-ac189d9b1002"
COMMAND = "e75a0010-2b3e-474e-8738-ac189d9b1003"

def decode(data: bytes) -> str:
    if len(data) != 20:
        return f"Unexpected packet length {len(data)}: {data.hex()}"
    version, flags, mv, steps, ax, ay, az, gx, gy, gz = struct.unpack("<BBHI6h", data)
    return (f"v{version}, flags=0x{flags:02x}, battery={mv/1000:.3f} V, steps={steps}, "
            f"accel(g)=({ax/1000:+.3f},{ay/1000:+.3f},{az/1000:+.3f}), "
            f"gyro(dps)=({gx/10:+.1f},{gy/10:+.1f},{gz/10:+.1f})")

async def run(args: argparse.Namespace) -> None:
    target = args.address
    if not target:
        target = await BleakScanner.find_device_by_name("LUXOR", timeout=15)
        if target is None:
            raise RuntimeError("Watch not found. Open its BLE page and tap Enable.")
    async with BleakClient(target, pair=True, timeout=60.0) as client:
        print(decode(bytes(await client.read_gatt_char(TELEMETRY))))
        if args.set_time:
            await client.write_gatt_char(COMMAND, f"TIME:{int(time.time())}".encode("ascii"), response=True)
            print("UTC time sent; the watch applies its configured timezone.")
        if args.message is not None:
            try:
                payload = ("MSG:" + args.message).encode("ascii")
            except UnicodeEncodeError as exc:
                raise ValueError("Use ASCII text for this V1 protocol.") from exc
            if len(payload) > 120 or any(c < 32 or c > 126 for c in payload):
                raise ValueError("Message must be printable ASCII and at most 116 characters.")
            await client.write_gatt_char(COMMAND, payload, response=True)
            print("Message sent.")
        if args.seconds > 0:
            await client.start_notify(TELEMETRY, lambda _sender, data: print(decode(bytes(data))))
            await asyncio.sleep(args.seconds)
            await client.stop_notify(TELEMETRY)

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--address", help="Optional BLE address, or macOS peripheral UUID")
    p.add_argument("--set-time", action="store_true")
    p.add_argument("--message", help="Printable ASCII, maximum 116 characters")
    p.add_argument("--seconds", type=float, default=0, help="Watch telemetry for this many seconds")
    args = p.parse_args()
    if not 0 <= args.seconds <= 240:
        p.error("--seconds must be between 0 and 240")
    try:
        asyncio.run(run(args))
    except (KeyboardInterrupt, Exception) as exc:
        p.exit(1, f"BLE operation failed: {exc}\nPair using the displayed watch PIN. Clear stale bonds on both devices when needed.\n")
