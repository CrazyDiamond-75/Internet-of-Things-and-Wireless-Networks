#include "main.h"

// Declare pointer to configuration at reserved address.
volatile boot_config_t *config = (volatile boot_config_t *)CONFIG_ADDR;

// Stores the current connections.
struct bt_conn *default_connections[MAX_CONNECTIONS];
short num_connections = 0;

// Stores the GATT characteristic handle per connection, filled during discovery.
static uint16_t char_handles[MAX_CONNECTIONS];

struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0xACDC); // Custom service UUID
struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0xDEAF);    // Custom characteristic UUID

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// True once this node has triggered its downstream scan and completed the chain.
static bool network_formed = false;

// KWork item associated with scan_work_handler.
static struct k_work_delayable scan_work;

static void scan_work_handler(struct k_work *work)
{
    if (network_formed) return;

    int err = start_scan();
    if (err)
    {
        // Controller still busy, retry.
        k_work_schedule(&scan_work, K_MSEC(1000));
    }
}

static void led_init(void)
{
    if (!device_is_ready(led.port))
    {
        printk("LED GPIO not ready\n");
        return;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

static void led_set(bool on)
{
    gpio_pin_set_dt(&led, on ? 1 : 0);
}

// Write parameters for each connection.
static struct bt_gatt_write_params write_params[MAX_CONNECTIONS];

static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params)
{
    if (err)
    {
        printk("Write failed, error (%u)\n", err);
    }
}

static void send_message_to_all(message_t *msg, int skip_index)
{
    for (short i = 0; i < num_connections; i++)
    {
        if (i == skip_index)                continue;
        if (default_connections[i] == NULL) continue;
        if (char_handles[i] == 0)           continue;

        write_params[i].func = write_cb;
        write_params[i].handle = char_handles[i];
        write_params[i].offset = 0;
        write_params[i].data = msg;
        write_params[i].length = sizeof(message_t);

        int err = bt_gatt_write(default_connections[i], &write_params[i]);
        if (err)
        {
            printk("Write to conn[%d] failed (%d)\n", i, err);
        }
    }
}

static void configure(void)
{
    if (config->magic != MAGIC_NUMBER)
    {
        printk("Invalid magic number: 0x%08X\n", config->magic);
        return;
    }
    printk("Configuration: role=%u node_id=%u\n", config->role, config->node_id);
}

static void redirect_message_TARGET(message_t message, int source_index)
{
    // Static variables to keep track of the last button event and its timestamp, which are used to determine if a received message is recent enough.
    static uint32_t lastTimestamp = 0;
    static uint8_t currEvent = RELEASED;

    if (message.timestamp > lastTimestamp)
    {
        lastTimestamp = message.timestamp;
        currEvent = message.event;
    }
    else
    {
        return;
    }

    if (currEvent == PRESSED)
    {
        printk("Button pressed at timestamp %u\n", lastTimestamp);
        led_set(true);
    }
    else if (currEvent == RELEASED)
    {
        printk("Button released at timestamp %u\n", lastTimestamp);
        led_set(false);
    }
    else
    {
        printk("Malformed button event: 0x%02X\n", currEvent);
        return;
    }

    send_message_to_all(&message, source_index);
}

static void send_message_SOURCE(void)
{
    static uint32_t timestamp = 0;
    static uint8_t event = PRESSED; // The button is pressed, when the first message should be sent.

    message_t message = {
        .timestamp = timestamp,
        .event = event,
    };

    timestamp++;
    event = (event == PRESSED) ? RELEASED : PRESSED;

    if (message.event == PRESSED)
        printk("Button pressed at timestamp %u\n", message.timestamp);
    else
        printk("Button released at timestamp %u\n", message.timestamp);

    led_set(message.event == PRESSED);
    send_message_to_all(&message, -1);
}

static ssize_t on_write(struct bt_conn *conn,
                        const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len,
                        uint16_t offset, uint8_t flags)
{
    if (len != sizeof(message_t))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    message_t msg;
    memcpy(&msg, buf, sizeof(msg));

    // Find which connection index this came from.
    int src = -1;
    for (short i = 0; i < num_connections; i++)
    {
        if (default_connections[i] == conn) { src = i; break; }
    }

    redirect_message_TARGET(msg, src);
    return len;
}

// Expose our characteristic so neighbours can write to us.
BT_GATT_SERVICE_DEFINE(button_svc,
    BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&char_uuid.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_write, NULL),
);

// Parameters for the dicovery of the characteristic handle, stored per connection.
static struct bt_gatt_discover_params disc_params[MAX_CONNECTIONS];

static uint8_t discover_cb(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params)
{
    if (!attr)
    {
        printk("Discovery complete\n");

        // TARGET nodes scan for their downstream neighbour after discovery,
        // ensuring the BLE controller is idle before starting a new scan.
        if (config->role == ROLE_TARGET && !network_formed)
        {
            printk("Triggering downstream scan\n");
            k_work_schedule(&scan_work, K_MSEC(500));
        }
        return BT_GATT_ITER_STOP;
    }

    for (short i = 0; i < num_connections; i++)
    {
        if (default_connections[i] == conn)
        {
            // attr->handle is the declaration handle; +1 is the value handle.
            char_handles[i] = attr->handle + 1;
            printk("conn[%d] char handle: %u\n", i, char_handles[i]);
            break;
        }
    }
    return BT_GATT_ITER_STOP;
}

static int discover_characteristic(struct bt_conn *conn)
{
    int idx = -1;
    for (short i = 0; i < num_connections; i++)
    {
        if (default_connections[i] == conn) { idx = i; break; }
    }
    if (idx < 0) return -ENOENT;

    disc_params[idx].uuid = &char_uuid.uuid;
    disc_params[idx].func = discover_cb;
    disc_params[idx].start_handle = BT_ATT_FIRST_ATTTRIBUTE_HANDLE;
    disc_params[idx].end_handle = BT_ATT_LAST_ATTTRIBUTE_HANDLE;
    disc_params[idx].type = BT_GATT_DISCOVER_CHARACTERISTIC;

    return bt_gatt_discover(conn, &disc_params[idx]);
}

static void connected(struct bt_conn *conn, uint8_t err)
{

    // If bt_conn_le_create failed, report the error.
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        return;
    }

    // Else, connection succeeded, save the connection reference...
    default_connections[num_connections] = bt_conn_ref(conn);
    char_handles[num_connections] = 0;
    num_connections++;

    printk("Connected\n");

    // ... and start service discovery.
    // Stop scanning and advertising once we have all the connections we need.
    // SOURCE only connects upstream (1 connection).
    // TARGET connects to one upstream and one downstream (2 connections).
    if (config->role == ROLE_SOURCE && num_connections >= 1)
    {
        bt_le_scan_stop();
        bt_le_adv_stop();
    }
    if (config->role == ROLE_TARGET && num_connections >= 2)
    {
        bt_le_scan_stop();
        bt_le_adv_stop();
        network_formed = true;
    }

    int disc_err = discover_characteristic(conn);
    // Close the connection if discovery failed, and restart scanning.
    if (disc_err)
    {
        printk("Discover failed, error (%d)\n", disc_err);
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    // Report the reason why the connection was closed.
    printk("Disconnected, reason (%d)\n", reason);

    // If the connection reference still exists, unreference it and set to NULL.
    for (short i = 0; i < num_connections; i++)
    {
        if (default_connections[i] == conn)
        {
            bt_conn_unref(default_connections[i]);
            default_connections[i] = NULL;
            break;
        }
    }
}

// Register connection and disconnection callbacks, they are the same for all roles, as we (hope to) send the write requests externally.
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
    bool *found = user_data;

    // If the advertising data is some UUID, check if it contains the desired service UUID.
    if (data->type == BT_DATA_UUID16_ALL || data->type == BT_DATA_UUID16_SOME)
    {
        // service_uuid.val is stored little-endian.
        for (int i = 0; i + 1 < data->data_len; i += 2)
        {
            // If the UUIDs match, set ctx->found to true and stop iterating.
            uint16_t uuid = sys_get_le16(&data->data[i]);
            if (uuid == service_uuid.val)
            {
                *found = true;
                return false; // Stop iterating
            }
        }
    }
    return true; // Continue iterating
}

static bool ad_parse(struct net_buf_simple *data)
{
    // Parse the advertising data and check if it contains the desired service UUID.
    bool found = false;
    bt_data_parse(data, ad_parse_cb, &found);
    return found;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad)
{
    // Convert the address to a string for printing.
    // SOURCE only needs one connection; ignore further advertisements.
    if (config->role == ROLE_SOURCE && num_connections >= 1)
    {
        return;
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    // Check if the advertised device has the temperature service UUID in its advertising data.
    if (!ad_parse(ad))
    {
        printk("Wrong service, ignoring device: %s\n", addr_str);
        return;
    }

    // Print out the device, but don't stop scanning.
    // Skip devices we are already connected to.
    for (short i = 0; i < num_connections; i++)
    {
        if (default_connections[i] == NULL) continue;
        const bt_addr_le_t *peer = bt_conn_get_dst(default_connections[i]);
        if (bt_addr_le_cmp(peer, addr) == 0)
        {
            printk("Already connected to %s, skipping\n", addr_str);
            return;
        }
    }

    printk("Device found: %s, RSSI %d\n", addr_str, rssi);

    // Stop scanning before connecting to avoid EAGAIN errors.
    int stop_err = bt_le_scan_stop();
    if (stop_err && stop_err != -EALREADY)
    {
        printk("Scan stop failed (%d)\n", stop_err);
        return;
    }

    // Try to initialize a connection with the device.
    // If the connection succeeds, the "connected" callback will be started.

    struct bt_conn *conn;
    int err = bt_conn_le_create(addr,
                                BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT, // Default connection interval, latency and timeout.
                                &conn);
    if (err)
    {
        printk("Connection failed, error (%d)\n", err);
        k_work_schedule(&scan_work, K_MSEC(500));
        return;
    }

    // Lastly, unreference the connection since we won't be using it in this callback.
    bt_conn_unref(conn);
}

static int start_scan(void)
{
    // Configure scan parameters: active scanning, no special options, fast interval and window.
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    // Start scanning and check for errors, when a device is found proceed to device_found callback.
    int err = bt_le_scan_start(&scan_param, device_found);
    if (err)
    {
        printk("Starting scanning failed, error (%d)\n", err);
        return err;
    }

    printk("Scanning started\n");
    return 0;
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0xACDC)),
};

static void start_advertise(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        printk("Advertising start failed (%d)\n", err);
        return;
    }
    printk("Advertising started\n");
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
    k_work_init_delayable(&scan_work, scan_work_handler);

    start_advertise();
    k_work_schedule(&scan_work, K_MSEC(config->node_id * 2000));
}

static void role_SOURCE(void)
{
    // 1. Initialize Bluetooth.
    // 2. Start advertising.
    // 3. Connect to all nodes in range.

    printk("Starting source node\n");
    led_init();

    // Initialize Bluetooth, and proceed to bt_ready.
    int err = bt_enable(bt_ready);
    if (err)
    {
        printk("bt_enable failed (err %d)\n", err);
    }

    printk("bt_enable called\n");

    // 4. Start sending data to all connected nodes at intervals determined by next_press_dt().
    button_thread(send_message_SOURCE);
}

static void role_TARGET(void)
{
    // 1. Initialize Bluetooth.
    // 2. Start advertising.
    // 3. Connect to all nodes in range.
    // 4. When a write request is received, call redirect_message_TARGET() with the received message.

    printk("Starting target node\n");
    led_init();

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

int main(void)
{
    configure();

    switch (config->role)
    {
    case ROLE_SOURCE:
        printk("Starting in SOURCE role\n");
        role_SOURCE();
        break;
    case ROLE_TARGET:
        printk("Starting in TARGET role\n");
        role_TARGET();
        break;
    default:
        return -1;
    }

    k_sleep(K_FOREVER);
    return 0;
}