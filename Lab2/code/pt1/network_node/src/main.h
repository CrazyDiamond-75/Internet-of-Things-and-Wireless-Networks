#include <zephyr.h>
#include <zephyr/types.h>

#include <stddef.h>
#include <stdint.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/byteorder.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <drivers/gpio.h>
#include <bluetooth/conn.h>

#include <button.h>

// Struct used to pass external information on boot to the main thread.
typedef struct
{
    uint32_t magic;
    uint32_t role;
    uint32_t node_id;
} boot_config_t;

// Corresponds to boot_config.role.
enum
{
    ROLE_SOURCE = 0,
    ROLE_TARGET = 1
};

// Reserved address for role configuration.
#define CONFIG_ADDR 0x2003F000

// Magic number to check if boot_config is valid.
#define MAGIC_NUMBER 0xDEADBEEF

/* Reads configuration form CONFIG_ADDR and sets role and node_id accordingly. */
static void configure();

/* Button events */
#define PRESSED 0xFF
#define RELEASED 0x00

/* Messages we want to send contain a button event (PRESSED or RELEASED) and a timestamp, which is used if a received message is recent enough. */
typedef struct
{
    uint32_t timestamp;
    uint8_t event;
} message_t;

/* Methods which execute the wanted role. */
static void role_SOURCE();
static void role_TARGET();

static void send_message_SOURCE();