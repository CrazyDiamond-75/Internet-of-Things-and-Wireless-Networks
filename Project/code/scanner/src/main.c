#include "main.h"

// Declare pointer to configuration at reserved address.
volatile boot_config_t *config = (volatile boot_config_t *)CONFIG_ADDR;

// Node ID, set during configuration.
static uint8_t node_id;

// Stores the current connection.
struct bt_conn *default_connection;

// Store whether the connection is ready for write requests, set after
// discovery.
static bool conn_ready = false;

// Stores the GATT characteristic handle, filled during discovery.
static uint16_t char_handle;

// Service and characteristic UUIDs, used to check if scanned devices are
// relevant and to discover the characteristic handle after connecting.
struct bt_uuid_16 service_uuid = BT_UUID_INIT_16(0xACDC); // Custom service UUID
struct bt_uuid_16 char_uuid =
    BT_UUID_INIT_16(0xDEAF); // Custom characteristic UUID

// Used for the timestamp.
static int64_t time_since_boot;

// KWork item associated with scan_work_handler.
static struct k_work_delayable scan_work;
static void scan_work_handler(struct k_work *work)
{
  int err = start_scan();
  if (err)
  {
    // Controller still busy, retry.
    k_work_schedule(&scan_work, K_MSEC(1000));
  }
}

static void send_message(message_t *msg)
{
  if (!default_connection)
    return;
  if (!char_handle)
    return;
  if (!conn_ready)
    return;

  // Todo: Maybe copy message to static variable if the asynchronous write
  // request gets overwritten before sending?

  int err = bt_gatt_write_without_response(default_connection, char_handle, msg,
                                           sizeof(message_t), false);

  if (err)
    printk("Write to central failed (%d)\n", err);
}

static void configure(void)
{
  if (config->magic != MAGIC_NUMBER)
  {
    printk("Invalid magic number: 0x%08X\n", config->magic);
    return;
  }
  node_id = config->node_id; // Copy the value to prevent corruption.
}

// Parameters for the dicovery of the characteristic handle.
static struct bt_gatt_discover_params disc_params;
static uint8_t discover_cb(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params)
{
  if (!attr)
  {
    printk("Discovery complete\n");

    // Mark connection ready for writes after full discovery.
    conn_ready = true;
    return BT_GATT_ITER_STOP;
  }

  if (default_connection == conn)
  {
    // attr-user_data contains a pointer to the characteristic metadata,
    // including its handle.
    const struct bt_gatt_chrc *gatt_chrc = attr->user_data;

    char_handle = gatt_chrc->value_handle;

    printk("Central char handle: %u\n", char_handle);
  }
  return BT_GATT_ITER_CONTINUE;
}

static int discover_characteristic(struct bt_conn *conn)
{
  disc_params.uuid = &char_uuid.uuid;
  disc_params.func = discover_cb;
  disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
  disc_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
  disc_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

  return bt_gatt_discover(conn, &disc_params);
}

static void connected(struct bt_conn *conn, uint8_t err)
{

  // If bt_conn_le_create failed, report the error.
  if (err)
  {
    printk("Connection failed, error (%d)\n", err);
    return;
  }

  // Else, connection succeeded, reset parameters (if they're not resetted) and
  // save the connection reference.
  conn_ready = false;
  default_connection = bt_conn_ref(conn);
  char_handle = 0;

  printk("Connected\n");

  // Start service discovery.

  int disc_err = discover_characteristic(conn);
  // Close the connection if discovery failed.
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
  bt_conn_unref(default_connection);
  default_connection = NULL;
  conn_ready = false;
}

// Register connection and disconnection callbacks, they are the same for all
// roles, as we (hope to) send the write requests externally.
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
  bool *found = user_data;

  // If the advertising data is some UUID, check if it contains the desired
  // service UUID.
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
  // Parse the advertising data and check if it contains the desired service
  // UUID.
  bool found = false;
  bt_data_parse(data, ad_parse_cb, &found);
  return found;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                         struct net_buf_simple *ad_foreign)
{
  // Ignore scan response reports.
  // if (type == BT_GAP_ADV_TYPE_SCAN_RSP)
  // {
  //     return;
  // }

  // Convert the address to a string for debugging.
  char addr_str[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

  // Check if the advertised device has the right service UUID in its
  // advertising data and that we're not connected.
  if (!conn_ready && ad_parse(ad_foreign))
  {
    printk("Central found: %s, RSSI %d\n", addr_str, rssi);
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
    int err = bt_conn_le_create(
        addr, BT_CONN_LE_CREATE_CONN,
        BT_LE_CONN_PARAM_DEFAULT, // Default connection interval, latency and
                                  // timeout.
        &conn);
    if (err)
    {
      printk("Connection failed, error (%d)\n", err);
      k_work_schedule(&scan_work, K_MSEC(500));
      return;
    }

    // Lastly, unreference the connection since we won't be using it in this
    // callback.
    bt_conn_unref(conn);
    return;
  }
  else if (conn_ready)
  {
    // If the service doesn't have the right UUID, but we're already connected
    // to the central, redirect the relevant data to the central.

    printk("Foreign device found: %s, RSSI %d\n", addr_str, rssi);

    message_t msg = {
        .nodeID = node_id,
        .timestamp = k_uptime_get() - time_since_boot,
        .rssi = rssi,
        .sender = *addr,
    };
    send_message(&msg);
  }
}

static int start_scan(void)
{
  // Configure scan parameters: passive scanning, no special options, fast
  // interval and window.
  struct bt_le_scan_param scan_param = {
      .type = BT_LE_SCAN_TYPE_PASSIVE,
      .options = BT_LE_SCAN_OPT_NONE,
      .interval = BT_GAP_SCAN_FAST_INTERVAL,
      .window = BT_GAP_SCAN_FAST_WINDOW,
  };

  // Start scanning and check for errors, when a device is found proceed to
  // device_found callback.
  int err = bt_le_scan_start(&scan_param, device_found);
  if (err)
  {
    printk("Starting scanning failed, error (%d)\n", err);
    return err;
  }

  printk("Scanning started\n");
  return 0;
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

  // Setup KWork item for scan retries and scan triggering after discovery.
  k_work_init_delayable(&scan_work, scan_work_handler);

  // Start scanning.
  k_work_schedule(&scan_work, K_MSEC(500));
}

int main(void)
{
  // Read node ID from static address.
  configure();

  printk("Starting scanner node.\n");

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

  return 0;
}
