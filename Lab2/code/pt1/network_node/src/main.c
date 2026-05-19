#include "main.h"

static void configurate()
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
    configurate();

    // Start the wanted role based on the configuration.
    switch (config->role)
    {
    case ROLE_SOURCE:
        printk("Starting in SOURCE role\n");
        role_source();
        break;
    case ROLE_PATH:
        printk("Starting in PATH role\n");
        role_path();
        break;
    case ROLE_TARGET:
        printk("Starting in TARGET role\n");
        role_target();
        break;
    default:
        return -1;
    }

    // Start sleeping until the end of time.
    k_sleep(K_FOREVER);

    return 0;
}

// Replace later.
void placeholder() {
    printk("Button pressed\n");
}
static void role_source() {

    button_thread(placeholder);
}
static void role_path() {}
static void role_target() {}