#include "temperature_humidity.h"

void set_temperature_random(uint8_t *temperature)
{
    // Generate a random unsigned byte.
    uint8_t base;
    sys_rand_get(&base, sizeof(base));
    return base;
}

void set_temperature_constant(uint8_t *temperature)
{
    // Set to 200°C
    *temperature = 255;
}

void set_temperature_random_walk(uint8_t *temperature)
{
    // ...
    uint8_t base;
    sys_rand_get(&base, sizeof(base));

    // Random change of at most about 2 degrees.
    *temperature += (base - 127) / 64;
}

void update_temperature(uint8_t *temperature)
{
#if METHOD_TEMP == 0
    set_temperature_random(temperature);
#elif METHOD_TEMP == 1
    set_temperature_constant(temperature);
#elif METHOD_TEMP == 2
    set_temperature_random_walk(temperature);
#endif
}

ALWAYS_INLINE int16_t human_readable_temp(uint8_t temperature)
{
    // Scale by 10 for one decimal.
    return -250 + ((temperature * 2250 + 127) / 255);
}

void set_humidity_random(uint8_t *humidity)
{
    set_temperature_random(humidity);
}
void set_humidity_constant(uint8_t *humidity)
{
    // Set to directly above 50%.
    *humidity = 128;
}
void set_humidity_random_walk(uint8_t *humidity)
{
    // Changes by at most 0.7%
    set_temperature_random_walk(humidity);
}
void update_humidity(uint8_t *humidity)
{
#if METHOD_HUMI == 0
    set_humidity_random(humidity);
#elif METHOD_HUMI == 1
    set_humidity_constant(humidity);
#elif METHOD_HUMI == 2
    set_humidity_random_walk(humidity);
#endif
}

ALWAYS_INLINE int16_t human_readable_humi(uint8_t humidity)
{
    return (humidity * 1000 + 127) / 255;
}