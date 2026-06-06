#include <kernel.h>
#include <net/socket.h>
#include <net/net_ip.h>
#include <net/net_if.h>
#include <net/net_config.h>
#include <logging/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(udp_receiver, LOG_LEVEL_DBG);

#define LISTEN_ADDR  "::"
#define UDP_PORT     4242
#define RX_BUF_SIZE  256

typedef struct {
    int   seq;
    float temperature;
    float humidity;
    int   light;
} sensor_data_t;


static float parse_float(const char *s)
{
    long integer_part = 0;
    long frac_part    = 0;
    int  frac_digits  = 0;
    int  negative     = 0;

    if (*s == '-') { negative = 1; s++; }

    while (*s >= '0' && *s <= '9')
        integer_part = integer_part * 10 + (*s++ - '0');

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && frac_digits < 4) {
            frac_part  = frac_part * 10 + (*s++ - '0');
            frac_digits++;
        }
    }

    while (frac_digits++ < 4)
        frac_part *= 10;

    float result = (float)integer_part + (float)frac_part / 10000.0f;
    return negative ? -result : result;
}

static int parse_sensor_data(const char *payload, sensor_data_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *p;

    p = strstr(payload, "seq=");
    if (!p) return -1;
    out->seq = atoi(p + 4);

    p = strstr(payload, "temp=");
    if (!p) return -1;
    out->temperature = parse_float(p + 5);

    p = strstr(payload, "hum=");
    if (!p) return -1;
    out->humidity = parse_float(p + 4);

    p = strstr(payload, "light=");
    if (!p) return -1;
    out->light = atoi(p + 6);

    return 0;
}

int main(void)
{
    LOG_INF("UDP Receiver starting (802.15.4 / 6LoWPAN)");

    LOG_INF("Waiting for network interface...");
    k_sleep(K_SECONDS(2));

    int sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    struct sockaddr_in6 bind_addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(UDP_PORT),
        .sin6_addr   = in6addr_any,
    };

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("bind failed: %d", errno);
        zsock_close(sock);
        return -errno;
    }

    LOG_INF("Listening on port %d ...", UDP_PORT);

    char rx_buf[RX_BUF_SIZE];
    struct sockaddr_in6 src;
    socklen_t src_len = sizeof(src);

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));

        ssize_t received = zsock_recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                                          (struct sockaddr *)&src, &src_len);
        if (received < 0) {
            LOG_ERR("recvfrom failed: %d", errno);
            k_sleep(K_MSEC(100));
            continue;
        }

        rx_buf[received] = '\0';

        char src_addr_str[NET_IPV6_ADDR_LEN];
        zsock_inet_ntop(AF_INET6, &src.sin6_addr,
                        src_addr_str, sizeof(src_addr_str));

        LOG_INF("RX [%d bytes] from [%s]:%d  raw=\"%s\"",
                (int)received, src_addr_str,
                ntohs(src.sin6_port), rx_buf);

        sensor_data_t data;
        if (parse_sensor_data(rx_buf, &data) == 0) {
            LOG_INF("  Packet #%d", data.seq);
            LOG_INF("  Temperature : %.2f °C",  (double)data.temperature);
            LOG_INF("  Humidity    : %.2f %%",  (double)data.humidity);
            LOG_INF("  Light       : %d lux",   data.light);
        } else {
            LOG_WRN("  Could not parse sensor data — unknown format");
        }
    }

    zsock_close(sock);
    return 0;
}