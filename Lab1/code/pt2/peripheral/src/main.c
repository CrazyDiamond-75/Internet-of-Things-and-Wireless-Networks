#include <zephyr.h>

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/gatt.h>

#include "temperature.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static double current_temperature = 20.0;

// FOR UUIDS, SEE REF A: https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf

static const struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0x1809); // UUID for health thermometor service (A section 3.4.1 Services by Name)
static const struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0x2A1C);    // Temperature measurement (A section 3.8.1 Characteristics by Name)

/* Set Advertisement data */
// Currently does not work that well, uncommenting the name field causes the advertisment data to be too big.
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),      // General discoverable, BR/EDR not supported
    BT_DATA(BT_DATA_GAP_APPEARANCE, 0x0300, sizeof(uint16_t)),               // Generic Thermometer (A section 2.6.3 Apperance Sub-category values)
    //BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),            // Name of the device
    BT_DATA(BT_DATA_UUID16_ALL, service_uuid.val, sizeof(service_uuid.val)), // Advertise the service UUID so that clients can filter for it.
};

#define USE_READ_CALLBACK 0

// GATT service declaration and registration.
BT_GATT_SERVICE_DEFINE(temperature_service,
                       BT_GATT_PRIMARY_SERVICE(&service_uuid),
#if !USE_READ_CALLBACK
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, NULL, NULL, &current_temperature), // Either read current_temperature directly or use a read callback to read it.
#else
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_temperature_callback, NULL, NULL),
#endif
                       // BT_GATT_CCC(callback, BT_GATT_PERM_READ), // Implement CCC change callback if we want to support notifications (asynchronous updates)
);

// Read callback for the temperature characteristic
#if USE_READ_CALLBACK
static ssize_t read_temperature_callback(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &current_temperature, sizeof(current_temperature));
}
#endif

// No scan response data needed, but could be added.

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
    err = bt_le_adv_start(
        BT_LE_ADV_PARAM(
            BT_LE_ADV_OPT_CONNECTABLE, // Allow connections
            BT_GAP_ADV_FAST_INT_MIN_2,
            BT_GAP_ADV_FAST_INT_MAX_2,
            NULL),
        ad,
        ARRAY_SIZE(ad),
        NULL, // No scan response data.
        0);
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

    // The GATT service does not have to be explicitly registered, as this is already done by the BT_GATT_SERVICE_DEFINE macro.
    // However, if we had to register it manually, we would call bt_gatt_service_register() here.

    printk("Brodcaster started, advertising as %s\n", addr_s);
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

    printk("Updating temperature data every 5 seconds\n");
    while (1)
    {
        k_sleep(K_SECONDS(5));
        update_temperature(&current_temperature);
        printk("Current temperature: %dm°C\n", (int)(1000 * current_temperature));
    }

    return 0;
}