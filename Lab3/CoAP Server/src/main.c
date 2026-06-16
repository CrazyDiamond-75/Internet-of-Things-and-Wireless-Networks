#include <kernel.h>
#include <net/socket.h>
#include <net/net_ip.h>
#include <net/net_if.h>
#include <net/net_config.h>
#include <net/coap.h>
#include <logging/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(coap_server, LOG_LEVEL_DBG);

#define COAP_PORT        5683
#define RX_BUF_SIZE      256
#define RESP_BUF_SIZE    256
#define MAX_OPTIONS      8

/* Simulated sensor data */
static float sim_temperature = 22.5f;
static float sim_humidity    = 55.0f;
static int   sim_light       = 300;

/* Simulated actuator state, toggled via /action */
static int actuator_state = 0;  /* 0 = off, 1 = on */

static void setup_network(void)
{
    k_sleep(K_MSEC(500));
    struct net_if *iface = net_if_get_default();
    struct in6_addr addr;
    net_addr_pton(AF_INET6, "2001:db8::2", &addr);
    net_if_ipv6_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
    LOG_INF("Network setup complete");
}

static void update_sensors(void)
{
    static int tick = 0;
    tick++;
    sim_temperature = 22.5f + 2.5f * (float)((tick % 10) - 5) / 5.0f;
    sim_humidity    = 57.5f + 7.5f * (float)((tick % 8) - 4) / 4.0f;
    sim_light       = 200 + (tick % 5) * 100;
}

static void split_float(float val, int *whole, int *frac)
{
    if (val < 0) val = -val;
    *whole = (int)val;
    *frac  = (int)((val - *whole) * 100.0f);
}

/* Returns true if the request's URI-Path matches `path` (single segment) */
static bool uri_path_is(struct coap_packet *cpkt, const char *path)
{
    struct coap_option options[MAX_OPTIONS];
    int count = coap_find_options(cpkt, COAP_OPTION_URI_PATH, options, MAX_OPTIONS);

    if (count != 1) {
        return false;
    }

    return (options[0].len == strlen(path)) &&
           (memcmp(options[0].value, path, options[0].len) == 0);
}

/* Build and send a CoAP response over the given socket */
static void send_response(int sock, struct sockaddr_in6 *dst, socklen_t dst_len,
                           uint8_t type, uint8_t code, uint16_t id,
                           const uint8_t *token, uint8_t token_len,
                           const char *payload, size_t payload_len)
{
    uint8_t response_data[RESP_BUF_SIZE];
    struct coap_packet response;
    int r;

    r = coap_packet_init(&response, response_data, sizeof(response_data),
                          COAP_VERSION_1, type, token_len, token, code, id);
    if (r < 0) {
        LOG_ERR("Failed to init response: %d", r);
        return;
    }

    if (payload_len > 0) {
        r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
                                    COAP_CONTENT_FORMAT_TEXT_PLAIN);
        if (r < 0) {
            LOG_ERR("Failed to add content format: %d", r);
            return;
        }

        r = coap_packet_append_payload_marker(&response);
        if (r < 0) {
            LOG_ERR("Failed to add payload marker: %d", r);
            return;
        }

        r = coap_packet_append_payload(&response, (uint8_t *)payload, payload_len);
        if (r < 0) {
            LOG_ERR("Failed to add payload: %d", r);
            return;
        }
    }

    r = zsock_sendto(sock, response.data, response.offset, 0,
                     (struct sockaddr *)dst, dst_len);
    if (r < 0) {
        LOG_ERR("Failed to send response: %d (errno %d)", r, errno);
    } else {
        LOG_INF("Sent CoAP response, code=0x%02x, %d bytes", code, r);
    }
}

/* Handle GET /sensor */
static void handle_get_sensor(int sock, struct sockaddr_in6 *src, socklen_t src_len,
                               uint8_t type, uint16_t id,
                               const uint8_t *token, uint8_t token_len)
{
    char payload[80];
    int t_whole, t_frac, h_whole, h_frac;

    update_sensors();
    split_float(sim_temperature, &t_whole, &t_frac);
    split_float(sim_humidity, &h_whole, &h_frac);

    int len = snprintf(payload, sizeof(payload),
                        "{\"temp\":%d.%02d,\"hum\":%d.%02d,\"light\":%d}",
                        t_whole, t_frac, h_whole, h_frac, sim_light);

    uint8_t resp_type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON;

    LOG_INF("GET /sensor -> %s", log_strdup(payload));

    send_response(sock, src, src_len, resp_type, COAP_RESPONSE_CODE_CONTENT,
                   id, token, token_len, payload, len);
}

/* Handle POST/PUT /action */
static void handle_action(int sock, struct sockaddr_in6 *src, socklen_t src_len,
                           struct coap_packet *req, uint8_t type, uint8_t code,
                           uint16_t id, const uint8_t *token, uint8_t token_len)
{
    uint16_t payload_len;
    const uint8_t *req_payload = coap_packet_get_payload(req, &payload_len);
    char cmd[16] = {0};

    if (req_payload && payload_len > 0 && payload_len < sizeof(cmd)) {
        memcpy(cmd, req_payload, payload_len);
        cmd[payload_len] = '\0';
    }

    LOG_INF("%s /action, body=\"%s\"",
            (code == COAP_METHOD_POST) ? "POST" : "PUT",
            log_strdup(cmd));

    if (strncmp(cmd, "on", 2) == 0) {
        actuator_state = 1;
    } else if (strncmp(cmd, "off", 3) == 0) {
        actuator_state = 0;
    }

    LOG_INF("Actuator state is now: %s", actuator_state ? "ON" : "OFF");

    char payload[32];
    int len = snprintf(payload, sizeof(payload), "state=%s",
                        actuator_state ? "on" : "off");

    uint8_t resp_type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON;
    uint8_t resp_code = (code == COAP_METHOD_POST) ?
                         COAP_RESPONSE_CODE_CREATED : COAP_RESPONSE_CODE_CHANGED;

    send_response(sock, src, src_len, resp_type, resp_code,
                   id, token, token_len, payload, len);
}

/* Handle requests to unknown paths */
static void handle_not_found(int sock, struct sockaddr_in6 *src, socklen_t src_len,
                              uint8_t type, uint16_t id,
                              const uint8_t *token, uint8_t token_len)
{
    uint8_t resp_type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON;

    send_response(sock, src, src_len, resp_type, COAP_RESPONSE_CODE_NOT_FOUND,
                   id, token, token_len, NULL, 0);
}

int main(void)
{
    LOG_INF("CoAP Server starting (802.15.4 / 6LoWPAN)");
    setup_network();

    int sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -errno;
    }

    struct sockaddr_in6 bind_addr = {
        .sin6_family = AF_INET6,
        .sin6_port   = htons(COAP_PORT),
        .sin6_addr   = in6addr_any,
    };

    if (zsock_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("bind failed: %d", errno);
        zsock_close(sock);
        return -errno;
    }

    LOG_INF("CoAP server listening on port %d", COAP_PORT);
    LOG_INF("Endpoints: GET /sensor, POST/PUT /action");

    uint8_t rx_buf[RX_BUF_SIZE];
    struct sockaddr_in6 src;
    socklen_t src_len;

    while (1) {
        src_len = sizeof(src);
        memset(rx_buf, 0, sizeof(rx_buf));

        ssize_t received = zsock_recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                                          (struct sockaddr *)&src, &src_len);
        if (received < 0) {
            LOG_ERR("recvfrom failed: %d", errno);
            k_sleep(K_MSEC(100));
            continue;
        }

        struct coap_packet request;
        int r = coap_packet_parse(&request, rx_buf, received, NULL, 0);
        if (r < 0) {
            LOG_ERR("Failed to parse CoAP packet: %d", r);
            continue;
        }

        uint8_t  code      = coap_header_get_code(&request);
        uint8_t  type      = coap_header_get_type(&request);
        uint16_t id        = coap_header_get_id(&request);
        uint8_t  token[8];
        uint8_t  token_len = coap_header_get_token(&request, token);

        char src_addr_str[NET_IPV6_ADDR_LEN];
        zsock_inet_ntop(AF_INET6, &src.sin6_addr, src_addr_str, sizeof(src_addr_str));

        LOG_INF("CoAP request from [%s]:%d, code=0x%02x, type=%d",
                log_strdup(src_addr_str), ntohs(src.sin6_port), code, type);

        if (code == COAP_METHOD_GET && uri_path_is(&request, "sensor")) {
            handle_get_sensor(sock, &src, src_len, type, id, token, token_len);

        } else if ((code == COAP_METHOD_POST || code == COAP_METHOD_PUT) &&
                   uri_path_is(&request, "action")) {
            handle_action(sock, &src, src_len, &request, type, code, id, token, token_len);

        } else {
            LOG_WRN("Unhandled request (code=0x%02x), responding 4.04", code);
            handle_not_found(sock, &src, src_len, type, id, token, token_len);
        }
    }

    zsock_close(sock);
    return 0;
}