import asyncio
from bless import (
    BlessServer,
    GATTCharacteristicProperties,
    GATTAttributePermissions,
)
import struct

FORMAT = "<B q b B 6s"
SIZE = struct.calcsize(FORMAT)


def parse_message(data: bytearray):
    if len(data) < SIZE:
        raise ValueError(f"Expected {SIZE} bytes, got {len(data)}")

    node_id, timestamp, rssi, addr_type, addr_raw = struct.unpack(FORMAT, data[:SIZE])

    # Convert MAC to readable format
    mac = ":".join(f"{b:02X}" for b in addr_raw[::-1])  # reverse for BLE display

    return {
        "node_id": node_id,
        "timestamp": timestamp,
        "rssi": rssi,
        "addr_type": addr_type,
        "mac": mac,
    }


SERVICE_UUID = "0000ACDC-0000-1000-8000-00805F9B34FB"
CHARACT_UUID = "0000DEAF-0000-1000-8000-00805F9B34FB"


def on_write(uuid, value):
    print(parse_message(value))


async def main():
    gatt = {
        SERVICE_UUID: {
            CHARACT_UUID: {
                "Properties": (GATTCharacteristicProperties.write),
                "Permissions": (GATTAttributePermissions.writeable),
                "Value": None,
            }
        }
    }

    server = BlessServer(name="Triangulation Peripheral")
    server.write_request_func = on_write

    await server.add_gatt(gatt)
    await server.start()

    print("Advertising...")

    while True:
        await asyncio.sleep(1)


asyncio.run(main())
