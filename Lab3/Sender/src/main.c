#include <kernel.h>
#include <net/socket.h>
#include <net/net_ip.h>
#include <net/net_if.h>
#include <net/net_config.h>
#include <logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(udp_sender, LOG_LEVEL_DBG);

#define RECEIVER_ADDR   "2001:db8::2"
#define UDP_PORT        4242
#define SEND_INTERVAL_MS 2000

static float sim_temperature = 22.0f;
static float sim_humidity    = 55.0f;
static int   sim_light       = 300;
static int   seq_num         = 0;

static void update_sensors(void)
{
    static int tick = 0;
    tick++;

    sim_temperature = 22.5f + 2.5f * (float)((tick % 10) - 5) / 5.0f;

    sim_humidity = 57.5f + 7.5f * (float)((tick % 8) - 4) / 4.0f;

    sim_light = 200 + (tick % 5) * 100;
}

int main(void)
{
    LOG_INF("UDP Sender starting (802.15.4 / 6LoWPAN)");

    LOG_INF("Waiting for network interface...");
    k_sleep(K_SECONDS(2));

    int sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    struct sockaddr_in6 dst = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(UDP_PORT),
    };

    if (zsock_inet_pton(AF_INET6, RECEIVER_ADDR, &dst.sin6_addr) != 1) {
        LOG_ERR("Invalid receiver address");
        zsock_close(sock);
        return -EINVAL;
    }

    LOG_INF("Sending to [%s]:%d every %d ms",
            RECEIVER_ADDR, UDP_PORT, SEND_INTERVAL_MS);

    while (1) {
        update_sensors();
        seq_num++;

        char payload[128];
        int len = snprintf(payload, sizeof(payload),
                           "seq=%d,temp=%.2f,hum=%.2f,light=%d",
                           seq_num,
                           (double)sim_temperature,
                           (double)sim_humidity,
                           sim_light);

        ssize_t sent = zsock_sendto(sock, payload, len, 0,
                                    (struct sockaddr *)&dst,
                                    sizeof(dst));
        if (sent < 0) {
            LOG_ERR("sendto failed: %d (errno %d)", (int)sent, errno);
        } else {
            LOG_INF("TX [%d bytes] %s", (int)sent, payload);
        }

        k_sleep(K_MSEC(SEND_INTERVAL_MS));
    }

    zsock_close(sock);
    return 0;
}