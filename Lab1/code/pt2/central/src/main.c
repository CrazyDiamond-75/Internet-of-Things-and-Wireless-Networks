#include <main.h>
#include <sys/byteorder.h>


// Push perform read to work queue

static uint8_t read_temperature(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params, const void *data, uint16_t length)
{
    if (err)
    {
        printk("Read callback error (%d)\n", err);
        return BT_GATT_ITER_STOP;
    }

    // data == NULL signals read completion, not an error
    if (!data)
    {
        printk("Read complete\n");
        return BT_GATT_ITER_STOP;
    }

    if (length != sizeof(double))
    {
        printk("Unexpected data length: %d\n", length);
        return BT_GATT_ITER_STOP;
    }

    double temp;
    memcpy(&temp, data, sizeof(double));

    printk("Received temperature: %d m°C\n", (int)(temp * 1000));

    return BT_GATT_ITER_STOP; // Single attribute read, stop after first result
}

static int read_characteristic(struct bt_conn *conn, uint16_t handle)
{
    static struct bt_gatt_read_params read_params;

    read_params.func = read_temperature;
    read_params.handle_count = 1;
    read_params.single.handle = handle;
    read_params.single.offset = 0;

    return bt_gatt_read(conn, &read_params);
}

// Replace K_WORK_DEFINE with delayed work
K_WORK_DELAYABLE_DEFINE(read_work, perform_read);

static void perform_read(struct k_work *work)
{
    if (!default_conn)
    {
        printk("Not connected, cannot read\n");
        return;
    }

    if (!temp_char_handle)
    {
        printk("Temperature characteristic handle not discovered, cannot read\n");
        return;
    }

    int err = read_characteristic(default_conn, temp_char_handle);
    if (err)
    {
        printk("Read failed, error (%d)\n", err);
    }

    // Reschedule itself every 5 seconds
    k_work_schedule(&read_work, K_SECONDS(5));
}

static uint8_t discover_char_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr)
    {
        printk("Characteristic discovery complete\n");
        params->func = NULL;
        return BT_GATT_ITER_STOP;
    }

    // Retreived characteristic
    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

    // Check if the retreived characteristic is the defined temperature characteristic
    if (!bt_uuid_cmp(chrc->uuid, &char_uuid.uuid))
    {
        temp_char_handle = chrc->value_handle;
        printk("Found temperature characteristic with handle %d\n", temp_char_handle);

        // Perform a read after discovery
        k_work_schedule(&read_work, K_NO_WAIT);
        // perform_read(NULL);

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_characteristic(struct bt_conn *conn)
{
    static struct bt_gatt_discover_params discover_params;

    discover_params.uuid = NULL;
    discover_params.func = discover_char_cb;
    discover_params.start_handle = 0x0001;
    discover_params.end_handle = 0xffff;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    return bt_gatt_discover(conn, &discover_params);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        return;
    }

    printk("Connected\n"); // Todo: Maybe print the address of the connected device
    default_conn = bt_conn_ref(conn);

    err = discover_characteristic(conn);
    if (err)
    {
        printk("Discover failed, error (%d)\n", err);
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        start_scan(); // Restart scanning if discovery failed
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected, reason (%d)\n", reason);

    if (default_conn)
    {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }

    // Restart scanning after disconnection
    start_scan();
}

// Register connection and disconnection callbacks
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void start_scan(void)
{
    int err;

    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

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
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    // Check if the advertised device has the temperature service UUID in its advertising data
    if (!ad_parse(ad))
    {
        printk("Wrong service, ignoring device: %s with UUID %d\n", addr_str, char_uuid.val);
        return;
    }

    printk("Device found: %s, RSSI %d\n", addr_str, rssi);
    bt_le_scan_stop();

    // Initalize a connection
    int err = bt_conn_le_create(addr,
                                BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT, // Default connection interval, latency and timeout.
                                &default_conn);
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        start_scan(); // Restart scanning if connection failed
    }
}

struct ad_parse_ctx {
    bool found;
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
    struct ad_parse_ctx *ctx = user_data;

    if (data->type == BT_DATA_UUID16_ALL || data->type == BT_DATA_UUID16_SOME)
    {
        // service_uuid.val is 0x1809, stored little-endian
        for (int i = 0; i + 1 < data->data_len; i += 2)
        {
            uint16_t uuid = sys_get_le16(&data->data[i]);
            if (uuid == 0x1809) // Health Thermometer Service
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
    struct ad_parse_ctx ctx = { .found = false };
    bt_data_parse(data, ad_parse_cb, &ctx);
    return ctx.found;
}

static void bt_ready(int err)
{
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    start_scan();
}

void main(void)
{
    printk("Starting Central\n");

    int err = bt_enable(bt_ready);
    if (err)
    {
        printk("bt_enable failed (err %d)\n", err);
    }

    printk("bt_enable called\n");

    while (1)
    {
        k_sleep(K_FOREVER);
    }
}