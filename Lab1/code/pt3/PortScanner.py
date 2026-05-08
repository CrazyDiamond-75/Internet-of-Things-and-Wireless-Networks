import socket
import threading
import time
import re
import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt

sent_packets = []  # list of (packet_id, timestamp_ms, temp)
recv_packets = []  # list of (timestamp_ms, temp)
lock = threading.Lock()
running = True


def read_uart(host, port, label, callback):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.settimeout(1.0)
        buf = ""
        while running:
            try:
                chunk = s.recv(1024).decode('utf-8', errors='ignore')
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
                print(f"{label} error: {e}")
                break


def on_peripheral_line(line):
    m = re.search(r'SEND id=(\d+) t=(\d+) temp=(-?\d+)', line)
    if m:
        pid, ts, temp = int(m.group(1)), int(m.group(2)), int(m.group(3))
        print(f"  >>> MATCHED SEND: id={pid} t={ts} temp={temp}")
        with lock:
            sent_packets.append((pid, ts, temp))


def on_central_line(line):
    m = re.search(r'RECV t=(\d+) temp=(-?\d+)', line)
    if m:
        ts, temp = int(m.group(1)), int(m.group(2))
        print(f"  >>> MATCHED RECV: t={ts} temp={temp}")
        with lock:
            recv_packets.append((ts, temp))


def collect(duration_seconds):
    global running
    running = True
    t1 = threading.Thread(target=read_uart,
                          args=('localhost', 60004, 'PERIPHERAL', on_peripheral_line),
                          daemon=True)
    t2 = threading.Thread(target=read_uart,
                          args=('localhost', 60003, 'CENTRAL', on_central_line),
                          daemon=True)
    t1.start()
    t2.start()
    time.sleep(duration_seconds)
    running = False
    time.sleep(2)  # Give threads time to finish processing


def analyze_and_plot(label="Default"):
    with lock:
        sent = list(sent_packets)
        recv = list(recv_packets)

    print(f"DEBUG: sent_packets has {len(sent)} entries")
    print(f"DEBUG: recv_packets has {len(recv)} entries")

    total = len(sent)
    received = len(recv)

    # Match each recv to the nearest sent with same temp, where t_recv > t_send
    used_sent = set()
    matched = []

    for t_recv, temp_recv in recv:
        best = None
        best_diff = float('inf')
        for i, (pid, t_send, temp_send) in enumerate(sent):
            if i in used_sent:
                continue
            if temp_send == temp_recv and t_recv > t_send:
                diff = t_recv - t_send
                if diff < best_diff:
                    best_diff = diff
                    best = (i, pid, diff)
        if best:
            used_sent.add(best[0])
            matched.append((best[1], best[2]))  # (packet_id, latency)

    lost = total - len(matched)
    reliability = (len(matched) / total * 100) if total > 0 else 0

    print(f"\n=== {label} ===")
    print(f"Sent: {total}, Matched: {len(matched)}, Lost: {lost}")
    print(f"Reliability: {reliability:.1f}%")

    if not matched:
        print("Not enough data to plot")
        return

    ids, lats = zip(*matched)
    avg_lat = sum(lats) / len(lats)
    print(f"Avg latency: {avg_lat:.1f} ms")

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4))
    fig.suptitle(f"BLE GATT Evaluation — {label}")

    ax1.plot(ids, lats, marker='o', label='Latency (ms)')
    ax1.axhline(avg_lat, color='r', linestyle='--', label=f'Avg {avg_lat:.0f} ms')
    ax1.set_xlabel('Packet ID')
    ax1.set_ylabel('Latency (ms)')
    ax1.set_title('Latency per Packet')
    ax1.legend()

    ax2.bar(['Received', 'Lost'], [len(matched), lost], color=['green', 'red'])
    ax2.set_title(f'Reliability: {reliability:.1f}%')
    ax2.set_ylabel('Packet Count')

    plt.tight_layout()
    plt.savefig(f'ble_eval_{label.replace(" ", "_")}.png', dpi=150, bbox_inches='tight')
    print(f"Plot saved as ble_eval_{label.replace(' ', '_')}.png")

if __name__ == '__main__':
    collect(duration_seconds=300)
    analyze_and_plot(label="Distance_12")
