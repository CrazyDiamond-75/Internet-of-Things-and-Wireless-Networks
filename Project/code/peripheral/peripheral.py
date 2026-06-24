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
from scipy.interpolate import LinearNDInterpolator


start_time = datetime.now().strftime("%Y-%m-%d_%H:%M:%S")


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


buffers = {}
positions = {}


def on_write(uuid, value):
    parsed = parse_message(value)

    # Log received message to disk.
    with open(f"received_messages_{start_time}.tout", "a") as f:
        print(parsed, file=f)

    node = parsed["node_id"]
    time = parsed["timestamp"]
    rssi = parsed["rssi"]
    addr = parsed["mac"]

    if addr not in buffers:
        # Each address should have three buffers, as we have tree nodes
        buffers[addr] = [
            [],
            [],
            [],
        ]

    buffers[addr][node - 1].append((time, rssi))


# Used for interpolation for getting (x,y) from ratios.
interp_x: None
interp_y: None

points = []
values = []

def init_lookup(res, xmin, xmax, ymin, ymax, triangle): 
    p1, p2, p3 = triangle

    x_steps = int((xmax - xmin) / res)
    y_steps = int((ymax - xmin) / res)

    # Use resolution as pixel width between x and y values.
    for i in range(x_steps + 1):
        x = xmin + i * res
        for j in range(y_steps + 1):
            y = ymin + j * res

            # Calculate positions
            d1 = sqrt((p1[0] - x) ** 2 + (p1[1] - y) ** 2)
            d2 = sqrt((p2[0] - x) ** 2 + (p2[1] - y) ** 2)
            d3 = sqrt((p3[0] - x) ** 2 + (p3[1] - y) ** 2)

            if d1 == 0:
                # Todo: Maybe skip these
                r21 = float("inf")
                r31 = float("inf")
            else:
                # Calculate ratios
                r21 = d2 / d1
                r31 = d3 / d1
    
            #lookup_table[(r21, r31)] = (x, y)
            points.append((r21, r31))
            values.append((x, y))
            # print(f"{x, y} -> {(r21, r31)}")


    # Convert to numpy arrays for scipy lib.
    points = np.array(points)
    values = np.array(values)

    # Interpolate using piecewise linear interpolation.
    global interp_x
    global interp_y

    interp_x = LinearNDInterpolator(points, values[:,0])
    interp_y = LinearNDInterpolator(points, values[:,1])
    
    # Quick section used to plot points and values
    """
    import matplotlib.pyplot as plt
    
    points_G = []
    values_I = []

    for r21 in np.arange(0, 20, 0.1):
        for r31 in np.arange(0, 20, 0.1):
            res = lookup(r21, r31)
            points_G.append((r21, r31))
            values_I.append(res)
    
    points_G = np.array(points_G)
    values_I = np.array(values_I)
    
    r21s = points_G[:, 0]
    r31s = points_G[:, 1]

    x = values_I[:, 0]
    y = values_I[:, 1]
    
    r = (x - x.min()) / (x.max() - x.min())
    g = (y - y.min()) / (y.max() - y.min())
    b = np.array([0.25] * len(x))

    rgb = np.column_stack([r, g, b])

    plt.scatter(
        r21s,
        r31s,
        c=rgb,
        s=3
    )
    plt.title("(x,y) from normalized distance ratios")
    plt.show()

    exit()

    """
    """
    r21s = np.log(points[:, 0])
    r31s = np.log(points[:, 1])

    #r = np.array([0.0] * len(r21s))
    
    r = (r31s - r31s.min()) / (r31s.max() - r31s.min())
    g = (r21s - r21s.min()) / (r21s.max() - r21s.min())
    b = np.array([0.25] * len(r21s))

    rgb = np.column_stack([r, g, b])
    
    plt.scatter(
        values[:,0], # x
        values[:,1], # y
        c=rgb,
        s=2
    )

    plt.title("Normalized distance ratios from (x,y)")
    """

    """
    plt.scatter(
        values[:,0], # x
        values[:,1], # y
        c=np.log(points[:,0] * points[:, 1]),
        cmap="viridis",
        s=1
    )
    plt.colorbar(label="log(r21 * r31)")
    plt.title("Normalized ratio product from (x,y)")
    """

    """
    x = values[:, 0]
    y = values[:, 1]

    r = (x - x.min()) / (x.max() - x.min())
    g = (y - y.min()) / (y.max() - y.min())
    b = np.array([0.25] * len(x))

    rgb = np.column_stack([r, g, b])

    plt.scatter(
        np.log(1 + points[:, 0]),
        np.log(1 + points[:, 1]),
        c=rgb,
        s=3
    )
    plt.title("(x,y) from normalized distance ratios")
    
    #plt.axis("equal")
    plt.xlim(-0.25, 3)
    plt.ylim(-0.25, 3)
    

    plt.axis("equal")
    
    plt.show()
    #plt.savefig("2.png", dpi=300)

    exit()
    """

def lookup_slow(r21, r31):
    # Simple algorithm which searches all entries and returns the best matching one.
    min_err = float("inf")
    best = -1

    for i in range(len(points)):
        r21_t, r31_t = points[i]
        err = (r21_t - r21) ** 2 + (r31_t - r31) ** 2
        if min_err > err:
            best = i
            min_err = err

    return values[best]

# Faster and interpolated method.
def lookup(r21, r31):
    return float(interp_x(r21, r31)), float(interp_y(r21, r31))

def triangulate(n1, n2, n3, N, rssi0=None, solve=False):
    """
    Triangulate position of a node based on RSSI measurements n1, n2, n3.
    Method 1: Calculate distances d1, d2, d3 to nodes based on n1, n2, and n3, RSSI at distance 1m (rssi0) and environment parameter N.
              Use classic triangulation to solve for (x,y), the coordinate is the only intersection of all circles on nodes 1, 2, and 3 with radi d1, d2, d3.

    Method 2: Used when rssi0 unused and solve is true.
              We have ni = Power_Transmitter - 10N log_10(di). Thus we have ni - nj = 10N log_10(dj / di).
              From this we can calculate dj / di = 10^((ni - nj) / 10N) and derive r21 = d2 / d1 as well as r31 = d3 / d1.
              For node positions x1, y1, x2, y2, x3, y3 we know: di = sqrt((x - xi)^2 + (y - yi)^2).

              Because of the ratios d2 = r21 * d1 and d3 = r31 * d1 we have:
                1. sqrt((x - x2)^2 + (y - y2)^2) = r21 * sqrt((x - x1)^2 + (y - y1)^2)
                2. sqrt((x - x3)^2 + (y - y3)^2) = r31 * sqrt((x - x1)^2 + (y - y1)^2)

              We can square both sides to get:
                1. (x - x2)^2 + (y - y2)^2 = r21^2 * (x - x1)^2 + (y - y1)^2
                2. (x - x3)^2 + (y - y3)^2 = r31^2 * (x - x1)^2 + (y - y1)^2

              This a non-linear equation system with two equations and two unknowns (x,y).
              All that is left to do is solve the system.
              We can either solve this system for every measurement triple or average/filter a chunk of triples.

    Method 3: Used when rssi0 is unused and solve is false.
              Alternatively to solving the system we can pre compute a lookup table which estimates (x,y) based on r21 and r31.
              This is done by setting x1, y1, x2, y2, x3, y3, and calculating expected r21 and r31 for a fine grid of positions.
              The mapping is bijective, which means we can store the inverse map to gain x,y from r21 and r31.
              For real values in between some two ratio pairs, use some interpolation algorithm.
    """

    # Method 1
    pass

    # Method 2
    pass

    # Method 3
    if rssi0 is None and not solve:
        # Calculate ratios
        r21 = 10.0 ** ((n1 - n2) / (10.0 * N))
        r31 = 10.0 ** ((n1 - n3) / (10.0 * N))

        return lookup(r21, r31)


def avg(tuples):
    if not tuples:
        return None

    n = len(tuples)
    return tuple(sum(col) / n for col in zip(*tuples))


def dequeue():
    if not buffers:
        return
    for addr in buffers.keys():
        if all(buffers[addr]):
            result: tuple(float, float)

            buff_1, buff_2, buff_3 = buffers[addr]

            # Triangulate based on one sample if the range between measurements is good enough.
            """
            tmin = min(buff_1[0], buff_2[0], buff_3[0])
            tmax = max(buff_1[0], buff_2[0], buff_3[0])
            
            # I will search for a sensible range...
            if tmax - tmin < 1000:
                n1 = buff_1.pop(0)[1]
                n2 = buff_2.pop(0)[1]
                n3 = buff_3.pop(0)[1]

                result = triangulate(n1, n2, n3)
            else:
                if buff_1 == tmin:
                    buff_1.pop(0)
                elif buff_2 == tmin:
                    buff_2.pop(0)
                else:
                    buff_3.pop(0)
            """

            # Triangulate based on everything that is in the buffer.
            results = []
            while buff_1 and buff_2 and buff_3:
                n1 = buff_1.pop(0)[1]
                n2 = buff_2.pop(0)[1]
                n3 = buff_3.pop(0)[1]

                # Use N=3, as N typically ranges between 2 and 4.
                # TODO: Find good estimations for N.
                results.append(triangulate(n1, n2, n3, 3))

            # Todo: Maybe also calculate variance to display uncertainty.
            result = avg(results)
            positions[addr] = result


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

    server = BlessServer(name="TriServer")
    server.write_request_func = on_write

    await server.add_gatt(gatt)
    await server.start()

    print("Advertising...")

    while True:
        # Average all values under 2 seconds.
        await asyncio.sleep(2)
        dequeue()

        # Log currently triangulated positions to disk.
        with open(f"triangul_positons_{start_time}.tout", "a") as f:
            print(
                [
                    (addr, positions[addr])
                    for addr in positions.keys()
                    if positions[addr]
                ],
                file=f,
            )

# Example configuration with 5 points per meter
# box around (0,0) with area 10m^2, and equidistant triangle
# (0, -5), (sqrt(25 - 2.5^2), 2.5), (-sqrt(25 - 2.5^2, 2.5).
"""
init_lookup(
    0.2,
    -5,
    5,
    -5,
    5,
    [(0, -5), (sqrt(25.0 - 2.5**2), 2.5), (-sqrt(25.0 - 2.5**2), 2.5)],
)
"""

# Example configuration mirroring my kitchen.
"""
init_lookup(
    0.2,
    -2,
    2,
    -1,
    3,
    [(0, 0), (-1.65, 2.5), (1.85, 2.5)],
)
"""

# Example call to test config.
# r21, r31 = (1.4317, 2.35)
# print(f"{(r21, r31)} -> {lookup(r21, r31)}")

# Example configuration mirroring the TV room.
init_lookup(
    0.02,
    -3,
    3,
    -3,
    3,
    [(-1.8180180180180183, -1.0095815621366517), (1.8819819819819819, -1.0095815621366517), (-0.0639639639639642, 2.0191631242733035)]
)

asyncio.run(main())
