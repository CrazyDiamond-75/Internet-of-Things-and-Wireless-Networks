"""
Bluetooth Low Energy (BLE) server for trilateration-based indoor positioning.

This module implements a BLE GATT server that receives RSSI measurements from
multiple nodes and performs position estimation using trilateration based on
distance ratios derived from RSSI values.
"""

import asyncio
from bless import (
    BlessServer,
    GATTCharacteristicProperties,
    GATTAttributePermissions,
)
import struct
from math import sqrt
from datetime import datetime
import numpy as np
from scipy.spatial import KDTree

# Change to linux format if needed; windows does not allow ":"
start_time = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

# Message format: node_id (B), timestamp (q), rssi (b), addr_type (B), mac (6s)
FORMAT = "<B q b B 6s"
SIZE = struct.calcsize(FORMAT)

SERVICE_UUID = "0000ACDC-0000-1000-8000-00805F9B34FB"
CHARACT_UUID = "0000DEAF-0000-1000-8000-00805F9B34FB"

# Global state
buffers = {}  # {mac: [buffer_node1, buffer_node2, buffer_node3]}
positions = {}  # {mac: (x, y)}

# KDTree lookup table for fast position estimation
_tree: KDTree = None
_tree_values: np.ndarray = None
points = []
values = []


def parse_message(data: bytearray) -> dict:
    """
    Parse a BLE message into structured data.

    Args:
        data: Raw message bytes

    Returns:
        Dictionary with keys: node_id, timestamp, rssi, addr_type, mac

    Raises:
        ValueError: If data is too short
    """
    if len(data) < SIZE:
        raise ValueError(f"Expected {SIZE} bytes, got {len(data)}")

    node_id, timestamp, rssi, addr_type, addr_raw = struct.unpack(FORMAT, data[:SIZE])
    mac = ":".join(f"{b:02X}" for b in addr_raw[::-1])  # Reverse for BLE display

    return {
        "node_id": node_id,
        "timestamp": timestamp,
        "rssi": rssi,
        "addr_type": addr_type,
        "mac": mac,
    }


def on_write(uuid, value):
    """Handle incoming BLE write requests."""
    parsed = parse_message(value)

    # Log received message to disk
    with open(f"received_messages_{start_time}.tout", "a") as f:
        print(parsed, file=f)

    node = parsed["node_id"]
    addr = parsed["mac"]
    timestamp = parsed["timestamp"]
    rssi = parsed["rssi"]

    # Initialize buffer for new MAC address
    if addr not in buffers:
        buffers[addr] = [[], [], []]

    # Store measurement in appropriate node buffer
    buffers[addr][node - 1].append((timestamp, rssi))


def init_lookup(res, xmin, xmax, ymin, ymax, triangle):
    """
    Build a KDTree lookup table for fast position estimation.

    Maps distance ratios (r21, r31) to (x, y) coordinates using a pre-computed grid.

    Args:
        res: Grid resolution in meters
        xmin, xmax, ymin, ymax: Bounds of the area
        triangle: List of 3 node positions [(x1, y1), (x2, y2), (x3, y3)]
    """
    global points, values, _tree, _tree_values

    p1, p2, p3 = triangle

    x_steps = int((xmax - xmin) / res)
    y_steps = int((ymax - ymin) / res)

    for i in range(x_steps + 1):
        x = xmin + i * res
        for j in range(y_steps + 1):
            y = ymin + j * res

            d1 = sqrt((p1[0] - x) ** 2 + (p1[1] - y) ** 2)
            d2 = sqrt((p2[0] - x) ** 2 + (p2[1] - y) ** 2)
            d3 = sqrt((p3[0] - x) ** 2 + (p3[1] - y) ** 2)

            if d1 == 0:
                continue  # Skip singularity at node 1

            r21 = d2 / d1
            r31 = d3 / d1

            points.append((r21, r31))
            values.append((x, y))

    _tree = KDTree(np.array(points))
    _tree_values = np.array(values)

    print(f"Lookup table built: {len(points)} points")


def lookup(r21, r31, k=4):
    """
    Estimate (x, y) position from distance ratios using k-NN with inverse-distance weighting.

    Args:
        r21: Ratio d2/d1
        r31: Ratio d3/d1
        k: Number of nearest neighbors to consider

    Returns:
        Estimated (x, y) position
    """
    dists, idxs = _tree.query((r21, r31), k=k)
    if np.any(dists == 0):
        return tuple(_tree_values[idxs[np.argmin(dists)]])
    w = 1.0 / dists
    w /= w.sum()
    xy = (_tree_values[idxs] * w[:, None]).sum(axis=0)
    return tuple(xy)


def triangulate(n1, n2, n3, N, rssi0=None, solve=False):
    """
    Triangulate device position from RSSI measurements.

    Supported methods:
    1. Absolute distances (requires rssi0) - NOT IMPLEMENTED
    2. Solving distance ratio system (requires solve=True) - NOT IMPLEMENTED
    3. Lookup table interpolation (default) - IMPLEMENTED

    Distance ratio method:
        n_i = P_tx - 10*N*log10(d_i)
        r_ij = d_i / d_j = 10^((n_j - n_i) / (10*N))

    Args:
        n1, n2, n3: RSSI measurements from nodes 1, 2, 3
        N: Path-loss exponent (typically 2-4 for indoors)
        rssi0: RSSI at 1m (for absolute distance method)
        solve: Whether to solve the system algebraically

    Returns:
        Estimated (x, y) position
    """
    if rssi0 is None and not solve:
        r21 = 10.0 ** ((n1 - n2) / (10.0 * N))
        r31 = 10.0 ** ((n1 - n3) / (10.0 * N))
        return lookup(r21, r31)


def avg(tuples):
    """
    Average a list of coordinate tuples.

    Args:
        tuples: List of (x, y) tuples

    Returns:
        Average (x, y) or None if empty
    """
    if not tuples:
        return None
    n = len(tuples)
    return tuple(sum(col) / n for col in zip(*tuples))


def dequeue():
    """
    Process buffered measurements and compute positions for all devices.

    For each device, triangulates position from all available measurement triples
    and stores the averaged result.
    """
    if not buffers:
        return

    for addr in buffers.keys():
        if all(buffers[addr]):
            buff_1, buff_2, buff_3 = buffers[addr]

            # Triangulate from all available measurements
            results = []
            while buff_1 and buff_2 and buff_3:
                n1 = buff_1.pop(0)[1]
                n2 = buff_2.pop(0)[1]
                n3 = buff_3.pop(0)[1]

                # N=3.5 is typical for indoor environments (range 2-4)
                results.append(triangulate(n1, n2, n3, N=3.5))

            result = avg(results)
            positions[addr] = result


async def main():
    """Start the BLE GATT server and process measurements."""
    gatt = {
        SERVICE_UUID: {
            CHARACT_UUID: {
                "Properties": GATTCharacteristicProperties.write,
                "Permissions": GATTAttributePermissions.writeable,
                "Value": None,
            }
        }
    }

    server = BlessServer(name="TriServer")
    server.write_request_func = on_write

    await server.add_gatt(gatt)
    await server.start()

    print("Advertising...")

    while True:
        await asyncio.sleep(2)
        dequeue()

        # Log triangulated positions to disk
        with open(f"triangul_positons_{start_time}.tout", "a") as f:
            print(
                [
                    (addr, positions[addr])
                    for addr in positions.keys()
                    if positions[addr]
                ],
                file=f,
            )


# Initialize lookup table for TV room (coordinates in meters)
init_lookup(
    0.02,
    -3,
    3,
    -3,
    3,
    [
        (-1.8180180180180183, -1.0095815621366517),
        (1.8819819819819819, -1.0095815621366517),
        (-0.0639639639639642, 2.0191631242733035),
    ],
)

asyncio.run(main())
