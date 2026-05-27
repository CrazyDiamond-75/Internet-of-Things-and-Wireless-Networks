#include <zephyr.h>
#include <zephyr/types.h>

#include <stddef.h>
#include <stdint.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/byteorder.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/conn.h>

#include <button.h>
#include <temperature_humidity.h>

/* Struct used to pass external information on boot to the main thread. */
typedef struct
{
    uint32_t magic;
    uint32_t role;
    uint32_t node_id;
} boot_config_t;

/* Messages we want to send contain a node ID, a timestamp, and temperature and humidity data. */
typedef struct
{
    uint8_t nodeID; // Node from which the measurement originates from.
    uint32_t timestamp;
    uint8_t temperature; // Represent as fixed-point number in range -25°C to 200°C.
    uint8_t humidity; // ... in range 0 to 1.
} message_t;

/* Corresponds to boot_config.role. */
enum
{
    ROLE_SOURCE = 0,
    ROLE_TARGET = 1
};

/* Reserved address for role configuration. */
#define CONFIG_ADDR 0x2003F000

/* Magic number to check if boot_config is valid. */
#define MAGIC_NUMBER 0xDEADBEEF

/* Number of nodes we want the nodes to maximally connect to. Restricted to 8 for "IoT performance reasons" */
#define MAX_CONNECTIONS 8

/* Reads configuration form CONFIG_ADDR and sets role and node_id accordingly. */
static void configure();

/* Methods which execute the wanted role. */
static void role_SOURCE(void);
static void role_TARGET(void);

/* Bluetooth ready callback, called when the Bluetooth stack is initialized and ready to use. */
static void bt_ready(int err);

/* Starts advertising with our service UUID. */
static void start_advertise(void);

/* Start scanning for other nodes to connect to. */
static int start_scan(void);

/* Handler which enqueues a delayed scan operation after 1 second. */
static void scan_work_handler(struct k_work *work);

/* Tries to initiate a connection to a scanned device if it advertises the wanted service. */
static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad);

/* Helper function to parse the advertising data and check if it contains the desired service UUID. */
static bool ad_parse(struct net_buf_simple *data);

/* Callback used by ad_parse to iterate through the advertising data and check for the presence of the desired service UUID. */
static bool ad_parse_cb(struct bt_data *data, void *user_data);

/* Connection callback, called after bt_conn_le_create when a connection is established.
 * Starts service and characteristic discovery. */
static void connected(struct bt_conn *conn, uint8_t err);

/* Disconnection callback, called when a connection is terminated, e.g, bt_conn_disconnect.
 * Reports why a connection was closed. */
static void disconnected(struct bt_conn *conn, uint8_t reason);

/* Characteristic discovery. */
static int discover_characteristic(struct bt_conn *conn);

/* BLE discovery callback. */
static uint8_t discover_cb(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params);

/* Callback for outgoing write requests. Only used for error handling. */
static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params);

/* Callback for received write requests. For TARGET nodes, redirects message after setting/unsetting led. */
static ssize_t on_write(struct bt_conn *conn,
                        const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len,
                        uint16_t offset, uint8_t flags);

/* Method for the SOURCE nodes, sends the current measurement to connected nodes. */
static void send_message_SOURCE();

/* Method for the SOURCE nodes, redirects some message to all connected nodes except the one it was received from. */
static void redirect_message_SOURCE(message_t message, int source_index);

/* Sends a message to all connected nodes. */
static void send_message_to_all(message_t *msg, int skip_index);
