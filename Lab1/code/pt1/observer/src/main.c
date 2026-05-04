#include <zephyr.h>
#include <sys/printk.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>


/* looks for the manufacturer-specific data field,
 * interpret it as a temperature value, and print it.
 */

static bool parse_cb(struct bt_data *data, void *user_data)
{
    if (data->type == BT_DATA_MANUFACTURER_DATA) {
        if (data->data_len == sizeof(double)) {
            double temp;

            memcpy(&temp, data->data, sizeof(double));

            printk("Received temperature: %d m°C\n", (int)(temp * 1000));
        } else {
            printk("Unexpected manufacturer data size: %d\n", data->data_len);
        }
    }

    return true; 
}

/* This function is called every time a BLE advertisement is received.
 * It prints basic info about the device (address + signal strength),
 * and then parses the advertisement payload to extract useful data.
 */

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

    bt_data_parse(ad, parse_cb, NULL);
}

/* Configures BLE scan parameters and starts scanning.
 * Once scanning is active, device_found() will be called
 * repeatedly whenever advertisements are received.
 */

static void start_scan(void)
{
    int err;

    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_ACTIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = BT_GAP_SCAN_FAST_INTERVAL,
        .window     = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, device_found);
    if (err) {
        printk("Starting scanning failed (err %d)\n", err);
        return;
    }

    printk("Scanning started\n");
}

static void bt_ready(int err)
{
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    start_scan();
}

void main(void)
{
    printk("Starting Observer\n");

    int err = bt_enable(bt_ready);
    if (err) {
        printk("bt_enable failed (err %d)\n", err);
    }

    printk("bt_enable called\n");
}