#include "temperature.h"

void set_temperature_random(double *temperature)
{
    uint8_t base;
    sys_rand_get(&base, sizeof(base));

    *temperature = (base / 255.0) * 40.0 - 10.0;
}

void set_temperature_constant(double *temperature)
{
    *temperature = 25.0;
}

void set_temperature_random_walk(double *temperature)
{
    uint8_t base;
    sys_rand_get(&base, sizeof(base));
    
    // Random change of 1.0 degrees at most.
    *temperature += 2.0 * (base / 255) - 1.0;
}

// Calls method based on METHOD parameter.
void update_temperature(double *temperature)
{
#if METHOD == 0
    set_temperature_random(temperature);
#elif METHOD == 1
    set_temperature_constant(temperature);
#elif METHOD == 2
    set_temperature_random_walk(temperature);
#endif
}