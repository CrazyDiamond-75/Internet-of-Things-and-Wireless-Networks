import sys
import ast
import numpy as np

# import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import matplotlib.patches as patches

# --------------------------------------------------
# Load frames
# --------------------------------------------------

with open(sys.argv[1]) as f:
    frames = [ast.literal_eval(line.strip()) for line in f if line.strip()]

# --------------------------------------------------
# Determine plot limits
# --------------------------------------------------

all_x = []
all_y = []

for frame in frames:
    for _, (x, y) in frame:
        all_x.append(x)
        all_y.append(y)

xmin = min(all_x) - 0.5
xmax = max(all_x) + 0.5
ymin = min(all_y) - 0.5
ymax = max(all_y) + 0.5

# xmin = -2
# xmax = 2
# ymin = -1
# ymax = 3

# --------------------------------------------------
# Create figure
# --------------------------------------------------

fig, ax = plt.subplots(figsize=(8, 6))

scatter = ax.scatter([], [])
texts = []

ax.set_xlim(xmin, xmax)
ax.set_ylim(ymin, ymax)
ax.set_aspect("equal")

ax.set_xlabel("x")
ax.set_ylabel("y")

triangle = patches.Polygon(
    [(-1.8180180180180183, -1.0095815621366517), (1.8819819819819819, -1.0095815621366517), (-0.0639639639639642, 2.0191631242733035)],
    closed=True,
    fill=False,
    edgecolor="blue",
    linewidth=1,
)

ax.add_patch(triangle)

# --------------------------------------------------
# Animation update
# --------------------------------------------------


def update(frame_idx):
    global texts

    # remove previous labels
    for t in texts:
        t.remove()
    texts = []

    frame = frames[frame_idx]

    if len(frame) == 0:
        scatter.set_offsets(np.empty((0, 2)))
    else:
        coords = [(x, y) for _, (x, y) in frame]
        scatter.set_offsets(coords)

        for identifier, (x, y) in frame:
            color = "blue"
            if identifier == "6C:A5:49:DC:D8:8D" or identifier == "A0:28:84:03:E9:02":
                color = "red"

            txt = ax.text(x, y, identifier, fontsize=8, color=color)
            texts.append(txt)

    ax.set_title(f"t = {frame_idx * 2}s")

    return [scatter, *texts]


# --------------------------------------------------
# Run animation
# --------------------------------------------------

ani = FuncAnimation(
    fig, update, frames=len(frames), interval=500, repeat=True  # ms between frames
)

plt.show()
