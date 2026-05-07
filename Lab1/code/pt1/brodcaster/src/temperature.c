#include "temperature.h"

void set_temperature_random(double *temperature)
{
    // Generate a random unsigned byte.
    uint8_t base;
    sys_rand_get(&base, sizeof(base));

    // Random temperature between -10.0 and 30.0 degrees Celsius.
    // (base / 255.0) gives a value between 0.0 and 1.0, which we then scale to the desired range.
    *temperature = (base / 255.0) * 40.0 - 10.0;
}

void set_temperature_constant(double *temperature)
{
    *temperature = 25.0;
}

void set_temperature_random_walk(double *temperature)
{
    // ...
    uint8_t base;
    sys_rand_get(&base, sizeof(base));
    
    // Random change of 1.0 degrees at most.
    *temperature += 2.0 * (base / 255.0) - 1.0;
}

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