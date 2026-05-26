#include <zephyr.h>
#include <random/rand32.h> // Took me a while to find this file...

/* Compiler parameter to select the method for updating the temperature.
    0: Random temperature
    1: Constant temperature
    2: Random walk temperature */
#define METHOD_TEMP 0

/* ... */
#define METHOD_HUMI 0

/* Sets a random internal temperature. */
void set_temperature_random(uint8_t *temperature);

/* Sets a constant internal temperature. */
void set_temperature_constant(uint8_t *temperature);

/* Sets a random walk internal temperature. */
void set_temperature_random_walk(uint8_t *temperature);

/* Updates temperature based on METHOD parameter. */
void update_temperature(uint8_t *temperature);

/* Converts the uint8_t representation into a classic fixed-point number with one decimal place.
   To print, use printf("%d.%d\n", v / 10, abs(v % 10)); */
ALWAYS_INLINE int16_t human_readable_temp(uint8_t temperature);

/* ... */
void set_humidity_random(uint8_t *humidity);
void set_humidity_constant(uint8_t *humidity);
void set_humidity_random_walk(uint8_t *humidity);
void update_humidity(uint8_t *humidity);

ALWAYS_INLINE int16_t human_readable_humi(uint8_t humidity);