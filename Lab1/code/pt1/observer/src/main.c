#include "main.h"

static bool parse_cb(struct bt_data *data, void *user_data)
{
    // Because how we defined the advertisement in the sensor code, we expect to find a manufacturer-specific data field.
    if (data->type == BT_DATA_MANUFACTURER_DATA)
    {
        // If the data has the length of a double, interpret it as a double and report it.
        if (data->data_len == sizeof(double))
        {
            double temp;
            memcpy(&temp, data->data, sizeof(double));

            printk("Received temperature: %dm°C\n", (int)(temp * 1000));
        }
        else
        {
            // Else warn about the unexpected data size.
            printk("Unexpected manufacturer data size: %d\n", data->data_len);
        }
    }

    return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad)
{
    // Convert the address to a string for printing.
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

    // Parse the advertisement data using the parse_cb callback..
    bt_data_parse(ad, parse_cb, NULL);
}

static void start_scan(void)
{
    int err;

    // Configure scan parameters: active scanning, no special options, fast interval and window.
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    // Start scanning and check for errors, when a device is found proceed to device_found callback.
    err = bt_le_scan_start(&scan_param, device_found);
    if (err)
    {
        printk("Starting scanning failed (err %d)\n", err);
        return;
    }

    printk("Scanning started\n");
}

static void bt_ready(int err)
{
    // If bt_enable failed, print an error message and return.
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    // Else, start scanning for devices.
    start_scan();
}

void main(void)
{
    printk("Starting Observer\n");

    // Initialize the Bluetooth subsystem, continue with bt_ready callback when finished.
    int err = bt_enable(bt_ready);
    if (err)
    {
        printk("bt_enable failed (err %d)\n", err);
    }
}