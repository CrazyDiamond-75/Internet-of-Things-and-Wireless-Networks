import sys
import math
import matplotlib.pyplot as plt


def triangle_positions(d_ab, d_bc, d_ca):
    """
    Returns coordinates of A, B, C such that:
      distance(A,B) = d_ab
      distance(B,C) = d_bc
      distance(C,A) = d_ca
    and the centroid is at (0, 0).
    """

    # Place A and B
    A = (0.0, 0.0)
    B = (d_ab, 0.0)

    # Compute C using circle intersection
    x = (d_ca**2 - d_bc**2 + d_ab**2) / (2 * d_ab)

    y_sq = d_ca**2 - x**2
    if y_sq < 0:
        raise ValueError("Distances do not form a valid triangle")

    y = math.sqrt(y_sq)

    C = (x, y)

    # Compute centroid
    cx = (A[0] + B[0] + C[0]) / 3
    cy = (A[1] + B[1] + C[1]) / 3

    # Shift so centroid is at (0,0)
    A = (A[0] - cx, A[1] - cy)
    B = (B[0] - cx, B[1] - cy)
    C = (C[0] - cx, C[1] - cy)

    return A, B, C

def plot_triangle(A, B, C):
    xs = [A[0], B[0], C[0], A[0]]
    ys = [A[1], B[1], C[1], A[1]]

    plt.figure(figsize=(6, 6))

    # Triangle edges
    plt.plot(xs, ys, "b-", linewidth=2)

    # Vertices
    plt.scatter(
        [A[0], B[0], C[0]],
        [A[1], B[1], C[1]],
        color="red",
        zorder=5,
    )

    # Labels
    plt.text(A[0], A[1], " A")
    plt.text(B[0], B[1], " B")
    plt.text(C[0], C[1], " C")

    # Centroid (guaranteed to be at origin)
    plt.scatter([0], [0], color="green", s=100, label="Centroid")
    plt.axhline(0, color="gray", linestyle="--", alpha=0.5)
    plt.axvline(0, color="gray", linestyle="--", alpha=0.5)

    plt.axis("equal")
    plt.grid(True)
    plt.legend()
    plt.title("Triangle from Pairwise Distances")
    plt.show()

ab = float(sys.argv[1])
bc = float(sys.argv[2])
ca = float(sys.argv[3])

A, B, C = triangle_positions(ab, bc, ca)

print(A, B, C)

plot_triangle(A, B, C)
