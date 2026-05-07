#include "main.h"

static ssize_t read_temperature_callback(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len, uint16_t offset)
{
    // Execute the read callback and store the result in the provided buffer.
    // (the temperature is stored in the user_data field of the attribute.)
    return bt_gatt_attr_read(conn, attr, buf, len, offset, (double *)attr->user_data, sizeof(double));
}

static void bt_ready(int err)
{
    char addr_s[BT_ADDR_LE_STR_LEN];
    bt_addr_le_t addr = {0};
    size_t count = 1;

    // If bt_enable failed, print an error message and return.
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    // Start advertising the device with the specified advertising data and parameters.
    err = bt_le_adv_start(
        BT_LE_ADV_PARAM(
            BT_LE_ADV_OPT_CONNECTABLE, // Allow connections.
            BT_GAP_ADV_FAST_INT_MIN_2, // 100ms minimum advertising interval.
            BT_GAP_ADV_FAST_INT_MAX_2, // 150ms maximum advertising interval.
            NULL), // Undirected advertising.
        ad,
        ARRAY_SIZE(ad),
        NULL, // No scan response data.
        0);
    if (err)
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    // Get the Bluetooth address of the device and print it.
    bt_id_get(&addr, &count);
    bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));

    printk("Brodcaster started, advertising as %s\n", addr_s);

    // The GATT service does not have to be explicitly registered, as this is already done by the BT_GATT_SERVICE_DEFINE macro.
    // However, if we had to register it manually, we would call bt_gatt_service_register() here.
}

int main(void)
{
    int err;

    printk("Starting Temperature Brodcaster\n");

    // Initialize the Bluetooth subsystem, continue with bt_ready callback when finished.
    err = bt_enable(bt_ready);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
    }

    // After starting advertising, we can update the temperature value periodically.
    printk("Updating temperature data every 5 seconds...\n");
    while (1)
    {
        k_sleep(K_SECONDS(5));
        update_temperature(&current_temperature);
        printk("Current temperature: %dm°C\n", (int)(1000 * current_temperature));
    }

    return 0;
}