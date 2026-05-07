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

// Stores the value for the last temperature measurement in °C.
static double current_temperature = 20.0;

// Advertisement data.
static const struct bt_data ad[] = {
    // General discoverable, BR/EDR not supported
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    // Manufacturer data containing the current temperature as a double.
    BT_DATA(BT_DATA_MANUFACTURER_DATA, (uint8_t *)&current_temperature, sizeof(double))};

// Scan response data.
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN), // Device name
};

/* Updates the stored temperature, the advertisement, and reports the new current temperature. */
void update_advertisement();

/* Callback for bt_enable after Bluetooth finishes or fails initialization.
 * Starts advertising and updates the advertisement every 5 seconds with the current temperature. */
static void bt_ready(int err);
