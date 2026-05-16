#include <zephyr.h>

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/gatt.h>

#include "temperature.h"

//#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
//#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// Stores the value for the last temperature measurement in °C.
static double current_temperature = 20.0;

// FOR UUIDS, SEE REF A: https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf

static struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0x1809); // UUID for health thermometor service (A section 3.4.1 Services by Name)
static struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0x2A1C);    // Temperature measurement (A section 3.8.1 Characteristics by Name)

// Advertising data which will be broadcasted by the device.
// Uses little-endian encoding, which is why the bytes are reversed in comparison to the UUIDs above.
static struct bt_data ad[] = {
    // General discoverable, BR/EDR not supported
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),

    // Generic Thermometer (A section 2.6.3 Apperance Sub-category values)
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, 0x00, 0x03),

    // Service UUID (...)
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x09, 0x18),
};

/* Read callback for the temperature characteristic.
 * Invoked, when a central wants to read the temperature characteristic. */
static ssize_t read_temperature_callback(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         void *buf, uint16_t len, uint16_t offset);

// Declare and define the GATT service which the peripheral hosts.
BT_GATT_SERVICE_DEFINE(temperature_service,
                       BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_temperature_callback, NULL, &current_temperature),
                       // BT_GATT_CCC(ccc_callback, BT_GATT_PERM_READ), // Implement CCC change callback if we want to support notifications (asynchronous updates)
);

/* Callback for bt_enable after Bluetooth finishes or fails initialization.*/
static void bt_ready(int err);
