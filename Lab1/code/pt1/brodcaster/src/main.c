#include <zephyr.h>

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#include "temperature.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static double current_temperature = 20.0;


// TODO: Maybe send the temperature as an integer of millidegrees Celsius (as its printed), as its easier to handle.
/* Set Advertisement data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, (uint8_t *)&current_temperature, sizeof(double))};

/* Set Scan Response data */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

void update_advertisement()
{
    update_temperature(&current_temperature);
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    printk("Updated advertisement with temperature: %dm°C\n", (int) (1000 * current_temperature));
}

static void bt_ready(int err)
{
    char addr_s[BT_ADDR_LE_STR_LEN];
    bt_addr_le_t addr = {0};
    size_t count = 1;

    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    /* Start advertising */
    err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err)
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    /* For connectable advertising you would use
     * bt_le_oob_get_local().  For non-connectable non-identity
     * advertising an non-resolvable private address is used;
     * there is no API to retrieve that.
     */

    bt_id_get(&addr, &count);
    bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));

    printk("Brodcaster started, advertising as %s\n", addr_s);

    printk("Updating advertisement data every 5 seconds\n");
    while (1)
    {
        k_sleep(K_SECONDS(5));
        update_advertisement();
    }
}

int main(void)
{
    int err;

    printk("Starting Temperature Brodcaster\n");

    /* Initialize the Bluetooth Subsystem */
    err = bt_enable(bt_ready);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
    }
    return 0;
}