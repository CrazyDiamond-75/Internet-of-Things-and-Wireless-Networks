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


def collect(duration_seconds=120):
    global running
    running = True
    t1 = threading.Thread(target=read_uart,
                          args=('localhost', 60002, 'PERIPHERAL', on_peripheral_line),
                          daemon=True)
    t2 = threading.Thread(target=read_uart,
                          args=('localhost', 60001, 'CENTRAL', on_central_line),
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

    total = len(sent)
    received = len(recv)
    lost = total - received
    reliability = (received / total * 100) if total > 0 else 0

    print(f"\n=== {label} ===")
    print(f"Sent: {total}, Received: {received}, Lost: {lost}")
    print(f"Reliability: {reliability:.1f}%")

    if not sent or not recv:
        print("Not enough data to plot")
        return

    # Match by temperature value — find closest temp in recv for each sent
    latencies = []
    for pid, t_send, temp_send in sent:
        # Find a recv packet with matching temp
        match = next(((t_recv, temp_recv) for t_recv, temp_recv in recv
                      if temp_recv == temp_send), None)
        if match:
            t_recv, _ = match
            latency = t_recv - t_send
            latencies.append((pid, latency))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4))
    fig.suptitle(f"BLE GATT Evaluation — {label}")

    if latencies:
        ids, lats = zip(*latencies)
        avg_lat = sum(lats) / len(lats)
        print(f"Avg latency: {avg_lat:.1f} ms")

        ax1.plot(ids, lats, marker='o', label='Latency (ms)')
        ax1.axhline(avg_lat, color='r', linestyle='--', label=f'Avg {avg_lat:.0f} ms')
        ax1.set_xlabel('Packet ID')
        ax1.set_ylabel('Latency (ms)')
        ax1.set_title('Latency per Packet')
        ax1.legend()
    else:
        ax1.text(0.5, 0.5, 'No matched packets', ha='center', va='center',
                 transform=ax1.transAxes)

    ax2.bar(['Received', 'Lost'], [received, lost], color=['green', 'red'])
    ax2.set_title(f'Reliability: {reliability:.1f}%')
    ax2.set_ylabel('Packet Count')

    plt.tight_layout()
    plt.savefig(f'ble_eval_{label.replace(" ", "_")}.png')


if __name__ == '__main__':
    collect(duration_seconds=300)
    analyze_and_plot(label="Distance_12")
