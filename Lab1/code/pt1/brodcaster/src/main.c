#include "main.h"

void update_advertisement()
{
    // Update the current temperature value.
    update_temperature(&current_temperature);
    // Update the advertisement with the new temperature value.
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    printk("Updated advertisement with temperature: %dm°C\n", (int)(1000 * current_temperature));
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
        BT_LE_ADV_NCONN_IDENTITY, // Non-connectable advertising with identity address and 100ms-150ms advertising interval.
        ad, ARRAY_SIZE(ad),
        sd, ARRAY_SIZE(sd));
    if (err)
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    // Get the Bluetooth address of the device and print it.
    bt_id_get(&addr, &count);
    bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));

    printk("Brodcaster started, advertising as %s\n", addr_s);
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
    printk("Updating advertisement data every 5 seconds\n");
    while (1)
    {
        k_sleep(K_SECONDS(5));
        update_advertisement();
    }
    return 0;
}