#include <stdint.h>
#include <button.h>

// Struct used to pass external information on boot to the main thread.
struct boot_config
{
    uint32_t magic;
    uint32_t role;
    uint32_t node_id;
};

// Corresponds to boot_config.role.
enum
{
    ROLE_SOURCE = 0,
    ROLE_PATH = 1,
    ROLE_TARGET = 2,
};

// Reserved address for role configuration.
#define CONFIG_ADDR 0x2003F000
volatile struct boot_config *config = (volatile struct boot_config *)CONFIG_ADDR;

// Magic number to check if boot_config is valid.
#define MAGIC_NUMBER 0xDEADBEEF

/* Reads configuration form CONFIG_ADDR and sets role and node_id accordingly. */
static void configurate();

/* Methods which execute the wanted role. */
static void role_source();
static void role_path();
static void role_target();