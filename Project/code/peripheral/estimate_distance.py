import argparse
import ast
import statistics
import math
import sys
import matplotlib.pyplot as plt


def rssi_to_distance(rssi: float, rssi0: float, N: float) -> float:
    return 10.0 ** ((rssi0 - rssi) / (10.0 * N))


WINDOW_MS = 5000


def auto_calibrate(samples: list[tuple[int, int]]) -> tuple[float, int, int]:
    if not samples:
        raise ValueError("No samples to calibrate from.")

    best_median = -math.inf
    best_start = best_end = 0

    for i, (t, _) in enumerate(samples):
        window = [r for ts, r in samples if t <= ts < t + WINDOW_MS]
        if len(window) < 3:
            continue
        m = statistics.median(window)
        if m > best_median:
            best_median = m
            best_start = t
            best_end = t + WINDOW_MS

    return float(best_median), best_start, best_end


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", required=True, help="Path to .tout log file from peripheral.py")
    parser.add_argument("--mac", required=True, help="MAC address of the target device")
    parser.add_argument("--rssi0", type=float, default=None,
                        help="RSSI at 1 m in dBm (skip auto-calibration if provided)")
    parser.add_argument("--cal-start", type=int, default=None,
                        help="Start of calibration window in ms (optional)")
    parser.add_argument("--cal-end", type=int, default=None,
                        help="End of calibration window in ms (optional)")
    parser.add_argument("--N", type=float, default=2.5,
                        help="Path-loss exponent (default 2.5, try 2-4 for indoors)")
    args = parser.parse_args()

    all_samples: list[tuple[int, int]] = []
    with open(args.file) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            msg = ast.literal_eval(line)
            if msg["mac"] == args.mac:
                all_samples.append((msg["timestamp"], msg["rssi"]))

    if not all_samples:
        sys.exit(f"No messages found for MAC {args.mac} in {args.file}")

    all_samples.sort()
    print(f"Loaded {len(all_samples)} samples for {args.mac}")

    cal_start = cal_end = None

    if args.rssi0 is not None:
        rssi0 = args.rssi0
        print(f"Using provided rssi0 = {rssi0:.1f} dBm")
    else:
        if args.cal_start is not None and args.cal_end is not None:
            cal_samples = [(t, r) for t, r in all_samples
                           if args.cal_start <= t <= args.cal_end]
            if len(cal_samples) < 3:
                sys.exit("Fewer than 3 samples in calibration window.")
            rssi0 = statistics.median(r for _, r in cal_samples)
            cal_start, cal_end = args.cal_start, args.cal_end
            print(f"Calibration window [{cal_start}–{cal_end} ms]: "
                  f"{len(cal_samples)} samples, rssi0 = {rssi0:.1f} dBm")
        else:
            rssi0, cal_start, cal_end = auto_calibrate(all_samples)
            print(f"Auto-calibrated from strongest window "
                  f"[{cal_start}–{cal_end} ms]: rssi0 = {rssi0:.1f} dBm")

    timestamps_s = [t / 1000.0 for t, _ in all_samples]
    rssies = [r for _, r in all_samples]
    distances = [rssi_to_distance(r, rssi0, args.N) for r in rssies]

    print(f"\nPath-loss exponent N = {args.N}")
    print(f"Distance range: {min(distances):.2f} m – {max(distances):.2f} m")

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    fig.suptitle(f"Distance estimation — {args.mac}\n"
                 f"rssi0 = {rssi0:.1f} dBm @ 1 m,  N = {args.N}")

    ax1.plot(timestamps_s, rssies, ".", markersize=3, color="steelblue", label="RSSI")
    ax1.set_ylabel("RSSI (dBm)")
    ax1.axhline(rssi0, color="orange", linestyle="--", linewidth=1,
                label=f"rssi0 = {rssi0:.1f} dBm")
    if cal_start is not None:
        ax1.axvspan(cal_start / 1000, cal_end / 1000, alpha=0.15, color="green",
                    label="calibration window")
    ax1.legend(fontsize=8)
    ax1.grid(True, alpha=0.3)

    ax2.plot(timestamps_s, distances, ".", markersize=3, color="tomato", label="distance")
    ax2.axhline(1.0, color="orange", linestyle="--", linewidth=1, label="1 m reference")
    ax2.set_ylabel("Estimated distance (m)")
    ax2.set_xlabel("Time (s)")
    ax2.set_ylim(bottom=0)
    if cal_start is not None:
        ax2.axvspan(cal_start / 1000, cal_end / 1000, alpha=0.15, color="green",
                    label="calibration window")
    ax2.legend(fontsize=8)
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
