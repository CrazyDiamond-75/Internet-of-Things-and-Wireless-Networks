import asyncio
from bless import (
    BlessServer,
    GATTCharacteristicProperties,
    GATTAttributePermissions,
)

SERVICE_UUID = "0000ACDC-0000-1000-8000-00805F9B34FB"
CHARACT_UUID = "0000DEAF-0000-1000-8000-00805F9B34FB"


def on_write(uuid, value):
    print(type(value))

    print(f"Received write: {value}")
    print(f"As string: {value.decode(errors="ignore")}")


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
