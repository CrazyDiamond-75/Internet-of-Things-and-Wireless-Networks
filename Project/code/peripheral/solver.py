# I DIDN'T TEST THIS!
# ALSO THIS IS 100% VIBE-CODED!

import math

def solve_position(x1, y1, x2, y2, x3, y3, r21, r31):
    k2 = r21**2
    k3 = r31**2

    # Circle coefficients
    A2 = 1 - k2
    B2 = 2 * (k2 * x1 - x2)
    C2 = 2 * (k2 * y1 - y2)
    D2 = x2**2 + y2**2 - k2 * (x1**2 + y1**2)

    A3 = 1 - k3
    B3 = 2 * (k3 * x1 - x3)
    C3 = 2 * (k3 * y1 - y3)
    D3 = x3**2 + y3**2 - k3 * (x1**2 + y1**2)

    # Eliminate x²+y²
    Lx = A3 * B2 - A2 * B3
    Ly = A3 * C2 - A2 * C3
    L0 = A3 * D2 - A2 * D3

    eps = 1e-12

    # Solve y = m*x + b
    if abs(Ly) > eps:
        m = -Lx / Ly
        b = -L0 / Ly

        alpha = A2 * (1 + m**2)
        beta = B2 + C2 * m + 2 * A2 * m * b
        gamma = A2 * b**2 + C2 * b + D2

        disc = beta**2 - 4 * alpha * gamma

        if disc < -eps:
            return []

        disc = max(disc, 0.0)
        s = math.sqrt(disc)

        x_sol1 = (-beta + s) / (2 * alpha)
        x_sol2 = (-beta - s) / (2 * alpha)

        y_sol1 = m * x_sol1 + b
        y_sol2 = m * x_sol2 + b

        if disc < eps:
            return [(x_sol1, y_sol1)]

        return [(x_sol1, y_sol1), (x_sol2, y_sol2)]

    # Otherwise solve x = m*y + b
    elif abs(Lx) > eps:
        m = -Ly / Lx
        b = -L0 / Lx

        alpha = A2 * (1 + m**2)
        beta = C2 + B2 * m + 2 * A2 * m * b
        gamma = A2 * b**2 + B2 * b + D2

        disc = beta**2 - 4 * alpha * gamma

        if disc < -eps:
            return []

        disc = max(disc, 0.0)
        s = math.sqrt(disc)

        y_sol1 = (-beta + s) / (2 * alpha)
        y_sol2 = (-beta - s) / (2 * alpha)

        x_sol1 = m * y_sol1 + b
        x_sol2 = m * y_sol2 + b

        if disc < eps:
            return [(x_sol1, y_sol1)]

        return [(x_sol1, y_sol1), (x_sol2, y_sol2)]

    else:
        raise ValueError("The two equations are degenerate.")

solutions = solve_position(
    x1=0, y1=0,
    x2=10, y2=0,
    x3=0, y3=10,
    r21=0.8,
    r31=0.9,
)

for x, y in solutions:
    print(x, y)
