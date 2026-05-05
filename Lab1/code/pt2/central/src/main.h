#include <zephyr.h>
#include <sys/printk.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

// FOR UUIDS, SEE REF A: https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf

static const struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0x1809); // UUID for health thermometor service (A section 3.4.1 Services by Name)
static const struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0x2A1C);    // Temperature measurement (A section 3.8.1 Characteristics by Name)

static struct bt_conn *default_conn; // Current connection
static uint16_t temp_char_handle;    // Handle for the temperature characteristic

// Read callback for the temperature characteristic
static uint8_t read_temperature(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length);

// Function to initialize read and call callback
static int read_characteristic(struct bt_conn *conn, uint16_t handle);

// Try to read the temperature characteristic based on the discovered handle
static void perform_read(struct k_work *work);

// Characteristic discovery callback
static uint8_t discover_char_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params);

// GATT service and characteristic discovery attempt
static uint8_t discover_characteristic(struct bt_conn *conn);

// Connection callback, called when a connection is established. Starts service and characteristic discovery.
static void connected(struct bt_conn *conn, uint8_t err);

// Disconnection callback, called when a connection is terminated.
static void disconnected(struct bt_conn *conn, uint8_t reason);

/* Configures BLE scan parameters and starts scanning.
 * Once scanning is active, device_found() will be called
 * repeatedly whenever advertisements are received.
 */
static void start_scan(void);

/* This function is called every time a BLE advertisement is received.
 * It prints the address and RSSI of the received advertisement, and then
 * tries to initiate a connection to the device.
 */

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad);

// Bluetooth ready callback, called when the Bluetooth stack is initialized and ready to use.
static void bt_ready(int err);