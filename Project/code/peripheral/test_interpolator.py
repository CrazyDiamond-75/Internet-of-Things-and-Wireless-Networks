import ast
import sys
import argparse
from math import sqrt

import numpy as np
from scipy.interpolate import LinearNDInterpolator
from scipy.spatial import KDTree

TRIANGLE = [
    (-1.8180180180180183, -1.0095815621366517),
    (1.8819819819819819, -1.0095815621366517),
    (-0.0639639639639642,  2.0191631242733035),
]
RES = 0.02
XMIN, XMAX = -3, 3
YMIN, YMAX = -3, 3

DEFAULT_LOG = "received_messages_2026-06-16_17_39_06.tout"

parser = argparse.ArgumentParser()
parser.add_argument("log", nargs="?", default=DEFAULT_LOG, help="Path to .tout log file")
parser.add_argument("--N", type=float, default=3.5, help="Path-loss exponent (default 3.5)")
args = parser.parse_args()

LOG = args.log
N = args.N

print(f"Building lookup grid (res={RES}, area [{XMIN},{XMAX}]x[{YMIN},{YMAX}])...")
p1, p2, p3 = TRIANGLE
x_steps = int((XMAX - XMIN) / RES)
y_steps = int((YMAX - YMIN) / RES)

grid_points, grid_values = [], []
for i in range(x_steps + 1):
    x = XMIN + i * RES
    for j in range(y_steps + 1):
        y = YMIN + j * RES
        d1 = sqrt((p1[0]-x)**2 + (p1[1]-y)**2)
        d2 = sqrt((p2[0]-x)**2 + (p2[1]-y)**2)
        d3 = sqrt((p3[0]-x)**2 + (p3[1]-y)**2)
        if d1 == 0:
            continue  # skip singularity at node 1's position
        grid_points.append((d2/d1, d3/d1))
        grid_values.append((x, y))

pts = np.array(grid_points)
vals = np.array(grid_values)
print(f"Grid points: {len(pts)}")

print("Building LinearNDInterpolator...")
interp_x = LinearNDInterpolator(pts, vals[:, 0])
interp_y = LinearNDInterpolator(pts, vals[:, 1])

print("Building KDTree...")
tree = KDTree(pts)


def lookup_interp(r21, r31):
    return float(interp_x(r21, r31)), float(interp_y(r21, r31))


def lookup_kdtree(r21, r31, k=4):
    dists, idxs = tree.query((r21, r31), k=k)
    if np.any(dists == 0):
        return tuple(vals[idxs[np.argmin(dists)]])
    w = 1.0 / dists
    w /= w.sum()
    xy = (vals[idxs] * w[:, None]).sum(axis=0)
    return float(xy[0]), float(xy[1])

def ratios(n1, n2, n3, N):
    r21 = 10.0 ** ((n1 - n2) / (10.0 * N))
    r31 = 10.0 ** ((n1 - n3) / (10.0 * N))
    return r21, r31


print(f"\nParsing log: {LOG}")
buffers = {}
total_lines = 0
with open(LOG) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        total_lines += 1
        row  = ast.literal_eval(line)
        mac  = row["mac"]
        node = row["node_id"]
        t    = row["timestamp"]
        rssi = row["rssi"]
        if mac not in buffers:
            buffers[mac] = {1: [], 2: [], 3: []}
        if node in (1, 2, 3):
            buffers[mac][node].append((t, rssi))

print(f"Lines parsed: {total_lines}")
print(f"Unique MACs : {len(buffers)}")

print(f"\nRunning triangulation with N={N} ...")

interp_total = interp_nan = 0
kd_total = 0
interp_nan_examples = []
interp_results, kd_results = [], []

for mac, bufs in buffers.items():
    b1, b2, b3 = bufs[1], bufs[2], bufs[3]
    pairs = min(len(b1), len(b2), len(b3))
    for k in range(pairs):
        n1, n2, n3 = b1[k][1], b2[k][1], b3[k][1]
        r21, r31   = ratios(n1, n2, n3, N)

        # Interpolator
        ix, iy = lookup_interp(r21, r31)
        interp_total += 1
        if np.isnan(ix) or np.isnan(iy):
            interp_nan += 1
            if len(interp_nan_examples) < 8:
                interp_nan_examples.append((n1, n2, n3, r21, r31))
        else:
            interp_results.append((ix, iy))

        # KDTree
        kx, ky = lookup_kdtree(r21, r31)
        kd_total += 1
        kd_results.append((kx, ky))

nan_rate = 100 * interp_nan / interp_total if interp_total else 0

print("\n" + "="*55)
print(f"  N = {N}   |   Log: {LOG.split('/')[-1]}")
print("="*55)

print(f"\n{'METHOD':<28} {'TOTAL':>7}  {'NaN':>6}  {'NaN%':>7}")
print("-"*55)
print(f"{'LinearNDInterpolator':<28} {interp_total:>7}  {interp_nan:>6}  {nan_rate:>6.1f}%")
print(f"{'KDTree (k=4, IDW)':<28} {kd_total:>7}  {'0':>6}  {'0.0':>6}%")

if interp_results:
    xs = [r[0] for r in interp_results]
    ys = [r[1] for r in interp_results]
    print(f"\nInterpolator valid positions:")
    print(f"  x: [{min(xs):.2f}, {max(xs):.2f}]   y: [{min(ys):.2f}, {max(ys):.2f}]")

if kd_results:
    xs = [r[0] for r in kd_results]
    ys = [r[1] for r in kd_results]
    print(f"KDTree positions (all {kd_total}):")
    print(f"  x: [{min(xs):.2f}, {max(xs):.2f}]   y: [{min(ys):.2f}, {max(ys):.2f}]")

print(f"\nNaN examples (interpolator only):")
print(f"  {'n1':>5} {'n2':>5} {'n3':>5}   {'r21':>7} {'r31':>7}")
print(f"  {'-'*5} {'-'*5} {'-'*5}   {'-'*7} {'-'*7}")
for ex in interp_nan_examples:
    print(f"  {ex[0]:>5} {ex[1]:>5} {ex[2]:>5}   {ex[3]:>7.3f} {ex[4]:>7.3f}")

print(f"\nPer-MAC triple counts:")
print(f"  {'MAC':<20} {'N1':>4} {'N2':>4} {'N3':>4}   {'triples':>7}")
print(f"  {'-'*20} {'-'*4} {'-'*4} {'-'*4}   {'-'*7}")
for mac, bufs in sorted(buffers.items(), key=lambda x: -min(len(x[1][1]), len(x[1][2]), len(x[1][3]))):
    c1, c2, c3 = len(bufs[1]), len(bufs[2]), len(bufs[3])
    pairs = min(c1, c2, c3)
    print(f"  {mac:<20} {c1:>4} {c2:>4} {c3:>4}   {pairs:>7}")
