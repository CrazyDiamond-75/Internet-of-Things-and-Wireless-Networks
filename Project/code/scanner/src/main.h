#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// Has sadly to be set for each device manually with my ESP32s.
#define NODE_ID 1

/* Calibration constants for the log-distance path loss model used by
 * rssi_to_distance().
 *
 * TX_POWER_AT_1M: the RSSI (dBm) you measure at exactly 1 meter from a
 * typical advertiser. This is device-dependent; -59 dBm is a commonly
 * cited default for BLE phones/wearables.
 *
 * PATH_LOSS_EXPONENT: how fast signal decays with distance.
 *   ~2.0  free space / open air
 *   ~2.5-3.0  indoors, line of sight, few obstacles
 *   ~3.0-4.0  indoors, walls/furniture/people in the way
 */
#define TX_POWER_AT_1M (-59)
#define PATH_LOSS_EXPONENT (2.5f)

/* Very slow advertising. */
#define BT_LE_ADV_CONN_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL)

/* Messages we want to send contain a node ID, a timestamp, RSSI, sender
   address, and an estimated distance derived from the RSSI.
   Packed to safe space. */
typedef struct __packed
{
    uint8_t nodeID;      // Our node ID. Either 1,2, or 3.
    int64_t timestamp;   // Used by the central to calculate accurate positioning.
    int8_t rssi;         // RSSI of the advertisement we received.
    bt_addr_le_t sender; // Address of the device which sent the advertisement.
    float distance;      // Estimated distance (meters) derived from rssi.
} message_t;

/* Bluetooth ready callback, called when the Bluetooth stack is initialized and ready to use. */
static void bt_ready(int err);

/* Converts a received RSSI to an estimated distance in meters using the
 * log-distance path loss model. See TX_POWER_AT_1M and PATH_LOSS_EXPONENT
 * above for calibration notes. */
static float rssi_to_distance(int8_t rssi);

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