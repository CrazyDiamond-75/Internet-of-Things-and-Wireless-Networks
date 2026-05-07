#include <zephyr.h>
#include <sys/printk.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

/* Callback for bt_data_parse.
 * Looks for the manufacturer-specific data field,
 * interpret it as a double-typed temperature value, and print it. */
static bool parse_cb(struct bt_data *data, void *user_data);

/* This function is called every time a BLE advertisement is received.
 * It prints basic info about the device (address + signal strength),
 * and then parses the advertisement payload to extract useful data. */
static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad);

/* Configures BLE scan parameters and starts scanning.
 * Once scanning is active, device_found() will be called
 * repeatedly whenever advertisements are received. */
static void start_scan(void);

/* Callback for bt_enable after Bluetooth finishes or fails initialization.*/
static void bt_ready(int err);