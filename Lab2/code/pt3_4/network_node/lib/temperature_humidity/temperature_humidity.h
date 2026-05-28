#include <zephyr.h>
#include <random/rand32.h>

/* Fixed-point random temperature: -250..2000 (= -25.0°C to 200.0°C) */
int16_t rand_temperature(void);

/* Fixed-point random humidity: 0..1000 (= 0.0% to 100.0%) */
uint16_t rand_humidity(void);