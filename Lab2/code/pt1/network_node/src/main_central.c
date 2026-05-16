#include "main.h"

static uint8_t read_temperature(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length)
{
    // If bt_gatt_read reports an error, print the error and stop.
    if (err)
    {
        printk("Read callback error (%d)\n", err);
        return BT_GATT_ITER_STOP;
    }

    // If data is NULL, the read is complete, print that and stop.
    if (!data)
    {
        printk("Read complete\n");
        return BT_GATT_ITER_STOP;
    }

    // If the received data length is unexpected, print that and stop.
    if (length != sizeof(double))
    {
        printk("Unexpected data length: %d\n", length);
        return BT_GATT_ITER_STOP;
    }

    // Else, copy the received data into a double variable, print it out and stop.
    double temp;
    memcpy(&temp, data, sizeof(double));
    uint32_t now = k_uptime_get_32();
    printk("RECV t=%u temp=%d\n", now, (int)(temp * 1000));

    return BT_GATT_ITER_STOP; // Single attribute read, stop after first result.
}

static int read_characteristic(struct bt_conn *conn, uint16_t handle)
{
    // Initialize parameters for a single attribute read.
    static struct bt_gatt_read_params read_params;

    read_params.func = read_temperature;
    read_params.handle_count = 1;
    read_params.single.handle = handle;
    read_params.single.offset = 0;

    // Start read query. Invoke read_temperature callback after read request attempt.
    return bt_gatt_read(conn, &read_params);
}

static void perform_read(struct k_work *work)
{
    // If not connected, or connection not referenced, report and return.
    if (!default_conn)
    {
        printk("Not connected, cannot read\n");
        return;
    }

    // If the temperature characteristic handle has not been discovered, report and return.
    if (!temp_char_handle)
    {
        printk("Temperature characteristic handle not discovered, cannot read\n");
        return;
    }

    // Else, try to read the temperature characteristic with the discovered handle, and report any errors.
    int err = read_characteristic(default_conn, temp_char_handle);
    if (err)
    {
        printk("Read failed, error (%d)\n", err);
    }

    // Do this again after 5 seconds.
    k_work_schedule(&read_work, K_SECONDS(5));
}

static uint8_t discover_char_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    // If attr is NULL, the discovery is complete.
    if (!attr)
    {
        printk("Characteristic discovery complete\n");
        params->func = NULL;
        return BT_GATT_ITER_STOP;
    }

    // Retreived characteristic.
    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

    // Check if the retreived characteristic is the defined temperature characteristic.
    if (!bt_uuid_cmp(chrc->uuid, &char_uuid.uuid))
    {
        // If it is, save the handle and print it out.
        temp_char_handle = chrc->value_handle;
        printk("Found temperature characteristic with handle %d\n", temp_char_handle);

        // Perform a read after discovery.
        k_work_schedule(&read_work, K_NO_WAIT);

        // Stop discovery after finding the desired characteristic.
        return BT_GATT_ITER_STOP;
    }

    // Continue discovery if this is not the desired characteristic.
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_characteristic(struct bt_conn *conn)
{
    // Initialize discovery parameters for characteristic discovery.
    static struct bt_gatt_discover_params discover_params;

    discover_params.uuid = NULL;
    discover_params.func = discover_char_cb;
    discover_params.start_handle = 0x0001;
    discover_params.end_handle = 0xffff;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    // Start discovery. If successful, invoke discover_char_cb callback.
    return bt_gatt_discover(conn, &discover_params);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    // If bt_conn_le_create failed, report the error and restart scanning.
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        default_conn = NULL;
        start_scan();
        return;
    }

    // Else, connection succeeded, save the connection reference...
    default_conn = bt_conn_ref(conn);
    printk("Connected\n");

    // ... and start service discovery.
    int disc_err = discover_characteristic(conn);
    // Close the connection if discovery failed, and restart scanning.
    if (disc_err)
    {
        printk("Discover failed, error (%d)\n", disc_err);
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        start_scan();
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    // Report the reason why the connection was closed.
    printk("Disconnected, reason (%d)\n", reason);

    // If the connection reference still exists, unreference it and set to NULL.
    if (default_conn)
    {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }

    // Restart scanning after disconnection.
    start_scan();
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
        printk("Starting scanning failed, error (%d)\n", err);
        return;
    }

    printk("Scanning started\n");
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad)
{
    // Convert the address to a string for printing.
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    // Check if the advertised device has the temperature service UUID in its advertising data.
    if (!ad_parse(ad))
    {
        printk("Wrong service, ignoring device: %s\n", addr_str);
        return;
    }

    // Print out the device and stop scanning.
    printk("Device found: %s, RSSI %d\n", addr_str, rssi);
    bt_le_scan_stop();

    // Initalize a connection with the device. If the connection fails, restart scanning.
    // If the connection succeeds, the "connected" callback will be started.
    struct bt_conn *conn;
    int err = bt_conn_le_create(addr,
                                BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT, // Default connection interval, latency and timeout.
                                &conn);
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        start_scan(); // Restart scanning if connection failed
        return;
    }

    bt_conn_unref(conn);
}

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
    struct ad_parse_ctx *ctx = user_data;

    // If the advertising data is some UUID, check if it contains the desired service UUID.
    if (data->type == BT_DATA_UUID16_ALL || data->type == BT_DATA_UUID16_SOME)
    {
        // service_uuid.val is 0x1809, stored little-endian.
        for (int i = 0; i + 1 < data->data_len; i += 2)
        {
            // If the UUIDs match, set ctx->found to true and stop iterating.
            uint16_t uuid = sys_get_le16(&data->data[i]);
            if (uuid == service_uuid.val) // Health Thermometer Service
            {
                ctx->found = true;
                return false; // Stop iterating
            }
        }
    }
    return true; // Continue iterating
}

static bool ad_parse(struct net_buf_simple *data)
{
    // Parse the advertising data and check if it contains the desired service UUID.
    struct ad_parse_ctx ctx = {.found = false};
    bt_data_parse(data, ad_parse_cb, &ctx);
    return ctx.found;
}

static void bt_ready(int err)
{
    // If Bluetooth didn't initialize, report the error and stop...
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
    printk("Bluetooth initialized\n");

    // ... else proceed to start scanning.
    start_scan();
}

// Central main function
void main(void)
{
    printk("Starting Central\n");

    // Initialize Bluetooth, and proceed to bt_ready.
    int err = bt_enable(bt_ready);
    if (err)
    {
        printk("bt_enable failed (err %d)\n", err);
    }

    printk("bt_enable called\n");

    // Sleep forever, all work is done in callbacks.
    while (1)
    {
        k_sleep(K_FOREVER);
    }
}