#include <kernel.h>
#include <net/socket.h>
#include <net/net_ip.h>
#include <net/net_if.h>
#include <net/net_config.h>
#include <net/coap.h>
#include <logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(coap_client, LOG_LEVEL_DBG);

#define SERVER_ADDR      "2001:db8::2"
#define COAP_PORT        5683
#define REQUEST_INTERVAL_MS 3000
#define BUF_SIZE         256

static void setup_network(void)
{
    k_sleep(K_MSEC(500));
    struct net_if *iface = net_if_get_default();
    struct in6_addr addr;
    net_addr_pton(AF_INET6, "2001:db8::1", &addr);
    net_if_ipv6_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
    LOG_INF("Network setup complete");
}

/* Build and send a CoAP GET /sensor request */
static int send_get_sensor(int sock, struct sockaddr_in6 *dst, uint16_t msg_id, uint8_t token)
{
    uint8_t buf[BUF_SIZE];
    struct coap_packet pkt;
    int r;

    r = coap_packet_init(&pkt, buf, sizeof(buf), COAP_VERSION_1, COAP_TYPE_CON,
                          1, &token, COAP_METHOD_GET, msg_id);
    if (r < 0) {
        LOG_ERR("packet_init failed: %d", r);
        return r;
    }

    r = coap_packet_append_option(&pkt, COAP_OPTION_URI_PATH,
                                   (uint8_t *)"sensor", strlen("sensor"));
    if (r < 0) {
        LOG_ERR("append_option failed: %d", r);
        return r;
    }

    r = zsock_sendto(sock, pkt.data, pkt.offset, 0,
                      (struct sockaddr *)dst, sizeof(*dst));
    if (r < 0) {
        LOG_ERR("sendto failed: %d (errno %d)", r, errno);
        return -errno;
    }

    LOG_INF("Sent GET /sensor (id=%d, %d bytes)", msg_id, r);
    return 0;
}

/* Build and send a CoAP POST /action request with a text payload */
static int send_post_action(int sock, struct sockaddr_in6 *dst, uint16_t msg_id,
                              uint8_t token, const char *cmd)
{
    uint8_t buf[BUF_SIZE];
    struct coap_packet pkt;
    int r;

    r = coap_packet_init(&pkt, buf, sizeof(buf), COAP_VERSION_1, COAP_TYPE_CON,
                          1, &token, COAP_METHOD_POST, msg_id);
    if (r < 0) {
        LOG_ERR("packet_init failed: %d", r);
        return r;
    }

    r = coap_packet_append_option(&pkt, COAP_OPTION_URI_PATH,
                                   (uint8_t *)"action", strlen("action"));
    if (r < 0) {
        LOG_ERR("append_option failed: %d", r);
        return r;
    }

    r = coap_append_option_int(&pkt, COAP_OPTION_CONTENT_FORMAT,
                                COAP_CONTENT_FORMAT_TEXT_PLAIN);
    if (r < 0) {
        LOG_ERR("append content format failed: %d", r);
        return r;
    }

    r = coap_packet_append_payload_marker(&pkt);
    if (r < 0) {
        LOG_ERR("payload marker failed: %d", r);
        return r;
    }

    r = coap_packet_append_payload(&pkt, (uint8_t *)cmd, strlen(cmd));
    if (r < 0) {
        LOG_ERR("append payload failed: %d", r);
        return r;
    }

    r = zsock_sendto(sock, pkt.data, pkt.offset, 0,
                      (struct sockaddr *)dst, sizeof(*dst));
    if (r < 0) {
        LOG_ERR("sendto failed: %d (errno %d)", r, errno);
        return -errno;
    }

    LOG_INF("Sent POST /action \"%s\" (id=%d, %d bytes)", log_strdup(cmd), msg_id, r);
    return 0;
}

/* Receive and print a CoAP response */
static void receive_response(int sock)
{
    uint8_t buf[BUF_SIZE];
    struct sockaddr_in6 src;
    socklen_t src_len = sizeof(src);

    ssize_t received = zsock_recvfrom(sock, buf, sizeof(buf), 0,
                                       (struct sockaddr *)&src, &src_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == ETIMEDOUT) {
            LOG_WRN("No response received (timeout)");
        } else {
            LOG_ERR("recvfrom failed: %d (errno %d)", (int)received, errno);
        }
        return;
    }

    struct coap_packet response;
    int r = coap_packet_parse(&response, buf, received, NULL, 0);
    if (r < 0) {
        LOG_ERR("Failed to parse CoAP response: %d", r);
        return;
    }

    uint8_t code = coap_header_get_code(&response);
    uint16_t id  = coap_header_get_id(&response);

    uint16_t payload_len;
    const uint8_t *payload = coap_packet_get_payload(&response, &payload_len);

    char payload_str[128] = {0};
    if (payload && payload_len > 0 && payload_len < sizeof(payload_str)) {
        memcpy(payload_str, payload, payload_len);
    }

    LOG_INF("Response: id=%d, code=%d.%02d, payload=\"%s\"",
            id, code >> 5, code & 0x1F, log_strdup(payload_str));
}

int main(void)
{
    LOG_INF("CoAP Client starting (802.15.4 / 6LoWPAN)");
    setup_network();

    int sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    /* Bind to our own address explicitly */
    struct sockaddr_in6 src_addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = 0,
    };
    net_addr_pton(AF_INET6, "2001:db8::1", &src_addr.sin6_addr);
    if (zsock_bind(sock, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
        LOG_ERR("bind failed: %d", errno);
    }

    /* Set a receive timeout so recvfrom doesn't block forever */
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in6 dst = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(COAP_PORT),
    };
    zsock_inet_pton(AF_INET6, SERVER_ADDR, &dst.sin6_addr);

    LOG_INF("Talking to CoAP server at [%s]:%d", SERVER_ADDR, COAP_PORT);

    uint16_t msg_id = 1;
    uint8_t  token  = 0;
    bool     toggle = false;

    while (1) {
        /* GET /sensor */
        send_get_sensor(sock, &dst, msg_id++, token++);
        receive_response(sock);

        k_sleep(K_MSEC(500));

        /* POST /action, alternating "on" / "off" */
        send_post_action(sock, &dst, msg_id++, token++, toggle ? "on" : "off");
        receive_response(sock);
        toggle = !toggle;

        k_sleep(K_MSEC(REQUEST_INTERVAL_MS));
    }

    zsock_close(sock);
    return 0;
}