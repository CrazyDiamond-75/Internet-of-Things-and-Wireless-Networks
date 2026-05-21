#include "main.h"

// Declare pointer to configuration at reserved address.
volatile boot_config_t *config = (volatile boot_config_t *)CONFIG_ADDR;

// Stores the current connections, restrict to 8 for "IoT performance reasons".
struct bt_conn *default_connections[8];
short num_connections = 0;

struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0xACDC); // Custom service UUID
struct bt_uuid_16 char_uuid = BT_UUID_INIT_16(0xDEAF);    // Custom characteristic UUID

static void configure()
{
    // If the magic number is not correct, print an error and halt.
    if (config->magic != MAGIC_NUMBER)
    {
        printk("Invalid magic number: 0x%08X\n", config->magic);
        return;
    }

    // Else, read role and node_id from config and print them.
    printk("Configuration: role=%u node_id=%u\n", config->role, config->node_id);
}

int main()
{
    // Read configuration from CONFIG_ADDR and set role and node_id accordingly.
    configure();

    // Start the wanted role based on the configuration.
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

    // Start sleeping until the end of time.
    k_sleep(K_FOREVER);

    return 0;
}

static void redirect_message_TARGET(message_t message)
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
    }
    else if (currEvent == RELEASED)
    {
        printk("Button released at timestamp %u\n", lastTimestamp);
    }
    else
    {
        printk("Malformed button event: 0x%02X\n", currEvent);
    }
}

static void send_message_SOURCE()
{
    static uint32_t timestamp = 0;
    static uint8_t event = PRESSED; // The button is pressed, when the first message should be sent.

    message_t message = {
        timestamp,
        event,
    };

    // Send message to all connected nodes.
    // ...
}

static int discover_characteristic(struct bt_conn *conn) {
    // Placeholder, will implement later.
    return 0;
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
    num_connections++;

    printk("Connected\n");

    // ... and start service discovery.
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
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    // Check if the advertised device has the temperature service UUID in its advertising data.
    if (!ad_parse(ad))
    {
        printk("Wrong service, ignoring device: %s\n", addr_str);
        return;
    }

    // Print out the device, but don't stop scanning.
    printk("Device found: %s, RSSI %d\n", addr_str, rssi);

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
        return;
    }

    // Lastly, unreference the connection since we won't be using it in this callback.
    bt_conn_unref(conn);
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

static void role_SOURCE()
{
    // 1. Initialize Bluetooth.
    // 2. Start advertising.
    // 3. Connect to all nodes in range.

    printk("Starting source node\n");

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

static void role_TARGET()
{
    // 1. Initialize Bluetooth.
    // 2. Start advertising.
    // 3. Connect to all nodes in range.
    // 4. When a write request is received, call redirect_message_TARGET() with the received message.

    printk("Starting target node\n");

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