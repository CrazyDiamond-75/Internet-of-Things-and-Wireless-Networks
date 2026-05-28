#include "temperature_humidity.h"

ALWAYS_INLINE int16_t rand_temperature(void)
{
    return (int16_t)(sys_rand32_get() % 2251) - 250;
}

ALWAYS_INLINE uint16_t rand_humidity(void)
{
    return (uint16_t)(sys_rand32_get() % 1001);
}