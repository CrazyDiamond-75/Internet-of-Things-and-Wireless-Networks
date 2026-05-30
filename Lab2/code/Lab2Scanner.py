import socket
import threading
import time
import re
import sys
import matplotlib
import matplotlib.pyplot as plt

matplotlib.use('Agg')

sent_packets = {}
recv_packets = {}
lock = threading.Lock()
running = True

SOURCE_PORTS = [
    ('localhost', 60001),  # device_1, node_id=0
    ('localhost', 60002),  # device_2, node_id=1
    ('localhost', 60003),  # device_3, node_id=2
]
SINK_PORT = ('localhost', 60004)  # device_4, node_id=3

TARGET_SENT = 200

LINE_RE = re.compile(r'^(\d+);(\d+);(-?\d+\.\d+);(\d+\.\d+);(\d+);(\d+)$')


def read_uart(host, port, label, callback):
    while running:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.connect((host, port))
                s.settimeout(1.0)
                buf = ""
                while running:
                    try:
                        chunk = s.recv(1024).decode('utf-8', errors='ignore')
                        if not chunk:
                            break
                        buf += chunk
                        while '\n' in buf:
                            line, buf = buf.split('\n', 1)
                            line = line.strip('\r').strip()
                            if line:
                                print(f"[{label}] {line}")
                                callback(line)
                    except socket.timeout:
                        continue
        except Exception as e:
            if running:
                print(f"[{label}] connection error: {e}, retrying in 2s...")
                time.sleep(2)


def on_source_line(line):
    m = LINE_RE.match(line)
    if not m:
        return
    node_id = int(m.group(1))
    counter = int(m.group(2))
    ts = int(m.group(5))
    tx_time = int(m.group(6))

    if tx_time != 0:
        return

    with lock:
        sent_packets.setdefault(node_id, []).append((counter, ts))


def on_sink_line(line):
    m = LINE_RE.match(line)
    if not m:
        return
    node_id = int(m.group(1))
    counter = int(m.group(2))
    ts = int(m.group(5))
    tx_time = int(m.group(6))
    if tx_time == 0:
        return

    with lock:
        recv_packets.setdefault(node_id, []).append((counter, ts, tx_time))


def enough_data():
    with lock:
        if len(sent_packets) < len(SOURCE_PORTS):
            return False
        return all(len(v) >= TARGET_SENT for v in sent_packets.values())


def collect(source_ports, sink_port, duration_seconds=600):
    global running
    running = True

    threads = []
    for i, (host, port) in enumerate(source_ports):
        t = threading.Thread(
            target=read_uart,
            args=(host, port, f'SOURCE_{i}', on_source_line),
            daemon=True)
        t.start()
        threads.append(t)

    sink_t = threading.Thread(
        target=read_uart,
        args=(sink_port[0], sink_port[1], 'SINK', on_sink_line),
        daemon=True)
    sink_t.start()
    threads.append(sink_t)

    deadline = time.time() + duration_seconds
    while time.time() < deadline:
        if enough_data():
            print(f"\nTarget of {TARGET_SENT} sent packets reached for all source nodes. Stopping.")
            break
        with lock:
            counts = {nid: len(v) for nid, v in sent_packets.items()}
        print(f"[Progress] Sent so far: {counts}", flush=True)
        time.sleep(10)
    else:
        print("Timeout reached.")

    running = False
    time.sleep(2)


def analyze_and_plot(label="eval"):
    with lock:
        sent = {k: list(v) for k, v in sent_packets.items()}
        recv = {k: list(v) for k, v in recv_packets.items()}

    source_node_ids = sorted(sent.keys())

    print(f"\n=== Results: {label} ===")

    has_latency = any(len(v) > 0 for v in recv.values())

    fig, axes = plt.subplots(2, len(source_node_ids), figsize=(6 * len(source_node_ids), 8))
    fig.suptitle(f"BLE Evaluation — {label}", fontsize=14)

    if len(source_node_ids) == 1:
        axes = [[axes[0]], [axes[1]]]

    for col, node_id in enumerate(source_node_ids):
        node_sent = {c: ts for c, ts in sent.get(node_id, [])}
        node_recv = recv.get(node_id, [])

        total = len(node_sent)
        matched = [(c, tx) for c, ts, tx in node_recv if c in node_sent]
        received = len(matched)
        lost = total - received
        reliability = (received / total * 100) if total > 0 else 0

        print(f"\n  Node {node_id}:")
        print(f"    Sent: {total}, Received at sink: {received}, Lost: {lost}")
        print(f"    Reliability: {reliability:.1f}%")

        ax_lat = axes[0][col]
        ax_rel = axes[1][col]
        ax_lat.set_title(f"Node {node_id} — Latency (ms)")
        ax_rel.set_title(f"Node {node_id} — Reliability: {reliability:.1f}%")

        if matched:
            ids, lats = zip(*matched)
            avg_lat = sum(lats) / len(lats)
            print(f"    Avg latency: {avg_lat:.1f} ms")
            ax_lat.plot(ids, lats, marker='.', markersize=3, linewidth=0.8, label='Latency (ms)')
            ax_lat.axhline(avg_lat, color='r', linestyle='--', label=f'Avg {avg_lat:.0f} ms')
            ax_lat.set_xlabel('Packet counter')
            ax_lat.set_ylabel('Latency (ms)')
            ax_lat.legend()
        else:
            ax_lat.text(0.5, 0.5, 'No latency data\n(sink received 0 forwarded packets)',
                        ha='center', va='center', transform=ax_lat.transAxes, fontsize=9,
                        color='gray')
            ax_lat.set_title(f"Node {node_id} — Latency (no data)")

        colors = ['green' if received > 0 else 'gray', 'red' if lost > 0 else 'gray']
        ax_rel.bar(['Received', 'Lost'], [received, lost], color=colors)
        ax_rel.set_ylabel('Packet count')
        ax_rel.set_ylim(0, max(total, 1) * 1.1)

    plt.tight_layout()
    filename = f'ble_eval_{label.replace(" ", "_")}.png'
    plt.savefig(filename, dpi=150, bbox_inches='tight')
    print(f"\nPlot saved as {filename}")
    plt.close()


if __name__ == '__main__':
    # Labels e.g.: 4node_noloss, 4node_loss, 2line_noloss, 2line_loss
    label = sys.argv[1] if len(sys.argv) > 1 else "4node_noloss"
    if len(sys.argv) > 2:
        TARGET_SENT = int(sys.argv[2])

    collect(SOURCE_PORTS, SINK_PORT, duration_seconds=600)
    analyze_and_plot(label=label)
